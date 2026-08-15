#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/io/volume_reader.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

bool near(double left, double right, double tolerance = 1.0e-5) {
  return std::abs(left - right) <= tolerance;
}

std::string read(const std::filesystem::path &path) {
  std::ifstream input{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{input}, {}};
}

void put_u16(std::string &bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<char>(value & 0xffU);
  bytes[offset + 1U] = static_cast<char>(value >> 8U);
}

void put_u32(std::string &bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t index = 0; index < 4U; ++index)
    bytes[offset + index] = static_cast<char>(value >> (index * 8U));
}

void put_i32(std::string &bytes, std::size_t offset, std::int32_t value) {
  put_u32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void put_f32(std::string &bytes, std::size_t offset, float value) {
  put_u32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

std::string mode_variant(const std::string &source, std::int32_t mode,
                         std::string payload) {
  auto result = source.substr(0U, 1104U);
  put_i32(result, 12U, mode);
  result += payload;
  return result;
}

molshredder::io::VolumeReadOptions mrc_options() {
  molshredder::io::VolumeReadOptions options;
  options.format = molshredder::io::VolumeFormat::mrc;
  return options;
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  bool passed = true;
  passed &=
      expect(argc == 3, "MRC reader test requires little and big fixtures");
  if (argc != 3)
    return 1;

  io::VolumeReadOptions options;
  options.coordinate_unit = operation::LengthUnit::nanometer;
  const auto little =
      io::read_volume_file(std::filesystem::path{argv[1]}, options);
  passed &= expect(little.has_value() &&
                       little.value().format == io::VolumeFormat::mrc &&
                       little.value().volumes.size() == 1U,
                   "little-endian MRC2014 fixture must load");
  if (little.has_value()) {
    const auto &volume = little.value().volumes.front();
    const auto &grid = *volume.grid;
    const auto y_delta = grid.deltas()[1];
    passed &= expect(
        volume.name == "density" &&
            grid.shape() == model::VolumeShape{2U, 2U, 3U} &&
            grid.scalars().precision() == model::VolumePrecision::float32 &&
            near(grid.origin().x, 1.0) && near(grid.origin().y, 2.0) &&
            near(grid.origin().z, 3.0) && near(grid.deltas()[0].x, 0.1) &&
            near(y_delta.x, 0.1) && near(y_delta.y, 0.1732050808) &&
            near(grid.deltas()[2].z, 0.15) &&
            near(grid.value(1U, 1U, 2U), 112.0) &&
            grid.scalars().range() == std::pair<double, double>{0.0, 112.0} &&
            grid.metadata().fields.at("mrc_axis_mapping") == "3,1,2" &&
            grid.metadata().fields.at("origin_source") == "mrc_origin" &&
            grid.metadata().fields.at("mrc_origin_start_conflict") == "true" &&
            grid.metadata().fields.at("mrc_extended_header_type") == "CCP4" &&
            grid.metadata().fields.at("mrc_handedness") ==
                "unspecified_by_standard",
        "MRC reader must preserve permutation, triclinic geometry, origin, "
        "ordering and metadata");
  }

  const auto big = io::read_volume_file(std::filesystem::path{argv[2]});
  passed &= expect(big.has_value(), "big-endian MRC mode 1 fixture must load");
  if (big.has_value()) {
    const auto &grid = *big.value().volumes.front().grid;
    passed &= expect(
        grid.shape() == model::VolumeShape{1U, 1U, 2U} &&
            grid.value(0U, 0U, 0U) == -2.0 && grid.value(0U, 0U, 1U) == 300.0 &&
            near(grid.origin().x, 1.0) && near(grid.origin().z, -2.0) &&
            grid.metadata().fields.at("mrc_byte_order") == "big" &&
            grid.metadata().fields.at("origin_source") == "permuted_grid_start",
        "big-endian integer map and start-derived origin must be preserved");
  }

  const auto source = read(argv[1]);
  std::string mode0_payload;
  std::string mode6_payload(24U, '\0');
  std::string mode12_payload(24U, '\0');
  std::string mode16_payload(36U, '\0');
  for (std::size_t index = 0; index < 12U; ++index) {
    mode0_payload.push_back(index == 0U ? static_cast<char>(0x80U)
                                        : static_cast<char>(index));
    put_u16(mode6_payload, index * 2U,
            static_cast<std::uint16_t>(60000U + index));
    put_u16(mode12_payload, index * 2U,
            index == 0U ? 0x3e00U : static_cast<std::uint16_t>(0U));
    mode16_payload[index * 3U] = static_cast<char>(index);
    mode16_payload[index * 3U + 1U] = static_cast<char>(index + 3U);
    mode16_payload[index * 3U + 2U] = static_cast<char>(index + 6U);
  }
  const auto mode0 =
      io::read_volume(mode_variant(source, 0, mode0_payload), mrc_options());
  const auto mode6 =
      io::read_volume(mode_variant(source, 6, mode6_payload), mrc_options());
  const auto mode12 =
      io::read_volume(mode_variant(source, 12, mode12_payload), mrc_options());
  const auto mode16 =
      io::read_volume(mode_variant(source, 16, mode16_payload), mrc_options());
  passed &= expect(
      mode0.has_value() && mode6.has_value() && mode12.has_value() &&
          mode16.has_value() &&
          mode0.value().volumes.front().grid->value(0U, 0U, 0U) == -128.0 &&
          mode0.value().volumes.front().grid->value(1U, 1U, 2U) == 11.0 &&
          mode6.value().volumes.front().grid->value(1U, 1U, 2U) == 60011.0 &&
          mode12.value().volumes.front().grid->value(0U, 0U, 0U) == 1.5 &&
          mode16.value().volumes.front().grid->value(0U, 0U, 0U) == 3.0 &&
          mode16.value().volumes.front().grid->metadata().fields.at(
              "mrc_rgb_conversion") == "arithmetic_mean",
      "MRC signed byte, unsigned 16-bit, IEEE binary16 and RGB modes must "
      "decode");

  auto imod_unsigned_bytes = mode_variant(source, 0, mode0_payload);
  put_i32(imod_unsigned_bytes, 152U, 1146047817);
  put_i32(imod_unsigned_bytes, 156U, 0);
  const auto imod_unsigned = io::read_volume(imod_unsigned_bytes, mrc_options());
  auto imod_signed_bytes = imod_unsigned_bytes;
  put_i32(imod_signed_bytes, 156U, 1);
  const auto imod_signed = io::read_volume(imod_signed_bytes, mrc_options());
  passed &= expect(
      imod_unsigned.has_value() && imod_signed.has_value() &&
          imod_unsigned.value().volumes.front().grid->value(0U, 0U, 0U) ==
              128.0 &&
          imod_signed.value().volumes.front().grid->value(0U, 0U, 0U) ==
              -128.0 &&
          imod_unsigned.value().volumes.front().grid->metadata().fields.at(
              "mrc_mode_0_signedness") == "unsigned_imod",
      "IMOD mode-0 flags must select unsigned or signed byte decoding");

  auto unknown_stamp = source;
  unknown_stamp[212U] = '\0';
  unknown_stamp[213U] = '\0';
  const auto inferred = io::read_volume(unknown_stamp, mrc_options());
  auto duplicate_axis = source;
  put_i32(duplicate_axis, 68U, 3);
  auto complex_mode = source;
  put_i32(complex_mode, 12U, 3);
  auto skew = source;
  put_i32(skew, 96U, 1);
  auto bad_sampling = source;
  put_i32(bad_sampling, 28U, 0);
  auto non_finite = source;
  put_f32(non_finite, 1104U, std::numeric_limits<float>::infinity());
  auto trailing = source;
  trailing.push_back('\0');
  const auto bad_axis = io::read_volume(duplicate_axis, mrc_options());
  const auto complex = io::read_volume(complex_mode, mrc_options());
  const auto skewed = io::read_volume(skew, mrc_options());
  const auto bad_cell = io::read_volume(bad_sampling, mrc_options());
  const auto bad_value = io::read_volume(non_finite, mrc_options());
  const auto truncated = io::read_volume(
      std::string_view{source}.substr(0U, source.size() - 1U), mrc_options());
  const auto excess = io::read_volume(trailing, mrc_options());
  passed &= expect(
      inferred.has_value() && !bad_axis.has_value() && !complex.has_value() &&
          !skewed.has_value() && !bad_cell.has_value() &&
          !bad_value.has_value() && !truncated.has_value() &&
          !excess.has_value(),
      "MRC must infer legacy endian safely and reject invalid axes, complex, "
      "skew, geometry, non-finite, truncated and trailing input");

  passed &= expect(io::to_string(io::VolumeFormat::mrc) == "mrc",
                   "MRC volume format name must be stable");
  return passed ? 0 : 1;
}
