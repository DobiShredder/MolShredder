#include "structure_writer_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <msgpack.hpp>

#include "structure_reader_internal.hpp"

// MMTF v1.0 writer. Coordinates use the delta + recursive-index codec with a
// 0.001 A resolution (B-factors and occupancies 0.01), as the specification
// recommends; everything else uses the plain codecs.
namespace molshredder::io::detail {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument, std::move(message), std::move(suggestion)};
}

using Buffer = msgpack::sbuffer;

void put_be32(std::string &out, std::int32_t value) {
  const auto u = static_cast<std::uint32_t>(value);
  for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>((u >> shift) & 0xffU));
}
void put_be16(std::string &out, std::int16_t value) {
  const auto u = static_cast<std::uint16_t>(value);
  out.push_back(static_cast<char>((u >> 8U) & 0xffU));
  out.push_back(static_cast<char>(u & 0xffU));
}

std::string header(std::int32_t codec, std::int32_t length, std::int32_t parameter) {
  std::string out;
  put_be32(out, codec);
  put_be32(out, length);
  put_be32(out, parameter);
  return out;
}

std::string encode_int32(const std::vector<std::int32_t> &values) {
  auto out = header(4, static_cast<std::int32_t>(values.size()), 0);
  for (const auto v : values) put_be32(out, v);
  return out;
}

std::string encode_int8(const std::vector<std::int32_t> &values) {
  auto out = header(2, static_cast<std::int32_t>(values.size()), 0);
  for (const auto v : values) out.push_back(static_cast<char>(static_cast<std::int8_t>(v)));
  return out;
}

std::vector<std::int32_t> run_length_encode(const std::vector<std::int32_t> &values) {
  std::vector<std::int32_t> out;
  for (std::size_t i = 0; i < values.size();) {
    std::size_t j = i;
    while (j < values.size() && values[j] == values[i]) ++j;
    out.push_back(values[i]);
    out.push_back(static_cast<std::int32_t>(j - i));
    i = j;
  }
  return out;
}

std::string encode_rle(std::int32_t codec, const std::vector<std::int32_t> &values, std::int32_t parameter,
                       const std::vector<std::int32_t> &pairs) {
  auto out = header(codec, static_cast<std::int32_t>(values.size()), parameter);
  for (const auto v : pairs) put_be32(out, v);
  return out;
}

std::string encode_char_rle(const std::vector<std::int32_t> &values) {
  return encode_rle(6, values, 0, run_length_encode(values));
}

std::string encode_delta_rle(const std::vector<std::int32_t> &values) {
  std::vector<std::int32_t> deltas(values.size());
  std::int32_t previous = 0;
  for (std::size_t i = 0; i < values.size(); ++i) {
    deltas[i] = static_cast<std::int32_t>(static_cast<std::uint32_t>(values[i]) - static_cast<std::uint32_t>(previous));
    previous = values[i];
  }
  return encode_rle(8, values, 0, run_length_encode(deltas));
}

std::string encode_float_rle(const std::vector<double> &values, std::int32_t divisor) {
  std::vector<std::int32_t> scaled(values.size());
  for (std::size_t i = 0; i < values.size(); ++i)
    scaled[i] = static_cast<std::int32_t>(std::llround(values[i] * divisor));
  return encode_rle(9, scaled, divisor, run_length_encode(scaled));
}

