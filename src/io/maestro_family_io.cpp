#include "structure_writer_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "molshredder/core/parse_number.hpp"
#include "structure_reader_internal.hpp"

// MacroModel (MMOD) and Maestro (MAE) readers/writers.
//  * MMOD uses the fixed-column layout documented for MacroModel .dat/.mmd files. The reader was checked against
//    Open-Source PyMOL 3.1.0; the atom-type -> element ranges follow what PyMOL does.
//  * MAE is read and written from the public Maestro block syntax (f_m_ct / m_atom / m_bond); PyMOL's open-source
//    build cannot read or write it, so the tests round-trip and use hand-written Maestro samples.
// Implemented in SEQ-319 S5.
namespace molshredder::io::detail {
namespace {

using operation::Result;

template <typename T>
std::optional<T> to_number(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1U);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1U);
  T value{};
  const auto result = molshredder::core::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()) return std::nullopt;
  return value;
}

bool equal_ci(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
         });
}

std::vector<std::string_view> split_lines(std::string_view content) {
  std::vector<std::string_view> lines;
  std::size_t position = 0;
  while (position <= content.size()) {
    const auto end = content.find('\n', position);
    auto line = content.substr(position, end == std::string_view::npos ? content.size() - position : end - position);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
    lines.push_back(line);
    if (end == std::string_view::npos) break;
    position = end + 1U;
  }
  return lines;
}

std::string_view column(std::string_view line, std::size_t start, std::size_t width) {
  if (start >= line.size()) return {};
  return line.substr(start, std::min(width, line.size() - start));
}

std::string stem_name(std::string_view source_name, std::string_view fallback) {
  if (source_name == "<memory>") return std::string{fallback};
  auto name = std::filesystem::path{source_name}.stem().string();
  return name.empty() ? std::string{fallback} : name;
}

// Element from an atom name when the file has no element field (leading digits are skipped; a two-letter
// element is used only when the whole name repeats the residue name, e.g. ZN in residue ZN).
std::uint8_t element_from_name(std::string_view name, std::string_view residue_name) {
  while (!name.empty() && std::isdigit(static_cast<unsigned char>(name.front())) != 0) name.remove_prefix(1U);
  if (name.empty() || std::isalpha(static_cast<unsigned char>(name.front())) == 0) return 0U;
  if (name.size() == 2U && equal_ci(name, residue_name))
    if (const auto two = atomic_number(name); two.has_value()) return *two;
  std::string letter(1U, static_cast<char>(std::toupper(static_cast<unsigned char>(name.front()))));
  return atomic_number(letter).value_or(0U);
}

Result<StructureData> assemble_structure(std::string name, std::string_view label, std::shared_ptr<const model::Topology> topology,
                                         std::vector<std::vector<model::Vec3d>> position_sets, model::FrameMetadata metadata = {}) {
  std::vector<std::shared_ptr<const model::CoordinateFrame>> frames;
  std::uint64_t step = 1U;
  for (auto &positions : position_sets) {
    auto frame_metadata = metadata;
    frame_metadata.source_step = step++;
    frame_metadata.coordinate_unit = operation::LengthUnit::angstrom;
    const auto frame = model::CoordinateFrame::create(model::CoordinateBuffer{std::move(positions)}, std::nullopt, {}, std::move(frame_metadata));
    if (!frame.has_value()) return Result<StructureData>::failure(frame.error());
    frames.push_back(frame.value());
  }
  const auto coordinates = model::InMemoryCoordinateSource::create(topology->atom_count(), std::move(frames));
  if (!coordinates.has_value()) return Result<StructureData>::failure(coordinates.error());
  StructureData structure;
  structure.name = std::move(name);
  structure.topology = std::move(topology);
  structure.coordinates = coordinates.value();
  structure.metadata.emplace("format", std::string{label});
  return Result<StructureData>::success(std::move(structure));
}

Result<StructureData> assemble_structure(std::string name, std::string_view label, std::shared_ptr<const model::Topology> topology,
                                         std::vector<model::Vec3d> positions, model::FrameMetadata metadata = {}) {
  std::vector<std::vector<model::Vec3d>> sets;
  sets.push_back(std::move(positions));
  return assemble_structure(std::move(name), label, std::move(topology), std::move(sets), std::move(metadata));
}

struct ResidueKey {
  std::int64_t number{};
  std::string insertion, chain, name;
  bool operator==(const ResidueKey &) const = default;
};

model::BondOrder order_from_number(long order) {
  switch (order) {
  case 1: return model::BondOrder::single;
  case 2: return model::BondOrder::double_bond;
  case 3: return model::BondOrder::triple;
  default: return model::BondOrder::unknown;
  }
}

int order_to_number(model::BondOrder order) {
  switch (order) {
  case model::BondOrder::double_bond: return 2;
  case model::BondOrder::triple: return 3;
  default: return 1;
  }
}

// =================================================================== MMOD

std::uint8_t mmod_element(long type, std::string_view name, std::string_view residue_name) {
  const char *symbol = nullptr;
  if (type <= 14) symbol = "C";
  else if (type <= 23) symbol = "O";
  else if (type <= 40) symbol = "N";
  else if (type <= 48) symbol = "H";
  else if (type <= 52) symbol = "S";
  else if (type <= 53) symbol = "P";
  else if (type <= 55) symbol = "B";
  else if (type <= 56) symbol = "F";
  else if (type <= 57) symbol = "Cl";
  else if (type <= 58) symbol = "Br";
  else if (type <= 59) symbol = "I";
  else if (type <= 60) symbol = "Si";
  if (symbol != nullptr && type > 0) return atomic_number(symbol).value_or(0U);
  return element_from_name(name, residue_name);  // dummy/lone-pair/unknown types
}

