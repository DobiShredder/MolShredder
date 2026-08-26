#include "structure_reader_internal.hpp"

#include <algorithm>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace molshredder::io::detail {
namespace {

using model::AtomIndex;
using model::AtomRecord;
using model::Bond;
using model::BondOrder;
using model::BooleanColumn;
using model::CoordinateBuffer;
using model::CoordinateFrame;
using model::FrameMetadata;
using model::PropertyMetadata;
using model::ResidueIndex;
using model::ResidueRecord;
using model::TopologyBuilder;
using model::Vec3d;
using operation::Result;

std::string_view field(std::string_view line, std::size_t first,
                       std::size_t count) {
  if (first >= line.size()) {
    return {};
  }
  return line.substr(first, std::min(count, line.size() - first));
}

template <typename Value>
Result<Value> parse_number(std::string_view raw, std::string_view source,
                           std::size_t line, std::string_view name) {
  const auto cleaned = trim(raw);
  if (cleaned.empty()) {
    return Result<Value>::failure(parse_error(
        source, line, "missing PDB field " + std::string{name}));
  }
  Value value{};
  const auto result =
      molshredder::core::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), value);
  if (result.ec != std::errc{} || result.ptr != cleaned.data() + cleaned.size()) {
    return Result<Value>::failure(parse_error(
        source, line,
        "invalid numeric PDB field " + std::string{name} + ": " + cleaned));
  }
  return Result<Value>::success(value);
}

template <typename Value>
Result<std::optional<Value>> parse_optional_number(std::string_view raw,
                                                   std::string_view source,
                                                   std::size_t line,
                                                   std::string_view name) {
  if (trim(raw).empty()) {
    return Result<std::optional<Value>>::success(std::nullopt);
  }
  const auto parsed = parse_number<Value>(raw, source, line, name);
  if (!parsed.has_value()) {
    return Result<std::optional<Value>>::failure(parsed.error());
  }
  return Result<std::optional<Value>>::success(parsed.value());
}

struct AtomIdentity {
  std::int64_t serial{};
  std::string atom_name;
  std::string alternate_location;
  std::string residue_name;
  std::string chain_id;
  std::int64_t residue_number{};
  std::string insertion_code;
  std::string segment_id;

  friend auto operator<=>(const AtomIdentity&, const AtomIdentity&) = default;
};

struct ParsedAtom {
  AtomIdentity identity;
  bool hetero{};
  std::uint8_t atomic_number{};
  std::int32_t formal_charge{};
  Vec3d position;
  std::optional<double> occupancy;
  std::optional<double> b_factor;
  std::size_t line{};
};

struct ParsedModel {
  std::uint64_t number{};
  std::vector<ParsedAtom> atoms;
};

struct ParsedConnection {
  std::int64_t source{};
  std::int64_t target{};
  std::size_t line{};
};

std::optional<std::uint8_t> pdb_element(std::string_view explicit_symbol,
                                        std::string_view atom_name_field) {
  if (!trim(explicit_symbol).empty()) {
    return atomic_number(explicit_symbol);
  }
  auto name = std::string{atom_name_field};
  if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0])) != 0) {
    name.erase(name.begin());
  }
  if (!name.empty() && name.front() == ' ') {
    name.erase(name.begin());
    return atomic_number(name.substr(0, 1));
  }
  name = trim(name);
  if (name.empty()) {
    return std::uint8_t{0};
  }
  if (name.size() >= 2) {
    if (const auto two_letter = atomic_number(name.substr(0, 2));
        two_letter.has_value()) {
      return two_letter;
    }
  }
  return atomic_number(name.substr(0, 1));
}

Result<std::int32_t> parse_charge(std::string_view raw, std::string_view source,
                                  std::size_t line) {
  const auto value = trim(raw);
  if (value.empty()) {
    return Result<std::int32_t>::success(0);
  }
  if (value.size() != 2 || (value[1] != '+' && value[1] != '-') ||
      value[0] < '0' || value[0] > '9') {
    return Result<std::int32_t>::failure(
        parse_error(source, line, "invalid PDB formal charge: " + value));
  }
  const auto magnitude = static_cast<std::int32_t>(value[0] - '0');
  return Result<std::int32_t>::success(value[1] == '-' ? -magnitude
                                                       : magnitude);
}

