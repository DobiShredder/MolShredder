#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include "molshredder/io/trajectory_reader.hpp"
#include "molshredder/io/trajectory_writer.hpp"

namespace {

void write_u32(std::ostream& output, std::uint32_t value) {
  const std::array<char, 4> bytes{
      static_cast<char>((value >> 24U) & 0xffU),
      static_cast<char>((value >> 16U) & 0xffU),
      static_cast<char>((value >> 8U) & 0xffU),
      static_cast<char>(value & 0xffU)};
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_i32(std::ostream& output, std::int32_t value) {
  write_u32(output, std::bit_cast<std::uint32_t>(value));
}

void write_f32(std::ostream& output, float value) {
  write_u32(output, std::bit_cast<std::uint32_t>(value));
}

void write_f64(std::ostream& output, double value) {
  const auto bits = std::bit_cast<std::uint64_t>(value);
  write_u32(output, static_cast<std::uint32_t>(bits >> 32U));
  write_u32(output, static_cast<std::uint32_t>(bits & 0xffffffffU));
}

struct FrameSpec {
  bool use_double{};
  std::int32_t step{};
  double time{};
  double lambda{};
  std::vector<std::array<double, 3>> positions;
  std::vector<std::array<double, 3>> velocities;
  std::vector<std::array<double, 3>> forces;
  std::optional<std::array<double, 9>> box;
  bool legacy_block{};
};

void write_reals(std::ostream& output, const std::vector<double>& values,
                 bool use_double) {
  for (const auto value : values) {
    if (use_double) {
      write_f64(output, value);
    } else {
      write_f32(output, static_cast<float>(value));
    }
  }
}

void write_frame(std::ostream& output, const FrameSpec& frame) {
  const auto real_size = frame.use_double ? 8U : 4U;
  const auto vector_size = static_cast<std::uint32_t>(
      frame.positions.size() * 3U * real_size);
  write_i32(output, 1993);
  write_i32(output, 13);
  write_i32(output, 12);
  output.write("GMX_trn_file", 12);
  const std::array<std::uint32_t, 10> sizes{
      frame.legacy_block ? 4U : 0U,
      0U,
      frame.box.has_value() ? static_cast<std::uint32_t>(9U * real_size) : 0U,
      0U,
      0U,
      0U,
      0U,
      vector_size,
      frame.velocities.empty() ? 0U : vector_size,
      frame.forces.empty() ? 0U : vector_size};
  for (const auto size : sizes) write_i32(output, static_cast<std::int32_t>(size));
  write_i32(output, static_cast<std::int32_t>(frame.positions.size()));
  write_i32(output, frame.step);
  write_i32(output, 0);
  if (frame.use_double) {
    write_f64(output, frame.time);
    write_f64(output, frame.lambda);
  } else {
    write_f32(output, static_cast<float>(frame.time));
    write_f32(output, static_cast<float>(frame.lambda));
  }
  if (frame.legacy_block) write_u32(output, 0U);
  if (frame.box.has_value()) {
    write_reals(output, std::vector<double>{frame.box->begin(), frame.box->end()},
                frame.use_double);
  }
  const auto write_vectors = [&](const auto& vectors) {
    std::vector<double> flattened;
    flattened.reserve(vectors.size() * 3U);
    for (const auto& vector : vectors) {
      flattened.insert(flattened.end(), vector.begin(), vector.end());
    }
    write_reals(output, flattened, frame.use_double);
  };
  write_vectors(frame.positions);
  write_vectors(frame.velocities);
  write_vectors(frame.forces);
}

bool near(double left, double right, double tolerance = 1.0e-5) {
  return std::abs(left - right) <= tolerance;
}

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace molshredder;
  if (argc != 2) return 2;
  const auto directory = std::filesystem::path{argv[1]};
  const auto float_path = directory / "synthetic_float.trr";
  const auto double_path = directory / "synthetic_double.trr";
  const auto mixed_path = directory / "synthetic_mixed.trr";
  const auto legacy_path = directory / "synthetic_legacy.trr";
  const auto infinite_path = directory / "synthetic_infinite.trr";
  const auto truncated_path = directory / "synthetic_truncated.trr";

  const std::array<double, 9> box{2.0, 0.0, 0.0, 0.2, 3.0,
                                  0.0, 0.1, 0.3, 4.0};
  const FrameSpec first{false,
                        10,
                        1.5,
                        0.25,
                        {{{0.1, 0.2, 0.3}}, {{1.0, 1.1, 1.2}}},
                        {{{0.01, 0.02, 0.03}}, {{0.1, 0.2, 0.3}}},
                        {{{10.0, 20.0, 30.0}}, {{40.0, 50.0, 60.0}}},
                        box,
                        false};
  const FrameSpec second{false,
                         20,
                         2.5,
                         0.5,
                         {{{2.0, 2.1, 2.2}}, {{3.0, 3.1, 3.2}}},
                         {{{0.4, 0.5, 0.6}}, {{0.7, 0.8, 0.9}}},
                         {{{70.0, 80.0, 90.0}}, {{100.0, 110.0, 120.0}}},
                         box,
                         false};
  const FrameSpec double_frame{true,
                               30,
                               3.125,
                               0.75,
                               {{{0.123456789, 0.25, 0.5}}},
                               {},
                               {},
                               std::nullopt,
                               false};
  {
    std::ofstream output{float_path, std::ios::binary};
    write_frame(output, first);
    write_frame(output, second);
  }
  {
    std::ofstream output{double_path, std::ios::binary};
    write_frame(output, double_frame);
  }
  {
    std::ofstream output{mixed_path, std::ios::binary};
    write_frame(output, first);
    auto double_two_atoms = second;
    double_two_atoms.use_double = true;
    write_frame(output, double_two_atoms);
  }
  {
    std::ofstream output{legacy_path, std::ios::binary};
    auto legacy = first;
    legacy.legacy_block = true;
    write_frame(output, legacy);
  }
  {
    std::ofstream output{infinite_path, std::ios::binary};
    auto infinite = double_frame;
    infinite.box = std::array<double, 9>{};
    write_frame(output, infinite);
  }
  {
    std::ofstream output{truncated_path, std::ios::binary};
    write_i32(output, 1993);
  }

  bool passed = true;
  const auto opened = io::open_trr(float_path, 2U);
  passed &= expect(opened.has_value(), "float TRR must open");
  if (!opened.has_value()) return 1;
  const auto& metadata = opened.value()->metadata();
  passed &= expect(metadata.atom_count == 2U && metadata.frame_count == 2U &&
                       metadata.has_unit_cell && metadata.has_velocities &&
                       metadata.has_forces &&
                       metadata.precision == io::TrrPrecision::float32,
                   "TRR aggregate metadata must preserve channels/precision");
  const auto frame = opened.value()->read_frame(1U);
  passed &= expect(frame.has_value(), "indexed second TRR frame must decode");
  if (frame.has_value()) {
    const auto& values =
        std::get<std::vector<model::Vec3f>>(frame.value()->positions().values());
    passed &= expect(values.size() == 2U && near(values[0].x, 20.0) &&
                         near(values[1].z, 32.0),
                     "TRR nm positions must convert to angstrom");
    const auto& velocities = std::get<std::vector<model::Vec3f>>(
        frame.value()->velocities()->values());
    passed &= expect(near(velocities[0].x, 4.0) &&
                         frame.value()->metadata().velocity_time_unit ==
                             model::TimeUnit::picosecond,
                     "TRR nm/ps velocities must convert to angstrom/ps");
    const auto& cell = frame.value()->metadata().unit_cell.value();
    passed &= expect(near(cell.a.x, 20.0) && near(cell.b.x, 2.0) &&
                         near(cell.c.z, 40.0),
                     "TRR triclinic box vectors must convert to angstrom");
    passed &= expect(frame.value()->metadata().source_step == 20U &&
                         near(frame.value()->metadata().physical_time->value, 2.5),
                     "TRR step and picosecond time must be retained");
    const auto force = frame.value()->metadata().atom_properties.find("force_z");
    passed &= expect(force != frame.value()->metadata().atom_properties.end() &&
                         near(std::get<std::vector<float>>(force->second.values)[1],
                              12.0) &&
                         force->second.metadata.unit ==
                             "kJ mol^-1 angstrom^-1",
                     "TRR force must convert from per-nm to per-angstrom");

    io::TrajectoryWriteOptions write_options;
    write_options.format = io::TrajectoryFormat::trr;
    operation::TaskContext write_context;
    const auto serialized = io::serialize_trajectory_frame(
        *frame.value(), write_options, write_context);
    passed &= expect(
        serialized.has_value() && serialized.value().report.has_time &&
            serialized.value().report.has_velocities &&
            serialized.value().report.has_forces &&
            serialized.value().report.has_unit_cell &&
            serialized.value().report.precision == io::TrrPrecision::float32,
        "TRR writer must preserve float32 trajectory channels");
    const auto roundtrip_path = directory / "roundtrip.trr";
    std::error_code ignored;
    std::filesystem::remove(roundtrip_path, ignored);
    const auto written = io::write_trajectory_frame_file(
        roundtrip_path, *frame.value(), write_options, false, write_context);
    const auto copy = io::open_trr(roundtrip_path, 2U);
    passed &= expect(written.has_value() && copy.has_value(),
                     "atomic TRR output must read back");
    if (copy.has_value()) {
      const auto copied = copy.value()->read_frame(0U);
      const auto &copied_positions = std::get<std::vector<model::Vec3f>>(
          copied.value()->positions().values());
      const auto &copied_velocities = std::get<std::vector<model::Vec3f>>(
          copied.value()->velocities()->values());
      const auto copied_force =
          copied.value()->metadata().atom_properties.find("force_z");
      passed &= expect(
          near(copied_positions[1].z, 32.0) &&
              near(copied_velocities[0].x, 4.0) &&
              near(std::get<std::vector<float>>(copied_force->second.values)[1],
                   12.0) &&
              copied.value()->metadata().source_step == 20U &&
              near(copied.value()->metadata().physical_time->value, 2.5) &&
              copied.value()->metadata().fields.at("trr.lambda").find("0.500") !=
                  std::string::npos,
          "TRR read-write-read must preserve coordinates, velocity, force, "
          "step, time and lambda");
    }
    const auto collision = io::write_trajectory_frame_file(
        roundtrip_path, *frame.value(), write_options, false, write_context);
    passed &= expect(!collision.has_value(),
                     "TRR writer must reject implicit overwrite");
    operation::TaskContext cancelled_context;
    cancelled_context.cancellation.request_cancel();
    const auto cancelled_path = directory / "cancelled.trr";
    std::filesystem::remove(cancelled_path, ignored);
    const auto cancelled = io::write_trajectory_frame_file(
        cancelled_path, *frame.value(), write_options, false,
        cancelled_context);
    passed &= expect(!cancelled.has_value() &&
                         cancelled.error().code ==
                             operation::ErrorCode::cancelled &&
                         !std::filesystem::exists(cancelled_path),
                     "cancelled TRR export must not publish a partial file");
  }
  passed &= expect(!opened.value()->read_frame(2U).has_value(),
                   "out-of-range TRR frame must fail");

  const auto double_source = io::open_trr(double_path).value();
  const auto precise = double_source->read_frame(0U).value();
  const auto& precise_values =
      std::get<std::vector<model::Vec3d>>(precise->positions().values());
  passed &= expect(double_source->metadata().precision == io::TrrPrecision::float64 &&
                       near(precise_values[0].x, 1.23456789, 1.0e-10) &&
                       !precise->metadata().unit_cell.has_value() &&
                       !precise->velocities().has_value(),
                   "double TRR precision and absent optional channels must survive");
  {
    io::TrajectoryWriteOptions options;
    options.format = io::TrajectoryFormat::trr;
    operation::TaskContext context;
    const auto output = directory / "roundtrip_double.trr";
    std::error_code ignored;
    std::filesystem::remove(output, ignored);
    const auto written = io::write_trajectory_frame_file(
        output, *precise, options, false, context);
    const auto copy = io::open_trr(output).value()->read_frame(0U).value();
    const auto &copy_values =
        std::get<std::vector<model::Vec3d>>(copy->positions().values());
    passed &= expect(written.has_value() &&
                         written.value().precision == io::TrrPrecision::float64 &&
                         near(copy_values[0].x, 1.23456789, 1.0e-10),
                     "TRR writer must retain float64 precision");
  }
  passed &= expect(io::open_trr(mixed_path).value()->metadata().precision ==
                       io::TrrPrecision::mixed,
                   "mixed per-frame precision must be reported");
  passed &= expect(!io::open_trr(infinite_path)
                        .value()
                        ->read_frame(0U)
                        .value()
                        ->metadata()
                        .unit_cell.has_value(),
                   "an all-zero TRR box must represent non-periodic boundaries");
  passed &= expect(!io::open_trr(float_path, 3U).has_value() &&
                       !io::open_trr(legacy_path).has_value() &&
                       !io::open_trr(truncated_path).has_value() &&
                       !io::open_trr(directory / "missing.trr").has_value(),
                   "topology mismatch, legacy, truncated and missing TRR must fail");

  model::FrameMetadata incomplete_metadata;
  incomplete_metadata.source_step = 1U;
  incomplete_metadata.physical_time =
      model::PhysicalTime{0.0, model::TimeUnit::picosecond};
  incomplete_metadata.fields.emplace("trr.lambda", "0");
  model::PropertyMetadata force_metadata;
  force_metadata.unit = "kJ mol^-1 angstrom^-1";
  incomplete_metadata.atom_properties.emplace(
      "force_x", model::AtomProperty{std::vector<float>{1.0F},
                                      std::move(force_metadata)});
  const auto incomplete = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::vector<model::Vec3f>(1U)}, std::nullopt, {},
      std::move(incomplete_metadata));
  io::TrajectoryWriteOptions strict_options;
  strict_options.format = io::TrajectoryFormat::trr;
  operation::TaskContext strict_context;
  passed &= expect(
      !io::serialize_trajectory_frame(*incomplete.value(), strict_options,
                                      strict_context)
           .has_value(),
      "TRR writer must reject incomplete force triplets");
  const auto untyped = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::vector<model::Vec3f>(1U)});
  passed &= expect(
      !io::serialize_trajectory_frame(*untyped.value(), strict_options,
                                      strict_context)
           .has_value(),
      "TRR writer must not invent step, time or lambda metadata");

  return passed ? 0 : 1;
}
