#include "structure_reader_internal.hpp"

#include <algorithm>
#include <cctype>
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

using Token = CifToken;

std::string lowercase(std::string_view value) {
  std::string result{value};
  std::transform(result.begin(), result.end(), result.begin(), [](char letter) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
  });
  return result;
}

bool is_space(char value) {
  return std::isspace(static_cast<unsigned char>(value)) != 0;
}

bool is_control(const Token &token) {
  if (token.quoted) {
    return false;
  }
  const auto value = lowercase(token.text);
  return !value.empty() &&
         (value.front() == '_' || value == "loop_" || value == "stop_" ||
          value == "global_" || value.starts_with("data_") ||
          value.starts_with("save_"));
}

Result<std::vector<Token>> tokenize(std::string_view content,
                                    std::string_view source) {
  std::vector<Token> tokens;
  std::size_t position = 0;
  std::size_t line = 1;
  std::size_t column = 1;

  auto advance = [&](char value) {
    ++position;
    if (value == '\n') {
      ++line;
      column = 1;
    } else {
      ++column;
    }
  };

  while (position < content.size()) {
    const auto current = content[position];
    if (is_space(current)) {
      advance(current);
      continue;
    }
    if (current == '#') {
      while (position < content.size() && content[position] != '\n') {
        advance(content[position]);
      }
      continue;
    }
    const auto token_line = line;
    if (current == ';' && column == 1) {
      advance(current);
      std::string value;
      while (position < content.size() && content[position] != '\n') {
        if (content[position] != '\r')
          value.push_back(content[position]);
        advance(content[position]);
      }
      if (position < content.size()) {
        advance(content[position]);
      }
      bool closed = false;
      while (position < content.size()) {
        if (column == 1 && content[position] == ';') {
          advance(content[position]);
          while (position < content.size() && content[position] != '\n') {
            if (!is_space(content[position])) {
              return Result<std::vector<Token>>::failure(parse_error(
                  source, line,
                  "non-whitespace follows an mmCIF text-field terminator"));
            }
            advance(content[position]);
          }
          closed = true;
          break;
        }
        if (!value.empty()) {
          value.push_back('\n');
        }
        while (position < content.size() && content[position] != '\n') {
          if (content[position] != '\r')
            value.push_back(content[position]);
          advance(content[position]);
        }
        if (position < content.size()) {
          advance(content[position]);
        }
      }
      if (!closed) {
        return Result<std::vector<Token>>::failure(parse_error(
            source, token_line, "unterminated mmCIF semicolon text field"));
      }
      tokens.push_back(Token{std::move(value), token_line, true});
      continue;
    }
    if (current == '\'' || current == '"') {
      const auto quote = current;
      advance(current);
      std::string value;
      bool closed = false;
      while (position < content.size()) {
        const auto character = content[position];
        if (character == quote &&
            (position + 1 == content.size() ||
             is_space(content[position + 1]) || content[position + 1] == '#')) {
          advance(character);
          closed = true;
          break;
        }
        if (character == '\n' || character == '\r') {
          return Result<std::vector<Token>>::failure(
              parse_error(source, token_line,
                          "quoted mmCIF value crosses a line boundary"));
        }
        value.push_back(character);
        advance(character);
      }
      if (!closed) {
        return Result<std::vector<Token>>::failure(
            parse_error(source, token_line, "unterminated quoted mmCIF value"));
      }
      tokens.push_back(Token{std::move(value), token_line, true});
      continue;
    }

    std::string value;
    while (position < content.size() && !is_space(content[position])) {
      value.push_back(content[position]);
      advance(content[position]);
    }
    tokens.push_back(Token{std::move(value), token_line, false});
  }
  return Result<std::vector<Token>>::success(std::move(tokens));
}

