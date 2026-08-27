#include "molshredder/selection/expression.hpp"

#include <cctype>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "molshredder/core/parse_number.hpp"
#include "molshredder/operation/error.hpp"

namespace molshredder::selection {
namespace {

enum class TokenKind {
  word,
  operation,
  left_parenthesis,
  right_parenthesis,
  end
};

struct Token {
  TokenKind kind{TokenKind::end};
  std::string text;
  std::size_t offset{};
  bool quoted{};
};

operation::Error syntax_error(std::string message, std::size_t offset) {
  return operation::Error{
      operation::ErrorCode::invalid_selection,
      std::move(message) + " at byte " + std::to_string(offset),
      "use explicit and/or/not operators, parentheses, and @name for named "
      "selections"};
}

std::string lowercase(std::string value) {
  for (auto& character : value) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  return value;
}

operation::Result<std::vector<Token>> tokenize(std::string_view source) {
  std::vector<Token> tokens;
  std::size_t offset{};
  while (offset < source.size()) {
    const auto byte = static_cast<unsigned char>(source[offset]);
    if (std::isspace(byte) != 0) {
      ++offset;
      continue;
    }
    if (source[offset] == '(' || source[offset] == ')') {
      tokens.push_back(Token{
          source[offset] == '(' ? TokenKind::left_parenthesis
                                : TokenKind::right_parenthesis,
          std::string{source[offset]}, offset, false});
      ++offset;
      continue;
    }
    if (source[offset] == '<' || source[offset] == '>' ||
        source[offset] == '=' || source[offset] == '!' ||
        source[offset] == '+' || source[offset] == '*' ||
        source[offset] == '/' || source[offset] == '&' ||
        source[offset] == '|') {
      const auto start = offset++;
      if (offset < source.size() && source[offset] == '=' &&
          (source[start] == '<' || source[start] == '>' ||
           source[start] == '=' || source[start] == '!')) {
        ++offset;
      }
      tokens.push_back(Token{TokenKind::operation,
                             std::string{source.substr(start, offset - start)},
                             start, false});
      continue;
    }
    if (source[offset] == '-') {
      const bool expects_operand =
          tokens.empty() || tokens.back().kind == TokenKind::operation ||
          tokens.back().kind == TokenKind::left_parenthesis ||
          (tokens.back().kind == TokenKind::word &&
           lowercase(tokens.back().text) == "in");
      const bool begins_number = offset + 1U < source.size() &&
                                 (std::isdigit(static_cast<unsigned char>(
                                      source[offset + 1U])) != 0 ||
                                  source[offset + 1U] == '.');
      if (!expects_operand || !begins_number) {
        tokens.push_back(
            Token{TokenKind::operation, "-", offset++, false});
        continue;
      }
    }
    const auto start = offset;
    std::string value;
    bool quoted{};
    if (source[offset] == '\'' || source[offset] == '"') {
      quoted = true;
      const auto quote = source[offset++];
      bool terminated = false;
      while (offset < source.size()) {
        const auto character = source[offset++];
        if (character == quote) {
          terminated = true;
          break;
        }
        if (character == '\\') {
          if (offset >= source.size()) {
            return operation::Result<std::vector<Token>>::failure(
                syntax_error("unterminated escape", offset - 1U));
          }
          value.push_back(source[offset++]);
        } else {
          value.push_back(character);
        }
      }
      if (!terminated) {
        return operation::Result<std::vector<Token>>::failure(
            syntax_error("unterminated quoted value", start));
      }
    } else {
      if (source[offset] == '-') {
        value.push_back(source[offset++]);
      }
      while (offset < source.size() &&
             std::isspace(static_cast<unsigned char>(source[offset])) == 0 &&
             source[offset] != '(' && source[offset] != ')' &&
             source[offset] != '<' && source[offset] != '>' &&
             source[offset] != '=' && source[offset] != '!' &&
             source[offset] != '+' && source[offset] != '*' &&
             source[offset] != '/' && source[offset] != '&' &&
             source[offset] != '|') {
        value.push_back(source[offset++]);
      }
    }
    if (value.empty() && !quoted) {
      return operation::Result<std::vector<Token>>::failure(
          syntax_error("empty selection token", start));
    }
    tokens.push_back(
        Token{TokenKind::word, std::move(value), start, quoted});
  }
  tokens.push_back(Token{TokenKind::end, {}, source.size(), false});
  return operation::Result<std::vector<Token>>::success(std::move(tokens));
}

std::optional<Field> field_from(std::string text) {
  text = lowercase(std::move(text));
  if (text == "name" || text == "n." || text == "n;")
    return Field::atom_name;
  if (text == "element" || text == "elem" || text == "symbol" ||
      text == "e." || text == "e;")
    return Field::element;
  if (text == "altloc" || text == "alt") return Field::alternate_location;
  if (text == "resname" || text == "resn" || text == "r." ||
      text == "r;")
    return Field::residue_name;
  if (text == "resid" || text == "resi" || text == "residue" ||
      text == "resident" || text == "i." || text == "i;")
    return Field::residue_id;
  if (text == "chain" || text == "c." || text == "c;")
    return Field::chain;
  if (text == "segment" || text == "segid" || text == "segi" ||
      text == "s." || text == "s;")
    return Field::segment;
  if (text == "object" || text == "model" || text == "o." ||
      text == "m." || text == "m;")
    return Field::object;
  if (text == "rank") return Field::rank;
  if (text == "pepseq" || text == "ps.") return Field::peptide_sequence;
  if (text == "index" || text == "idx.") return Field::index;
  if (text == "id") return Field::source_id;
  if (text == "state") return Field::state;
  if (text == "ss" || text == "stereo" || text == "label" ||
      text == "flag" || text == "f." || text == "f;" ||
      text == "color" || text == "cartoon_color" ||
      text == "ribbon_color" || text == "rep" ||
      text == "numeric_type" || text == "nt." || text == "nt;" ||
      text == "text_type" || text == "tt." || text == "tt;" ||
      text == "custom")
    return Field::property;
  if (text.starts_with("p.") && text.size() > 2U) return Field::property;
  return std::nullopt;
}

std::string property_name_from(std::string text) {
  text = lowercase(std::move(text));
  if (text.starts_with("p.")) return text.substr(2U);
  if (text == "ss") return "secondary_structure";
  if (text == "nt." || text == "nt;") return "numeric_type";
  if (text == "tt." || text == "tt;") return "text_type";
  if (text == "f." || text == "f;") return "flag";
  if (text == "rep") return "representation";
  return text;
}

std::optional<ChemicalClass> chemical_class_from(std::string text) {
  text = lowercase(std::move(text));
  if (text == "hetatm" || text == "het") return ChemicalClass::hetero;
  if (text == "hydrogens" || text == "hydro" || text == "h." ||
      text == "h;")
    return ChemicalClass::hydrogen;
  if (text == "donors" || text == "don." || text == "hbd.")
    return ChemicalClass::donor;
  if (text == "acceptors" || text == "acc." || text == "hba.")
    return ChemicalClass::acceptor;
  if (text == "polymer" || text == "pol.") return ChemicalClass::polymer;
  if (text == "polymer.protein") return ChemicalClass::protein;
  if (text == "polymer.nucleic") return ChemicalClass::nucleic;
  if (text == "organic" || text == "org.") return ChemicalClass::organic;
  if (text == "inorganic" || text == "ino.")
    return ChemicalClass::inorganic;
  if (text == "solvent" || text == "sol.") return ChemicalClass::solvent;
  if (text == "metals") return ChemicalClass::metal;
  if (text == "backbone" || text == "bb.") return ChemicalClass::backbone;
  if (text == "sidechain" || text == "sc.")
    return ChemicalClass::sidechain;
  if (text == "guide") return ChemicalClass::guide;
  return std::nullopt;
}

std::optional<double> number(std::string_view text) {
  double value{};
  const auto parsed = molshredder::core::from_chars(
      text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::pair<NumericField, std::string>> numeric_field_from(
    std::string text) {
  text = lowercase(std::move(text));
  if (text == "index") return std::pair{NumericField::index, std::string{}};
  if (text == "id") return std::pair{NumericField::source_id, std::string{}};
  if (text == "formal_charge" || text == "fc" || text == "fc." ||
      text == "fc;") {
    return std::pair{NumericField::formal_charge, std::string{}};
  }
  if (text == "b" || text == "b_factor" || text == "b_iso_or_equiv") {
    return std::pair{NumericField::property, std::string{"b_factor"}};
  }
  if (text == "q" || text == "occupancy") {
    return std::pair{NumericField::property, std::string{"occupancy"}};
  }
  if (text == "partial_charge" || text == "pc" || text == "pc." ||
      text == "pc;") {
    return std::pair{NumericField::property, std::string{"partial_charge"}};
  }
  if (text == "x")
    return std::pair{NumericField::coordinate_x, std::string{}};
  if (text == "y")
    return std::pair{NumericField::coordinate_y, std::string{}};
  if (text == "z")
    return std::pair{NumericField::coordinate_z, std::string{}};
  if (text.starts_with("p.") && text.size() > 2U) {
    return std::pair{NumericField::property, text.substr(2U)};
  }
  return std::nullopt;
}

std::shared_ptr<const Node> leaf(NodeKind kind) {
  auto node = std::make_shared<Node>();
  node->kind = kind;
  return node;
}

class Parser {
 public:
  explicit Parser(const std::vector<Token>& tokens) : tokens_{tokens} {}

  operation::Result<std::shared_ptr<const Node>> parse() {
    auto root = parse_or();
    if (!root.has_value()) {
      return root;
    }
    if (current().kind != TokenKind::end) {
      return failure("unexpected token '" + current().text + "'",
                     current().offset);
    }
    return root;
  }

 private:
  const Token& current() const { return tokens_[position_]; }

  bool keyword(std::string_view value) const {
    return current().kind == TokenKind::word && !current().quoted &&
           lowercase(current().text) == value;
  }

  bool operation(std::string_view value) const {
    return current().kind == TokenKind::operation && current().text == value;
  }

  operation::Result<std::shared_ptr<const Node>> failure(
      std::string message, std::size_t offset) const {
    return operation::Result<std::shared_ptr<const Node>>::failure(
        syntax_error(std::move(message), offset));
  }

  operation::Result<std::shared_ptr<const Node>> parse_or() {
    auto left = parse_and();
    if (!left.has_value()) return left;
    while (keyword("or") || operation("|") || operation("+")) {
      ++position_;
      auto right = parse_and();
      if (!right.has_value()) return right;
      auto node = std::make_shared<Node>();
      node->kind = NodeKind::disjunction;
      node->left = left.value();
      node->right = right.value();
      left = operation::Result<std::shared_ptr<const Node>>::success(node);
    }
    return left;
  }

  operation::Result<std::shared_ptr<const Node>> parse_and() {
    auto left = parse_relation();
    if (!left.has_value()) return left;
    const auto implicit_term = [&] {
      if (current().kind == TokenKind::left_parenthesis) return true;
      if (operation("!") || operation("*")) return true;
      if (current().kind != TokenKind::word || current().quoted) return false;
      const auto token = lowercase(current().text);
      return token != "or" && token != "within" && token != "near_to" &&
             token != "beyond" && token != "in" && token != "like" &&
             token != "l." && token != "l;" && token != "of";
    };
    while (keyword("and") || operation("&") || operation("-") ||
           implicit_term()) {
      const bool subtract = operation("-");
      if (!implicit_term() || keyword("and") || operation("&") || subtract)
        ++position_;
      auto right = parse_relation();
      if (!right.has_value()) return right;
      auto node = std::make_shared<Node>();
      node->kind = subtract ? NodeKind::subtraction : NodeKind::conjunction;
      node->left = left.value();
      node->right = right.value();
      left = operation::Result<std::shared_ptr<const Node>>::success(node);
    }
    return left;
  }

  operation::Result<std::shared_ptr<const Node>> parse_unary() {
    if (keyword("not") || operation("!")) {
      ++position_;
      auto operand = parse_unary();
      if (!operand.has_value()) return operand;
      auto node = std::make_shared<Node>();
      node->kind = NodeKind::negation;
      node->left = operand.value();
      return operation::Result<std::shared_ptr<const Node>>::success(node);
    }
    if (keyword("first") || keyword("last")) {
      const bool first = keyword("first");
      ++position_;
      auto operand = parse_unary();
      if (!operand.has_value()) return operand;
      auto node = std::make_shared<Node>();
      node->kind = first ? NodeKind::first : NodeKind::last;
      node->left = operand.value();
      return operation::Result<std::shared_ptr<const Node>>::success(node);
    }
    const auto expansion = expansion_operator();
    if (expansion.has_value()) {
      ++position_;
      auto operand = parse_unary();
      if (!operand.has_value()) return operand;
      auto node = std::make_shared<Node>();
      node->kind = NodeKind::expansion;
      node->expansion.operation = expansion.value();
      node->left = operand.value();
      return operation::Result<std::shared_ptr<const Node>>::success(node);
    }
    return parse_postfix();
  }

  std::optional<ExpansionOperator> expansion_operator() const {
    if (keyword("byresidue") || keyword("byresi") || keyword("byres") ||
        keyword("br.") || keyword("br;") || keyword("b;"))
      return ExpansionOperator::residue;
    if (keyword("bychain") || keyword("bc."))
      return ExpansionOperator::chain;
    if (keyword("bysegment") || keyword("byseg") || keyword("bysegi") ||
        keyword("bs."))
      return ExpansionOperator::segment;
    if (keyword("byobject") || keyword("byobj") || keyword("bo.") ||
        keyword("bo;"))
      return ExpansionOperator::object;
    if (keyword("bymolecule") || keyword("bymol") || keyword("bm."))
      return ExpansionOperator::molecule;
    if (keyword("byfragment") || keyword("byfrag") || keyword("bf."))
      return ExpansionOperator::molecule;
    if (keyword("bycalpha") || keyword("bca."))
      return ExpansionOperator::calpha;
    if (keyword("neighbor") || keyword("nbr.") || keyword("nbr;"))
      return ExpansionOperator::neighbor;
    if (keyword("bound_to") || keyword("bto."))
      return ExpansionOperator::bound_to;
    if (keyword("byring")) return ExpansionOperator::ring;
    if (keyword("bycell")) return ExpansionOperator::unit_cell;
    return std::nullopt;
  }

  operation::Result<std::shared_ptr<const Node>> parse_postfix() {
    auto operand = parse_primary();
    if (!operand.has_value()) return operand;
    while (keyword("around") || keyword("a.") || keyword("a;") ||
           keyword("expand") || keyword("x.") || keyword("x;") ||
           keyword("extend") || keyword("xt.") || keyword("gap")) {
      const auto token = lowercase(current().text);
      ++position_;
      double sign = 1.0;
      if (token == "gap" && operation("-")) {
        sign = -1.0;
        ++position_;
      }
      if (current().kind != TokenKind::word || current().quoted) {
        return failure("selection expansion requires a numeric distance or step",
                       current().offset);
      }
      auto node = std::make_shared<Node>();
      node->left = operand.value();
      if (token == "extend" || token == "xt.") {
        const auto parsed_steps = number(current().text);
        if (!parsed_steps.has_value() || parsed_steps.value() < 0.0 ||
            std::floor(parsed_steps.value()) != parsed_steps.value()) {
          return failure("extend requires a non-negative integer step count",
                         current().offset);
        }
        node->kind = NodeKind::expansion;
        node->expansion.operation = ExpansionOperator::bond_steps;
        node->expansion.steps = static_cast<std::size_t>(parsed_steps.value());
      } else {
        const auto parsed_distance = number(current().text);
        const auto distance = parsed_distance.has_value()
                                  ? std::optional<double>{sign * parsed_distance.value()}
                                  : std::nullopt;
        if (!distance.has_value() || !std::isfinite(distance.value()) ||
            (token != "gap" && distance.value() < 0.0)) {
          return failure("spatial distance must be finite and non-negative",
                         current().offset);
        }
        node->kind = NodeKind::spatial;
        node->spatial.operation =
            token == "around" || token == "a." || token == "a;"
                ? SpatialOperator::around
            : token == "gap" ? SpatialOperator::vdw_gap
                              : SpatialOperator::expand;
        node->spatial.distance = distance.value();
      }
      ++position_;
      operand = operation::Result<std::shared_ptr<const Node>>::success(node);
    }
    return operand;
  }

  operation::Result<std::shared_ptr<const Node>> parse_relation() {
    auto left = parse_unary();
    if (!left.has_value()) return left;
    while (keyword("within") || keyword("w.") || keyword("near_to") ||
           keyword("nto.") || keyword("beyond") || keyword("be.") ||
           keyword("in") || keyword("like") || keyword("l.") ||
           keyword("l;")) {
      const auto token = lowercase(current().text);
      ++position_;
      if (token == "in" || token == "like" || token == "l." ||
          token == "l;") {
        auto right = parse_unary();
        if (!right.has_value()) return right;
        auto node = std::make_shared<Node>();
        node->kind = NodeKind::match;
        node->left = left.value();
        node->right = right.value();
        node->match = token == "in" ? MatchOperator::identifiers
                                    : MatchOperator::name_residue;
        left = operation::Result<std::shared_ptr<const Node>>::success(node);
        continue;
      }
      if (current().kind != TokenKind::word || current().quoted) {
        return failure("spatial relation requires a numeric distance",
                       current().offset);
      }
      const auto distance = number(current().text);
      if (!distance.has_value() || !std::isfinite(distance.value()) ||
          distance.value() < 0.0) {
        return failure("spatial distance must be finite and non-negative",
                       current().offset);
      }
      ++position_;
      if (!keyword("of"))
        return failure("spatial relation requires 'of'", current().offset);
      ++position_;
      auto right = parse_unary();
      if (!right.has_value()) return right;
      auto node = std::make_shared<Node>();
      node->kind = NodeKind::spatial;
      node->left = left.value();
      node->right = right.value();
      node->spatial.distance = distance.value();
      node->spatial.operation =
          token == "within" || token == "w."
              ? SpatialOperator::within
          : token == "near_to" || token == "nto."
              ? SpatialOperator::near_to
              : SpatialOperator::beyond;
      left = operation::Result<std::shared_ptr<const Node>>::success(node);
    }
    return left;
  }

  operation::Result<std::shared_ptr<const Node>> parse_primary() {
    if (current().kind == TokenKind::left_parenthesis) {
      std::size_t depth{};
      std::size_t close = position_;
      for (; close < tokens_.size(); ++close) {
        if (tokens_[close].kind == TokenKind::left_parenthesis)
          ++depth;
        else if (tokens_[close].kind == TokenKind::right_parenthesis &&
                 --depth == 0U)
          break;
      }
      if (close + 1U < tokens_.size() &&
          (tokens_[close + 1U].kind == TokenKind::operation ||
           (tokens_[close + 1U].kind == TokenKind::word &&
            !tokens_[close + 1U].quoted &&
            lowercase(tokens_[close + 1U].text) == "in"))) {
        return parse_numeric_comparison();
      }
      const auto start = current().offset;
      ++position_;
      auto expression = parse_or();
      if (!expression.has_value()) return expression;
      if (current().kind != TokenKind::right_parenthesis) {
        return failure("missing ')'", start);
      }
      ++position_;
      return expression;
    }
    if (operation("*")) {
      ++position_;
      return operation::Result<std::shared_ptr<const Node>>::success(
          leaf(NodeKind::all));
    }
    if (current().kind != TokenKind::word) {
      return failure("expected selection term", current().offset);
    }
    const auto token = current();
    const auto lowered = lowercase(token.text);
    if (!token.quoted && (lowered == "all" || lowered == "none")) {
      ++position_;
      return operation::Result<std::shared_ptr<const Node>>::success(
          leaf(lowered == "all" ? NodeKind::all : NodeKind::none));
    }
    if (!token.quoted &&
        (lowered == "present" || lowered == "pr." || lowered == "bonded")) {
      ++position_;
      return operation::Result<std::shared_ptr<const Node>>::success(
          leaf(lowered == "bonded" ? NodeKind::bonded : NodeKind::present));
    }
    if (!token.quoted && (lowered == "center" || lowered == "origin")) {
      ++position_;
      return operation::Result<std::shared_ptr<const Node>>::success(
          leaf(lowered == "center" ? NodeKind::scene_center
                                    : NodeKind::rotation_origin));
    }
    if (!token.quoted &&
        (lowered == "enabled" || lowered == "visible" || lowered == "v." ||
         lowered == "v;" || lowered == "fixed" || lowered == "fxd." ||
         lowered == "masked" || lowered == "msk." ||
         lowered == "protected" || lowered == "restrained" ||
         lowered == "rst.")) {
      auto node = std::make_shared<Node>();
      node->kind = NodeKind::boolean_property;
      node->name = lowered == "v." || lowered == "v;" ? "visible"
                   : lowered == "fxd."                 ? "fixed"
                   : lowered == "msk."                 ? "masked"
                   : lowered == "rst."                 ? "restrained"
                                                        : lowered;
      ++position_;
      return operation::Result<std::shared_ptr<const Node>>::success(node);
    }
    if (!token.quoted) {
      const auto chemical = chemical_class_from(token.text);
      if (chemical.has_value()) {
        auto node = std::make_shared<Node>();
        node->kind = NodeKind::chemical;
        node->chemical = chemical.value();
        ++position_;
        return operation::Result<std::shared_ptr<const Node>>::success(node);
      }
    }
    if (!token.quoted &&
        (token.text.starts_with('@') || token.text.starts_with('%'))) {
      if (token.text.size() == 1U) {
        return failure("named selection requires a name", token.offset);
      }
      auto node = std::make_shared<Node>();
      node->kind = NodeKind::named;
      node->name = token.text.substr(1U);
      ++position_;
      return operation::Result<std::shared_ptr<const Node>>::success(node);
    }
    if (!token.quoted && numeric_field_from(token.text).has_value() &&
        ((tokens_[position_ + 1U].kind == TokenKind::operation) ||
         (tokens_[position_ + 1U].kind == TokenKind::word &&
          !tokens_[position_ + 1U].quoted &&
          lowercase(tokens_[position_ + 1U].text) == "in"))) {
      return parse_numeric_comparison();
    }
    const auto field = token.quoted ? std::nullopt : field_from(token.text);
    if (!field.has_value()) {
      return failure("unknown selection keyword '" + token.text + "'",
                     token.offset);
    }
    ++position_;
    if ((current().kind != TokenKind::word && !operation("*")) ||
        keyword("and") || keyword("or")) {
      return failure("selection keyword requires a value", current().offset);
    }
    std::string value;
    bool literal{};
    if (operation("*")) {
      value = "*";
      ++position_;
    } else {
      value = current().text;
      literal = current().quoted;
      ++position_;
    }
    while (!literal && (operation("+") || operation("*"))) {
      const auto separator = current().text;
      if (separator == "+" && tokens_[position_ + 1U].kind == TokenKind::word &&
          field_from(tokens_[position_ + 1U].text).has_value()) {
        break;
      }
      value += separator;
      ++position_;
      if (current().kind == TokenKind::word && !keyword("and") &&
          !keyword("or")) {
        value += current().text;
        ++position_;
      } else if (separator == "+") {
        return failure("selection value list has an empty item",
                       current().offset);
      }
    }
    auto node = std::make_shared<Node>();
    node->kind = NodeKind::predicate;
    node->predicate = Predicate{field.value(), std::move(value), literal,
                                field.value() == Field::property
                                    ? property_name_from(token.text)
                                    : std::string{}};
    return operation::Result<std::shared_ptr<const Node>>::success(node);
  }

  operation::Result<std::shared_ptr<const NumericNode>> numeric_failure(
      std::string message, std::size_t offset) const {
    return operation::Result<std::shared_ptr<const NumericNode>>::failure(
        syntax_error(std::move(message), offset));
  }

  operation::Result<std::shared_ptr<const NumericNode>> parse_numeric_primary() {
    if (operation("-")) {
      ++position_;
      auto operand = parse_numeric_primary();
      if (!operand.has_value()) return operand;
      auto node = std::make_shared<NumericNode>();
      node->kind = NumericNodeKind::negation;
      node->left = operand.value();
      return operation::Result<std::shared_ptr<const NumericNode>>::success(node);
    }
    if (current().kind == TokenKind::left_parenthesis) {
      const auto start = current().offset;
      ++position_;
      auto expression = parse_numeric_sum();
      if (!expression.has_value()) return expression;
      if (current().kind != TokenKind::right_parenthesis) {
        return numeric_failure("missing ')' in numeric expression", start);
      }
      ++position_;
      return expression;
    }
    if (current().kind != TokenKind::word || current().quoted) {
      return numeric_failure("expected numeric literal or property",
                             current().offset);
    }
    const auto token = current();
    auto node = std::make_shared<NumericNode>();
    if (const auto literal = number(token.text); literal.has_value()) {
      node->kind = NumericNodeKind::literal;
      node->literal = literal.value();
    } else if (const auto field = numeric_field_from(token.text);
               field.has_value()) {
      node->kind = NumericNodeKind::field;
      node->field = field->first;
      node->property = field->second;
    } else {
      return numeric_failure("unknown numeric property '" + token.text + "'",
                             token.offset);
    }
    ++position_;
    return operation::Result<std::shared_ptr<const NumericNode>>::success(node);
  }

  operation::Result<std::shared_ptr<const NumericNode>> parse_numeric_product() {
    auto left = parse_numeric_primary();
    if (!left.has_value()) return left;
    while (operation("*") || operation("/")) {
      const auto kind = operation("*") ? NumericNodeKind::multiplication
                                         : NumericNodeKind::division;
      ++position_;
      auto right = parse_numeric_primary();
      if (!right.has_value()) return right;
      auto node = std::make_shared<NumericNode>();
      node->kind = kind;
      node->left = left.value();
      node->right = right.value();
      left = operation::Result<std::shared_ptr<const NumericNode>>::success(node);
    }
    return left;
  }

  operation::Result<std::shared_ptr<const NumericNode>> parse_numeric_sum() {
    auto left = parse_numeric_product();
    if (!left.has_value()) return left;
    while (operation("+") || operation("-")) {
      const auto kind = operation("+") ? NumericNodeKind::addition
                                         : NumericNodeKind::subtraction;
      ++position_;
      auto right = parse_numeric_product();
      if (!right.has_value()) return right;
      auto node = std::make_shared<NumericNode>();
      node->kind = kind;
      node->left = left.value();
      node->right = right.value();
      left = operation::Result<std::shared_ptr<const NumericNode>>::success(node);
    }
    return left;
  }

  operation::Result<std::vector<NumericInterval>> parse_intervals() {
    if (current().kind != TokenKind::word || current().quoted) {
      return operation::Result<std::vector<NumericInterval>>::failure(
          syntax_error("'in' requires a numeric range or list", current().offset));
    }
    std::vector<NumericInterval> intervals;
    const auto source = std::string_view{current().text};
    std::size_t begin{};
    while (begin <= source.size()) {
      const auto comma = source.find(',', begin);
      const auto item = source.substr(
          begin, comma == std::string_view::npos ? source.size() - begin
                                                 : comma - begin);
      const auto colon = item.find(':');
      const auto first = number(item.substr(0U, colon));
      const auto last = colon == std::string_view::npos
                            ? first
                            : number(item.substr(colon + 1U));
      if (!first.has_value() || !last.has_value() ||
          first.value() > last.value()) {
        return operation::Result<std::vector<NumericInterval>>::failure(
            syntax_error("invalid numeric range/list", current().offset));
      }
      intervals.push_back({first.value(), last.value()});
      if (comma == std::string_view::npos) break;
      begin = comma + 1U;
    }
    ++position_;
    return operation::Result<std::vector<NumericInterval>>::success(
        std::move(intervals));
  }

  operation::Result<std::shared_ptr<const Node>> parse_numeric_comparison() {
    auto left = parse_numeric_sum();
    if (!left.has_value()) {
      return operation::Result<std::shared_ptr<const Node>>::failure(left.error());
    }
    ComparisonOperator comparison{};
    bool range{};
    if (keyword("in")) {
      comparison = ComparisonOperator::range;
      range = true;
    } else if (current().kind == TokenKind::operation) {
      if (current().text == "=" || current().text == "==")
        comparison = ComparisonOperator::equal;
      else if (current().text == "!=")
        comparison = ComparisonOperator::not_equal;
      else if (current().text == "<")
        comparison = ComparisonOperator::less;
      else if (current().text == "<=")
        comparison = ComparisonOperator::less_equal;
      else if (current().text == ">")
        comparison = ComparisonOperator::greater;
      else if (current().text == ">=")
        comparison = ComparisonOperator::greater_equal;
      else
        return failure("expected numeric comparison operator", current().offset);
    } else {
      return failure("numeric property requires a comparison operator",
                     current().offset);
    }
    ++position_;
    auto node = std::make_shared<Node>();
    node->kind = NodeKind::numeric_comparison;
    node->numeric_comparison.operation = comparison;
    node->numeric_comparison.left = left.value();
    if (range) {
      auto intervals = parse_intervals();
      if (!intervals.has_value()) {
        return operation::Result<std::shared_ptr<const Node>>::failure(
            intervals.error());
      }
      node->numeric_comparison.intervals = std::move(intervals.value());
    } else {
      auto right = parse_numeric_sum();
      if (!right.has_value()) {
        return operation::Result<std::shared_ptr<const Node>>::failure(
            right.error());
      }
      node->numeric_comparison.right = right.value();
    }
    return operation::Result<std::shared_ptr<const Node>>::success(node);
  }

  const std::vector<Token>& tokens_;
  std::size_t position_{};
};

void collect_references(const std::shared_ptr<const Node>& node,
                        std::set<std::string, std::less<>>& result) {
  if (node == nullptr) return;
  if (node->kind == NodeKind::named) result.insert(node->name);
  collect_references(node->left, result);
  collect_references(node->right, result);
}

}  // namespace

operation::Result<Expression> Expression::parse(std::string_view source) {
  const auto tokens = tokenize(source);
  if (!tokens.has_value()) {
    return operation::Result<Expression>::failure(tokens.error());
  }
  if (tokens.value().size() == 1U) {
    return operation::Result<Expression>::failure(
        syntax_error("selection expression is empty", 0U));
  }
  Parser parser{tokens.value()};
  const auto root = parser.parse();
  if (!root.has_value()) {
    return operation::Result<Expression>::failure(root.error());
  }
  return operation::Result<Expression>::success(
      Expression{std::string{source}, root.value()});
}

std::set<std::string, std::less<>> Expression::named_references() const {
  std::set<std::string, std::less<>> result;
  collect_references(root_, result);
  return result;
}

}  // namespace molshredder::selection
