#include "molshredder/io/trajectory_reader.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace molshredder::io {
namespace {

using operation::Result;

operation::Error invalid(const std::filesystem::path &path, std::string message,
                         std::string suggestion = {}) {
  return {operation::ErrorCode::invalid_argument,
          "LAMMPS dump '" + path.string() + "': " + std::move(message),
          std::move(suggestion)};
}

operation::Error unsupported(const std::filesystem::path &path,
                             std::string message, std::string suggestion = {}) {
  return {operation::ErrorCode::unsupported,
          "LAMMPS dump '" + path.string() + "': " + std::move(message),
          std::move(suggestion)};
}

std::vector<std::string_view> split(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t begin{};
  while (begin < line.size()) {
    while (begin < line.size() &&
           (line[begin] == ' ' || line[begin] == '\t' || line[begin] == '\r'))
      ++begin;
    if (begin == line.size())
      break;
    auto end = begin;
    while (end < line.size() && line[end] != ' ' && line[end] != '\t' &&
           line[end] != '\r')
      ++end;
    fields.push_back(line.substr(begin, end - begin));
    begin = end;
  }
  return fields;
}

Result<double> real_value(std::string_view text,
                          const std::filesystem::path &path,
                          std::size_t line_number, std::string_view field) {
  std::string normalized{text};
  std::replace(normalized.begin(), normalized.end(), 'D', 'E');
  std::replace(normalized.begin(), normalized.end(), 'd', 'e');
  double value{};
  const auto parsed = std::from_chars(normalized.data(),
                                      normalized.data() + normalized.size(),
                                      value);
  if (normalized.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != normalized.data() + normalized.size() ||
      !std::isfinite(value)) {
    return Result<double>::failure(invalid(
        path, "line " + std::to_string(line_number) + " has invalid " +
                  std::string{field} + " value '" + normalized + "'"));
  }
  return Result<double>::success(value);
}

Result<std::int64_t> integer_value(std::string_view text,
                                   const std::filesystem::path &path,
                                   std::size_t line_number,
                                   std::string_view field) {
  std::int64_t value{};
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != text.data() + text.size()) {
    return Result<std::int64_t>::failure(invalid(
        path, "line " + std::to_string(line_number) + " has invalid " +
                  std::string{field} + " value '" + std::string{text} + "'"));
  }
  return Result<std::int64_t>::success(value);
}

Result<std::string> line(std::istream &input, const std::filesystem::path &path,
                         std::size_t &line_number, std::string_view purpose) {
  std::string value;
  if (!std::getline(input, value)) {
    return Result<std::string>::failure(
        invalid(path, std::string{purpose} + " is truncated"));
  }
  ++line_number;
  if (!value.empty() && value.back() == '\r')
    value.pop_back();
  return Result<std::string>::success(std::move(value));
}

Result<std::size_t> count_value(std::string_view text,
                                const std::filesystem::path &path,
                                std::size_t line_number) {
  auto parsed = integer_value(text, path, line_number, "atom count");
  if (!parsed.has_value())
    return Result<std::size_t>::failure(parsed.error());
  if (parsed.value() <= 0 ||
      static_cast<std::uint64_t>(parsed.value()) >
          static_cast<std::uint64_t>(
              std::numeric_limits<std::size_t>::max())) {
    return Result<std::size_t>::failure(
        invalid(path, "atom count must be positive and addressable"));
  }
  return Result<std::size_t>::success(
      static_cast<std::size_t>(parsed.value()));
}

struct Box {
  model::UnitCell cell;
  model::Vec3d origin;
  std::string boundary;
  bool restricted_triclinic{};
};

