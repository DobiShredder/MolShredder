#include "molshredder/application/label_expression.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "molshredder/core/parse_number.hpp"
#include "molshredder/selection/evaluator.hpp"

namespace molshredder::application {
namespace {

operation::Error label_error(std::string message) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message),
                          "use atom fields such as name, resn, resi, chain, "
                          "elem, b, q, ID with + - * / % and str()"};
}

struct None {};
struct Value;
using Tuple = std::shared_ptr<const std::vector<Value>>;
struct Value {
  std::variant<None, bool, std::int64_t, double, std::string, Tuple> data;
};

using ValueResult = operation::Result<Value>;

enum class TokenKind { number, string, name, symbol, end };

struct Token {
  TokenKind kind{TokenKind::end};
  std::string text;
  std::size_t offset{};
};

operation::Result<std::vector<Token>> tokenize(std::string_view source) {
  using R = operation::Result<std::vector<Token>>;
  std::vector<Token> tokens;
  std::size_t position = 0U;
  const auto is_digit = [](char item) { return item >= '0' && item <= '9'; };
  const auto is_name = [](char item) {
    return (item >= 'a' && item <= 'z') || (item >= 'A' && item <= 'Z') ||
           item == '_' || (item >= '0' && item <= '9');
  };
  while (position < source.size()) {
    const auto item = source[position];
    if (item == ' ' || item == '\t') {
      ++position;
      continue;
    }
    const auto start = position;
    if (is_digit(item) ||
        (item == '.' && position + 1U < source.size() &&
         is_digit(source[position + 1U]))) {
      while (position < source.size() &&
             (is_digit(source[position]) || source[position] == '.'))
        ++position;
      if (position < source.size() &&
          (source[position] == 'e' || source[position] == 'E')) {
        auto next = position + 1U;
        if (next < source.size() && (source[next] == '+' || source[next] == '-'))
          ++next;
        if (next < source.size() && is_digit(source[next])) {
          position = next;
          while (position < source.size() && is_digit(source[position]))
            ++position;
        }
      }
      tokens.push_back({TokenKind::number,
                        std::string{source.substr(start, position - start)},
                        start});
      continue;
    }
    if (item == '\'' || item == '"') {
      ++position;
      std::string text;
      bool closed = false;
      while (position < source.size()) {
        const auto current = source[position++];
        if (current == item) {
          closed = true;
          break;
        }
        if (current == '\\' && position < source.size()) {
          const auto escaped = source[position++];
          text.push_back(escaped == 'n'   ? '\n'
                         : escaped == 't' ? '\t'
                                          : escaped);
          continue;
        }
        text.push_back(current);
      }
      if (!closed)
        return R::failure(label_error("unterminated string literal"));
      tokens.push_back({TokenKind::string, std::move(text), start});
      continue;
    }
    if (is_name(item)) {
      while (position < source.size() && is_name(source[position]))
        ++position;
      tokens.push_back({TokenKind::name,
                        std::string{source.substr(start, position - start)},
                        start});
      continue;
    }
    if (item == '/' && position + 1U < source.size() &&
        source[position + 1U] == '/') {
      tokens.push_back({TokenKind::symbol, "//", start});
      position += 2U;
      continue;
    }
    if (item == '*' && position + 1U < source.size() &&
        source[position + 1U] == '*') {
      tokens.push_back({TokenKind::symbol, "**", start});
      position += 2U;
      continue;
    }
    if (std::string_view{"+-*/%(),"}.find(item) != std::string_view::npos) {
      tokens.push_back({TokenKind::symbol, std::string(1U, item), start});
      ++position;
      continue;
    }
    return R::failure(label_error("invalid syntax at offset " +
                                  std::to_string(position)));
  }
  tokens.push_back({TokenKind::end, {}, source.size()});
  return R::success(std::move(tokens));
}

} // namespace

struct LabelExpression::Node {
  enum class Kind { literal, name, unary, binary, call, tuple };
  Kind kind{Kind::literal};
  Value literal;
  std::string text;
  std::vector<std::shared_ptr<const Node>> children;
};

namespace {

using Node = LabelExpression::Node;
using NodePtr = std::shared_ptr<const Node>;
using NodeResult = operation::Result<NodePtr>;

class Parser {
public:
  explicit Parser(std::vector<Token> tokens) : tokens_{std::move(tokens)} {}