// MacroModel atom type chosen for writing (documented approximation).
int mmod_type(const model::AtomRecord &atom, const std::vector<std::pair<std::size_t, model::BondOrder>> &bonds,
              const std::vector<std::uint8_t> &neighbour_elements) {
  const auto count = static_cast<int>(bonds.size());
  int doubles = 0, triples = 0;
  for (const auto &[other, order] : bonds) {
    if (order == model::BondOrder::double_bond || order == model::BondOrder::aromatic) ++doubles;
    if (order == model::BondOrder::triple) ++triples;
  }
  const auto symbol = std::string{element_symbol(atom.atomic_number)};
  const auto charge = atom.formal_charge;
  if (symbol == "C") return triples > 0 || doubles >= 2 ? 1 : (doubles > 0 ? 2 : 3);
  if (symbol == "O") {
    if (count == 0 || (count == 2 && std::all_of(neighbour_elements.begin(), neighbour_elements.end(), [](auto e) { return e == 1U; })))
      return 19;  // water
    if (charge < 0) return 18;
    if (charge > 0) return doubles > 0 ? 20 : 21;
    if (doubles > 0) return 15;
    return count >= 2 ? 16 : 17;
  }
  if (symbol == "N") {
    if (charge > 0) {
      if (doubles > 0) return count >= 3 ? 31 : (count == 2 ? 36 : 37);
      return count >= 4 ? 32 : (count == 3 ? 33 : (count == 2 ? 34 : 35));
    }
    if (triples > 0) return 24;
    if (doubles > 0) return count >= 2 ? 25 : 30;
    return count >= 3 ? 26 : (count == 2 ? 27 : (count == 1 ? 28 : 29));
  }
  if (symbol == "H") {
    if (count == 0) return 44;
    if (!neighbour_elements.empty() && neighbour_elements.front() == 7U) return 42;
    if (!neighbour_elements.empty() && neighbour_elements.front() == 8U) return 43;
    return 41;
  }
  if (symbol == "S") return charge < 0 ? 51 : (count <= 1 ? 50 : 49);
  if (symbol == "P") return 53;
  if (symbol == "B") return count >= 4 ? 55 : 54;
  if (symbol == "F") return 56;
  if (symbol == "Cl") return 57;
  if (symbol == "Br") return 58;
  if (symbol == "I") return 59;
  if (symbol == "Si") return 60;
  return 64;
}

std::string clean(std::string_view value) {
  std::size_t b = 0, e = value.size();
  while (b < e && std::isspace(static_cast<unsigned char>(value[b])) != 0) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(value[e - 1U])) != 0) --e;
  return std::string{value.substr(b, e - b)};
}

// =================================================================== MAE

struct MaeToken {
  std::string text;
  bool quoted{};
  std::size_t line{};
};

struct MaeBlock {
  std::string name;
  bool indexed{};
  std::size_t declared_rows{};
  std::vector<std::string> names;
  std::vector<MaeToken> values;
  std::vector<MaeBlock> children;
  std::size_t line{};

  [[nodiscard]] std::optional<std::size_t> column_of(std::string_view key) const {
    for (std::size_t i = 0; i < names.size(); ++i)
      if (names[i] == key) return i;
    return std::nullopt;
  }
  [[nodiscard]] const MaeBlock *child(std::string_view key) const {
    for (const auto &c : children)
      if (c.name == key) return &c;
    return nullptr;
  }
};

struct MaeError {
  std::string message;
  std::size_t line{};
};

std::vector<MaeToken> mae_tokenize(std::string_view text) {
  std::vector<MaeToken> tokens;
  std::size_t i = 0, line = 1;
  while (i < text.size()) {
    const auto c = text[i];
    if (c == '\n') { ++line; ++i; continue; }
    if (std::isspace(static_cast<unsigned char>(c)) != 0) { ++i; continue; }
    if (c == '#') {
      while (i < text.size() && text[i] != '\n') ++i;
      continue;
    }
    if (c == '"') {
      MaeToken token;
      token.quoted = true;
      token.line = line;
      ++i;
      for (;;) {
        if (i >= text.size()) throw MaeError{"unterminated quoted string", token.line};
        if (text[i] == '\n') throw MaeError{"a quoted string runs over the end of a line", token.line};
        if (text[i] == '\\' && i + 1U < text.size()) {
          token.text.push_back(text[i + 1U]);
          i += 2U;
          continue;
        }
        if (text[i] == '"') { ++i; break; }
        token.text.push_back(text[i++]);
      }
      tokens.push_back(std::move(token));
      continue;
    }
    MaeToken token;
    token.line = line;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) == 0) token.text.push_back(text[i++]);
    tokens.push_back(std::move(token));
  }
  return tokens;
}

bool is_symbol(const MaeToken &token, std::string_view symbol) { return !token.quoted && token.text == symbol; }

class MaeParser {
public:
  explicit MaeParser(std::vector<MaeToken> tokens) : tokens_{std::move(tokens)} {}

  std::vector<MaeBlock> parse_all() {
    std::vector<MaeBlock> blocks;
    while (position_ < tokens_.size()) {
      MaeBlock block;
      block.line = tokens_[position_].line;
      if (is_symbol(tokens_[position_], "{")) {
        // anonymous header block
      } else {
        set_name(block, tokens_[position_].text);
        ++position_;
        if (position_ >= tokens_.size() || !is_symbol(tokens_[position_], "{"))
          throw MaeError{"expected '{' after block name '" + block.name + "'", block.line};
      }
      parse_body(block, 0U);
      blocks.push_back(std::move(block));
    }
    return blocks;
  }

private:
  std::vector<MaeToken> tokens_;
  std::size_t position_{};

