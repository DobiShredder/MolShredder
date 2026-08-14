#include "molshredder/selection/expression.hpp"

#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "molshredder/operation/error.hpp"

namespace molshredder::selection {
namespace {

enum class TokenKind { word, left_parenthesis, right_parenthesis, end };

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
      while (offset < source.size() &&
             std::isspace(static_cast<unsigned char>(source[offset])) == 0 &&
             source[offset] != '(' && source[offset] != ')') {
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
  if (text == "name") return Field::atom_name;
  if (text == "element" || text == "elem") return Field::element;
  if (text == "resname" || text == "resn") return Field::residue_name;
  if (text == "resid" || text == "resi") return Field::residue_id;
  if (text == "chain") return Field::chain;
  if (text == "segment" || text == "segi") return Field::segment;
  if (text == "index") return Field::index;
  if (text == "id") return Field::source_id;
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

  operation::Result<std::shared_ptr<const Node>> failure(
      std::string message, std::size_t offset) const {
    return operation::Result<std::shared_ptr<const Node>>::failure(
        syntax_error(std::move(message), offset));
  }

  operation::Result<std::shared_ptr<const Node>> parse_or() {
    auto left = parse_and();
    if (!left.has_value()) return left;
    while (keyword("or")) {
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
    auto left = parse_unary();
    if (!left.has_value()) return left;
    while (keyword("and")) {
      ++position_;
      auto right = parse_unary();
      if (!right.has_value()) return right;
      auto node = std::make_shared<Node>();
      node->kind = NodeKind::conjunction;
      node->left = left.value();
      node->right = right.value();
      left = operation::Result<std::shared_ptr<const Node>>::success(node);
    }
    return left;
  }

  operation::Result<std::shared_ptr<const Node>> parse_unary() {
    if (keyword("not")) {
      ++position_;
      auto operand = parse_unary();
      if (!operand.has_value()) return operand;
      auto node = std::make_shared<Node>();
      node->kind = NodeKind::negation;
      node->left = operand.value();
      return operation::Result<std::shared_ptr<const Node>>::success(node);
    }
    return parse_primary();
  }

  operation::Result<std::shared_ptr<const Node>> parse_primary() {
    if (current().kind == TokenKind::left_parenthesis) {
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
    if (!token.quoted && token.text.starts_with('@')) {
      if (token.text.size() == 1U) {
        return failure("named selection requires a name", token.offset);
      }
      auto node = std::make_shared<Node>();
      node->kind = NodeKind::named;
      node->name = token.text.substr(1U);
      ++position_;
      return operation::Result<std::shared_ptr<const Node>>::success(node);
    }
    const auto field = token.quoted ? std::nullopt : field_from(token.text);
    if (!field.has_value()) {
      return failure("unknown selection keyword '" + token.text + "'",
                     token.offset);
    }
    ++position_;
    if (current().kind != TokenKind::word || keyword("and") ||
        keyword("or")) {
      return failure("selection keyword requires a value", current().offset);
    }
    auto node = std::make_shared<Node>();
    node->kind = NodeKind::predicate;
    node->predicate = Predicate{field.value(), current().text};
    ++position_;
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