  NodeResult parse() {
    auto root = sum();
    if (!root.has_value())
      return root;
    if (current().kind != TokenKind::end)
      return NodeResult::failure(
          label_error("invalid syntax near '" + current().text + "'"));
    return root;
  }

private:
  const Token &current() const { return tokens_[position_]; }
  bool symbol(std::string_view text) const {
    return current().kind == TokenKind::symbol && current().text == text;
  }

  static NodePtr make(Node::Kind kind, std::string text,
                      std::vector<NodePtr> children = {}) {
    auto node = std::make_shared<Node>();
    node->kind = kind;
    node->text = std::move(text);
    node->children = std::move(children);
    return node;
  }

  NodeResult sum() {
    auto left = product();
    while (left.has_value() && (symbol("+") || symbol("-"))) {
      const auto op = current().text;
      ++position_;
      auto right = product();
      if (!right.has_value())
        return right;
      left = NodeResult::success(
          make(Node::Kind::binary, op, {left.value(), right.value()}));
    }
    return left;
  }

  NodeResult product() {
    auto left = unary();
    while (left.has_value() &&
           (symbol("*") || symbol("/") || symbol("//") || symbol("%"))) {
      const auto op = current().text;
      ++position_;
      auto right = unary();
      if (!right.has_value())
        return right;
      left = NodeResult::success(
          make(Node::Kind::binary, op, {left.value(), right.value()}));
    }
    return left;
  }

  NodeResult unary() {
    if (symbol("-") || symbol("+")) {
      const auto op = current().text;
      ++position_;
      auto operand = unary();
      if (!operand.has_value())
        return operand;
      return NodeResult::success(make(Node::Kind::unary, op, {operand.value()}));
    }
    auto base = primary();
    if (base.has_value() && symbol("**")) {
      ++position_;
      auto exponent = unary();
      if (!exponent.has_value())
        return exponent;
      return NodeResult::success(
          make(Node::Kind::binary, "**", {base.value(), exponent.value()}));
    }
    return base;
  }

  NodeResult primary() {
    const auto token = current();
    if (token.kind == TokenKind::number) {
      ++position_;
      auto node = std::make_shared<Node>();
      node->kind = Node::Kind::literal;
      const auto is_float =
          token.text.find_first_of(".eE") != std::string::npos;
      if (is_float) {
        double value{};
        const auto parsed = core::from_chars(
            token.text.data(), token.text.data() + token.text.size(), value);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != token.text.data() + token.text.size())
          return NodeResult::failure(label_error("invalid number " + token.text));
        node->literal.data = value;
      } else {
        std::int64_t value{};
        const auto parsed = std::from_chars(
            token.text.data(), token.text.data() + token.text.size(), value);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != token.text.data() + token.text.size())
          return NodeResult::failure(label_error("invalid number " + token.text));
        node->literal.data = value;
      }
      return NodeResult::success(node);
    }
    if (token.kind == TokenKind::string) {
      ++position_;
      auto node = std::make_shared<Node>();
      node->kind = Node::Kind::literal;
      node->literal.data = token.text;
      // Adjacent string literals concatenate as in Python.
      while (current().kind == TokenKind::string) {
        std::get<std::string>(node->literal.data) += current().text;
        ++position_;
      }
      return NodeResult::success(node);
    }
    if (token.kind == TokenKind::name) {
      ++position_;
      if (symbol("(")) {
        ++position_;
        std::vector<NodePtr> arguments;
        if (!symbol(")")) {
          while (true) {
            auto argument = sum();
            if (!argument.has_value())
              return argument;
            arguments.push_back(argument.value());
            if (symbol(",")) {
              ++position_;
              continue;
            }
            break;
          }
        }
        if (!symbol(")"))
          return NodeResult::failure(label_error("missing ')' in call"));
        ++position_;
        return NodeResult::success(
            make(Node::Kind::call, token.text, std::move(arguments)));
      }
      return NodeResult::success(make(Node::Kind::name, token.text));
    }
    if (symbol("(")) {
      ++position_;
      std::vector<NodePtr> items;
      bool tuple = false;
      if (symbol(")")) {
        ++position_;
        return NodeResult::success(make(Node::Kind::tuple, {}, {}));
      }
      while (true) {
        auto item = sum();
        if (!item.has_value())
          return item;
        items.push_back(item.value());
        if (symbol(",")) {
          tuple = true;
          ++position_;
          if (symbol(")"))
            break;
          continue;
        }
        break;
      }
      if (!symbol(")"))
        return NodeResult::failure(label_error("missing ')'"));
      ++position_;
      if (!tuple)
        return NodeResult::success(items.front());
      return NodeResult::success(make(Node::Kind::tuple, {}, std::move(items)));
    }
    return NodeResult::failure(label_error(
        token.kind == TokenKind::end ? "unexpected end of label expression"
                                     : "invalid syntax near '" + token.text + "'"));
  }

  std::vector<Token> tokens_;
  std::size_t position_{};
};