  static void set_name(MaeBlock &block, const std::string &text) {
    const auto bracket = text.find('[');
    if (bracket != std::string::npos && text.back() == ']') {
      block.name = text.substr(0U, bracket);
      block.indexed = true;
      const auto number = to_number<long>(std::string_view{text}.substr(bracket + 1U, text.size() - bracket - 2U));
      if (!number || *number < 0) throw MaeError{"invalid row count in block name '" + text + "'", 0U};
      block.declared_rows = static_cast<std::size_t>(*number);
    } else {
      block.name = text;
    }
  }

  void parse_body(MaeBlock &block, std::size_t depth) {
    if (depth > 16U) throw MaeError{"Maestro blocks are nested too deeply", block.line};
    ++position_;  // '{'
    while (position_ < tokens_.size() && !is_symbol(tokens_[position_], ":::") && !is_symbol(tokens_[position_], "}")) {
      block.names.push_back(tokens_[position_].text);
      ++position_;
    }
    if (position_ >= tokens_.size()) throw MaeError{"block '" + block.name + "' is not terminated", block.line};
    if (is_symbol(tokens_[position_], "}")) {
      ++position_;
      return;
    }
    ++position_;  // ':::'
    if (block.indexed) {
      while (position_ < tokens_.size() && !is_symbol(tokens_[position_], ":::")) block.values.push_back(tokens_[position_++]);
      if (position_ >= tokens_.size()) throw MaeError{"indexed block '" + block.name + "' lacks its closing ':::'", block.line};
      ++position_;
      const auto expected = block.declared_rows * (block.names.size() + 1U);
      if (block.values.size() != expected)
        throw MaeError{"block '" + block.name + "' declares " + std::to_string(block.declared_rows) + " rows but holds " +
                           std::to_string(block.values.size()) + " values for " + std::to_string(block.names.size() + 1U) + " columns",
                       block.line};
      if (position_ >= tokens_.size() || !is_symbol(tokens_[position_], "}"))
        throw MaeError{"block '" + block.name + "' is not closed by '}'", block.line};
      ++position_;
      return;
    }
    for (std::size_t i = 0; i < block.names.size(); ++i) {
      if (position_ >= tokens_.size() || is_symbol(tokens_[position_], "}"))
        throw MaeError{"block '" + block.name + "' has fewer values than property names", block.line};
      block.values.push_back(tokens_[position_++]);
    }
    while (position_ < tokens_.size() && !is_symbol(tokens_[position_], "}")) {
      MaeBlock child;
      child.line = tokens_[position_].line;
      set_name(child, tokens_[position_].text);
      ++position_;
      if (position_ >= tokens_.size() || !is_symbol(tokens_[position_], "{"))
        throw MaeError{"expected '{' after block name '" + child.name + "'", child.line};
      parse_body(child, depth + 1U);
      block.children.push_back(std::move(child));
    }
    if (position_ >= tokens_.size()) throw MaeError{"block '" + block.name + "' is not terminated", block.line};
    ++position_;
  }
};

struct MaeAtomColumns {
  std::optional<std::size_t> x, y, z, atomic_number, name, residue_name, residue_number, chain, insertion, charge, formal_charge, occupancy,
      tfactor, segment;
};

bool mae_missing(const MaeToken &token) { return !token.quoted && token.text == "<>"; }

