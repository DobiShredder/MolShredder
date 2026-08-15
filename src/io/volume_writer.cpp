#include "molshredder/io/volume_writer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "molshredder/operation/error.hpp"

namespace molshredder::io {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

operation::Error io_error(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::internal, std::move(message),
                          std::move(suggestion)};
}

VolumeFormat resolve_format(VolumeFormat requested,
                            const std::filesystem::path &path = {}) {
  if (requested != VolumeFormat::auto_detect)
    return requested;
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  return extension == ".dx" ? VolumeFormat::opendx
         : extension == ".mrc" || extension == ".map" ||
                 extension == ".ccp4" || extension == ".mrcs"
             ? VolumeFormat::mrc
             : VolumeFormat::auto_detect;
}

double dot(model::Vec3d left, model::Vec3d right) noexcept {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

double length(model::Vec3d value) noexcept {
  return std::sqrt(dot(value, value));
}

double degrees(model::Vec3d left, model::Vec3d right) noexcept {
  constexpr double radians_to_degrees = 57.2957795130823208768;
  return std::acos(std::clamp(dot(left, right) /
                                 (length(left) * length(right)),
                             -1.0, 1.0)) *
         radians_to_degrees;
}

void put_u32(std::string &output, std::size_t offset, std::uint32_t value) {
  for (std::size_t index = 0; index < 4U; ++index)
    output[offset + index] =
        static_cast<char>((value >> (index * 8U)) & 0xffU);
}

void put_i32(std::string &output, std::size_t offset, std::int32_t value) {
  put_u32(output, offset, std::bit_cast<std::uint32_t>(value));
}

void put_f32(std::string &output, std::size_t offset, float value) {
  put_u32(output, offset, std::bit_cast<std::uint32_t>(value));
}

void append_f32(std::string &output, float value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  for (std::size_t index = 0; index < 4U; ++index)
    output.push_back(static_cast<char>((bits >> (index * 8U)) & 0xffU));
}

operation::Result<std::int32_t>
mrc_space_group(const model::VolumeMetadata &metadata) {
  const auto found = metadata.fields.find("mrc_space_group");
  if (found == metadata.fields.end())
    return operation::Result<std::int32_t>::success(0);
  std::int32_t value{};
  const auto parsed = std::from_chars(
      found->second.data(), found->second.data() + found->second.size(), value);
  if (parsed.ec != std::errc{} ||
      parsed.ptr != found->second.data() + found->second.size() || value < 0) {
    return operation::Result<std::int32_t>::failure(
        invalid("MRC space-group metadata must be a non-negative 32-bit "
                "integer"));
  }
  return operation::Result<std::int32_t>::success(value);
}

operation::Result<SerializedVolume>
write_mrc(const model::VolumeGrid &grid, VolumeWriteOptions options,
          operation::TaskContext &context) {
  constexpr std::size_t header_bytes = 1024U;
  if (context.cancellation.is_cancelled()) {
    return operation::Result<SerializedVolume>::failure(operation::Error{
        operation::ErrorCode::cancelled, "MRC export cancelled", {}});
  }
  const auto &shape = grid.shape();
  for (const auto count : {shape.x, shape.y, shape.z}) {
    if (count == 0U ||
        count >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
      return operation::Result<SerializedVolume>::failure(
          invalid("MRC dimensions must fit positive signed 32-bit fields"));
    }
  }
  if (grid.value_count() >
      (std::numeric_limits<std::size_t>::max() - header_bytes) / 4U) {
    return operation::Result<SerializedVolume>::failure(
        invalid("MRC float32 payload exceeds addressable memory"));
  }
  const auto coordinate_to_angstrom =
      grid.metadata().coordinate_unit == operation::LengthUnit::nanometer
          ? 10.0
          : 1.0;
  std::array<model::Vec3d, 3U> deltas = grid.deltas();
  for (auto &delta : deltas) {
    delta.x *= coordinate_to_angstrom;
    delta.y *= coordinate_to_angstrom;
    delta.z *= coordinate_to_angstrom;
  }
  auto origin = grid.origin();
  origin.x *= coordinate_to_angstrom;
  origin.y *= coordinate_to_angstrom;
  origin.z *= coordinate_to_angstrom;
  const auto orientation_scale =
      std::max({length(deltas[0]), length(deltas[1]), length(deltas[2])});
  const auto tolerance = orientation_scale * 1.0e-10;
  if (deltas[0].x <= 0.0 || deltas[1].y <= 0.0 || deltas[2].z <= 0.0 ||
      std::abs(deltas[0].y) > tolerance ||
      std::abs(deltas[0].z) > tolerance ||
      std::abs(deltas[1].z) > tolerance) {
    return operation::Result<SerializedVolume>::failure(invalid(
        "MRC2014 cannot preserve an arbitrarily rotated grid basis",
        "reorient the grid to crystallographic a/x and b/xy convention or "
        "export OpenDX"));
  }
  const auto space_group = mrc_space_group(grid.metadata());
  if (!space_group.has_value())
    return operation::Result<SerializedVolume>::failure(space_group.error());

  const std::array<std::size_t, 3U> sampling{shape.x, shape.y, shape.z};
  const std::array<double, 3U> cell_lengths{
      length(deltas[0]) * static_cast<double>(shape.x),
      length(deltas[1]) * static_cast<double>(shape.y),
      length(deltas[2]) * static_cast<double>(shape.z)};
  const std::array<double, 3U> cell_angles{
      degrees(deltas[1], deltas[2]), degrees(deltas[0], deltas[2]),
      degrees(deltas[0], deltas[1])};
  for (const auto value : {origin.x, origin.y, origin.z, cell_lengths[0],
                           cell_lengths[1], cell_lengths[2], cell_angles[0],
                           cell_angles[1], cell_angles[2]}) {
    if (!std::isfinite(value) ||
        !std::isfinite(static_cast<float>(value))) {
      return operation::Result<SerializedVolume>::failure(
          invalid("MRC geometry exceeds finite float32 header fields"));
    }
  }

  double sum{};
  std::vector<float> values;
  values.reserve(grid.value_count());
  for (std::size_t index = 0; index < grid.value_count(); ++index) {
    const auto value = static_cast<float>(grid.scalars().value(index));
    if (!std::isfinite(value)) {
      return operation::Result<SerializedVolume>::failure(
          invalid("MRC mode 2 narrowing produced a non-finite scalar"));
    }
    values.push_back(value);
    sum += static_cast<double>(value);
  }
  const auto mean = sum / static_cast<double>(values.size());
  double squared_sum{};
  for (const auto value : values) {
    const auto difference = static_cast<double>(value) - mean;
    squared_sum += difference * difference;
  }
  const auto rms = std::sqrt(squared_sum / static_cast<double>(values.size()));
  const auto [minimum, maximum] =
      std::minmax_element(values.begin(), values.end());

  std::string output(header_bytes, '\0');
  for (std::size_t axis = 0; axis < 3U; ++axis) {
    put_i32(output, axis * 4U,
            static_cast<std::int32_t>(sampling[axis]));
    put_i32(output, 16U + axis * 4U, 0);
    put_i32(output, 28U + axis * 4U,
            static_cast<std::int32_t>(sampling[axis]));
    put_f32(output, 40U + axis * 4U,
            static_cast<float>(cell_lengths[axis]));
    put_f32(output, 52U + axis * 4U,
            static_cast<float>(cell_angles[axis]));
    put_i32(output, 64U + axis * 4U,
            static_cast<std::int32_t>(axis + 1U));
  }
  put_i32(output, 12U, 2);
  put_f32(output, 76U, *minimum);
  put_f32(output, 80U, *maximum);
  put_f32(output, 84U, static_cast<float>(mean));
  put_i32(output, 88U, space_group.value());
  put_i32(output, 92U, 0);
  put_i32(output, 108U, 20141);
  put_f32(output, 196U, static_cast<float>(origin.x));
  put_f32(output, 200U, static_cast<float>(origin.y));
  put_f32(output, 204U, static_cast<float>(origin.z));
  output.replace(208U, 4U, "MAP ");
  output[212U] = static_cast<char>(0x44U);
  output[213U] = static_cast<char>(0x44U);
  put_f32(output, 216U, static_cast<float>(rms));

  auto label = options.name.empty() ? std::string{"MolShredder MRC2014"}
                                    : "MolShredder MRC2014: " + options.name;
  bool label_changed{};
  for (auto &character : label) {
    const auto code = static_cast<unsigned char>(character);
    if (code < 0x20U || code > 0x7eU) {
      character = ' ';
      label_changed = true;
    }
  }
  if (label.size() > 80U) {
    label.resize(80U);
    label_changed = true;
  }
  put_i32(output, 220U, 1);
  output.replace(224U, label.size(), label);

  output.reserve(header_bytes + values.size() * 4U);
  for (std::size_t section = 0; section < shape.z; ++section) {
    for (std::size_t row = 0; row < shape.y; ++row) {
      for (std::size_t column = 0; column < shape.x; ++column) {
        const auto logical = grid.linear_index(column, row, section);
        if ((logical & 0x3fffU) == 0U &&
            context.cancellation.is_cancelled()) {
          return operation::Result<SerializedVolume>::failure(
              operation::Error{operation::ErrorCode::cancelled,
                               "MRC export cancelled while writing scalars",
                               {}});
        }
        append_f32(output, values[logical]);
      }
    }
  }

  std::vector<VolumeFormatLoss> losses;
  if (grid.scalars().precision() == model::VolumePrecision::float64) {
    losses.push_back({"scalar_precision",
                      static_cast<std::uint64_t>(grid.value_count()),
                      "MRC mode 2 narrows float64 scalars to float32"});
  }
  losses.push_back({"geometry_precision", 13U,
                    "MRC2014 stores origin, cell geometry and statistics in "
                    "float32 header fields"});
  losses.push_back({"handedness", 1U,
                    "MRC2014 does not define data-block handedness"});
  if (grid.metadata().scalar_unit.has_value()) {
    losses.push_back({"scalar_unit", 1U,
                      "MRC2014 does not encode a typed scalar unit"});
  }
  std::uint64_t metadata_fields{};
  for (const auto &[key, unused] : grid.metadata().fields) {
    static_cast<void>(unused);
    if (key != "format" && key != "ordering" &&
        key != "coordinate_unit_source" && key != "mrc_space_group")
      ++metadata_fields;
  }
  if (metadata_fields != 0U) {
    losses.push_back({"metadata_fields", metadata_fields,
                      "MRC output normalizes axis/start/extended metadata to "
                      "a canonical MRC2014 header"});
  }
  if (label_changed) {
    losses.push_back({"label", 1U,
                      "MRC label was sanitized or truncated to 80 ASCII "
                      "characters"});
  }
  if (context.report_progress)
    context.report_progress({1.0, "write-mrc"});
  return operation::Result<SerializedVolume>::success(SerializedVolume{
      std::move(output),
      VolumeWriteReport{VolumeFormat::mrc,
                        shape,
                        model::VolumePrecision::float32,
                        static_cast<std::uint64_t>(grid.value_count()),
                        0U,
                        std::move(losses)}});
}

std::string field_name(std::string value, bool &changed) {
  if (value.empty())
    value = "MolShredder field";
  std::string encoded;
  encoded.reserve(value.size());
  for (const auto character : value) {
    if (character == '"') {
      encoded.push_back('\'');
      changed = true;
    } else if (character == '\\') {
      encoded += "\\\\";
    } else if (character == '\n' || character == '\r' || character == '\0') {
      encoded.push_back(' ');
      changed = true;
    } else {
      encoded.push_back(character);
    }
  }
  return encoded;
}

operation::Result<SerializedVolume>
write_opendx(const model::VolumeGrid &grid, VolumeWriteOptions options,
             operation::TaskContext &context) {
  if (context.cancellation.is_cancelled()) {
    return operation::Result<SerializedVolume>::failure(operation::Error{
        operation::ErrorCode::cancelled, "OpenDX export cancelled", {}});
  }
  bool name_changed{};
  auto name = field_name(std::move(options.name), name_changed);
  const auto &shape = grid.shape();
  const auto &origin = grid.origin();
  const auto &deltas = grid.deltas();
  const auto precision = grid.scalars().precision();

  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::setprecision(
      precision == model::VolumePrecision::float32
          ? std::numeric_limits<float>::max_digits10
          : std::numeric_limits<double>::max_digits10);
  output << "# OpenDX regular scalar grid written by MolShredder\n"
         << "object 1 class gridpositions counts " << shape.x << ' ' << shape.y
         << ' ' << shape.z << '\n'
         << "origin " << origin.x << ' ' << origin.y << ' ' << origin.z << '\n';
  for (const auto delta : deltas)
    output << "delta " << delta.x << ' ' << delta.y << ' ' << delta.z << '\n';
  output << "object 2 class gridconnections counts " << shape.x << ' '
         << shape.y << ' ' << shape.z << '\n'
         << "object 3 class array type "
         << (precision == model::VolumePrecision::float32 ? "float" : "double")
         << " rank 0 items " << grid.value_count() << " data follows\n";

  std::size_t on_line{};
  for (std::size_t index = 0; index < grid.value_count(); ++index) {
    if ((index & 0x3fffU) == 0U &&
        context.cancellation.is_cancelled()) {
      return operation::Result<SerializedVolume>::failure(operation::Error{
          operation::ErrorCode::cancelled,
          "OpenDX export cancelled before scalar " + std::to_string(index),
          {}});
    }
    if (on_line != 0U)
      output << ' ';
    output << grid.scalars().value(index);
    ++on_line;
    if (on_line == 3U || index + 1U == grid.value_count()) {
      output << '\n';
      on_line = 0U;
    }
    if (!output) {
      return operation::Result<SerializedVolume>::failure(
          io_error("failed while writing OpenDX scalar data"));
    }
  }
  output << "attribute \"dep\" string \"positions\"\n"
         << "object \"" << name << "\" class field\n"
         << "component \"positions\" value 1\n"
         << "component \"connections\" value 2\n"
         << "component \"data\" value 3\n"
         << "end\n";
  if (!output) {
    return operation::Result<SerializedVolume>::failure(
        io_error("failed while finalizing OpenDX output"));
  }

  std::vector<VolumeFormatLoss> losses;
  losses.push_back(
      {"coordinate_unit", 1U,
       "OpenDX does not encode the coordinate length unit; numeric grid "
       "coordinates are preserved"});
  if (grid.metadata().scalar_unit.has_value()) {
    losses.push_back({"scalar_unit", 1U,
                      "OpenDX regular scalar syntax does not encode the typed "
                      "scalar unit"});
  }
  std::uint64_t unrepresented_fields{};
  for (const auto &[key, unused] : grid.metadata().fields) {
    static_cast<void>(unused);
    if (key != "format" && key != "ordering" && key != "dependency" &&
        key != "coordinate_unit_source") {
      ++unrepresented_fields;
    }
  }
  if (unrepresented_fields != 0U) {
    losses.push_back({"metadata_fields", unrepresented_fields,
                      "OpenDX output does not preserve auxiliary typed volume "
                      "metadata fields"});
  }
  if (name_changed) {
    losses.push_back({"field_name", 1U,
                      "OpenDX field name control characters or quotes were "
                      "sanitized"});
  }
  auto content = std::move(output).str();
  if (context.report_progress)
    context.report_progress({1.0, "write-opendx"});
  return operation::Result<SerializedVolume>::success(SerializedVolume{
      std::move(content),
      VolumeWriteReport{VolumeFormat::opendx,
                        shape,
                        precision,
                        static_cast<std::uint64_t>(grid.value_count()),
                        0U,
                        std::move(losses)}});
}

std::filesystem::path temporary_path(const std::filesystem::path &target) {
  static std::atomic_uint64_t counter{};
  for (std::size_t attempt = 0; attempt < 1024U; ++attempt) {
    auto candidate = target.parent_path() /
                     (target.filename().string() + ".molshredder.tmp." +
                      std::to_string(counter.fetch_add(1U)));
    std::error_code error;
    if (!std::filesystem::exists(candidate, error) && !error)
      return candidate;
  }
  return {};
}

bool replace_file(const std::filesystem::path &source,
                  const std::filesystem::path &target, bool overwrite) {
#ifdef _WIN32
  const auto flags = overwrite
                         ? MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
                         : MOVEFILE_WRITE_THROUGH;
  return MoveFileExW(source.c_str(), target.c_str(), flags) != 0;
#else
  if (overwrite)
    return std::rename(source.c_str(), target.c_str()) == 0;
  std::error_code error;
  std::filesystem::create_hard_link(source, target, error);
  if (error)
    return false;
  std::filesystem::remove(source, error);
  return true;
#endif
}

} // namespace

