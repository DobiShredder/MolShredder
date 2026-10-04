#include "molshredder/command/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <variant>

namespace molshredder::command {
namespace {

using operation::Error;
using operation::ErrorCode;
using operation::Result;

Error json_error(std::string message) {
  return {ErrorCode::invalid_argument, std::move(message), {}};
}

class Parser {
 public:
  Parser(std::string_view text, JsonLimits limits) : text_{text}, limits_{limits} {}

  Result<Value> run() {
    if (text_.size() > limits_.max_bytes) {
      return Result<Value>::failure(json_error("JSON exceeds the size limit"));
    }
    auto value = parse_value(0U);
    if (!value.has_value()) return value;
    skip_space();
    if (position_ != text_.size()) return fail("unexpected content after the JSON value");
    return value;
  }

 private:
  Result<Value> fail(const std::string& message) const {
    return Result<Value>::failure(
        json_error("invalid JSON at byte " + std::to_string(position_) + ": " + message));
  }

  void skip_space() {
    while (position_ < text_.size() &&
           (text_[position_] == ' ' || text_[position_] == '\t' ||
            text_[position_] == '\n' || text_[position_] == '\r')) {
      ++position_;
    }
  }

  bool consume(std::string_view literal) {
    if (text_.substr(position_, literal.size()) != literal) return false;
    position_ += literal.size();
    return true;
  }

  Result<Value> parse_value(std::size_t depth) {
    if (depth > limits_.max_depth) return fail("nesting is too deep");
    skip_space();
    if (position_ >= text_.size()) return fail("unexpected end of input");
    const char c = text_[position_];
    if (c == '{') return parse_object(depth);
    if (c == '[') return parse_array(depth);
    if (c == '"') {
      auto text = parse_string();
      if (!text.has_value()) return Result<Value>::failure(text.error());
      return Result<Value>::success(Value{std::move(text.value())});
    }
    if (consume("true")) return Result<Value>::success(Value{true});
    if (consume("false")) return Result<Value>::success(Value{false});
    if (consume("null")) return Result<Value>::success(Value{nullptr});
    return parse_number();
  }

