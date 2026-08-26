#include "structure_reader_internal.hpp"

#include <algorithm>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace molshredder::io::detail {
namespace {

using operation::Result;

std::vector<std::string_view> fields(std::string_view line) {
  std::vector<std::string_view> result;
  std::size_t position{};
  while (position < line.size()) {
    while (position < line.size() &&
           std::isspace(static_cast<unsigned char>(line[position])) != 0) {
      ++position;
    }
    if (position == line.size()) break;
    const auto start = position;
    while (position < line.size() &&
           std::isspace(static_cast<unsigned char>(line[position])) == 0) {
      ++position;
    }
    result.push_back(line.substr(start, position - start));
  }
  return result;
}

template <typename Value>
Result<Value> number(std::string_view text, std::string_view source,
                     std::size_t line, std::string_view field_name) {
  Value value{};
  const auto parsed =
      molshredder::core::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    return Result<Value>::failure(parse_error(
        source, line,
        "invalid PQR " + std::string{field_name} + ": " + std::string{text}));
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

std::optional<std::uint8_t> infer_element(std::string_view atom_name,
                                          std::string_view residue_name) {
  while (!atom_name.empty() &&
         std::isdigit(static_cast<unsigned char>(atom_name.front())) != 0) {
    atom_name.remove_prefix(1U);
  }
  if (atom_name.empty() ||
      std::isalpha(static_cast<unsigned char>(atom_name.front())) == 0) {
    return std::nullopt;
  }
  if (atom_name.size() >= 2U &&
      std::isalpha(static_cast<unsigned char>(atom_name[1])) != 0) {
    const auto candidate = atom_name.substr(0U, 2U);
    const auto two_letter = atomic_number(candidate);
    const auto conventional_case =
        std::islower(static_cast<unsigned char>(atom_name[1])) != 0;
    const auto elemental_residue =
        atom_name.size() == 2U &&
        equal_case_insensitive(atom_name, residue_name);
    const auto common_unambiguous =
        equal_case_insensitive(candidate, "CL") ||
        equal_case_insensitive(candidate, "BR") ||
        (atom_name.size() == 2U && equal_case_insensitive(candidate, "FE"));
    if (two_letter.has_value() &&
        (conventional_case || elemental_residue || common_unambiguous)) {
      return two_letter;
    }
  }
  return atomic_number(atom_name.substr(0U, 1U));
}

struct ParsedAtom {
  bool hetero{};
  std::int64_t serial{};
  std::string name;
  std::string residue_name;
  std::string chain_id;
  std::int64_t residue_number{};
  std::uint8_t atomic_number{};
  model::Vec3d position;
  double partial_charge{};
  double radius{};
  std::size_t line{};
};

Result<ParsedAtom> parse_atom(std::string_view line, std::size_t line_number,
                              std::string_view source) {
  const auto columns = fields(line);
  if (columns.size() != 10U && columns.size() != 11U) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line_number,
        "PQR ATOM/HETATM record requires 10 fields without chain ID or 11 fields with chain ID",
        "use whitespace-delimited PQR: record serial atom residue [chain] residue-number x y z charge radius"));
  }
  const auto with_chain = columns.size() == 11U;
  const auto residue_index = with_chain ? 5U : 4U;
  const auto coordinate_index = with_chain ? 6U : 5U;
  const auto serial =
      number<std::int64_t>(columns[1], source, line_number, "atom serial");
  const auto residue_number = number<std::int64_t>(
      columns[residue_index], source, line_number, "residue number");
  const auto x = number<double>(columns[coordinate_index], source, line_number,
                                "x coordinate");
  const auto y = number<double>(columns[coordinate_index + 1U], source,
                                line_number, "y coordinate");
  const auto z = number<double>(columns[coordinate_index + 2U], source,
                                line_number, "z coordinate");
  const auto charge = number<double>(columns[coordinate_index + 3U], source,
                                     line_number, "partial charge");
  const auto radius = number<double>(columns[coordinate_index + 4U], source,
                                     line_number, "radius");
  if (!serial.has_value()) return Result<ParsedAtom>::failure(serial.error());
  if (!residue_number.has_value()) {
    return Result<ParsedAtom>::failure(residue_number.error());
  }
  for (const auto* parsed : {&x, &y, &z, &charge, &radius}) {
    if (!parsed->has_value()) {
      return Result<ParsedAtom>::failure(parsed->error());
    }
  }
  if (!std::isfinite(x.value()) || !std::isfinite(y.value()) ||
      !std::isfinite(z.value()) || !std::isfinite(charge.value()) ||
      !std::isfinite(radius.value()) || radius.value() <= 0.0) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line_number,
        "PQR coordinates and charge must be finite and radius must be finite and positive"));
  }
  const auto element = infer_element(columns[2], columns[3]);
  if (!element.has_value() || element.value() == 0U) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line_number,
        "could not infer an element from PQR atom name: " +
            std::string{columns[2]},
        "use a conventional element-prefixed atom name"));
  }
  return Result<ParsedAtom>::success(ParsedAtom{
      columns[0] == "HETATM", serial.value(), std::string{columns[2]},
      std::string{columns[3]},
      with_chain ? std::string{columns[4]} : std::string{},
      residue_number.value(), element.value(),
      {x.value(), y.value(), z.value()}, charge.value(), radius.value(),
      line_number});
}

