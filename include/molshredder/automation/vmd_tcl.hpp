#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/automation/pml_translator.hpp"
#include "molshredder/automation/tcl.hpp"

// Translation of a conservative subset of VMD Tcl scripts to canonical
// MolShredder operations. Only commands whose documented meaning maps one to
// one are translated; representation lists, rotation conventions and other
// commands without a verified equivalent are reported as unsupported.
// VMD itself is not available as a reference, so every mapping rests on the
// public user's guide.
namespace molshredder::automation::vmd {

using Invocation = pml::Invocation;
using Diagnostic = pml::Diagnostic;

struct TranslationContext {
  std::function<bool(std::string_view)> is_object;
};

struct TranslatedCommand {
  std::size_t line{};
  std::string text;
  // First word after substitution.
  std::string command;
  std::vector<Invocation> invocations;
  std::vector<Diagnostic> diagnostics;
  // exit/quit: later commands must not run.
  bool stop_script{};
  // A Tcl error (substitution or syntax): Tcl would abort the script here.
  bool tcl_error{};

  [[nodiscard]] bool has_error() const;
};

struct Translation {
  std::vector<TranslatedCommand> commands;
  std::size_t translated_commands{};
  std::size_t failed_commands{};
  std::size_t invocation_count{};
  std::size_t comment_lines{};
  bool stopped{};
};

class Translator {
 public:
  explicit Translator(TranslationContext context = {});

  // Translates one parsed command, substituting variables first.
  [[nodiscard]] TranslatedCommand translate(const tcl::Command& command);

  // A syntax error found after the commands already translated.
  [[nodiscard]] static TranslatedCommand syntax_failure(const tcl::SyntaxError& error);

 // Interpreter state; opaque outside the translator.
  struct State;

 private:
  std::shared_ptr<State> state_;
};

[[nodiscard]] Translation translate_script(std::string_view source,
                                           TranslationContext context = {});

}  // namespace molshredder::automation::vmd
