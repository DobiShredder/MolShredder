#include "molshredder/io/trajectory_reader.hpp"

#include <algorithm>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "structure_reader_internal.hpp"

namespace molshredder::io {
namespace {

using operation::Result;

operation::Error invalid(const std::filesystem::path &path, std::string message,
                         std::string suggestion = {}) {
  return {operation::ErrorCode::invalid_argument,
          "Amber ASCII trajectory '" + path.string() +
              "': " + std::move(message),
          std::move(suggestion)};
}

Result<double> parse_real(std::string field, const std::filesystem::path &path,
                          std::size_t line_number) {
  std::replace(field.begin(), field.end(), 'D', 'E');
  std::replace(field.begin(), field.end(), 'd', 'e');
  double value{};
  const auto parsed =
      molshredder::core::from_chars(field.data(), field.data() + field.size(), value);
  if (field.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != field.data() + field.size() || !std::isfinite(value)) {
    return Result<double>::failure(
        invalid(path, "line " + std::to_string(line_number) +
                          " contains an invalid fixed-width real: " + field));
  }
  return Result<double>::success(value);
}

Result<std::vector<double>> parse_line(std::string line,
                                       const std::filesystem::path &path,
                                       std::size_t line_number) {
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  if (line.size() > 80U) {
    return Result<std::vector<double>>::failure(
        invalid(path, "line " + std::to_string(line_number) +
                          " exceeds ten Amber F8 fields"));
  }
  std::vector<double> values;
  bool saw_blank{};
  for (std::size_t offset = 0U; offset < line.size(); offset += 8U) {
    auto field = detail::trim(std::string_view{line}.substr(
        offset, std::min<std::size_t>(8U, line.size() - offset)));
    if (field.empty()) {
      saw_blank = true;
      continue;
    }
    if (saw_blank) {
      return Result<std::vector<double>>::failure(
          invalid(path, "line " + std::to_string(line_number) +
                            " has a populated field after a blank F8 field"));
    }
    auto value = parse_real(std::move(field), path, line_number);
    if (!value.has_value()) {
      return Result<std::vector<double>>::failure(value.error());
    }
    values.push_back(value.value());
  }
  if (values.empty()) {
    return Result<std::vector<double>>::failure(
        invalid(path, "line " + std::to_string(line_number) + " is empty"));
  }
  return Result<std::vector<double>>::success(std::move(values));
}

Result<std::vector<double>>
read_coordinate_block(std::istream &input, const std::filesystem::path &path,
                      std::size_t coordinate_count, std::size_t &line_number) {
  std::vector<double> values;
  values.reserve(coordinate_count);
  while (values.size() < coordinate_count) {
    std::string line;
    if (!std::getline(input, line)) {
      return Result<std::vector<double>>::failure(
          invalid(path, "coordinate frame is truncated"));
    }
    ++line_number;
    auto parsed = parse_line(std::move(line), path, line_number);
    if (!parsed.has_value()) {
      return Result<std::vector<double>>::failure(parsed.error());
    }
    const auto remaining = coordinate_count - values.size();
    const auto expected = std::min<std::size_t>(10U, remaining);
    if (parsed.value().size() != expected) {
      return Result<std::vector<double>>::failure(
          invalid(path, "line " + std::to_string(line_number) + " contains " +
                            std::to_string(parsed.value().size()) +
                            " values; the coordinate frame requires " +
                            std::to_string(expected) + " on this line"));
    }
    values.insert(values.end(), parsed.value().begin(), parsed.value().end());
  }
  return Result<std::vector<double>>::success(std::move(values));
}

Result<std::vector<double>> read_box_line(std::istream &input,
                                          const std::filesystem::path &path,
                                          std::size_t box_value_count,
                                          std::size_t &line_number) {
  std::string line;
  if (!std::getline(input, line)) {
    return Result<std::vector<double>>::failure(
        invalid(path, "unit-cell block is truncated"));
  }
  ++line_number;
  auto values = parse_line(std::move(line), path, line_number);
  if (!values.has_value()) {
    return Result<std::vector<double>>::failure(values.error());
  }
  if (values.value().size() != box_value_count) {
    return Result<std::vector<double>>::failure(invalid(
        path, "line " + std::to_string(line_number) + " contains " +
                  std::to_string(values.value().size()) +
                  " box values; expected " + std::to_string(box_value_count)));
  }
  return values;
}

bool remd_header(std::string_view line) {
  return line.starts_with("REMD") || line.starts_with("HREM") ||
         line.starts_with("RXSG");
}

} // namespace

operation::Result<std::shared_ptr<const AmberAsciiCoordinateSource>>
open_amber_ascii_trajectory(const std::filesystem::path &path,
                            std::size_t expected_atom_count,
                            std::optional<double> box_angle_degrees,
                            AmberAsciiBoxLayout box_layout) {
  if (expected_atom_count == 0U ||
      expected_atom_count > std::numeric_limits<std::size_t>::max() / 3U) {
    return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::failure(
        invalid(path,
                "active topology atom count must be positive and addressable"));
  }
  if (box_angle_degrees.has_value() &&
      (!std::isfinite(*box_angle_degrees) || *box_angle_degrees <= 0.0 ||
       *box_angle_degrees >= 180.0)) {
    return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::failure(
        invalid(
            path,
            "topology box angle must be finite and between 0 and 180 degrees"));
  }
  if (box_layout == AmberAsciiBoxLayout::three_lengths &&
      !box_angle_degrees.has_value()) {
    return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::failure(
        invalid(path,
                "explicit CRDBOX requires the topology BOX_DIMENSIONS angle",
                "attach CRDBOX to a matching PRMTOP with BOX_DIMENSIONS"));
  }
  std::ifstream input{path, std::ios::binary};
  if (!input) {
    return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::failure(
        {operation::ErrorCode::not_found,
         "cannot open Amber ASCII trajectory file: " + path.string(),
         {}});
  }
  std::string title;
  if (!std::getline(input, title)) {
    return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::failure(
        invalid(path, "file is missing its title line"));
  }
  if (!title.empty() && title.back() == '\r')
    title.pop_back();
  if (title.size() > 80U) {
    return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::failure(
        invalid(path, "title exceeds 80 characters"));
  }

  const auto coordinate_count = expected_atom_count * 3U;
  std::vector<std::uint64_t> offsets;
  std::size_t box_value_count{
      box_layout == AmberAsciiBoxLayout::three_lengths ? 3U : 0U};
  std::size_t line_number{1U};
  bool first_frame = true;
  while (true) {
    const auto frame_position = input.tellg();
    if (frame_position < std::streampos{}) {
      return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::failure(
          invalid(path, "could not determine a frame offset"));
    }
    if (input.peek() == std::char_traits<char>::eof())
      break;
    std::string header_probe;
    std::getline(input, header_probe);
    input.clear();
    input.seekg(frame_position);
    if (remd_header(header_probe)) {
      return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::failure(
          operation::Error{
              operation::ErrorCode::unsupported,
              "Amber REMD/RXSGLD ASCII trajectory headers are not supported",
              "convert to ordinary mdcrd or NetCDF trajectory"});
    }
    offsets.push_back(static_cast<std::uint64_t>(frame_position));
    auto coordinates =
        read_coordinate_block(input, path, coordinate_count, line_number);
    if (!coordinates.has_value()) {
      return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::failure(
          coordinates.error());
    }
    if (box_layout == AmberAsciiBoxLayout::three_lengths) {
      auto box = read_box_line(input, path, box_value_count, line_number);
      if (!box.has_value()) {
        return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::
            failure(box.error());
      }
      first_frame = false;
    } else if (box_layout == AmberAsciiBoxLayout::coordinate_only) {
      first_frame = false;
    } else if (first_frame) {
      const auto candidate_position = input.tellg();
      if (candidate_position < std::streampos{}) {
        return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::
            failure(invalid(path, "could not inspect the optional box block"));
      }
      std::string candidate;
      if (std::getline(input, candidate)) {
        if (expected_atom_count < 3U) {
          return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::
              failure(operation::Error{
                  operation::ErrorCode::unsupported,
                  "Amber ASCII trajectories with fewer than three atoms are "
                  "ambiguous between a box line and the next frame",
                  "convert the trajectory to DCD, XTC, TRR or NetCDF"});
        }
        auto parsed = parse_line(candidate, path, line_number + 1U);
        if (!parsed.has_value()) {
          return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::
              failure(parsed.error());
        }
        if (parsed.value().size() == 3U || parsed.value().size() == 6U) {
          box_value_count = parsed.value().size();
          ++line_number;
          if (box_value_count == 3U && !box_angle_degrees.has_value()) {
            return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::
                failure(invalid(path,
                                "three-value box lengths require the topology "
                                "BOX_DIMENSIONS angle",
                                "attach the trajectory to a matching PRMTOP "
                                "with BOX_DIMENSIONS or use a six-value box"));
          }
        } else {
          input.clear();
          input.seekg(candidate_position);
        }
      } else {
        input.clear();
      }
      first_frame = false;
    } else if (box_value_count != 0U) {
      auto box = read_box_line(input, path, box_value_count, line_number);
      if (!box.has_value()) {
        return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::
            failure(box.error());
      }
    }
  }
  if (offsets.empty()) {
    return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::failure(
        invalid(path, "trajectory contains no coordinate frame"));
  }
  AmberAsciiTrajectoryMetadata metadata{expected_atom_count, offsets.size(),
                                        box_value_count, std::move(title)};
  return Result<std::shared_ptr<const AmberAsciiCoordinateSource>>::success(
      std::shared_ptr<const AmberAsciiCoordinateSource>{
          new AmberAsciiCoordinateSource{path, std::move(metadata),
                                         std::move(offsets),
                                         box_angle_degrees}});
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
AmberAsciiCoordinateSource::read_frame(std::size_t frame_index) const {
  if (frame_index >= frame_offsets_.size()) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        invalid(path_, "frame index " + std::to_string(frame_index) +
                           " is outside the available range"));
  }
  std::ifstream input{path_, std::ios::binary};
  if (!input) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        {operation::ErrorCode::not_found,
         "cannot reopen Amber ASCII trajectory file: " + path_.string(),
         {}});
  }
  input.seekg(static_cast<std::streamoff>(frame_offsets_[frame_index]));
  if (!input) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        invalid(path_,
                "could not seek to frame " + std::to_string(frame_index)));
  }
  std::size_t line_number{};
  auto coordinates =
      read_coordinate_block(input, path_, atom_count() * 3U, line_number);
  if (!coordinates.has_value()) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        coordinates.error());
  }
  std::vector<model::Vec3d> positions;
  positions.reserve(atom_count());
  for (std::size_t index = 0U; index < coordinates.value().size();
       index += 3U) {
    positions.push_back({coordinates.value()[index],
                         coordinates.value()[index + 1U],
                         coordinates.value()[index + 2U]});
  }
  model::FrameMetadata frame_metadata;
  frame_metadata.coordinate_unit = operation::LengthUnit::angstrom;
  frame_metadata.fields.emplace("format", "amber-mdcrd");
  frame_metadata.fields.emplace("title", metadata_.title);
  frame_metadata.fields.emplace("coordinate_field", "F8");
  if (metadata_.box_value_count != 0U) {
    auto box =
        read_box_line(input, path_, metadata_.box_value_count, line_number);
    if (!box.has_value()) {
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          box.error());
    }
    const auto alpha =
        metadata_.box_value_count == 6U ? box.value()[3U] : *box_angle_degrees_;
    const auto beta =
        metadata_.box_value_count == 6U ? box.value()[4U] : *box_angle_degrees_;
    const auto gamma =
        metadata_.box_value_count == 6U ? box.value()[5U] : *box_angle_degrees_;
    auto cell = detail::make_unit_cell(box.value()[0U], box.value()[1U],
                                       box.value()[2U], alpha, beta, gamma,
                                       path_.string(), frame_index + 1U);
    if (!cell.has_value()) {
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          cell.error());
    }
    frame_metadata.unit_cell = cell.value();
    frame_metadata.fields.emplace("box_angle_source",
                                  metadata_.box_value_count == 3U
                                      ? "prmtop-box-dimensions"
                                      : "trajectory");
  }
  auto frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::move(positions)}, std::nullopt, {},
      std::move(frame_metadata));
  if (!frame.has_value()) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        frame.error());
  }
  return Result<std::shared_ptr<const model::CoordinateFrame>>::success(
      frame.value());
}

} // namespace molshredder::io