  Result<Value> parse_number() {
    const auto start = position_;
    if (position_ < text_.size() && text_[position_] == '-') ++position_;
    if (position_ >= text_.size()) return fail("expected a digit");
    if (text_[position_] == '0') {
      ++position_;
    } else if (text_[position_] >= '1' && text_[position_] <= '9') {
      while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
    } else {
      return fail("unexpected character");
    }
    bool integral = true;
    if (position_ < text_.size() && text_[position_] == '.') {
      integral = false;
      ++position_;
      const auto digits = position_;
      while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
      if (position_ == digits) return fail("expected digits after the decimal point");
    }
    if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
      integral = false;
      ++position_;
      if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_;
      const auto digits = position_;
      while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
      if (position_ == digits) return fail("expected exponent digits");
    }
    const auto token = text_.substr(start, position_ - start);
    if (integral) {
      if (token.front() == '-') {
        std::int64_t value{};
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
        if (parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size()) {
          return Result<Value>::success(Value{value});
        }
      } else {
        std::uint64_t value{};
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
        if (parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size()) {
          if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return Result<Value>::success(Value{static_cast<std::int64_t>(value)});
          }
          return Result<Value>::success(Value{value});
        }
      }
    }
    double value{};
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || !std::isfinite(value)) {
      return fail("number is out of range");
    }
    return Result<Value>::success(Value{value});
  }

  static void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80U) {
      out += static_cast<char>(code);
    } else if (code < 0x800U) {
      out += static_cast<char>(0xC0U | (code >> 6U));
      out += static_cast<char>(0x80U | (code & 0x3FU));
    } else if (code < 0x10000U) {
      out += static_cast<char>(0xE0U | (code >> 12U));
      out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
      out += static_cast<char>(0x80U | (code & 0x3FU));
    } else {
      out += static_cast<char>(0xF0U | (code >> 18U));
      out += static_cast<char>(0x80U | ((code >> 12U) & 0x3FU));
      out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
      out += static_cast<char>(0x80U | (code & 0x3FU));
    }
  }

  std::optional<std::uint32_t> parse_hex4() {
    if (position_ + 4U > text_.size()) return std::nullopt;
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4U; ++i) {
      const char c = text_[position_ + i];
      value <<= 4U;
      if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
      else return std::nullopt;
    }
    position_ += 4U;
    return value;
  }

  Result<std::string> parse_string() {
    ++position_;  // opening quote
    std::string out;
    while (true) {
      if (position_ >= text_.size()) {
        return Result<std::string>::failure(json_error("unterminated string"));
      }
      const auto c = static_cast<unsigned char>(text_[position_]);
      if (c == '"') {
        ++position_;
        return Result<std::string>::success(std::move(out));
      }
      if (c < 0x20U) {
        return Result<std::string>::failure(json_error("control character in string"));
      }
      if (c != '\\') {
        // Validate UTF-8 sequences.
        std::size_t extra = 0;
        if (c < 0x80U) extra = 0;
        else if (c >= 0xC2U && c <= 0xDFU) extra = 1;
        else if (c >= 0xE0U && c <= 0xEFU) extra = 2;
        else if (c >= 0xF0U && c <= 0xF4U) extra = 3;
        else return Result<std::string>::failure(json_error("invalid UTF-8"));
        for (std::size_t i = 1; i <= extra; ++i) {
          if (position_ + i >= text_.size() ||
              (static_cast<unsigned char>(text_[position_ + i]) & 0xC0U) != 0x80U) {
            return Result<std::string>::failure(json_error("invalid UTF-8"));
          }
        }
        if (extra == 2U) {
          const auto second = static_cast<unsigned char>(text_[position_ + 1U]);
          if ((c == 0xE0U && second < 0xA0U) || (c == 0xEDU && second > 0x9FU)) {
            return Result<std::string>::failure(json_error("invalid UTF-8"));
          }
        } else if (extra == 3U) {
          const auto second = static_cast<unsigned char>(text_[position_ + 1U]);
          if ((c == 0xF0U && second < 0x90U) || (c == 0xF4U && second > 0x8FU)) {
            return Result<std::string>::failure(json_error("invalid UTF-8"));
          }
        }
        out.append(text_.substr(position_, extra + 1U));
        position_ += extra + 1U;
        continue;
      }
      ++position_;
      if (position_ >= text_.size()) {
        return Result<std::string>::failure(json_error("unterminated escape"));
      }
      const char escape = text_[position_++];
      switch (escape) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          auto code = parse_hex4();
          if (!code.has_value()) return Result<std::string>::failure(json_error("invalid \\u escape"));
          if (*code >= 0xD800U && *code <= 0xDBFFU) {
            if (!consume("\\u")) return Result<std::string>::failure(json_error("lone surrogate"));
            const auto low = parse_hex4();
            if (!low.has_value() || *low < 0xDC00U || *low > 0xDFFFU) {
              return Result<std::string>::failure(json_error("invalid surrogate pair"));
            }
            code = 0x10000U + ((*code - 0xD800U) << 10U) + (*low - 0xDC00U);
          } else if (*code >= 0xDC00U && *code <= 0xDFFFU) {
            return Result<std::string>::failure(json_error("lone surrogate"));
          }
          append_utf8(out, *code);
          break;
        }
        default:
          return Result<std::string>::failure(json_error("unknown escape"));
      }
    }
  }

  Result<Value> parse_array(std::size_t depth) {
    ++position_;
    Value::Array array;
    skip_space();
    if (position_ < text_.size() && text_[position_] == ']') {
      ++position_;
      return Result<Value>::success(Value{std::move(array)});
    }
    while (true) {
      auto element = parse_value(depth + 1U);
      if (!element.has_value()) return element;
      array.push_back(std::move(element.value()));
      skip_space();
      if (position_ >= text_.size()) return fail("unterminated array");
      if (text_[position_] == ',') {
        ++position_;
        continue;
      }
      if (text_[position_] == ']') {
        ++position_;
        return Result<Value>::success(Value{std::move(array)});
      }
      return fail("expected ',' or ']'");
    }
  }

  Result<Value> parse_object(std::size_t depth) {
    ++position_;
    Value::Object object;
    skip_space();
    if (position_ < text_.size() && text_[position_] == '}') {
      ++position_;
      return Result<Value>::success(Value{std::move(object)});
    }
    while (true) {
      skip_space();
      if (position_ >= text_.size() || text_[position_] != '"') return fail("expected a string key");
      auto key = parse_string();
      if (!key.has_value()) return Result<Value>::failure(key.error());
      skip_space();
      if (position_ >= text_.size() || text_[position_] != ':') return fail("expected ':'");
      ++position_;
      auto element = parse_value(depth + 1U);
      if (!element.has_value()) return element;
      if (!object.emplace(std::move(key.value()), std::move(element.value())).second) {
        return fail("duplicate object key");
      }
      skip_space();
      if (position_ >= text_.size()) return fail("unterminated object");
      if (text_[position_] == ',') {
        ++position_;
        continue;
      }
      if (text_[position_] == '}') {
        ++position_;
        return Result<Value>::success(Value{std::move(object)});
      }
      return fail("expected ',' or '}'");
    }
  }

  std::string_view text_;
  JsonLimits limits_;
  std::size_t position_{};
};

