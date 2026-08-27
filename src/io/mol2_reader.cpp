#include "structure_reader_internal.hpp"

#include <algorithm>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace molshredder::io::detail {
namespace {

using operation::Result;

struct SourceLine {
  std::string text;
  std::size_t number{};
};

struct Section {
  std::string name;
  std::size_t marker_line{};
  std::vector<SourceLine> lines;
};

struct ParsedAtom {
  std::int64_t id{};
  std::string name;
  model::Vec3d position;
  std::string atom_type;
  std::int64_t substructure_id{1};
  std::string substructure_name{"MOL"};
  double partial_charge{};
  bool partial_charge_present{};
  std::string status_bits;
  std::uint8_t atomic_number{};
};

struct ParsedBond {
  std::int64_t id{};
  std::int64_t first{};
  std::int64_t second{};
  model::BondOrder order{model::BondOrder::unknown};
  bool connected{true};
  std::string status_bits;
};

struct Substructure {
  std::int64_t id{};
  std::string name;
  std::int64_t root_atom{};
  std::string chain_id;
  std::string raw;
};

std::vector<SourceLine> source_lines(std::string_view content) {
  std::vector<SourceLine> result;
  std::size_t position{};
  std::size_t number{1U};
  while (position < content.size()) {
    const auto end = content.find('\n', position);
    auto line = content.substr(
        position, end == std::string_view::npos ? content.size() - position
                                                : end - position);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
    result.push_back({std::string{line}, number++});
    if (end == std::string_view::npos) break;
    position = end + 1U;
  }
  return result;
}

std::vector<std::string_view> tokens(std::string_view line) {
  std::vector<std::string_view> result;
  std::size_t position{};
  while (position < line.size()) {
    while (position < line.size() &&
           std::isspace(static_cast<unsigned char>(line[position])) != 0) {
      ++position;
    }
    if (position == line.size() || line[position] == '#') break;
    const auto first = position;
    while (position < line.size() &&
           std::isspace(static_cast<unsigned char>(line[position])) == 0) {
      ++position;
    }
    result.push_back(line.substr(first, position - first));
  }
  return result;
}

template <typename Value>
Result<Value> number(std::string_view raw, std::string_view source,
                     std::size_t line, std::string_view field_name) {
  Value value{};
  const auto parsed =
      molshredder::core::from_chars(raw.data(), raw.data() + raw.size(), value);
  if (raw.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != raw.data() + raw.size()) {
    return Result<Value>::failure(parse_error(
        source, line,
        "invalid MOL2 " + std::string{field_name} + ": " + std::string{raw}));
  }
  return Result<Value>::success(value);
}

std::string uppercase(std::string_view value) {
  std::string result{value};
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return result;
}

std::optional<std::string> section_marker(std::string_view line) {
  const auto cleaned = trim(line);
  constexpr std::string_view prefix{"@<TRIPOS>"};
  if (cleaned.size() <= prefix.size() ||
      !std::equal(prefix.begin(), prefix.end(), cleaned.begin(),
                  [](char left, char right) {
                    return std::toupper(static_cast<unsigned char>(left)) ==
                           std::toupper(static_cast<unsigned char>(right));
                  })) {
    return std::nullopt;
  }
  return uppercase(std::string_view{cleaned}.substr(prefix.size()));
}

std::vector<SourceLine> data_lines(const Section& section) {
  std::vector<SourceLine> result;
  for (const auto& line : section.lines) {
    const auto cleaned = trim(line.text);
    if (cleaned.empty() || cleaned.front() == '#') continue;
    result.push_back(line);
  }
  return result;
}

std::uint8_t element_from_type(std::string_view atom_type) {
  const auto separator = atom_type.find('.');
  const auto prefix = atom_type.substr(0U, separator);
  const auto parsed = atomic_number(prefix);
  return parsed.value_or(0U);
}

Result<ParsedAtom> parse_atom(const SourceLine& line, std::string_view source,
                              bool no_charges) {
  const auto fields = tokens(line.text);
  if (fields.size() < 6U || fields.size() > 10U || fields.size() == 7U) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line.number,
        "MOL2 atom requires 6 fields or subst_id/subst_name with optional charge/status"));
  }
  const auto id = number<std::int64_t>(fields[0], source, line.number,
                                       "atom identifier");
  const auto x = number<double>(fields[2], source, line.number, "atom x");
  const auto y = number<double>(fields[3], source, line.number, "atom y");
  const auto z = number<double>(fields[4], source, line.number, "atom z");
  if (!id.has_value()) return Result<ParsedAtom>::failure(id.error());
  if (!x.has_value()) return Result<ParsedAtom>::failure(x.error());
  if (!y.has_value()) return Result<ParsedAtom>::failure(y.error());
  if (!z.has_value()) return Result<ParsedAtom>::failure(z.error());
  if (id.value() <= 0 || fields[1].empty() || fields[5].empty() ||
      !std::isfinite(x.value()) || !std::isfinite(y.value()) ||
      !std::isfinite(z.value())) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line.number,
        "MOL2 atom identifier, name, type and coordinates must be valid"));
  }
  ParsedAtom atom;
  atom.id = id.value();
  atom.name = fields[1];
  atom.position = {x.value(), y.value(), z.value()};
  atom.atom_type = fields[5];
  atom.atomic_number = element_from_type(atom.atom_type);
  if (fields.size() >= 8U) {
    const auto substructure = number<std::int64_t>(
        fields[6], source, line.number, "atom substructure identifier");
    if (!substructure.has_value()) {
      return Result<ParsedAtom>::failure(substructure.error());
    }
    if (substructure.value() <= 0 || fields[7].empty()) {
      return Result<ParsedAtom>::failure(parse_error(
          source, line.number,
          "MOL2 atom substructure identifier/name must be positive and non-empty"));
    }
    atom.substructure_id = substructure.value();
    atom.substructure_name = fields[7];
  }
  if (fields.size() >= 9U) {
    const auto charge = number<double>(fields[8], source, line.number,
                                       "atom partial charge");
    if (!charge.has_value()) return Result<ParsedAtom>::failure(charge.error());
    if (!std::isfinite(charge.value())) {
      return Result<ParsedAtom>::failure(parse_error(
          source, line.number, "MOL2 partial charge must be finite"));
    }
    atom.partial_charge = charge.value();
    atom.partial_charge_present = !no_charges;
  }
  if (fields.size() == 10U) atom.status_bits = fields[9];
  return Result<ParsedAtom>::success(std::move(atom));
}

