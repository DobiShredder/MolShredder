#include "structure_reader_internal.hpp"

#include <algorithm>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace molshredder::io::detail {
namespace {

using operation::Result;

struct SourceLine {
  std::string_view text;
  std::size_t number{};
};

class LineCursor {
 public:
  explicit LineCursor(std::string_view content) : content_{content} {}

  [[nodiscard]] std::optional<SourceLine> next() {
    if (position_ >= content_.size()) return std::nullopt;
    const auto start = position_;
    const auto end = content_.find('\n', start);
    position_ = end == std::string_view::npos ? content_.size() : end + 1U;
    auto text = content_.substr(
        start, end == std::string_view::npos ? content_.size() - start
                                             : end - start);
    if (!text.empty() && text.back() == '\r') text.remove_suffix(1U);
    return SourceLine{text, line_++};
  }

  [[nodiscard]] std::optional<SourceLine> next_header() {
    while (const auto line = next()) {
      const auto cleaned = trim(line->text);
      if (!cleaned.empty() && !cleaned.starts_with('#')) return line;
    }
    return std::nullopt;
  }

 private:
  std::string_view content_;
  std::size_t position_{};
  std::size_t line_{1U};
};

template <typename Value>
Result<Value> number(std::string_view text, std::string_view source,
                     std::size_t line, std::string_view field) {
  const auto cleaned = trim(text);
  Value value{};
  const auto parsed = molshredder::core::from_chars(cleaned.data(),
                                      cleaned.data() + cleaned.size(), value);
  if (cleaned.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != cleaned.data() + cleaned.size()) {
    return Result<Value>::failure(parse_error(
        source, line, "invalid G96 " + std::string{field} + ": " + cleaned));
  }
  return Result<Value>::success(value);
}

Result<std::vector<SourceLine>> read_block(LineCursor& cursor,
                                           const SourceLine& header,
                                           std::string_view source) {
  std::vector<SourceLine> lines;
  while (const auto line = cursor.next()) {
    if (trim(line->text) == "END") {
      return Result<std::vector<SourceLine>>::success(std::move(lines));
    }
    lines.push_back(*line);
  }
  return Result<std::vector<SourceLine>>::failure(parse_error(
      source, header.number,
      "G96 " + trim(header.text) + " block is missing END"));
}

std::vector<SourceLine> data_lines(const std::vector<SourceLine>& lines) {
  std::vector<SourceLine> result;
  for (const auto& line : lines) {
    const auto cleaned = trim(line.text);
    if (!cleaned.empty() && !cleaned.starts_with('#')) result.push_back(line);
  }
  return result;
}

bool equal_case_insensitive(std::string_view left, std::string_view right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](unsigned char first, unsigned char second) {
                      return std::toupper(first) == std::toupper(second);
                    });
}

std::uint8_t infer_element(std::string_view atom_name,
                           std::string_view residue_name) {
  while (!atom_name.empty() &&
         std::isdigit(static_cast<unsigned char>(atom_name.front())) != 0) {
    atom_name.remove_prefix(1U);
  }
  if (atom_name.empty() ||
      std::isalpha(static_cast<unsigned char>(atom_name.front())) == 0) {
    return 0U;
  }
  if (atom_name.size() >= 2U &&
      std::isalpha(static_cast<unsigned char>(atom_name[1])) != 0) {
    const auto candidate = atom_name.substr(0U, 2U);
    const auto parsed = atomic_number(candidate);
    const auto conventional_case =
        std::islower(static_cast<unsigned char>(atom_name[1])) != 0;
    const auto elemental_residue =
        atom_name.size() == 2U && equal_case_insensitive(atom_name, residue_name);
    const auto unambiguous = equal_case_insensitive(candidate, "CL") ||
                             equal_case_insensitive(candidate, "BR") ||
                             equal_case_insensitive(candidate, "FE");
    if (parsed.has_value() &&
        (conventional_case || elemental_residue || unambiguous)) {
      return parsed.value();
    }
  }
  return atomic_number(atom_name.substr(0U, 1U)).value_or(0U);
}

