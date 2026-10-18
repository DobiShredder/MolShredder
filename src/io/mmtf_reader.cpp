#include "structure_reader_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <set>
#include <type_traits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <msgpack.hpp>

// MMTF (Macromolecular Transmission Format) v1.0 reader. Implemented from the
// public specification; the binary codec numbering was cross-checked against
// real files (docs/capabilities/tools notes, SEQ-319 S1).
namespace molshredder::io::detail {
namespace {

using operation::Result;

constexpr std::size_t maximum_mmtf_atoms = 50'000'000U;
constexpr std::size_t maximum_mmtf_groups = 20'000'000U;

class MmtfError final : public std::runtime_error {
public:
  explicit MmtfError(std::string message) : std::runtime_error(std::move(message)) {}
};

const msgpack::object *find(const msgpack::object &map, std::string_view key) {
  if (map.type != msgpack::type::MAP) throw MmtfError("expected a MessagePack map");
  const msgpack::object *found{};
  for (std::uint32_t i = 0; i < map.via.map.size; ++i) {
    const auto &item = map.via.map.ptr[i];
    if (item.key.type != msgpack::type::STR) continue;
    if (std::string_view{item.key.via.str.ptr, item.key.via.str.size} == key) {
      if (found != nullptr) throw MmtfError("duplicate field '" + std::string{key} + "'");
      found = &item.val;
    }
  }
  return found;
}

const msgpack::object &required(const msgpack::object &map, std::string_view key) {
  const auto *found = find(map, key);
  if (found == nullptr) throw MmtfError("missing required field '" + std::string{key} + "'");
  return *found;
}

std::int64_t as_integer(const msgpack::object &object, std::string_view what) {
  if (object.type == msgpack::type::POSITIVE_INTEGER) {
    if (object.via.u64 > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
      throw MmtfError(std::string{what} + " is out of range");
    return static_cast<std::int64_t>(object.via.u64);
  }
  if (object.type == msgpack::type::NEGATIVE_INTEGER) return object.via.i64;
  throw MmtfError(std::string{what} + " must be an integer");
}

double as_number(const msgpack::object &object, std::string_view what) {
  if (object.type == msgpack::type::FLOAT32 || object.type == msgpack::type::FLOAT64)
    return object.via.f64;
  return static_cast<double>(as_integer(object, what));
}

std::string as_string(const msgpack::object &object, std::string_view what) {
  if (object.type != msgpack::type::STR) throw MmtfError(std::string{what} + " must be a string");
  return std::string{object.via.str.ptr, object.via.str.size};
}

// ----- binary codecs (MMTF "strategies" 1-15)

struct Bytes {
  const unsigned char *data{};
  std::size_t size{};
};

template <typename T>
std::vector<T> big_endian_array(Bytes bytes, std::size_t count) {
  if (bytes.size != count * sizeof(T)) throw MmtfError("binary field has an inconsistent length");
  std::vector<T> out(count);
  for (std::size_t i = 0; i < count; ++i) {
    std::uint64_t raw = 0;
    for (std::size_t b = 0; b < sizeof(T); ++b) raw = (raw << 8U) | bytes.data[i * sizeof(T) + b];
    if constexpr (std::is_same_v<T, float>) {
      std::uint32_t bits = static_cast<std::uint32_t>(raw);
      std::memcpy(&out[i], &bits, sizeof(float));
    } else {
      using Unsigned = std::make_unsigned_t<T>;
      out[i] = static_cast<T>(static_cast<Unsigned>(raw));
    }
  }
  return out;
}

std::vector<std::int32_t> run_length_decode(const std::vector<std::int32_t> &pairs, std::size_t limit) {
  if (pairs.size() % 2U != 0U) throw MmtfError("run-length data has an odd number of values");
  std::vector<std::int32_t> out;
  for (std::size_t i = 0; i < pairs.size(); i += 2U) {
    const auto value = pairs[i];
    const auto count = pairs[i + 1U];
    if (count < 0 || static_cast<std::size_t>(count) > limit - std::min(limit, out.size()))
      throw MmtfError("run-length data expands beyond the size limit");
    out.insert(out.end(), static_cast<std::size_t>(count), value);
  }
  return out;
}

void delta_decode(std::vector<std::int32_t> &values) {
  std::int32_t sum = 0;
  for (auto &value : values) {
    sum = static_cast<std::int32_t>(static_cast<std::uint32_t>(sum) + static_cast<std::uint32_t>(value));
    value = sum;
  }
}

template <typename Small>
std::vector<std::int32_t> recursive_index_decode(const std::vector<Small> &input) {
  constexpr std::int32_t high = std::numeric_limits<Small>::max();
  constexpr std::int32_t low = std::numeric_limits<Small>::min();
  std::vector<std::int32_t> out;
  std::int32_t current = 0;
  bool pending = false;
  for (const auto raw : input) {
    const std::int32_t value = raw;
    current += value;
    pending = true;
    if (value != high && value != low) {
      out.push_back(current);
      current = 0;
      pending = false;
    }
  }
  if (pending) out.push_back(current);
  return out;
}

std::vector<double> to_float(const std::vector<std::int32_t> &values, std::int32_t divisor) {
  if (divisor == 0) throw MmtfError("integer-encoded float has a zero divisor");
  std::vector<double> out(values.size());
  for (std::size_t i = 0; i < values.size(); ++i)
    out[i] = static_cast<double>(static_cast<float>(values[i]) / static_cast<float>(divisor));
  return out;
}

struct Decoded {
  std::vector<std::int32_t> integers;
  std::vector<double> floats;
  std::vector<std::string> strings;
  bool is_float{};
  bool is_string{};
};

Decoded decode_binary(const msgpack::object &object, std::string_view what, std::size_t limit) {
  if (object.type != msgpack::type::BIN) throw MmtfError(std::string{what} + " must be binary");
  const auto *raw = reinterpret_cast<const unsigned char *>(object.via.bin.ptr);
  if (object.via.bin.size < 12U) throw MmtfError(std::string{what} + " has a truncated codec header");
  const auto header = big_endian_array<std::int32_t>(Bytes{raw, 12U}, 3U);
  const auto strategy = header[0];
  const auto length = header[1];
  const auto parameter = header[2];
  if (length < 0 || static_cast<std::size_t>(length) > limit)
    throw MmtfError(std::string{what} + " declares an unreasonable length");
  const auto count = static_cast<std::size_t>(length);
  const Bytes body{raw + 12U, object.via.bin.size - 12U};
  Decoded out;
  const auto array_of = [&](auto tag) {
    using T = decltype(tag);
    // The encoded array length differs from the decoded length for packed strategies.
    const std::size_t elements = body.size / sizeof(T);
    return big_endian_array<T>(body, elements);
  };
  const auto check = [&](std::size_t decoded) {
    if (decoded != count) throw MmtfError(std::string{what} + " decodes to a different length than declared");
  };
  switch (strategy) {
    case 1: {
      auto values = array_of(float{});
      check(values.size());
      out.floats.assign(values.begin(), values.end());
      out.is_float = true;
      break;
    }
    case 2: {
      auto values = array_of(std::int8_t{});
      check(values.size());
      out.integers.assign(values.begin(), values.end());
      break;
    }
    case 3: {
      auto values = array_of(std::int16_t{});
      check(values.size());
      out.integers.assign(values.begin(), values.end());
      break;
    }
    case 4: {
      out.integers = array_of(std::int32_t{});
      check(out.integers.size());
      break;
    }
    case 5: {
      if (parameter <= 0) throw MmtfError(std::string{what} + " has an invalid string length");
      const auto width = static_cast<std::size_t>(parameter);
      if (body.size % width != 0U) throw MmtfError(std::string{what} + " has a truncated string array");
      check(body.size / width);
      for (std::size_t i = 0; i < count; ++i) {
        std::string text(reinterpret_cast<const char *>(body.data + i * width), width);
        const auto end = text.find('\0');
        if (end != std::string::npos) text.resize(end);
        out.strings.push_back(std::move(text));
      }
      out.is_string = true;
      break;
    }
    case 6:
    case 7: {
      auto pairs = array_of(std::int32_t{});
      out.integers = run_length_decode(pairs, limit);
      check(out.integers.size());
      break;
    }
    case 8: {
      auto pairs = array_of(std::int32_t{});
      out.integers = run_length_decode(pairs, limit);
      delta_decode(out.integers);
      check(out.integers.size());
      break;
    }
    case 9: {
      auto pairs = array_of(std::int32_t{});
      out.floats = to_float(run_length_decode(pairs, limit), parameter);
      out.is_float = true;
      check(out.floats.size());
      break;
    }
    case 10: {
      auto packed = array_of(std::int16_t{});
      auto values = recursive_index_decode(packed);
      delta_decode(values);
      out.floats = to_float(values, parameter);
      out.is_float = true;
      check(out.floats.size());
      break;
    }
    case 11: {
      auto packed = array_of(std::int16_t{});
      out.floats = to_float(std::vector<std::int32_t>(packed.begin(), packed.end()), parameter);
      out.is_float = true;
      check(out.floats.size());
      break;
    }
    case 12: {
      auto packed = array_of(std::int16_t{});
      out.floats = to_float(recursive_index_decode(packed), parameter);
      out.is_float = true;
      check(out.floats.size());
      break;
    }
    case 13: {
      auto packed = array_of(std::int8_t{});
      out.floats = to_float(recursive_index_decode(packed), parameter);
      out.is_float = true;
      check(out.floats.size());
      break;
    }
    case 14: {
      auto packed = array_of(std::int16_t{});
      out.integers = recursive_index_decode(packed);
      check(out.integers.size());
      break;
    }
    case 15: {
      auto packed = array_of(std::int8_t{});
      out.integers = recursive_index_decode(packed);
      check(out.integers.size());
      break;
    }
    default:
      throw MmtfError(std::string{what} + " uses the unsupported codec " + std::to_string(strategy));
  }
  return out;
}

// Integer list that may be a plain MessagePack array or a binary field.
std::vector<std::int32_t> integer_list(const msgpack::object &object, std::string_view what,
                                       std::size_t limit) {
  if (object.type == msgpack::type::BIN) {
    auto decoded = decode_binary(object, what, limit);
    if (decoded.is_float || decoded.is_string) throw MmtfError(std::string{what} + " must hold integers");
    return decoded.integers;
  }
  if (object.type != msgpack::type::ARRAY) throw MmtfError(std::string{what} + " must be a list");
  if (object.via.array.size > limit) throw MmtfError(std::string{what} + " is too long");
  std::vector<std::int32_t> out;
  out.reserve(object.via.array.size);
  for (std::uint32_t i = 0; i < object.via.array.size; ++i) {
    const auto value = as_integer(object.via.array.ptr[i], what);
    if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max())
      throw MmtfError(std::string{what} + " holds a value outside 32 bits");
    out.push_back(static_cast<std::int32_t>(value));
  }
  return out;
}

std::vector<double> float_list(const msgpack::object &object, std::string_view what, std::size_t limit) {
  if (object.type == msgpack::type::BIN) {
    auto decoded = decode_binary(object, what, limit);
    if (!decoded.is_float) throw MmtfError(std::string{what} + " must hold floats");
    return decoded.floats;
  }
  if (object.type != msgpack::type::ARRAY) throw MmtfError(std::string{what} + " must be a list");
  if (object.via.array.size > limit) throw MmtfError(std::string{what} + " is too long");
  std::vector<double> out;
  for (std::uint32_t i = 0; i < object.via.array.size; ++i)
    out.push_back(as_number(object.via.array.ptr[i], what));
  return out;
}

std::vector<std::string> string_list(const msgpack::object &object, std::string_view what, std::size_t limit) {
  if (object.type == msgpack::type::BIN) {
    auto decoded = decode_binary(object, what, limit);
    if (!decoded.is_string) throw MmtfError(std::string{what} + " must hold strings");
    return decoded.strings;
  }
  if (object.type != msgpack::type::ARRAY) throw MmtfError(std::string{what} + " must be a list");
  if (object.via.array.size > limit) throw MmtfError(std::string{what} + " is too long");
  std::vector<std::string> out;
  for (std::uint32_t i = 0; i < object.via.array.size; ++i)
    out.push_back(as_string(object.via.array.ptr[i], what));
  return out;
}

// A one-character code list (altLoc, insertion code): integers, 0 = none.
std::vector<std::string> char_list(const msgpack::object &object, std::string_view what, std::size_t limit) {
  const auto codes = integer_list(object, what, limit);
  std::vector<std::string> out;
  out.reserve(codes.size());
  for (const auto code : codes) {
    if (code < 0 || code > 127) throw MmtfError(std::string{what} + " holds a non-ASCII character code");
    out.push_back(code == 0 ? std::string{} : std::string(1U, static_cast<char>(code)));
  }
  return out;
}

struct GroupType {
  std::string name;
  std::vector<std::int32_t> formal_charges;
  std::vector<std::string> atom_names;
  std::vector<std::string> elements;
  std::vector<std::int32_t> bond_atoms;
  std::vector<std::int32_t> bond_orders;
  std::string chem_comp_type;
};

GroupType parse_group_type(const msgpack::object &object) {
  GroupType group;
  group.name = as_string(required(object, "groupName"), "groupName");
  group.atom_names = string_list(required(object, "atomNameList"), "atomNameList", 1'000'000U);
  group.elements = string_list(required(object, "elementList"), "elementList", 1'000'000U);
  const auto atoms = group.atom_names.size();
  // Charges and bonds are optional in practice (files without them exist).
  if (const auto *v = find(object, "formalChargeList"); v != nullptr)
    group.formal_charges = integer_list(*v, "formalChargeList", 1'000'000U);
  else
    group.formal_charges.assign(atoms, 0);
  if (const auto *v = find(object, "bondAtomList"); v != nullptr)
    group.bond_atoms = integer_list(*v, "bondAtomList", 4'000'000U);
  if (const auto *v = find(object, "bondOrderList"); v != nullptr)
    group.bond_orders = integer_list(*v, "bondOrderList", 2'000'000U);
  if (const auto *v = find(object, "chemCompType"); v != nullptr && v->type == msgpack::type::STR)
    group.chem_comp_type = as_string(*v, "chemCompType");
  if (group.formal_charges.size() != atoms || group.elements.size() != atoms)
    throw MmtfError("group '" + group.name + "' has inconsistent atom lists");
  // A missing order list means "bonded, order unknown".
  if (group.bond_orders.empty() && !group.bond_atoms.empty() && group.bond_atoms.size() % 2U == 0U)
    group.bond_orders.assign(group.bond_atoms.size() / 2U, 0);
  if (group.bond_atoms.size() != 2U * group.bond_orders.size())
    throw MmtfError("group '" + group.name + "' has inconsistent bond lists (" +
                    std::to_string(group.bond_atoms.size()) + " atom entries, " +
                    std::to_string(group.bond_orders.size()) + " orders)");
  for (const auto index : group.bond_atoms)
    if (index < 0 || static_cast<std::size_t>(index) >= atoms)
      throw MmtfError("group '" + group.name + "' has a bond to a missing atom");
  return group;
}

model::BondOrder bond_order_of(std::int32_t order) {
  switch (order) {
    case 1: return model::BondOrder::single;
    case 2: return model::BondOrder::double_bond;
    case 3: return model::BondOrder::triple;
    default: return model::BondOrder::unknown;
  }
}

bool contains(std::string_view text, std::string_view needle) {
  std::string upper{text};
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return upper.find(needle) != std::string::npos;
}

// chemCompType (PDB chemical-component "type") decides what a residue is; groups
// without it keep the untyped (name-heuristic) classification.
void classify_residue(const GroupType &type, model::ResidueRecord &record,
                      const std::string &entity_type) {
  if (!entity_type.empty() && entity_type != "polymer") {
    // Entities that are not polymers (free amino acids, ligands, water, glycans)
    // are never polymer residues, whatever their chemical-component type says.
    record.chemical_origin = model::ChemicalAnnotationOrigin::explicit_input;
    if (entity_type == "water" || type.name == "HOH" || type.name == "WAT" || type.name == "DOD")
      record.kind = model::ResidueKind::solvent;
    else if (entity_type == "branched" || contains(type.chem_comp_type, "SACCHARIDE"))
      record.kind = model::ResidueKind::carbohydrate;
    else if (type.atom_names.size() == 1U)
      record.kind = model::ResidueKind::ion;
    else
      record.kind = model::ResidueKind::ligand;
    return;
  }
  if (type.chem_comp_type.empty()) return;
  const auto &t = type.chem_comp_type;
  using model::PolymerType;
  using model::ResidueKind;
  record.chemical_origin = model::ChemicalAnnotationOrigin::explicit_input;
  if (contains(t, "PEPTIDE LINKING") || contains(t, "PEPTIDE COOH") || contains(t, "PEPTIDE NH3")) {
    record.kind = ResidueKind::amino_acid;
    record.polymer_type = PolymerType::protein;
  } else if (contains(t, "DNA LINKING") || contains(t, "DNA OH")) {
    record.kind = ResidueKind::nucleic_acid;
    record.polymer_type = PolymerType::dna;
  } else if (contains(t, "RNA LINKING") || contains(t, "RNA OH")) {
    record.kind = ResidueKind::nucleic_acid;
    record.polymer_type = PolymerType::rna;
  } else if (contains(t, "SACCHARIDE")) {
    record.kind = ResidueKind::carbohydrate;
  } else if (type.name == "HOH" || type.name == "WAT" || type.name == "DOD" || type.name == "H2O") {
    record.kind = ResidueKind::solvent;
  } else if (type.atom_names.size() == 1U) {
    record.kind = ResidueKind::ion;
  } else {
    record.kind = ResidueKind::ligand;
  }
}

std::string structure_name(std::string_view source_name) {
  if (source_name == "<memory>") return "mmtf_structure";
  auto name = std::filesystem::path{source_name}.stem().string();
  return name.empty() ? std::string{"mmtf_structure"} : name;
}

std::string capitalize(std::string text) {
  for (std::size_t i = 0; i < text.size(); ++i)
    text[i] = static_cast<char>(i == 0 ? std::toupper(static_cast<unsigned char>(text[i]))
                                       : std::tolower(static_cast<unsigned char>(text[i])));
  return text;
}

} // namespace

Result<StructureDocument> read_mmtf(std::string_view content, std::string source_name) {
  try {
    std::size_t offset{};
    const msgpack::unpack_limit limits{10'000'000U, 10'000'000U, content.size(), content.size(), 0U, 64U};
    const auto unpacked =
        msgpack::unpack(content.data(), content.size(), offset, nullptr, nullptr, limits);
    if (offset != content.size()) throw MmtfError("trailing bytes follow the MessagePack object");
    const auto &root = unpacked.get();
    const auto version = as_string(required(root, "mmtfVersion"), "mmtfVersion");
    {
      std::size_t used = 0;
      int major = -1;
      try { major = std::stoi(version, &used); } catch (const std::exception &) { major = -1; }
      if (major < 0 || major > 1)
        throw MmtfError("unsupported MMTF version " + version + "; this reader supports 0.x and 1.x");
    }

    const auto atom_total = as_integer(required(root, "numAtoms"), "numAtoms");
    const auto group_total = as_integer(required(root, "numGroups"), "numGroups");
    const auto chain_total = as_integer(required(root, "numChains"), "numChains");
    const auto model_total = as_integer(required(root, "numModels"), "numModels");
    if (atom_total <= 0 || static_cast<std::size_t>(atom_total) > maximum_mmtf_atoms)
      throw MmtfError("numAtoms is zero or beyond the supported size");
    if (group_total < 0 || static_cast<std::size_t>(group_total) > maximum_mmtf_groups)
      throw MmtfError("numGroups is beyond the supported size");
    if (model_total <= 0 || chain_total <= 0) throw MmtfError("the file contains no models or chains");
    const auto atoms = static_cast<std::size_t>(atom_total);
    const auto groups = static_cast<std::size_t>(group_total);

    std::vector<GroupType> group_types;
    const auto &group_list = required(root, "groupList");
    if (group_list.type != msgpack::type::ARRAY) throw MmtfError("groupList must be a list");
    for (std::uint32_t i = 0; i < group_list.via.array.size; ++i)
      group_types.push_back(parse_group_type(group_list.via.array.ptr[i]));

    const auto xs = float_list(required(root, "xCoordList"), "xCoordList", atoms);
    const auto ys = float_list(required(root, "yCoordList"), "yCoordList", atoms);
    const auto zs = float_list(required(root, "zCoordList"), "zCoordList", atoms);
    if (xs.size() != atoms || ys.size() != atoms || zs.size() != atoms)
      throw MmtfError("coordinate lists do not match numAtoms");
    const auto group_ids = integer_list(required(root, "groupIdList"), "groupIdList", groups);
    const auto group_type_index = integer_list(required(root, "groupTypeList"), "groupTypeList", groups);
    if (group_ids.size() != groups || group_type_index.size() != groups)
      throw MmtfError("group lists do not match numGroups");
    const auto groups_per_chain = integer_list(required(root, "groupsPerChain"), "groupsPerChain", 10'000'000U);
    const auto chains_per_model = integer_list(required(root, "chainsPerModel"), "chainsPerModel", 10'000'000U);
    const auto chain_ids = string_list(required(root, "chainIdList"), "chainIdList", 10'000'000U);
    std::vector<std::string> chain_names;
    if (const auto *names = find(root, "chainNameList"); names != nullptr)
      chain_names = string_list(*names, "chainNameList", 10'000'000U);
    if (groups_per_chain.size() != static_cast<std::size_t>(chain_total) ||
        chain_ids.size() != groups_per_chain.size() ||
        (!chain_names.empty() && chain_names.size() != groups_per_chain.size()))
      throw MmtfError("chain lists do not match numChains");
    if (chains_per_model.size() != static_cast<std::size_t>(model_total))
      throw MmtfError("chainsPerModel does not match numModels");

    // Entity type per chain (polymer, non-polymer, water, branched) when the file has entities.
    std::vector<std::string> entity_of_chain(groups_per_chain.size());
    if (const auto *entities = find(root, "entityList"); entities != nullptr && entities->type == msgpack::type::ARRAY) {
      for (std::uint32_t e = 0; e < entities->via.array.size; ++e) {
        const auto &entity = entities->via.array.ptr[e];
        const auto *type_field = find(entity, "type");
        const auto *chain_field = find(entity, "chainIndexList");
        if (type_field == nullptr || chain_field == nullptr || type_field->type != msgpack::type::STR) continue;
        const auto type_text = as_string(*type_field, "entity type");
        for (const auto chain_index : integer_list(*chain_field, "chainIndexList", groups_per_chain.size()))
          if (chain_index >= 0 && static_cast<std::size_t>(chain_index) < entity_of_chain.size())
            entity_of_chain[static_cast<std::size_t>(chain_index)] = type_text;
      }
    }
    const auto chain_entity_type = [&](std::size_t chain) -> const std::string & { return entity_of_chain[chain]; };
    std::vector<std::int32_t> serials;
    std::vector<std::string> altlocs;
    std::vector<std::string> insertion_codes;
    std::vector<double> b_factors;
    std::vector<double> occupancies;
    std::vector<std::int32_t> secondary;
    if (const auto *v = find(root, "atomIdList"); v != nullptr) serials = integer_list(*v, "atomIdList", atoms);
    if (const auto *v = find(root, "altLocList"); v != nullptr) altlocs = char_list(*v, "altLocList", atoms);
    if (const auto *v = find(root, "insCodeList"); v != nullptr)
      insertion_codes = char_list(*v, "insCodeList", groups);
    if (const auto *v = find(root, "bFactorList"); v != nullptr) b_factors = float_list(*v, "bFactorList", atoms);
    if (const auto *v = find(root, "occupancyList"); v != nullptr)
      occupancies = float_list(*v, "occupancyList", atoms);
    if (const auto *v = find(root, "secStructList"); v != nullptr)
      secondary = integer_list(*v, "secStructList", groups);
    const auto expect = [&](std::size_t size, std::size_t wanted, const char *name) {
      if (size != 0U && size != wanted) throw MmtfError(std::string{name} + " does not match the atom or group count");
    };
    expect(serials.size(), atoms, "atomIdList");
    expect(altlocs.size(), atoms, "altLocList");
    expect(insertion_codes.size(), groups, "insCodeList");
    expect(b_factors.size(), atoms, "bFactorList");
    expect(occupancies.size(), atoms, "occupancyList");
    expect(secondary.size(), groups, "secStructList");

    std::vector<std::int32_t> inter_atoms;
    std::vector<std::int32_t> inter_orders;
    if (const auto *v = find(root, "bondAtomList"); v != nullptr) inter_atoms = integer_list(*v, "bondAtomList", atoms * 8U);
    if (const auto *v = find(root, "bondOrderList"); v != nullptr) inter_orders = integer_list(*v, "bondOrderList", atoms * 4U);
    if (inter_atoms.size() != 2U * inter_orders.size()) throw MmtfError("inter-group bond lists are inconsistent");

    // Walk the first model to build the topology; later models must repeat its layout.
    model::TopologyBuilder builder;
    std::size_t atom_cursor = 0;
    std::size_t group_cursor = 0;
    std::size_t chain_cursor = 0;
    std::vector<std::size_t> model_atom_start;
    std::vector<std::string> ss_values;
    bool any_ss = false;
    std::size_t first_model_atoms = 0;
    std::vector<std::size_t> model_atom_count;
    for (std::int64_t model_index = 0; model_index < model_total; ++model_index) {
      const auto model_start = atom_cursor;
      model_atom_start.push_back(model_start);
      for (std::int32_t c = 0; c < chains_per_model[static_cast<std::size_t>(model_index)]; ++c, ++chain_cursor) {
        if (chain_cursor >= groups_per_chain.size()) throw MmtfError("chainsPerModel exceeds numChains");
        const auto chain_name = chain_names.empty() ? chain_ids[chain_cursor] : chain_names[chain_cursor];
        for (std::int32_t g = 0; g < groups_per_chain[chain_cursor]; ++g, ++group_cursor) {
          if (group_cursor >= groups) throw MmtfError("groupsPerChain exceeds numGroups");
          const auto type_index = group_type_index[group_cursor];
          if (type_index < 0 || static_cast<std::size_t>(type_index) >= group_types.size())
            throw MmtfError("groupTypeList references a missing group type");
          const auto &type = group_types[static_cast<std::size_t>(type_index)];
          if (atom_cursor + type.atom_names.size() > atoms) throw MmtfError("groups hold more atoms than numAtoms");
          if (model_index == 0) {
            const auto insertion = insertion_codes.empty() ? std::string{} : insertion_codes[group_cursor];
            model::ResidueRecord record{type.name, group_ids[group_cursor], insertion, chain_name, ""};
            classify_residue(type, record, chain_entity_type(chain_cursor));
            const auto residue = builder.add_residue(std::move(record));
            if (!residue.has_value()) return Result<StructureDocument>::failure(residue.error());
            // MMTF DSSP codes: 0 pi helix, 1 bend, 2 alpha helix, 3 extended, 4 3-10 helix,
            // 5 bridge, 6 turn, 7 coil, -1 undefined. Helices -> H, strands/bridges -> S.
            char ss_letter = '\0';
            if (!secondary.empty()) {
              const auto code = secondary[group_cursor];
              if (code == 0 || code == 2 || code == 4) ss_letter = 'H';
              else if (code == 3 || code == 5) ss_letter = 'S';
            }
            for (std::size_t a = 0; a < type.atom_names.size(); ++a) {
              const auto element = atomic_number(capitalize(type.elements[a]));
              const auto number = element.value_or(0U);
              const auto alt = altlocs.empty() ? std::string{} : altlocs[atom_cursor + a];
              std::optional<std::int64_t> serial;
              if (!serials.empty()) serial = serials[atom_cursor + a];
              const auto added = builder.add_atom({type.atom_names[a], number, residue.value(), alt,
                                                   type.formal_charges[a], serial});
              if (!added.has_value()) return Result<StructureDocument>::failure(added.error());
              ss_values.push_back(ss_letter == '\0' ? std::string{} : std::string(1U, ss_letter));
              any_ss = any_ss || ss_letter != '\0';
            }
            for (std::size_t b = 0; b < type.bond_orders.size(); ++b) {
              const auto first = atom_cursor + static_cast<std::size_t>(type.bond_atoms[2U * b]);
              const auto second = atom_cursor + static_cast<std::size_t>(type.bond_atoms[2U * b + 1U]);
              if (first == second) continue;
              const auto error = builder.add_bond({model::AtomIndex{std::min(first, second)},
                                                   model::AtomIndex{std::max(first, second)},
                                                   bond_order_of(type.bond_orders[b]),
                                                   model::BondQuery::none, model::BondStereo::none,
                                                   model::ChemicalAnnotationOrigin::explicit_input});
              if (error.has_value()) return Result<StructureDocument>::failure(*error);
            }
          }
          atom_cursor += type.atom_names.size();
        }
      }
      if (model_index == 0) first_model_atoms = atom_cursor - model_start;
      model_atom_count.push_back(atom_cursor - model_start);
    }
    if (atom_cursor != atoms) throw MmtfError("the groups hold fewer atoms than numAtoms");
    for (const auto count : model_atom_count)
      if (count != first_model_atoms)
        throw MmtfError("models with different atom counts are not supported");

    // Inter-group bonds (global atom indices); only the first model's atoms define the topology.
    std::set<std::pair<std::size_t, std::size_t>> seen;
    for (std::size_t b = 0; b < inter_orders.size(); ++b) {
      const auto first = inter_atoms[2U * b];
      const auto second = inter_atoms[2U * b + 1U];
      if (first < 0 || second < 0 || static_cast<std::size_t>(first) >= atoms ||
          static_cast<std::size_t>(second) >= atoms)
        throw MmtfError("an inter-group bond references a missing atom");
      if (static_cast<std::size_t>(first) >= first_model_atoms || static_cast<std::size_t>(second) >= first_model_atoms ||
          first == second)
        continue;
      const auto lo = static_cast<std::size_t>(std::min(first, second));
      const auto hi = static_cast<std::size_t>(std::max(first, second));
      if (!seen.insert({lo, hi}).second) continue;
      const auto error = builder.add_bond({model::AtomIndex{lo}, model::AtomIndex{hi},
                                           bond_order_of(inter_orders[b]), model::BondQuery::none,
                                           model::BondStereo::none,
                                           model::ChemicalAnnotationOrigin::explicit_input});
      if (error.has_value()) {
        // A group-internal duplicate of an inter-group bond is tolerated.
        continue;
      }
    }
    if (any_ss) {
      if (const auto error = builder.add_property("ss", std::move(ss_values),
                                                  model::PropertyMetadata{std::nullopt, "MMTF secStructList", {}});
          error.has_value())
        return Result<StructureDocument>::failure(*error);
    }
    builder.set_source_metadata("format", "mmtf");
    builder.set_source_metadata("format_version", version);
    if (const auto *producer = find(root, "mmtfProducer"); producer != nullptr && producer->type == msgpack::type::STR)
      builder.set_source_metadata("mmtf.producer", as_string(*producer, "mmtfProducer"));
    std::string entry_id;
    if (const auto *v = find(root, "structureId"); v != nullptr && v->type == msgpack::type::STR)
      entry_id = as_string(*v, "structureId");
    if (!entry_id.empty()) builder.set_source_metadata("entry_id", entry_id);
    const auto topology = builder.build();
    if (!topology.has_value()) return Result<StructureDocument>::failure(topology.error());

    std::optional<model::UnitCell> unit_cell;
    if (const auto *cell = find(root, "unitCell"); cell != nullptr && cell->type == msgpack::type::ARRAY &&
                                                    cell->via.array.size == 6U) {
      std::array<double, 6> v{};
      for (std::uint32_t i = 0; i < 6U; ++i) v[i] = as_number(cell->via.array.ptr[i], "unitCell");
      if (v[0] > 0.0 && v[1] > 0.0 && v[2] > 0.0) {
        const auto parsed = make_unit_cell(v[0], v[1], v[2], v[3], v[4], v[5], source_name, 1U);
        if (parsed.has_value()) unit_cell = parsed.value();
      }
    }

    std::vector<std::shared_ptr<const model::CoordinateFrame>> frames;
    for (std::int64_t model_index = 0; model_index < model_total; ++model_index) {
      const auto start = model_atom_start[static_cast<std::size_t>(model_index)];
      std::vector<model::Vec3f> positions(first_model_atoms);
      for (std::size_t a = 0; a < first_model_atoms; ++a)
        positions[a] = model::Vec3f{static_cast<float>(xs[start + a]), static_cast<float>(ys[start + a]),
                                    static_cast<float>(zs[start + a])};
      model::FrameMetadata metadata;
      metadata.source_step = static_cast<std::uint64_t>(model_index + 1);
      metadata.unit_cell = unit_cell;
      metadata.coordinate_unit = operation::LengthUnit::angstrom;
      if (!occupancies.empty()) {
        std::vector<double> values(occupancies.begin() + static_cast<std::ptrdiff_t>(start),
                                   occupancies.begin() + static_cast<std::ptrdiff_t>(start + first_model_atoms));
        metadata.atom_properties.emplace(
            "occupancy", model::AtomProperty{std::move(values),
                                             model::PropertyMetadata{std::nullopt, "MMTF occupancyList", {}}});
      }
      if (!b_factors.empty()) {
        std::vector<double> values(b_factors.begin() + static_cast<std::ptrdiff_t>(start),
                                   b_factors.begin() + static_cast<std::ptrdiff_t>(start + first_model_atoms));
        metadata.atom_properties.emplace(
            "b_iso_or_equiv",
            model::AtomProperty{std::move(values),
                                model::PropertyMetadata{"angstrom^2", "MMTF bFactorList", {}}});
      }
      const auto frame = model::CoordinateFrame::create(model::CoordinateBuffer{std::move(positions)},
                                                        std::nullopt, {}, std::move(metadata));
      if (!frame.has_value()) return Result<StructureDocument>::failure(frame.error());
      frames.push_back(frame.value());
    }
    const auto coordinates = model::InMemoryCoordinateSource::create(first_model_atoms, std::move(frames));
    if (!coordinates.has_value()) return Result<StructureDocument>::failure(coordinates.error());

    StructureData structure;
    structure.name = entry_id.empty() ? structure_name(source_name) : entry_id;
    structure.topology = topology.value();
    structure.coordinates = coordinates.value();
    structure.metadata.emplace("format", "mmtf");
    structure.metadata.emplace("mmtf.version", version);
    if (!entry_id.empty()) structure.metadata.emplace("entry_id", entry_id);
    StructureDocument document;
    document.format = StructureFormat::mmtf;
    document.source_name = std::move(source_name);
    document.structures.push_back(std::move(structure));
    return Result<StructureDocument>::success(std::move(document));
  } catch (const MmtfError &error) {
    return Result<StructureDocument>::failure(parse_error(source_name, 1U, std::string{"MMTF: "} + error.what()));
  } catch (const msgpack::unpack_error &error) {
    return Result<StructureDocument>::failure(
        parse_error(source_name, 1U, std::string{"MMTF is not valid MessagePack: "} + error.what()));
  } catch (const std::bad_alloc &) {
    return Result<StructureDocument>::failure(parse_error(source_name, 1U, "MMTF: allocation failed"));
  }
}

} // namespace molshredder::io::detail