Result<StructureData> build_mae_ct(const MaeBlock &ct, const std::string &source_name, std::size_t index) {
  using R = Result<StructureData>;
  const auto *atoms = ct.child("m_atom");
  if (atoms == nullptr || atoms->declared_rows == 0U)
    return R::failure(parse_error(source_name, ct.line, "Maestro structure block has no m_atom rows"));
  MaeAtomColumns cols;
  const auto look = [&](std::string_view key) { return atoms->column_of(key); };
  cols.x = look("r_m_x_coord");
  cols.y = look("r_m_y_coord");
  cols.z = look("r_m_z_coord");
  if (!cols.x || !cols.y || !cols.z)
    return R::failure(parse_error(source_name, atoms->line, "m_atom lacks r_m_x_coord/r_m_y_coord/r_m_z_coord"));
  cols.atomic_number = look("i_m_atomic_number");
  cols.name = look("s_m_pdb_atom_name");
  if (!cols.name) cols.name = look("s_m_atom_name");
  cols.residue_name = look("s_m_pdb_residue_name");
  cols.residue_number = look("i_m_residue_number");
  cols.chain = look("s_m_chain_name");
  cols.insertion = look("s_m_insertion_code");
  cols.charge = look("r_m_charge1");
  cols.formal_charge = look("i_m_formal_charge");
  cols.occupancy = look("r_m_pdb_occupancy");
  cols.tfactor = look("r_m_pdb_tfactor");
  cols.segment = look("s_m_pdb_segment_name");
  const auto width = atoms->names.size() + 1U;
  const auto cell = [&](std::size_t row, std::size_t column_index) -> const MaeToken & { return atoms->values[row * width + 1U + column_index]; };

  model::TopologyBuilder builder;
  std::vector<model::Vec3d> positions;
  std::vector<double> charges, occupancy, tfactor;
  std::vector<std::uint8_t> occupancy_present, tfactor_present;
  bool any_charge = false;
  std::map<long, std::size_t> by_index;
  std::optional<ResidueKey> current_key;
  std::optional<model::ResidueIndex> current_residue;
  for (std::size_t row = 0; row < atoms->declared_rows; ++row) {
    const auto &id_token = atoms->values[row * width];
    const auto id = to_number<long>(id_token.text);
    if (!id) return R::failure(parse_error(source_name, id_token.line, "m_atom row has a non-integer index"));
    if (!by_index.emplace(*id, row).second) return R::failure(parse_error(source_name, id_token.line, "m_atom row index is duplicated"));
    double xyz[3]{};
    const std::size_t axis_columns[3] = {*cols.x, *cols.y, *cols.z};
    for (int axis = 0; axis < 3; ++axis) {
      const auto &token = cell(row, axis_columns[axis]);
      const auto value = mae_missing(token) ? std::nullopt : to_number<double>(token.text);
      if (!value) return R::failure(parse_error(source_name, token.line, "m_atom row has an invalid coordinate"));
      xyz[axis] = *value;
    }
    positions.push_back({xyz[0], xyz[1], xyz[2]});
    const auto text_of = [&](const std::optional<std::size_t> &c) {
      if (!c || mae_missing(cell(row, *c))) return std::string{};
      return clean(cell(row, *c).text);
    };
    const auto name = text_of(cols.name);
    const auto residue_name = text_of(cols.residue_name);
    long atomic = -2;
    if (cols.atomic_number && !mae_missing(cell(row, *cols.atomic_number))) {
      const auto parsed = to_number<long>(cell(row, *cols.atomic_number).text);
      if (!parsed) return R::failure(parse_error(source_name, cell(row, *cols.atomic_number).line, "m_atom row has an invalid atomic number"));
      atomic = *parsed;
    }
    std::uint8_t element = 0U;
    if (atomic > 0 && atomic < 119) element = static_cast<std::uint8_t>(atomic);
    else if (atomic == -2 && !cols.atomic_number) element = element_from_name(name, residue_name);
    ResidueKey key;
    if (cols.residue_number && !mae_missing(cell(row, *cols.residue_number))) key.number = to_number<long>(cell(row, *cols.residue_number).text).value_or(0);
    key.insertion = text_of(cols.insertion);
    key.chain = text_of(cols.chain);
    key.name = residue_name.empty() ? std::string{"UNK"} : residue_name;
    if (!current_key || !(*current_key == key)) {
      const auto residue = builder.add_residue({key.name, key.number, key.insertion, key.chain, text_of(cols.segment)});
      if (!residue.has_value()) return R::failure(residue.error());
      current_residue = residue.value();
      current_key = key;
    }
    int formal = 0;
    if (cols.formal_charge && !mae_missing(cell(row, *cols.formal_charge))) formal = static_cast<int>(to_number<long>(cell(row, *cols.formal_charge).text).value_or(0));
    const auto atom_name = name.empty() ? std::string{element == 0U ? "X" : element_symbol(element)} : name;
    const auto added = builder.add_atom({atom_name, element, *current_residue, "", formal, static_cast<std::int64_t>(*id)});
    if (!added.has_value()) return R::failure(added.error());
    double charge = 0.0;
    if (cols.charge && !mae_missing(cell(row, *cols.charge))) {
      charge = to_number<double>(cell(row, *cols.charge).text).value_or(0.0);
      any_charge = true;
    }
    charges.push_back(charge);
    const auto optional_real = [&](const std::optional<std::size_t> &c, std::vector<double> &values, std::vector<std::uint8_t> &present) {
      if (c && !mae_missing(cell(row, *c))) {
        const auto parsed = to_number<double>(cell(row, *c).text);
        values.push_back(parsed.value_or(0.0));
        present.push_back(parsed.has_value() ? 1U : 0U);
      } else {
        values.push_back(0.0);
        present.push_back(0U);
      }
    };
    optional_real(cols.occupancy, occupancy, occupancy_present);
    optional_real(cols.tfactor, tfactor, tfactor_present);
  }

  if (const auto *bonds = ct.child("m_bond"); bonds != nullptr && bonds->declared_rows > 0U) {
    const auto from = bonds->column_of("i_m_from"), to = bonds->column_of("i_m_to"), order = bonds->column_of("i_m_order");
    if (!from || !to) return R::failure(parse_error(source_name, bonds->line, "m_bond lacks i_m_from/i_m_to"));
    const auto bond_width = bonds->names.size() + 1U;
    std::set<std::pair<std::size_t, std::size_t>> seen;
    for (std::size_t row = 0; row < bonds->declared_rows; ++row) {
      const auto &first_token = bonds->values[row * bond_width + 1U + *from];
      const auto &second_token = bonds->values[row * bond_width + 1U + *to];
      const auto first = to_number<long>(first_token.text), second = to_number<long>(second_token.text);
      if (!first || !second) return R::failure(parse_error(source_name, first_token.line, "m_bond row has an invalid atom index"));
      const auto a = by_index.find(*first), b = by_index.find(*second);
      if (a == by_index.end() || b == by_index.end())
        return R::failure(parse_error(source_name, first_token.line, "m_bond row references an atom that is not in m_atom"));
      long order_value = 1;
      if (order) order_value = to_number<long>(bonds->values[row * bond_width + 1U + *order].text).value_or(1);
      if (order_value == 0) continue;  // zero-order bond (e.g. metal coordination) is not a covalent bond
      const auto lo = std::min(a->second, b->second), hi = std::max(a->second, b->second);
      if (lo == hi || !seen.insert({lo, hi}).second) continue;
      const auto error = builder.add_bond({model::AtomIndex{lo}, model::AtomIndex{hi}, order_from_number(order_value), model::BondQuery::none,
                                           model::BondStereo::none, model::ChemicalAnnotationOrigin::explicit_input});
      if (error.has_value()) return R::failure(*error);
    }
  }
  if (any_charge) {
    if (const auto error = builder.add_property("partial_charge", std::move(charges), model::PropertyMetadata{"elementary_charge", "r_m_charge1", {}});
        error.has_value())
      return R::failure(*error);
  }
  builder.set_source_metadata("format", "mae");
  const auto topology = builder.build();
  if (!topology.has_value()) return R::failure(topology.error());

  model::FrameMetadata metadata;
  if (std::any_of(occupancy_present.begin(), occupancy_present.end(), [](auto v) { return v != 0U; })) {
    metadata.atom_properties.emplace("occupancy", model::AtomProperty{std::move(occupancy), model::PropertyMetadata{std::nullopt, "r_m_pdb_occupancy", {}}});
    metadata.atom_properties.emplace("occupancy_present", model::AtomProperty{model::BooleanColumn{std::move(occupancy_present)},
                                                                              model::PropertyMetadata{std::nullopt, "r_m_pdb_occupancy", {}}});
  }
  if (std::any_of(tfactor_present.begin(), tfactor_present.end(), [](auto v) { return v != 0U; })) {
    metadata.atom_properties.emplace("b_iso_or_equiv", model::AtomProperty{std::move(tfactor), model::PropertyMetadata{"angstrom^2", "r_m_pdb_tfactor", {}}});
    metadata.atom_properties.emplace("b_iso_or_equiv_present", model::AtomProperty{model::BooleanColumn{std::move(tfactor_present)},
                                                                                   model::PropertyMetadata{std::nullopt, "r_m_pdb_tfactor", {}}});
  }
  std::string title;
  if (const auto c = ct.column_of("s_m_title"); c && !mae_missing(ct.values[*c])) title = clean(ct.values[*c].text);
  const auto fallback = stem_name(source_name, "mae_structure") + (index == 0U ? std::string{} : "_" + std::to_string(index + 1U));
  return assemble_structure(title.empty() ? fallback : title, "mae", topology.value(), std::move(positions), std::move(metadata));
}