// codec 10: float -> integer (x divisor) -> delta -> recursive 16-bit indexing.
operation::Result<std::string> encode_float_delta(const std::vector<double> &values, std::int32_t divisor) {
  std::int64_t previous = 0;
  std::vector<std::int16_t> packed;
  packed.reserve(values.size());
  for (const auto value : values) {
    const auto scaled = std::llround(value * divisor);
    if (scaled > std::numeric_limits<std::int32_t>::max() / 2 ||
        scaled < std::numeric_limits<std::int32_t>::min() / 2)
      return operation::Result<std::string>::failure(invalid("a coordinate is too large for the MMTF encoding"));
    std::int64_t delta = scaled - previous;
    previous = scaled;
    while (delta >= std::numeric_limits<std::int16_t>::max()) {
      packed.push_back(std::numeric_limits<std::int16_t>::max());
      delta -= std::numeric_limits<std::int16_t>::max();
    }
    while (delta <= std::numeric_limits<std::int16_t>::min()) {
      packed.push_back(std::numeric_limits<std::int16_t>::min());
      delta -= std::numeric_limits<std::int16_t>::min();
    }
    packed.push_back(static_cast<std::int16_t>(delta));
  }
  auto out = header(10, static_cast<std::int32_t>(values.size()), divisor);
  for (const auto v : packed) put_be16(out, v);
  return operation::Result<std::string>::success(std::move(out));
}

std::string encode_strings(const std::vector<std::string> &values, std::size_t width) {
  auto out = header(5, static_cast<std::int32_t>(values.size()), static_cast<std::int32_t>(width));
  for (const auto &text : values) {
    std::string padded = text.substr(0, width);
    padded.resize(width, '\0');
    out += padded;
  }
  return out;
}

template <typename Packer>
void pack_bin(Packer &packer, const std::string &bytes) {
  packer.pack_bin(static_cast<std::uint32_t>(bytes.size()));
  packer.pack_bin_body(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}

template <typename Packer>
void pack_string(Packer &packer, std::string_view text) {
  packer.pack_str(static_cast<std::uint32_t>(text.size()));
  packer.pack_str_body(text.data(), static_cast<std::uint32_t>(text.size()));
}

model::Vec3d position_at(const model::CoordinateFrame &frame, std::size_t index) {
  return std::visit(
      [index](const auto &values) {
        return model::Vec3d{static_cast<double>(values[index].x), static_cast<double>(values[index].y),
                            static_cast<double>(values[index].z)};
      },
      frame.positions().values());
}

char one_letter(std::string_view name) {
  static const std::map<std::string, char, std::less<>> table{
      {"ALA", 'A'}, {"ARG", 'R'}, {"ASN", 'N'}, {"ASP", 'D'}, {"CYS", 'C'}, {"GLN", 'Q'}, {"GLU", 'E'},
      {"GLY", 'G'}, {"HIS", 'H'}, {"ILE", 'I'}, {"LEU", 'L'}, {"LYS", 'K'}, {"MET", 'M'}, {"PHE", 'F'},
      {"PRO", 'P'}, {"SER", 'S'}, {"THR", 'T'}, {"TRP", 'W'}, {"TYR", 'Y'}, {"VAL", 'V'}, {"DA", 'A'},
      {"DC", 'C'}, {"DG", 'G'}, {"DT", 'T'}, {"A", 'A'}, {"C", 'C'}, {"G", 'G'}, {"U", 'U'}};
  const auto found = table.find(name);
  return found == table.end() ? '?' : found->second;
}

std::string chem_comp_type(const model::ResidueRecord &residue) {
  using model::PolymerType;
  if (residue.chemical_origin != model::ChemicalAnnotationOrigin::unspecified) {
    switch (residue.polymer_type) {
      case PolymerType::protein: return "L-PEPTIDE LINKING";
      case PolymerType::dna: return "DNA LINKING";
      case PolymerType::rna: return "RNA LINKING";
      default: break;
    }
    return residue.kind == model::ResidueKind::carbohydrate ? "SACCHARIDE" : "NON-POLYMER";
  }
  static const std::set<std::string, std::less<>> amino{"ALA", "ARG", "ASN", "ASP", "CYS", "GLN", "GLU", "GLY", "HIS",
                                                        "ILE", "LEU", "LYS", "MET", "PHE", "PRO", "SER", "THR", "TRP",
                                                        "TYR", "VAL", "MSE"};
  static const std::set<std::string, std::less<>> dna{"DA", "DC", "DG", "DT"};
  static const std::set<std::string, std::less<>> rna{"A", "C", "G", "U"};
  if (amino.contains(residue.name)) return "L-PEPTIDE LINKING";
  if (dna.contains(residue.name)) return "DNA LINKING";
  if (rna.contains(residue.name)) return "RNA LINKING";
  return "NON-POLYMER";
}

bool is_water(const model::ResidueRecord &residue) {
  return residue.kind == model::ResidueKind::solvent || residue.name == "HOH" || residue.name == "WAT" ||
         residue.name == "DOD";
}

int bond_code(model::BondOrder order) {
  switch (order) {
    case model::BondOrder::double_bond: return 2;
    case model::BondOrder::triple: return 3;
    default: return 1;
  }
}

} // namespace

