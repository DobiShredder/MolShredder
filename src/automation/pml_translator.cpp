#include "molshredder/automation/pml_translator.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <unordered_map>
#include <utility>

namespace molshredder::automation::pml {
namespace {

using Arguments = std::map<std::string, std::string, std::less<>>;

std::string lower(std::string_view text) {
  std::string out{text};
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::string trimmed(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1U);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1U);
  return std::string{text};
}

// Removes one pair of matching surrounding quotes.
std::string unquote(std::string_view text) {
  const auto value = trimmed(text);
  if (value.size() >= 2U && (value.front() == '"' || value.front() == '\'') &&
      value.back() == value.front()) {
    return value.substr(1U, value.size() - 2U);
  }
  return value;
}

bool is_complete_string_literal(std::string_view text) {
  const auto value = trimmed(text);
  if (value.size() < 2U || (value.front() != '"' && value.front() != '\'') ||
      value.back() != value.front()) {
    return false;
  }
  for (std::size_t i = 1; i + 1U < value.size(); ++i) {
    if (value[i] == '\\') {
      ++i;
    } else if (value[i] == value.front()) {
      return false;
    }
  }
  return true;
}

std::optional<double> parse_number(std::string_view text) {
  const auto value = trimmed(text);
  if (value.empty()) return std::nullopt;
  char* end = nullptr;
  const std::string owned{value};
  const double parsed = std::strtod(owned.c_str(), &end);
  if (end == owned.c_str() || *end != '\0' || !std::isfinite(parsed)) {
    return std::nullopt;
  }
  return parsed;
}

std::optional<bool> parse_flag(std::string_view text) {
  const auto value = lower(unquote(text));
  if (value == "1" || value == "on" || value == "yes" || value == "true") return true;
  if (value == "0" || value == "off" || value == "no" || value == "false") return false;
  return std::nullopt;
}

std::string format_number(double value) {
  if (value == std::floor(value) && std::fabs(value) < 1.0e15) {
    return std::to_string(static_cast<long long>(value));
  }
  char buffer[40];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string{buffer, result.ptr};
}

bool numerically_equal(std::string_view text, std::string_view expected) {
  const auto a = parse_number(text);
  const auto b = parse_number(expected);
  return a.has_value() && b.has_value() && *a == *b;
}

std::string extension_of(std::string_view path) {
  return lower(std::filesystem::path{std::string{path}}.extension().string());
}

std::string stem_of(std::string_view path) {
  return std::filesystem::path{std::string{path}}.stem().string();
}

// PyMOL representation name -> native representation mask.
std::optional<std::string> native_representation(std::string_view name) {
  static const std::map<std::string, std::string, std::less<>> table{
      {"lines", "lines"},   {"wire", "wire"},       {"sticks", "sticks"},
      {"spheres", "spheres"}, {"ribbon", "ribbon"}, {"cartoon", "cartoon"},
      {"labels", "labels"}, {"everything", "everything"}};
  const auto found = table.find(name);
  if (found == table.end()) return std::nullopt;
  return found->second;
}

const std::map<std::string, std::string, std::less<>>& structure_formats() {
  static const std::map<std::string, std::string, std::less<>> table{
      {".pdb", "pdb"},   {".ent", "pdb"},    {".pdb1", "pdb"},
      {".cif", "mmcif"}, {".mmcif", "mmcif"}, {".bcif", "bcif"},
      {".pqr", "pqr"},   {".mol", "mol"},    {".mol2", "mol2"},
      {".sdf", "sdf"},   {".xyz", "xyz"},    {".gro", "gro"},
      {".g96", "g96"},   {".psf", "psf"},    {".prmtop", "prmtop"},
      {".parm7", "prmtop"}, {".vtf", "vtf"}};
  return table;
}

const std::map<std::string, std::string, std::less<>>& volume_formats() {
  static const std::map<std::string, std::string, std::less<>> table{
      {".ccp4", "ccp4"}, {".map", "ccp4"}, {".mrc", "mrc"},
      {".mrcs", "mrcs"}, {".dx", "dx"}};
  return table;
}

const std::map<std::string, std::string, std::less<>>& trajectory_formats() {
  static const std::map<std::string, std::string, std::less<>> table{
      {".dcd", "dcd"},     {".xtc", "xtc"},         {".trr", "trr"},
      {".nc", "netcdf"},   {".ncdf", "netcdf"},     {".netcdf", "netcdf"},
      {".mdcrd", "mdcrd"}, {".crd", "mdcrd"},       {".crdbox", "crdbox"},
      {".binpos", "binpos"}, {".lammpstrj", "lammps"}, {".rst7", "rst7"},
      {".h5md", "h5md"}};
  return table;
}

// Commands with no native operation, grouped by the reason shown to users.
const std::map<std::string, std::string, std::less<>>& unsupported_reasons() {
  static const std::map<std::string, std::string, std::less<>> table = [] {
    std::map<std::string, std::string, std::less<>> result;
    const auto add = [&result](std::initializer_list<const char*> names,
                               const char* reason) {
      for (const char* name : names) result.emplace(name, reason);
    };
    add({"alter", "iterate", "alter_state", "iterate_state", "map_set",
         "sort", "unset_bond"},
        "evaluates Python expressions over atoms; run it as a trusted Python script");
    add({"fetch"}, "downloads from a remote service, which is not file I/O");
    add({"create", "copy", "group", "extract", "pseudoatom", "ungroup",
         "split_states", "join_states", "fuse", "bond", "unbond", "remove",
         "remove_picked", "h_add", "h_fill", "replace", "attach", "cycle_valence",
         "invert", "dss", "sculpt_activate", "sculpt_iterate", "clean"},
        "no native operation builds or edits structures this way");
    add({"align", "super", "cealign", "fit", "pair_fit", "intra_fit", "rms",
         "rms_cur", "intra_rms", "intra_rms_cur", "fab", "rotate", "translate",
         "matrix_copy", "matrix_reset", "transform_selection", "transform_object"},
        "no native operation applies this PyMOL alignment or object transform");
    add({"isomesh", "isosurface", "isodot", "gradient", "slice_new", "volume",
         "volume_color", "map_new", "map_double", "map_halve", "map_trim",
         "ramp_new"},
        "map and ramp construction is available through the native volume and ramp commands");
    add({"ray", "mpng", "mdo", "mappend", "mmatrix",
         "mview", "mtoggle", "mdump", "mcopy", "rock", "viewport", "stereo",
         "full_screen", "window", "wizard", "set_key", "button", "mouse"},
        "interactive, animation-script or window state has no native equivalent");
    add({"pwd", "cd", "ls", "dir", "edit", "unpick", "pick", "bond_order",
         "hide_menu"},
        "interactive or shell command with no native equivalent");
    return result;
  }();
  return table;
}

}  // namespace

