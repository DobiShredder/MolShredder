#include "volume_reader_internal.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "molshredder/operation/error.hpp"

namespace molshredder::io::detail {
namespace {

constexpr std::size_t kHeaderBytes = 1024U;
constexpr std::size_t kMaximumVoxels = 100'000'000U;
constexpr double kDegreesToRadians = 0.017453292519943295769;
constexpr std::int32_t kImodMagicStamp = 1146047817;
constexpr std::int32_t kImodSignedByteFlag = 0x1;

enum class ByteOrder { little, big };

struct Header {
  std::array<std::int32_t, 3U> stored_counts{};
  std::int32_t mode{};
  std::array<std::int32_t, 3U> starts{};
  std::array<std::int32_t, 3U> sampling{};
  std::array<float, 3U> cell_lengths{};
  std::array<float, 3U> cell_angles{};
  std::array<std::int32_t, 3U> axes{};
  std::array<float, 3U> statistics{};
  std::int32_t space_group{};
  std::int32_t extended_bytes{};
  std::int32_t skew_flag{};
  std::int32_t version{};
  std::int32_t imod_stamp{};
  std::int32_t imod_flags{};
  std::array<float, 3U> origin{};
  float rms{};
  std::int32_t label_count{};
};

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

operation::Error unsupported(std::string message) {
  return operation::Error{
      operation::ErrorCode::unsupported, std::move(message),
      "use a real-valued MRC2014/CCP4 scalar map in mode 0, 1, 2, 6, 12 "
      "or RGB mode 16"};
}

std::uint16_t unsigned16(std::string_view content, std::size_t offset,
                         ByteOrder order) noexcept {
  const auto first = static_cast<std::uint8_t>(content[offset]);
  const auto second = static_cast<std::uint8_t>(content[offset + 1U]);
  return order == ByteOrder::little
             ? static_cast<std::uint16_t>(
                   first | (static_cast<std::uint16_t>(second) << 8U))
             : static_cast<std::uint16_t>(
                   (static_cast<std::uint16_t>(first) << 8U) | second);
}

std::uint32_t unsigned32(std::string_view content, std::size_t offset,
                         ByteOrder order) noexcept {
  std::uint32_t result{};
  if (order == ByteOrder::little) {
    for (std::size_t index = 0; index < 4U; ++index) {
      result |= static_cast<std::uint32_t>(
                    static_cast<std::uint8_t>(content[offset + index]))
                << (index * 8U);
    }
  } else {
    for (std::size_t index = 0; index < 4U; ++index) {
      result =
          (result << 8U) | static_cast<std::uint8_t>(content[offset + index]);
    }
  }
  return result;
}

std::int16_t signed16(std::string_view content, std::size_t offset,
                      ByteOrder order) noexcept {
  return std::bit_cast<std::int16_t>(unsigned16(content, offset, order));
}

std::int32_t signed32(std::string_view content, std::size_t offset,
                      ByteOrder order) noexcept {
  return std::bit_cast<std::int32_t>(unsigned32(content, offset, order));
}

float float32(std::string_view content, std::size_t offset,
              ByteOrder order) noexcept {
  return std::bit_cast<float>(unsigned32(content, offset, order));
}

float float16(std::uint16_t bits) noexcept {
  const auto sign = (bits & 0x8000U) == 0U ? 1.0F : -1.0F;
  const auto exponent = static_cast<unsigned>((bits >> 10U) & 0x1fU);
  const auto fraction = static_cast<unsigned>(bits & 0x03ffU);
  if (exponent == 0U) {
    return sign * std::ldexp(static_cast<float>(fraction), -24);
  }
  if (exponent == 31U) {
    return fraction == 0U ? sign * std::numeric_limits<float>::infinity()
                          : std::numeric_limits<float>::quiet_NaN();
  }
  return sign * std::ldexp(static_cast<float>(1024U + fraction),
                           static_cast<int>(exponent) - 25);
}

Header header(std::string_view content, ByteOrder order) noexcept {
  Header result;
  for (std::size_t index = 0; index < 3U; ++index) {
    result.stored_counts[index] = signed32(content, index * 4U, order);
    result.starts[index] = signed32(content, 16U + index * 4U, order);
    result.sampling[index] = signed32(content, 28U + index * 4U, order);
    result.cell_lengths[index] = float32(content, 40U + index * 4U, order);
    result.cell_angles[index] = float32(content, 52U + index * 4U, order);
    result.axes[index] = signed32(content, 64U + index * 4U, order);
    result.statistics[index] = float32(content, 76U + index * 4U, order);
    result.origin[index] = float32(content, 196U + index * 4U, order);
  }
  result.mode = signed32(content, 12U, order);
  result.space_group = signed32(content, 88U, order);
  result.extended_bytes = signed32(content, 92U, order);
  result.skew_flag = signed32(content, 96U, order);
  result.version = signed32(content, 108U, order);
  result.imod_stamp = signed32(content, 152U, order);
  result.imod_flags = signed32(content, 156U, order);
  result.rms = float32(content, 216U, order);
  result.label_count = signed32(content, 220U, order);
  return result;
}

bool known_mode(std::int32_t mode) noexcept {
  return mode == 0 || mode == 1 || mode == 2 || mode == 3 || mode == 4 ||
         mode == 6 || mode == 12 || mode == 16 || mode == 101;
}

bool plausible(const Header &value) noexcept {
  std::size_t product{1U};
  for (const auto count : value.stored_counts) {
    if (count <= 0 || static_cast<std::uint64_t>(count) > kMaximumVoxels ||
        product > kMaximumVoxels / static_cast<std::size_t>(count)) {
      return false;
    }
    product *= static_cast<std::size_t>(count);
  }
  std::array<bool, 3U> seen{};
  for (const auto axis : value.axes) {
    if (axis < 1 || axis > 3 || seen[static_cast<std::size_t>(axis - 1)])
      return false;
    seen[static_cast<std::size_t>(axis - 1)] = true;
  }
  return known_mode(value.mode) && value.extended_bytes >= 0 &&
         value.label_count >= 0 && value.label_count <= 10;
}

operation::Result<ByteOrder> byte_order(std::string_view content) {
  const auto first = static_cast<std::uint8_t>(content[212U]);
  const auto second = static_cast<std::uint8_t>(content[213U]);
  if (first == 0x11U && second == 0x11U)
    return operation::Result<ByteOrder>::success(ByteOrder::big);
  if (first == 0x44U && (second == 0x44U || second == 0x41U))
    return operation::Result<ByteOrder>::success(ByteOrder::little);

  const auto little = plausible(header(content, ByteOrder::little));
  const auto big = plausible(header(content, ByteOrder::big));
  if (little == big) {
    return operation::Result<ByteOrder>::failure(
        invalid("MRC machine stamp is unknown and byte order is ambiguous",
                "write a standard little-endian or big-endian MACHST value"));
  }
  return operation::Result<ByteOrder>::success(little ? ByteOrder::little
                                                      : ByteOrder::big);
}

std::optional<std::size_t> bytes_per_value(std::int32_t mode) noexcept {
  switch (mode) {
  case 0:
    return 1U;
  case 1:
  case 6:
  case 12:
    return 2U;
  case 2:
    return 4U;
  case 16:
    return 3U;
  default:
    return std::nullopt;
  }
}

operation::Result<std::size_t>
voxel_count(const std::array<std::int32_t, 3U> &counts) {
  std::size_t result{1U};
  for (const auto count : counts) {
    if (count <= 0 || static_cast<std::uint64_t>(count) > kMaximumVoxels ||
        result > kMaximumVoxels / static_cast<std::size_t>(count)) {
      return operation::Result<std::size_t>::failure(
          invalid("MRC dimensions are invalid or exceed 100,000,000 voxels"));
    }
    result *= static_cast<std::size_t>(count);
  }
  return operation::Result<std::size_t>::success(result);
}

operation::Result<std::array<model::Vec3d, 3U>>
cell_vectors(const Header &value) {
  for (const auto length : value.cell_lengths) {
    if (!std::isfinite(length) || length <= 0.0F) {
      return operation::Result<std::array<model::Vec3d, 3U>>::failure(
          invalid("MRC cell lengths must be finite and positive"));
    }
  }
  for (const auto angle : value.cell_angles) {
    if (!std::isfinite(angle) || angle <= 0.0F || angle >= 180.0F) {
      return operation::Result<std::array<model::Vec3d, 3U>>::failure(invalid(
          "MRC cell angles must be finite and between 0 and 180 degrees"));
    }
  }
  for (const auto sampling : value.sampling) {
    if (sampling <= 0) {
      return operation::Result<std::array<model::Vec3d, 3U>>::failure(
          invalid("MRC unit-cell sampling counts must all be positive"));
    }
  }

  const auto alpha =
      static_cast<double>(value.cell_angles[0]) * kDegreesToRadians;
  const auto beta =
      static_cast<double>(value.cell_angles[1]) * kDegreesToRadians;
  const auto gamma =
      static_cast<double>(value.cell_angles[2]) * kDegreesToRadians;
  const auto cos_alpha = std::cos(alpha);
  const auto cos_beta = std::cos(beta);
  const auto cos_gamma = std::cos(gamma);
  const auto sin_gamma = std::sin(gamma);
  if (!std::isfinite(sin_gamma) || std::abs(sin_gamma) <= 1.0e-12) {
    return operation::Result<std::array<model::Vec3d, 3U>>::failure(
        invalid("MRC gamma angle produces a degenerate unit cell"));
  }
  const auto c_y = (cos_alpha - cos_beta * cos_gamma) / sin_gamma;
  const auto c_z_squared = 1.0 - cos_beta * cos_beta - c_y * c_y;
  if (!std::isfinite(c_z_squared) || c_z_squared <= 1.0e-12) {
    return operation::Result<std::array<model::Vec3d, 3U>>::failure(
        invalid("MRC cell angles produce a degenerate unit cell"));
  }

  const auto a = static_cast<double>(value.cell_lengths[0]);
  const auto b = static_cast<double>(value.cell_lengths[1]);
  const auto c = static_cast<double>(value.cell_lengths[2]);
  return operation::Result<std::array<model::Vec3d, 3U>>::success(
      {{{a, 0.0, 0.0},
        {b * cos_gamma, b * sin_gamma, 0.0},
        {c * cos_beta, c * c_y, c * std::sqrt(c_z_squared)}}});
}

std::string printable_field(std::string_view content, std::size_t offset,
                            std::size_t size) {
  std::string result;
  result.reserve(size);
  for (std::size_t index = 0; index < size; ++index) {
    const auto value = static_cast<unsigned char>(content[offset + index]);
    result.push_back(value >= 32U && value <= 126U ? static_cast<char>(value)
                                                   : ' ');
  }
  const auto first = result.find_first_not_of(' ');
  if (first == std::string::npos)
    return {};
  const auto last = result.find_last_not_of(' ');
  return result.substr(first, last - first + 1U);
}

std::string vector_text(const std::array<std::int32_t, 3U> &values) {
  return std::to_string(values[0]) + "," + std::to_string(values[1]) + "," +
         std::to_string(values[2]);
}

std::string vector_text(const std::array<float, 3U> &values) {
  return std::to_string(values[0]) + "," + std::to_string(values[1]) + "," +
         std::to_string(values[2]);
}

float scalar(std::string_view content, std::size_t offset, std::int32_t mode,
             ByteOrder order, bool unsigned_mode_zero) noexcept {
  switch (mode) {
  case 0:
    if (unsigned_mode_zero)
      return static_cast<float>(static_cast<std::uint8_t>(content[offset]));
    return static_cast<float>(
        std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(content[offset])));
  case 1:
    return static_cast<float>(signed16(content, offset, order));
  case 2:
    return float32(content, offset, order);
  case 6:
    return static_cast<float>(unsigned16(content, offset, order));
  case 12:
    return float16(unsigned16(content, offset, order));
  case 16:
    return (static_cast<float>(static_cast<std::uint8_t>(content[offset])) +
            static_cast<float>(
                static_cast<std::uint8_t>(content[offset + 1U])) +
            static_cast<float>(
                static_cast<std::uint8_t>(content[offset + 2U]))) /
           3.0F;
  default:
    return std::numeric_limits<float>::quiet_NaN();
  }
}

model::Vec3d scaled(model::Vec3d value, double factor) noexcept {
  return {value.x * factor, value.y * factor, value.z * factor};
}

model::Vec3d add(model::Vec3d left, model::Vec3d right) noexcept {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

model::Vec3d multiplied(model::Vec3d value, double factor) noexcept {
  return scaled(value, factor);
}

double distance(model::Vec3d left, model::Vec3d right) noexcept {
  const auto dx = left.x - right.x;
  const auto dy = left.y - right.y;
  const auto dz = left.z - right.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

bool is_mrc(std::string_view content) noexcept {
  return content.size() >= kHeaderBytes && content.substr(208U, 4U) == "MAP ";
}

operation::Result<VolumeDocument> read_mrc(std::string_view content,
                                           VolumeReadOptions options) {
  if (!is_mrc(content)) {
    return operation::Result<VolumeDocument>::failure(
        invalid("MRC input is missing the 1024-byte header or MAP identifier"));
  }
  const auto order_result = byte_order(content);
  if (!order_result.has_value())
    return operation::Result<VolumeDocument>::failure(order_result.error());
  const auto order = order_result.value();
  const auto parsed = header(content, order);
  if (!plausible(parsed)) {
    return operation::Result<VolumeDocument>::failure(invalid(
        "MRC header dimensions, axis mapping, labels or mode are invalid"));
  }
  if (parsed.skew_flag == 1) {
    return operation::Result<VolumeDocument>::failure(
        unsupported("CCP4 skew transformation records are not yet supported"));
  }
  if (parsed.mode == 3 || parsed.mode == 4) {
    return operation::Result<VolumeDocument>::failure(
        unsupported("complex transform MRC modes are not scalar volumes"));
  }
  if (parsed.mode == 101) {
    return operation::Result<VolumeDocument>::failure(
        unsupported("packed 4-bit MRC mode 101 is not yet supported"));
  }
  const auto value_bytes = bytes_per_value(parsed.mode);
  if (!value_bytes.has_value()) {
    return operation::Result<VolumeDocument>::failure(
        unsupported("MRC scalar mode is not supported"));
  }
  const auto count_result = voxel_count(parsed.stored_counts);
  if (!count_result.has_value())
    return operation::Result<VolumeDocument>::failure(count_result.error());
  const auto count = count_result.value();
  const auto is_imod = parsed.imod_stamp == kImodMagicStamp;
  const auto unsigned_mode_zero =
      parsed.mode == 0 && is_imod &&
      (parsed.imod_flags & kImodSignedByteFlag) == 0;
  const auto extended = static_cast<std::size_t>(parsed.extended_bytes);
  if (extended > content.size() - kHeaderBytes ||
      count >
          (std::numeric_limits<std::size_t>::max() - kHeaderBytes - extended) /
              *value_bytes) {
    return operation::Result<VolumeDocument>::failure(
        invalid("MRC extended header or data size overflows the input"));
  }
  const auto data_offset = kHeaderBytes + extended;
  const auto expected_size = data_offset + count * *value_bytes;
  if (content.size() != expected_size) {
    return operation::Result<VolumeDocument>::failure(
        invalid(content.size() < expected_size
                    ? "MRC scalar data is truncated"
                    : "MRC file has trailing bytes after the scalar data"));
  }

  std::array<std::size_t, 3U> logical_counts{};
  std::array<std::int32_t, 3U> logical_starts{};
  for (std::size_t storage_axis = 0; storage_axis < 3U; ++storage_axis) {
    const auto logical_axis =
        static_cast<std::size_t>(parsed.axes[storage_axis] - 1);
    logical_counts[logical_axis] =
        static_cast<std::size_t>(parsed.stored_counts[storage_axis]);
    logical_starts[logical_axis] = parsed.starts[storage_axis];
  }

  std::vector<float> values(count);
  std::array<std::size_t, 3U> logical_index{};
  std::size_t storage_index{};
  for (std::size_t section = 0;
       section < static_cast<std::size_t>(parsed.stored_counts[2]); ++section) {
    for (std::size_t row = 0;
         row < static_cast<std::size_t>(parsed.stored_counts[1]); ++row) {
      for (std::size_t column = 0;
           column < static_cast<std::size_t>(parsed.stored_counts[0]);
           ++column, ++storage_index) {
        logical_index[static_cast<std::size_t>(parsed.axes[0] - 1)] = column;
        logical_index[static_cast<std::size_t>(parsed.axes[1] - 1)] = row;
        logical_index[static_cast<std::size_t>(parsed.axes[2] - 1)] = section;
        const auto logical_offset =
            (logical_index[0] * logical_counts[1] + logical_index[1]) *
                logical_counts[2] +
            logical_index[2];
        const auto value =
            scalar(content, data_offset + storage_index * *value_bytes,
                   parsed.mode, order, unsigned_mode_zero);
        if (!std::isfinite(value)) {
          return operation::Result<VolumeDocument>::failure(
              invalid("MRC scalar data contains a non-finite value"));
        }
        values[logical_offset] = value;
      }
    }
  }

  const auto cell_result = cell_vectors(parsed);
  if (!cell_result.has_value())
    return operation::Result<VolumeDocument>::failure(cell_result.error());
  auto deltas = cell_result.value();
  for (std::size_t axis = 0; axis < 3U; ++axis) {
    deltas[axis] = multiplied(deltas[axis],
                              1.0 / static_cast<double>(parsed.sampling[axis]));
  }

  auto start_origin = model::Vec3d{};
  for (std::size_t axis = 0; axis < 3U; ++axis) {
    start_origin = add(
        start_origin,
        multiplied(deltas[axis], static_cast<double>(logical_starts[axis])));
  }
  const model::Vec3d header_origin{parsed.origin[0], parsed.origin[1],
                                   parsed.origin[2]};
  const auto has_header_origin = parsed.origin[0] != 0.0F ||
                                 parsed.origin[1] != 0.0F ||
                                 parsed.origin[2] != 0.0F;
  auto origin = has_header_origin ? header_origin : start_origin;
  const auto unit_factor =
      options.coordinate_unit == operation::LengthUnit::nanometer ? 0.1 : 1.0;
  origin = scaled(origin, unit_factor);
  for (auto &delta : deltas)
    delta = scaled(delta, unit_factor);

  model::VolumeMetadata metadata;
  metadata.coordinate_unit = options.coordinate_unit;
  metadata.fields.emplace("format", "mrc");
  metadata.fields.emplace("mrc_version", std::to_string(parsed.version));
  metadata.fields.emplace("mrc_mode", std::to_string(parsed.mode));
  if (is_imod) {
    metadata.fields.emplace("mrc_imod_stamp", "true");
    metadata.fields.emplace("mrc_imod_flags",
                            std::to_string(parsed.imod_flags));
    if (parsed.mode == 0) {
      metadata.fields.emplace("mrc_mode_0_signedness",
                              unsigned_mode_zero ? "unsigned_imod"
                                                 : "signed_imod");
    }
  } else if (parsed.mode == 0) {
    metadata.fields.emplace("mrc_mode_0_signedness", "signed_mrc2014");
  }
  if (parsed.mode == 16)
    metadata.fields.emplace("mrc_rgb_conversion", "arithmetic_mean");
  metadata.fields.emplace("mrc_byte_order",
                          order == ByteOrder::little ? "little" : "big");
  metadata.fields.emplace("mrc_axis_mapping", vector_text(parsed.axes));
  metadata.fields.emplace("mrc_grid_start", vector_text(logical_starts));
  metadata.fields.emplace("mrc_cell_lengths_angstrom",
                          vector_text(parsed.cell_lengths));
  metadata.fields.emplace("mrc_cell_angles_degrees",
                          vector_text(parsed.cell_angles));
  metadata.fields.emplace("mrc_space_group",
                          std::to_string(parsed.space_group));
  metadata.fields.emplace("mrc_extended_header_bytes",
                          std::to_string(parsed.extended_bytes));
  metadata.fields.emplace("mrc_handedness", "unspecified_by_standard");
  metadata.fields.emplace("origin_source", has_header_origin
                                               ? "mrc_origin"
                                               : "permuted_grid_start");
  metadata.fields.emplace("coordinate_unit_source",
                          options.coordinate_unit ==
                                  operation::LengthUnit::angstrom
                              ? "mrc_header_angstrom"
                              : "mrc_header_angstrom_converted_to_nanometer");
  metadata.fields.emplace("ordering", "z_fastest_logical_xyz");
  metadata.fields.emplace("mrc_header_statistics",
                          vector_text(parsed.statistics));
  if (std::isfinite(parsed.rms))
    metadata.fields.emplace("mrc_header_rms", std::to_string(parsed.rms));
  const auto extension_type = printable_field(content, 104U, 4U);
  if (!extension_type.empty())
    metadata.fields.emplace("mrc_extended_header_type", extension_type);
  for (std::int32_t index = 0; index < parsed.label_count; ++index) {
    const auto label = printable_field(
        content, 224U + static_cast<std::size_t>(index) * 80U, 80U);
    if (!label.empty())
      metadata.fields.emplace("mrc_label_" + std::to_string(index + 1), label);
  }
  if (has_header_origin && distance(header_origin, start_origin) > 1.0e-5) {
    metadata.fields.emplace("mrc_origin_start_conflict", "true");
    metadata.fields.emplace("mrc_grid_start_origin_angstrom",
                            std::to_string(start_origin.x) + "," +
                                std::to_string(start_origin.y) + "," +
                                std::to_string(start_origin.z));
  }

  const auto grid = model::VolumeGrid::create(
      model::VolumeShape{logical_counts[0], logical_counts[1],
                         logical_counts[2]},
      origin, deltas, model::VolumeScalarBuffer{std::move(values)},
      std::move(metadata));
  if (!grid.has_value())
    return operation::Result<VolumeDocument>::failure(grid.error());
  auto name = options.name.value_or("MRC map");
  if (name.empty()) {
    return operation::Result<VolumeDocument>::failure(
        invalid("volume name must not be empty"));
  }
  return operation::Result<VolumeDocument>::success(VolumeDocument{
      VolumeFormat::mrc, {VolumeData{std::move(name), grid.value()}}});
}

} // namespace molshredder::io::detail