Result<ParsedAtom> parse_atom(std::string_view line, std::size_t line_number,
                              std::string_view source, bool hetero) {
  if (line.size() < 54) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line_number,
        "PDB ATOM/HETATM record is shorter than coordinate column 54"));
  }
  const auto serial =
      parse_number<std::int64_t>(field(line, 6, 5), source, line_number,
                                 "atom serial");
  const auto residue_number =
      parse_number<std::int64_t>(field(line, 22, 4), source, line_number,
                                 "residue sequence number");
  const auto x = parse_number<double>(field(line, 30, 8), source, line_number,
                                      "Cartn_x");
  const auto y = parse_number<double>(field(line, 38, 8), source, line_number,
                                      "Cartn_y");
  const auto z = parse_number<double>(field(line, 46, 8), source, line_number,
                                      "Cartn_z");
  const auto occupancy = parse_optional_number<double>(
      field(line, 54, 6), source, line_number, "occupancy");
  const auto b_factor = parse_optional_number<double>(
      field(line, 60, 6), source, line_number, "temperature factor");
  const auto charge = parse_charge(field(line, 78, 2), source, line_number);
  if (!serial.has_value()) {
    return Result<ParsedAtom>::failure(serial.error());
  }
  if (!residue_number.has_value()) {
    return Result<ParsedAtom>::failure(residue_number.error());
  }
  if (!x.has_value()) {
    return Result<ParsedAtom>::failure(x.error());
  }
  if (!y.has_value()) {
    return Result<ParsedAtom>::failure(y.error());
  }
  if (!z.has_value()) {
    return Result<ParsedAtom>::failure(z.error());
  }
  if (!occupancy.has_value()) {
    return Result<ParsedAtom>::failure(occupancy.error());
  }
  if (!b_factor.has_value()) {
    return Result<ParsedAtom>::failure(b_factor.error());
  }
  if (!charge.has_value()) {
    return Result<ParsedAtom>::failure(charge.error());
  }

  auto atom_name = trim(field(line, 12, 4));
  auto residue_name = trim(field(line, 17, 3));
  if (atom_name.empty() || residue_name.empty()) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line_number, "PDB atom and residue names must not be empty"));
  }
  const auto element_text = trim(field(line, 76, 2));
  const auto element = pdb_element(field(line, 76, 2), field(line, 12, 4));
  if (!element.has_value()) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line_number, "unknown PDB element symbol: " + element_text));
  }

  ParsedAtom atom;
  atom.identity = AtomIdentity{
      serial.value(),
      std::move(atom_name),
      trim(field(line, 16, 1)),
      std::move(residue_name),
      trim(field(line, 21, 1)),
      residue_number.value(),
      trim(field(line, 26, 1)),
      trim(field(line, 72, 4))};
  atom.hetero = hetero;
  atom.atomic_number = element.value();
  atom.formal_charge = charge.value();
  atom.position = Vec3d{x.value(), y.value(), z.value()};
  atom.occupancy = occupancy.value();
  atom.b_factor = b_factor.value();
  atom.line = line_number;
  return Result<ParsedAtom>::success(std::move(atom));
}

Result<model::UnitCell> parse_cryst1(std::string_view line,
                                    std::size_t line_number,
                                    std::string_view source) {
  const auto a =
      parse_number<double>(field(line, 6, 9), source, line_number, "cell a");
  const auto b =
      parse_number<double>(field(line, 15, 9), source, line_number, "cell b");
  const auto c =
      parse_number<double>(field(line, 24, 9), source, line_number, "cell c");
  const auto alpha = parse_number<double>(field(line, 33, 7), source,
                                          line_number, "cell alpha");
  const auto beta = parse_number<double>(field(line, 40, 7), source,
                                         line_number, "cell beta");
  const auto gamma = parse_number<double>(field(line, 47, 7), source,
                                          line_number, "cell gamma");
  if (!a.has_value()) {
    return Result<model::UnitCell>::failure(a.error());
  }
  if (!b.has_value()) {
    return Result<model::UnitCell>::failure(b.error());
  }
  if (!c.has_value()) {
    return Result<model::UnitCell>::failure(c.error());
  }
  if (!alpha.has_value()) {
    return Result<model::UnitCell>::failure(alpha.error());
  }
  if (!beta.has_value()) {
    return Result<model::UnitCell>::failure(beta.error());
  }
  if (!gamma.has_value()) {
    return Result<model::UnitCell>::failure(gamma.error());
  }
  return make_unit_cell(a.value(), b.value(), c.value(), alpha.value(),
                        beta.value(), gamma.value(), source, line_number);
}