bool TranslatedStatement::has_error() const {
  return std::any_of(diagnostics.begin(), diagnostics.end(),
                     [](const Diagnostic& d) { return d.severity == "error"; });
}

bool Translator::is_object(std::string_view name) const {
  if (deleted_objects_.count(name) != 0U) return false;
  if (objects_.count(name) != 0U) return true;
  return context_.is_object && context_.is_object(name);
}

bool Translator::is_selection(std::string_view name) const {
  if (selections_.count(name) != 0U) return true;
  return context_.is_selection && context_.is_selection(name);
}

std::vector<std::string> Translator::known_objects() const {
  std::set<std::string, std::less<>> names{objects_};
  if (context_.list_objects) {
    for (auto& name : context_.list_objects()) names.insert(std::move(name));
  }
  for (const auto& name : deleted_objects_) names.erase(name);
  return {names.begin(), names.end()};
}

// Translation of one statement. A friend of Translator so it can update the
// names the script defines.
class StatementTranslation {
 public:
  StatementTranslation(Translator& translator, const Statement& statement,
                       TranslatedStatement& out)
      : translator_{translator}, statement_{statement}, out_{out} {}

  void run();

 private:
  using Handler = void (StatementTranslation::*)();

  void error(std::string code, std::string message) {
    out_.diagnostics.push_back({std::move(code), std::move(message), out_.line, "error"});
  }
  void warning(std::string code, std::string message) {
    out_.diagnostics.push_back({std::move(code), std::move(message), out_.line, "warning"});
  }
  void info(std::string code, std::string message) {
    out_.diagnostics.push_back({std::move(code), std::move(message), out_.line, "info"});
  }

  // Binds positional and keyword arguments to PyMOL's parameter names.
  bool bind(std::initializer_list<std::string_view> names) {
    values_.clear();
    const std::vector<std::string_view> order{names};
    if (statement_.args.size() > order.size()) {
      error("too_many_arguments",
            "'" + statement_.command + "' takes at most " +
                std::to_string(order.size()) + " arguments");
      return false;
    }
    for (std::size_t i = 0; i < statement_.args.size(); ++i) {
      if (statement_.args[i].has_value()) {
        values_[std::string{order[i]}] = *statement_.args[i];
      }
    }
    for (const auto& [name, value] : statement_.kwargs) {
      if (std::find(order.begin(), order.end(), name) == order.end()) {
        error("unknown_argument",
              "'" + statement_.command + "' has no argument named '" + name + "'");
        return false;
      }
      if (values_.count(name) != 0U) {
        error("duplicate_argument", "argument '" + name + "' is given twice");
        return false;
      }
      values_[name] = value;
    }
    return true;
  }

  [[nodiscard]] bool given(std::string_view name) const {
    const auto found = values_.find(name);
    return found != values_.end() && !unquote(found->second).empty();
  }
  [[nodiscard]] std::optional<std::string> text(std::string_view name) const {
    const auto found = values_.find(name);
    if (found == values_.end()) return std::nullopt;
    auto value = unquote(found->second);
    if (value.empty()) return std::nullopt;
    return value;
  }
  [[nodiscard]] std::string selection(std::string_view name) const {
    const auto found = values_.find(name);
    if (found == values_.end()) return "all";
    const auto value = trimmed(found->second);
    return value.empty() ? "all" : rewrite_names(value);
  }
  // A selection argument that must be given (no "all" fallback).
  [[nodiscard]] std::string expression(std::string_view name) const {
    const auto found = values_.find(name);
    return found == values_.end() ? std::string{} : rewrite_names(trimmed(found->second));
  }
  // PyMOL lets a bare word name a stored selection or an object; the native
  // grammar needs `@name` and `object name` for those.
  [[nodiscard]] std::string rewrite_names(const std::string& text) const;
  [[nodiscard]] bool require(std::string_view name) {
    if (given(name)) return true;
    error("missing_argument",
          "'" + statement_.command + "' needs the argument '" + std::string{name} + "'");
    return false;
  }
  // The argument changes behavior that the native operation cannot express.
  void require_default(std::string_view name,
                       std::initializer_list<std::string_view> defaults) {
    const auto value = text(name);
    if (!value.has_value()) return;
    for (const auto expected : defaults) {
      if (*value == expected || numerically_equal(*value, expected)) return;
    }
    error("unsupported_argument",
          "argument '" + std::string{name} + "'=" + *value +
              " has no native equivalent");
  }
  void ignored_hint(std::string_view name) {
    if (given(name)) {
      warning("ignored_argument",
              "argument '" + std::string{name} + "' has no effect in MolShredder");
    }
  }
  [[nodiscard]] std::string flag(std::string_view name, bool fallback) {
    const auto value = text(name);
    if (!value.has_value()) return fallback ? "true" : "false";
    const auto parsed = parse_flag(*value);
    if (!parsed.has_value()) {
      error("invalid_argument", "argument '" + std::string{name} +
                                    "' is not a boolean: " + *value);
      return fallback ? "true" : "false";
    }
    return *parsed ? "true" : "false";
  }
  [[nodiscard]] std::string state_value(std::string_view name, std::string fallback) {
    const auto value = text(name);
    if (!value.has_value()) return fallback;
    const auto number = parse_number(*value);
    if (!number.has_value() || *number != std::floor(*number)) {
      error("invalid_argument", "argument '" + std::string{name} +
                                    "' is not a state number: " + *value);
      return fallback;
    }
    return format_number(*number);
  }
  [[nodiscard]] std::string duration(std::string_view name) {
    const auto value = text(name);
    if (!value.has_value()) return "0";
    const auto number = parse_number(*value);
    if (!number.has_value()) {
      error("invalid_argument", "argument '" + std::string{name} + "' is not a number");
      return "0";
    }
    return *number > 0.0 ? format_number(*number) : "0";
  }
  void emit(std::string command, Arguments arguments) {
    out_.invocations.push_back({std::move(command), std::move(arguments)});
  }

