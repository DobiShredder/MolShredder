#include "molshredder/automation/vmd_tcl.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <utility>

namespace molshredder::automation::vmd {
namespace {

using operation::Error;
using operation::ErrorCode;
using operation::Result;
using Arguments = std::map<std::string, std::string, std::less<>>;

std::string lower(std::string_view text) {
  std::string out{text};
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::optional<double> number(std::string_view text) {
  if (text.empty()) return std::nullopt;
  char* end = nullptr;
  const std::string owned{text};
  const double value = std::strtod(owned.c_str(), &end);
  if (end == owned.c_str() || *end != '\0' || !std::isfinite(value)) return std::nullopt;
  return value;
}

std::string format_number(double value) {
  if (value == std::floor(value) && std::fabs(value) < 1.0e15) {
    return std::to_string(static_cast<long long>(value));
  }
  char buffer[40];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string{buffer, result.ptr};
}

enum class Kind { structure, trajectory, volume };
struct FileType {
  Kind kind;
  std::string native;
};

// VMD file type names (its plugin names) and extensions.
const std::map<std::string, FileType, std::less<>>& type_table() {
  static const std::map<std::string, FileType, std::less<>> table{
      {"pdb", {Kind::structure, "pdb"}},     {"psf", {Kind::structure, "psf"}},
      {"mol2", {Kind::structure, "mol2"}},   {"xyz", {Kind::structure, "xyz"}},
      {"gro", {Kind::structure, "gro"}},     {"g96", {Kind::structure, "g96"}},
      {"pqr", {Kind::structure, "pqr"}},     {"parm7", {Kind::structure, "prmtop"}},
      {"dcd", {Kind::trajectory, "dcd"}},    {"trr", {Kind::trajectory, "trr"}},
      {"xtc", {Kind::trajectory, "xtc"}},    {"binpos", {Kind::trajectory, "binpos"}},
      {"crd", {Kind::trajectory, "mdcrd"}},  {"crdbox", {Kind::trajectory, "crdbox"}},
      {"netcdf", {Kind::trajectory, "netcdf"}}, {"lammpstrj", {Kind::trajectory, "lammps"}},
      {"rst7", {Kind::trajectory, "rst7"}},  {"ccp4", {Kind::volume, "ccp4"}},
      {"dx", {Kind::volume, "dx"}},
  };
  return table;
}

std::string extension_type(std::string_view path) {
  auto extension = lower(std::filesystem::path{std::string{path}}.extension().string());
  if (!extension.empty()) extension.erase(0U, 1U);
  static const std::map<std::string, std::string, std::less<>> aliases{
      {"ent", "pdb"}, {"prmtop", "parm7"}, {"nc", "netcdf"}, {"mdcrd", "crd"},
      {"mrc", "ccp4"}, {"map", "ccp4"}};
  const auto found = aliases.find(extension);
  return found == aliases.end() ? extension : found->second;
}

const std::set<std::string, std::less<>>& control_flow() {
  static const std::set<std::string, std::less<>> names{
      "if", "for", "foreach", "while", "proc", "switch", "catch", "eval", "uplevel",
      "return", "break", "continue", "namespace", "upvar", "global", "variable"};
  return names;
}

const std::set<std::string, std::less<>>& code_execution() {
  static const std::set<std::string, std::less<>> names{
      "exec", "source", "open", "socket", "load", "package", "interp", "file", "glob",
      "cd", "pwd", "fconfigure", "gets", "read", "puts_to_file", "rename", "after"};
  return names;
}

const std::map<std::string, std::string, std::less<>>& unsupported_molecule_reasons() {
  static const std::map<std::string, std::string, std::less<>> table{
      {"representation", "representation lists have no one-to-one native mapping; use show, hide and color"},
      {"color", "representation lists have no one-to-one native mapping; use show, hide and color"},
      {"selection", "representation lists have no one-to-one native mapping; use show, hide and color"},
      {"material", "representation lists have no one-to-one native mapping; use show, hide and color"},
      {"addrep", "representation lists have no one-to-one native mapping; use show, hide and color"},
      {"delrep", "representation lists have no one-to-one native mapping; use show, hide and color"},
      {"modstyle", "representation lists have no one-to-one native mapping; use show, hide and color"},
      {"modcolor", "representation lists have no one-to-one native mapping; use show, hide and color"},
      {"modselect", "representation lists have no one-to-one native mapping; use show, hide and color"},
      {"modmaterial", "representation lists have no one-to-one native mapping; use show, hide and color"},
      {"showrep", "representation lists have no one-to-one native mapping; use show, hide and color"},
      {"smoothrep", "representation lists have no one-to-one native mapping; use show, hide and color"},
      {"drawframes", "representation lists have no one-to-one native mapping; use show, hide and color"},
      {"on", "native object visibility is addressed by object id"},
      {"off", "native object visibility is addressed by object id"},
      {"top", "native object activation is addressed by object id"},
      {"fix", "per-molecule fixed flags have no native equivalent"},
      {"free", "per-molecule fixed flags have no native equivalent"},
  };
  return table;
}

}  // namespace

bool TranslatedCommand::has_error() const {
  return std::any_of(diagnostics.begin(), diagnostics.end(),
                     [](const Diagnostic& d) { return d.severity == "error"; });
}

struct Translator::State {
  explicit State(TranslationContext ctx) : context{std::move(ctx)} {}