Result<ParsedBond> parse_bond(const SourceLine& line, std::string_view source) {
  const auto fields = tokens(line.text);
  if (fields.size() < 4U || fields.size() > 5U) {
    return Result<ParsedBond>::failure(parse_error(
        source, line.number, "MOL2 bond requires 4 fields and optional status"));
  }
  const auto id = number<std::int64_t>(fields[0], source, line.number,
                                       "bond identifier");
  const auto first = number<std::int64_t>(fields[1], source, line.number,
                                          "bond origin atom");
  const auto second = number<std::int64_t>(fields[2], source, line.number,
                                           "bond target atom");
  if (!id.has_value()) return Result<ParsedBond>::failure(id.error());
  if (!first.has_value()) return Result<ParsedBond>::failure(first.error());
  if (!second.has_value()) return Result<ParsedBond>::failure(second.error());
  if (id.value() <= 0 || first.value() <= 0 || second.value() <= 0 ||
      first.value() == second.value()) {
    return Result<ParsedBond>::failure(parse_error(
        source, line.number, "MOL2 bond identifiers/endpoints must be positive and distinct"));
  }
  model::BondOrder order;
  const auto type = uppercase(fields[3]);
  if (type == "1") {
    order = model::BondOrder::single;
  } else if (type == "2") {
    order = model::BondOrder::double_bond;
  } else if (type == "3") {
    order = model::BondOrder::triple;
  } else if (type == "AR" || type == "4") {
    order = model::BondOrder::aromatic;
  } else if (type == "AM") {
    order = model::BondOrder::amide;
  } else if (type == "NC") {
    order = model::BondOrder::unknown;
  } else {
    return Result<ParsedBond>::failure(operation::Error{
        operation::ErrorCode::unsupported,
        std::string{source} + ":" + std::to_string(line.number) +
            ": unsupported MOL2 dummy/query/unknown bond type: " +
            std::string{fields[3]},
        "use 1, 2, 3, ar/4, am or nc until the generalized bond-kind model is available"});
  }
  return Result<ParsedBond>::success(
      {id.value(), first.value(), second.value(), order, type != "NC",
       fields.size() == 5U ? std::string{fields[4]} : std::string{}});
}