bool valid_utf8(std::string_view text) {
  for (std::size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i]);
    std::size_t extra = 0;
    if (c < 0x80U) extra = 0;
    else if (c >= 0xC2U && c <= 0xDFU) extra = 1;
    else if (c >= 0xE0U && c <= 0xEFU) extra = 2;
    else if (c >= 0xF0U && c <= 0xF4U) extra = 3;
    else return false;
    for (std::size_t k = 1; k <= extra; ++k) {
      if (i + k >= text.size() || (static_cast<unsigned char>(text[i + k]) & 0xC0U) != 0x80U) return false;
    }
    i += extra + 1U;
  }
  return true;
}

Result<std::string> quote(std::string_view text) {
  if (!valid_utf8(text)) return Result<std::string>::failure(json_error("string is not valid UTF-8"));
  std::string out = "\"";
  for (const char raw : text) {
    const auto c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20U) {
          static constexpr char kDigits[] = "0123456789abcdef";
          out += "\\u00";
          out += kDigits[(c >> 4U) & 0xFU];
          out += kDigits[c & 0xFU];
        } else {
          out += raw;
        }
    }
  }
  out += '"';
  return Result<std::string>::success(std::move(out));
}

Result<std::string> render(const Value& value) {
  return std::visit(
      [](const auto& item) -> Result<std::string> {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
          return Result<std::string>::success("null");
        } else if constexpr (std::is_same_v<T, bool>) {
          return Result<std::string>::success(item ? "true" : "false");
        } else if constexpr (std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t>) {
          return Result<std::string>::success(std::to_string(item));
        } else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, Number>) {
          double number{};
          if constexpr (std::is_same_v<T, double>) number = item; else number = item.value;
          if (!std::isfinite(number)) return Result<std::string>::failure(json_error("non-finite number"));
          char buffer[40];
          const auto result = std::to_chars(buffer, buffer + sizeof(buffer), number);
          std::string text{buffer, result.ptr};
          // Keep integral doubles recognizable as non-integers ("3.0", not "3").
          if (text.find_first_of(".eE") == std::string::npos) text += ".0";
          return Result<std::string>::success(std::move(text));
        } else if constexpr (std::is_same_v<T, std::string>) {
          return quote(item);
        } else if constexpr (std::is_same_v<T, Value::Array>) {
          std::string out = "[";
          bool first = true;
          for (const auto& element : item) {
            auto rendered = render(element);
            if (!rendered.has_value()) return rendered;
            if (!first) out += ',';
            first = false;
            out += rendered.value();
          }
          return Result<std::string>::success(out + "]");
        } else {
          std::string out = "{";
          bool first = true;
          for (const auto& [key, element] : item) {
            auto name = quote(key);
            auto rendered = render(element);
            if (!name.has_value()) return name;
            if (!rendered.has_value()) return rendered;
            if (!first) out += ',';
            first = false;
            out += name.value() + ":" + rendered.value();
          }
          return Result<std::string>::success(out + "}");
        }
      },
      value.data);
}

}  // namespace

Result<Value> parse_json(std::string_view text, JsonLimits limits) {
  return Parser{text, limits}.run();
}

Result<std::string> to_json(const Value& value) { return render(value); }

}  // namespace molshredder::command
