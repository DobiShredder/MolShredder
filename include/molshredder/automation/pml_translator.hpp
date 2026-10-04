#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/automation/pml.hpp"

// Translation of parsed PyMOL statements to canonical MolShredder
// invocations. Nothing here runs a command: the result is the list of
// `invoke` calls a frontend would issue, plus diagnostics for everything that
// could not be translated or loses information.
namespace molshredder::automation::pml {

struct Invocation {
  std::string command;
  std::map<std::string, std::string, std::less<>> arguments;

  friend bool operator==(const Invocation&, const Invocation&) = default;
};

// Names known outside the script (the live Workspace when running). Empty
// callables mean "unknown". Names created by earlier statements of the same
// script are tracked by the translator itself.
struct TranslationContext {
  std::function<bool(std::string_view)> is_object;
  std::function<bool(std::string_view)> is_selection;
  std::function<std::vector<std::string>()> list_objects;
};

struct TranslatedStatement {
  std::size_t line{};
  std::string text;
  StatementKind kind{StatementKind::command};
  std::string command;
  std::vector<Invocation> invocations;
  std::vector<Diagnostic> diagnostics;
  // quit/abort: later statements must not run.
  bool stop_script{};

  [[nodiscard]] bool has_error() const;
};

struct Translation {
  std::vector<TranslatedStatement> statements;
  std::vector<Diagnostic> parse_diagnostics;
  std::size_t translated_statements{};
  std::size_t failed_statements{};
  std::size_t invocation_count{};
  // quit or abort ended the script; later statements are not translated.
  bool stopped{};
};

class Translator {
 public:
  explicit Translator(TranslationContext context = {})
      : context_{std::move(context)} {}

  [[nodiscard]] TranslatedStatement translate(const Statement& statement);

 private:
  [[nodiscard]] bool is_object(std::string_view name) const;
  [[nodiscard]] bool is_selection(std::string_view name) const;
  [[nodiscard]] std::vector<std::string> known_objects() const;

  friend class StatementTranslation;
  TranslationContext context_;
  std::set<std::string, std::less<>> objects_;
  std::set<std::string, std::less<>> selections_;
  std::set<std::string, std::less<>> deleted_objects_;
};

[[nodiscard]] Translation translate_script(const Script& script,
                                           TranslationContext context = {});

}  // namespace molshredder::automation::pml
