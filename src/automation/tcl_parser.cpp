#include "molshredder/automation/tcl.hpp"

#include <cctype>
#include <string>
#include <utility>

namespace molshredder::automation::tcl {
namespace {

using operation::Error;
using operation::ErrorCode;
using operation::Result;

Error tcl_error(std::string message) {
  return {ErrorCode::script_failed, std::move(message), {}};
}

bool is_blank(char c) noexcept { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }

// Index just past the brace group opened at `open`, or npos.
std::size_t skip_braces(std::string_view text, std::size_t open) {
  std::size_t depth = 0;
  for (std::size_t i = open; i < text.size(); ++i) {
    if (text[i] == '\\') {
      ++i;
    } else if (text[i] == '{') {
      ++depth;
    } else if (text[i] == '}') {
      if (--depth == 0U) return i + 1U;
    }
  }
  return std::string_view::npos;
}

// Index just past the bracket group opened at `open`, or npos.
std::size_t skip_brackets(std::string_view text, std::size_t open) {
  std::size_t depth = 0;
  for (std::size_t i = open; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\\') {
      ++i;
    } else if (c == '{') {
      const auto end = skip_braces(text, i);
      if (end == std::string_view::npos) return end;
      i = end - 1U;
    } else if (c == '[') {
      ++depth;
    } else if (c == ']') {
      if (--depth == 0U) return i + 1U;
    }
  }
  return std::string_view::npos;
}

// Backslash-newline plus following blanks becomes one space (inside braces).
std::string collapse_continuations(std::string_view raw) {
  std::string out;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '\\' && i + 1U < raw.size() && raw[i + 1U] == '\n') {
      i += 1U;
      while (i + 1U < raw.size() && (raw[i + 1U] == ' ' || raw[i + 1U] == '\t')) ++i;
      out += ' ';
    } else if (raw[i] == '\\' && i + 1U < raw.size()) {
      out += raw[i];
      out += raw[++i];
    } else {
      out += raw[i];
    }
  }
  return out;
}

void append_utf8(std::string& out, unsigned long code) {
  if (code < 0x80UL) {
    out += static_cast<char>(code);
  } else if (code < 0x800UL) {
    out += static_cast<char>(0xC0UL | (code >> 6U));
    out += static_cast<char>(0x80UL | (code & 0x3FUL));
  } else {
    out += static_cast<char>(0xE0UL | (code >> 12U));
    out += static_cast<char>(0x80UL | ((code >> 6U) & 0x3FUL));
    out += static_cast<char>(0x80UL | (code & 0x3FUL));
  }
}

