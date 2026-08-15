#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
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

bool near(double left, double right, double tolerance = 1.0e-5) {
  return std::abs(left - right) <= tolerance;
}

std::uint32_t u32(std::string_view bytes, std::size_t offset) {
  std::uint32_t value{};
  for (std::size_t index = 0; index < 4U; ++index) {
    value |= static_cast<std::uint32_t>(
                 static_cast<std::uint8_t>(bytes[offset + index]))
             << (index * 8U);
  }
  return value;
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  if (argc != 2)
    return 2;
  bool passed = true;

  std::vector<double> scalars;
  for (std::size_t index = 0; index < 12U; ++index)
    scalars.push_back(static_cast<double>(index) + 0.123456789);
  model::VolumeMetadata metadata;
  metadata.coordinate_unit = operation::LengthUnit::nanometer;
  metadata.scalar_unit = "electron per nm^3";
  metadata.fields.emplace("mrc_space_group", "1");
  metadata.fields.emplace("source_note", "writer fixture");
  const auto grid = model::VolumeGrid::create(
      {2U, 2U, 3U}, {-1.0, 2.0, 0.5},
      {{{0.1, 0.0, 0.0}, {0.02, 0.2, 0.0}, {0.01, 0.03, 0.3}}},
      model::VolumeScalarBuffer{std::move(scalars)}, std::move(metadata));
  passed &= expect(grid.has_value(), "canonical MRC writer grid must create");
  if (!grid.has_value())
    return 1;

  io::VolumeWriteOptions options;
  options.format = io::VolumeFormat::mrc;
  options.name = "density";
  operation::TaskContext context;
  const auto serialized = io::serialize_volume(*grid.value(), options, context);
  passed &= expect(
      serialized.has_value() && serialized.value().content.size() == 1072U &&
          serialized.value().content.substr(208U, 4U) == "MAP " &&
          u32(serialized.value().content, 12U) == 2U &&
          u32(serialized.value().content, 64U) == 1U &&
          u32(serialized.value().content, 68U) == 2U &&
          u32(serialized.value().content, 72U) == 3U &&
          u32(serialized.value().content, 108U) == 20141U &&
          serialized.value().report.precision ==
              model::VolumePrecision::float32 &&
          serialized.value().report.losses.size() == 5U,
      "MRC writer must emit canonical little-endian MRC2014 mode 2 header");
  if (serialized.has_value()) {
    io::VolumeReadOptions read_options;
    read_options.format = io::VolumeFormat::mrc;
    read_options.coordinate_unit = operation::LengthUnit::nanometer;
    const auto roundtrip =
        io::read_volume(serialized.value().content, read_options);
    passed &= expect(roundtrip.has_value(), "MRC output must read back");
    if (roundtrip.has_value()) {
      const auto &copy = *roundtrip.value().volumes.front().grid;
      passed &= expect(
          copy.shape() == model::VolumeShape{2U, 2U, 3U} &&
              near(copy.origin().x, -1.0) && near(copy.origin().y, 2.0) &&
              near(copy.deltas()[0].x, 0.1) &&
              near(copy.deltas()[1].x, 0.02) &&
              near(copy.deltas()[1].y, 0.2) &&
              near(copy.deltas()[2].z, 0.3) &&
              near(copy.value(1U, 1U, 2U), 11.123456789, 1.0e-5) &&
              copy.metadata().fields.at("mrc_space_group") == "1" &&
              copy.metadata().fields.at("mrc_axis_mapping") == "1,2,3",
          "MRC read-write-read must preserve physical geometry, scalar order "
          "and space group");
    }
  }

  const auto output = std::filesystem::path{argv[1]} / "roundtrip-writer.mrc";
  std::error_code ignored;
  std::filesystem::remove(output, ignored);
  const auto written = io::write_volume_file(output, *grid.value(), options,
                                              false, context);
  const auto collision = io::write_volume_file(output, *grid.value(), options,
                                                false, context);
  passed &= expect(written.has_value() && !collision.has_value(),
                   "MRC publish must be atomic and reject implicit overwrite");
  operation::TaskContext cancelled_context;
  cancelled_context.cancellation.request_cancel();
  const auto cancelled_path =
      std::filesystem::path{argv[1]} / "cancelled-writer.mrc";
  std::filesystem::remove(cancelled_path, ignored);
  const auto cancelled = io::write_volume_file(
      cancelled_path, *grid.value(), options, false, cancelled_context);
  passed &= expect(!cancelled.has_value() &&
                       cancelled.error().code ==
                           operation::ErrorCode::cancelled &&
                       !std::filesystem::exists(cancelled_path),
                   "cancelled MRC export must not publish a partial target");

  const auto rotated = model::VolumeGrid::create(
      {1U, 1U, 1U}, {},
      {{{0.1, 0.01, 0.0}, {0.0, 0.2, 0.0}, {0.0, 0.0, 0.3}}},
      model::VolumeScalarBuffer{std::vector<float>{1.0F}});
  const auto rejected =
      io::serialize_volume(*rotated.value(), options, context);
  passed &= expect(
      !rejected.has_value() &&
          rejected.error().message.find("arbitrarily rotated") !=
              std::string::npos,
      "MRC writer must reject orientation loss instead of rotating silently");

  return passed ? 0 : 1;
}
