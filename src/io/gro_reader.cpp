#include "structure_reader_internal.hpp"

#include <algorithm>
#include <charconv>
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
  const auto parsed = std::from_chars(cleaned.data(),
                                      cleaned.data() + cleaned.size(), value);
  if (cleaned.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != cleaned.data() + cleaned.size()) {
    return Result<Value>::failure(parse_error(
        source, line,
        "invalid GRO " + std::string{field} + ": " + cleaned));
  }
  return Result<Value>::success(value);
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

struct ParsedAtom {
  AtomIdentity identity;
  std::uint8_t atomic_number{};
  model::Vec3d position;
  std::optional<model::Vec3d> velocity;
};

Result<ParsedAtom> parse_atom(const SourceLine& line, std::string_view source) {
  if (line.text.size() < 44U) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line.number,
        "GRO atom record is shorter than the fixed identity/coordinate fields"));
  }
  const auto residue_number =
      number<std::int64_t>(line.text.substr(0U, 5U), source, line.number,
                           "residue number");
  const auto atom_number =
      number<std::int64_t>(line.text.substr(15U, 5U), source, line.number,
                           "atom number");
  if (!residue_number.has_value()) {
    return Result<ParsedAtom>::failure(residue_number.error());
  }
  if (!atom_number.has_value()) {
    return Result<ParsedAtom>::failure(atom_number.error());
  }
  if (residue_number.value() < 0 || atom_number.value() < 0) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line.number,
        "GRO residue and atom numbers must be non-negative"));
  }
  const auto residue_name = trim(line.text.substr(5U, 5U));
  const auto atom_name = trim(line.text.substr(10U, 5U));
  if (residue_name.empty() || atom_name.empty()) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line.number, "GRO residue and atom names must be non-empty"));
  }

  std::vector<std::size_t> decimal_points;
  for (std::size_t index = 20U; index < line.text.size(); ++index) {
    if (line.text[index] == '.') decimal_points.push_back(index);
  }
  if (decimal_points.size() != 3U && decimal_points.size() != 6U) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line.number,
        "GRO atom record requires three coordinate decimals and optional three velocity decimals"));
  }
  const auto width = decimal_points[1] - decimal_points[0];
  if (width < 6U || width > 20U ||
      decimal_points[2] - decimal_points[1] != width ||
      line.text.size() < 20U + 3U * width) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line.number, "GRO coordinate field width/precision is inconsistent"));
  }
  std::vector<double> values;
  values.reserve(decimal_points.size());
  for (std::size_t index = 0; index < 3U; ++index) {
    const auto parsed = number<double>(
        line.text.substr(20U + index * width, width), source, line.number,
        "coordinate");
    if (!parsed.has_value()) return Result<ParsedAtom>::failure(parsed.error());
    values.push_back(parsed.value());
  }
  std::optional<model::Vec3d> velocity;
  if (decimal_points.size() == 6U) {
    // Velocity fields have one more fractional digit than coordinates while
    // retaining the same total field width, so their decimal point is one
    // column earlier within the field.
    if (decimal_points[3] - decimal_points[2] != width - 1U ||
        decimal_points[4] - decimal_points[3] != width ||
        decimal_points[5] - decimal_points[4] != width ||
        line.text.size() < 20U + 6U * width) {
      return Result<ParsedAtom>::failure(parse_error(
          source, line.number, "GRO velocity field width/precision is inconsistent"));
    }
    for (std::size_t index = 0; index < 3U; ++index) {
      const auto parsed = number<double>(
          line.text.substr(20U + (index + 3U) * width, width), source,
          line.number, "velocity");
      if (!parsed.has_value()) {
        return Result<ParsedAtom>::failure(parsed.error());
      }
      values.push_back(parsed.value());
    }
    velocity = model::Vec3d{values[3], values[4], values[5]};
  }
  const auto represented_end = 20U + decimal_points.size() * width;
  if (!trim(line.text.substr(represented_end)).empty()) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line.number, "GRO atom record contains trailing columns"));
  }
  if (std::any_of(values.begin(), values.end(),
                  [](double value) { return !std::isfinite(value); })) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line.number, "GRO coordinates and velocities must be finite"));
  }
  return Result<ParsedAtom>::success(ParsedAtom{
      {residue_number.value(), residue_name, atom_name, atom_number.value()},
      infer_element(atom_name, residue_name),
      {values[0], values[1], values[2]}, velocity});
}

Result<std::optional<model::UnitCell>> parse_box(const SourceLine& line,
                                                  std::string_view source) {
  std::istringstream stream{std::string{line.text}};
  stream.imbue(std::locale::classic());
  std::vector<double> values;
  double value{};
  while (stream >> value) values.push_back(value);
  if (!stream.eof() || (values.size() != 3U && values.size() != 9U) ||
      std::any_of(values.begin(), values.end(),
                  [](double item) { return !std::isfinite(item); })) {
    return Result<std::optional<model::UnitCell>>::failure(parse_error(
        source, line.number, "GRO box requires three or nine finite values"));
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
        source, line.number, "GRO box vectors are degenerate or left-handed"));
  }
  return Result<std::optional<model::UnitCell>>::success(cell);
}

std::optional<double> title_time(std::string_view title) {
  const auto marker = title.find("t=");
  if (marker == std::string_view::npos) return std::nullopt;
  auto value = title.substr(marker + 2U);
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1U);
  }
  double parsed{};
  const auto result =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec != std::errc{} || result.ptr == value.data() ||
      !std::isfinite(parsed)) {
    return std::nullopt;
  }
  return parsed;
}