  void translate_load();
  void translate_load_traj();
  void translate_save();
  void translate_show();
  void translate_hide();
  void translate_as();
  void translate_toggle();
  void translate_color();
  void translate_select();
  void translate_zoom();
  void translate_center();
  void translate_orient();
  void translate_reset();
  void translate_turn();
  void translate_move();
  void translate_clip();
  void translate_origin();
  void translate_set();
  void translate_unset();
  void translate_bg_color();
  void translate_png();
  void translate_label();
  void translate_delete();
  void translate_enable_disable(bool enable);
  void translate_group();
  void translate_enable() { translate_enable_disable(true); }
  void translate_disable() { translate_enable_disable(false); }
  void translate_spectrum();
  void translate_distance();
  void translate_angle();
  void translate_dihedral();
  void translate_cartoon();
  void translate_scene();
  void translate_view();
  void translate_set_view();
  void translate_get_view();
  void translate_frame();
  void translate_mplay();
  void translate_movie_map(bool append);
  void translate_mset() { translate_movie_map(false); }
  void translate_madd() { translate_movie_map(true); }
  void translate_mclear();
  void translate_mstop();
  void translate_quit();
  void translate_refresh();
  void translate_recolor();
  void translate_set_color();
  void translate_util_atomic_colors();
  void translate_code_execution();
  void translate_representation(std::string command, std::string default_representation);

  Translator& translator_;
  const Statement& statement_;
  TranslatedStatement& out_;
  std::map<std::string, std::string, std::less<>> values_;
};

std::string StatementTranslation::rewrite_names(const std::string& text) const {
  struct Token {
    std::string text;
    bool word{};
  };
  std::vector<Token> tokens;
  for (std::size_t i = 0; i < text.size();) {
    const char c = text[i];
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      tokens.push_back({std::string{c}, false});
      ++i;
    } else if (c == '(' || c == ')') {
      tokens.push_back({std::string{c}, false});
      ++i;
    } else if (c == '"' || c == '\'') {
      std::size_t j = i + 1;
      while (j < text.size() && text[j] != c) {
        if (text[j] == '\\' && j + 1 < text.size()) ++j;
        ++j;
      }
      j = std::min(j + 1U, text.size());
      tokens.push_back({text.substr(i, j - i), false});
      i = j;
    } else {
      std::size_t j = i;
      while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j])) == 0 &&
             text[j] != '(' && text[j] != ')') {
        ++j;
      }
      tokens.push_back({text.substr(i, j - i), true});
      i = j;
    }
  }
  static const std::set<std::string, std::less<>> before{
      "and", "or", "not", "!", "&", "|", "+", "byres", "br.", "bychain", "bc.",
      "bymolecule", "bm.", "byobject", "bo.", "bycalpha", "bca.", "bysegi", "bs.",
      "bycell", "neighbor", "nbr.", "bound_to", "bto.", "first", "last", "of"};
  static const std::set<std::string, std::less<>> after{
      "and", "or", "&", "|", "+", "around", "a.", "expand", "x.", "gap", "near_to", "nto."};
  std::vector<std::size_t> significant;  // indices of non-space tokens
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (std::isspace(static_cast<unsigned char>(tokens[i].text.front())) == 0 ||
        tokens[i].text.size() > 1U) {
      significant.push_back(i);
    }
  }
  std::string result;
  std::size_t position = 0;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    auto word = tokens[i].text;
    if (tokens[i].word) {
      const std::size_t current = position;
      const bool starts = current == 0U || [&] {
        const auto& previous = tokens[significant[current - 1U]].text;
        return previous == "(" || before.count(lower(previous)) != 0U;
      }();
      const bool ends = current + 1U >= significant.size() || [&] {
        const auto& next = tokens[significant[current + 1U]].text;
        return next == ")" || after.count(lower(next)) != 0U;
      }();
      if (starts && ends) {
        if (translator_.is_selection(word)) {
          word = "@" + word;
        } else if (translator_.is_object(word)) {
          word = "object " + word;
        }
      }
    }
    if (std::isspace(static_cast<unsigned char>(tokens[i].text.front())) == 0 ||
        tokens[i].text.size() > 1U) {
      ++position;
    }
    result += word;
  }
  return result;
}

void StatementTranslation::translate_code_execution() {
  error("requires_python_trust",
        "'" + statement_.command +
            "' executes code; run the file as a trusted Python script instead");
}

void StatementTranslation::translate_load() {
  if (!bind({"filename", "object", "state", "format", "finish", "discrete", "quiet",
             "multiplex", "zoom", "partial", "mimic", "object_props", "atom_props"}) ||
      !require("filename")) {
    return;
  }
  require_default("state", {"0"});
  require_default("finish", {"1"});
  require_default("discrete", {"-1", "0"});
  require_default("multiplex", {"-2", "-1", "0"});
  require_default("partial", {"0"});
  require_default("mimic", {"1"});
  if (given("object_props") || given("atom_props")) {
    error("unsupported_argument", "object_props and atom_props are not supported");
  }
  if (const auto zoom = text("zoom"); zoom.has_value() && !numerically_equal(*zoom, "-1")) {
    warning("ignored_argument", "argument 'zoom' has no effect in MolShredder");
  }
  const auto path = *text("filename");
  auto extension = extension_of(path);
  if (extension == ".gz") {
    error("unsupported_argument", "compressed input is not supported: " + path);
    return;
  }
  std::string hint = text("format").has_value() ? lower(*text("format")) : "";
  if (hint.empty()) hint = extension.empty() ? "" : extension.substr(1U);
  const auto key = "." + hint;
  const auto object_name = text("object").value_or(stem_of(path));
  if (const auto found = structure_formats().find(key); found != structure_formats().end()) {
    // Always explicit: the native default comes from the file content.
    Arguments args{{"path", path}, {"file-format", found->second}, {"name", object_name}};
    emit("load", std::move(args));
    translator_.deleted_objects_.erase(object_name);
    translator_.objects_.insert(object_name);
  } else if (const auto volume = volume_formats().find(key); volume != volume_formats().end()) {
    Arguments args{{"path", path}, {"file-format", volume->second}, {"name", object_name}};
    emit("volume load", std::move(args));
    translator_.deleted_objects_.erase(object_name);
    translator_.objects_.insert(object_name);
  } else if (const auto traj = trajectory_formats().find(key); traj != trajectory_formats().end()) {
    emit("traj load", {{"path", path}, {"file-format", traj->second}, {"mapping", "index"}});
    if (text("object").has_value()) {
      warning("applied_to_active_object",
              "the trajectory is attached to the active object, not to '" + object_name + "'");
    }
  } else if (key == ".pml" || key == ".py" || key == ".pym") {
    translate_code_execution();
  } else if (key == ".pse" || key == ".pze" || key == ".psw") {
    error("unsupported_format", "PyMOL session files are not MolShredder sessions: " + path);
  } else {
    error("unsupported_format", "no native reader for '" + hint + "' (" + path + ")");
  }
}