Result<Substructure> parse_substructure(const SourceLine& line,
                                        std::string_view source) {
  const auto fields = tokens(line.text);
  if (fields.size() < 3U) {
    return Result<Substructure>::failure(parse_error(
        source, line.number, "MOL2 substructure requires id, name and root atom"));
  }
  const auto id = number<std::int64_t>(fields[0], source, line.number,
                                       "substructure identifier");
  const auto root = number<std::int64_t>(fields[2], source, line.number,
                                         "substructure root atom");
  if (!id.has_value()) return Result<Substructure>::failure(id.error());
  if (!root.has_value()) return Result<Substructure>::failure(root.error());
  if (id.value() <= 0 || root.value() <= 0 || fields[1].empty()) {
    return Result<Substructure>::failure(parse_error(
        source, line.number,
        "MOL2 substructure id/root/name must be positive and non-empty"));
  }
  std::string chain;
  if (fields.size() >= 6U && fields[5] != "****" && fields[5] != "0") {
    chain = fields[5];
  }
  return Result<Substructure>::success(
      {id.value(), std::string{fields[1]}, root.value(), std::move(chain),
       line.text});
}

std::string join_lines(const std::vector<SourceLine>& lines) {
  std::string result;
  for (const auto& line : lines) {
    if (!result.empty()) result.push_back('\n');
    result += line.text;
  }
  return result;
}