Result<Box> read_box(std::istream &input, const std::filesystem::path &path,
                     std::size_t &line_number, std::string_view header) {
  const auto words = split(header);
  if (words.size() < 3U || words[0] != "ITEM:" || words[1] != "BOX" ||
      words[2] != "BOUNDS") {
    return Result<Box>::failure(
        invalid(path, "line " + std::to_string(line_number) +
                          " must be ITEM: BOX BOUNDS"));
  }
  if (std::find(words.begin() + 3, words.end(), "abc") != words.end() ||
      std::find(words.begin() + 3, words.end(), "origin") != words.end()) {
    return Result<Box>::failure(unsupported(
        path, "general triclinic BOX BOUNDS abc origin is not supported",
        "write a restricted triclinic dump or convert through an explicit "
        "trajectory format"));
  }
  const bool triclinic = words.size() >= 6U && words[3] == "xy" &&
                         words[4] == "xz" && words[5] == "yz";
  std::array<std::array<double, 3U>, 3U> values{};
  for (std::size_t row = 0; row < 3U; ++row) {
    auto read = line(input, path, line_number, "box bounds");
    if (!read.has_value())
      return Result<Box>::failure(read.error());
    const auto fields = split(read.value());
    const auto expected = triclinic ? 3U : 2U;
    if (fields.size() != expected) {
      return Result<Box>::failure(
          invalid(path, "line " + std::to_string(line_number) + " has " +
                            std::to_string(fields.size()) +
                            " box values; expected " +
                            std::to_string(expected)));
    }
    for (std::size_t column = 0; column < expected; ++column) {
      auto parsed = real_value(fields[column], path, line_number, "box");
      if (!parsed.has_value())
        return Result<Box>::failure(parsed.error());
      values[row][column] = parsed.value();
    }
  }
  double xlo = values[0][0];
  double xhi = values[0][1];
  double ylo = values[1][0];
  double yhi = values[1][1];
  const double zlo = values[2][0];
  const double zhi = values[2][1];
  double xy{};
  double xz{};
  double yz{};
  if (triclinic) {
    xy = values[0][2];
    xz = values[1][2];
    yz = values[2][2];
    xlo -= std::min({0.0, xy, xz, xy + xz});
    xhi -= std::max({0.0, xy, xz, xy + xz});
    ylo -= std::min(0.0, yz);
    yhi -= std::max(0.0, yz);
  }
  Box box{{{xhi - xlo, 0.0, 0.0},
           {xy, yhi - ylo, 0.0},
           {xz, yz, zhi - zlo}},
          {xlo, ylo, zlo},
          {},
          triclinic};
  if (!box.cell.is_valid()) {
    return Result<Box>::failure(
        invalid(path, "box vectors must define a finite positive-volume cell"));
  }
  const auto boundary_start = triclinic ? 6U : 3U;
  for (std::size_t index = boundary_start; index < words.size(); ++index) {
    if (!box.boundary.empty())
      box.boundary += ' ';
    box.boundary += words[index];
  }
  return Result<Box>::success(std::move(box));
}

struct ColumnLayout {
  std::size_t id{};
  std::array<std::size_t, 3U> coordinates{};
  std::optional<std::array<std::size_t, 3U>> velocities;
  LammpsCoordinateConvention convention{};
  std::vector<std::size_t> extras;
  std::vector<std::string> names;
};