Result<std::vector<CifBlock>> parse_blocks(const std::vector<Token> &tokens,
                                           std::string_view source) {
  std::vector<CifBlock> blocks;
  CifBlock *block = nullptr;
  std::size_t index = 0;
  while (index < tokens.size()) {
    const auto keyword = lowercase(tokens[index].text);
    if (!tokens[index].quoted && keyword.starts_with("data_")) {
      const auto name = tokens[index].text.substr(5);
      if (name.empty()) {
        return Result<std::vector<CifBlock>>::failure(parse_error(
            source, tokens[index].line, "mmCIF data block has no name"));
      }
      blocks.push_back(CifBlock{name, tokens[index].line, {}, {}});
      block = &blocks.back();
      ++index;
      continue;
    }
    if (block == nullptr) {
      return Result<std::vector<CifBlock>>::failure(
          parse_error(source, tokens[index].line,
                      "mmCIF content appears before the first data_ block"));
    }
    if (!tokens[index].quoted && keyword == "loop_") {
      CifLoop loop;
      loop.line = tokens[index].line;
      ++index;
      while (index < tokens.size() && !tokens[index].quoted &&
             !tokens[index].text.empty() && tokens[index].text.front() == '_') {
        loop.columns.push_back(lowercase(tokens[index].text));
        ++index;
      }
      if (loop.columns.empty()) {
        return Result<std::vector<CifBlock>>::failure(parse_error(
            source, loop.line, "mmCIF loop_ has no data-item columns"));
      }
      const auto category_end = loop.columns.front().find('.');
      const auto category = loop.columns.front().substr(0, category_end);
      if (category_end == std::string::npos ||
          std::any_of(loop.columns.begin(), loop.columns.end(),
                      [&category](const std::string &column) {
                        return !column.starts_with(category + ".");
                      })) {
        return Result<std::vector<CifBlock>>::failure(parse_error(
            source, loop.line,
            "all columns in an mmCIF loop must belong to one category"));
      }
      while (index < tokens.size()) {
        if (is_control(tokens[index]) &&
            loop.values.size() % loop.columns.size() == 0) {
          break;
        }
        loop.values.push_back(tokens[index]);
        ++index;
      }
      if (loop.values.empty() ||
          loop.values.size() % loop.columns.size() != 0) {
        return Result<std::vector<CifBlock>>::failure(
            parse_error(source, loop.line,
                        "mmCIF loop value count is not a positive multiple of "
                        "its columns"));
      }
      block->loops.push_back(std::move(loop));
      continue;
    }
    if (!tokens[index].quoted && !tokens[index].text.empty() &&
        tokens[index].text.front() == '_') {
      const auto name = lowercase(tokens[index].text);
      const auto item_line = tokens[index].line;
      ++index;
      if (index >= tokens.size() || is_control(tokens[index])) {
        return Result<std::vector<CifBlock>>::failure(parse_error(
            source, item_line, "mmCIF data item has no value: " + name));
      }
      if (block->scalars.contains(name)) {
        return Result<std::vector<CifBlock>>::failure(parse_error(
            source, item_line, "duplicate mmCIF data item: " + name));
      }
      block->scalars.emplace(name, tokens[index]);
      ++index;
      continue;
    }
    if (!tokens[index].quoted && keyword == "stop_") {
      return Result<std::vector<CifBlock>>::failure(parse_error(
          source, tokens[index].line, "unexpected mmCIF stop_ outside a loop"));
    }
    if (!tokens[index].quoted &&
        (keyword == "global_" || keyword.starts_with("save_"))) {
      return Result<std::vector<CifBlock>>::failure(parse_error(
          source, tokens[index].line,
          "global/save-frame syntax is not valid in a PDBx/mmCIF data file"));
    }
    return Result<std::vector<CifBlock>>::failure(parse_error(
        source, tokens[index].line,
        "unexpected token in mmCIF data block: " + tokens[index].text));
  }
  if (blocks.empty()) {
    return Result<std::vector<CifBlock>>::failure(
        parse_error(source, 1, "mmCIF input contains no data_ block"));
  }
  return Result<std::vector<CifBlock>>::success(std::move(blocks));
}

bool is_missing(std::string_view value) { return value == "." || value == "?"; }

std::optional<std::string> present(const Token *token) {
  if (token == nullptr || is_missing(token->text)) {
    return std::nullopt;
  }
  return token->text;
}

const Token *scalar(const CifBlock &block, std::string_view name) {
  const auto found = block.scalars.find(name);
  return found == block.scalars.end() ? nullptr : &found->second;
}