operation::Result<SerializedVolume>
serialize_volume(const model::VolumeGrid &grid, VolumeWriteOptions options,
                 operation::TaskContext &context) {
  options.format = resolve_format(options.format);
  if (options.format != VolumeFormat::opendx &&
      options.format != VolumeFormat::mrc) {
    return operation::Result<SerializedVolume>::failure(operation::Error{
        operation::ErrorCode::unsupported,
        "volume writer does not support format: " +
            std::string{to_string(options.format)},
        "use OpenDX or MRC2014 output"});
  }
  auto result = options.format == VolumeFormat::mrc
                    ? write_mrc(grid, std::move(options), context)
                    : write_opendx(grid, std::move(options), context);
  if (result.has_value())
    result.value().report.byte_count = result.value().content.size();
  return result;
}

operation::Result<VolumeWriteReport>
write_volume_file(const std::filesystem::path &path,
                  const model::VolumeGrid &grid, VolumeWriteOptions options,
                  bool overwrite, operation::TaskContext &context) {
  options.format = resolve_format(options.format, path);
  if (options.format == VolumeFormat::auto_detect) {
    return operation::Result<VolumeWriteReport>::failure(invalid(
        "could not infer volume output format from path: " + path.string(),
        "use a .dx/.mrc/.map/.ccp4/.mrcs suffix or an explicit "
        "--file-format"));
  }
  if (path.empty() || path.filename().empty()) {
    return operation::Result<VolumeWriteReport>::failure(
        invalid("volume output path must name a file"));
  }
  std::error_code filesystem_error;
  if (!overwrite && std::filesystem::exists(path, filesystem_error)) {
    return operation::Result<VolumeWriteReport>::failure(
        invalid("volume output already exists: " + path.string(),
                "choose another path or pass --overwrite true"));
  }
  if (filesystem_error) {
    return operation::Result<VolumeWriteReport>::failure(
        io_error("could not inspect volume output path: " + path.string()));
  }
  auto serialized = serialize_volume(grid, std::move(options), context);
  if (!serialized.has_value())
    return operation::Result<VolumeWriteReport>::failure(serialized.error());
  const auto temporary = temporary_path(path);
  if (temporary.empty()) {
    return operation::Result<VolumeWriteReport>::failure(
        io_error("could not allocate a temporary volume output path"));
  }
  std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
  if (!output) {
    return operation::Result<VolumeWriteReport>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "could not create temporary volume output: " + temporary.string(),
        "check directory permissions and free space"});
  }
  output.write(serialized.value().content.data(),
               static_cast<std::streamsize>(serialized.value().content.size()));
  output.flush();
  const auto stream_ok = output.good();
  output.close();
  if (!stream_ok) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return operation::Result<VolumeWriteReport>::failure(io_error(
        "failed while flushing volume output: " + path.string(),
        "check free space and filesystem health"));
  }
  if (!replace_file(temporary, path, overwrite)) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return operation::Result<VolumeWriteReport>::failure(io_error(
        "could not atomically publish volume output: " + path.string(),
        overwrite ? "check target permissions"
                  : "target may have appeared; retry with another path"));
  }
  return operation::Result<VolumeWriteReport>::success(
      std::move(serialized.value().report));
}

} // namespace molshredder::io
