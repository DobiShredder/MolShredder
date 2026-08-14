#include "molshredder/io/volume_reader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/operation/error.hpp"
#include "volume_reader_internal.hpp"

namespace molshredder::io {
namespace {

constexpr std::uintmax_t kMaximumVolumeBytes = 512U * 1024U * 1024U;
constexpr std::size_t kMaximumOpenDxVoxels = 100'000'000U;
constexpr std::size_t kMaximumLineBytes = 1024U * 1024U;

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

operation::Error unsupported(std::string message) {
  return operation::Error{operation::ErrorCode::unsupported, std::move(message),
                          "use an ASCII regular scalar OpenDX grid"};
}

operation::Result<std::vector<std::string>> tokens(std::string_view line,
                                                   std::size_t line_number) {
  std::vector<std::string> result;
  std::size_t index{};
  while (index < line.size()) {
    while (index < line.size() &&
           (line[index] == ' ' || line[index] == '\t' || line[index] == '\r')) {
      ++index;
    }
    if (index == line.size() || line[index] == '#')
      break;
    std::string token;
    if (line[index] == '"') {
      ++index;
      bool closed{};
      while (index < line.size()) {
        if (line[index] == '"') {
          ++index;
          closed = true;
          break;
        }
        if (line[index] == '\\' && index + 1U < line.size() &&
            (line[index + 1U] == '\\' || line[index + 1U] == '"')) {
          token.push_back(line[index + 1U]);
          index += 2U;
          continue;
        }
        token.push_back(line[index++]);
      }
      if (!closed) {
        return operation::Result<std::vector<std::string>>::failure(
            invalid("OpenDX line " + std::to_string(line_number) +
                    " contains an unterminated quoted token"));
      }
    } else {
      const auto start = index;
      while (index < line.size() && line[index] != ' ' && line[index] != '\t' &&
             line[index] != '\r' && line[index] != '#') {
        ++index;
      }
      token.assign(line.substr(start, index - start));
    }
    if (!token.empty())
      result.push_back(std::move(token));
    if (index < line.size() && line[index] == '#')
      break;
  }
  return operation::Result<std::vector<std::string>>::success(
      std::move(result));
}

operation::Result<std::size_t> positive_size(std::string_view text,
                                             std::string_view field,
                                             std::size_t line_number) {
  unsigned long long value{};
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
      value == 0U || value > std::numeric_limits<std::size_t>::max()) {
    return operation::Result<std::size_t>::failure(
        invalid("OpenDX line " + std::to_string(line_number) + " has invalid " +
                std::string{field} + ": " + std::string{text}));
  }
  return operation::Result<std::size_t>::success(
      static_cast<std::size_t>(value));
}

operation::Result<double> finite_number(std::string_view text,
                                        std::string_view field,
                                        std::size_t line_number) {
  double value{};
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
      !std::isfinite(value)) {
    return operation::Result<double>::failure(
        invalid("OpenDX line " + std::to_string(line_number) + " has invalid " +
                std::string{field} + ": " + std::string{text}));
  }
  return operation::Result<double>::success(value);
}

std::optional<std::size_t> keyword(const std::vector<std::string> &values,
                                   std::string_view sought,
                                   std::size_t start = 0U) {
  if (start >= values.size())
    return std::nullopt;
  const auto found =
      std::find(values.begin() + static_cast<std::ptrdiff_t>(start),
                values.end(), sought);
  if (found == values.end())
    return std::nullopt;
  return static_cast<std::size_t>(found - values.begin());
}

operation::Result<model::VolumeShape>
counts(const std::vector<std::string> &values, std::size_t offset,
       std::size_t line_number) {
  if (offset + 3U > values.size()) {
    return operation::Result<model::VolumeShape>::failure(
        invalid("OpenDX line " + std::to_string(line_number) +
                " requires three grid counts"));
  }
  const auto x = positive_size(values[offset], "x count", line_number);
  const auto y = positive_size(values[offset + 1U], "y count", line_number);
  const auto z = positive_size(values[offset + 2U], "z count", line_number);
  if (!x.has_value())
    return operation::Result<model::VolumeShape>::failure(x.error());
  if (!y.has_value())
    return operation::Result<model::VolumeShape>::failure(y.error());
  if (!z.has_value())
    return operation::Result<model::VolumeShape>::failure(z.error());
  if (x.value() > std::numeric_limits<std::size_t>::max() / y.value() ||
      x.value() * y.value() >
          std::numeric_limits<std::size_t>::max() / z.value() ||
      x.value() * y.value() * z.value() > kMaximumOpenDxVoxels) {
    return operation::Result<model::VolumeShape>::failure(
        invalid("OpenDX grid exceeds the 100,000,000-voxel in-memory limit"));
  }
  return operation::Result<model::VolumeShape>::success(
      {x.value(), y.value(), z.value()});
}