int hex_value(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool is_name_char(char c) noexcept {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

}  // namespace

Script parse(std::string_view source) {
  Script script;
  std::size_t i = 0;
  std::size_t line = 1;
  const auto advance = [&](std::size_t to) {
    for (; i < to; ++i) {
      if (source[i] == '\n') ++line;
    }
  };
  const auto fail = [&](std::string code, std::string message) {
    script.error = SyntaxError{std::move(code), std::move(message), line};
  };
  while (i < source.size()) {
    // Between commands: blanks, newlines, semicolons and comments.
    if (is_blank(source[i]) || source[i] == '\n' || source[i] == ';') {
      advance(i + 1U);
      continue;
    }
    if (source[i] == '\\' && i + 1U < source.size() && source[i + 1U] == '\n') {
      advance(i + 2U);
      continue;
    }
    if (source[i] == '#') {
      ++script.comment_lines;
      while (i < source.size() && source[i] != '\n') {
        if (source[i] == '\\' && i + 1U < source.size()) {
          advance(i + 2U);
        } else {
          ++i;
        }
      }
      continue;
    }
    Command command;
    command.line = line;
    const auto command_start = i;
    bool ended = false;
    while (i < source.size() && !ended) {
      while (i < source.size() &&
             (is_blank(source[i]) ||
              (source[i] == '\\' && i + 1U < source.size() && source[i + 1U] == '\n'))) {
        advance(source[i] == '\\' ? i + 2U : i + 1U);
      }
      if (i >= source.size()) break;
      if (source[i] == '\n' || source[i] == ';') {
        ended = true;
        break;
      }
      Word word;
      word.line = line;
      const auto separator_follows = [&](std::size_t at) {
        return at >= source.size() || is_blank(source[at]) || source[at] == '\n' ||
               source[at] == ';' ||
               (source[at] == '\\' && at + 1U < source.size() && source[at + 1U] == '\n');
      };
      if (source[i] == '{') {
        const auto end = skip_braces(source, i);
        if (end == std::string_view::npos) {
          fail("missing_close_brace", "missing close-brace");
          return script;
        }
        if (!separator_follows(end)) {
          fail("extra_characters_after_close_brace",
               "extra characters after close-brace");
          return script;
        }
        word.kind = WordKind::braced;
        word.text = collapse_continuations(source.substr(i + 1U, end - i - 2U));
        advance(end);
      } else if (source[i] == '"') {
        std::size_t j = i + 1U;
        bool closed = false;
        while (j < source.size()) {
          if (source[j] == '\\') {
            j += 2U;
          } else if (source[j] == '[') {
            const auto end = skip_brackets(source, j);
            if (end == std::string_view::npos) {
              fail("missing_close_bracket", "missing close-bracket");
              return script;
            }
            j = end;
          } else if (source[j] == '"') {
            closed = true;
            break;
          } else {
            ++j;
          }
        }
        if (!closed) {
          fail("missing_close_quote", "missing \"");
          return script;
        }
        if (!separator_follows(j + 1U)) {
          fail("extra_characters_after_close_quote",
               "extra characters after close-quote");
          return script;
        }
        word.kind = WordKind::quoted;
        word.text = std::string{source.substr(i + 1U, j - i - 1U)};
        advance(j + 1U);
      } else {
        std::size_t j = i;
        while (j < source.size() && !separator_follows(j)) {
          if (source[j] == '\\') {
            j += 2U;
          } else if (source[j] == '[') {
            const auto end = skip_brackets(source, j);
            if (end == std::string_view::npos) {
              fail("missing_close_bracket", "missing close-bracket");
              return script;
            }
            j = end;
          } else {
            ++j;
          }
        }
        j = std::min(j, source.size());
        word.kind = WordKind::bare;
        word.text = std::string{source.substr(i, j - i)};
        advance(j);
      }
      command.words.push_back(std::move(word));
    }
    command.text = std::string{source.substr(command_start, i - command_start)};
    if (!command.words.empty()) script.commands.push_back(std::move(command));
  }
  return script;
}

std::string format_list(const std::vector<std::string>& elements) {
  std::string out;
  for (std::size_t n = 0; n < elements.size(); ++n) {
    const auto& element = elements[n];
    if (n != 0U) out += ' ';
    if (element.empty()) {
      out += "{}";
      continue;
    }
    bool needs_quoting = element.front() == '#' && n == 0U;
    bool balanced = true;
    long depth = 0;
    for (std::size_t i = 0; i < element.size(); ++i) {
      const char c = element[i];
      if (is_blank(c) || c == '\n' || c == ';' || c == '$' || c == '[' || c == ']' ||
          c == '"' || c == '{' || c == '}' || c == '\\') {
        needs_quoting = true;
      }
      if (c == '\\') {
        if (i + 1U >= element.size()) balanced = false;
        ++i;
      } else if (c == '{') {
        ++depth;
      } else if (c == '}') {
        if (--depth < 0) balanced = false;
      }
    }
    if (depth != 0) balanced = false;
    if (!needs_quoting) {
      out += element;
    } else if (balanced) {
      out += '{' + element + '}';
    } else {
      for (const char c : element) {
        if (is_blank(c) || c == ';' || c == '$' || c == '[' || c == ']' || c == '"' ||
            c == '{' || c == '}' || c == '\\') {
          out += '\\';
        }
        if (c == '\n') {
          out += "\\n";
        } else {
          out += c;
        }
      }
    }
  }
  return out;
}

void Interpreter::register_command(std::string name, Handler handler) {
  commands_[std::move(name)] = std::move(handler);
}

Result<std::string> Interpreter::substitute_text(std::string_view text) {
  std::string out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\\') {
      if (i + 1U >= text.size()) {
        out += '\\';
        continue;
      }
      const char next = text[++i];
      switch (next) {
        case 'a': out += '\a'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'v': out += '\v'; break;
        case '\n':
          while (i + 1U < text.size() && (text[i + 1U] == ' ' || text[i + 1U] == '\t')) ++i;
          out += ' ';
          break;
        case 'x': {
          unsigned long value = 0;
          std::size_t digits = 0;
          while (digits < 2U && i + 1U < text.size() && hex_value(text[i + 1U]) >= 0) {
            value = value * 16U + static_cast<unsigned long>(hex_value(text[++i]));
            ++digits;
          }
          if (digits == 0U) {
            out += 'x';
          } else {
            append_utf8(out, value);
          }
          break;
        }
        case 'u': {
          unsigned long value = 0;
          std::size_t digits = 0;
          while (digits < 4U && i + 1U < text.size() && hex_value(text[i + 1U]) >= 0) {
            value = value * 16U + static_cast<unsigned long>(hex_value(text[++i]));
            ++digits;
          }
          if (digits == 0U) {
            out += 'u';
          } else {
            append_utf8(out, value);
          }
          break;
        }
        default:
          if (next >= '0' && next <= '7') {
            unsigned long value = static_cast<unsigned long>(next - '0');
            std::size_t digits = 1;
            while (digits < 3U && i + 1U < text.size() && text[i + 1U] >= '0' &&
                   text[i + 1U] <= '7') {
              value = value * 8U + static_cast<unsigned long>(text[++i] - '0');
              ++digits;
            }
            append_utf8(out, value & 0xFFUL);
          } else {
            out += next;
          }
      }
    } else if (c == '$') {
      std::string name;
      std::size_t j = i + 1U;
      if (j < text.size() && text[j] == '{') {
        const auto close = text.find('}', j);
        if (close == std::string_view::npos) return Result<std::string>::failure(tcl_error("missing close-brace for variable name"));
        name = std::string{text.substr(j + 1U, close - j - 1U)};
        j = close + 1U;
      } else {
        while (j < text.size() && (is_name_char(text[j]) ||
                                   (text[j] == ':' && j + 1U < text.size() && text[j + 1U] == ':'))) {
          if (text[j] == ':') ++j;
          name += text[j++];
        }
        if (name.empty()) {
          out += '$';
          continue;
        }
        if (j < text.size() && text[j] == '(') {
          const auto close = text.find(')', j);
          if (close != std::string_view::npos) {
            const auto index = substitute_text(text.substr(j + 1U, close - j - 1U));
            if (!index.has_value()) return index;
            name += '(' + index.value() + ')';
            j = close + 1U;
          }
        }
      }
      const auto found = variables_.find(name);
      if (found == variables_.end()) {
        return Result<std::string>::failure(
            tcl_error("can't read \"" + name + "\": no such variable"));
      }
      out += found->second;
      i = j - 1U;
    } else if (c == '[') {
      const auto end = skip_brackets(text, i);
      if (end == std::string_view::npos) {
        return Result<std::string>::failure(tcl_error("missing close-bracket"));
      }
      ++depth_;
      const auto result = evaluate_script(text.substr(i + 1U, end - i - 2U));
      --depth_;
      if (!result.has_value()) return result;
      out += result.value();
      i = end - 1U;
    } else {
      out += c;
    }
  }
  return Result<std::string>::success(std::move(out));
}