void StatementTranslation::translate_load_traj() {
  if (!bind({"filename", "object", "state", "format", "interval", "average", "start", "stop",
             "max", "selection", "image", "shift", "plugin"}) ||
      !require("filename")) {
    return;
  }
  require_default("state", {"1"});
  require_default("interval", {"1"});
  require_default("average", {"1"});
  require_default("start", {"1"});
  require_default("stop", {"-1"});
  require_default("max", {"-1"});
  require_default("image", {"1"});
  if (given("plugin")) error("unsupported_argument", "plugin selection is not supported");
  if (const auto sel = text("selection");
      sel.has_value() && *sel != "all" && *sel != "(all)") {
    error("unsupported_argument", "loading a trajectory for part of the object is not supported");
  }
  if (const auto shift = text("shift"); shift.has_value() && trimmed(*shift) != "[0.0,0.0,0.0]") {
    error("unsupported_argument", "argument 'shift' has no native equivalent");
  }
  const auto path = *text("filename");
  auto hint = text("format").has_value() ? lower(*text("format")) : extension_of(path);
  if (!hint.empty() && hint.front() == '.') hint.erase(0U, 1U);
  const auto found = trajectory_formats().find("." + hint);
  if (found == trajectory_formats().end()) {
    error("unsupported_format", "no native trajectory reader for '" + hint + "'");
    return;
  }
  emit("traj load", {{"path", path}, {"file-format", found->second}, {"mapping", "index"}});
  if (text("object").has_value()) {
    warning("applied_to_active_object",
            "the trajectory is attached to the active object, not to '" +
                *text("object") + "'");
  }
}

void StatementTranslation::translate_save() {
  if (!bind({"filename", "selection", "state", "format", "ref", "ref_state", "quiet", "partial"}) ||
      !require("filename")) {
    return;
  }
  if (const auto sel = text("selection");
      sel.has_value() && *sel != "all" && *sel != "(all)") {
    error("unsupported_argument", "saving part of the object is not supported");
  }
  if (given("ref") || given("partial")) {
    error("unsupported_argument", "reference alignment and partial saving are not supported");
  }
  const auto path = *text("filename");
  auto hint = text("format").has_value() ? lower(*text("format")) : extension_of(path);
  if (!hint.empty() && hint.front() == '.') hint.erase(0U, 1U);
  const auto state = text("state").value_or("-1");
  std::string frames = "current";
  if (numerically_equal(state, "0")) {
    frames = "all";
  } else if (!numerically_equal(state, "-1")) {
    error("unsupported_argument", "saving a single numbered state is not supported");
  }
  if (const auto found = structure_formats().find("." + hint);
      found != structure_formats().end() && found->second != "bcif" &&
      found->second != "prmtop" && found->second != "vtf") {
    emit("save", {{"path", path}, {"file-format", found->second}, {"frames", frames},
                  {"overwrite", "true"}});
  } else if (const auto traj = trajectory_formats().find("." + hint);
             traj != trajectory_formats().end() && traj->second != "xtc" &&
             traj->second != "netcdf" && traj->second != "lammps" &&
             traj->second != "h5md") {
    emit("traj save", {{"path", path}, {"file-format", traj->second}, {"overwrite", "true"}});
  } else if (volume_formats().count("." + hint) != 0U) {
    emit("volume save", {{"path", path}, {"overwrite", "true"}});
  } else if (hint == "png") {
    emit("image save", {{"path", path}});
  } else {
    error("unsupported_format", "no native writer for '" + hint + "' (" + path + ")");
  }
}

void StatementTranslation::translate_representation(std::string command,
                                                    std::string default_representation) {
  if (!bind({"representation", "selection"})) return;
  const auto representation = text("representation").value_or(default_representation);
  const auto selection_text = selection("selection");
  if (representation == "surface") {
    if (command == "show") {
      emit("surface show", {{"selection", selection_text}});
    } else if (command == "hide") {
      if (selection_text != "all" && selection_text != "(all)") {
        warning("selection_not_applied", "the native surface is hidden for the whole object");
      }
      emit("surface hide", {});
    } else {
      error("unsupported_representation", "'" + command + " surface' has no native equivalent");
    }
    return;
  }
  const auto native = native_representation(representation);
  if (!native.has_value()) {
    error("unsupported_representation",
          "representation '" + representation + "' is not available");
    return;
  }
  emit(command, {{"representation", *native}, {"selection", selection_text},
                 {"scope", "workspace"}});
}

void StatementTranslation::translate_show() { translate_representation("show", "wire"); }
void StatementTranslation::translate_hide() { translate_representation("hide", "everything"); }
void StatementTranslation::translate_as() { translate_representation("as", "wire"); }
void StatementTranslation::translate_toggle() { translate_representation("toggle", "lines"); }

void StatementTranslation::translate_color() {
  if (!bind({"color", "selection", "quiet", "flags"}) || !require("color")) return;
  require_default("flags", {"0"});
  emit("color", {{"color", *text("color")}, {"selection", selection("selection")},
                 {"scope", "workspace"}});
}

void StatementTranslation::translate_select() {
  if (!bind({"name", "selection", "enable", "quiet", "merge", "state", "domain"}) ||
      !require("name")) {
    return;
  }
  require_default("merge", {"0"});
  require_default("state", {"0"});
  if (given("domain")) error("unsupported_argument", "argument 'domain' has no native equivalent");
  ignored_hint("enable");
  std::string name = *text("name");
  std::string expression = trimmed(values_.count("selection") != 0U ? values_["selection"] : "");
  if (expression.empty()) {
    expression = *text("name");  // `select <expression>` names the result "sele"
    name = "sele";
  }
  emit("select", {{"name", name}, {"expression", rewrite_names(expression)}});
  translator_.selections_.insert(name);
}

void StatementTranslation::translate_zoom() {
  if (!bind({"selection", "buffer", "state", "complete", "animate"})) return;
  Arguments args{{"selection", selection("selection")},
                 {"state", state_value("state", "0")},
                 {"complete", flag("complete", false)},
                 {"duration", duration("animate")}};
  if (const auto buffer = text("buffer")) {
    const auto number = parse_number(*buffer);
    if (!number.has_value()) {
      error("invalid_argument", "argument 'buffer' is not a number");
    } else {
      args["buffer"] = format_number(*number);
    }
  }
  emit("view zoom", std::move(args));
}

