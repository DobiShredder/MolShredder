#include "molshredder/automation/pml.hpp"

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace molshredder::automation::pml {
namespace {

bool is_space(char c) noexcept { return c == ' ' || c == '\t' || c == '\r'; }

std::string_view trim(std::string_view text) noexcept {
  while (!text.empty() && is_space(text.front())) text.remove_prefix(1U);
  while (!text.empty() && is_space(text.back())) text.remove_suffix(1U);
  return text;
}

bool is_identifier_start(char c) noexcept {
  return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}
bool is_identifier_part(char c) noexcept {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

struct PhysicalLine {
  std::size_t number{};
  std::string text;
};

std::vector<PhysicalLine> split_lines(std::string_view source) {
  std::vector<PhysicalLine> lines;
  std::size_t start = 0;
  std::size_t number = 1;
  for (std::size_t i = 0; i <= source.size(); ++i) {
    const bool end = i == source.size();
    if (!end && source[i] != '\n' && source[i] != '\r') continue;
    if (end && start == source.size()) break;
    lines.push_back({number++, std::string{source.substr(start, i - start)}});
    if (!end && source[i] == '\r' && i + 1 < source.size() &&
        source[i + 1] == '\n') {
      ++i;
    }
    start = i + 1;
  }
  return lines;
}

// Position of the character that closes the group opened at `open`, honoring
// nesting of the same bracket and quoted text. npos when the group is open.
std::size_t matching_close(std::string_view text, std::size_t open) {
  const char opener = text[open];
  const char closer = opener == '(' ? ')' : ']';
  std::size_t depth = 0;
  for (std::size_t i = open; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '"' || c == '\'') {
      std::size_t j = i + 1;
      while (j < text.size() && text[j] != c) {
        if (text[j] == '\\' && j + 1 < text.size()) ++j;
        ++j;
      }
      if (j < text.size()) i = j;
      continue;
    }
    if (c == opener) {
      ++depth;
    } else if (c == closer) {
      if (--depth == 0U) return i;
    }
  }
  return std::string_view::npos;
}

struct SplitResult {
  std::vector<std::string_view> pieces;
  bool unbalanced_quote{};
};

// Splits `text` at delimiter characters that are outside quotes and balanced
// (...)/[...] groups. A quote that opens in the middle of a word and never
// closes makes the whole text invalid; one that starts a token is literal.
SplitResult split_top_level(std::string_view text, char delimiter) {
  SplitResult result;
  std::size_t piece_start = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '"' || c == '\'') {
      std::size_t j = i + 1;
      while (j < text.size() && text[j] != c) {
        if (text[j] == '\\' && j + 1 < text.size()) ++j;
        ++j;
      }
      if (j < text.size()) {
        i = j;
        continue;
      }
      const bool starts_token =
          i == 0U || is_space(text[i - 1U]) ||
          std::string_view{"=(,[;"}.find(text[i - 1U]) !=
              std::string_view::npos;
      if (!starts_token) {
        result.unbalanced_quote = true;
        return result;
      }
      continue;
    }
    if (c == '(' || c == '[') {
      const auto close = matching_close(text, i);
      if (close != std::string_view::npos) i = close;
      continue;
    }
    if (c == delimiter) {
      result.pieces.push_back(text.substr(piece_start, i - piece_start));
      piece_start = i + 1;
    }
  }
  result.pieces.push_back(text.substr(piece_start));
  return result;
}

struct KeywordField {
  std::string name;
  std::string value;
};

// `identifier = value` with optional blanks around `=`.
std::optional<KeywordField> as_keyword(std::string_view field) {
  field = trim(field);
  if (field.empty() || !is_identifier_start(field.front())) {
    return std::nullopt;
  }
  std::size_t i = 1;
  while (i < field.size() && is_identifier_part(field[i])) ++i;
  const auto name_end = i;
  while (i < field.size() && is_space(field[i])) ++i;
  if (i >= field.size() || field[i] != '=') return std::nullopt;
  return KeywordField{std::string{field.substr(0, name_end)},
                      std::string{trim(field.substr(i + 1U))}};
}

bool is_block_end(std::string_view line, std::string_view word) {
  line = trim(line);
  if (line.substr(0, word.size()) != word) return false;
  line.remove_prefix(word.size());
  if (line.empty() || !is_space(line.front())) return false;
  return trim(line) == "end";
}

struct ParsedArguments {
  std::vector<std::optional<std::string>> args;
  std::vector<std::pair<std::string, std::string>> kwargs;
  std::optional<Diagnostic> error;
};