struct AtomIdentity {
  std::int64_t residue_number{};
  std::string residue_name;
  std::string atom_name;
  std::int64_t atom_number{};

  friend bool operator==(const AtomIdentity&, const AtomIdentity&) = default;
};

struct ParsedVectorRow {
  std::optional<AtomIdentity> identity;
  model::Vec3d value;
};

Result<model::Vec3d> parse_fixed_vector(std::string_view text,
                                        std::size_t offset,
                                        std::string_view source,
                                        std::size_t line,
                                        std::string_view field) {
  if (text.size() < offset + 45U) {
    return Result<model::Vec3d>::failure(parse_error(
        source, line,
        "G96 " + std::string{field} +
            " row is shorter than three F15.9 fields"));
  }
  const auto x = number<double>(text.substr(offset, 15U), source, line,
                                std::string{field} + " x");
  const auto y = number<double>(text.substr(offset + 15U, 15U), source, line,
                                std::string{field} + " y");
  const auto z = number<double>(text.substr(offset + 30U, 15U), source, line,
                                std::string{field} + " z");
  if (!x.has_value()) return Result<model::Vec3d>::failure(x.error());
  if (!y.has_value()) return Result<model::Vec3d>::failure(y.error());
  if (!z.has_value()) return Result<model::Vec3d>::failure(z.error());
  if (!trim(text.substr(offset + 45U)).empty()) {
    return Result<model::Vec3d>::failure(parse_error(
        source, line, "G96 " + std::string{field} + " row has trailing columns"));
  }
  const model::Vec3d result{x.value(), y.value(), z.value()};
  if (!std::isfinite(result.x) || !std::isfinite(result.y) ||
      !std::isfinite(result.z)) {
    return Result<model::Vec3d>::failure(parse_error(
        source, line, "G96 " + std::string{field} + " must be finite"));
  }
  return Result<model::Vec3d>::success(result);
}

Result<ParsedVectorRow> parse_vector_row(const SourceLine& line, bool reduced,
                                         std::string_view source,
                                         std::string_view field) {
  if (reduced) {
    const auto value =
        parse_fixed_vector(line.text, 0U, source, line.number, field);
    if (!value.has_value()) {
      return Result<ParsedVectorRow>::failure(value.error());
    }
    return Result<ParsedVectorRow>::success({std::nullopt, value.value()});
  }
  if (line.text.size() < 69U) {
    return Result<ParsedVectorRow>::failure(parse_error(
        source, line.number,
        "G96 full " + std::string{field} + " row is shorter than 69 columns"));
  }
  const auto residue_number = number<std::int64_t>(
      line.text.substr(0U, 5U), source, line.number, "residue number");
  const auto atom_number = number<std::int64_t>(
      line.text.substr(17U, 7U), source, line.number, "atom number");
  if (!residue_number.has_value()) {
    return Result<ParsedVectorRow>::failure(residue_number.error());
  }
  if (!atom_number.has_value()) {
    return Result<ParsedVectorRow>::failure(atom_number.error());
  }
  if (residue_number.value() < 0 || atom_number.value() < 0) {
    return Result<ParsedVectorRow>::failure(parse_error(
        source, line.number,
        "G96 residue and atom numbers must be non-negative"));
  }
  const auto residue_name = trim(line.text.substr(6U, 5U));
  const auto atom_name = trim(line.text.substr(12U, 5U));
  if (line.text[5U] != ' ' || line.text[11U] != ' ' ||
      residue_name.empty() || atom_name.empty()) {
    return Result<ParsedVectorRow>::failure(parse_error(
        source, line.number,
        "G96 full row requires space-delimited non-empty five-character names"));
  }
  const auto value =
      parse_fixed_vector(line.text, 24U, source, line.number, field);
  if (!value.has_value()) {
    return Result<ParsedVectorRow>::failure(value.error());
  }
  return Result<ParsedVectorRow>::success(
      {AtomIdentity{residue_number.value(), residue_name, atom_name,
                    atom_number.value()},
       value.value()});
}