void StatementTranslation::translate_center() {
  if (!bind({"selection", "state", "origin", "animate"})) return;
  emit("view center", {{"selection", selection("selection")},
                       {"state", state_value("state", "0")},
                       {"move-origin", flag("origin", true)},
                       {"duration", duration("animate")}});
}

void StatementTranslation::translate_orient() {
  if (!bind({"selection", "state", "animate"})) return;
  emit("view orient", {{"selection", selection("selection")},
                       {"state", state_value("state", "0")},
                       {"duration", duration("animate")}});
}

void StatementTranslation::translate_reset() {
  if (!bind({"object"})) return;
  Arguments args;
  if (const auto object = text("object")) args["object"] = *object;
  emit("view reset", std::move(args));
}

void StatementTranslation::translate_turn() {
  if (!bind({"axis", "angle"}) || !require("axis") || !require("angle")) return;
  const auto axis = lower(*text("axis"));
  const auto angle = parse_number(*text("angle"));
  if (axis != "x" && axis != "y" && axis != "z") {
    error("invalid_argument", "axis must be x, y or z");
    return;
  }
  if (!angle.has_value()) {
    error("invalid_argument", "angle is not a number");
    return;
  }
  emit("view turn", {{"axis", axis}, {"angle", format_number(*angle)}});
}

void StatementTranslation::translate_move() {
  if (!bind({"axis", "distance"}) || !require("axis") || !require("distance")) return;
  const auto axis = lower(*text("axis"));
  const auto distance = parse_number(*text("distance"));
  if (axis != "x" && axis != "y" && axis != "z") {
    error("invalid_argument", "axis must be x, y or z");
    return;
  }
  if (!distance.has_value()) {
    error("invalid_argument", "distance is not a number");
    return;
  }
  emit("view move", {{"axis", axis}, {"distance", format_number(*distance)}});
}

void StatementTranslation::translate_clip() {
  if (!bind({"mode", "distance", "selection", "state"}) || !require("mode") ||
      !require("distance")) {
    return;
  }
  auto mode = lower(*text("mode"));
  std::replace(mode.begin(), mode.end(), '_', '-');
  static const std::set<std::string, std::less<>> modes{
      "near", "far", "move", "slab", "atoms", "near-set", "far-set"};
  if (modes.count(mode) == 0U) {
    error("unsupported_argument", "clip mode '" + mode + "' is not available");
    return;
  }
  const auto distance = parse_number(*text("distance"));
  if (!distance.has_value()) {
    error("invalid_argument", "distance is not a number");
    return;
  }
  Arguments args{{"mode", mode}, {"distance", format_number(*distance)}};
  if (const auto sel = text("selection")) args["selection"] = expression("selection");
  if (text("state").has_value()) args["state"] = state_value("state", "0");
  emit("view clip", std::move(args));
}

void StatementTranslation::translate_origin() {
  if (!bind({"selection", "object", "position", "state"})) return;
  Arguments args{{"state", state_value("state", "0")}};
  if (const auto position = text("position")) {
    auto value = *position;
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](char c) { return c == '[' || c == ']' || c == ' '; }),
                value.end());
    args["position"] = value;
  } else {
    args["selection"] = selection("selection");
  }
  if (const auto object = text("object")) args["object"] = *object;
  emit("view origin", std::move(args));
}

void StatementTranslation::translate_set() {
  if (!bind({"name", "value", "selection", "state", "updates", "log", "quiet"}) ||
      !require("name")) {
    return;
  }
  require_default("state", {"0"});
  Arguments args{{"name", *text("name")}, {"value", text("value").value_or("1")}};
  if (const auto target = text("selection")) {
    const auto token = trimmed(values_["selection"]);
    const bool identifier = std::all_of(token.begin(), token.end(), [](char c) {
      return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-' || c == '.';
    });
    if (identifier && translator_.is_object(token)) {
      args["scope"] = "object";
      args["object"] = token;
    } else if (identifier && translator_.is_selection(token)) {
      args["scope"] = "atom";
      args["selection"] = "@" + token;
    } else if (identifier && token != "all" && token != "sele") {
      args["scope"] = "object";
      args["object"] = token;
      info("assumed_object_scope", "'" + token + "' is not a known name; used as an object");
    } else {
      args["scope"] = "atom";
      args["selection"] = rewrite_names(token);
    }
    (void)target;
  }
  emit("setting set", std::move(args));
}

void StatementTranslation::translate_unset() {
  if (!bind({"name", "selection", "state", "updates", "log", "quiet"}) || !require("name")) {
    return;
  }
  require_default("state", {"0"});
  Arguments args{{"name", *text("name")}};
  if (text("selection").has_value()) {
    const auto token = trimmed(values_["selection"]);
    if (translator_.is_object(token)) {
      args["scope"] = "object";
      args["object"] = token;
    } else {
      args["scope"] = "atom";
      args["selection"] = translator_.is_selection(token) ? "@" + token : rewrite_names(token);
    }
  }
  emit("setting unset", std::move(args));
}

void StatementTranslation::translate_bg_color() {
  if (!bind({"color"})) return;
  emit("background set", {{"color", text("color").value_or("black")}});
}

void StatementTranslation::translate_png() {
  if (!bind({"filename", "width", "height", "dpi", "ray", "quiet", "prior", "format"}) ||
      !require("filename")) {
    return;
  }
  require_default("prior", {"0"});
  require_default("format", {"0"});
  Arguments args{{"path", *text("filename")}};
  for (const auto key : {"width", "height", "dpi"}) {
    if (const auto value = text(key)) {
      const auto number = parse_number(*value);
      if (!number.has_value()) {
        error("invalid_argument", std::string{"argument '"} + key + "' is not a number");
      } else if (*number > 0.0) {
        args[key] = format_number(*number);
      }
    }
  }
  if (text("ray").has_value()) args["ray"] = flag("ray", false);
  emit("image save", std::move(args));
}

void StatementTranslation::translate_label() {
  if (!bind({"selection", "expression", "quiet"})) return;
  const auto selection_text = selection("selection");
  const auto expression = trimmed(values_.count("expression") != 0U ? values_["expression"] : "");
  if (expression.empty() || expression == "''" || expression == "\"\"") {
    emit("hide", {{"representation", "labels"}, {"selection", selection_text},
                  {"scope", "workspace"}});
  } else if (is_complete_string_literal(expression)) {
    emit("label literal", {{"selection", selection_text}, {"text", unquote(expression)}});
  } else {
    emit("label", {{"selection", selection_text}, {"expression", expression}});
  }
}