std::string type_name(const Value &value) {
  switch (value.data.index()) {
  case 0: return "NoneType";
  case 1: return "bool";
  case 2: return "int";
  case 3: return "float";
  case 4: return "str";
  default: return "tuple";
  }
}

bool is_number(const Value &value) {
  return std::holds_alternative<bool>(value.data) ||
         std::holds_alternative<std::int64_t>(value.data) ||
         std::holds_alternative<double>(value.data);
}

bool is_integral(const Value &value) {
  return std::holds_alternative<bool>(value.data) ||
         std::holds_alternative<std::int64_t>(value.data);
}

std::int64_t as_int(const Value &value) {
  if (const auto *flag = std::get_if<bool>(&value.data))
    return *flag ? 1 : 0;
  if (const auto *integer = std::get_if<std::int64_t>(&value.data))
    return *integer;
  return static_cast<std::int64_t>(std::get<double>(value.data));
}

double as_float(const Value &value) {
  if (const auto *number = std::get_if<double>(&value.data))
    return *number;
  return static_cast<double>(as_int(value));
}

std::string python_repr(const Value &value);

std::string python_str(const Value &value) {
  return std::visit(
      [](const auto &item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, None>)
          return "None";
        else if constexpr (std::is_same_v<T, bool>)
          return item ? "True" : "False";
        else if constexpr (std::is_same_v<T, std::int64_t>)
          return std::to_string(item);
        else if constexpr (std::is_same_v<T, double>)
          return python_float_repr(item);
        else if constexpr (std::is_same_v<T, std::string>)
          return item;
        else {
          std::string text = "(";
          for (std::size_t index = 0; index < item->size(); ++index) {
            if (index != 0U)
              text += ", ";
            text += python_repr((*item)[index]);
          }
          if (item->size() == 1U)
            text += ',';
          return text + ")";
        }
      },
      value.data);
}

std::string python_repr(const Value &value) {
  if (const auto *text = std::get_if<std::string>(&value.data)) {
    const auto quote =
        text->find('\'') != std::string::npos &&
                text->find('"') == std::string::npos
            ? '"'
            : '\'';
    std::string result(1U, quote);
    for (const auto item : *text) {
      if (item == '\\' || item == quote)
        result.push_back('\\');
      result.push_back(item);
    }
    result.push_back(quote);
    return result;
  }
  return python_str(value);
}