operation::Result<model::Vec3d> vector3(const std::vector<std::string> &values,
                                        std::size_t offset,
                                        std::string_view field,
                                        std::size_t line_number) {
  if (offset + 3U > values.size()) {
    return operation::Result<model::Vec3d>::failure(
        invalid("OpenDX line " + std::to_string(line_number) +
                " requires three " + std::string{field} + " values"));
  }
  const auto x = finite_number(values[offset], field, line_number);
  const auto y = finite_number(values[offset + 1U], field, line_number);
  const auto z = finite_number(values[offset + 2U], field, line_number);
  if (!x.has_value())
    return operation::Result<model::Vec3d>::failure(x.error());
  if (!y.has_value())
    return operation::Result<model::Vec3d>::failure(y.error());
  if (!z.has_value())
    return operation::Result<model::Vec3d>::failure(z.error());
  return operation::Result<model::Vec3d>::success(
      {x.value(), y.value(), z.value()});
}

operation::Result<VolumeDocument> read_opendx(std::string_view content,
                                              VolumeReadOptions options) {
  if (content.empty()) {
    return operation::Result<VolumeDocument>::failure(
        invalid("OpenDX input is empty"));
  }
  if (content.find('\0') != std::string_view::npos) {
    return operation::Result<VolumeDocument>::failure(
        unsupported("binary OpenDX input is not supported"));
  }

  std::optional<model::VolumeShape> position_shape;
  std::optional<model::VolumeShape> connection_shape;
  std::optional<model::Vec3d> origin;
  std::array<model::Vec3d, 3U> deltas{};
  std::size_t delta_count{};
  std::optional<std::size_t> declared_items;
  std::optional<model::VolumePrecision> precision;
  std::vector<double> values;
  bool reading_data{};
  bool saw_field{};
  bool depends_on_positions{};

  std::size_t line_number{};
  std::size_t cursor{};
  while (cursor <= content.size()) {
    const auto end = content.find('\n', cursor);
    const auto line_end = end == std::string_view::npos ? content.size() : end;
    const auto line = content.substr(cursor, line_end - cursor);
    ++line_number;
    if (line.size() > kMaximumLineBytes) {
      return operation::Result<VolumeDocument>::failure(
          invalid("OpenDX line " + std::to_string(line_number) +
                  " exceeds the 1 MiB line limit"));
    }
    const auto parsed_tokens = tokens(line, line_number);
    if (!parsed_tokens.has_value())
      return operation::Result<VolumeDocument>::failure(parsed_tokens.error());
    const auto &line_tokens = parsed_tokens.value();
    if (!line_tokens.empty()) {
      if (reading_data && values.size() < declared_items.value()) {
        for (const auto &token : line_tokens) {
          if (values.size() == declared_items.value()) {
            return operation::Result<VolumeDocument>::failure(invalid(
                "OpenDX scalar array contains more values than declared"));
          }
          const auto number = finite_number(token, "scalar", line_number);
          if (!number.has_value())
            return operation::Result<VolumeDocument>::failure(number.error());
          values.push_back(number.value());
        }
      } else if (reading_data && declared_items.has_value() &&
                 values.size() == declared_items.value() &&
                 line_tokens[0] != "attribute" && line_tokens[0] != "object" &&
                 line_tokens[0] != "component" && line_tokens[0] != "end") {
        return operation::Result<VolumeDocument>::failure(invalid(
            "OpenDX contains an unexpected record after the scalar array"));
      } else if (line_tokens[0] == "object") {
        const auto class_index = keyword(line_tokens, "class");
        if (!class_index.has_value() ||
            *class_index + 1U >= line_tokens.size()) {
          return operation::Result<VolumeDocument>::failure(
              invalid("OpenDX line " + std::to_string(line_number) +
                      " has an object without a class"));
        }
        const auto &object_class = line_tokens[*class_index + 1U];
        if (object_class == "gridpositions") {
          if (position_shape.has_value()) {
            return operation::Result<VolumeDocument>::failure(unsupported(
                "multiple OpenDX position grids are not supported"));
          }
          const auto count_index =
              keyword(line_tokens, "counts", *class_index + 2U);
          if (!count_index.has_value()) {
            return operation::Result<VolumeDocument>::failure(
                invalid("OpenDX gridpositions object is missing counts"));
          }
          const auto parsed =
              counts(line_tokens, *count_index + 1U, line_number);
          if (!parsed.has_value())
            return operation::Result<VolumeDocument>::failure(parsed.error());
          position_shape = parsed.value();
        } else if (object_class == "gridconnections") {
          if (connection_shape.has_value()) {
            return operation::Result<VolumeDocument>::failure(unsupported(
                "multiple OpenDX connection grids are not supported"));
          }
          const auto count_index =
              keyword(line_tokens, "counts", *class_index + 2U);
          if (!count_index.has_value()) {
            return operation::Result<VolumeDocument>::failure(
                invalid("OpenDX gridconnections object is missing counts"));
          }
          const auto parsed =
              counts(line_tokens, *count_index + 1U, line_number);
          if (!parsed.has_value())
            return operation::Result<VolumeDocument>::failure(parsed.error());
          connection_shape = parsed.value();
        } else if (object_class == "array") {
          if (declared_items.has_value()) {
            return operation::Result<VolumeDocument>::failure(
                unsupported("multiple OpenDX scalar arrays are not supported"));
          }
          const auto header_start = *class_index + 2U;
          const auto type_index = keyword(line_tokens, "type", header_start);
          const auto rank_index = keyword(line_tokens, "rank", header_start);
          const auto items_index = keyword(line_tokens, "items", header_start);
          const auto data_index = keyword(line_tokens, "data", header_start);
          if (!type_index.has_value() ||
              *type_index + 1U >= line_tokens.size() ||
              !rank_index.has_value() ||
              *rank_index + 1U >= line_tokens.size() ||
              !items_index.has_value() ||
              *items_index + 1U >= line_tokens.size() ||
              !data_index.has_value() ||
              *data_index + 1U >= line_tokens.size() ||
              line_tokens[*rank_index + 1U] != "0" ||
              line_tokens[*data_index + 1U] != "follows") {
            return operation::Result<VolumeDocument>::failure(unsupported(
                "OpenDX array must be a rank-0 inline scalar data array"));
          }
          if (line_tokens[*type_index + 1U] == "float") {
            precision = model::VolumePrecision::float32;
          } else if (line_tokens[*type_index + 1U] == "double") {
            precision = model::VolumePrecision::float64;
          } else {
            return operation::Result<VolumeDocument>::failure(
                unsupported("OpenDX scalar type must be float or double"));
          }
          const auto item_count = positive_size(
              line_tokens[*items_index + 1U], "array item count", line_number);
          if (!item_count.has_value())
            return operation::Result<VolumeDocument>::failure(
                item_count.error());
          if (item_count.value() > kMaximumOpenDxVoxels) {
            return operation::Result<VolumeDocument>::failure(invalid(
                "OpenDX array exceeds the 100,000,000-value in-memory limit"));
          }
          declared_items = item_count.value();
          values.reserve(item_count.value());
          reading_data = true;
          for (std::size_t index = *data_index + 2U; index < line_tokens.size();
               ++index) {
            if (values.size() == declared_items.value()) {
              return operation::Result<VolumeDocument>::failure(invalid(
                  "OpenDX scalar array contains more values than declared"));
            }
            const auto number =
                finite_number(line_tokens[index], "scalar", line_number);
            if (!number.has_value())
              return operation::Result<VolumeDocument>::failure(number.error());
            values.push_back(number.value());
          }
        } else if (object_class == "field") {
          if (saw_field) {
            return operation::Result<VolumeDocument>::failure(
                unsupported("multiple OpenDX field objects are not supported"));
          }
          saw_field = true;
        }
      } else if (line_tokens[0] == "origin") {
        if (origin.has_value()) {
          return operation::Result<VolumeDocument>::failure(
              invalid("OpenDX contains duplicate origin records"));
        }
        const auto parsed = vector3(line_tokens, 1U, "origin", line_number);
        if (!parsed.has_value())
          return operation::Result<VolumeDocument>::failure(parsed.error());
        origin = parsed.value();
      } else if (line_tokens[0] == "delta") {
        if (delta_count == deltas.size()) {
          return operation::Result<VolumeDocument>::failure(
              invalid("OpenDX contains more than three delta vectors"));
        }
        const auto parsed = vector3(line_tokens, 1U, "delta", line_number);
        if (!parsed.has_value())
          return operation::Result<VolumeDocument>::failure(parsed.error());
        deltas[delta_count++] = parsed.value();
      } else if (line_tokens[0] == "attribute" && line_tokens.size() >= 4U &&
                 line_tokens[1] == "dep" && line_tokens[2] == "string" &&
                 line_tokens[3] == "positions") {
        depends_on_positions = true;
      }
    }
    if (end == std::string_view::npos)
      break;
    cursor = end + 1U;
  }

  if (!position_shape.has_value() || !connection_shape.has_value() ||
      !origin.has_value() || delta_count != 3U || !declared_items.has_value() ||
      !precision.has_value()) {
    return operation::Result<VolumeDocument>::failure(invalid(
        "OpenDX regular scalar grid is missing positions, origin, three "
        "deltas, connections, or array data"));
  }
  if (*position_shape != *connection_shape) {
    return operation::Result<VolumeDocument>::failure(
        invalid("OpenDX gridpositions and gridconnections counts differ"));
  }
  const auto expected =
      position_shape->x * position_shape->y * position_shape->z;
  if (*declared_items != expected || values.size() != expected) {
    return operation::Result<VolumeDocument>::failure(
        invalid("OpenDX scalar item count does not match the grid dimensions"));
  }
  model::VolumeMetadata metadata;
  metadata.coordinate_unit = options.coordinate_unit;
  metadata.fields.emplace("format", "opendx");
  metadata.fields.emplace("ordering", "z_fastest");
  metadata.fields.emplace("dependency",
                          depends_on_positions ? "positions" : "unspecified");
  metadata.fields.emplace("coordinate_unit_source", "user_or_apbs_default");

  std::shared_ptr<const model::VolumeGrid> grid;
  if (*precision == model::VolumePrecision::float32) {
    std::vector<float> converted;
    converted.reserve(values.size());
    for (const auto value : values) {
      const auto narrowed = static_cast<float>(value);
      if (!std::isfinite(narrowed)) {
        return operation::Result<VolumeDocument>::failure(
            invalid("OpenDX float scalar is outside the finite float32 range"));
      }
      converted.push_back(narrowed);
    }
    const auto built = model::VolumeGrid::create(
        *position_shape, *origin, deltas,
        model::VolumeScalarBuffer{std::move(converted)}, std::move(metadata));
    if (!built.has_value())
      return operation::Result<VolumeDocument>::failure(built.error());
    grid = built.value();
  } else {
    const auto built = model::VolumeGrid::create(
        *position_shape, *origin, deltas,
        model::VolumeScalarBuffer{std::move(values)}, std::move(metadata));
    if (!built.has_value())
      return operation::Result<VolumeDocument>::failure(built.error());
    grid = built.value();
  }
  auto name = options.name.value_or("OpenDX field");
  if (name.empty()) {
    return operation::Result<VolumeDocument>::failure(
        invalid("volume name must not be empty"));
  }
  return operation::Result<VolumeDocument>::success(VolumeDocument{
      VolumeFormat::opendx, {VolumeData{std::move(name), std::move(grid)}}});
}