void StatementTranslation::translate_group() {
  if (!bind({"name", "members", "action", "quiet"}) || !require("name")) return;
  const std::string name = *text("name");
  const std::string members = given("members") ? *text("members") : std::string{};
  std::string action = given("action") ? *text("action") : std::string{"auto"};
  if (action == "auto") action = members.empty() ? "toggle" : "add";
  if (action == "add") {
    if (members.empty()) {
      error("missing_argument", "group add needs members");
      return;
    }
    emit("group add", {{"name", name}, {"members", members}});
  } else if (action == "remove") {
    if (members.empty()) {
      error("missing_argument", "group remove needs members");
      return;
    }
    emit("group remove", {{"name", name}, {"members", members}});
  } else if (action == "ungroup") {
    emit("group ungroup", {{"members", members.empty() ? name : members}});
  } else if (action == "open" || action == "close" || action == "toggle" || action == "empty" ||
             action == "purge" || action == "excise") {
    emit("group " + action, {{"name", name}});
  } else {
    error("unsupported_argument", "group action '" + action + "' has no native equivalent");
  }
}

void StatementTranslation::translate_enable_disable(bool enable) {
  if (!bind({"name", "parents"})) return;
  require_default("parents", {"0"});
  std::string name = given("name") ? *text("name") : std::string{"all"};
  const std::string command = enable ? "object enable" : "object disable";
  if (name == "all" || name == "*") {
    emit(command, {{"pattern", "*"}});
  } else if (translator_.is_selection(name) && !translator_.is_object(name)) {
    emit(command, {{"selection", "@" + name}});
  } else {
    emit(command, {{"pattern", name}});
  }
}

void StatementTranslation::translate_delete() {
  if (!bind({"name"}) || !require("name")) return;
  const auto name = *text("name");
  if (name == "all" || name == "*") {
    const auto objects = translator_.known_objects();
    if (objects.empty()) {
      warning("nothing_to_delete", "no object is known at this point of the script");
    }
    for (const auto& object : objects) {
      emit("object delete", {{"object", object}});
      translator_.objects_.erase(object);
      translator_.deleted_objects_.insert(object);
    }
    return;
  }
  if (name.find('*') != std::string::npos || name.find('?') != std::string::npos) {
    error("unsupported_argument", "wildcard names are not supported by delete");
    return;
  }
  if (translator_.is_selection(name) && !translator_.is_object(name)) {
    translator_.selections_.erase(name);
    emit("select delete", {{"name", name}});
    return;
  }
  if (!translator_.is_object(name)) {
    info("assumed_object", "'" + name + "' is not a known name; used as an object");
  }
  emit("object delete", {{"object", name}});
  translator_.objects_.erase(name);
  translator_.deleted_objects_.insert(name);
}

void StatementTranslation::translate_spectrum() {
  if (!bind({"expression", "palette", "selection", "minimum", "maximum", "byres", "quiet",
             "interpolation"})) {
    return;
  }
  Arguments args;
  if (const auto v = text("expression")) args["expression"] = *v;
  if (const auto v = text("palette")) args["palette"] = *v;
  args["selection"] = selection("selection");
  for (const auto key : {"minimum", "maximum"}) {
    if (const auto value = text(key)) {
      const auto number = parse_number(*value);
      if (!number.has_value()) {
        error("invalid_argument", std::string{"argument '"} + key + "' is not a number");
      } else {
        args[key] = format_number(*number);
      }
    }
  }
  if (text("byres").has_value()) args["byres"] = flag("byres", false);
  if (const auto v = text("interpolation")) args["interpolation"] = lower(*v);
  emit("spectrum", std::move(args));
}

void StatementTranslation::translate_distance() {
  if (!bind({"name", "selection1", "selection2", "cutoff", "mode", "zoom", "width", "length",
             "gap", "label", "quiet", "reset", "state", "state1", "state2"})) {
    return;
  }
  if (!given("selection1") || !given("selection2")) {
    error("missing_argument", "picked atoms (pk1/pk2) are not available in scripts");
    return;
  }
  require_default("state", {"0"});
  require_default("state1", {"-3"});
  require_default("state2", {"-3"});
  for (const auto key : {"cutoff", "width", "length", "gap", "reset"}) {
    if (given(key) && !(std::string_view{key} == "reset" && numerically_equal(*text(key), "0"))) {
      error("unsupported_argument", std::string{"argument '"} + key +
                                        "' has no native equivalent");
    }
  }
  ignored_hint("zoom");
  if (text("label").has_value() && !numerically_equal(*text("label"), "1")) {
    warning("ignored_argument", "the measurement label is always created");
  }
  std::string mode = "atom";
  if (const auto value = text("mode")) {
    if (numerically_equal(*value, "0")) {
      mode = "atom";
    } else if (numerically_equal(*value, "4")) {
      mode = "centroid";
    } else {
      error("unsupported_argument", "distance mode " + *value + " has no native equivalent");
    }
  }
  Arguments args{{"from", expression("selection1")},
                 {"to", expression("selection2")}, {"mode", mode}};
  if (const auto name = text("name")) args["result-name"] = *name;
  emit("measure distance", std::move(args));
}

void StatementTranslation::translate_angle() {
  if (!bind({"name", "selection1", "selection2", "selection3", "mode", "label", "reset", "zoom",
             "state", "quiet", "state1", "state2", "state3"})) {
    return;
  }
  if (!given("selection1") || !given("selection2") || !given("selection3")) {
    error("missing_argument", "picked atoms (pk1..pk3) are not available in scripts");
    return;
  }
  require_default("mode", {"0"});
  require_default("state", {"0"});
  require_default("reset", {"0"});
  Arguments args{{"first", expression("selection1")},
                 {"vertex", expression("selection2")},
                 {"third", expression("selection3")}};
  if (const auto name = text("name")) args["result-name"] = *name;
  emit("measure angle", std::move(args));
}

void StatementTranslation::translate_dihedral() {
  if (!bind({"name", "selection1", "selection2", "selection3", "selection4", "mode", "label",
             "reset", "zoom", "state", "quiet"})) {
    return;
  }
  if (!given("selection1") || !given("selection2") || !given("selection3") ||
      !given("selection4")) {
    error("missing_argument", "picked atoms (pk1..pk4) are not available in scripts");
    return;
  }
  require_default("mode", {"0"});
  require_default("state", {"0"});
  require_default("reset", {"0"});
  Arguments args{{"first", expression("selection1")},
                 {"second", expression("selection2")},
                 {"third", expression("selection3")},
                 {"fourth", expression("selection4")}};
  if (const auto name = text("name")) args["result-name"] = *name;
  emit("measure dihedral", std::move(args));
}