Result<std::optional<model::UnitCell>> parse_box(
    const std::vector<SourceLine>& raw_lines, std::string_view source,
    const SourceLine& header) {
  const auto lines = data_lines(raw_lines);
  if (lines.size() != 1U) {
    return Result<std::optional<model::UnitCell>>::failure(parse_error(
        source, header.number, "G96 BOX requires exactly one data row"));
  }
  const auto& row = lines.front();
  std::size_t value_count{};
  if (row.text.size() >= 135U && trim(row.text.substr(135U)).empty()) {
    value_count = 9U;
  } else if (row.text.size() >= 45U &&
             trim(row.text.substr(45U)).empty()) {
    value_count = 3U;
  } else {
    return Result<std::optional<model::UnitCell>>::failure(parse_error(
        source, row.number,
        "G96 BOX row must contain exactly three or nine F15.9 fields"));
  }
  std::vector<double> values;
  values.reserve(value_count);
  for (std::size_t index = 0; index < value_count; ++index) {
    const auto parsed = number<double>(row.text.substr(index * 15U, 15U),
                                       source, row.number, "BOX value");
    if (!parsed.has_value() || !std::isfinite(parsed.value())) {
      return Result<std::optional<model::UnitCell>>::failure(
          parsed.has_value()
              ? parse_error(source, row.number,
                            "G96 BOX values must be finite")
              : parsed.error());
    }
    values.push_back(parsed.value());
  }
  if (std::all_of(values.begin(), values.end(),
                  [](double item) { return item == 0.0; })) {
    return Result<std::optional<model::UnitCell>>::success(std::nullopt);
  }
  if (values.size() == 3U) values.resize(9U, 0.0);
  model::UnitCell cell{{values[0], values[3], values[4]},
                       {values[5], values[1], values[6]},
                       {values[7], values[8], values[2]}};
  if (!cell.is_valid()) {
    return Result<std::optional<model::UnitCell>>::failure(parse_error(
        source, lines.front().number,
        "G96 BOX vectors are degenerate or left-handed"));
  }
  return Result<std::optional<model::UnitCell>>::success(cell);
}

std::string structure_name(std::string_view source_name) {
  if (source_name == "<memory>") return "g96_structure";
  auto name = std::filesystem::path{source_name}.stem().string();
  return name.empty() ? std::string{"g96_structure"} : name;
}

}  // namespace