operation::Result<std::string> percent_format(const std::string &format,
                                              const Value &arguments) {
  using R = operation::Result<std::string>;
  std::vector<Value> values;
  if (const auto *tuple = std::get_if<Tuple>(&arguments.data))
    values = **tuple;
  else
    values.push_back(arguments);
  std::size_t next = 0U;
  std::string output;
  for (std::size_t position = 0; position < format.size(); ++position) {
    if (format[position] != '%') {
      output.push_back(format[position]);
      continue;
    }
    ++position;
    std::string flags;
    while (position < format.size() &&
           std::string_view{"-+ #0"}.find(format[position]) !=
               std::string_view::npos)
      flags.push_back(format[position++]);
    std::string width;
    while (position < format.size() && format[position] >= '0' &&
           format[position] <= '9')
      width.push_back(format[position++]);
    std::string precision;
    bool has_precision = false;
    if (position < format.size() && format[position] == '.') {
      has_precision = true;
      ++position;
      while (position < format.size() && format[position] >= '0' &&
             format[position] <= '9')
        precision.push_back(format[position++]);
    }
    while (position < format.size() &&
           std::string_view{"hlL"}.find(format[position]) != std::string_view::npos)
      ++position;
    if (position >= format.size())
      return R::failure(label_error("incomplete format"));
    const auto conversion = format[position];
    if (conversion == '%') {
      output.push_back('%');
      continue;
    }
    if (next >= values.size())
      return R::failure(label_error("not enough arguments for format string"));
    const auto &value = values[next++];
    const auto spec = "%" + flags + width +
                      (has_precision ? "." + (precision.empty() ? "0" : precision)
                                     : std::string{});
    std::array<char, 512> buffer{};
    if (conversion == 's' || conversion == 'r' || conversion == 'a') {
      auto text = conversion == 's' ? python_str(value) : python_repr(value);
      std::snprintf(buffer.data(), buffer.size(), (spec + "s").c_str(),
                    text.c_str());
      output += buffer.data();
    } else if (std::string_view{"diuoxX"}.find(conversion) !=
               std::string_view::npos) {
      if (!is_number(value))
        return R::failure(label_error(std::string{"%"} + conversion +
                                      " format: a real number is required, not " +
                                      type_name(value)));
      const auto letter = conversion == 'i' || conversion == 'u' ? 'd' : conversion;
      std::snprintf(buffer.data(), buffer.size(),
                    (spec + "ll" + letter).c_str(),
                    static_cast<long long>(as_int(value)));
      output += buffer.data();
    } else if (std::string_view{"eEfFgG"}.find(conversion) !=
               std::string_view::npos) {
      if (!is_number(value))
        return R::failure(label_error(std::string{"must be real number, not "} +
                                      type_name(value)));
      std::snprintf(buffer.data(), buffer.size(),
                    (spec + conversion).c_str(), as_float(value));
      output += buffer.data();
    } else if (conversion == 'c') {
      if (const auto *text = std::get_if<std::string>(&value.data);
          text != nullptr && text->size() == 1U)
        output += *text;
      else if (is_integral(value))
        output.push_back(static_cast<char>(as_int(value)));
      else
        return R::failure(label_error("%c requires int or char"));
    } else {
      return R::failure(label_error(std::string{"unsupported format character '"} +
                                    conversion + "'"));
    }
  }
  if (next < values.size())
    return R::failure(
        label_error("not all arguments converted during string formatting"));
  return R::success(std::move(output));
}

double python_floor_mod(double left, double right) {
  auto result = std::fmod(left, right);
  if (result != 0.0 && ((result < 0.0) != (right < 0.0)))
    result += right;
  return result;
}

std::int64_t python_floor_div(std::int64_t left, std::int64_t right) {
  auto quotient = left / right;
  if ((left % right != 0) && ((left < 0) != (right < 0)))
    --quotient;
  return quotient;
}

ValueResult binary(const std::string &op, const Value &left,
                   const Value &right) {
  const auto unsupported = [&] {
    return ValueResult::failure(label_error(
        "unsupported operand type(s) for " + op + ": '" + type_name(left) +
        "' and '" + type_name(right) + "'"));
  };
  const auto *left_text = std::get_if<std::string>(&left.data);
  const auto *right_text = std::get_if<std::string>(&right.data);
  if (op == "%" && left_text != nullptr) {
    auto formatted = percent_format(*left_text, right);
    if (!formatted.has_value())
      return ValueResult::failure(formatted.error());
    return ValueResult::success(Value{formatted.value()});
  }
  if (op == "+" && left_text != nullptr && right_text != nullptr)
    return ValueResult::success(Value{*left_text + *right_text});
  if (op == "*" && ((left_text != nullptr && is_integral(right)) ||
                    (right_text != nullptr && is_integral(left)))) {
    const auto &text = left_text != nullptr ? *left_text : *right_text;
    const auto count = as_int(left_text != nullptr ? right : left);
    std::string repeated;
    for (std::int64_t index = 0; index < count; ++index)
      repeated += text;
    return ValueResult::success(Value{repeated});
  }
  if (!is_number(left) || !is_number(right))
    return unsupported();
  const auto integral = is_integral(left) && is_integral(right);
  if (op == "/") {
    if (as_float(right) == 0.0)
      return ValueResult::failure(label_error("division by zero"));
    return ValueResult::success(Value{as_float(left) / as_float(right)});
  }
  if (integral) {
    const auto a = as_int(left);
    const auto b = as_int(right);
    if (op == "+")
      return ValueResult::success(Value{a + b});
    if (op == "-")
      return ValueResult::success(Value{a - b});
    if (op == "*")
      return ValueResult::success(Value{a * b});
    if (op == "//" || op == "%") {
      if (b == 0)
        return ValueResult::failure(label_error("integer division or modulo by zero"));
      return ValueResult::success(
          Value{op == "//" ? python_floor_div(a, b) : a - python_floor_div(a, b) * b});
    }
    if (op == "**") {
      if (b < 0)
        return ValueResult::success(
            Value{std::pow(static_cast<double>(a), static_cast<double>(b))});
      std::int64_t power = 1;
      for (std::int64_t index = 0; index < b; ++index)
        power *= a;
      return ValueResult::success(Value{power});
    }
    return unsupported();
  }
  const auto a = as_float(left);
  const auto b = as_float(right);
  if (op == "+")
    return ValueResult::success(Value{a + b});
  if (op == "-")
    return ValueResult::success(Value{a - b});
  if (op == "*")
    return ValueResult::success(Value{a * b});
  if (op == "//" || op == "%") {
    if (b == 0.0)
      return ValueResult::failure(label_error("float divmod by zero"));
    return ValueResult::success(
        Value{op == "//" ? std::floor(a / b) : python_floor_mod(a, b)});
  }
  if (op == "**")
    return ValueResult::success(Value{std::pow(a, b)});
  return unsupported();
}