std::string mae_quote(std::string_view value) {
  std::string out = "\"";
  for (const auto c : value) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

std::string pad_right(std::string_view value, std::size_t width) {
  std::string out{value.substr(0U, width)};
  out.resize(width, ' ');
  return out;
}

std::vector<double> optional_doubles(const model::Topology &topology, std::string_view name) {
  const auto *property = topology.properties().find(name);
  if (property == nullptr) return {};
  if (const auto *d = std::get_if<std::vector<double>>(&property->values)) return *d;
  if (const auto *f = std::get_if<std::vector<float>>(&property->values)) return {f->begin(), f->end()};
  return {};
}

std::optional<std::vector<double>> frame_doubles(const model::CoordinateFrame &frame, std::string_view name, std::string_view present_name) {
  const auto found = frame.metadata().atom_properties.find(std::string{name});
  if (found == frame.metadata().atom_properties.end()) return std::nullopt;
  const auto *values = std::get_if<std::vector<double>>(&found->second.values);
  if (values == nullptr) return std::nullopt;
  std::vector<double> out = *values;
  const auto present = frame.metadata().atom_properties.find(std::string{present_name});
  if (present != frame.metadata().atom_properties.end())
    if (const auto *flags = std::get_if<model::BooleanColumn>(&present->second.values))
      for (std::size_t i = 0; i < out.size() && i < flags->values.size(); ++i)
        if (flags->values[i] == 0U) out[i] = 0.0;
  return out;
}

Result<std::vector<std::size_t>> writer_frames(const model::CoordinateSource &coordinates, const StructureWriteOptions &options,
                                               std::string_view label) {
  using R = Result<std::vector<std::size_t>>;
  const auto count = coordinates.frame_count();
  if (!count.has_value() || *count == 0U)
    return R::failure(operation::Error{operation::ErrorCode::invalid_argument, std::string{label} + " export requires a known non-zero frame count", {}});
  std::vector<std::size_t> frames = options.frame_indices;
  if (frames.empty()) {
    frames.resize(*count);
    std::iota(frames.begin(), frames.end(), std::size_t{});
  }
  for (const auto frame : frames)
    if (frame >= *count)
      return R::failure(operation::Error{operation::ErrorCode::invalid_argument, std::string{label} + " export frame index is out of range", {}});
  return R::success(std::move(frames));
}

model::Vec3d position_at(const std::variant<std::vector<model::Vec3f>, std::vector<model::Vec3d>> &values, std::size_t index) {
  return std::visit(
      [index](const auto &items) {
        return model::Vec3d{static_cast<double>(items[index].x), static_cast<double>(items[index].y), static_cast<double>(items[index].z)};
      },
      values);
}

struct AtomNeighbourhood {
  std::vector<std::vector<std::pair<std::size_t, model::BondOrder>>> bonds;
};

AtomNeighbourhood neighbourhood_of(const model::Topology &topology) {
  AtomNeighbourhood result;
  result.bonds.resize(topology.atom_count());
  for (const auto &bond : topology.bonds()) {
    result.bonds[bond.first.value].push_back({bond.second.value, bond.order});
    result.bonds[bond.second.value].push_back({bond.first.value, bond.order});
  }
  return result;
}

std::string format_real(double value, int decimals) {
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
  return buffer;
}

} // namespace

// =================================================================== MMOD reader