std::string_view trim_spaces(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1U);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1U);
  }
  return value;
}

Result<model::UnitCell> parse_cryst1(
    std::string_view line, const std::vector<std::string_view>& columns,
    std::size_t line_number, std::string_view source) {
  if (line.size() < 54U && columns.size() < 7U) {
    return Result<model::UnitCell>::failure(parse_error(
        source, line_number,
        "PQR CRYST1 record requires a, b, c, alpha, beta and gamma"));
  }
  std::vector<double> values;
  values.reserve(6U);
  constexpr std::size_t offsets[]{6U, 15U, 24U, 33U, 40U, 47U};
  constexpr std::size_t widths[]{9U, 9U, 9U, 7U, 7U, 7U};
  for (std::size_t index = 0U; index < 6U; ++index) {
    const auto text = line.size() >= 54U
                          ? trim_spaces(line.substr(offsets[index], widths[index]))
                          : columns[index + 1U];
    const auto value = number<double>(text, source, line_number,
                                      "CRYST1 cell parameter");
    if (!value.has_value()) {
      return Result<model::UnitCell>::failure(value.error());
    }
    values.push_back(value.value());
  }
  return make_unit_cell(values[0], values[1], values[2], values[3], values[4],
                        values[5], source, line_number);
}

std::string structure_name(std::string_view source_name) {
  if (source_name == "<memory>") return "pqr_structure";
  auto name = std::filesystem::path{source_name}.stem().string();
  return name.empty() ? std::string{"pqr_structure"} : name;
}

}  // namespace