std::optional<double> numeric_property(const LabelExpression::AtomContext &context,
                                       std::string_view name,
                                       std::size_t atom) {
  const model::AtomProperty *property = nullptr;
  const model::AtomProperty *presence = nullptr;
  if (context.frame != nullptr) {
    const auto &frame_properties = context.frame->metadata().atom_properties;
    if (const auto found = frame_properties.find(std::string{name});
        found != frame_properties.end()) {
      if (!context.frame->atom_present(atom))
        return std::nullopt;
      property = &found->second;
      if (const auto flags = frame_properties.find(std::string{name} + "_present");
          flags != frame_properties.end())
        presence = &flags->second;
    }
  }
  const auto &topology = *context.topology;
  if (property == nullptr) {
    property = topology.properties().find(name);
    presence = topology.properties().find(std::string{name} + "_present");
  }
  if (property == nullptr)
    return std::nullopt;
  if (presence != nullptr) {
    if (const auto *flags = std::get_if<model::BooleanColumn>(&presence->values);
        flags != nullptr && flags->values[atom] == 0U)
      return std::nullopt;
  }
  return std::visit(
      [atom](const auto &values) -> std::optional<double> {
        using T = std::decay_t<decltype(values)>;
        if constexpr (std::is_same_v<T, model::BooleanColumn>)
          return static_cast<double>(values.values[atom]);
        else if constexpr (std::is_same_v<T, std::vector<std::string>>)
          return std::nullopt;
        else
          return static_cast<double>(values[atom]);
      },
      property->values);
}

double float32(double value) {
  return static_cast<double>(static_cast<float>(value));
}