Result<StructureDocument> read_mmod(std::string_view content, std::string source_name) {
  using R = Result<StructureDocument>;
  const auto lines = split_lines(content);
  StructureDocument document;
  document.format = StructureFormat::mmod;
  document.source_name = source_name;
  std::size_t line_index = 0;
  struct Block {
    std::shared_ptr<const model::Topology> topology;
    std::vector<model::Vec3d> positions;
    std::string title;
  };
  std::vector<Block> blocks;
  while (line_index < lines.size()) {
    if (clean(lines[line_index]).empty()) { ++line_index; continue; }
    const auto header = lines[line_index];
    const auto declared = to_number<long>(column(header, 0U, 6U));
    if (!declared || *declared <= 0)
      return R::failure(parse_error(source_name, line_index + 1U, "MacroModel header does not start with a positive atom count"));
    const auto count = static_cast<std::size_t>(*declared);
    if (line_index + 1U + count > lines.size())
      return R::failure(parse_error(source_name, line_index + 1U, "MacroModel structure ends before its " + std::to_string(count) + " atom records"));
    const auto title = clean(column(header, 6U, std::string_view::npos));
    model::TopologyBuilder builder;
    std::vector<model::Vec3d> positions;
    std::vector<double> charges;
    std::optional<ResidueKey> current_key;
    std::optional<model::ResidueIndex> current_residue;
    struct PendingBond { std::size_t first, second; long order; };
    std::vector<PendingBond> pending;
    for (std::size_t atom = 0; atom < count; ++atom) {
      const auto line = lines[line_index + 1U + atom];
      const auto line_number = line_index + 2U + atom;
      if (line.size() < 88U) return R::failure(parse_error(source_name, line_number, "a MacroModel atom record is shorter than 88 columns"));
      const auto type = to_number<long>(column(line, 0U, 4U));
      if (!type) return R::failure(parse_error(source_name, line_number, "a MacroModel atom record has an invalid atom type"));
      for (int slot = 0; slot < 6; ++slot) {
        const auto field = column(line, 4U + 8U * static_cast<std::size_t>(slot), 8U);
        std::istringstream stream{std::string{field}};
        long partner = 0, order = 0;
        if (!(stream >> partner >> order)) return R::failure(parse_error(source_name, line_number, "a MacroModel atom record has an invalid bond field"));
        if (partner > 0 && order > 0 && static_cast<long>(atom) < partner - 1) {
          if (static_cast<std::size_t>(partner) > count)
            return R::failure(parse_error(source_name, line_number, "a MacroModel bond references an atom beyond the structure"));
          pending.push_back({atom, static_cast<std::size_t>(partner - 1), order});
        }
      }
      double xyz[3]{};
      for (int axis = 0; axis < 3; ++axis) {
        const auto value = to_number<double>(column(line, 52U + 12U * static_cast<std::size_t>(axis), 12U));
        if (!value) return R::failure(parse_error(source_name, line_number, "a MacroModel atom record has an invalid coordinate"));
        xyz[axis] = *value;
      }
      positions.push_back({xyz[0], xyz[1], xyz[2]});
      ResidueKey key;
      key.number = to_number<long>(column(line, 89U, 5U)).value_or(0);
      key.chain = clean(column(line, 95U, 1U));
      key.name = clean(column(line, 119U, 4U));
      if (key.name.empty()) key.name = "UNK";
      auto name = clean(column(line, 124U, 4U));
      const auto element = mmod_element(*type, name, key.name);
      if (name.empty()) {
        name = std::string{element == 0U ? "X" : element_symbol(element)};
        char suffix[16];
        std::snprintf(suffix, sizeof suffix, "%02zu", atom + 1U);
        name += suffix;
      }
      if (!current_key || !(*current_key == key)) {
        const auto residue = builder.add_residue({key.name, key.number, "", key.chain, ""});
        if (!residue.has_value()) return R::failure(residue.error());
        current_residue = residue.value();
        current_key = key;
      }
      const auto added = builder.add_atom({name, element, *current_residue, "", 0, static_cast<std::int64_t>(atom + 1U)});
      if (!added.has_value()) return R::failure(added.error());
      charges.push_back(to_number<double>(column(line, 100U, 9U)).value_or(0.0));
    }
    for (const auto &bond : pending) {
      const auto error = builder.add_bond({model::AtomIndex{bond.first}, model::AtomIndex{bond.second}, order_from_number(bond.order),
                                           model::BondQuery::none, model::BondStereo::none, model::ChemicalAnnotationOrigin::explicit_input});
      if (error.has_value()) return R::failure(*error);
    }
    if (const auto error = builder.add_property("partial_charge", std::move(charges), model::PropertyMetadata{"elementary_charge", "MacroModel charge", {}});
        error.has_value())
      return R::failure(*error);
    builder.set_source_metadata("format", "mmod");
    const auto topology = builder.build();
    if (!topology.has_value()) return R::failure(topology.error());
    blocks.push_back({topology.value(), std::move(positions), title});
    line_index += 1U + count;
  }
  if (blocks.empty()) return R::failure(parse_error(source_name, 1U, "MacroModel input is empty"));
  // Consecutive blocks with the same atoms are states of one structure (as PyMOL loads them); others start a new structure.
  const auto same_atoms = [](const model::Topology &a, const model::Topology &b) {
    if (a.atom_count() != b.atom_count()) return false;
    for (std::size_t i = 0; i < a.atom_count(); ++i)
      if (a.atoms()[i].atomic_number != b.atoms()[i].atomic_number || a.atoms()[i].name != b.atoms()[i].name) return false;
    return true;
  };
  for (std::size_t first = 0; first < blocks.size();) {
    std::size_t last = first + 1U;
    while (last < blocks.size() && same_atoms(*blocks[first].topology, *blocks[last].topology)) ++last;
    std::vector<std::vector<model::Vec3d>> sets;
    for (std::size_t i = first; i < last; ++i) sets.push_back(std::move(blocks[i].positions));
    const auto fallback = stem_name(source_name, "mmod_structure") + (document.structures.empty() ? std::string{} : "_" + std::to_string(document.structures.size() + 1U));
    auto structure = assemble_structure(blocks[first].title.empty() ? fallback : blocks[first].title, "mmod", blocks[first].topology, std::move(sets));
    if (!structure.has_value()) return R::failure(structure.error());
    document.structures.push_back(std::move(structure.value()));
    first = last;
  }
  return R::success(std::move(document));
}

// =================================================================== MMOD writer

