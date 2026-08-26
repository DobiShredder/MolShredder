#include "molshredder/io/trajectory_reader.hpp"

#include <algorithm>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "structure_reader_internal.hpp"

namespace molshredder::io {
namespace {

using operation::Result;

operation::Error invalid(const std::filesystem::path &path, std::string message,
                         std::string suggestion = {}) {
  return {operation::ErrorCode::invalid_argument,
          "Amber restart '" + path.string() + "': " + std::move(message),
          std::move(suggestion)};
}

Result<double> real(std::string raw, const std::filesystem::path &path,
                    std::string_view field) {
  std::replace(raw.begin(), raw.end(), 'D', 'E');
  std::replace(raw.begin(), raw.end(), 'd', 'e');
  double value{};
  const auto parsed =
      molshredder::core::from_chars(raw.data(), raw.data() + raw.size(), value);
  if (raw.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != raw.data() + raw.size() || !std::isfinite(value)) {
    return Result<double>::failure(
        invalid(path, "invalid " + std::string{field} + " value: " + raw));
  }
  return Result<double>::success(value);
}

std::vector<std::string> split(std::string_view line) {
  std::istringstream input{std::string{line}};
  std::vector<std::string> result;
  for (std::string value; input >> value;)
    result.push_back(std::move(value));
  return result;
}

Result<std::vector<double>> fixed_values(const std::vector<std::string> &lines,
                                         const std::filesystem::path &path) {
  std::vector<double> result;
  for (const auto &line : lines) {
    for (std::size_t offset = 0U; offset < line.size(); offset += 12U) {
      auto field = detail::trim(std::string_view{line}.substr(
          offset, std::min<std::size_t>(12U, line.size() - offset)));
      if (field.empty())
        continue;
      auto value = real(std::move(field), path, "F12.7");
      if (!value.has_value()) {
        return Result<std::vector<double>>::failure(value.error());
      }
      result.push_back(value.value());
    }
  }
  return Result<std::vector<double>>::success(std::move(result));
}

} // namespace

operation::Result<std::shared_ptr<const model::CoordinateSource>>
open_amber_restart(const std::filesystem::path &path,
                   std::optional<std::size_t> expected_atom_count,
                   AmberRestartMetadata *metadata) {
  std::ifstream input{path};
  if (!input) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        {operation::ErrorCode::not_found,
         "cannot open Amber restart file: " + path.string(),
         {}});
  }
  std::string title;
  std::string header;
  if (!std::getline(input, title) || !std::getline(input, header)) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "file is missing title or NATOM header"));
  }
  if (!title.empty() && title.back() == '\r')
    title.pop_back();
  if (!header.empty() && header.back() == '\r')
    header.pop_back();
  if (title.size() > 80U) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "title exceeds the 80-character Amber field"));
  }
  const auto header_fields = split(header);
  if (header_fields.empty() || header_fields.size() > 3U) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(
            path,
            "NATOM header requires atom count and optional time/temperature"));
  }
  std::uint64_t atom_count64{};
  const auto atom_parsed = molshredder::core::from_chars(
      header_fields[0].data(),
      header_fields[0].data() + header_fields[0].size(), atom_count64);
  if (atom_parsed.ec != std::errc{} ||
      atom_parsed.ptr != header_fields[0].data() + header_fields[0].size() ||
      atom_count64 == 0U ||
      atom_count64 > std::numeric_limits<std::size_t>::max()) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "NATOM must be a positive addressable integer"));
  }
  const auto atom_count = static_cast<std::size_t>(atom_count64);
  if (expected_atom_count.has_value() &&
      expected_atom_count.value() != atom_count) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path,
                "atom count " + std::to_string(atom_count) +
                    " does not match the active topology atom count " +
                    std::to_string(expected_atom_count.value()),
                "attach a restart produced for the active topology"));
  }
  std::optional<double> time;
  std::optional<double> temperature;
  if (header_fields.size() >= 2U) {
    auto value = real(header_fields[1], path, "time");
    if (!value.has_value()) {
      return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
          value.error());
    }
    time = value.value();
  }
  if (header_fields.size() == 3U) {
    auto value = real(header_fields[2], path, "temperature");
    if (!value.has_value()) {
      return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
          value.error());
    }
    temperature = value.value();
  }

  std::vector<std::string> data_lines;
  for (std::string line; std::getline(input, line);) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!detail::trim(line).empty())
      data_lines.push_back(std::move(line));
  }
  auto values = fixed_values(data_lines, path);
  if (!values.has_value()) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        values.error());
  }
  if (atom_count > std::numeric_limits<std::size_t>::max() / 3U) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "coordinate count overflows addressable memory"));
  }
  const auto coordinate_count = atom_count * 3U;
  if (values.value().size() < coordinate_count) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "coordinate block is truncated"));
  }
  const auto trailing = values.value().size() - coordinate_count;
  const bool ambiguous = (trailing == 3U && coordinate_count == 3U) ||
                         (trailing == 6U && coordinate_count == 6U);
  if (ambiguous) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path,
                "small-system trailing values are ambiguous between velocities "
                "and box",
                "use a NetCDF restart or remove the ambiguous optional block"));
  }
  bool has_velocity{};
  std::size_t box_count{};
  if (trailing == 3U || trailing == 6U) {
    box_count = trailing;
  } else if (trailing == coordinate_count) {
    has_velocity = true;
  } else if (trailing == coordinate_count + 3U ||
             trailing == coordinate_count + 6U) {
    has_velocity = true;
    box_count = trailing - coordinate_count;
  } else if (trailing != 0U) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path,
                "trailing block must be velocities and/or 3/6 box values"));
  }

  std::vector<model::Vec3d> positions;
  positions.reserve(atom_count);
  for (std::size_t index = 0U; index < coordinate_count; index += 3U) {
    positions.push_back({values.value()[index], values.value()[index + 1U],
                         values.value()[index + 2U]});
  }
  std::optional<model::CoordinateBuffer> velocities;
  if (has_velocity) {
    constexpr double amber_velocity_scale = 20.455;
    std::vector<model::Vec3d> converted;
    converted.reserve(atom_count);
    for (std::size_t index = coordinate_count; index < coordinate_count * 2U;
         index += 3U) {
      converted.push_back({values.value()[index] * amber_velocity_scale,
                           values.value()[index + 1U] * amber_velocity_scale,
                           values.value()[index + 2U] * amber_velocity_scale});
    }
    velocities.emplace(std::move(converted));
  }
  model::FrameMetadata frame_metadata;
  frame_metadata.coordinate_unit = operation::LengthUnit::angstrom;
  if (time.has_value()) {
    frame_metadata.physical_time =
        model::PhysicalTime{time.value(), model::TimeUnit::picosecond};
  }
  if (temperature.has_value()) {
    frame_metadata.fields.emplace("temperature", std::to_string(*temperature));
    frame_metadata.fields.emplace("temperature_unit", "kelvin");
  }
  if (has_velocity) {
    frame_metadata.velocity_time_unit = model::TimeUnit::picosecond;
    frame_metadata.fields.emplace("velocity_source_unit", "amber-akma");
    frame_metadata.fields.emplace("velocity_scale_to_angstrom_per_ps",
                                  "20.455");
  }
  if (box_count != 0U) {
    const auto offset =
        coordinate_count + (has_velocity ? coordinate_count : 0U);
    const auto alpha = box_count == 6U ? values.value()[offset + 3U] : 90.0;
    const auto beta = box_count == 6U ? values.value()[offset + 4U] : 90.0;
    const auto gamma = box_count == 6U ? values.value()[offset + 5U] : 90.0;
    auto cell = detail::make_unit_cell(
        values.value()[offset], values.value()[offset + 1U],
        values.value()[offset + 2U], alpha, beta, gamma, path.string(), 0U);
    if (!cell.has_value()) {
      return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
          cell.error());
    }
    frame_metadata.unit_cell = cell.value();
  }
  frame_metadata.fields.emplace("title", title);
  frame_metadata.fields.emplace("format", "amber-rst7");
  auto frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::move(positions)}, std::move(velocities), {},
      std::move(frame_metadata));
  if (!frame.has_value()) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        frame.error());
  }
  auto source =
      model::InMemoryCoordinateSource::create(atom_count, {frame.value()});
  if (!source.has_value()) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        source.error());
  }
  if (metadata != nullptr) {
    *metadata = {atom_count,       title,
                 time.has_value(), temperature.has_value(),
                 has_velocity,     box_count != 0U};
  }
  return Result<std::shared_ptr<const model::CoordinateSource>>::success(
      source.value());
}

} // namespace molshredder::io