std::string default_name(std::string_view source_name) {
  const auto slash = source_name.find_last_of("/\\");
  auto name = std::string{source_name.substr(
      slash == std::string_view::npos ? 0 : slash + 1)};
  const auto dot = name.find_last_of('.');
  if (dot != std::string::npos) {
    name.resize(dot);
  }
  return name.empty() || name == "<memory>" ? "pdb_structure" : name;
}

}  // namespace

Result<StructureDocument> read_pdb(std::string_view content,
                                   std::string source_name) {
  std::vector<ParsedModel> models;
  ParsedModel implicit_model{1, {}};
  ParsedModel* current_model = &implicit_model;
  bool explicit_models = false;
  bool inside_model = false;
  std::optional<model::UnitCell> unit_cell;
  std::string entry_id;
  std::string deposition_date;
  std::string title;
  std::string space_group;
  std::string z_value;
  std::vector<ParsedConnection> connections;
  std::string molecule_remarks;

  const auto retain_molecule_remark = [&molecule_remarks](std::string_view line) {
    molecule_remarks.append(line);
    molecule_remarks.push_back('\n');
  };

  std::size_t line_number = 0;
  std::size_t position = 0;
  while (position <= content.size()) {
    ++line_number;
    const auto end = content.find('\n', position);
    auto line = content.substr(
        position, end == std::string_view::npos ? content.size() - position
                                                : end - position);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    const auto record = trim(field(line, 0, 6));
    if (record == "MODEL") {
      if (inside_model || !implicit_model.atoms.empty()) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, line_number,
            "MODEL record is nested or appears after ungrouped atom records"));
      }
      const auto model_number = parse_number<std::uint64_t>(
          field(line, 10, 4), source_name, line_number, "model serial");
      if (!model_number.has_value()) {
        return Result<StructureDocument>::failure(model_number.error());
      }
      explicit_models = true;
      inside_model = true;
      models.push_back(ParsedModel{model_number.value(), {}});
      current_model = &models.back();
    } else if (record == "ENDMDL") {
      if (!inside_model) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, line_number, "ENDMDL appears without an open MODEL"));
      }
      if (current_model->atoms.empty()) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, line_number, "MODEL contains no atoms"));
      }
      inside_model = false;
      current_model = nullptr;
    } else if (record == "ATOM" || record == "HETATM") {
      if (explicit_models && !inside_model) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, line_number,
            "ATOM/HETATM record is outside MODEL/ENDMDL"));
      }
      const auto atom =
          parse_atom(line, line_number, source_name, record == "HETATM");
      if (!atom.has_value()) {
        return Result<StructureDocument>::failure(atom.error());
      }
      current_model->atoms.push_back(atom.value());
    } else if (record == "CRYST1") {
      if (unit_cell.has_value()) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, line_number, "duplicate CRYST1 record"));
      }
      const auto parsed = parse_cryst1(line, line_number, source_name);
      if (!parsed.has_value()) {
        return Result<StructureDocument>::failure(parsed.error());
      }
      unit_cell = parsed.value();
      space_group = trim(field(line, 55, 11));
      z_value = trim(field(line, 66, 4));
    } else if (record == "HEADER") {
      entry_id = trim(field(line, 62, 4));
      deposition_date = trim(field(line, 50, 9));
    } else if (record == "TITLE") {
      if (!title.empty()) {
        title.push_back(' ');
      }
      title += trim(field(line, 10, 70));
    } else if (record == "CONECT") {
      retain_molecule_remark(line);
      const auto source_serial = parse_number<std::int64_t>(
          field(line, 6, 5), source_name, line_number, "CONECT source serial");
      if (!source_serial.has_value()) {
        return Result<StructureDocument>::failure(source_serial.error());
      }
      for (const auto offset : {11U, 16U, 21U, 26U}) {
        const auto raw = field(line, offset, 5);
        if (trim(raw).empty()) {
          continue;
        }
        const auto target = parse_number<std::int64_t>(
            raw, source_name, line_number, "CONECT target serial");
        if (!target.has_value()) {
          return Result<StructureDocument>::failure(target.error());
        }
        connections.push_back(
            ParsedConnection{source_serial.value(), target.value(), line_number});
      }
    } else if (record == "REMARK") {
      retain_molecule_remark(line);
    } else if (!record.empty() && record != "END") {
      // Match the observable molfile metadata channel: records that are not
      // consumed as structure or coordinate data remain available verbatim.
      retain_molecule_remark(line);
    }
    if (end == std::string_view::npos) {
      break;
    }
    position = end + 1;
  }

  if (inside_model) {
    return Result<StructureDocument>::failure(parse_error(
        source_name, line_number, "MODEL is missing its ENDMDL record"));
  }
  if (!explicit_models) {
    if (implicit_model.atoms.empty()) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, 1, "PDB input contains no ATOM or HETATM records"));
    }
    models.push_back(std::move(implicit_model));
  }

  TopologyBuilder builder;
  std::map<std::tuple<std::string, std::int64_t, std::string, std::string,
                      std::string>,
           ResidueIndex>
      residues;
  std::map<AtomIdentity, AtomIndex> identity_to_index;
  std::map<std::int64_t, AtomIndex> serial_to_index;
  BooleanColumn hetero_values;

  const auto& first_model = models.front();
  for (const auto& atom : first_model.atoms) {
    const auto residue_key = std::tuple{
        atom.identity.chain_id, atom.identity.residue_number,
        atom.identity.insertion_code, atom.identity.residue_name,
        atom.identity.segment_id};
    auto residue = residues.find(residue_key);
    if (residue == residues.end()) {
      const auto added = builder.add_residue(ResidueRecord{
          atom.identity.residue_name, atom.identity.residue_number,
          atom.identity.insertion_code, atom.identity.chain_id,
          atom.identity.segment_id});
      if (!added.has_value()) {
        return Result<StructureDocument>::failure(added.error());
      }
      residue = residues.emplace(residue_key, added.value()).first;
    }
    if (identity_to_index.contains(atom.identity) ||
        serial_to_index.contains(atom.identity.serial)) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, atom.line,
          "duplicate atom identity or serial in first PDB model"));
    }
    const auto added = builder.add_atom(AtomRecord{
        atom.identity.atom_name, atom.atomic_number, residue->second,
        atom.identity.alternate_location, atom.formal_charge,
        atom.identity.serial});
    if (!added.has_value()) {
      return Result<StructureDocument>::failure(added.error());
    }
    identity_to_index.emplace(atom.identity, added.value());
    serial_to_index.emplace(atom.identity.serial, added.value());
    hetero_values.values.push_back(atom.hetero ? 1U : 0U);
  }

  std::set<std::pair<std::size_t, std::size_t>> unique_bonds;
  for (const auto& connection : connections) {
    const auto source_atom = serial_to_index.find(connection.source);
    const auto target_atom = serial_to_index.find(connection.target);
    if (source_atom == serial_to_index.end() ||
        target_atom == serial_to_index.end()) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, connection.line,
          "CONECT references an unknown atom serial"));
    }
    auto endpoints = std::minmax(source_atom->second.value,
                                 target_atom->second.value);
    if (endpoints.first == endpoints.second ||
        !unique_bonds.emplace(endpoints.first, endpoints.second).second) {
      continue;
    }
    if (const auto error = builder.add_bond(
            Bond{AtomIndex{endpoints.first}, AtomIndex{endpoints.second},
                 BondOrder::unknown});
        error.has_value()) {
      return Result<StructureDocument>::failure(*error);
    }
  }
  if (const auto error = builder.add_property(
          "pdb.is_hetero", std::move(hetero_values),
          PropertyMetadata{std::nullopt, "PDB ATOM/HETATM", {}});
      error.has_value()) {
    return Result<StructureDocument>::failure(*error);
  }
  builder.set_source_metadata("format", "pdb");
  builder.set_source_metadata("format_version", "3.3");
  builder.set_source_metadata("source_name", source_name);
  if (!entry_id.empty()) {
    builder.set_source_metadata("entry_id", entry_id);
  }
  if (!deposition_date.empty()) {
    builder.set_source_metadata("pdb.deposition_date", deposition_date);
  }
  if (!molecule_remarks.empty()) {
    builder.set_source_metadata("pdb.molecule_remarks", molecule_remarks);
  }
  const auto topology = builder.build();
  if (!topology.has_value()) {
    return Result<StructureDocument>::failure(topology.error());
  }

  std::vector<std::shared_ptr<const CoordinateFrame>> frames;
  frames.reserve(models.size());
  for (const auto& model : models) {
    std::vector<Vec3d> positions(topology.value()->atom_count());
    std::vector<std::uint8_t> presence(topology.value()->atom_count(), 0U);
    std::vector<double> occupancy(topology.value()->atom_count(), 0.0);
    std::vector<std::uint8_t> occupancy_present(topology.value()->atom_count(),
                                                0U);
    std::vector<double> b_factor(topology.value()->atom_count(), 0.0);
    std::vector<std::uint8_t> b_factor_present(topology.value()->atom_count(),
                                               0U);
    std::set<std::size_t> seen;
    for (const auto& atom : model.atoms) {
      const auto found = identity_to_index.find(atom.identity);
      if (found == identity_to_index.end()) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, atom.line,
            "later PDB model contains an atom identity absent from model 1"));
      }
      const auto index = found->second.value;
      if (!seen.insert(index).second) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, atom.line, "duplicate atom identity within PDB model"));
      }
      positions[index] = atom.position;
      presence[index] = 1U;
      if (atom.occupancy.has_value()) {
        occupancy[index] = *atom.occupancy;
        occupancy_present[index] = 1U;
      }
      if (atom.b_factor.has_value()) {
        b_factor[index] = *atom.b_factor;
        b_factor_present[index] = 1U;
      }
    }
    FrameMetadata metadata;
    metadata.source_step = model.number;
    metadata.unit_cell = unit_cell;
    metadata.coordinate_unit = operation::LengthUnit::angstrom;
    metadata.atom_properties.emplace(
        "occupancy",
        model::AtomProperty{std::move(occupancy),
                            PropertyMetadata{std::nullopt, "PDB occupancy", {}}});
    metadata.atom_properties.emplace(
        "occupancy_present",
        model::AtomProperty{BooleanColumn{std::move(occupancy_present)},
                            PropertyMetadata{std::nullopt, "PDB occupancy", {}}});
    metadata.atom_properties.emplace(
        "b_iso_or_equiv",
        model::AtomProperty{
            std::move(b_factor),
            PropertyMetadata{"angstrom^2", "PDB temperature factor", {}}});
    metadata.atom_properties.emplace(
        "b_iso_or_equiv_present",
        model::AtomProperty{
            BooleanColumn{std::move(b_factor_present)},
            PropertyMetadata{std::nullopt, "PDB temperature factor", {}}});
    const auto frame = CoordinateFrame::create(
        CoordinateBuffer{std::move(positions)}, std::nullopt,
        std::move(presence), std::move(metadata));
    if (!frame.has_value()) {
      return Result<StructureDocument>::failure(frame.error());
    }
    frames.push_back(frame.value());
  }
  const auto coordinates = model::InMemoryCoordinateSource::create(
      topology.value()->atom_count(), std::move(frames));
  if (!coordinates.has_value()) {
    return Result<StructureDocument>::failure(coordinates.error());
  }

  StructureData structure;
  structure.name = entry_id.empty() ? default_name(source_name) : entry_id;
  structure.topology = topology.value();
  structure.coordinates = coordinates.value();
  structure.metadata.emplace("format", "pdb");
  structure.metadata.emplace("format_version", "3.3");
  if (!entry_id.empty()) {
    structure.metadata.emplace("entry_id", entry_id);
  }
  if (!deposition_date.empty()) {
    structure.metadata.emplace("deposition_date", deposition_date);
  }
  if (!molecule_remarks.empty()) {
    structure.metadata.emplace("molecule_remarks", molecule_remarks);
  }
  if (!title.empty()) {
    structure.metadata.emplace("title", title);
  }
  if (!space_group.empty()) {
    structure.metadata.emplace("space_group", space_group);
  }
  if (!z_value.empty()) {
    structure.metadata.emplace("cell_z", z_value);
  }
  StructureDocument document;
  document.format = StructureFormat::pdb;
  document.source_name = std::move(source_name);
  document.structures.push_back(std::move(structure));
  return Result<StructureDocument>::success(std::move(document));
}

}  // namespace molshredder::io::detail