Result<StructureWriteReport> write_mmod(std::ostream &output, const model::Topology &topology, const model::CoordinateSource &coordinates,
                                        const StructureWriteOptions &options, operation::TaskContext &context) {
  using R = Result<StructureWriteReport>;
  if (topology.atom_count() == 0U)
    return R::failure(operation::Error{operation::ErrorCode::invalid_argument, "MacroModel export needs at least one atom", {}});
  if (coordinates.atom_count() != topology.atom_count())
    return R::failure(operation::Error{operation::ErrorCode::invalid_argument, "MacroModel export requires a matching topology/coordinate source", {}});
  const auto frames = writer_frames(coordinates, options, "MacroModel");
  if (!frames.has_value()) return R::failure(frames.error());
  const auto neighbourhood = neighbourhood_of(topology);
  const auto charges = optional_doubles(topology, "partial_charge");
  const auto &atoms = topology.atoms();
  const auto &residues = topology.residues();
  std::uint64_t too_many_bonds = 0, untyped = 0, lossy_orders = 0, missing_atoms = 0;
  for (const auto &bond : topology.bonds())
    if (bond.order != model::BondOrder::single && bond.order != model::BondOrder::double_bond && bond.order != model::BondOrder::triple) ++lossy_orders;
  bool cell_seen = false;
  for (const auto frame_index : frames.value()) {
    if (context.cancellation.is_cancelled())
      return R::failure(operation::Error{operation::ErrorCode::cancelled, "MacroModel export cancelled", {}});
    const auto frame = coordinates.read_frame(frame_index);
    if (!frame.has_value()) return R::failure(frame.error());
    const auto &frame_positions = frame.value()->positions().values();
    cell_seen = cell_seen || frame.value()->metadata().unit_cell.has_value();
    char line[512];
    std::snprintf(line, sizeof line, " %5zu  %-70s\n", topology.atom_count(), options.comment.empty() ? "MolShredder" : options.comment.substr(0U, 70U).c_str());
    output << line;
    for (std::size_t a = 0; a < atoms.size(); ++a) {
      std::vector<std::uint8_t> neighbour_elements;
      for (const auto &[other, order] : neighbourhood.bonds[a]) neighbour_elements.push_back(atoms[other].atomic_number);
      const auto type = mmod_type(atoms[a], neighbourhood.bonds[a], neighbour_elements);
      if (type == 64) ++untyped;
      std::string text;
      std::snprintf(line, sizeof line, " %3d", type);
      text += line;
      for (std::size_t slot = 0; slot < 6U; ++slot) {
        if (slot < neighbourhood.bonds[a].size()) {
          std::snprintf(line, sizeof line, " %5zu %1d", neighbourhood.bonds[a][slot].first + 1U, order_to_number(neighbourhood.bonds[a][slot].second));
        } else {
          std::snprintf(line, sizeof line, " %5d %1d", 0, 0);
        }
        text += line;
      }
      if (neighbourhood.bonds[a].size() > 6U) ++too_many_bonds;
      const auto present = frame.value()->atom_present(a);
      if (!present) ++missing_atoms;
      const auto p = position_at(frame_positions, a);
      std::snprintf(line, sizeof line, " %11.6f %11.6f %11.6f ", present ? p.x : 0.0, present ? p.y : 0.0, present ? p.z : 0.0);
      text += line;
      const auto &residue = residues[atoms[a].residue.value];
      const double charge = a < charges.size() ? charges[a] : 0.0;
      std::snprintf(line, sizeof line, "%5lld%1s%1s%4d%9.5f%9.5f %-4s %4s\n", static_cast<long long>(residue.sequence_number), "", residue.chain_id.substr(0U, 1U).c_str(), 0,
                    charge, charge, residue.name.substr(0U, 4U).c_str(), atoms[a].name.substr(0U, 4U).c_str());
      text += line;
      output << text;
    }
  }
  const auto position = output.tellp();
  if (position < std::streampos{})
    return R::failure(operation::Error{operation::ErrorCode::internal, "could not determine MacroModel output size", {}});
  std::vector<FormatLoss> losses;
  if (untyped > 0U)
    losses.push_back({"atom_types", untyped, "elements without a MacroModel atom type are written as type 64 (Du); MolShredder chooses the other types from element, bonds and charge"});
  if (too_many_bonds > 0U) losses.push_back({"bonds", too_many_bonds, "MacroModel records hold at most six bonds per atom; extra bonds are dropped"});
  if (lossy_orders > 0U) losses.push_back({"bond_orders", lossy_orders * frames.value().size(), "aromatic and unknown bond orders are written as single bonds"});
  if (cell_seen) losses.push_back({"unit_cell", 1U, "MacroModel records have no periodic cell"});
  if (missing_atoms > 0U) losses.push_back({"presence", missing_atoms, "absent atoms are written at the origin"});
  losses.push_back({"atom_properties", 1U, "formal charges, alternate locations, occupancy, B-factors and insertion codes are not stored"});
  return R::success(StructureWriteReport{StructureFormat::mmod, topology.atom_count(), frames.value().size(), static_cast<std::uint64_t>(position), std::move(losses)});
}

// =================================================================== MAE reader

Result<StructureDocument> read_mae(std::string_view content, std::string source_name) {
  using R = Result<StructureDocument>;
  std::vector<MaeBlock> blocks;
  try {
    MaeParser parser{mae_tokenize(content)};
    blocks = parser.parse_all();
  } catch (const MaeError &error) {
    return R::failure(parse_error(source_name, error.line, "invalid Maestro file: " + error.message, "provide an uncompressed .mae file"));
  }
  StructureDocument document;
  document.format = StructureFormat::mae;
  document.source_name = source_name;
  for (const auto &block : blocks) {
    if (block.name != "f_m_ct") continue;
    auto structure = build_mae_ct(block, source_name, document.structures.size());
    if (!structure.has_value()) return R::failure(structure.error());
    document.structures.push_back(std::move(structure.value()));
  }
  if (document.structures.empty()) return R::failure(parse_error(source_name, 1U, "Maestro file contains no f_m_ct structure block"));
  return R::success(std::move(document));
}

