#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/io/volume_reader.hpp"
#include "molshredder/io/volume_writer.hpp"
#include "molshredder/model/volume.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

bool near(double left, double right, double tolerance = 1.0e-12) {
  return std::abs(left - right) <= tolerance;
}

bool has_loss(const std::vector<molshredder::io::VolumeFormatLoss> &losses,
              std::string_view channel) {
  for (const auto &loss : losses) {
    if (loss.channel == channel)
      return true;
  }
  return false;
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  bool passed = true;
  passed &= expect(argc == 3,
                   "OpenDX writer test requires fixture and output directory");
  if (argc != 3)
    return 1;

  const auto input = io::read_volume_file(argv[1]);
  passed &= expect(input.has_value(), "OpenDX writer fixture must load");
  if (!input.has_value())
    return 1;
  const auto &grid = *input.value().volumes.front().grid;
  operation::TaskContext context;
  io::VolumeWriteOptions options;
  options.format = io::VolumeFormat::opendx;
  options.name = "potential\\field\"";
  const auto serialized = io::serialize_volume(grid, options, context);
  passed &= expect(
      serialized.has_value() &&
          serialized.value().content.find("type double rank 0 items 12") !=
              std::string::npos &&
          serialized.value().content.find("object \"potential\\\\field'\"") !=
              std::string::npos &&
          serialized.value().report.byte_count ==
              serialized.value().content.size() &&
          has_loss(serialized.value().report.losses, "coordinate_unit") &&
          has_loss(serialized.value().report.losses, "field_name"),
      "OpenDX serialization must preserve precision and report unencoded or "
      "sanitized semantics");
  if (serialized.has_value()) {
    const auto roundtrip = io::read_volume(serialized.value().content);
    passed &= expect(roundtrip.has_value(),
                     "serialized OpenDX must be accepted by the native reader");
    if (roundtrip.has_value()) {
      const auto &copy = *roundtrip.value().volumes.front().grid;
      passed &= expect(
          copy.shape() == grid.shape() && copy.origin() == grid.origin() &&
              copy.deltas() == grid.deltas() &&
              copy.scalars().precision() == grid.scalars().precision() &&
              copy.value_count() == grid.value_count(),
          "OpenDX round-trip must preserve shape, skewed geometry and "
          "precision");
      for (std::size_t index = 0; index < grid.value_count(); ++index) {
        passed &= expect(near(copy.scalars().value(index),
                              grid.scalars().value(index)),
                         "OpenDX round-trip must preserve z-fastest values");
      }
    }
  }

  model::VolumeMetadata metadata;
  metadata.scalar_unit = "kT/e";
  metadata.fields.emplace("provenance", "fixture");
  const auto singleton = model::VolumeGrid::create(
      {1U, 1U, 2U}, {0.0, 0.0, 0.0},
      {model::Vec3d{1.0, 0.0, 0.0}, model::Vec3d{0.0, 2.0, 0.0},
       model::Vec3d{0.0, 0.0, 0.5}},
      model::VolumeScalarBuffer{std::vector<float>{1.25F, -2.5F}},
      std::move(metadata));
  passed &= expect(singleton.has_value(), "singleton-axis grid must be valid");
  if (singleton.has_value()) {
    io::VolumeWriteOptions singleton_options;
    singleton_options.format = io::VolumeFormat::opendx;
    singleton_options.name = "singleton";
    const auto written =
        io::serialize_volume(*singleton.value(), singleton_options, context);
    passed &= expect(
        written.has_value() &&
            written.value().content.find("counts 1 1 2") != std::string::npos &&
            written.value().content.find("type float rank 0 items 2") !=
                std::string::npos &&
            has_loss(written.value().report.losses, "scalar_unit") &&
            has_loss(written.value().report.losses, "metadata_fields"),
        "OpenDX writer must support singleton axes and retain float32 while "
        "reporting metadata loss");
  }

  const auto output = std::filesystem::path{argv[2]} / "writer_roundtrip.dx";
  std::error_code ignored;
  std::filesystem::remove(output, ignored);
  const auto file =
      io::write_volume_file(output, grid, options, false, context);
  passed &= expect(file.has_value() && std::filesystem::exists(output),
                   "atomic OpenDX file write must publish a complete target");
  const auto collision =
      io::write_volume_file(output, grid, options, false, context);
  passed &= expect(!collision.has_value() &&
                       collision.error().code ==
                           operation::ErrorCode::invalid_argument,
                   "OpenDX non-overwrite export must preserve existing target");
  const auto replacement =
      io::write_volume_file(output, grid, options, true, context);
  passed &= expect(replacement.has_value(),
                   "OpenDX explicit overwrite must replace the target");

  operation::TaskContext cancelled_context;
  cancelled_context.cancellation.request_cancel();
  const auto cancelled_output =
      std::filesystem::path{argv[2]} / "writer_cancelled.dx";
  std::filesystem::remove(cancelled_output, ignored);
  const auto cancelled = io::write_volume_file(
      cancelled_output, grid, options, false, cancelled_context);
  passed &= expect(!cancelled.has_value() &&
                       cancelled.error().code == operation::ErrorCode::cancelled &&
                       !std::filesystem::exists(cancelled_output),
                   "cancelled OpenDX export must not publish a partial target");
  return passed ? 0 : 1;
}