Result<ColumnLayout> columns(std::string_view header,
                             const std::filesystem::path &path,
                             std::size_t line_number) {
  const auto words = split(header);
  if (words.size() < 3U || words[0] != "ITEM:" || words[1] != "ATOMS") {
    return Result<ColumnLayout>::failure(
        invalid(path, "line " + std::to_string(line_number) +
                          " must be ITEM: ATOMS with column names"));
  }
  ColumnLayout result;
  result.id = std::numeric_limits<std::size_t>::max();
  std::map<std::string, std::size_t, std::less<>> indices;
  for (std::size_t index = 2U; index < words.size(); ++index) {
    std::string name{words[index]};
    if (!indices.emplace(name, index - 2U).second) {
      return Result<ColumnLayout>::failure(
          invalid(path, "duplicate atom column '" + name + "'"));
    }
    result.names.push_back(std::move(name));
  }
  const auto id = indices.find("id");
  if (id == indices.end()) {
    return Result<ColumnLayout>::failure(unsupported(
        path, "atom rows have no id column",
        "include id in dump custom output; row order is not stable across "
        "LAMMPS snapshots"));
  }
  result.id = id->second;
  struct Candidate {
    std::array<std::string_view, 3U> names;
    LammpsCoordinateConvention convention;
  };
  constexpr std::array<Candidate, 4U> candidates{{
      {{{"x", "y", "z"}}, LammpsCoordinateConvention::wrapped_cartesian},
      {{{"xs", "ys", "zs"}}, LammpsCoordinateConvention::scaled_wrapped},
      {{{"xu", "yu", "zu"}}, LammpsCoordinateConvention::unwrapped_cartesian},
      {{{"xsu", "ysu", "zsu"}},
       LammpsCoordinateConvention::scaled_unwrapped},
  }};
  std::size_t matches{};
  for (const auto &candidate : candidates) {
    bool complete = true;
    for (std::size_t axis = 0; axis < 3U; ++axis) {
      const auto found = indices.find(candidate.names[axis]);
      if (found == indices.end()) {
        complete = false;
        break;
      }
      result.coordinates[axis] = found->second;
    }
    if (complete) {
      ++matches;
      result.convention = candidate.convention;
    }
  }
  if (matches == 0U) {
    return Result<ColumnLayout>::failure(unsupported(
        path, "atom rows have no complete x/y/z, xs/ys/zs, xu/yu/zu or "
              "xsu/ysu/zsu coordinate triplet"));
  }
  if (matches > 1U) {
    return Result<ColumnLayout>::failure(unsupported(
        path, "atom rows contain multiple coordinate triplets",
        "write one coordinate convention per dump or convert explicitly"));
  }
  const std::array<std::string_view, 3U> velocity_names{"vx", "vy", "vz"};
  std::array<std::size_t, 3U> velocity_indices{};
  std::size_t velocity_matches{};
  for (std::size_t axis = 0U; axis < velocity_names.size(); ++axis) {
    const auto found = indices.find(velocity_names[axis]);
    if (found != indices.end()) {
      velocity_indices[axis] = found->second;
      ++velocity_matches;
    }
  }
  if (velocity_matches != 0U && velocity_matches != velocity_names.size()) {
    return Result<ColumnLayout>::failure(unsupported(
        path, "atom rows contain an incomplete vx/vy/vz velocity triplet",
        "write all three velocity components or omit all of them"));
  }
  if (velocity_matches == velocity_names.size())
    result.velocities = velocity_indices;
  std::set<std::size_t> reserved{result.id, result.coordinates[0],
                                 result.coordinates[1], result.coordinates[2]};
  for (std::size_t index = 0; index < result.names.size(); ++index) {
    if (!reserved.contains(index))
      result.extras.push_back(index);
  }
  return Result<ColumnLayout>::success(std::move(result));
}

struct ParsedFrame {
  std::uint64_t step{};
  std::optional<double> time;
  std::string units_style;
  Box box;
  ColumnLayout layout;
  std::vector<model::Vec3d> positions;
  std::optional<std::vector<model::Vec3d>> velocities;
  std::map<std::string, std::vector<std::string>, std::less<>> properties;
};