ParsedArguments parse_standard_arguments(std::string_view text,
                                         std::size_t line) {
  ParsedArguments parsed;
  if (trim(text).empty()) return parsed;
  const auto split = split_top_level(text, ',');
  if (split.unbalanced_quote) {
    parsed.error = Diagnostic{"unbalanced_quote",
                              "a quote opens inside a word and never closes",
                              line};
    return parsed;
  }
  auto fields = split.pieces;
  if (fields.size() > 1U && trim(fields.back()).empty()) fields.pop_back();
  for (const auto field : fields) {
    if (auto keyword = as_keyword(field)) {
      if (keyword->value.empty()) {
        parsed.error = Diagnostic{
            "empty_keyword_value",
            "keyword argument '" + keyword->name + "' has no value", line};
        return parsed;
      }
      parsed.kwargs.emplace_back(std::move(keyword->name),
                                 std::move(keyword->value));
      continue;
    }
    const auto value = trim(field);
    if (value.empty()) {
      parsed.args.emplace_back(std::nullopt);
    } else {
      parsed.args.emplace_back(std::string{value});
    }
  }
  return parsed;
}

// Modes 21: the first argument ends at the first top-level comma and the rest
// of the line is one argument (label, alter, iterate, alias, set_key).
ParsedArguments parse_rest_of_line_arguments(std::string_view text,
                                             std::size_t line) {
  ParsedArguments parsed;
  if (trim(text).empty()) return parsed;
  const auto split = split_top_level(text, ',');
  if (split.unbalanced_quote) {
    parsed.error = Diagnostic{"unbalanced_quote",
                              "a quote opens inside a word and never closes",
                              line};
    return parsed;
  }
  const auto first = split.pieces.front();
  std::string rest;
  if (split.pieces.size() > 1U) {
    const auto offset =
        static_cast<std::size_t>(split.pieces[1].data() - text.data());
    rest = std::string{trim(text.substr(offset))};
  }
  if (auto keyword = as_keyword(first)) {
    if (keyword->value.empty()) {
      parsed.error = Diagnostic{
          "empty_keyword_value",
          "keyword argument '" + keyword->name + "' has no value", line};
      return parsed;
    }
    parsed.kwargs.emplace_back(std::move(keyword->name),
                               std::move(keyword->value));
  } else {
    parsed.args.emplace_back(std::string{trim(first)});
  }
  parsed.args.emplace_back(std::move(rest));
  return parsed;
}

void add_diagnostic(Script& script, std::string code, std::string message,
                    std::size_t line) {
  script.diagnostics.push_back({std::move(code), std::move(message), line});
}

}  // namespace

const KeywordTable& KeywordTable::pymol_310() {
  static const KeywordTable table{std::vector<KeywordEntry>{
#include "pml_keywords.inc"
  }};
  return table;
}

KeywordTable::Resolution KeywordTable::resolve(std::string_view name) const {
  if (name.empty()) return {};
  const KeywordEntry* prefix_match = nullptr;
  std::size_t prefix_count = 0;
  for (const auto& entry : entries_) {
    if (entry.name == name) return {Match::exact, &entry};
    if (entry.name.size() > name.size() &&
        entry.name.compare(0, name.size(), name) == 0) {
      prefix_match = &entry;
      ++prefix_count;
    }
  }
  if (prefix_count == 1U) return {Match::prefix, prefix_match};
  if (prefix_count > 1U) return {Match::ambiguous, nullptr};
  return {};
}

std::string_view to_string(StatementKind kind) noexcept {
  switch (kind) {
    case StatementKind::command: return "command";
    case StatementKind::unknown_command: return "unknown_command";
    case StatementKind::python_statement: return "python_statement";
    case StatementKind::python_block: return "python_block";
    case StatementKind::skip_block: return "skip_block";
    case StatementKind::embed_block: return "embed_block";
    case StatementKind::include_script: return "include_script";
    case StatementKind::abort: return "abort";
  }
  return "command";
}

