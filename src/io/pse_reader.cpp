#include "structure_reader_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pickle_reader.hpp"

// PyMOL session (.pse) reader. A session is a Python pickle; it is read with the restricted unpickler in
// pickle_reader.cpp (no code execution, explicit allow-list). Molecular objects (all states, bonds, atom
// identity, charges, B-factors, occupancy, secondary structure, unit cell), the per-atom representations that
// MolShredder can draw and named selections are imported. Surfaces, maps, CGO, measurements, colors, settings,
// scenes, movie and camera are reported in the import notes and are not applied.
// The list layout follows what Open-Source PyMOL 3.1.0 writes (checked against its own sessions) in SEQ-319 S6.
namespace molshredder::io::detail {
namespace {

using operation::Result;

const PickleValue *at(const PickleValue *list, std::size_t index) {
  if (list == nullptr || !list->is_sequence() || index >= list->items.size()) return nullptr;
  return list->items[index].get();
}

std::optional<std::int64_t> as_int(const PickleValue *value) {
  if (value == nullptr || !value->is_number()) return std::nullopt;
  return value->as_integer();
}

std::optional<double> as_real(const PickleValue *value) {
  if (value == nullptr || !value->is_number()) return std::nullopt;
  return value->as_double();
}

std::string as_text(const PickleValue *value) {
  if (value == nullptr) return {};
  if (value->kind == PickleValue::Kind::text || value->kind == PickleValue::Kind::bytes) return value->text;
  return {};
}

// A sequence of numbers stored either as a list or (binary dump) as raw little-endian bytes.
bool read_floats(const PickleValue *value, std::vector<double> &out) {
  if (value == nullptr) return false;
  if (value->kind == PickleValue::Kind::bytes) {
    if (value->text.size() % 4U != 0U) return false;
    out.resize(value->text.size() / 4U);
    for (std::size_t i = 0; i < out.size(); ++i) {
      float number{};
      std::memcpy(&number, value->text.data() + 4U * i, 4U);
      out[i] = static_cast<double>(number);
    }
    return true;
  }
  if (!value->is_sequence()) return false;
  out.reserve(value->items.size());
  for (const auto &item : value->items) {
    const auto number = as_real(item.get());
    if (!number) return false;
    out.push_back(*number);
  }
  return true;
}

bool read_ints(const PickleValue *value, std::vector<std::int64_t> &out) {
  if (value == nullptr) return false;
  if (value->kind == PickleValue::Kind::bytes) {
    if (value->text.size() % 4U != 0U) return false;
    out.resize(value->text.size() / 4U);
    for (std::size_t i = 0; i < out.size(); ++i) {
      std::int32_t number{};
      std::memcpy(&number, value->text.data() + 4U * i, 4U);
      out[i] = number;
    }
    return true;
  }
  if (!value->is_sequence()) return false;
  out.reserve(value->items.size());
  for (const auto &item : value->items) {
    const auto number = as_int(item.get());
    if (!number) return false;
    out.push_back(*number);
  }
  return true;
}

struct RepresentationBit {
  int bit;
  const char *command_name;  // empty: not drawable in MolShredder yet
  const char *session_name;
};

constexpr RepresentationBit kRepresentationBits[] = {
    {0, "sticks", "sticks"},     {1, "spheres", "spheres"}, {2, "", "surface"},   {3, "", "labels"},
    {4, "", "nonbonded spheres"}, {5, "cartoon", "cartoon"}, {6, "ribbon", "ribbon"}, {7, "lines", "lines"},
    {8, "", "mesh"},             {9, "", "dots"},           {11, "", "nonbonded"},
};

std::string compress_ranges(const std::vector<std::size_t> &zero_based) {
  std::string out;
  for (std::size_t i = 0; i < zero_based.size();) {
    std::size_t j = i;
    while (j + 1U < zero_based.size() && zero_based[j + 1U] == zero_based[j] + 1U) ++j;
    if (!out.empty()) out.push_back('+');
    out += std::to_string(zero_based[i] + 1U);
    if (j > i) out += "-" + std::to_string(zero_based[j] + 1U);
    i = j + 1U;
  }
  return out;
}

struct Fail {
  std::string message;
};

Result<StructureData> build_molecule(const PickleValue &record, const std::string &object_name, const std::string &source_name,
                                     std::vector<std::string> &notes) {
  using R = Result<StructureData>;
  const auto fail = [&](const std::string &message) {
    return R::failure(parse_error(source_name, 1U, "PSE object '" + object_name + "': " + message));
  };
  const auto *head = at(&record, 0);
  const auto atom_count_value = as_int(at(&record, 3));
  const auto *states = at(&record, 4);
  const auto *bonds = at(&record, 6);
  const auto *atoms = at(&record, 7);
  if (!atom_count_value || *atom_count_value <= 0 || states == nullptr || !states->is_sequence() || atoms == nullptr || !atoms->is_sequence())
    return fail("the molecule record is incomplete");
  const auto atom_count = static_cast<std::size_t>(*atom_count_value);
  if (atoms->items.size() != atom_count || at(atoms, 0) == nullptr || !at(atoms, 0)->is_sequence() || at(atoms, 0)->items.size() < 23U)
    return fail("the atom table uses the binary dump form; save the session again with `set pse_binary_dump, 0`");
  if (bonds != nullptr && bonds->is_sequence() && !bonds->items.empty() && (at(bonds, 0) == nullptr || !at(bonds, 0)->is_sequence()))
    return fail("the bond table uses the binary dump form; save the session again with `set pse_binary_dump, 0`");

  model::TopologyBuilder builder;
  std::vector<double> partial_charge(atom_count, 0.0), occupancy(atom_count, 0.0), b_factor(atom_count, 0.0);
  std::vector<std::uint8_t> hetero(atom_count, 0U);
  std::vector<std::string> secondary(atom_count);
  std::vector<std::uint64_t> visible_mask(atom_count, 0U);
  bool any_secondary = false, any_charge = false;
  struct Key {
    std::string chain, segment, name, insertion;
    std::int64_t number{};
    bool operator==(const Key &) const = default;
  };
  std::optional<Key> current_key;
  std::optional<model::ResidueIndex> current_residue;
  for (std::size_t a = 0; a < atom_count; ++a) {
    const auto *atom = at(atoms, a);
    if (atom == nullptr || !atom->is_sequence() || atom->items.size() < 23U) return fail("atom " + std::to_string(a + 1U) + " is malformed");
    Key key;
    key.number = as_int(at(atom, 0)).value_or(0);
    key.chain = as_text(at(atom, 1));
    auto resi = as_text(at(atom, 3));
    // PyMOL >= 1.8.1 stores only the insertion code; older sessions hold the full residue identifier.
    const bool numeric_prefix = !resi.empty() && (std::isdigit(static_cast<unsigned char>(resi.front())) != 0 || resi.front() == '-');
    if (numeric_prefix) {
      std::size_t split = (resi.front() == '-') ? 1U : 0U;
      while (split < resi.size() && std::isdigit(static_cast<unsigned char>(resi[split])) != 0) ++split;
      resi = resi.substr(split);
    }
    key.insertion = resi;
    key.segment = as_text(at(atom, 4));
    key.name = as_text(at(atom, 5));
    if (key.name.empty()) key.name = "UNK";
    if (!current_key || !(*current_key == key)) {
      const auto residue = builder.add_residue({key.name, key.number, key.insertion, key.chain, key.segment});
      if (!residue.has_value()) return R::failure(residue.error());
      current_residue = residue.value();
      current_key = key;
    }
    const auto element_text = as_text(at(atom, 7));
    const auto element = element_text.empty() ? std::optional<std::uint8_t>{} : atomic_number(element_text);
    const auto formal = static_cast<std::int32_t>(as_int(at(atom, 18)).value_or(0));
    const auto id = as_int(at(atom, 22));
    const auto alt = as_text(at(atom, 2));
    const auto added = builder.add_atom({as_text(at(atom, 6)), element.value_or(0U), *current_residue, alt, formal,
                                         id.has_value() ? std::optional<std::int64_t>{*id} : std::nullopt});
    if (!added.has_value()) return R::failure(added.error());
    b_factor[a] = as_real(at(atom, 14)).value_or(0.0);
    occupancy[a] = as_real(at(atom, 15)).value_or(1.0);
    partial_charge[a] = as_real(at(atom, 17)).value_or(0.0);
    any_charge = any_charge || partial_charge[a] != 0.0;
    hetero[a] = as_int(at(atom, 19)).value_or(0) != 0 ? 1U : 0U;
    const auto ss = as_text(at(atom, 10));
    if (ss == "H" || ss == "S") {
      secondary[a] = ss;
      any_secondary = true;
    }
    visible_mask[a] = static_cast<std::uint64_t>(as_int(at(atom, 20)).value_or(0));
  }
  if (bonds != nullptr && bonds->is_sequence()) {
    std::set<std::pair<std::size_t, std::size_t>> seen;
    for (const auto &entry : bonds->items) {
      const auto first = as_int(at(entry.get(), 0)), second = as_int(at(entry.get(), 1)), order = as_int(at(entry.get(), 2));
      if (!first || !second || *first < 0 || *second < 0 || static_cast<std::size_t>(*first) >= atom_count ||
          static_cast<std::size_t>(*second) >= atom_count)
        return fail("a bond references an atom outside the object");
      const auto lo = std::min<std::size_t>(static_cast<std::size_t>(*first), static_cast<std::size_t>(*second));
      const auto hi = std::max<std::size_t>(static_cast<std::size_t>(*first), static_cast<std::size_t>(*second));
      if (lo == hi || !seen.insert({lo, hi}).second) continue;
      model::BondOrder bond_order = model::BondOrder::unknown;
      switch (order.value_or(1)) {
      case 1: bond_order = model::BondOrder::single; break;
      case 2: bond_order = model::BondOrder::double_bond; break;
      case 3: bond_order = model::BondOrder::triple; break;
      case 4: bond_order = model::BondOrder::aromatic; break;
      default: break;
      }
      const auto error = builder.add_bond({model::AtomIndex{lo}, model::AtomIndex{hi}, bond_order, model::BondQuery::none, model::BondStereo::none,
                                           model::ChemicalAnnotationOrigin::explicit_input});
      if (error.has_value()) return R::failure(*error);
    }
  }
  if (any_charge) {
    if (const auto error = builder.add_property("partial_charge", std::vector<double>{partial_charge},
                                                model::PropertyMetadata{"elementary_charge", "PSE partial charge", {}});
        error.has_value())
      return R::failure(*error);
  }
  if (const auto error = builder.add_property("pdb.is_hetero", model::BooleanColumn{hetero}, model::PropertyMetadata{std::nullopt, "PSE hetatm flag", {}});
      error.has_value())
    return R::failure(*error);
  if (any_secondary) {
    if (const auto error = builder.add_property("ss", std::move(secondary), model::PropertyMetadata{std::nullopt, "PSE secondary structure", {}});
        error.has_value())
      return R::failure(*error);
  }
  builder.set_source_metadata("format", "pse");
  const auto topology = builder.build();
  if (!topology.has_value()) return R::failure(topology.error());

  // Unit cell of the object: symmetry = [[[a, b, c], [alpha, beta, gamma]], space group].
  std::optional<model::UnitCell> unit_cell;
  std::string space_group;
  if (const auto *symmetry = at(&record, 10); symmetry != nullptr && symmetry->is_sequence()) {
    const auto *cell = at(symmetry, 0);
    std::vector<double> lengths, angles;
    if (read_floats(at(cell, 0), lengths) && read_floats(at(cell, 1), angles) && lengths.size() == 3U && angles.size() == 3U && lengths[0] > 0.0 &&
        lengths[1] > 0.0 && lengths[2] > 0.0) {
      const auto made = make_unit_cell(lengths[0], lengths[1], lengths[2], angles[0], angles[1], angles[2], source_name, 1U);
      if (made.has_value()) unit_cell = made.value();
    }
    space_group = as_text(at(symmetry, 1));
  }

  model::FrameMetadata base_metadata;
  base_metadata.coordinate_unit = operation::LengthUnit::angstrom;
  base_metadata.unit_cell = unit_cell;
  base_metadata.atom_properties.emplace("occupancy", model::AtomProperty{std::move(occupancy), model::PropertyMetadata{std::nullopt, "PSE occupancy", {}}});
  base_metadata.atom_properties.emplace("occupancy_present",
                                        model::AtomProperty{model::BooleanColumn{std::vector<std::uint8_t>(atom_count, 1U)}, model::PropertyMetadata{}});
  base_metadata.atom_properties.emplace("b_iso_or_equiv", model::AtomProperty{std::move(b_factor), model::PropertyMetadata{"angstrom^2", "PSE B-factor", {}}});
  base_metadata.atom_properties.emplace("b_iso_or_equiv_present",
                                        model::AtomProperty{model::BooleanColumn{std::vector<std::uint8_t>(atom_count, 1U)}, model::PropertyMetadata{}});

  std::vector<std::shared_ptr<const model::CoordinateFrame>> frames;
  for (std::size_t s = 0; s < states->items.size(); ++s) {
    const auto *coordset = states->items[s].get();
    if (coordset == nullptr || !coordset->is_sequence()) continue;  // empty state
    std::vector<double> flat;
    std::vector<std::int64_t> index_to_atom;
    if (!read_floats(at(coordset, 2), flat) || !read_ints(at(coordset, 3), index_to_atom))
      return fail("state " + std::to_string(s + 1U) + " has unreadable coordinates");
    if (flat.size() != index_to_atom.size() * 3U) return fail("state " + std::to_string(s + 1U) + " coordinate count does not match its atom index list");
    std::vector<model::Vec3d> positions(atom_count);
    std::vector<std::uint8_t> presence(atom_count, 0U);
    for (std::size_t i = 0; i < index_to_atom.size(); ++i) {
      const auto atom = index_to_atom[i];
      if (atom < 0 || static_cast<std::size_t>(atom) >= atom_count) return fail("state " + std::to_string(s + 1U) + " refers to an atom outside the object");
      if (presence[static_cast<std::size_t>(atom)] != 0U) return fail("state " + std::to_string(s + 1U) + " lists an atom twice");
      positions[static_cast<std::size_t>(atom)] = {flat[3U * i], flat[3U * i + 1U], flat[3U * i + 2U]};
      presence[static_cast<std::size_t>(atom)] = 1U;
    }
    auto metadata = base_metadata;
    metadata.source_step = s + 1U;
    const auto frame = model::CoordinateFrame::create(model::CoordinateBuffer{std::move(positions)}, std::nullopt, std::move(presence), std::move(metadata));
    if (!frame.has_value()) return R::failure(frame.error());
    frames.push_back(frame.value());
  }
  if (frames.empty()) return fail("the object has no coordinate set");
  const auto coordinates = model::InMemoryCoordinateSource::create(atom_count, std::move(frames));
  if (!coordinates.has_value()) return R::failure(coordinates.error());

  StructureData structure;
  structure.name = object_name;
  structure.topology = topology.value();
  structure.coordinates = coordinates.value();
  structure.metadata.emplace("format", "pse");
  if (!space_group.empty()) structure.metadata.emplace("pse.space_group", space_group);

  // Representations: PyMOL keeps the visible state per atom (the object-level mask is not an aggregate of it).
  (void)head;
  for (const auto &rep : kRepresentationBits) {
    const std::uint64_t bit = std::uint64_t{1} << rep.bit;
    std::vector<std::size_t> shown;
    for (std::size_t a = 0; a < atom_count; ++a)
      if ((visible_mask[a] & bit) != 0U) shown.push_back(a);
    if (shown.empty()) continue;
    if (rep.command_name[0] == '\0') {
      notes.push_back("object " + object_name + ": " + rep.session_name + " shown on " + std::to_string(shown.size()) + " atoms (not imported)");
      continue;
    }
    structure.representation_hints.push_back({rep.command_name, shown.size() == atom_count ? std::string{"all"} : "index " + compress_ranges(shown)});
  }
  return R::success(std::move(structure));
}

} // namespace

Result<StructureDocument> read_pse(std::string_view content, std::string source_name) {
  using R = Result<StructureDocument>;
  const auto parsed = unpickle_restricted(content);
  if (!parsed.has_value()) return R::failure(parse_error(source_name, 1U, parsed.error().message, parsed.error().suggestion));
  const auto &root = *parsed.value();
  const auto *names = root.find("names");
  if (root.kind != PickleValue::Kind::dict || names == nullptr || !names->is_sequence())
    return R::failure(parse_error(source_name, 1U, "the file is a pickle but not a PyMOL session (no 'names' list)"));

  StructureDocument document;
  document.format = StructureFormat::pse;
  document.source_name = source_name;
  std::map<std::string, std::size_t> skipped;  // object type -> count
  std::vector<const PickleValue *> selections;
  static const std::map<std::int64_t, const char *> kObjectTypes{{2, "map"},        {3, "mesh"},   {4, "measurement"}, {5, "callback"}, {6, "CGO"},
                                                                 {7, "surface"},    {8, "gadget"}, {9, "calculator"},  {10, "slice"},   {11, "alignment"},
                                                                 {12, "group"},     {13, "volume"}};
  for (const auto &entry : names->items) {
    if (entry == nullptr || entry->kind == PickleValue::Kind::none) continue;
    const auto type = as_int(at(entry.get(), 1));
    const auto name = as_text(at(entry.get(), 0));
    if (!type || name.empty()) continue;
    if (*type == 1) {
      selections.push_back(entry.get());
      continue;
    }
    if (*type != 0) continue;
    const auto *record = at(entry.get(), 5);
    const auto object_type = as_int(at(at(record, 0), 0));
    if (record == nullptr || !object_type) continue;
    if (*object_type != 1) {
      const auto found = kObjectTypes.find(*object_type);
      ++skipped[found == kObjectTypes.end() ? "unknown" : found->second];
      continue;
    }
    auto structure = build_molecule(*record, name, source_name, document.import_notes);
    if (!structure.has_value()) return R::failure(structure.error());
    structure.value().visible = as_int(at(entry.get(), 2)).value_or(1) != 0;
    document.structures.push_back(std::move(structure.value()));
  }
  if (document.structures.empty()) return R::failure(parse_error(source_name, 1U, "the PyMOL session contains no molecular object"));
  for (const auto &[type, count] : skipped) document.import_notes.push_back(std::to_string(count) + " " + type + " object(s) were not imported");

  std::set<std::string> imported;
  for (const auto &structure : document.structures) imported.insert(structure.name);
  for (const auto *entry : selections) {
    const auto name = as_text(at(entry, 0));
    std::string expression;
    const auto *parts = at(entry, 5);
    if (parts == nullptr || !parts->is_sequence()) continue;
    for (const auto &part : parts->items) {
      const auto object = as_text(at(part.get(), 0));
      std::vector<std::int64_t> indices;
      if (!imported.contains(object) || !read_ints(at(part.get(), 1), indices) || indices.empty()) continue;
      std::vector<std::size_t> sorted;
      sorted.reserve(indices.size());
      for (const auto index : indices)
        if (index >= 0) sorted.push_back(static_cast<std::size_t>(index));
      std::sort(sorted.begin(), sorted.end());
      sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
      if (sorted.empty()) continue;
      if (!expression.empty()) expression += " or ";
      expression += "(model " + object + " and index " + compress_ranges(sorted) + ")";
    }
    if (!expression.empty()) document.named_selections.emplace_back(name, expression);
    else document.import_notes.push_back("selection " + name + " is empty or refers to objects that were not imported");
  }
  document.import_notes.push_back("colors, settings, scenes, movie and camera view are not imported");
  return R::success(std::move(document));
}

} // namespace molshredder::io::detail
