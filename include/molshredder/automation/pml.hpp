#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Parser for PyMOL command scripts (PML). It follows the observable parsing
// rules of PyMOL 3.1.0 (argument splitting, comments, continuation, blocks)
// and produces statements only; it never executes anything. Translation to
// MolShredder operations lives in pml_translator.hpp.
namespace molshredder::automation::pml {

// PyMOL parser mode of a command (the values PyMOL reports for its keywords).
enum class ParseMode : int {
  movie = 1,
  abort = 4,
  python_statement = 5,
  embed = 6,
  python_block = 7,
  skip = 8,
  standard = 11,
  path = 12,
  assignment = 13,
  system = 20,
  rest_of_line = 21,
  state_rest_of_line = 22,
};

struct KeywordEntry {
  std::string name;
  int mode{static_cast<int>(ParseMode::standard)};
};

class KeywordTable {
 public:
  // The 317 commands of PyMOL 3.1.0.
  [[nodiscard]] static const KeywordTable& pymol_310();

  KeywordTable() = default;
  explicit KeywordTable(std::vector<KeywordEntry> entries)
      : entries_{std::move(entries)} {}

  void add(std::string name, int mode) {
    entries_.push_back({std::move(name), mode});
  }

  enum class Match { exact, prefix, ambiguous, unknown };
  struct Resolution {
    Match match{Match::unknown};
    const KeywordEntry* entry{};
  };
  // Exact name first, then a unique prefix, as PyMOL accepts abbreviations.
  [[nodiscard]] Resolution resolve(std::string_view name) const;

 private:
  std::vector<KeywordEntry> entries_;
};

struct Diagnostic {
  std::string code;
  std::string message;
  std::size_t line{};
  // "error" statements are not executed, "warning" ones are but lose or
  // ignore something, "info" ones only explain.
  std::string severity{"error"};
};

enum class StatementKind {
  command,
  unknown_command,
  python_statement,
  python_block,
  skip_block,
  embed_block,
  include_script,
  abort,
};

struct Statement {
  std::size_t line{};
  std::string text;
  StatementKind kind{StatementKind::command};
  // Resolved PyMOL command name (abbreviations expanded); empty when unknown.
  std::string command;
  // Positional arguments; std::nullopt marks an empty field (PyMOL's None).
  std::vector<std::optional<std::string>> args;
  std::vector<std::pair<std::string, std::string>> kwargs;
  // Raw lines of a python block (not executed by the parser).
  std::vector<std::string> block_lines;
  std::vector<Diagnostic> diagnostics;
};

struct Script {
  std::vector<Statement> statements;
  std::vector<Diagnostic> diagnostics;
  std::size_t comment_lines{};
  std::size_t blank_lines{};
  bool aborted{};
};

[[nodiscard]] Script parse(std::string_view source,
                           const KeywordTable& keywords =
                               KeywordTable::pymol_310());

[[nodiscard]] std::string_view to_string(StatementKind kind) noexcept;

}  // namespace molshredder::automation::pml