std::string structure_name(std::string_view source_name) {
  if (source_name == "<memory>") return "gro_structure";
  auto name = std::filesystem::path{source_name}.stem().string();
  return name.empty() ? std::string{"gro_structure"} : name;
}

}  // namespace

Result<StructureDocument> read_gro(std::string_view content,
                                   std::string source_name) {
  LineCursor cursor{content};
  std::vector<AtomIdentity> identities;
  std::vector<std::uint8_t> atomic_numbers;
  std::vector<std::shared_ptr<const model::CoordinateFrame>> frames;
  std::string first_title;
  while (true) {
    auto title = cursor.next();
    if (!title.has_value()) break;
    const auto count_line = cursor.next();
    if (!count_line.has_value()) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, title->number, "GRO frame is missing its atom count"));
    }
    const auto count = number<std::size_t>(count_line->text, source_name,
                                           count_line->number, "atom count");
    if (!count.has_value()) return Result<StructureDocument>::failure(count.error());
    if (count.value() == 0U) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, count_line->number, "GRO atom count must be positive"));
    }
    std::vector<AtomIdentity> frame_identities;
    std::vector<std::uint8_t> frame_atomic_numbers;
    std::vector<model::Vec3d> positions;
    std::vector<model::Vec3d> velocities;
    bool has_velocities{};
    for (std::size_t index = 0; index < count.value(); ++index) {
      const auto atom_line = cursor.next();
      if (!atom_line.has_value()) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, count_line->number,
            "GRO frame ended before all declared atoms were read"));
      }
      const auto atom = parse_atom(*atom_line, source_name);
      if (!atom.has_value()) return Result<StructureDocument>::failure(atom.error());
      if (index == 0U) has_velocities = atom.value().velocity.has_value();
      if (atom.value().velocity.has_value() != has_velocities) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, atom_line->number,
            "GRO frame mixes atom rows with and without velocities"));
      }
      frame_identities.push_back(atom.value().identity);
      frame_atomic_numbers.push_back(atom.value().atomic_number);
      positions.push_back(atom.value().position);
      if (atom.value().velocity.has_value()) {
        velocities.push_back(*atom.value().velocity);
      }
    }
    const auto box_line = cursor.next();
    if (!box_line.has_value()) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, count_line->number, "GRO frame is missing its box row"));
    }
    const auto box = parse_box(*box_line, source_name);
    if (!box.has_value()) return Result<StructureDocument>::failure(box.error());
    if (frames.empty()) {
      identities = frame_identities;
      atomic_numbers = frame_atomic_numbers;
      first_title = std::string{title->text};
    } else if (frame_identities != identities) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, title->number,
          "GRO frame atom/residue identity or order differs from the first frame",
          "split changing-topology GRO frames or preserve stable atom order"));
    }
    model::FrameMetadata metadata;
    metadata.source_step = static_cast<std::uint64_t>(frames.size());
    metadata.coordinate_unit = operation::LengthUnit::nanometer;
    metadata.unit_cell = box.value();
    metadata.fields.emplace("gro.title", std::string{title->text});
    if (const auto time = title_time(title->text); time.has_value()) {
      metadata.physical_time =
          model::PhysicalTime{*time, model::TimeUnit::picosecond};
    }
    std::optional<model::CoordinateBuffer> velocity_buffer;
    if (has_velocities) {
      velocity_buffer = model::CoordinateBuffer{std::move(velocities)};
      metadata.velocity_time_unit = model::TimeUnit::picosecond;
    }
    const auto frame = model::CoordinateFrame::create(
        model::CoordinateBuffer{std::move(positions)},
        std::move(velocity_buffer), {}, std::move(metadata));
    if (!frame.has_value()) return Result<StructureDocument>::failure(frame.error());
    frames.push_back(frame.value());
  }
  if (frames.empty()) {
    return Result<StructureDocument>::failure(parse_error(
        source_name, 1U, "GRO input contains no frames"));
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
      if (!added.has_value()) return Result<StructureDocument>::failure(added.error());
      current_residue = added.value();
      current_identity = residue_identity;
    }
    const auto added = builder.add_atom(
        {identity.atom_name, atomic_numbers[index], *current_residue, "", 0,
         identity.atom_number});
    if (!added.has_value()) return Result<StructureDocument>::failure(added.error());
    element_inferred.values.push_back(atomic_numbers[index] == 0U ? 0U : 1U);
  }
  if (const auto error = builder.add_property(
          "gro.element_inferred", std::move(element_inferred),
          {std::nullopt, "GRO atom/residue name inference", {}});
      error.has_value()) {
    return Result<StructureDocument>::failure(*error);
  }
  builder.set_source_metadata("format", "gro");
  builder.set_source_metadata("gro.title", first_title);
  const auto topology = builder.build();
  if (!topology.has_value()) return Result<StructureDocument>::failure(topology.error());
  const auto coordinates = model::InMemoryCoordinateSource::create(
      identities.size(), std::move(frames));
  if (!coordinates.has_value()) {
    return Result<StructureDocument>::failure(coordinates.error());
  }
  StructureData structure;
  structure.name = structure_name(source_name);
  structure.topology = topology.value();
  structure.coordinates = coordinates.value();
  structure.metadata.emplace("format", "gro");
  structure.metadata.emplace("gro.title", first_title);
  StructureDocument document;
  document.format = StructureFormat::gro;
  document.source_name = std::move(source_name);
  document.structures.push_back(std::move(structure));
  return Result<StructureDocument>::success(std::move(document));
}

}  // namespace molshredder::io::detail