ValueResult field(const LabelExpression::AtomContext &context, std::size_t atom,
                  const std::string &name) {
  const auto &topology = *context.topology;
  const auto &record = topology.atoms()[atom];
  const auto &residue = topology.residues()[record.residue.value];
  if (name == "name")
    return ValueResult::success(Value{record.name});
  if (name == "resn")
    return ValueResult::success(Value{residue.name});
  if (name == "resi")
    return ValueResult::success(
        Value{std::to_string(residue.sequence_number) + residue.insertion_code});
  if (name == "resv")
    return ValueResult::success(Value{residue.sequence_number});
  if (name == "chain")
    return ValueResult::success(Value{residue.chain_id});
  if (name == "segi")
    return ValueResult::success(Value{residue.segment_id});
  if (name == "alt")
    return ValueResult::success(Value{record.alternate_location});
  if (name == "elem")
    return ValueResult::success(
        Value{selection::atom_element_symbol(record.atomic_number)});
  if (name == "type") {
    const auto hetero = numeric_property(context, "pdb.is_hetero", atom);
    return ValueResult::success(
        Value{std::string{hetero.value_or(0.0) != 0.0 ? "HETATM" : "ATOM"}});
  }
  if (name == "model")
    return ValueResult::success(Value{std::string{context.object_name}});
  if (name == "ss") {
    // PDB HELIX/SHEET codes ("" without a record), as in spectrum.
    const auto *property = topology.properties().find("ss");
    const auto *codes = property == nullptr
                            ? nullptr
                            : std::get_if<std::vector<std::string>>(&property->values);
    return ValueResult::success(Value{
        codes != nullptr && atom < codes->size() ? (*codes)[atom] : std::string{}});
  }
  if (name == "text_type")
    return ValueResult::success(Value{std::string{}});
  if (name == "vdw") {
    const auto radii = selection::atom_vdw_radii(topology);
    if (!radii.has_value())
      return ValueResult::failure(radii.error());
    return ValueResult::success(Value{float32(radii.value()[atom])});
  }
  if (name == "b") {
    auto value = numeric_property(context, "b_factor", atom);
    if (!value.has_value())
      value = numeric_property(context, "b_iso_or_equiv", atom);
    return ValueResult::success(Value{float32(value.value_or(0.0))});
  }
  if (name == "q")
    return ValueResult::success(Value{
        float32(numeric_property(context, "occupancy", atom).value_or(1.0))});
  if (name == "partial_charge")
    return ValueResult::success(Value{float32(
        numeric_property(context, "partial_charge", atom).value_or(0.0))});
  if (name == "formal_charge")
    return ValueResult::success(
        Value{static_cast<std::int64_t>(record.formal_charge)});
  if (name == "ID")
    return ValueResult::success(Value{
        record.source_serial.value_or(static_cast<std::int64_t>(atom + 1U))});
  if (name == "index")
    return ValueResult::success(Value{static_cast<std::int64_t>(atom + 1U)});
  if (name == "rank")
    return ValueResult::success(Value{static_cast<std::int64_t>(atom)});
  if (name == "None")
    return ValueResult::success(Value{None{}});
  if (name == "True" || name == "False")
    return ValueResult::success(Value{name == "True"});
  return ValueResult::failure(
      label_error("name '" + name + "' is not defined"));
}

ValueResult evaluate(const Node &node, const LabelExpression::AtomContext &context,
                     std::size_t atom) {
  switch (node.kind) {
  case Node::Kind::literal:
    return ValueResult::success(node.literal);
  case Node::Kind::name:
    return field(context, atom, node.text);
  case Node::Kind::unary: {
    auto operand = evaluate(*node.children.front(), context, atom);
    if (!operand.has_value())
      return operand;
    if (!is_number(operand.value()))
      return ValueResult::failure(label_error(
          "bad operand type for unary " + node.text + ": '" +
          type_name(operand.value()) + "'"));
    if (node.text == "+")
      return ValueResult::success(
          is_integral(operand.value()) ? Value{as_int(operand.value())}
                                       : operand.value());
    return ValueResult::success(
        is_integral(operand.value()) ? Value{-as_int(operand.value())}
                                     : Value{-as_float(operand.value())});
  }
  case Node::Kind::binary: {
    auto left = evaluate(*node.children[0], context, atom);
    if (!left.has_value())
      return left;
    auto right = evaluate(*node.children[1], context, atom);
    if (!right.has_value())
      return right;
    return binary(node.text, left.value(), right.value());
  }
  case Node::Kind::tuple: {
    auto items = std::make_shared<std::vector<Value>>();
    for (const auto &child : node.children) {
      auto item = evaluate(*child, context, atom);
      if (!item.has_value())
        return item;
      items->push_back(item.value());
    }
    return ValueResult::success(Value{Tuple{items}});
  }
  case Node::Kind::call: {
    std::vector<Value> arguments;
    for (const auto &child : node.children) {
      auto item = evaluate(*child, context, atom);
      if (!item.has_value())
        return item;
      arguments.push_back(item.value());
    }
    const auto &name = node.text;
    if (arguments.size() != 1U && !(name == "round" && arguments.size() == 2U))
      return ValueResult::failure(label_error(name + "() takes one argument"));
    const auto &value = arguments.front();
    if (name == "str")
      return ValueResult::success(Value{python_str(value)});
    if (name == "len") {
      if (const auto *text = std::get_if<std::string>(&value.data))
        return ValueResult::success(Value{static_cast<std::int64_t>(text->size())});
      return ValueResult::failure(label_error("object has no len()"));
    }
    if (name == "int" || name == "float" || name == "abs" || name == "round") {
      Value number = value;
      if (const auto *text = std::get_if<std::string>(&value.data)) {
        double parsed{};
        const auto result =
            core::from_chars(text->data(), text->data() + text->size(), parsed);
        if (result.ec != std::errc{} || result.ptr != text->data() + text->size())
          return ValueResult::failure(label_error(
              "invalid literal for " + name + "(): '" + *text + "'"));
        number = Value{parsed};
      }
      if (!is_number(number))
        return ValueResult::failure(label_error(name + "() argument must be a number"));
      if (name == "int")
        return ValueResult::success(Value{as_int(number)});
      if (name == "float")
        return ValueResult::success(Value{as_float(number)});
      if (name == "abs")
        return ValueResult::success(
            is_integral(number) ? Value{std::llabs(as_int(number))}
                                : Value{std::fabs(as_float(number))});
      if (arguments.size() == 2U) {
        const auto scale = std::pow(10.0, as_float(arguments[1]));
        return ValueResult::success(
            Value{std::nearbyint(as_float(number) * scale) / scale});
      }
      return ValueResult::success(
          Value{static_cast<std::int64_t>(std::nearbyint(as_float(number)))});
    }
    return ValueResult::failure(label_error("name '" + name + "' is not defined"));
  }
  }
  return ValueResult::failure(label_error("invalid label expression"));
}

} // namespace