VolumeFormat detect(std::string_view content) {
  if (detail::is_mrc(content))
    return VolumeFormat::mrc;
  return content.find("class gridpositions") != std::string_view::npos &&
                 content.find("class gridconnections") != std::string_view::npos
             ? VolumeFormat::opendx
             : VolumeFormat::auto_detect;
}

} // namespace

operation::Result<VolumeDocument> read_volume(std::string_view content,
                                              VolumeReadOptions options) {
  const auto format = options.format == VolumeFormat::auto_detect
                          ? detect(content)
                          : options.format;
  if (format == VolumeFormat::mrc) {
    options.format = format;
    return detail::read_mrc(content, std::move(options));
  }
  if (format != VolumeFormat::opendx) {
    return operation::Result<VolumeDocument>::failure(
        unsupported("volume format could not be detected from the input"));
  }
  options.format = format;
  return read_opendx(content, std::move(options));
}

operation::Result<VolumeDocument>
read_volume_file(const std::filesystem::path &path, VolumeReadOptions options) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    return operation::Result<VolumeDocument>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "cannot inspect volume file: " + path.string(),
                         "verify that the path exists and is readable"});
  }
  if (size > kMaximumVolumeBytes) {
    return operation::Result<VolumeDocument>::failure(
        invalid("volume file exceeds the 512 MiB in-memory reader limit"));
  }
  if (options.format == VolumeFormat::auto_detect) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                     return static_cast<char>(std::tolower(value));
                   });
    if (extension == ".dx")
      options.format = VolumeFormat::opendx;
    else if (extension == ".mrc" || extension == ".ccp4" ||
             extension == ".mrcs")
      options.format = VolumeFormat::mrc;
  }
  if (!options.name.has_value())
    options.name = path.stem().string();
  std::ifstream input{path, std::ios::binary};
  if (!input) {
    return operation::Result<VolumeDocument>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "cannot open volume file: " + path.string(),
                         "verify that the path exists and is readable"});
  }
  std::string content{std::istreambuf_iterator<char>{input}, {}};
  if (!input.good() && !input.eof()) {
    return operation::Result<VolumeDocument>::failure(
        invalid("failed while reading volume file: " + path.string()));
  }
  return read_volume(content, std::move(options));
}

std::string_view to_string(VolumeFormat format) noexcept {
  switch (format) {
  case VolumeFormat::opendx:
    return "opendx";
  case VolumeFormat::mrc:
    return "mrc";
  case VolumeFormat::auto_detect:
    return "auto";
  }
  return "auto";
}

} // namespace molshredder::io