void StatementTranslation::translate_cartoon() {
  if (!bind({"type", "selection"}) || !require("type")) return;
  emit("cartoon", {{"type", *text("type")}, {"selection", selection("selection")}});
}

void StatementTranslation::translate_scene() {
  if (!bind({"key", "action", "message", "view", "color", "active", "rep", "frame", "animate",
             "new_key", "hand", "quiet", "sele"})) {
    return;
  }
  for (const auto key : {"view", "color", "active", "rep", "frame"}) {
    require_default(key, {"1"});
  }
  if (given("message") || given("new_key")) {
    error("unsupported_argument", "scene messages and renaming are not supported");
  }
  const auto key = text("key").value_or("auto");
  const auto action = lower(text("action").value_or("recall"));
  static const std::set<std::string, std::less<>> reserved{"auto", "next", "prev", "*"};
  if (reserved.count(key) != 0U && action != "clear") {
    error("unsupported_argument", "scene key '" + key + "' is not supported");
    return;
  }
  if (action == "store") {
    emit("scene store", {{"name", key}});
  } else if (action == "recall") {
    emit("scene recall", {{"name", key}});
  } else if (action == "clear" || action == "delete") {
    emit("scene delete", {{"name", key}});
  } else {
    error("unsupported_argument", "scene action '" + action + "' is not supported");
  }
}

void StatementTranslation::translate_view() {
  if (!bind({"key", "action", "animate"}) || !require("key")) return;
  const auto key = *text("key");
  const auto action = lower(text("action").value_or("recall"));
  if (action == "store") {
    emit("view store", {{"name", key}});
  } else if (action == "recall") {
    emit("view recall", {{"name", key}, {"duration", duration("animate")}});
  } else if (action == "clear" || action == "delete") {
    emit("view delete", {{"name", key}});
  } else {
    error("unsupported_argument", "view action '" + action + "' is not supported");
  }
}

void StatementTranslation::translate_set_view() {
  if (!bind({"view", "animate", "quiet", "hand"}) || !require("view")) return;
  emit("view import-pymol", {{"values", trimmed(values_["view"])},
                             {"duration", duration("animate")}});
}

void StatementTranslation::translate_get_view() {
  if (!bind({"output", "quiet"})) return;
  emit("view export-pymol", {});
}

void StatementTranslation::translate_frame() {
  if (!bind({"frame", "trigger", "scene"}) || !require("frame")) return;
  require_default("trigger", {"-1"});
  require_default("scene", {"0"});
  const auto number = parse_number(*text("frame"));
  if (!number.has_value() || *number < 1.0 || *number != std::floor(*number)) {
    error("invalid_argument", "frame must be a positive integer");
    return;
  }
  emit("traj frame", {{"frame", format_number(*number - 1.0)}});
}

void StatementTranslation::translate_movie_map(bool append) {
  if (!bind({"specification", "frame", "freeze"})) return;
  require_default("freeze", {"0"});
  Arguments arguments{{"spec", text("specification").value_or("")}};
  if (const auto frame = text("frame"); frame.has_value()) {
    const auto number = parse_number(*frame);
    if (!number.has_value() || *number < 0.0 || *number != std::floor(*number)) {
      error("invalid_argument", "frame must be a non-negative integer");
      return;
    }
    // mset defaults to frame 1 (replace); madd defaults to 0 (append). 0 always means append.
    if (*number > 0.0) arguments["frame"] = format_number(*number);
    else append = true;
  }
  if (append) arguments["append"] = "true";
  emit("movie map", arguments);
}

void StatementTranslation::translate_mclear() {
  if (!bind({})) return;
  emit("movie clear", {});
}

void StatementTranslation::translate_mplay() {
  if (!bind({})) return;
  emit("movie play", {});
}

void StatementTranslation::translate_mstop() {
  if (!bind({})) return;
  emit("movie pause", {});
}

void StatementTranslation::translate_quit() {
  if (!bind({"code"})) return;
  out_.stop_script = true;
  info("script_stopped", "quit ends the script; the application keeps running");
}

void StatementTranslation::translate_refresh() {
  if (!bind({})) return;
  emit("view refresh", {});
}

void StatementTranslation::translate_recolor() {
  if (!bind({"selection", "representation"})) return;
  if (const auto sel = text("selection"); sel.has_value() && *sel != "all" && *sel != "(all)") {
    warning("selection_not_applied", "colors are rebuilt for the whole workspace");
  }
  emit("recolor", {});
}

// util.cnc colors every non-carbon atom by element and leaves carbon alone;
// util.cba<x> also gives the carbons one named color (PyMOL util.py).
void StatementTranslation::translate_util_atomic_colors() {
  static const std::map<char, const char*> carbon_colors{
      {'b', "slate"}, {'c', "cyan"}, {'g', "carbon"}, {'k', "pink"},
      {'m', "lightmagenta"}, {'o', "brightorange"}, {'p', "purple"},
      {'s', "salmon"}, {'w', "hydrogen"}, {'y', "yellow"}};
  if (!bind({"selection", "quiet"})) return;
  const auto scope = selection("selection");
  emit("color", {{"color", "atomic"}, {"selection", "((" + scope + ") and not elem C)"},
                 {"scope", "workspace"}});
  if (statement_.command != "util.cnc") {
    const auto found = carbon_colors.find(statement_.command.back());
    if (found == carbon_colors.end()) return;
    emit("color", {{"color", found->second}, {"selection", "(elem C and (" + scope + "))"},
                   {"scope", "workspace"}});
  }
}

void StatementTranslation::translate_set_color() {
  if (!bind({"name", "rgb", "mode", "quiet"}) || !require("name") || !require("rgb")) return;
  require_default("mode", {"0"});
  auto rgb = *text("rgb");
  rgb.erase(std::remove_if(rgb.begin(), rgb.end(),
                           [](char c) { return c == '[' || c == ']' || c == '(' || c == ')'; }),
            rgb.end());
  std::vector<double> parts;
  std::size_t start = 0;
  while (start <= rgb.size()) {
    const auto comma = rgb.find(',', start);
    const auto piece = rgb.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    const auto number = parse_number(piece);
    if (!number.has_value()) {
      error("invalid_argument", "rgb needs three numbers");
      return;
    }
    parts.push_back(*number);
    if (comma == std::string::npos) break;
    start = comma + 1U;
  }
  if (parts.size() != 3U) {
    error("invalid_argument", "rgb needs exactly three numbers");
    return;
  }
  emit("color define", {{"name", *text("name")}, {"red", format_number(parts[0])},
                        {"green", format_number(parts[1])}, {"blue", format_number(parts[2])}});
}