template <typename Value>
Result<Value> parse_number(const Token &token, std::string_view source,
                           std::string_view field_name) {
  Value value{};
  const auto result = molshredder::core::from_chars(
      token.text.data(), token.text.data() + token.text.size(), value);
  if (result.ec != std::errc{} ||
      result.ptr != token.text.data() + token.text.size()) {
    return Result<Value>::failure(parse_error(source, token.line,
                                              "invalid numeric mmCIF field " +
                                                  std::string{field_name} +
                                                  ": " + token.text));
  }
  return Result<Value>::success(value);
}

struct RowView {
  const CifLoop &loop;
  std::size_t row{};
  const std::map<std::string, std::size_t, std::less<>> &columns;

  const Token *get(std::string_view name) const {
    const auto found = columns.find(name);
    if (found == columns.end()) {
      return nullptr;
    }
    return &loop.values[row * loop.columns.size() + found->second];
  }
};

const Token *choose(const RowView &row, std::string_view preferred,
                    std::string_view fallback) {
  const auto *first = row.get(preferred);
  return present(first).has_value() ? first : row.get(fallback);
}

struct AtomIdentity {
  std::string asym;
  std::string sequence;
  std::string component;
  std::string atom;
  std::string alternate_location;

  friend auto operator<=>(const AtomIdentity &, const AtomIdentity &) = default;
};

struct ParsedAtom {
  AtomIdentity identity;
  std::optional<AtomIdentity> author_identity;
  std::string source_id;
  std::string atom_name;
  std::string residue_name;
  std::string chain_id;
  std::string label_asym_id;
  std::string label_seq_id;
  std::string label_entity_id;
  std::int64_t residue_number{};
  std::string insertion_code;
  std::uint8_t atomic_number{};
  std::int32_t formal_charge{};
  bool formal_charge_present{};
  bool hetero{};
  Vec3d position;
  std::optional<double> occupancy;
  std::optional<double> b_factor;
  std::uint64_t model_number{1};
  std::size_t line{};
};

std::optional<AtomIdentity>
identity_from_row(const RowView &row, std::string_view category_prefix) {
  const auto value = [&](std::string_view suffix) {
    return present(row.get(std::string{category_prefix} + std::string{suffix}));
  };
  const auto label_asym = value("label_asym_id");
  const auto label_sequence = value("label_seq_id");
  const auto label_component = value("label_comp_id");
  const auto label_atom = value("label_atom_id");
  if (label_asym.has_value() && label_sequence.has_value() &&
      label_component.has_value() && label_atom.has_value()) {
    return AtomIdentity{*label_asym, *label_sequence, *label_component,
                        *label_atom, value("label_alt_id").value_or("")};
  }
  const auto auth_asym = value("auth_asym_id");
  const auto auth_sequence = value("auth_seq_id");
  const auto auth_component = value("auth_comp_id");
  const auto auth_atom = value("auth_atom_id");
  if (auth_asym.has_value() && auth_sequence.has_value() &&
      auth_component.has_value() && auth_atom.has_value()) {
    return AtomIdentity{*auth_asym, *auth_sequence, *auth_component, *auth_atom,
                        value("auth_alt_id").value_or("")};
  }
  return std::nullopt;
}