Result<StructureDocument> read_g96(std::string_view content,
                                   std::string source_name) {
  LineCursor cursor{content};
  const auto title_header = cursor.next_header();
  if (!title_header.has_value() || trim(title_header->text) != "TITLE") {
    return Result<StructureDocument>::failure(parse_error(
        source_name, title_header.has_value() ? title_header->number : 1U,
        "G96 input must begin with a TITLE block"));
  }
  const auto title_block = read_block(cursor, *title_header, source_name);
  if (!title_block.has_value()) {
    return Result<StructureDocument>::failure(title_block.error());
  }
  std::string title;
  for (const auto& line : title_block.value()) {
    if (!title.empty()) title.push_back('\n');
    title.append(line.text);
  }

  std::vector<AtomIdentity> identities;
  std::vector<std::uint8_t> atomic_numbers;
  std::vector<std::shared_ptr<const model::CoordinateFrame>> frames;
  std::optional<SourceLine> header = cursor.next_header();
  while (header.has_value()) {
    model::FrameMetadata metadata;
    metadata.source_step = static_cast<std::uint64_t>(frames.size());
    metadata.coordinate_unit = operation::LengthUnit::nanometer;

    if (trim(header->text) == "TIMESTEP") {
      const auto block = read_block(cursor, *header, source_name);
      if (!block.has_value()) {
        return Result<StructureDocument>::failure(block.error());
      }
      const auto lines = data_lines(block.value());
      if (lines.size() != 1U) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, header->number,
            "G96 TIMESTEP requires exactly one step/time row"));
      }
      std::istringstream stream{std::string{lines.front().text}};
      stream.imbue(std::locale::classic());
      std::int64_t step{};
      double time{};
      std::string extra;
      if (!(stream >> step >> time) || (stream >> extra) || step < 0 ||
          !std::isfinite(time)) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, lines.front().number,
            "G96 TIMESTEP requires one non-negative integer step and finite time"));
      }
      metadata.source_step = static_cast<std::uint64_t>(step);
      metadata.physical_time =
          model::PhysicalTime{time, model::TimeUnit::picosecond};
      header = cursor.next_header();
    }

    if (!header.has_value() ||
        (trim(header->text) != "POSITION" &&
         trim(header->text) != "POSITIONRED")) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, header.has_value() ? header->number : title_header->number,
          "each G96 frame requires POSITION or POSITIONRED after optional TIMESTEP"));
    }
    const bool reduced = trim(header->text) == "POSITIONRED";
    const auto position_header = *header;
    const auto position_block =
        read_block(cursor, position_header, source_name);
    if (!position_block.has_value()) {
      return Result<StructureDocument>::failure(position_block.error());
    }
    const auto position_lines = data_lines(position_block.value());
    if (position_lines.empty()) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, position_header.number,
          "G96 POSITION block must contain at least one atom"));
    }
    std::vector<AtomIdentity> frame_identities;
    std::vector<model::Vec3d> positions;
    frame_identities.reserve(position_lines.size());
    positions.reserve(position_lines.size());
    for (std::size_t index = 0; index < position_lines.size(); ++index) {
      const auto row = parse_vector_row(position_lines[index], reduced,
                                        source_name, "position");
      if (!row.has_value()) return Result<StructureDocument>::failure(row.error());
      positions.push_back(row.value().value);
      if (row.value().identity.has_value()) {
        frame_identities.push_back(*row.value().identity);
      } else if (frames.empty()) {
        frame_identities.push_back(
            {1, "MOL", "X", static_cast<std::int64_t>(index + 1U)});
      } else {
        frame_identities.push_back(identities[index < identities.size() ? index : 0U]);
      }
    }
    if (!frames.empty() && position_lines.size() != identities.size()) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, position_header.number,
          "G96 frame atom count differs from the first frame"));
    }
    if (frames.empty()) {
      identities = frame_identities;
      atomic_numbers.reserve(identities.size());
      for (const auto& identity : identities) {
        atomic_numbers.push_back(
            infer_element(identity.atom_name, identity.residue_name));
      }
    } else if (frame_identities != identities) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, position_header.number,
          "G96 frame atom/residue identity or order differs from the first frame"));
    }
    metadata.fields.emplace("g96.position_kind",
                            reduced ? "POSITIONRED" : "POSITION");

    header = cursor.next_header();
    std::optional<model::CoordinateBuffer> velocity_buffer;
    if (header.has_value() &&
        (trim(header->text) == "VELOCITY" ||
         trim(header->text) == "VELOCITYRED")) {
      const bool velocity_reduced = trim(header->text) == "VELOCITYRED";
      if (velocity_reduced != reduced) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, header->number,
            "G96 POSITION and VELOCITY blocks must use matching full/reduced identity modes"));
      }
      const auto velocity_header = *header;
      const auto velocity_block =
          read_block(cursor, velocity_header, source_name);
      if (!velocity_block.has_value()) {
        return Result<StructureDocument>::failure(velocity_block.error());
      }
      const auto velocity_lines = data_lines(velocity_block.value());
      if (velocity_lines.size() != identities.size()) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, velocity_header.number,
            "G96 VELOCITY atom count differs from POSITION"));
      }
      std::vector<model::Vec3d> velocities;
      velocities.reserve(velocity_lines.size());
      for (std::size_t index = 0; index < velocity_lines.size(); ++index) {
        const auto row = parse_vector_row(velocity_lines[index],
                                          velocity_reduced, source_name,
                                          "velocity");
        if (!row.has_value()) {
          return Result<StructureDocument>::failure(row.error());
        }
        if (row.value().identity.has_value() &&
            *row.value().identity != identities[index]) {
          return Result<StructureDocument>::failure(parse_error(
              source_name, velocity_lines[index].number,
              "G96 VELOCITY identity differs from POSITION"));
        }
        velocities.push_back(row.value().value);
      }
      velocity_buffer = model::CoordinateBuffer{std::move(velocities)};
      metadata.velocity_time_unit = model::TimeUnit::picosecond;
      metadata.fields.emplace("g96.velocity_kind",
                              velocity_reduced ? "VELOCITYRED" : "VELOCITY");
      header = cursor.next_header();
    }
    if (header.has_value() && trim(header->text) == "BOX") {
      const auto box_header = *header;
      const auto box_block = read_block(cursor, box_header, source_name);
      if (!box_block.has_value()) {
        return Result<StructureDocument>::failure(box_block.error());
      }
      const auto cell = parse_box(box_block.value(), source_name, box_header);
      if (!cell.has_value()) {
        return Result<StructureDocument>::failure(cell.error());
      }
      metadata.unit_cell = cell.value();
      header = cursor.next_header();
    }
    const auto frame = model::CoordinateFrame::create(
        model::CoordinateBuffer{std::move(positions)},
        std::move(velocity_buffer), {}, std::move(metadata));
    if (!frame.has_value()) {
      return Result<StructureDocument>::failure(frame.error());
    }
    frames.push_back(frame.value());
    if (header.has_value()) {
      const auto next = trim(header->text);
      if (next != "TIMESTEP" && next != "POSITION" &&
          next != "POSITIONRED") {
        return Result<StructureDocument>::failure(parse_error(
            source_name, header->number,
            "unsupported or out-of-order G96 block: " + next));
      }
    }
  }
  if (frames.empty()) {
    return Result<StructureDocument>::failure(parse_error(
        source_name, title_header->number,
        "G96 input contains no POSITION frames"));
  }

  model::TopologyBuilder builder;
  std::optional<model::ResidueIndex> current_residue;
  std::optional<std::pair<std::int64_t, std::string>> current_identity;
  model::BooleanColumn element_inferred;
  for (std::size_t index = 0; index < identities.size(); ++index) {
    const auto& identity = identities[index];
    const auto residue_identity =
        std::pair{identity.residue_number, identity.residue_name};
    if (!current_identity.has_value() || *current_identity != residue_identity) {
      const auto added = builder.add_residue(
          {identity.residue_name, identity.residue_number, "", "", ""});
      if (!added.has_value()) {
        return Result<StructureDocument>::failure(added.error());
      }
      current_residue = added.value();
      current_identity = residue_identity;
    }
    const auto added = builder.add_atom(
        {identity.atom_name, atomic_numbers[index], *current_residue, "", 0,
         identity.atom_number});
    if (!added.has_value()) {
      return Result<StructureDocument>::failure(added.error());
    }
    element_inferred.values.push_back(atomic_numbers[index] == 0U ? 0U : 1U);
  }
  if (const auto error = builder.add_property(
          "g96.element_inferred", std::move(element_inferred),
          {std::nullopt, "G96 atom/residue name inference", {}});
      error.has_value()) {
    return Result<StructureDocument>::failure(*error);
  }
  builder.set_source_metadata("format", "g96");
  builder.set_source_metadata("g96.title", title);
  const auto topology = builder.build();
  if (!topology.has_value()) {
    return Result<StructureDocument>::failure(topology.error());
  }
  const auto coordinates = model::InMemoryCoordinateSource::create(
      identities.size(), std::move(frames));
  if (!coordinates.has_value()) {
    return Result<StructureDocument>::failure(coordinates.error());
  }
  StructureData structure;
  structure.name = structure_name(source_name);
  structure.topology = topology.value();
  structure.coordinates = coordinates.value();
  structure.metadata.emplace("format", "g96");
  structure.metadata.emplace("g96.title", title);
  StructureDocument document;
  document.format = StructureFormat::g96;
  document.source_name = std::move(source_name);
  document.structures.push_back(std::move(structure));
  return Result<StructureDocument>::success(std::move(document));
}

}  // namespace molshredder::io::detail
