#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <variant>

#include "molshredder/io/trajectory_reader.hpp"
#include "molshredder/io/trajectory_writer.hpp"
#include "molshredder/model/coordinates.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  if (argc != 3) {
    std::cerr << "expected RST7 fixture and build directory\n";
    return 2;
  }
  bool passed = true;
  io::AmberRestartMetadata metadata;
  const auto source = io::open_amber_restart(argv[1], 4U, &metadata);
  passed &= expect(
      source.has_value() && source.value()->atom_count() == 4U &&
          source.value()->frame_count() == 1U && metadata.has_time &&
          metadata.has_temperature && metadata.has_velocities &&
          metadata.has_unit_cell,
      "RST7 must expose one coordinate/velocity/time/temperature/box frame");
  if (source.has_value()) {
    const auto frame = source.value()->read_frame(0U);
    passed &= expect(frame.has_value(), "RST7 frame zero must decode");
    if (frame.has_value()) {
      const auto &positions = std::get<std::vector<model::Vec3d>>(
          frame.value()->positions().values());
      const auto &velocities = std::get<std::vector<model::Vec3d>>(
          frame.value()->velocities()->values());
      passed &= expect(
          positions[3].z == 11.0 &&
              std::abs(velocities[0].x - 2.0455) < 1.0e-12 &&
              frame.value()->metadata().physical_time.has_value() &&
              frame.value()->metadata().physical_time->value == 1.25 &&
              frame.value()->metadata().unit_cell.has_value() &&
              frame.value()->metadata().unit_cell->signed_volume() > 7000.0 &&
              frame.value()->metadata().fields.at("temperature_unit") ==
                  "kelvin",
          "RST7 values, AKMA velocity scale and triclinic cell must be typed");

      io::TrajectoryWriteOptions options;
      options.format = io::TrajectoryFormat::rst7;
      options.title = "MolShredder round-trip";
      operation::TaskContext context;
      const auto serialized =
          io::serialize_trajectory_frame(*frame.value(), options, context);
      passed &= expect(
          serialized.has_value() && serialized.value().report.has_time &&
              serialized.value().report.has_temperature &&
              serialized.value().report.has_velocities &&
              serialized.value().report.has_unit_cell &&
              serialized.value().content.find("    4 1.2500000E+00 ") !=
                  std::string::npos,
          "RST7 writer must preserve typed optional restart channels");
      if (serialized.has_value()) {
        const auto output =
            std::filesystem::path{argv[2]} / "roundtrip.rst7";
        std::error_code ignored;
        std::filesystem::remove(output, ignored);
        const auto written = io::write_trajectory_frame_file(
            output, *frame.value(), options, false, context);
        const auto copy = io::open_amber_restart(output, 4U);
        passed &= expect(written.has_value() && copy.has_value(),
                         "atomic RST7 output must read back");
        if (copy.has_value()) {
          const auto copy_frame = copy.value()->read_frame(0U);
          const auto &copy_positions = std::get<std::vector<model::Vec3d>>(
              copy_frame.value()->positions().values());
          const auto &copy_velocities = std::get<std::vector<model::Vec3d>>(
              copy_frame.value()->velocities()->values());
          passed &= expect(
              std::abs(copy_positions[3].z - positions[3].z) < 1.0e-7 &&
                  std::abs(copy_velocities[0].x - velocities[0].x) < 3.0e-6 &&
                  copy_frame.value()->metadata().physical_time->value == 1.25 &&
                  copy_frame.value()->metadata().unit_cell.has_value(),
              "RST7 read-write-read must preserve coordinate, velocity, time "
              "and cell semantics");
        }
        const auto collision = io::write_trajectory_frame_file(
            output, *frame.value(), options, false, context);
        passed &= expect(!collision.has_value() &&
                             collision.error().code ==
                                 operation::ErrorCode::invalid_argument,
                         "RST7 export must reject implicit overwrite");
        const auto replacement = io::write_trajectory_frame_file(
            output, *frame.value(), options, true, context);
        passed &= expect(replacement.has_value(),
                         "RST7 explicit overwrite must replace target");
        operation::TaskContext cancelled_context;
        cancelled_context.cancellation.request_cancel();
        const auto cancelled_output =
            std::filesystem::path{argv[2]} / "cancelled.rst7";
        std::filesystem::remove(cancelled_output, ignored);
        const auto cancelled = io::write_trajectory_frame_file(
            cancelled_output, *frame.value(), options, false,
            cancelled_context);
        passed &= expect(
            !cancelled.has_value() &&
                cancelled.error().code == operation::ErrorCode::cancelled &&
                !std::filesystem::exists(cancelled_output),
            "cancelled RST7 export must not publish a partial target");
      }
    }
  }

  model::FrameMetadata temperature_only_metadata;
  temperature_only_metadata.fields.emplace("temperature", "300");
  temperature_only_metadata.fields.emplace("temperature_unit", "kelvin");
  const auto temperature_only = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::vector<model::Vec3d>(3U)}, std::nullopt, {},
      std::move(temperature_only_metadata));
  operation::TaskContext strict_context;
  io::TrajectoryWriteOptions strict_options;
  strict_options.format = io::TrajectoryFormat::rst7;
  const auto missing_time = io::serialize_trajectory_frame(
      *temperature_only.value(), strict_options, strict_context);
  passed &= expect(
      !missing_time.has_value() &&
          missing_time.error().message.find("temperature without") !=
              std::string::npos,
      "RST7 writer must not invent time to encode temperature");

  const auto coordinates_only = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::vector<model::Vec3d>(3U)});
  const auto no_optional = io::serialize_trajectory_frame(
      *coordinates_only.value(), strict_options, strict_context);
  passed &= expect(
      no_optional.has_value() && !no_optional.value().report.has_time &&
          !no_optional.value().report.has_temperature &&
          !no_optional.value().report.has_velocities &&
          !no_optional.value().report.has_unit_cell,
      "RST7 writer must omit absent optional channels instead of zero-fill");

  model::FrameMetadata small_metadata;
  small_metadata.velocity_time_unit = model::TimeUnit::picosecond;
  const auto small = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::vector<model::Vec3d>(1U)},
      model::CoordinateBuffer{std::vector<model::Vec3d>(1U)}, {},
      std::move(small_metadata));
  const auto ambiguous = io::serialize_trajectory_frame(
      *small.value(), strict_options, strict_context);
  passed &= expect(
      !ambiguous.has_value() &&
          ambiguous.error().message.find("ambiguous") != std::string::npos,
      "RST7 writer must reject small-system optional-block ambiguity");
  const auto mismatch =
      io::open_trajectory(argv[1], io::TrajectoryFormat::rst7, 5U);
  passed &= expect(!mismatch.has_value() &&
                       mismatch.error().message.find("does not match") !=
                           std::string::npos,
                   "RST7 attachment must reject topology atom-count mismatch");

  const auto malformed_path =
      std::filesystem::path{argv[2]} / "malformed_optional.rst7";
  {
    std::ofstream output{malformed_path};
    output << "bad optional block\n    4\n"
              "   0.0000000   0.0000000   0.0000000   1.0000000   1.0000000   "
              "1.0000000\n"
              "   2.0000000   2.0000000   2.0000000   3.0000000   3.0000000   "
              "3.0000000\n"
              "   1.0000000\n";
  }
  const auto malformed = io::open_amber_restart(malformed_path, 4U);
  passed &= expect(
      !malformed.has_value() &&
          malformed.error().message.find("trailing block") != std::string::npos,
      "RST7 incomplete optional blocks must fail deterministically");
  std::error_code ignored;
  std::filesystem::remove(malformed_path, ignored);
  return passed ? 0 : 1;
}