Result<StructureData> parse_record(std::span<const SourceLine> record,
                                   std::string_view source,
                                   std::size_t record_index) {
  std::map<std::string, Section, std::less<>> sections;
  Section* current{};
  for (const auto& line : record) {
    if (const auto marker = section_marker(line.text); marker.has_value()) {
      if (sections.contains(*marker)) {
        return Result<StructureData>::failure(parse_error(
            source, line.number, "duplicate MOL2 section: " + *marker));
      }
      auto [found, inserted] = sections.emplace(
          *marker, Section{*marker, line.number, {}});
      static_cast<void>(inserted);
      current = &found->second;
      continue;
    }
    if (current == nullptr) {
      const auto cleaned = trim(line.text);
      if (!cleaned.empty() && cleaned.front() != '#') {
        return Result<StructureData>::failure(parse_error(
            source, line.number, "content appears before @<TRIPOS>MOLECULE"));
      }
      continue;
    }
    current->lines.push_back(line);
  }
  const auto molecule = sections.find("MOLECULE");
  const auto atoms_section = sections.find("ATOM");
  if (molecule == sections.end() || atoms_section == sections.end()) {
    return Result<StructureData>::failure(parse_error(
        source, record.front().number,
        "MOL2 record requires MOLECULE and ATOM sections"));
  }
  const auto molecule_lines = data_lines(molecule->second);
  if (molecule_lines.size() < 4U) {
    return Result<StructureData>::failure(parse_error(
        source, molecule->second.marker_line,
        "MOL2 MOLECULE section requires name, counts, type and charge type"));
  }
  const auto count_fields = tokens(molecule_lines[1].text);
  if (count_fields.size() < 2U || count_fields.size() > 5U) {
    return Result<StructureData>::failure(parse_error(
        source, molecule_lines[1].number,
        "MOL2 counts line requires 2..5 non-negative integers"));
  }
  std::vector<std::size_t> counts;
  for (const auto value : count_fields) {
    const auto parsed = number<std::size_t>(value, source,
                                            molecule_lines[1].number, "count");
    if (!parsed.has_value()) return Result<StructureData>::failure(parsed.error());
    counts.push_back(parsed.value());
  }
  while (counts.size() < 5U) counts.push_back(0U);
  const auto atom_lines = data_lines(atoms_section->second);
  const auto bonds_found = sections.find("BOND");
  const auto bond_lines = bonds_found == sections.end()
                              ? std::vector<SourceLine>{}
                              : data_lines(bonds_found->second);
  const auto substructures_found = sections.find("SUBSTRUCTURE");
  const auto substructure_lines =
      substructures_found == sections.end()
          ? std::vector<SourceLine>{}
          : data_lines(substructures_found->second);
  if (counts[0] == 0U || atom_lines.size() != counts[0] ||
      bond_lines.size() != counts[1] ||
      substructure_lines.size() != counts[2]) {
    return Result<StructureData>::failure(parse_error(
        source, molecule_lines[1].number,
        "MOL2 declared atom/bond/substructure counts do not match section rows"));
  }
  const auto charge_type = uppercase(trim(molecule_lines[3].text));
  const auto no_charges = charge_type == "NO_CHARGES";
  std::vector<ParsedAtom> atoms;
  std::map<std::int64_t, std::size_t> atom_indices;
  atoms.reserve(atom_lines.size());
  for (const auto& line : atom_lines) {
    const auto parsed = parse_atom(line, source, no_charges);
    if (!parsed.has_value()) return Result<StructureData>::failure(parsed.error());
    if (atom_indices.contains(parsed.value().id)) {
      return Result<StructureData>::failure(parse_error(
          source, line.number, "duplicate MOL2 atom identifier"));
    }
    atom_indices.emplace(parsed.value().id, atoms.size());
    atoms.push_back(parsed.value());
  }
  std::vector<ParsedBond> bonds;
  std::vector<ParsedBond> not_connected;
  std::set<std::int64_t> bond_ids;
  bonds.reserve(bond_lines.size());
  for (const auto& line : bond_lines) {
    const auto parsed = parse_bond(line, source);
    if (!parsed.has_value()) return Result<StructureData>::failure(parsed.error());
    if (!bond_ids.insert(parsed.value().id).second ||
        !atom_indices.contains(parsed.value().first) ||
        !atom_indices.contains(parsed.value().second)) {
      return Result<StructureData>::failure(parse_error(
          source, line.number,
          "MOL2 bond identifier is duplicate or references an unknown atom"));
    }
    if (parsed.value().connected) {
      bonds.push_back(parsed.value());
    } else {
      not_connected.push_back(parsed.value());
    }
  }
  std::map<std::int64_t, Substructure> substructures;
  for (const auto& line : substructure_lines) {
    const auto parsed = parse_substructure(line, source);
    if (!parsed.has_value()) return Result<StructureData>::failure(parsed.error());
    if (!atom_indices.contains(parsed.value().root_atom) ||
        !substructures.emplace(parsed.value().id, parsed.value()).second) {
      return Result<StructureData>::failure(parse_error(
          source, line.number,
          "MOL2 substructure is duplicate or references an unknown root atom"));
    }
  }
  for (const auto& atom : atoms) {
    const auto found = substructures.find(atom.substructure_id);
    if (!substructures.empty() && found == substructures.end()) {
      return Result<StructureData>::failure(parse_error(
          source, atoms_section->second.marker_line,
          "MOL2 atom references an undeclared substructure"));
    }
    if (found != substructures.end() && found->second.name != atom.substructure_name) {
      return Result<StructureData>::failure(parse_error(
          source, atoms_section->second.marker_line,
          "MOL2 atom and SUBSTRUCTURE names disagree"));
    }
  }

  std::optional<model::UnitCell> unit_cell;
  std::string crystal_space_group;
  std::string crystal_setting;
  if (const auto crystal = sections.find("CRYSIN"); crystal != sections.end()) {
    const auto crystal_lines = data_lines(crystal->second);
    if (crystal_lines.size() != 1U) {
      return Result<StructureData>::failure(parse_error(
          source, crystal->second.marker_line,
          "MOL2 CRYSIN section requires exactly one row"));
    }
    const auto fields = tokens(crystal_lines.front().text);
    if (fields.size() < 6U || fields.size() > 8U) {
      return Result<StructureData>::failure(parse_error(
          source, crystal_lines.front().number,
          "MOL2 CRYSIN requires six cell values and optional group/setting"));
    }
    std::vector<double> values;
    for (std::size_t index = 0; index < 6U; ++index) {
      const auto parsed = number<double>(fields[index], source,
                                         crystal_lines.front().number,
                                         "CRYSIN value");
      if (!parsed.has_value()) return Result<StructureData>::failure(parsed.error());
      values.push_back(parsed.value());
    }
    const auto cell = make_unit_cell(values[0], values[1], values[2], values[3],
                                     values[4], values[5], source,
                                     crystal_lines.front().number);
    if (!cell.has_value()) return Result<StructureData>::failure(cell.error());
    unit_cell = cell.value();
    if (fields.size() >= 7U) crystal_space_group = fields[6];
    if (fields.size() == 8U) crystal_setting = fields[7];
  }

  model::TopologyBuilder builder;
  std::map<std::int64_t, model::ResidueIndex> residue_indices;
  for (const auto& atom : atoms) {
    if (residue_indices.contains(atom.substructure_id)) continue;
    const auto found = substructures.find(atom.substructure_id);
    const auto chain = found == substructures.end() ? std::string{}
                                                     : found->second.chain_id;
    const auto residue = builder.add_residue(
        {atom.substructure_name, atom.substructure_id, "", chain, ""});
    if (!residue.has_value()) return Result<StructureData>::failure(residue.error());
    residue_indices.emplace(atom.substructure_id, residue.value());
  }
  std::vector<std::string> atom_types;
  std::vector<std::int64_t> substructure_ids;
  std::vector<std::string> substructure_names;
  std::vector<double> partial_charges;
  model::BooleanColumn partial_charge_present;
  std::vector<std::string> status_bits;
  std::vector<model::Vec3d> positions;
  for (const auto& atom : atoms) {
    const auto added = builder.add_atom(
        {atom.name, atom.atomic_number, residue_indices.at(atom.substructure_id),
         "", 0, atom.id, std::nullopt,
         model::AtomStereoParity::unspecified, model::RadicalState::none,
         false, model::ChemicalAnnotationOrigin::explicit_input});
    if (!added.has_value()) return Result<StructureData>::failure(added.error());
    atom_types.push_back(atom.atom_type);
    substructure_ids.push_back(atom.substructure_id);
    substructure_names.push_back(atom.substructure_name);
    partial_charges.push_back(atom.partial_charge);
    partial_charge_present.values.push_back(atom.partial_charge_present ? 1U : 0U);
    status_bits.push_back(atom.status_bits);
    positions.push_back(atom.position);
  }
  for (std::size_t index = 0; index < bonds.size(); ++index) {
    const auto& bond = bonds[index];
    if (const auto error = builder.add_bond(
            {{atom_indices.at(bond.first)}, {atom_indices.at(bond.second)},
             bond.order, model::BondQuery::none, model::BondStereo::none,
             model::ChemicalAnnotationOrigin::explicit_input});
        error.has_value()) {
      return Result<StructureData>::failure(*error);
    }
    builder.set_source_metadata("mol2.bond_source_id." +
                                    std::to_string(index),
                                std::to_string(bond.id));
    builder.set_source_metadata("mol2.bond_source_first." +
                                    std::to_string(index),
                                std::to_string(bond.first));
    builder.set_source_metadata("mol2.bond_source_second." +
                                    std::to_string(index),
                                std::to_string(bond.second));
    if (!bond.status_bits.empty()) {
      builder.set_source_metadata("mol2.bond_status." + std::to_string(index),
                                  bond.status_bits);
    }
  }
  for (auto property :
       {std::tuple<std::string, model::AtomPropertyColumn,
                   model::PropertyMetadata>{
            "mol2.atom_type", std::move(atom_types),
            {std::nullopt, "MOL2 ATOM atom_type", {}}},
        {"mol2.substructure_id", std::move(substructure_ids),
         {std::nullopt, "MOL2 ATOM subst_id", {}}},
        {"mol2.substructure_name", std::move(substructure_names),
         {std::nullopt, "MOL2 ATOM subst_name", {}}},
        {"partial_charge", std::move(partial_charges),
         {"elementary_charge", "MOL2 ATOM charge", {}}},
        {"partial_charge_present", std::move(partial_charge_present),
         {std::nullopt, "MOL2 charge_type/ATOM charge", {}}},
        {"mol2.status_bits", std::move(status_bits),
         {std::nullopt, "MOL2 ATOM status_bit", {}}}}) {
    if (const auto error = builder.add_property(
            std::move(std::get<0>(property)), std::move(std::get<1>(property)),
            std::move(std::get<2>(property)));
        error.has_value()) {
      return Result<StructureData>::failure(*error);
    }
  }
  builder.set_source_metadata("format", "mol2");
  builder.set_source_metadata("mol2.name", trim(molecule_lines[0].text));
  builder.set_source_metadata("mol2.molecule_type", trim(molecule_lines[2].text));
  builder.set_source_metadata("mol2.charge_type", trim(molecule_lines[3].text));
  if (!not_connected.empty()) {
    builder.set_source_metadata("mol2.not_connected_count",
                                std::to_string(not_connected.size()));
    for (std::size_t index = 0; index < not_connected.size(); ++index) {
      const auto &connection = not_connected[index];
      const auto prefix =
          "mol2.not_connected." + std::to_string(index) + ".";
      builder.set_source_metadata(prefix + "id", std::to_string(connection.id));
      builder.set_source_metadata(prefix + "first",
                                  std::to_string(connection.first));
      builder.set_source_metadata(prefix + "second",
                                  std::to_string(connection.second));
      if (!connection.status_bits.empty()) {
        builder.set_source_metadata(prefix + "status", connection.status_bits);
      }
    }
  }
  if (molecule_lines.size() > 4U) {
    std::vector<SourceLine> extra(molecule_lines.begin() + 4U,
                                  molecule_lines.end());
    builder.set_source_metadata("mol2.molecule_extra", join_lines(extra));
  }
  if (!substructure_lines.empty()) {
    builder.set_source_metadata("mol2.substructure_lines",
                                join_lines(substructure_lines));
  }
  if (!crystal_space_group.empty()) {
    builder.set_source_metadata("mol2.crystal_space_group", crystal_space_group);
  }
  if (!crystal_setting.empty()) {
    builder.set_source_metadata("mol2.crystal_setting", crystal_setting);
  }
  for (const auto& [name, section] : sections) {
    if (name == "MOLECULE" || name == "ATOM" || name == "BOND" ||
        name == "SUBSTRUCTURE" || name == "CRYSIN") {
      continue;
    }
    builder.set_source_metadata("mol2.section." + name,
                                join_lines(section.lines));
  }
  const auto topology = builder.build();
  if (!topology.has_value()) return Result<StructureData>::failure(topology.error());
  model::FrameMetadata metadata;
  metadata.source_step = 0U;
  metadata.coordinate_unit = operation::LengthUnit::angstrom;
  metadata.unit_cell = unit_cell;
  const auto frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::move(positions)}, std::nullopt, {},
      std::move(metadata));
  if (!frame.has_value()) return Result<StructureData>::failure(frame.error());
  const auto coordinates = model::InMemoryCoordinateSource::create(
      topology.value()->atom_count(), {frame.value()});
  if (!coordinates.has_value()) {
    return Result<StructureData>::failure(coordinates.error());
  }
  StructureData structure;
  structure.name = trim(molecule_lines[0].text);
  if (structure.name.empty()) {
    structure.name = "mol2_" + std::to_string(record_index + 1U);
  }
  structure.topology = topology.value();
  structure.coordinates = coordinates.value();
  structure.metadata.emplace("format", "mol2");
  structure.metadata.emplace("mol2.molecule_type",
                             trim(molecule_lines[2].text));
  structure.metadata.emplace("mol2.charge_type",
                             trim(molecule_lines[3].text));
  return Result<StructureData>::success(std::move(structure));
}

}  // namespace