Result<ParsedFrame> parse_frame(
    std::istream &input, const std::filesystem::path &path,
    std::size_t &line_number, std::span<const std::int64_t> source_atom_ids,
    bool retain_payload, std::string inherited_units) {
  auto timestep_header = line(input, path, line_number, "timestep header");
  if (!timestep_header.has_value())
    return Result<ParsedFrame>::failure(timestep_header.error());
  std::optional<double> physical_time;
  bool saw_units{};
  bool saw_time{};
  while (timestep_header.value() != "ITEM: TIMESTEP") {
    if (timestep_header.value() == "ITEM: UNITS") {
      if (saw_units) {
        return Result<ParsedFrame>::failure(
            invalid(path, "one snapshot prefix contains duplicate UNITS"));
      }
      auto units = line(input, path, line_number, "units style");
      if (!units.has_value())
        return Result<ParsedFrame>::failure(units.error());
      const auto fields = split(units.value());
      if (fields.size() != 1U) {
        return Result<ParsedFrame>::failure(
            invalid(path, "UNITS value must contain exactly one style"));
      }
      if (!inherited_units.empty() && inherited_units != fields.front()) {
        return Result<ParsedFrame>::failure(invalid(
            path, "units style changes from '" + inherited_units + "' to '" +
                      std::string{fields.front()} + "' inside one trajectory"));
      }
      inherited_units = std::string{fields.front()};
      saw_units = true;
    } else if (timestep_header.value() == "ITEM: TIME") {
      if (saw_time) {
        return Result<ParsedFrame>::failure(
            invalid(path, "one snapshot prefix contains duplicate TIME"));
      }
      auto time = line(input, path, line_number, "physical time");
      if (!time.has_value())
        return Result<ParsedFrame>::failure(time.error());
      auto parsed_time =
          real_value(time.value(), path, line_number, "physical time");
      if (!parsed_time.has_value())
        return Result<ParsedFrame>::failure(parsed_time.error());
      physical_time = parsed_time.value();
      saw_time = true;
    } else {
      return Result<ParsedFrame>::failure(invalid(
          path, "line " + std::to_string(line_number) +
                    " must be ITEM: UNITS, ITEM: TIME or ITEM: TIMESTEP"));
    }
    timestep_header = line(input, path, line_number, "timestep header");
    if (!timestep_header.has_value())
      return Result<ParsedFrame>::failure(timestep_header.error());
  }
  auto timestep_line = line(input, path, line_number, "timestep value");
  if (!timestep_line.has_value())
    return Result<ParsedFrame>::failure(timestep_line.error());
  auto timestep = integer_value(timestep_line.value(), path, line_number,
                                "timestep");
  if (!timestep.has_value())
    return Result<ParsedFrame>::failure(timestep.error());
  if (timestep.value() < 0) {
    return Result<ParsedFrame>::failure(
        invalid(path, "negative timestep cannot be represented"));
  }
  auto count_header = line(input, path, line_number, "atom-count header");
  if (!count_header.has_value())
    return Result<ParsedFrame>::failure(count_header.error());
  if (count_header.value() != "ITEM: NUMBER OF ATOMS") {
    return Result<ParsedFrame>::failure(
        invalid(path, "line " + std::to_string(line_number) +
                          " must be ITEM: NUMBER OF ATOMS"));
  }
  auto count_line = line(input, path, line_number, "atom count");
  if (!count_line.has_value())
    return Result<ParsedFrame>::failure(count_line.error());
  auto count = count_value(count_line.value(), path, line_number);
  if (!count.has_value())
    return Result<ParsedFrame>::failure(count.error());
  if (count.value() != source_atom_ids.size()) {
    return Result<ParsedFrame>::failure(
        invalid(path, "frame atom count " + std::to_string(count.value()) +
                          " does not match active topology count " +
                          std::to_string(source_atom_ids.size())));
  }
  auto box_header = line(input, path, line_number, "box header");
  if (!box_header.has_value())
    return Result<ParsedFrame>::failure(box_header.error());
  auto box = read_box(input, path, line_number, box_header.value());
  if (!box.has_value())
    return Result<ParsedFrame>::failure(box.error());
  auto atom_header = line(input, path, line_number, "atom header");
  if (!atom_header.has_value())
    return Result<ParsedFrame>::failure(atom_header.error());
  auto layout = columns(atom_header.value(), path, line_number);
  if (!layout.has_value())
    return Result<ParsedFrame>::failure(layout.error());

  std::map<std::int64_t, std::size_t> atom_map;
  for (std::size_t index = 0; index < source_atom_ids.size(); ++index) {
    if (source_atom_ids[index] <= 0 ||
        !atom_map.emplace(source_atom_ids[index], index).second) {
      return Result<ParsedFrame>::failure(invalid(
          path, "active topology source atom IDs must be positive and unique"));
    }
  }
  ParsedFrame frame{static_cast<std::uint64_t>(timestep.value()),
                    physical_time,
                    std::move(inherited_units),
                    std::move(box.value()),
                    std::move(layout.value()),
                    {},
                    std::nullopt,
                    {}};
  std::vector<std::uint8_t> seen(source_atom_ids.size(), 0U);
  if (retain_payload) {
    frame.positions.resize(source_atom_ids.size());
    if (frame.layout.velocities.has_value())
      frame.velocities.emplace(source_atom_ids.size());
    for (const auto extra : frame.layout.extras)
      frame.properties["lammps." + frame.layout.names[extra]].resize(
          source_atom_ids.size());
  }
  for (std::size_t row = 0; row < count.value(); ++row) {
    auto atom_line = line(input, path, line_number, "atom rows");
    if (!atom_line.has_value())
      return Result<ParsedFrame>::failure(atom_line.error());
    const auto fields = split(atom_line.value());
    if (fields.size() != frame.layout.names.size()) {
      return Result<ParsedFrame>::failure(
          invalid(path, "line " + std::to_string(line_number) + " has " +
                            std::to_string(fields.size()) +
                            " atom values; expected " +
                            std::to_string(frame.layout.names.size())));
    }
    auto id = integer_value(fields[frame.layout.id], path, line_number,
                            "atom id");
    if (!id.has_value())
      return Result<ParsedFrame>::failure(id.error());
    const auto mapped = atom_map.find(id.value());
    if (mapped == atom_map.end()) {
      return Result<ParsedFrame>::failure(
          invalid(path, "atom id " + std::to_string(id.value()) +
                            " is absent from the active topology"));
    }
    if (seen[mapped->second] != 0U) {
      return Result<ParsedFrame>::failure(
          invalid(path, "duplicate atom id " + std::to_string(id.value()) +
                            " in one frame"));
    }
    seen[mapped->second] = 1U;
    model::Vec3d coordinate;
    std::array<double *, 3U> axes{&coordinate.x, &coordinate.y,
                                  &coordinate.z};
    for (std::size_t axis = 0; axis < 3U; ++axis) {
      auto value = real_value(fields[frame.layout.coordinates[axis]], path,
                              line_number, "coordinate");
      if (!value.has_value())
        return Result<ParsedFrame>::failure(value.error());
      *axes[axis] = value.value();
    }
    if (frame.layout.convention == LammpsCoordinateConvention::scaled_wrapped ||
        frame.layout.convention ==
            LammpsCoordinateConvention::scaled_unwrapped) {
      const auto scaled = coordinate;
      coordinate = {
          frame.box.origin.x + scaled.x * frame.box.cell.a.x +
              scaled.y * frame.box.cell.b.x + scaled.z * frame.box.cell.c.x,
          frame.box.origin.y + scaled.x * frame.box.cell.a.y +
              scaled.y * frame.box.cell.b.y + scaled.z * frame.box.cell.c.y,
          frame.box.origin.z + scaled.x * frame.box.cell.a.z +
              scaled.y * frame.box.cell.b.z + scaled.z * frame.box.cell.c.z};
    }
    if (retain_payload) {
      frame.positions[mapped->second] = coordinate;
      if (frame.layout.velocities.has_value()) {
        model::Vec3d velocity;
        std::array<double *, 3U> velocity_axes{&velocity.x, &velocity.y,
                                               &velocity.z};
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
          auto value = real_value(
              fields[(*frame.layout.velocities)[axis]], path, line_number,
              "velocity");
          if (!value.has_value())
            return Result<ParsedFrame>::failure(value.error());
          *velocity_axes[axis] = value.value();
        }
        (*frame.velocities)[mapped->second] = velocity;
      }
      for (const auto extra : frame.layout.extras) {
        frame.properties["lammps." + frame.layout.names[extra]][mapped->second] =
            std::string{fields[extra]};
      }
    }
  }
  if (std::find(seen.begin(), seen.end(), 0U) != seen.end()) {
    return Result<ParsedFrame>::failure(
        invalid(path, "frame does not contain every topology atom id"));
  }
  return Result<ParsedFrame>::success(std::move(frame));
}