std::string python_float_repr(double value) {
  if (std::isnan(value))
    return "nan";
  if (std::isinf(value))
    return value < 0.0 ? "-inf" : "inf";
  std::array<char, 64> buffer{};
  const auto converted =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                    std::chars_format::scientific);
  std::string scientific{buffer.data(), converted.ptr};
  const auto e = scientific.find('e');
  auto mantissa = scientific.substr(0U, e);
  const auto exponent = std::stoi(scientific.substr(e + 1U));
  const auto negative = !mantissa.empty() && mantissa.front() == '-';
  if (negative)
    mantissa.erase(0U, 1U);
  std::string digits;
  for (const auto item : mantissa)
    if (item != '.')
      digits.push_back(item);
  std::string result;
  if (exponent >= -4 && exponent < 16) {
    if (exponent < 0) {
      result = "0." + std::string(static_cast<std::size_t>(-exponent - 1), '0') +
               digits;
    } else {
      const auto integer_digits = static_cast<std::size_t>(exponent) + 1U;
      if (digits.size() <= integer_digits) {
        result = digits + std::string(integer_digits - digits.size(), '0') + ".0";
      } else {
        result = digits.substr(0U, integer_digits) + "." +
                 digits.substr(integer_digits);
      }
    }
  } else {
    result = digits.substr(0U, 1U);
    if (digits.size() > 1U)
      result += "." + digits.substr(1U);
    std::array<char, 16> exponent_text{};
    std::snprintf(exponent_text.data(), exponent_text.size(), "e%c%02d",
                  exponent < 0 ? '-' : '+', std::abs(exponent));
    result += exponent_text.data();
  }
  return negative ? "-" + result : result;
}

operation::Result<LabelExpression>
LabelExpression::parse(std::string_view source) {
  using R = operation::Result<LabelExpression>;
  auto tokens = tokenize(source);
  if (!tokens.has_value())
    return R::failure(tokens.error());
  LabelExpression expression;
  expression.source_ = std::string{source};
  if (tokens.value().size() == 1U) {
    // An empty expression clears labels, like PyMOL label SELECTION, ''.
    auto node = std::make_shared<Node>();
    node->literal.data = std::string{};
    expression.root_ = node;
    return R::success(std::move(expression));
  }
  Parser parser{std::move(tokens.value())};
  auto root = parser.parse();
  if (!root.has_value())
    return R::failure(root.error());
  expression.root_ = root.value();
  return R::success(std::move(expression));
}

operation::Result<std::string>
LabelExpression::evaluate(const AtomContext &context, std::size_t atom) const {
  using R = operation::Result<std::string>;
  if (context.topology == nullptr || atom >= context.topology->atom_count())
    return R::failure(label_error("label atom is out of range"));
  auto value = molshredder::application::evaluate(*root_, context, atom);
  if (!value.has_value())
    return R::failure(value.error());
  if (const auto *number = std::get_if<double>(&value.value().data)) {
    const auto digits = std::clamp<std::int64_t>(context.digits, 0, 10);
    std::array<char, 512> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.*f",
                  static_cast<int>(digits), *number);
    return R::success(std::string{buffer.data()});
  }
  return R::success(python_str(value.value()));
}

} // namespace molshredder::application