Result<ParsedAtom> parse_atom_row(const RowView &row, std::string_view source) {
  const auto *x_token = row.get("_atom_site.cartn_x");
  const auto *y_token = row.get("_atom_site.cartn_y");
  const auto *z_token = row.get("_atom_site.cartn_z");
  const auto *element_token = row.get("_atom_site.type_symbol");
  const auto *atom_token =
      choose(row, "_atom_site.auth_atom_id", "_atom_site.label_atom_id");
  const auto *residue_token =
      choose(row, "_atom_site.auth_comp_id", "_atom_site.label_comp_id");
  const auto *chain_token =
      choose(row, "_atom_site.auth_asym_id", "_atom_site.label_asym_id");
  const auto *sequence_token =
      choose(row, "_atom_site.auth_seq_id", "_atom_site.label_seq_id");
  const auto required = {x_token,       y_token,       z_token,
                         element_token, atom_token,    residue_token,
                         chain_token,   sequence_token};
  if (std::any_of(required.begin(), required.end(), [](const Token *token) {
        return !present(token).has_value();
      })) {
    const auto line = row.loop.values[row.row * row.loop.columns.size()].line;
    return Result<ParsedAtom>::failure(parse_error(
        source, line,
        "atom_site row lacks a required coordinate, element, atom, residue, "
        "chain, or sequence value"));
  }
  const auto x = parse_number<double>(*x_token, source, "_atom_site.Cartn_x");
  const auto y = parse_number<double>(*y_token, source, "_atom_site.Cartn_y");
  const auto z = parse_number<double>(*z_token, source, "_atom_site.Cartn_z");
  const auto residue_number =
      parse_number<std::int64_t>(*sequence_token, source, "residue sequence");
  if (!x.has_value()) {
    return Result<ParsedAtom>::failure(x.error());
  }
  if (!y.has_value()) {
    return Result<ParsedAtom>::failure(y.error());
  }
  if (!z.has_value()) {
    return Result<ParsedAtom>::failure(z.error());
  }
  if (!residue_number.has_value()) {
    return Result<ParsedAtom>::failure(residue_number.error());
  }
  const auto element = atomic_number(element_token->text);
  if (!element.has_value() || element.value() == 0U) {
    return Result<ParsedAtom>::failure(
        parse_error(source, element_token->line,
                    "unknown mmCIF element symbol: " + element_token->text));
  }

  ParsedAtom atom;
  atom.atom_name = atom_token->text;
  atom.residue_name = residue_token->text;
  atom.chain_id = chain_token->text;
  atom.residue_number = residue_number.value();
  atom.label_asym_id =
      present(row.get("_atom_site.label_asym_id")).value_or("");
  atom.label_seq_id = present(row.get("_atom_site.label_seq_id")).value_or("");
  atom.label_entity_id =
      present(row.get("_atom_site.label_entity_id")).value_or("");
  atom.insertion_code =
      present(row.get("_atom_site.pdbx_pdb_ins_code")).value_or("");
  const auto identity = identity_from_row(row, "_atom_site.");
  if (!identity.has_value()) {
    return Result<ParsedAtom>::failure(
        parse_error(source, x_token->line,
                    "atom_site row lacks a complete label or author identity"));
  }
  atom.identity = *identity;
  const auto auth_asym = present(row.get("_atom_site.auth_asym_id"));
  const auto auth_sequence = present(row.get("_atom_site.auth_seq_id"));
  const auto auth_component = present(row.get("_atom_site.auth_comp_id"));
  const auto auth_atom = present(row.get("_atom_site.auth_atom_id"));
  if (auth_asym.has_value() && auth_sequence.has_value() &&
      auth_component.has_value() && auth_atom.has_value()) {
    atom.author_identity =
        AtomIdentity{*auth_asym, *auth_sequence, *auth_component, *auth_atom,
                     present(row.get("_atom_site.label_alt_id")).value_or("")};
  }
  atom.source_id = present(row.get("_atom_site.id")).value_or("");
  atom.atomic_number = element.value();
  atom.position = Vec3d{x.value(), y.value(), z.value()};
  atom.hetero =
      lowercase(present(row.get("_atom_site.group_pdb")).value_or("ATOM")) ==
      "hetatm";
  atom.line = x_token->line;

  if (const auto charge = present(row.get("_atom_site.pdbx_formal_charge"));
      charge.has_value()) {
    Token charge_token{*charge, row.get("_atom_site.pdbx_formal_charge")->line,
                       false};
    const auto parsed = parse_number<std::int32_t>(
        charge_token, source, "_atom_site.pdbx_formal_charge");
    if (!parsed.has_value()) {
      return Result<ParsedAtom>::failure(parsed.error());
    }
    atom.formal_charge = parsed.value();
    atom.formal_charge_present = true;
  }
  if (const auto *token = row.get("_atom_site.occupancy");
      present(token).has_value()) {
    const auto parsed =
        parse_number<double>(*token, source, "_atom_site.occupancy");
    if (!parsed.has_value()) {
      return Result<ParsedAtom>::failure(parsed.error());
    }
    atom.occupancy = parsed.value();
  }
  if (const auto *token = row.get("_atom_site.b_iso_or_equiv");
      present(token).has_value()) {
    const auto parsed =
        parse_number<double>(*token, source, "_atom_site.B_iso_or_equiv");
    if (!parsed.has_value()) {
      return Result<ParsedAtom>::failure(parsed.error());
    }
    atom.b_factor = parsed.value();
  }
  if (const auto *token = row.get("_atom_site.pdbx_pdb_model_num");
      present(token).has_value()) {
    const auto parsed = parse_number<std::uint64_t>(
        *token, source, "_atom_site.pdbx_PDB_model_num");
    if (!parsed.has_value()) {
      return Result<ParsedAtom>::failure(parsed.error());
    }
    atom.model_number = parsed.value();
  }
  return Result<ParsedAtom>::success(std::move(atom));
}