void StatementTranslation::run() {
  switch (statement_.kind) {
    case StatementKind::unknown_command:
      return;  // the parser already reported it
    case StatementKind::python_statement:
    case StatementKind::python_block:
      return;  // the parser already reported that this needs Python trust
    case StatementKind::include_script:
      error("requires_python_trust",
            "script includes (@file) are not run by the PML adapter");
      return;
    case StatementKind::skip_block:
      info("skip_block", "lines inside the skip block are not run");
      return;
    case StatementKind::embed_block:
      return;
    case StatementKind::abort:
      out_.stop_script = true;
      info("script_aborted", "abort ends the script");
      return;
    case StatementKind::command:
      break;
  }
  static const std::unordered_map<std::string, Handler> handlers{
      {"load", &StatementTranslation::translate_load},
      {"load_traj", &StatementTranslation::translate_load_traj},
      {"save", &StatementTranslation::translate_save},
      {"show", &StatementTranslation::translate_show},
      {"hide", &StatementTranslation::translate_hide},
      {"as", &StatementTranslation::translate_as},
      {"show_as", &StatementTranslation::translate_as},
      {"toggle", &StatementTranslation::translate_toggle},
      {"color", &StatementTranslation::translate_color},
      {"select", &StatementTranslation::translate_select},
      {"zoom", &StatementTranslation::translate_zoom},
      {"center", &StatementTranslation::translate_center},
      {"orient", &StatementTranslation::translate_orient},
      {"reset", &StatementTranslation::translate_reset},
      {"turn", &StatementTranslation::translate_turn},
      {"move", &StatementTranslation::translate_move},
      {"clip", &StatementTranslation::translate_clip},
      {"origin", &StatementTranslation::translate_origin},
      {"set", &StatementTranslation::translate_set},
      {"unset", &StatementTranslation::translate_unset},
      {"bg_color", &StatementTranslation::translate_bg_color},
      {"bg_colour", &StatementTranslation::translate_bg_color},
      {"png", &StatementTranslation::translate_png},
      {"label", &StatementTranslation::translate_label},
      {"delete", &StatementTranslation::translate_delete},
      {"group", &StatementTranslation::translate_group},
      {"enable", &StatementTranslation::translate_enable},
      {"disable", &StatementTranslation::translate_disable},
      {"spectrum", &StatementTranslation::translate_spectrum},
      {"distance", &StatementTranslation::translate_distance},
      {"angle", &StatementTranslation::translate_angle},
      {"dihedral", &StatementTranslation::translate_dihedral},
      {"cartoon", &StatementTranslation::translate_cartoon},
      {"scene", &StatementTranslation::translate_scene},
      {"view", &StatementTranslation::translate_view},
      {"set_view", &StatementTranslation::translate_set_view},
      {"get_view", &StatementTranslation::translate_get_view},
      {"frame", &StatementTranslation::translate_frame},
      {"mplay", &StatementTranslation::translate_mplay},
      {"mset", &StatementTranslation::translate_mset},
      {"madd", &StatementTranslation::translate_madd},
      {"mclear", &StatementTranslation::translate_mclear},
      {"mstop", &StatementTranslation::translate_mstop},
      {"quit", &StatementTranslation::translate_quit},
      {"exit", &StatementTranslation::translate_quit},
      {"refresh", &StatementTranslation::translate_refresh},
      {"recolor", &StatementTranslation::translate_recolor},
      {"util.cbab", &StatementTranslation::translate_util_atomic_colors},
      {"util.cbac", &StatementTranslation::translate_util_atomic_colors},
      {"util.cbag", &StatementTranslation::translate_util_atomic_colors},
      {"util.cbak", &StatementTranslation::translate_util_atomic_colors},
      {"util.cbam", &StatementTranslation::translate_util_atomic_colors},
      {"util.cbao", &StatementTranslation::translate_util_atomic_colors},
      {"util.cbap", &StatementTranslation::translate_util_atomic_colors},
      {"util.cbas", &StatementTranslation::translate_util_atomic_colors},
      {"util.cbaw", &StatementTranslation::translate_util_atomic_colors},
      {"util.cbay", &StatementTranslation::translate_util_atomic_colors},
      {"util.cnc", &StatementTranslation::translate_util_atomic_colors},
      {"set_color", &StatementTranslation::translate_set_color},
      {"run", &StatementTranslation::translate_code_execution},
      {"spawn", &StatementTranslation::translate_code_execution},
      {"fork", &StatementTranslation::translate_code_execution},
      {"system", &StatementTranslation::translate_code_execution},
  };
  if (const auto found = handlers.find(statement_.command); found != handlers.end()) {
    (this->*(found->second))();
    if (out_.has_error()) out_.invocations.clear();
    return;
  }
  const auto& reasons = unsupported_reasons();
  const auto reason = reasons.find(statement_.command);
  error("unsupported_command",
        "PyMOL command '" + statement_.command + "': " +
            (reason != reasons.end() ? reason->second
                                     : std::string{"no native operation maps to it"}));
}

TranslatedStatement Translator::translate(const Statement& statement) {
  TranslatedStatement out;
  out.line = statement.line;
  out.text = statement.text;
  out.kind = statement.kind;
  out.command = statement.command;
  out.diagnostics = statement.diagnostics;
  StatementTranslation{*this, statement, out}.run();
  return out;
}

Translation translate_script(const Script& script, TranslationContext context) {
  Translation translation;
  translation.parse_diagnostics = script.diagnostics;
  Translator translator{std::move(context)};
  for (const auto& statement : script.statements) {
    auto translated = translator.translate(statement);
    translation.invocation_count += translated.invocations.size();
    if (translated.has_error()) {
      ++translation.failed_statements;
    } else if (!translated.invocations.empty()) {
      ++translation.translated_statements;
    }
    const bool stop = translated.stop_script;
    translation.statements.push_back(std::move(translated));
    if (stop) {
      translation.stopped = true;
      break;  // later statements never run
    }
  }
  return translation;
}

}  // namespace molshredder::automation::pml