  TranslationContext context;
  tcl::Interpreter interpreter;
  TranslatedCommand* current{};
  // molid -> native object name (empty after deletion).
  std::vector<std::string> molecules;
  std::set<std::string, std::less<>> object_names;
  // atomselect handle -> selection text.
  std::map<std::string, std::string> selections;
  std::size_t next_selection{};

  void add(std::string code, std::string message, std::string severity) {
    current->diagnostics.push_back(
        {std::move(code), std::move(message), current->line, std::move(severity)});
  }
  void error(std::string code, std::string message) { add(std::move(code), std::move(message), "error"); }
  void warning(std::string code, std::string message) { add(std::move(code), std::move(message), "warning"); }
  void info(std::string code, std::string message) { add(std::move(code), std::move(message), "info"); }
  void emit(std::string command, Arguments arguments) {
    current->invocations.push_back({std::move(command), std::move(arguments)});
  }
  bool is_object(std::string_view name) const {
    return object_names.count(name) != 0U || (context.is_object && context.is_object(name));
  }
};

namespace {

using State = Translator::State;
using Words = std::vector<std::string>;

Result<std::string> ok(std::string value = {}) { return Result<std::string>::success(std::move(value)); }

// Options after the file of `mol new`/`mol addfile`: key value pairs.
struct FileOptions {
  std::string type;
  std::string molid;
  bool valid{true};
};

FileOptions parse_file_options(State& state, const Words& words, std::size_t start) {
  FileOptions options;
  for (std::size_t i = start; i < words.size(); i += 2U) {
    const auto key = lower(words[i]);
    if (i + 1U >= words.size()) {
      state.error("wrong_arguments", "option '" + key + "' needs a value");
      options.valid = false;
      return options;
    }
    const auto& value = words[i + 1U];
    if (key == "type") {
      options.type = lower(value);
    } else if (key == "molid") {
      options.molid = value;
    } else if (key == "waitfor") {
      // Loading is synchronous here; any waitfor value gives the same result.
    } else if (key == "first" || key == "step") {
      if (number(value) != std::optional<double>{key == "first" ? 0.0 : 1.0}) {
        state.error("unsupported_argument", "'" + key + " " + value + "' is not supported");
        options.valid = false;
      }
    } else if (key == "last") {
      if (number(value) != std::optional<double>{-1.0}) {
        state.error("unsupported_argument", "'last " + value + "' is not supported");
        options.valid = false;
      }
    } else if (key == "filebonds" || key == "autobonds") {
      const auto flag = lower(value);
      if (flag != "1" && flag != "on" && flag != "yes" && flag != "true") {
        state.error("unsupported_argument", "'" + key + " " + value + "' is not supported");
        options.valid = false;
      }
    } else {
      state.error("unsupported_argument", "option '" + key + "' is not supported");
      options.valid = false;
    }
  }
  return options;
}

std::string unique_name(State& state, const std::string& stem) {
  std::string name = stem;
  for (std::size_t n = 2; state.is_object(name); ++n) name = stem + "_" + std::to_string(n);
  if (name != stem) {
    state.info("renamed_object", "'" + stem + "' is already an object; used '" + name + "'");
  }
  return name;
}

Result<std::string> mol_new(State& state, const Words& words, bool loader_form) {
  // `mol new FILE ?options?` or the older `mol load TYPE FILE`.
  std::string path;
  std::string type;
  FileOptions options;
  if (loader_form) {
    if (words.size() < 4U) {
      state.error("wrong_arguments", "mol load needs a file type and a file");
      return ok();
    }
    type = lower(words[2]);
    path = words[3];
    options = parse_file_options(state, words, 4U);
  } else {
    if (words.size() < 3U) {
      state.error("wrong_arguments", "mol new needs a file");
      return ok();
    }
    path = words[2];
    options = parse_file_options(state, words, 3U);
    type = options.type;
  }
  if (!options.valid) return ok();
  if (type.empty()) type = extension_type(path);
  const auto found = type_table().find(type);
  if (found == type_table().end()) {
    state.error("unsupported_format", "no native reader for VMD file type '" + type + "'");
    return ok();
  }
  const auto stem = std::filesystem::path{path}.stem().string();
  const auto name = unique_name(state, stem);
  // Always explicit: the native default comes from the file content.
  Arguments args{{"path", path}, {"file-format", found->second.native}, {"name", name}};
  switch (found->second.kind) {
    case Kind::structure: state.emit("load", std::move(args)); break;
    case Kind::volume: state.emit("volume load", std::move(args)); break;
    case Kind::trajectory:
      state.error("unsupported_format",
                  "'mol new' of a trajectory file creates an atom-less molecule; load a structure first and use mol addfile");
      return ok();
  }
  state.object_names.insert(name);
  state.molecules.push_back(name);
  return ok(std::to_string(state.molecules.size() - 1U));
}

Result<std::string> mol_addfile(State& state, const Words& words) {
  if (words.size() < 3U) {
    state.error("wrong_arguments", "mol addfile needs a file");
    return ok();
  }
  const auto options = parse_file_options(state, words, 3U);
  if (!options.valid) return ok();
  const auto type = options.type.empty() ? extension_type(words[2]) : options.type;
  const auto found = type_table().find(type);
  if (found == type_table().end() || found->second.kind != Kind::trajectory) {
    state.error("unsupported_format", "no native trajectory reader for VMD file type '" + type + "'");
    return ok();
  }
  state.emit("traj load", {{"path", words[2]}, {"file-format", found->second.native}, {"mapping", "index"}});
  if (!options.molid.empty() && options.molid != "top") {
    state.warning("applied_to_active_object",
                  "the trajectory is attached to the active object, not to molecule " + options.molid);
  }
  return ok();
}

std::optional<std::string> molecule_name(State& state, const std::string& id) {
  if (id == "top") {
    for (auto it = state.molecules.rbegin(); it != state.molecules.rend(); ++it) {
      if (!it->empty()) return *it;
    }
    return std::nullopt;
  }
  const auto value = number(id);
  if (!value.has_value() || *value < 0.0 || *value != std::floor(*value) ||
      static_cast<std::size_t>(*value) >= state.molecules.size() ||
      state.molecules[static_cast<std::size_t>(*value)].empty()) {
    return std::nullopt;
  }
  return state.molecules[static_cast<std::size_t>(*value)];
}

Result<std::string> mol_delete(State& state, const Words& words) {
  if (words.size() != 3U) {
    state.error("wrong_arguments", "mol delete needs one molecule id, 'top' or 'all'");
    return ok();
  }
  if (words[2] == "all") {
    for (auto& name : state.molecules) {
      if (name.empty()) continue;
      state.emit("object delete", {{"object", name}});
      state.object_names.erase(name);
      name.clear();
    }
    return ok();
  }
  const auto name = molecule_name(state, words[2]);
  if (!name.has_value()) {
    state.error("unknown_molecule", "no molecule '" + words[2] + "' is known at this point of the script");
    return ok();
  }
  state.emit("object delete", {{"object", *name}});
  state.object_names.erase(*name);
  for (auto& entry : state.molecules) {
    if (entry == *name) entry.clear();
  }
  return ok();
}

Result<std::string> mol_command(State& state, const Words& words) {
  if (words.size() < 2U) {
    state.error("wrong_arguments", "mol needs a subcommand");
    return ok();
  }
  const auto sub = lower(words[1]);
  if (sub == "new") return mol_new(state, words, false);
  if (sub == "load") return mol_new(state, words, true);
  if (sub == "addfile") return mol_addfile(state, words);
  if (sub == "delete") return mol_delete(state, words);
  if (sub == "rename") {
    if (words.size() != 4U) {
      state.error("wrong_arguments", "mol rename needs a molecule id and a name");
      return ok();
    }
    const auto name = molecule_name(state, words[2]);
    if (!name.has_value()) {
      state.error("unknown_molecule", "no molecule '" + words[2] + "' is known at this point of the script");
      return ok();
    }
    if (state.is_object(words[3]) && words[3] != *name) {
      state.error("name_in_use", "an object named '" + words[3] + "' already exists");
      return ok();
    }
    state.emit("object rename", {{"object", *name}, {"name", words[3]}});
    state.object_names.erase(*name);
    state.object_names.insert(words[3]);
    for (auto& entry : state.molecules) {
      if (entry == *name) entry = words[3];
    }
    return ok();
  }
  const auto& reasons = unsupported_molecule_reasons();
  const auto found = reasons.find(sub);
  state.error("unsupported_command",
              "VMD 'mol " + sub + "': " +
                  (found != reasons.end() ? found->second : std::string{"no native operation maps to it"}));
  return ok();
}

Result<std::string> display_command(State& state, const Words& words) {
  if (words.size() < 2U) {
    state.error("wrong_arguments", "display needs a subcommand");
    return ok();
  }
  const auto sub = lower(words[1]);
  if (sub == "projection") {
    if (words.size() != 3U) {
      state.error("wrong_arguments", "display projection needs Orthographic or Perspective");
      return ok();
    }
    const auto mode = lower(words[2]);
    if (mode == "orthographic" || mode == "perspective") {
      state.emit("view projection", {{"mode", mode}});
    } else {
      state.error("unsupported_argument", "projection '" + words[2] + "' is not supported");
    }
    return ok();
  }
  if (sub == "resetview") {
    state.emit("view reset", {});
    return ok();
  }
  if (sub == "update") {
    state.info("no_effect", "the viewport refreshes after each operation");
    return ok();
  }
  state.error("unsupported_command",
              "VMD 'display " + sub + "' has no verified native equivalent");
  return ok();
}

Result<std::string> color_command(State& state, const Words& words) {
  if (words.size() >= 4U && lower(words[1]) == "display" && lower(words[2]) == "background") {
    if (words.size() != 4U) {
      state.error("wrong_arguments", "color Display Background needs one color");
      return ok();
    }
    const auto name = lower(words[3]);
    if (name != "black" && name != "white") {
      state.warning("color_values_not_verified",
                    "VMD and MolShredder may define the color '" + words[3] + "' differently");
    }
    state.emit("background set", {{"color", words[3]}});
    return ok();
  }
  if (words.size() == 7U && lower(words[1]) == "change" && lower(words[2]) == "rgb") {
    const auto r = number(words[4]);
    const auto g = number(words[5]);
    const auto b = number(words[6]);
    if (!r || !g || !b) {
      state.error("invalid_argument", "color change rgb needs three numbers");
      return ok();
    }
    state.emit("color define", {{"name", words[3]}, {"red", format_number(*r)},
                                {"green", format_number(*g)}, {"blue", format_number(*b)}});
    return ok();
  }
  state.error("unsupported_command",
              "VMD 'color" + (words.size() > 1U ? " " + words[1] : std::string{}) +
                  "' has no verified native equivalent");
  return ok();
}

Result<std::string> animate_command(State& state, const Words& words) {
  if (words.size() < 2U) {
    state.error("wrong_arguments", "animate needs a subcommand");
    return ok();
  }
  const auto sub = lower(words[1]);
  if (sub == "goto" && words.size() == 3U) {
    if (lower(words[2]) == "start") {
      state.emit("traj frame", {{"frame", "0"}});
      return ok();
    }
    const auto frame = number(words[2]);
    if (!frame || *frame < 0.0 || *frame != std::floor(*frame)) {
      state.error("invalid_argument", "animate goto needs a frame number from 0 or 'start'");
      return ok();
    }
    state.emit("traj frame", {{"frame", format_number(*frame)}});
    return ok();
  }
  if (sub == "pause" && words.size() == 2U) {
    state.emit("traj pause", {});
    return ok();
  }
  state.error("unsupported_command",
              "VMD 'animate " + sub + "' has no verified native equivalent");
  return ok();
}

Result<std::string> atomselect_command(State& state, const Words& words) {
  if (words.size() < 3U || words.size() > 5U) {
    state.error("wrong_arguments", "atomselect needs a molecule and a selection text");
    return ok();
  }
  if (words.size() > 3U) {
    state.error("unsupported_argument", "atomselect frame options are not supported");
    return ok();
  }
  if (words[1] != "top" && !molecule_name(state, words[1]).has_value()) {
    state.error("unknown_molecule", "no molecule '" + words[1] + "' is known at this point of the script");
    return ok();
  }
  if (words[1] != "top" && molecule_name(state, words[1]) != molecule_name(state, "top")) {
    state.warning("applied_to_active_object",
                  "native selections apply to the active object, not molecule " + words[1]);
  }
  state.info("selection_syntax_passed_through",
             "the VMD selection text is passed to the native selection grammar unchanged");
  const auto handle = "atomselect" + std::to_string(state.next_selection++);
  state.selections[handle] = words[2];
  return ok(handle);
}

Result<std::string> measure_command(State& state, const Words& words) {
  if (words.size() >= 3U && lower(words[1]) == "center") {
    const auto found = state.selections.find(words[2]);
    if (found == state.selections.end()) {
      state.error("unknown_selection", "'" + words[2] + "' is not an atomselect handle");
      return ok();
    }
    std::string mode = "centroid";
    for (std::size_t i = 3; i < words.size(); i += 2U) {
      if (lower(words[i]) == "weight" && i + 1U < words.size() && lower(words[i + 1U]) == "mass") {
        mode = "com";
      } else {
        state.error("unsupported_argument", "measure center option '" + words[i] + "' is not supported");
        return ok();
      }
    }
    state.emit("analyze center", {{"selection", found->second}, {"mode", mode}});
    state.warning("result_not_returned",
                  "the center is a native result; its value is not available to later Tcl commands");
    return ok();
  }
  state.error("unsupported_command",
              "VMD 'measure" + (words.size() > 1U ? " " + words[1] : std::string{}) +
                  "' has no verified native equivalent");
  return ok();
}

Result<std::string> handle_command(State& state, const Words& words) {
  const auto handle = words[0];
  if (words.size() == 2U && lower(words[1]) == "delete") {
    state.selections.erase(handle);
    return ok();
  }
  state.error("unsupported_command",
              "atom selection method '" + (words.size() > 1U ? words[1] : std::string{}) +
                  "' needs values the native adapter does not return");
  return ok();
}

Result<std::string> unknown_command(State& state, const Words& words, bool nested) {
  const auto& name = words.front();
  if (state.selections.count(name) != 0U) return handle_command(state, words);
  if (nested) {
    state.error("unsupported_command_substitution",
                "'" + name + "' inside [...] cannot be evaluated by the adapter");
    return Result<std::string>::failure(
        Error{ErrorCode::script_failed, "cannot evaluate [" + name + "]", {}});
  }
  if (name == "exit" || name == "quit") {
    state.current->stop_script = true;
    state.info("script_stopped", "exit ends the script");
  } else if (name == "puts" || name == "echo") {
    state.info("no_effect", "'" + name + "' only prints");
  } else if (control_flow().count(name) != 0U) {
    state.error("unsupported_control_flow",
                "'" + name + "' needs a full Tcl interpreter; only straight-line commands are translated");
  } else if (code_execution().count(name) != 0U) {
    state.error("requires_code_execution",
                "'" + name + "' runs code or touches the system and is never run by the adapter");
  } else {
    state.error("unsupported_command", "no native operation maps to '" + name + "'");
  }
  return ok();
}

}  // namespace

Translator::Translator(TranslationContext context)
    : state_{std::make_shared<State>(std::move(context))} {
  auto* state = state_.get();
  const auto bind = [state](Result<std::string> (*function)(State&, const Words&)) {
    return [state, function](tcl::Interpreter&, const Words& words) {
      return function(*state, words);
    };
  };
  state->interpreter.register_command("mol", bind(&mol_command));
  state->interpreter.register_command("display", bind(&display_command));
  state->interpreter.register_command("color", bind(&color_command));
  state->interpreter.register_command("animate", bind(&animate_command));
  state->interpreter.register_command("atomselect", bind(&atomselect_command));
  state->interpreter.register_command("measure", bind(&measure_command));
  state->interpreter.set_unknown_handler(
      [state](tcl::Interpreter&, const Words& words, bool nested) {
        return unknown_command(*state, words, nested);
      });
}

TranslatedCommand Translator::translate(const tcl::Command& command) {
  TranslatedCommand out;
  out.line = command.line;
  out.text = command.text;
  state_->current = &out;
  Words words;
  words.reserve(command.words.size());
  bool failed = false;
  for (const auto& word : command.words) {
    auto value = state_->interpreter.substitute(word);
    if (!value.has_value()) {
      state_->error("tcl_error", value.error().message);
      out.tcl_error = true;
      failed = true;
      break;
    }
    words.push_back(std::move(value.value()));
  }
  if (!failed && !words.empty()) {
    out.command = words.front();
    const auto result = state_->interpreter.run_words(words);
    if (!result.has_value() && !out.tcl_error) {
      // Only substitution and builtin errors reach here (handlers never fail).
      if (!out.has_error()) state_->error("tcl_error", result.error().message);
      out.tcl_error = true;
    }
  }
  state_->current = nullptr;
  if (out.has_error()) out.invocations.clear();
  return out;
}

TranslatedCommand Translator::syntax_failure(const tcl::SyntaxError& error) {
  TranslatedCommand out;
  out.line = error.line;
  out.command = "";
  out.tcl_error = true;
  out.diagnostics.push_back({"tcl_syntax_error", error.message, error.line, "error"});
  return out;
}

Translation translate_script(std::string_view source, TranslationContext context) {
  Translation translation;
  const auto script = tcl::parse(source);
  translation.comment_lines = script.comment_lines;
  Translator translator{std::move(context)};
  for (const auto& command : script.commands) {
    auto translated = translator.translate(command);
    translation.invocation_count += translated.invocations.size();
    if (translated.has_error()) {
      ++translation.failed_commands;
    } else if (!translated.invocations.empty()) {
      ++translation.translated_commands;
    }
    const bool stop = translated.stop_script || translated.tcl_error;
    translation.commands.push_back(std::move(translated));
    if (stop) {
      translation.stopped = true;
      return translation;  // a Tcl error or exit ends the script
    }
  }
  if (script.error.has_value()) {
    translation.commands.push_back(Translator::syntax_failure(*script.error));
    ++translation.failed_commands;
    translation.stopped = true;
  }
  return translation;
}

}  // namespace molshredder::automation::vmd