std::string_view convention_name(LammpsCoordinateConvention convention) {
  switch (convention) {
  case LammpsCoordinateConvention::wrapped_cartesian:
    return "x-y-z";
  case LammpsCoordinateConvention::scaled_wrapped:
    return "xs-ys-zs";
  case LammpsCoordinateConvention::unwrapped_cartesian:
    return "xu-yu-zu";
  case LammpsCoordinateConvention::scaled_unwrapped:
    return "xsu-ysu-zsu";
  case LammpsCoordinateConvention::mixed:
    return "mixed";
  }
  return "mixed";
}

model::AtomProperty property_from_strings(std::vector<std::string> values) {
  std::vector<std::int64_t> integers;
  integers.reserve(values.size());
  bool all_integer = true;
  for (const auto &value : values) {
    std::int64_t parsed{};
    const auto result =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
      all_integer = false;
      break;
    }
    integers.push_back(parsed);
  }
  model::AtomProperty property;
  if (all_integer) {
    property.values = std::move(integers);
  } else {
    std::vector<double> numbers;
    numbers.reserve(values.size());
    bool all_numeric = true;
    for (auto value : values) {
      std::replace(value.begin(), value.end(), 'D', 'E');
      std::replace(value.begin(), value.end(), 'd', 'e');
      double parsed{};
      const auto result =
          std::from_chars(value.data(), value.data() + value.size(), parsed);
      if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
          !std::isfinite(parsed)) {
        all_numeric = false;
        break;
      }
      numbers.push_back(parsed);
    }
    property.values = all_numeric
                          ? model::AtomPropertyColumn{std::move(numbers)}
                          : model::AtomPropertyColumn{std::move(values)};
  }
  property.metadata.source = "lammps-dump-custom";
  property.metadata.annotations["unit"] = "not-encoded-by-dump";
  return property;
}