Result<std::optional<model::UnitCell>>
block_unit_cell(const CifBlock &block, std::string_view source) {
  const std::vector<std::string_view> names{
      "_cell.length_a",    "_cell.length_b",   "_cell.length_c",
      "_cell.angle_alpha", "_cell.angle_beta", "_cell.angle_gamma"};
  std::vector<const Token *> values;
  values.reserve(names.size());
  for (const auto name : names) {
    values.push_back(scalar(block, name));
  }
  const auto present_count =
      std::count_if(values.begin(), values.end(), [](const Token *token) {
        return present(token).has_value();
      });
  if (present_count == 0) {
    return Result<std::optional<model::UnitCell>>::success(std::nullopt);
  }
  if (present_count != static_cast<std::ptrdiff_t>(values.size())) {
    return Result<std::optional<model::UnitCell>>::failure(parse_error(
        source, block.line,
        "mmCIF unit cell must provide all three lengths and three angles"));
  }
  std::vector<double> numbers;
  numbers.reserve(values.size());
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto parsed =
        parse_number<double>(*values[index], source, names[index]);
    if (!parsed.has_value()) {
      return Result<std::optional<model::UnitCell>>::failure(parsed.error());
    }
    numbers.push_back(parsed.value());
  }
  const auto cell =
      make_unit_cell(numbers[0], numbers[1], numbers[2], numbers[3], numbers[4],
                     numbers[5], source, values.front()->line);
  if (!cell.has_value()) {
    return Result<std::optional<model::UnitCell>>::failure(cell.error());
  }
  return Result<std::optional<model::UnitCell>>::success(cell.value());
}