Result<StructureDocument> read_pqr(std::string_view content,
                                   std::string source_name) {
  std::vector<ParsedAtom> atoms;
  std::set<std::int64_t> serials;
  std::optional<model::UnitCell> unit_cell;
  std::size_t line_number{};
  std::size_t position{};
  while (position <= content.size()) {
    ++line_number;
    const auto end = content.find('\n', position);
    auto line = content.substr(
        position, end == std::string_view::npos ? content.size() - position
                                                : end - position);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
    const auto columns = fields(line);
    if (!columns.empty() &&
        (columns.front() == "ATOM" || columns.front() == "HETATM")) {
      const auto atom = parse_atom(line, line_number, source_name);
      if (!atom.has_value()) {
        return Result<StructureDocument>::failure(atom.error());
      }
      if (!serials.insert(atom.value().serial).second) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, line_number, "duplicate PQR atom serial"));
      }
      atoms.push_back(atom.value());
    } else if (!columns.empty() && columns.front() == "CRYST1") {
      if (unit_cell.has_value()) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, line_number, "duplicate PQR CRYST1 record"));
      }
      const auto parsed =
          parse_cryst1(line, columns, line_number, source_name);
      if (!parsed.has_value()) {
        return Result<StructureDocument>::failure(parsed.error());
      }
      unit_cell = parsed.value();
    } else if (!columns.empty() &&
               (columns.front() == "MODEL" || columns.front() == "ENDMDL")) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, line_number,
          "multi-model PQR is not supported by the native reader",
          "load one PQR molecule per file"));
    }
    if (end == std::string_view::npos) break;
    position = end + 1U;
  }
  if (atoms.empty()) {
    return Result<StructureDocument>::failure(parse_error(
        source_name, 1U, "PQR input contains no ATOM or HETATM records"));
  }

  model::TopologyBuilder builder;
  std::map<std::tuple<std::string, std::int64_t, std::string>,
           model::ResidueIndex>
      residues;
  model::BooleanColumn hetero;
  std::vector<double> charges;
  std::vector<double> radii;
  std::vector<model::Vec3d> positions;
  for (const auto& atom : atoms) {
    const auto key =
        std::tuple{atom.chain_id, atom.residue_number, atom.residue_name};
    auto residue = residues.find(key);
    if (residue == residues.end()) {
      const auto added = builder.add_residue({atom.residue_name,
                                              atom.residue_number, "",
                                              atom.chain_id, ""});
      if (!added.has_value()) {
        return Result<StructureDocument>::failure(added.error());
      }
      residue = residues.emplace(key, added.value()).first;
    }
    const auto added = builder.add_atom({atom.name, atom.atomic_number,
                                         residue->second, "", 0, atom.serial});
    if (!added.has_value()) {
      return Result<StructureDocument>::failure(added.error());
    }
    hetero.values.push_back(atom.hetero ? 1U : 0U);
    charges.push_back(atom.partial_charge);
    radii.push_back(atom.radius);
    positions.push_back(atom.position);
  }
  for (auto property :
       {std::tuple<std::string, model::AtomPropertyColumn,
                   model::PropertyMetadata>{
            "partial_charge", std::move(charges),
            {"elementary_charge", "PQR charge", {}}},
        {"pqr.radius", std::move(radii), {"angstrom", "PQR radius", {}}},
        {"pqr.is_hetero", std::move(hetero),
         {std::nullopt, "PQR ATOM/HETATM", {}}}}) {
    if (const auto error = builder.add_property(
            std::move(std::get<0>(property)), std::move(std::get<1>(property)),
            std::move(std::get<2>(property)));
        error.has_value()) {
      return Result<StructureDocument>::failure(*error);
    }
  }
  builder.set_source_metadata("format", "pqr");
  builder.set_source_metadata("source_name", source_name);
  const auto topology = builder.build();
  if (!topology.has_value()) {
    return Result<StructureDocument>::failure(topology.error());
  }
  model::FrameMetadata metadata;
  metadata.source_step = 1U;
  metadata.coordinate_unit = operation::LengthUnit::angstrom;
  metadata.unit_cell = unit_cell;
  const auto frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::move(positions)}, std::nullopt, {},
      std::move(metadata));
  if (!frame.has_value()) {
    return Result<StructureDocument>::failure(frame.error());
  }
  const auto coordinates = model::InMemoryCoordinateSource::create(
      topology.value()->atom_count(), {frame.value()});
  if (!coordinates.has_value()) {
    return Result<StructureDocument>::failure(coordinates.error());
  }
  StructureData structure;
  structure.name = structure_name(source_name);
  structure.topology = topology.value();
  structure.coordinates = coordinates.value();
  structure.metadata.emplace("format", "pqr");
  StructureDocument document;
  document.format = StructureFormat::pqr;
  document.source_name = std::move(source_name);
  document.structures.push_back(std::move(structure));
  return Result<StructureDocument>::success(std::move(document));
}

}  // namespace molshredder::io::detail