std::optional<operation::Error>
validate_units_style(const std::filesystem::path &path,
                     std::string_view units_style,
                     operation::LengthUnit coordinate_unit) {
  if (units_style.empty())
    return std::nullopt;
  if ((units_style == "real" || units_style == "metal") &&
      coordinate_unit != operation::LengthUnit::angstrom) {
    return invalid(path, "LAMMPS units style '" + std::string{units_style} +
                             "' requires angstrom coordinates",
                   "set --coordinate-unit angstrom");
  }
  if (units_style == "nano" &&
      coordinate_unit != operation::LengthUnit::nanometer) {
    return invalid(path,
                   "LAMMPS units style 'nano' requires nanometer coordinates",
                   "set --coordinate-unit nanometer");
  }
  if (units_style != "real" && units_style != "metal" &&
      units_style != "nano") {
    return unsupported(
        path, "LAMMPS units style '" + std::string{units_style} +
                  "' cannot be represented by the current length/time model",
        "convert coordinates and time to real, metal or nano units");
  }
  return std::nullopt;
}

std::string real_text(double value) {
  std::array<char, 64U> storage{};
  const auto written = std::to_chars(
      storage.data(), storage.data() + storage.size(), value,
      std::chars_format::general, std::numeric_limits<double>::max_digits10);
  return written.ec == std::errc{}
             ? std::string{storage.data(), written.ptr}
             : std::to_string(value);
}

} // namespace