// =================================================================== MAE writer

Result<StructureWriteReport> write_mae(std::ostream &output, const model::Topology &topology, const model::CoordinateSource &coordinates,
                                       const StructureWriteOptions &options, operation::TaskContext &context) {
  using R = Result<StructureWriteReport>;
  if (topology.atom_count() == 0U)
    return R::failure(operation::Error{operation::ErrorCode::invalid_argument, "Maestro export needs at least one atom", {}});
  if (coordinates.atom_count() != topology.atom_count())
    return R::failure(operation::Error{operation::ErrorCode::invalid_argument, "Maestro export requires a matching topology/coordinate source", {}});
  const auto frames = writer_frames(coordinates, options, "Maestro");
  if (!frames.has_value()) return R::failure(frames.error());
  const auto charges = optional_doubles(topology, "partial_charge");
  const auto &atoms = topology.atoms();
  const auto &residues = topology.residues();
  const auto precision = static_cast<int>(std::min(options.decimal_places, 10U));
  std::uint64_t lossy_orders = 0, missing_atoms = 0;
  bool cell_seen = false;
  for (const auto &bond : topology.bonds())
    if (bond.order != model::BondOrder::single && bond.order != model::BondOrder::double_bond && bond.order != model::BondOrder::triple) ++lossy_orders;
  output << "{\n s_m_m2io_version\n :::\n 2.0.0\n}\n\n";
  std::size_t structure_number = 0;
  for (const auto frame_index : frames.value()) {
    if (context.cancellation.is_cancelled())
      return R::failure(operation::Error{operation::ErrorCode::cancelled, "Maestro export cancelled", {}});
    const auto frame = coordinates.read_frame(frame_index);
    if (!frame.has_value()) return R::failure(frame.error());
    const auto &frame_positions = frame.value()->positions().values();
    cell_seen = cell_seen || frame.value()->metadata().unit_cell.has_value();
    const auto occupancy = frame_doubles(*frame.value(), "occupancy", "occupancy_present");
    const auto tfactor = frame_doubles(*frame.value(), "b_iso_or_equiv", "b_iso_or_equiv_present");
    ++structure_number;
    output << "f_m_ct {\n s_m_title\n s_m_entry_id\n :::\n " << mae_quote(options.comment.empty() ? "MolShredder" : options.comment) << "\n "
           << structure_number << "\n m_atom[" << atoms.size() << "] {\n  # First column is atom index #\n"
           << "  r_m_x_coord\n  r_m_y_coord\n  r_m_z_coord\n  i_m_residue_number\n  s_m_insertion_code\n  s_m_chain_name\n  s_m_pdb_residue_name\n"
           << "  s_m_pdb_atom_name\n  i_m_atomic_number\n  i_m_formal_charge\n  r_m_charge1\n";
    if (occupancy) output << "  r_m_pdb_occupancy\n";
    if (tfactor) output << "  r_m_pdb_tfactor\n";
    output << "  :::\n";
    for (std::size_t a = 0; a < atoms.size(); ++a) {
      const auto present = frame.value()->atom_present(a);
      if (!present) ++missing_atoms;
      const auto p = position_at(frame_positions, a);
      const auto &residue = residues[atoms[a].residue.value];
      output << "  " << (a + 1U) << ' ' << format_real(present ? p.x : 0.0, precision) << ' ' << format_real(present ? p.y : 0.0, precision) << ' '
             << format_real(present ? p.z : 0.0, precision) << ' ' << residue.sequence_number << ' '
             << mae_quote(residue.insertion_code.empty() ? std::string{" "} : residue.insertion_code) << ' '
             << mae_quote(residue.chain_id.empty() ? std::string{" "} : residue.chain_id) << ' ' << mae_quote(pad_right(residue.name, 4U)) << ' '
             << mae_quote(pad_right(atoms[a].name, 4U)) << ' ' << static_cast<int>(atoms[a].atomic_number == 0U ? 0 : atoms[a].atomic_number) << ' '
             << atoms[a].formal_charge << ' ' << format_real(a < charges.size() ? charges[a] : 0.0, 6);
      if (occupancy) output << ' ' << format_real((*occupancy)[a], 3);
      if (tfactor) output << ' ' << format_real((*tfactor)[a], 3);
      output << '\n';
    }
    output << "  :::\n }\n";
    if (!topology.bonds().empty()) {
      output << " m_bond[" << topology.bonds().size() << "] {\n  # First column is bond index #\n  i_m_from\n  i_m_to\n  i_m_order\n  :::\n";
      std::size_t index = 0;
      for (const auto &bond : topology.bonds())
        output << "  " << ++index << ' ' << (bond.first.value + 1U) << ' ' << (bond.second.value + 1U) << ' ' << order_to_number(bond.order) << '\n';
      output << "  :::\n }\n";
    }
    output << "}\n\n";
  }
  const auto position = output.tellp();
  if (position < std::streampos{})
    return R::failure(operation::Error{operation::ErrorCode::internal, "could not determine Maestro output size", {}});
  std::vector<FormatLoss> losses;
  if (lossy_orders > 0U) losses.push_back({"bond_orders", lossy_orders * frames.value().size(), "aromatic and unknown bond orders are written as single bonds"});
  if (cell_seen) losses.push_back({"unit_cell", 1U, "periodic cell data is not written to the Maestro structure block"});
  if (missing_atoms > 0U) losses.push_back({"presence", missing_atoms, "absent atoms are written at the origin"});
  losses.push_back({"atom_properties", 1U, "alternate locations, secondary structure and other static properties are not written"});
  return R::success(StructureWriteReport{StructureFormat::mae, topology.atom_count(), frames.value().size(), static_cast<std::uint64_t>(position), std::move(losses)});
}

} // namespace molshredder::io::detail