Result<StructureDocument> read_mol2(std::string_view content,
                                    std::string source_name) {
  const auto lines = source_lines(content);
  if (lines.empty()) {
    return Result<StructureDocument>::failure(
        parse_error(source_name, 1U, "MOL2 input is empty"));
  }
  std::vector<std::span<const SourceLine>> records;
  std::optional<std::size_t> start;
  std::optional<std::size_t> first_start;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const auto marker = section_marker(lines[index].text);
    if (!marker.has_value() || *marker != "MOLECULE") continue;
    if (!first_start.has_value()) first_start = index;
    if (start.has_value()) {
      records.emplace_back(lines.data() + *start, index - *start);
    }
    start = index;
  }
  if (!start.has_value()) {
    return Result<StructureDocument>::failure(parse_error(
        source_name, 1U, "MOL2 input has no @<TRIPOS>MOLECULE section"));
  }
  for (std::size_t index = 0; index < *first_start; ++index) {
    const auto cleaned = trim(lines[index].text);
    if (!cleaned.empty() && cleaned.front() != '#') {
      return Result<StructureDocument>::failure(parse_error(
          source_name, lines[index].number,
          "non-comment content appears before @<TRIPOS>MOLECULE"));
    }
  }
  records.emplace_back(lines.data() + *start, lines.size() - *start);
  StructureDocument document;
  document.format = StructureFormat::mol2;
  document.source_name = source_name;
  document.structures.reserve(records.size());
  for (std::size_t index = 0; index < records.size(); ++index) {
    const auto parsed = parse_record(records[index], source_name, index);
    if (!parsed.has_value()) {
      return Result<StructureDocument>::failure(parsed.error());
    }
    document.structures.push_back(parsed.value());
  }
  return Result<StructureDocument>::success(std::move(document));
}

}  // namespace molshredder::io::detail