operation::Result<StructureWriteReport>
write_mmtf(std::ostream &output, const model::Topology &topology, const model::CoordinateSource &coordinates,
           const StructureWriteOptions &options, operation::TaskContext &context) {
  using R = operation::Result<StructureWriteReport>;
  const auto count = coordinates.frame_count();
  if (!count.has_value() || *count == 0U) return R::failure(invalid("MMTF export requires a known non-zero frame count"));
  std::vector<std::size_t> frame_indices = options.frame_indices;
  if (frame_indices.empty()) {
    frame_indices.resize(*count);
    std::iota(frame_indices.begin(), frame_indices.end(), std::size_t{});
  }
  for (const auto index : frame_indices)
    if (index >= *count) return R::failure(invalid("MMTF export frame index is out of range"));
  if (topology.atom_count() == 0U) return R::failure(invalid("MMTF export needs at least one atom"));

  // Atoms in residue order; consecutive residues with one chain id form a chain.
  const auto &atoms = topology.atoms();
  const auto &residues = topology.residues();
  std::vector<std::vector<std::size_t>> atoms_of_residue(residues.size());
  for (std::size_t a = 0; a < atoms.size(); ++a) atoms_of_residue[atoms[a].residue.value].push_back(a);
  std::vector<std::size_t> file_order;  // file position -> topology atom
  std::vector<std::size_t> file_position(atoms.size(), 0);
  struct Group {
    std::size_t residue;
    std::size_t first_atom;  // file position of the first atom
  };
  std::vector<Group> groups;
  std::vector<std::vector<std::size_t>> chain_groups;  // group indices per chain
  std::vector<std::string> chain_labels;
  for (std::size_t r = 0; r < residues.size(); ++r) {
    if (atoms_of_residue[r].empty()) continue;
    if (chain_labels.empty() || chain_labels.back() != residues[r].chain_id) {
      chain_labels.push_back(residues[r].chain_id);
      chain_groups.emplace_back();
    }
    chain_groups.back().push_back(groups.size());
    groups.push_back({r, file_order.size()});
    for (const auto a : atoms_of_residue[r]) {
      file_position[a] = file_order.size();
      file_order.push_back(a);
    }
  }
  const bool reordered = !std::is_sorted(file_order.begin(), file_order.end());

  // Group types: identical name/atoms/charges/internal bonds share one entry.
  struct TypeKey {
    std::string name;
    std::vector<std::string> names, elements;
    std::vector<std::int32_t> charges;
    std::vector<std::int32_t> bond_atoms, bond_orders;
    std::string chem;
    bool operator<(const TypeKey &o) const {
      return std::tie(name, names, elements, charges, bond_atoms, bond_orders, chem) <
             std::tie(o.name, o.names, o.elements, o.charges, o.bond_atoms, o.bond_orders, o.chem);
    }
  };
  // Bonds by residue pair.
  std::vector<std::vector<std::pair<std::size_t, int>>> bonds_of_atom(atoms.size());
  for (const auto &bond : topology.bonds()) {
    bonds_of_atom[bond.first.value].push_back({bond.second.value, bond_code(bond.order)});
    bonds_of_atom[bond.second.value].push_back({bond.first.value, bond_code(bond.order)});
  }
  std::map<TypeKey, std::size_t> type_index;
  std::vector<TypeKey> types;
  std::vector<std::int32_t> group_type_list;
  for (const auto &group : groups) {
    TypeKey key;
    key.name = residues[group.residue].name;
    key.chem = chem_comp_type(residues[group.residue]);
    const auto &members = atoms_of_residue[group.residue];
    for (const auto a : members) {
      key.names.push_back(atoms[a].name);
      key.elements.push_back(std::string{element_symbol(atoms[a].atomic_number)});
      key.charges.push_back(atoms[a].formal_charge);
    }
    for (std::size_t i = 0; i < members.size(); ++i)
      for (const auto &[other, order] : bonds_of_atom[members[i]]) {
        if (other < members[i]) continue;
        const auto found = std::find(members.begin(), members.end(), other);
        if (found == members.end()) continue;
        key.bond_atoms.push_back(static_cast<std::int32_t>(i));
        key.bond_atoms.push_back(static_cast<std::int32_t>(found - members.begin()));
        key.bond_orders.push_back(order);
      }
    const auto [it, inserted] = type_index.emplace(key, types.size());
    if (inserted) types.push_back(key);
    group_type_list.push_back(static_cast<std::int32_t>(it->second));
  }

  // Inter-group bonds in file positions.
  std::vector<std::int32_t> inter_atoms, inter_orders;
  for (const auto &bond : topology.bonds()) {
    if (atoms[bond.first.value].residue.value == atoms[bond.second.value].residue.value) continue;
    inter_atoms.push_back(static_cast<std::int32_t>(file_position[bond.first.value]));
    inter_atoms.push_back(static_cast<std::int32_t>(file_position[bond.second.value]));
    inter_orders.push_back(bond_code(bond.order));
  }
  const auto bonds_per_model = topology.bonds().size();
  std::uint64_t lossy_bonds = 0;
  for (const auto &bond : topology.bonds())
    if (bond.order != model::BondOrder::single && bond.order != model::BondOrder::double_bond &&
        bond.order != model::BondOrder::triple)
      ++lossy_bonds;
  const auto model_count = frame_indices.size();

  // Per-group lists.
  std::vector<std::int32_t> group_ids, insertion, secondary;
  const auto *ss_property = topology.properties().find("ss");
  const auto *ss_values = ss_property == nullptr ? nullptr : std::get_if<std::vector<std::string>>(&ss_property->values);
  bool any_ss = false;
  for (const auto &group : groups) {
    const auto &residue = residues[group.residue];
    group_ids.push_back(static_cast<std::int32_t>(residue.sequence_number));
    insertion.push_back(residue.insertion_code.empty() ? 0 : static_cast<unsigned char>(residue.insertion_code.front()));
    std::int32_t code = -1;
    if (ss_values != nullptr) {
      const auto first = atoms_of_residue[group.residue].front();
      const auto &letter = (*ss_values)[first];
      if (letter == "H") code = 2;
      else if (letter == "S") code = 3;
      else code = 7;
      any_ss = any_ss || letter == "H" || letter == "S";
    }
    secondary.push_back(code);
  }

  // Coordinates and per-atom lists across models.
  std::vector<double> xs, ys, zs, bfactors, occupancies;
  std::vector<std::int32_t> serials, altlocs;
  bool any_b = false, any_occ = false, any_alt = false, any_serial = false;
  std::uint64_t missing_atoms = 0;
  for (std::size_t m = 0; m < model_count; ++m) {
    const auto frame = coordinates.read_frame(frame_indices[m]);
    if (!frame.has_value()) return R::failure(frame.error());
    const auto scale = frame.value()->metadata().coordinate_unit == operation::LengthUnit::nanometer ? 10.0 : 1.0;
    const auto &properties = frame.value()->metadata().atom_properties;
    const auto b_found = properties.find("b_iso_or_equiv");
    const auto o_found = properties.find("occupancy");
    const auto *b_values = b_found == properties.end() ? nullptr : std::get_if<std::vector<double>>(&b_found->second.values);
    const auto *o_values = o_found == properties.end() ? nullptr : std::get_if<std::vector<double>>(&o_found->second.values);
    for (const auto a : file_order) {
      if (!frame.value()->atom_present(a)) {
        ++missing_atoms;
        xs.push_back(0.0); ys.push_back(0.0); zs.push_back(0.0);
      } else {
        const auto p = position_at(*frame.value(), a);
        xs.push_back(p.x * scale); ys.push_back(p.y * scale); zs.push_back(p.z * scale);
      }
      bfactors.push_back(b_values != nullptr ? (*b_values)[a] : 0.0);
      occupancies.push_back(o_values != nullptr ? (*o_values)[a] : 1.0);
      any_b = any_b || b_values != nullptr;
      any_occ = any_occ || o_values != nullptr;
      if (m == 0) {
        serials.push_back(atoms[a].source_serial.has_value() ? static_cast<std::int32_t>(*atoms[a].source_serial)
                                                             : static_cast<std::int32_t>(file_position[a] + 1U));
        any_serial = any_serial || atoms[a].source_serial.has_value();
        const auto &alt = atoms[a].alternate_location;
        altlocs.push_back(alt.empty() ? 0 : static_cast<unsigned char>(alt.front()));
        any_alt = any_alt || !alt.empty();
      }
    }
  }
  // atom-id / altloc lists repeat per model
  {
    const auto base_serial = serials;
    const auto base_alt = altlocs;
    for (std::size_t m = 1; m < model_count; ++m) {
      serials.insert(serials.end(), base_serial.begin(), base_serial.end());
      altlocs.insert(altlocs.end(), base_alt.begin(), base_alt.end());
    }
  }

  // Chains / models layout (every model repeats the chain structure).
  std::vector<std::string> chain_ids, chain_names;
  std::vector<std::int32_t> groups_per_chain;
  std::vector<std::int32_t> group_ids_all, group_types_all, secondary_all, insertion_all;
  for (std::size_t m = 0; m < model_count; ++m) {
    for (std::size_t c = 0; c < chain_groups.size(); ++c) {
      std::string label = chain_labels[c];
      if (label.empty()) label = "A";
      chain_ids.push_back(label);
      chain_names.push_back(label);
      groups_per_chain.push_back(static_cast<std::int32_t>(chain_groups[c].size()));
    }
    group_ids_all.insert(group_ids_all.end(), group_ids.begin(), group_ids.end());
    group_types_all.insert(group_types_all.end(), group_type_list.begin(), group_type_list.end());
    secondary_all.insert(secondary_all.end(), secondary.begin(), secondary.end());
    insertion_all.insert(insertion_all.end(), insertion.begin(), insertion.end());
  }
  std::vector<std::int32_t> chains_per_model(model_count, static_cast<std::int32_t>(chain_groups.size()));
  std::vector<std::int32_t> inter_all_atoms, inter_all_orders;
  for (std::size_t m = 0; m < model_count; ++m) {
    for (const auto v : inter_atoms) inter_all_atoms.push_back(v + static_cast<std::int32_t>(m * atoms.size()));
    inter_all_orders.insert(inter_all_orders.end(), inter_orders.begin(), inter_orders.end());
  }

  // Entities: one per chain (polymer / water / non-polymer) of the first model.
  struct Entity { std::string type, sequence; std::vector<std::int32_t> chains; };
  std::vector<Entity> entities;
  for (std::size_t c = 0; c < chain_groups.size(); ++c) {
    Entity entity;
    bool polymer = false, water = true;
    for (const auto g : chain_groups[c]) {
      const auto &residue = residues[groups[g].residue];
      const auto chem = chem_comp_type(residue);
      polymer = polymer || chem == "L-PEPTIDE LINKING" || chem == "DNA LINKING" || chem == "RNA LINKING";
      water = water && is_water(residue);
      if (chem == "L-PEPTIDE LINKING" || chem == "DNA LINKING" || chem == "RNA LINKING")
        entity.sequence.push_back(one_letter(residue.name));
    }
    entity.type = polymer ? "polymer" : (water ? "water" : "non-polymer");
    if (!polymer) entity.sequence.clear();
    for (std::size_t m = 0; m < model_count; ++m)
      entity.chains.push_back(static_cast<std::int32_t>(m * chain_groups.size() + c));
    entities.push_back(std::move(entity));
  }

  // ---- pack
  Buffer buffer;
  msgpack::packer<Buffer> packer(buffer);
  std::uint32_t fields = 17U;  // required keys below
  if (any_b) ++fields;
  if (any_occ) ++fields;
  if (any_serial) ++fields;
  if (any_alt) ++fields;
  if (any_ss) ++fields;
  const bool any_insertion = std::any_of(insertion.begin(), insertion.end(), [](auto v) { return v != 0; });
  if (any_insertion) ++fields;
  const auto cell_unit = coordinates.read_frame(frame_indices.front());
  const bool has_cell = cell_unit.has_value() && cell_unit.value()->metadata().unit_cell.has_value();
  if (has_cell) ++fields;
  if (!inter_all_atoms.empty()) fields += 2U;
  if (!entities.empty()) ++fields;
  packer.pack_map(fields);
  const auto put = [&](std::string_view key) { pack_string(packer, key); };
  put("mmtfVersion"); pack_string(packer, "1.0.0");
  put("mmtfProducer"); pack_string(packer, std::string{"MolShredder "} + MOLSHREDDER_VERSION);
  put("numBonds"); packer.pack_uint64(bonds_per_model * model_count);
  put("numAtoms"); packer.pack_uint64(atoms.size() * model_count);
  put("numGroups"); packer.pack_uint64(groups.size() * model_count);
  put("numChains"); packer.pack_uint64(chain_groups.size() * model_count);
  put("numModels"); packer.pack_uint64(model_count);
  put("groupList");
  packer.pack_array(static_cast<std::uint32_t>(types.size()));
  for (const auto &type : types) {
    packer.pack_map(8U);
    put("formalChargeList"); packer.pack_array(static_cast<std::uint32_t>(type.charges.size()));
    for (const auto v : type.charges) packer.pack_int32(v);
    put("atomNameList"); packer.pack_array(static_cast<std::uint32_t>(type.names.size()));
    for (const auto &n : type.names) pack_string(packer, n);
    put("elementList"); packer.pack_array(static_cast<std::uint32_t>(type.elements.size()));
    for (const auto &e : type.elements) pack_string(packer, e);
    put("bondAtomList"); packer.pack_array(static_cast<std::uint32_t>(type.bond_atoms.size()));
    for (const auto v : type.bond_atoms) packer.pack_int32(v);
    put("bondOrderList"); packer.pack_array(static_cast<std::uint32_t>(type.bond_orders.size()));
    for (const auto v : type.bond_orders) packer.pack_int32(v);
    put("groupName"); pack_string(packer, type.name);
    put("singleLetterCode"); pack_string(packer, std::string(1U, one_letter(type.name)));
    put("chemCompType"); pack_string(packer, type.chem);
  }
  for (const auto &[key, values] : {std::pair<std::string_view, const std::vector<double> *>{"xCoordList", &xs},
                                    {"yCoordList", &ys}, {"zCoordList", &zs}}) {
    const auto encoded = encode_float_delta(*values, 1000);
    if (!encoded.has_value()) return R::failure(encoded.error());
    put(key);
    pack_bin(packer, encoded.value());
  }
  if (any_b) {
    const auto encoded = encode_float_delta(bfactors, 100);
    if (!encoded.has_value()) return R::failure(encoded.error());
    put("bFactorList"); pack_bin(packer, encoded.value());
  }
  if (any_occ) { put("occupancyList"); pack_bin(packer, encode_float_rle(occupancies, 100)); }
  if (any_serial) { put("atomIdList"); pack_bin(packer, encode_delta_rle(serials)); }
  if (any_alt) { put("altLocList"); pack_bin(packer, encode_char_rle(altlocs)); }
  put("groupIdList"); pack_bin(packer, encode_delta_rle(group_ids_all));
  put("groupTypeList"); pack_bin(packer, encode_int32(group_types_all));
  if (any_ss) { put("secStructList"); pack_bin(packer, encode_int8(secondary_all)); }
  if (any_insertion) { put("insCodeList"); pack_bin(packer, encode_char_rle(insertion_all)); }
  put("chainIdList"); pack_bin(packer, encode_strings(chain_ids, 4U));
  put("chainNameList"); pack_bin(packer, encode_strings(chain_names, 4U));
  put("groupsPerChain"); packer.pack_array(static_cast<std::uint32_t>(groups_per_chain.size()));
  for (const auto v : groups_per_chain) packer.pack_int32(v);
  put("chainsPerModel"); packer.pack_array(static_cast<std::uint32_t>(chains_per_model.size()));
  for (const auto v : chains_per_model) packer.pack_int32(v);
  if (!inter_all_atoms.empty()) {
    put("bondAtomList"); pack_bin(packer, encode_int32(inter_all_atoms));
    put("bondOrderList"); pack_bin(packer, encode_int8(inter_all_orders));
  }
  if (!entities.empty()) {
    put("entityList");
    packer.pack_array(static_cast<std::uint32_t>(entities.size()));
    for (const auto &entity : entities) {
      packer.pack_map(4U);
      put("chainIndexList"); packer.pack_array(static_cast<std::uint32_t>(entity.chains.size()));
      for (const auto v : entity.chains) packer.pack_int32(v);
      put("description"); pack_string(packer, "");
      put("type"); pack_string(packer, entity.type);
      put("sequence"); pack_string(packer, entity.sequence);
    }
  }
  if (has_cell) {
    const auto &cell = *cell_unit.value()->metadata().unit_cell;
    const auto length = [](model::Vec3d v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
    const auto dot = [](model::Vec3d a, model::Vec3d b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    const auto angle = [&](model::Vec3d a, model::Vec3d b) {
      return std::acos(std::clamp(dot(a, b) / (length(a) * length(b)), -1.0, 1.0)) * 180.0 / 3.14159265358979323846;
    };
    put("unitCell");
    packer.pack_array(6U);
    packer.pack_float(static_cast<float>(length(cell.a)));
    packer.pack_float(static_cast<float>(length(cell.b)));
    packer.pack_float(static_cast<float>(length(cell.c)));
    packer.pack_float(static_cast<float>(angle(cell.b, cell.c)));
    packer.pack_float(static_cast<float>(angle(cell.a, cell.c)));
    packer.pack_float(static_cast<float>(angle(cell.a, cell.b)));
  }
  if (context.report_progress) context.report_progress({1.0, "write-mmtf"});
  output.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  if (!output) return R::failure(operation::Error{operation::ErrorCode::internal, "could not write MMTF output", {}});
  std::vector<FormatLoss> report_losses;
  if (reordered)
    report_losses.push_back({"atom-order", atoms.size(), "atoms were regrouped by residue (MMTF stores atoms in group order)"});
  if (missing_atoms > 0U)
    report_losses.push_back({"missing-coordinates", missing_atoms, "atoms absent from a frame were written at the origin"});
  if (lossy_bonds > 0U)
    report_losses.push_back({"bond-orders", lossy_bonds,
                             "MMTF stores bond orders 1-4 only; aromatic, amide, zero, query and unknown orders were written as single"});
  if (!any_serial) report_losses.push_back({"atom-ids", atoms.size(), "atoms without source serials were numbered from 1"});
  return R::success(StructureWriteReport{StructureFormat::mmtf, atoms.size(), model_count,
                                         static_cast<std::uint64_t>(buffer.size()), std::move(report_losses)});
}

} // namespace molshredder::io::detail