Script parse(std::string_view source, const KeywordTable& keywords) {
  Script script;
  const auto lines = split_lines(source);
  std::size_t index = 0;
  while (index < lines.size() && !script.aborted) {
    const std::size_t line_number = lines[index].number;
    std::string logical = lines[index].text;
    ++index;
    // A trailing backslash joins the next physical line.
    while (true) {
      const auto trimmed = trim(logical);
      if (trimmed.empty() || trimmed.back() != '\\') break;
      logical = std::string{trimmed.substr(0, trimmed.size() - 1U)};
      if (index >= lines.size()) {
        script.diagnostics.push_back(
            {"continuation_at_end", "the last line ends with a backslash",
             line_number, "warning"});
        break;
      }
      logical += lines[index].text;
      ++index;
    }
    const auto text = trim(logical);
    if (text.empty()) {
      ++script.blank_lines;
      continue;
    }
    if (text.front() == '#') {
      ++script.comment_lines;
      continue;
    }
    if (text.front() == '/') {
      Statement statement;
      statement.line = line_number;
      statement.text = std::string{text};
      statement.kind = StatementKind::python_statement;
      statement.diagnostics.push_back(
          {"python_statement_requires_trust",
           "a '/' line runs a Python statement", line_number});
      script.statements.push_back(std::move(statement));
      continue;
    }
    if (text.front() == '@') {
      Statement statement;
      statement.line = line_number;
      statement.text = std::string{text};
      statement.kind = StatementKind::include_script;
      statement.command = "@";
      statement.args.emplace_back(std::string{trim(text.substr(1U))});
      script.statements.push_back(std::move(statement));
      continue;
    }

    const auto parts = split_top_level(text, ';');
    if (parts.unbalanced_quote) {
      add_diagnostic(script, "unbalanced_quote",
                     "a quote opens inside a word and never closes",
                     line_number);
      continue;
    }
    for (const auto raw_part : parts.pieces) {
      const auto part = trim(raw_part);
      if (part.empty() || part.front() == '#') continue;
      const auto name_end = part.find(' ');
      const auto name = part.substr(0, name_end);
      const auto rest = name_end == std::string_view::npos
                            ? std::string_view{}
                            : part.substr(name_end + 1U);
      Statement statement;
      statement.line = line_number;
      statement.text = std::string{part};
      const auto resolved = keywords.resolve(name);
      if (resolved.match == KeywordTable::Match::unknown ||
          resolved.match == KeywordTable::Match::ambiguous) {
        statement.kind = StatementKind::unknown_command;
        statement.command = std::string{name};
        statement.diagnostics.push_back(
            {resolved.match == KeywordTable::Match::ambiguous
                 ? "ambiguous_command"
                 : "unknown_command",
             "no unique PyMOL command named '" + std::string{name} + "'",
             line_number});
        script.statements.push_back(std::move(statement));
        continue;
      }
      statement.command = resolved.entry->name;
      const auto mode = static_cast<ParseMode>(resolved.entry->mode);
      if (mode == ParseMode::python_statement) {
        statement.kind = StatementKind::python_statement;
        statement.diagnostics.push_back(
            {"python_statement_requires_trust",
             "'" + statement.command + "' runs a Python statement",
             line_number});
        script.statements.push_back(std::move(statement));
        continue;
      }
      if (mode == ParseMode::abort) {
        statement.kind = StatementKind::abort;
        script.statements.push_back(std::move(statement));
        script.aborted = true;
        break;
      }
      if (mode == ParseMode::python_block && trim(rest).empty()) {
        statement.kind = StatementKind::python_block;
        bool terminated = false;
        while (index < lines.size()) {
          const auto& block_line = lines[index];
          ++index;
          if (is_block_end(block_line.text, statement.command)) {
            terminated = true;
            break;
          }
          statement.block_lines.push_back(block_line.text);
        }
        statement.diagnostics.push_back(
            {terminated ? "python_block_requires_trust"
                        : "unterminated_python_block",
             terminated ? "a python block runs arbitrary Python code"
                        : "the python block has no 'python end' and is "
                          "never run",
             line_number});
        script.statements.push_back(std::move(statement));
        continue;
      }
      if (mode == ParseMode::skip && trim(rest).empty()) {
        statement.kind = StatementKind::skip_block;
        bool terminated = false;
        while (index < lines.size()) {
          const auto& block_line = lines[index];
          ++index;
          if (is_block_end(block_line.text, statement.command)) {
            terminated = true;
            break;
          }
        }
        if (!terminated) {
          statement.diagnostics.push_back(
              {"unterminated_skip_block",
               "the skip block has no 'skip end'; the rest of the file is "
               "skipped",
               line_number});
        }
        script.statements.push_back(std::move(statement));
        continue;
      }
      if (mode == ParseMode::embed) {
        statement.kind = StatementKind::embed_block;
        std::size_t end_index = index;
        while (end_index < lines.size() &&
               !is_block_end(lines[end_index].text, statement.command)) {
          ++end_index;
        }
        if (end_index < lines.size()) {
          statement.diagnostics.push_back(
              {"embed_block_skipped",
               "embedded data is not stored; its lines are not run as "
               "commands",
               line_number, "warning"});
          index = end_index + 1U;
        } else {
          statement.diagnostics.push_back(
              {"unterminated_embed_block",
               "the embed block has no 'embed end'", line_number});
        }
        script.statements.push_back(std::move(statement));
        continue;
      }
      auto parsed = mode == ParseMode::rest_of_line
                        ? parse_rest_of_line_arguments(rest, line_number)
                        : parse_standard_arguments(rest, line_number);
      if (parsed.error.has_value()) {
        script.diagnostics.push_back(std::move(*parsed.error));
        continue;
      }
      statement.kind = StatementKind::command;
      statement.args = std::move(parsed.args);
      statement.kwargs = std::move(parsed.kwargs);
      script.statements.push_back(std::move(statement));
    }
  }
  return script;
}

}  // namespace molshredder::automation::pml