Result<StructureData> build_structure(const CifBlock &block,
                                      std::string_view source,
                                      StructureFormat format,
                                      std::string_view syntax) {
  const CifLoop *atom_loop = nullptr;
  std::vector<const CifLoop *> connection_loops;
  for (const auto &loop : block.loops) {
    if (!loop.columns.empty() &&
        loop.columns.front().starts_with("_atom_site.")) {
      if (atom_loop != nullptr) {
        return Result<StructureData>::failure(parse_error(
            source, loop.line, "multiple atom_site loops in one data block"));
      }
      atom_loop = &loop;
    } else if (!loop.columns.empty() &&
               loop.columns.front().starts_with("_struct_conn.")) {
      connection_loops.push_back(&loop);
    }
  }
  if (atom_loop == nullptr) {
    return Result<StructureData>::failure(parse_error(
        source, block.line, "mmCIF data block contains no atom_site loop"));
  }
  std::map<std::string, std::size_t, std::less<>> columns;
  for (std::size_t index = 0; index < atom_loop->columns.size(); ++index) {
    if (!columns.emplace(atom_loop->columns[index], index).second) {
      return Result<StructureData>::failure(parse_error(
          source, atom_loop->line, "duplicate atom_site column name"));
    }
  }

  std::vector<ParsedAtom> atoms;
  const auto row_count = atom_loop->values.size() / atom_loop->columns.size();
  atoms.reserve(row_count);
  for (std::size_t row_index = 0; row_index < row_count; ++row_index) {
    const auto atom =
        parse_atom_row(RowView{*atom_loop, row_index, columns}, source);
    if (!atom.has_value()) {
      return Result<StructureData>::failure(atom.error());
    }
    atoms.push_back(atom.value());
  }

  std::vector<std::uint64_t> model_order;
  std::map<std::uint64_t, std::vector<const ParsedAtom *>> models;
  for (const auto &atom : atoms) {
    if (!models.contains(atom.model_number)) {
      model_order.push_back(atom.model_number);
    }
    models[atom.model_number].push_back(&atom);
  }
  const auto first_model_number = model_order.front();
  const auto &first_model = models.at(first_model_number);

  TopologyBuilder builder;
  std::map<std::tuple<std::string, std::int64_t, std::string, std::string,
                      std::string>,
           ResidueIndex>
      residues;
  std::map<AtomIdentity, AtomIndex> identity_to_index;
  std::vector<std::string> atom_site_ids;
  std::vector<std::string> label_asym_ids;
  std::vector<std::string> label_seq_ids;
  std::vector<std::string> label_entity_ids;
  BooleanColumn hetero;
  BooleanColumn formal_charge_present;

  for (const auto *atom : first_model) {
    const auto residue_key =
        std::tuple{atom->chain_id, atom->residue_number, atom->insertion_code,
                   atom->residue_name, atom->label_asym_id};
    auto residue = residues.find(residue_key);
    if (residue == residues.end()) {
      const auto added = builder.add_residue(ResidueRecord{
          atom->residue_name, atom->residue_number, atom->insertion_code,
          atom->chain_id, atom->label_asym_id});
      if (!added.has_value()) {
        return Result<StructureData>::failure(added.error());
      }
      residue = residues.emplace(residue_key, added.value()).first;
    }
    if (identity_to_index.contains(atom->identity) ||
        (atom->author_identity.has_value() &&
         identity_to_index.contains(*atom->author_identity))) {
      return Result<StructureData>::failure(parse_error(
          source, atom->line, "duplicate atom identity in first mmCIF model"));
    }
    std::optional<std::int64_t> numeric_id;
    if (!atom->source_id.empty()) {
      Token source_token{atom->source_id, atom->line, false};
      const auto parsed =
          parse_number<std::int64_t>(source_token, source, "_atom_site.id");
      if (parsed.has_value()) {
        numeric_id = parsed.value();
      }
    }
    const auto added = builder.add_atom(AtomRecord{
        atom->atom_name, atom->atomic_number, residue->second,
        atom->identity.alternate_location, atom->formal_charge, numeric_id,
        std::nullopt, model::AtomStereoParity::unspecified,
        model::RadicalState::none, atom->formal_charge_present,
        model::ChemicalAnnotationOrigin::explicit_input});
    if (!added.has_value()) {
      return Result<StructureData>::failure(added.error());
    }
    identity_to_index.emplace(atom->identity, added.value());
    if (atom->author_identity.has_value() &&
        *atom->author_identity != atom->identity) {
      identity_to_index.emplace(*atom->author_identity, added.value());
    }
    atom_site_ids.push_back(atom->source_id);
    label_asym_ids.push_back(atom->label_asym_id);
    label_seq_ids.push_back(atom->label_seq_id);
    label_entity_ids.push_back(atom->label_entity_id);
    hetero.values.push_back(atom->hetero ? 1U : 0U);
    formal_charge_present.values.push_back(atom->formal_charge_present ? 1U
                                                                       : 0U);
  }

  std::set<std::pair<std::size_t, std::size_t>> unique_bonds;
  for (const auto *loop : connection_loops) {
    std::map<std::string, std::size_t, std::less<>> connection_columns;
    for (std::size_t index = 0; index < loop->columns.size(); ++index) {
      connection_columns.emplace(loop->columns[index], index);
    }
    const auto connection_rows = loop->values.size() / loop->columns.size();
    for (std::size_t row_index = 0; row_index < connection_rows; ++row_index) {
      const RowView row{*loop, row_index, connection_columns};
      auto first = identity_from_row(row, "_struct_conn.ptnr1_");
      auto second = identity_from_row(row, "_struct_conn.ptnr2_");
      if (first.has_value()) {
        first->alternate_location =
            present(row.get("_struct_conn.pdbx_ptnr1_label_alt_id"))
                .value_or(first->alternate_location);
      }
      if (second.has_value()) {
        second->alternate_location =
            present(row.get("_struct_conn.pdbx_ptnr2_label_alt_id"))
                .value_or(second->alternate_location);
      }
      if (!first.has_value() || !second.has_value()) {
        return Result<StructureData>::failure(parse_error(
            source, loop->values[row_index * loop->columns.size()].line,
            "struct_conn row lacks label identifiers for both partners"));
      }
      const auto first_atom = identity_to_index.find(*first);
      const auto second_atom = identity_to_index.find(*second);
      if (first_atom == identity_to_index.end() ||
          second_atom == identity_to_index.end()) {
        return Result<StructureData>::failure(parse_error(
            source, loop->values[row_index * loop->columns.size()].line,
            "struct_conn references an atom absent from model 1"));
      }
      const auto endpoints =
          std::minmax(first_atom->second.value, second_atom->second.value);
      if (endpoints.first == endpoints.second ||
          !unique_bonds.emplace(endpoints.first, endpoints.second).second) {
        continue;
      }
      auto order = model::BondOrder::unknown;
      if (const auto value = present(row.get("_struct_conn.pdbx_value_order"));
          value.has_value()) {
        const auto normalized = lowercase(*value);
        if (normalized == "sing") {
          order = model::BondOrder::single;
        } else if (normalized == "doub") {
          order = model::BondOrder::double_bond;
        } else if (normalized == "trip") {
          order = model::BondOrder::triple;
        } else if (normalized == "arom") {
          order = model::BondOrder::aromatic;
        }
      }
      if (const auto error = builder.add_bond(model::Bond{
              AtomIndex{endpoints.first}, AtomIndex{endpoints.second}, order,
              model::BondQuery::none, model::BondStereo::none,
              model::ChemicalAnnotationOrigin::explicit_input});
          error.has_value()) {
        return Result<StructureData>::failure(*error);
      }
    }
  }

  const auto add_property =
      [&](std::string name, model::AtomPropertyColumn values,
          std::string source_name) -> std::optional<operation::Error> {
    return builder.add_property(
        std::move(name), std::move(values),
        PropertyMetadata{std::nullopt, std::move(source_name), {}});
  };
  for (auto property :
       {std::tuple{"mmcif.atom_site_id",
                   model::AtomPropertyColumn{std::move(atom_site_ids)},
                   "_atom_site.id"},
        std::tuple{"mmcif.label_asym_id",
                   model::AtomPropertyColumn{std::move(label_asym_ids)},
                   "_atom_site.label_asym_id"},
        std::tuple{"mmcif.label_seq_id",
                   model::AtomPropertyColumn{std::move(label_seq_ids)},
                   "_atom_site.label_seq_id"},
        std::tuple{"mmcif.label_entity_id",
                   model::AtomPropertyColumn{std::move(label_entity_ids)},
                   "_atom_site.label_entity_id"},
        std::tuple{"mmcif.is_hetero",
                   model::AtomPropertyColumn{std::move(hetero)},
                   "_atom_site.group_PDB"},
        std::tuple{"formal_charge_present",
                   model::AtomPropertyColumn{std::move(formal_charge_present)},
                   "_atom_site.pdbx_formal_charge"}}) {
    if (const auto error = add_property(std::get<0>(property),
                                        std::move(std::get<1>(property)),
                                        std::get<2>(property));
        error.has_value()) {
      return Result<StructureData>::failure(*error);
    }
  }

  builder.set_source_metadata("format", std::string{to_string(format)});
  builder.set_source_metadata("syntax", std::string{syntax});
  builder.set_source_metadata("data_block", block.name);
  builder.set_source_metadata("source_name", std::string{source});
  const auto topology = builder.build();
  if (!topology.has_value()) {
    return Result<StructureData>::failure(topology.error());
  }

  const auto cell = block_unit_cell(block, source);
  if (!cell.has_value()) {
    return Result<StructureData>::failure(cell.error());
  }
  std::vector<std::shared_ptr<const CoordinateFrame>> frames;
  frames.reserve(model_order.size());
  for (const auto model_number : model_order) {
    std::vector<Vec3d> positions(topology.value()->atom_count());
    std::vector<std::uint8_t> presence(topology.value()->atom_count(), 0U);
    std::vector<double> occupancy(topology.value()->atom_count(), 0.0);
    std::vector<std::uint8_t> occupancy_present(topology.value()->atom_count(),
                                                0U);
    std::vector<double> b_factor(topology.value()->atom_count(), 0.0);
    std::vector<std::uint8_t> b_factor_present(topology.value()->atom_count(),
                                               0U);
    std::set<std::size_t> seen;
    for (const auto *atom : models.at(model_number)) {
      const auto found = identity_to_index.find(atom->identity);
      if (found == identity_to_index.end()) {
        return Result<StructureData>::failure(parse_error(
            source, atom->line,
            "later mmCIF model contains an atom identity absent from model 1"));
      }
      const auto index = found->second.value;
      if (!seen.insert(index).second) {
        return Result<StructureData>::failure(parse_error(
            source, atom->line, "duplicate atom identity within mmCIF model"));
      }
      positions[index] = atom->position;
      presence[index] = 1U;
      if (atom->occupancy.has_value()) {
        occupancy[index] = *atom->occupancy;
        occupancy_present[index] = 1U;
      }
      if (atom->b_factor.has_value()) {
        b_factor[index] = *atom->b_factor;
        b_factor_present[index] = 1U;
      }
    }
    FrameMetadata metadata;
    metadata.source_step = model_number;
    metadata.unit_cell = cell.value();
    metadata.coordinate_unit = operation::LengthUnit::angstrom;
    metadata.atom_properties.emplace(
        "occupancy",
        model::AtomProperty{
            std::move(occupancy),
            PropertyMetadata{std::nullopt, "_atom_site.occupancy", {}}});
    metadata.atom_properties.emplace(
        "occupancy_present",
        model::AtomProperty{
            BooleanColumn{std::move(occupancy_present)},
            PropertyMetadata{std::nullopt, "_atom_site.occupancy", {}}});
    metadata.atom_properties.emplace(
        "b_iso_or_equiv",
        model::AtomProperty{
            std::move(b_factor),
            PropertyMetadata{"angstrom^2", "_atom_site.B_iso_or_equiv", {}}});
    metadata.atom_properties.emplace(
        "b_iso_or_equiv_present",
        model::AtomProperty{
            BooleanColumn{std::move(b_factor_present)},
            PropertyMetadata{std::nullopt, "_atom_site.B_iso_or_equiv", {}}});
    const auto frame = CoordinateFrame::create(
        CoordinateBuffer{std::move(positions)}, std::nullopt,
        std::move(presence), std::move(metadata));
    if (!frame.has_value()) {
      return Result<StructureData>::failure(frame.error());
    }
    frames.push_back(frame.value());
  }
  const auto coordinates = model::InMemoryCoordinateSource::create(
      topology.value()->atom_count(), std::move(frames));
  if (!coordinates.has_value()) {
    return Result<StructureData>::failure(coordinates.error());
  }

  StructureData structure;
  structure.name = present(scalar(block, "_entry.id")).value_or(block.name);
  structure.topology = topology.value();
  structure.coordinates = coordinates.value();
  structure.metadata.emplace("format", to_string(format));
  structure.metadata.emplace("syntax", syntax);
  structure.metadata.emplace("data_block", block.name);
  for (const auto &[name, value] : block.scalars) {
    structure.metadata.emplace(name, value.text);
  }
  return Result<StructureData>::success(std::move(structure));
}

} // namespace

Result<StructureDocument>
build_cif_document(const std::vector<CifBlock> &blocks, std::string source_name,
                   StructureFormat format, std::string syntax) {
  if (blocks.empty()) {
    return Result<StructureDocument>::failure(
        parse_error(source_name, 1, "CIF document contains no data block"));
  }
  StructureDocument document;
  document.format = format;
  document.source_name = source_name;
  for (const auto &block : blocks) {
    const auto structure = build_structure(block, source_name, format, syntax);
    if (!structure.has_value()) {
      return Result<StructureDocument>::failure(structure.error());
    }
    document.structures.push_back(structure.value());
  }
  return Result<StructureDocument>::success(std::move(document));
}

Result<StructureDocument> read_mmcif(std::string_view content,
                                     std::string source_name) {
  const auto tokens = tokenize(content, source_name);
  if (!tokens.has_value()) {
    return Result<StructureDocument>::failure(tokens.error());
  }
  const auto blocks = parse_blocks(tokens.value(), source_name);
  if (!blocks.has_value()) {
    return Result<StructureDocument>::failure(blocks.error());
  }
  return build_cif_document(blocks.value(), std::move(source_name),
                            StructureFormat::mmcif, "CIF 1.1");
}

} // namespace molshredder::io::detail