operation::Result<std::shared_ptr<const LammpsDumpCoordinateSource>>
open_lammps_dump(const std::filesystem::path &path,
                 const std::vector<std::int64_t> &source_atom_ids,
                 operation::LengthUnit coordinate_unit) {
  if (source_atom_ids.empty()) {
    return Result<std::shared_ptr<const LammpsDumpCoordinateSource>>::failure(
        invalid(path, "active topology must expose source atom IDs",
                "load a topology with complete unique source serials before "
                "attaching the dump"));
  }
  std::ifstream input{path, std::ios::binary};
  if (!input) {
    return Result<std::shared_ptr<const LammpsDumpCoordinateSource>>::failure(
        {operation::ErrorCode::not_found,
         "cannot open LAMMPS dump file: " + path.string(), {}});
  }
  std::vector<std::uint64_t> offsets;
  std::vector<std::string> frame_units;
  std::size_t line_number{};
  std::optional<LammpsCoordinateConvention> convention;
  bool mixed_convention{};
  bool has_triclinic{};
  bool has_time{};
  bool has_velocities{};
  std::string units_style;
  while (input.peek() != std::char_traits<char>::eof()) {
    const auto position = input.tellg();
    if (position < std::streampos{}) {
      return Result<std::shared_ptr<const LammpsDumpCoordinateSource>>::failure(
          invalid(path, "could not determine frame offset"));
    }
    offsets.push_back(static_cast<std::uint64_t>(position));
    auto frame = parse_frame(input, path, line_number, source_atom_ids, false,
                             units_style);
    if (!frame.has_value()) {
      return Result<std::shared_ptr<const LammpsDumpCoordinateSource>>::failure(
          frame.error());
    }
    has_triclinic = has_triclinic || frame.value().box.restricted_triclinic;
    has_time = has_time || frame.value().time.has_value();
    has_velocities =
        has_velocities || frame.value().layout.velocities.has_value();
    if (const auto units_error = validate_units_style(
            path, frame.value().units_style, coordinate_unit);
        units_error.has_value()) {
      return Result<std::shared_ptr<const LammpsDumpCoordinateSource>>::failure(
          *units_error);
    }
    units_style = frame.value().units_style;
    frame_units.push_back(units_style);
    if (!convention.has_value())
      convention = frame.value().layout.convention;
    else if (*convention != frame.value().layout.convention)
      mixed_convention = true;
  }
  if (offsets.empty()) {
    return Result<std::shared_ptr<const LammpsDumpCoordinateSource>>::failure(
        invalid(path, "trajectory contains no snapshot"));
  }
  LammpsDumpMetadata metadata{
      source_atom_ids.size(), offsets.size(), has_triclinic, has_time,
      has_velocities,
      mixed_convention ? LammpsCoordinateConvention::mixed : *convention,
      units_style};
  return Result<std::shared_ptr<const LammpsDumpCoordinateSource>>::success(
      std::shared_ptr<const LammpsDumpCoordinateSource>{
          new LammpsDumpCoordinateSource{
              path, metadata, std::move(offsets), std::move(frame_units),
              source_atom_ids, coordinate_unit}});
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
LammpsDumpCoordinateSource::read_frame(std::size_t frame_index) const {
  if (frame_index >= frame_offsets_.size()) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        invalid(path_, "frame index " + std::to_string(frame_index) +
                           " is outside the available range"));
  }
  std::ifstream input{path_, std::ios::binary};
  if (!input) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        {operation::ErrorCode::not_found,
         "cannot reopen LAMMPS dump file: " + path_.string(), {}});
  }
  input.seekg(static_cast<std::streamoff>(frame_offsets_[frame_index]));
  if (!input) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        invalid(path_, "could not seek to indexed frame"));
  }
  std::size_t line_number{};
  auto parsed = parse_frame(input, path_, line_number, source_atom_ids_, true,
                            frame_units_[frame_index]);
  if (!parsed.has_value()) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        parsed.error());
  }
  model::FrameMetadata frame_metadata;
  frame_metadata.source_step = parsed.value().step;
  frame_metadata.unit_cell = parsed.value().box.cell;
  frame_metadata.coordinate_unit = coordinate_unit_;
  frame_metadata.fields["format"] = "lammps-dump";
  frame_metadata.fields["atom_mapping"] = "source-id";
  frame_metadata.fields["coordinate_convention"] =
      convention_name(parsed.value().layout.convention);
  frame_metadata.fields["coordinate_unit_source"] = "user-specified";
  if (!parsed.value().units_style.empty())
    frame_metadata.fields["lammps.units"] = parsed.value().units_style;
  if (parsed.value().time.has_value()) {
    frame_metadata.fields["lammps.time"] = real_text(*parsed.value().time);
    if (parsed.value().units_style == "real") {
      frame_metadata.physical_time =
          model::PhysicalTime{*parsed.value().time,
                              model::TimeUnit::femtosecond};
    } else if (parsed.value().units_style == "metal") {
      frame_metadata.physical_time =
          model::PhysicalTime{*parsed.value().time,
                              model::TimeUnit::picosecond};
    } else if (parsed.value().units_style == "nano") {
      frame_metadata.physical_time =
          model::PhysicalTime{*parsed.value().time * 1000.0,
                              model::TimeUnit::picosecond};
    }
  }
  frame_metadata.fields["box_origin"] =
      std::to_string(parsed.value().box.origin.x) + " " +
      std::to_string(parsed.value().box.origin.y) + " " +
      std::to_string(parsed.value().box.origin.z);
  if (!parsed.value().box.boundary.empty())
    frame_metadata.fields["boundary"] = parsed.value().box.boundary;
  for (auto &[name, values] : parsed.value().properties)
    frame_metadata.atom_properties.emplace(name,
                                           property_from_strings(std::move(values)));
  std::optional<model::CoordinateBuffer> velocities;
  if (parsed.value().velocities.has_value()) {
    if (parsed.value().units_style == "nano") {
      for (auto &velocity : *parsed.value().velocities) {
        velocity.x *= 0.001;
        velocity.y *= 0.001;
        velocity.z *= 0.001;
      }
      frame_metadata.velocity_time_unit = model::TimeUnit::picosecond;
      frame_metadata.fields["velocity_unit_source"] =
          "lammps-nano:nanometer-per-nanosecond-to-nanometer-per-picosecond";
    } else if (parsed.value().units_style == "real") {
      frame_metadata.velocity_time_unit = model::TimeUnit::femtosecond;
      frame_metadata.fields["velocity_unit_source"] =
          "lammps-real:angstrom-per-femtosecond";
    } else if (parsed.value().units_style == "metal") {
      frame_metadata.velocity_time_unit = model::TimeUnit::picosecond;
      frame_metadata.fields["velocity_unit_source"] =
          "lammps-metal:angstrom-per-picosecond";
    } else {
      frame_metadata.fields["velocity_unit_source"] =
          "not-encoded-by-dump";
    }
    velocities.emplace(std::move(*parsed.value().velocities));
  }
  return model::CoordinateFrame::create(
      model::CoordinateBuffer{std::move(parsed.value().positions)},
      std::move(velocities), {}, std::move(frame_metadata));
}

} // namespace molshredder::io