Result<std::string> Interpreter::substitute(const Word& word) {
  if (word.kind == WordKind::braced) return Result<std::string>::success(word.text);
  return substitute_text(word.text);
}

Result<std::string> Interpreter::evaluate(const Command& command) {
  std::vector<std::string> words;
  words.reserve(command.words.size());
  for (const auto& word : command.words) {
    auto value = substitute(word);
    if (!value.has_value()) return value;
    words.push_back(std::move(value.value()));
  }
  return run_words(words);
}

Result<std::string> Interpreter::run_words(const std::vector<std::string>& words) {
  const auto& name = words.front();
  if (name == "set") {
    if (words.size() == 2U) {
      const auto found = variables_.find(words[1]);
      if (found == variables_.end()) {
        return Result<std::string>::failure(
            tcl_error("can't read \"" + words[1] + "\": no such variable"));
      }
      return Result<std::string>::success(found->second);
    }
    if (words.size() == 3U) {
      variables_[words[1]] = words[2];
      return Result<std::string>::success(words[2]);
    }
    return Result<std::string>::failure(
        tcl_error("wrong # args: should be \"set varName ?newValue?\""));
  }
  if (name == "unset") {
    for (std::size_t i = 1; i < words.size(); ++i) {
      if (variables_.erase(words[i]) == 0U) {
        return Result<std::string>::failure(
            tcl_error("can't unset \"" + words[i] + "\": no such variable"));
      }
    }
    return Result<std::string>::success({});
  }
  if (name == "list") {
    return Result<std::string>::success(
        format_list({words.begin() + 1, words.end()}));
  }
  const auto found = commands_.find(name);
  if (found == commands_.end()) {
    if (unknown_) return unknown_(*this, words, depth_ > 0U);
    return Result<std::string>::failure(
        tcl_error("invalid command name \"" + name + "\""));
  }
  return found->second(*this, words);
}

Result<std::string> Interpreter::evaluate_script(std::string_view script) {
  const auto parsed = parse(script);
  std::string result;
  for (const auto& command : parsed.commands) {
    auto value = evaluate(command);
    if (!value.has_value()) return value;
    result = std::move(value.value());
  }
  if (parsed.error.has_value()) {
    return Result<std::string>::failure(tcl_error(parsed.error->message));
  }
  return Result<std::string>::success(std::move(result));
}

}  // namespace molshredder::automation::tcl
