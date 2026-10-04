#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/operation/result.hpp"

// A small Tcl front end for the VMD script adapter: word splitting, variable
// and command substitution, and a tiny interpreter with `set`, `unset` and
// `list`. It follows the observable behavior of Tcl 8.5 (the language VMD
// scripts are written in) and has no control flow or arithmetic: anything
// else is a registered command or an error.
namespace molshredder::automation::tcl {

enum class WordKind { bare, braced, quoted };

struct Word {
  WordKind kind{WordKind::bare};
  // Raw text: the content between braces or quotes, or the bare word.
  std::string text;
  std::size_t line{};
};

struct Command {
  std::size_t line{};
  std::vector<Word> words;
  std::string text;
};

struct SyntaxError {
  std::string code;
  std::string message;
  std::size_t line{};
};

struct Script {
  // Commands before the first syntax error, which Tcl would already have run.
  std::vector<Command> commands;
  std::optional<SyntaxError> error;
  std::size_t comment_lines{};
};

[[nodiscard]] Script parse(std::string_view source);

// Tcl list formatting: braces or backslashes as Tcl would quote each element.
[[nodiscard]] std::string format_list(const std::vector<std::string>& elements);

class Interpreter {
 public:
  using Handler = std::function<operation::Result<std::string>(
      Interpreter&, const std::vector<std::string>& words)>;

  void register_command(std::string name, Handler handler);
  // Called for command names that are neither builtin nor registered; the
  // flag tells whether the command runs inside a [...] substitution.
  using UnknownHandler = std::function<operation::Result<std::string>(
      Interpreter&, const std::vector<std::string>& words, bool nested)>;
  void set_unknown_handler(UnknownHandler handler) {
    unknown_ = std::move(handler);
  }

  [[nodiscard]] operation::Result<std::string> substitute(const Word& word);
  // Substitutes the words and runs the command (builtins first).
  [[nodiscard]] operation::Result<std::string> evaluate(const Command& command);
  // Runs a command whose words were already substituted.
  [[nodiscard]] operation::Result<std::string> run_words(
      const std::vector<std::string>& words);
  [[nodiscard]] operation::Result<std::string> evaluate_script(
      std::string_view script);

  [[nodiscard]] bool has_variable(const std::string& name) const {
    return variables_.count(name) != 0U;
  }
  void set_variable(std::string name, std::string value) {
    variables_[std::move(name)] = std::move(value);
  }

 private:
  [[nodiscard]] operation::Result<std::string> substitute_text(
      std::string_view text);

  std::map<std::string, std::string> variables_;
  std::map<std::string, Handler> commands_;
  UnknownHandler unknown_;
  std::size_t depth_{};
};

}  // namespace molshredder::automation::tcl
