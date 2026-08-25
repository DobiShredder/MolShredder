#include "molshredder/render/setting_store.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>
#include <utility>

namespace molshredder::render {
namespace {

operation::Error invalid(std::string message) {
  return {operation::ErrorCode::invalid_argument, std::move(message), {}};
}

constexpr int rank(RenderSettingScopeLevel level) {
  return static_cast<int>(level);
}

RenderSettingDefinition definition(
    std::uint32_t id, std::string name, RenderSettingValueType type,
    RenderSettingValue default_value, RenderSettingScopeLevel maximum_scope,
    std::optional<double> minimum = std::nullopt,
    std::optional<double> maximum = std::nullopt,
    bool minimum_exclusive = false, std::string unit = {}) {
  return {id, std::move(name), type, std::move(default_value), minimum, maximum,
          minimum_exclusive, maximum_scope, std::move(unit)};
}

bool valid_scope_shape(const RenderSettingScope &scope) {
  switch (scope.level) {
  case RenderSettingScopeLevel::global:
    return scope.object_id == 0U && scope.state_index == 0U &&
           scope.atom_id == 0U && scope.bond_id == 0U;
  case RenderSettingScopeLevel::object:
    return scope.object_id != 0U && scope.state_index == 0U &&
           scope.atom_id == 0U && scope.bond_id == 0U;
  case RenderSettingScopeLevel::object_state:
    return scope.object_id != 0U && scope.atom_id == 0U &&
           scope.bond_id == 0U;
  case RenderSettingScopeLevel::atom:
    return scope.object_id != 0U && scope.atom_id != 0U &&
           scope.bond_id == 0U;
  case RenderSettingScopeLevel::bond:
    return scope.object_id != 0U && scope.atom_id == 0U &&
           scope.bond_id != 0U;
  }
  return false;
}

bool matches_type(RenderSettingValueType type, const RenderSettingValue &value) {
  switch (type) {
  case RenderSettingValueType::boolean:
    return std::holds_alternative<bool>(value);
  case RenderSettingValueType::integer:
    return std::holds_alternative<std::int64_t>(value);
  case RenderSettingValueType::number:
    return std::holds_alternative<double>(value);
  case RenderSettingValueType::color:
    return std::holds_alternative<std::string>(value);
  }
  return false;
}

std::optional<operation::Error>
validate_value(const RenderSettingDefinition &definition,
               const RenderSettingValue &value) {
  if (!matches_type(definition.value_type, value))
    return invalid("render setting value type mismatch: " + definition.name);
  if (const auto *color = std::get_if<std::string>(&value)) {
    if (color->empty())
      return invalid("render setting color must not be empty");
    constexpr std::array<std::string_view, 17U> names{
        "-1", "atom", "atomic", "black", "blue", "carbon", "cyan",
        "green", "hydrogen", "magenta", "nitrogen", "orange", "oxygen",
        "red", "sulfur", "white", "yellow"};
    const auto hex = color->size() == 7U && color->front() == '#' &&
                     std::all_of(color->begin() + 1, color->end(), [](char item) {
                       return std::isxdigit(
                                  static_cast<unsigned char>(item)) != 0;
                     });
    if (!hex && std::find(names.begin(), names.end(), *color) == names.end())
      return invalid("unsupported render setting color token: " + *color);
    return std::nullopt;
  }
  double numeric{};
  if (const auto *number = std::get_if<double>(&value)) {
    if (!std::isfinite(*number))
      return invalid("render setting number must be finite");
    numeric = *number;
  } else if (const auto *integer = std::get_if<std::int64_t>(&value)) {
    numeric = static_cast<double>(*integer);
  } else {
    return std::nullopt;
  }
  if (definition.minimum.has_value() &&
      (definition.minimum_exclusive ? numeric <= *definition.minimum
                                    : numeric < *definition.minimum)) {
    return invalid("render setting value is below its minimum: " +
                   definition.name);
  }
  if (definition.maximum.has_value() && numeric > *definition.maximum) {
    return invalid("render setting value is above its maximum: " +
                   definition.name);
  }
  return std::nullopt;
}

std::string_view scope_name(RenderSettingScopeLevel level) {
  switch (level) {
  case RenderSettingScopeLevel::global:
    return "global";
  case RenderSettingScopeLevel::object:
    return "object";
  case RenderSettingScopeLevel::object_state:
    return "object_state";
  case RenderSettingScopeLevel::atom:
    return "atom";
  case RenderSettingScopeLevel::bond:
    return "bond";
  }
  return "unknown";
}

std::optional<RenderSettingScopeLevel> parse_scope(std::string_view name) {
  if (name == "global")
    return RenderSettingScopeLevel::global;
  if (name == "object")
    return RenderSettingScopeLevel::object;
  if (name == "object_state")
    return RenderSettingScopeLevel::object_state;
  if (name == "atom")
    return RenderSettingScopeLevel::atom;
  if (name == "bond")
    return RenderSettingScopeLevel::bond;
  return std::nullopt;
}

char value_tag(RenderSettingValueType type) {
  switch (type) {
  case RenderSettingValueType::boolean:
    return 'b';
  case RenderSettingValueType::integer:
    return 'i';
  case RenderSettingValueType::number:
    return 'f';
  case RenderSettingValueType::color:
    return 'c';
  }
  return '?';
}

}  // namespace

const std::vector<RenderSettingDefinition> &p0_render_setting_definitions() {
  using enum RenderSettingScopeLevel;
  using enum RenderSettingValueType;
  static const std::vector<RenderSettingDefinition> definitions{
      definition(43, "line_smooth", boolean, true, global),
      definition(44, "line_width", number, 1.49, bond, 0.0, std::nullopt,
                 true, "pixel"),
      definition(110, "line_radius", number, 0.0, object_state, 0.0,
                 std::nullopt, false, "angstrom"),
      definition(391, "line_stick_helper", boolean, true, object_state),
      definition(526, "line_color", color, std::string{"-1"}, bond),
      definition(645, "line_use_shader", boolean, true, global),
      definition(679, "line_as_cylinders", boolean, false, global),
      definition(21, "stick_radius", number, 0.25, bond, 0.0, std::nullopt,
                 true, "angstrom"),
      definition(46, "stick_quality", integer, std::int64_t{8}, object_state,
                 3.0, 100.0),
      definition(47, "stick_overlap", number, 0.2, object_state, 0.0),
      definition(48, "stick_nub", number, 0.7, object_state, 0.0),
      definition(198, "stick_transparency", number, 0.0, bond, 0.0, 1.0),
      definition(276, "stick_ball", boolean, false, atom),
      definition(277, "stick_ball_ratio", number, 1.0, object_state, 0.0),
      definition(278, "stick_fixed_radius", boolean, false, object_state),
      definition(376, "stick_color", color, std::string{"-1"}, bond),
      definition(512, "stick_valence_scale", number, 1.0, object_state, 0.0),
      definition(604, "stick_ball_color", color, std::string{"-1"},
                 object_state),
      definition(605, "stick_h_scale", number, 0.4, object_state, 0.0,
                 std::nullopt, true),
      definition(644, "stick_use_shader", boolean, true, global),
      definition(673, "stick_debug", integer, std::int64_t{0}, global, 0.0),
      definition(675, "stick_round_nub", integer, std::int64_t{0},
                 object_state, 0.0),
      definition(676, "stick_good_geometry", integer, std::int64_t{0},
                 object_state, 0.0),
      definition(677, "stick_as_cylinders", boolean, true, global),
      definition(87, "sphere_quality", integer, std::int64_t{1}, object_state,
                 0.0, 4.0),
      definition(155, "sphere_scale", number, 1.0, atom, 0.0, std::nullopt,
                 true),
      definition(172, "sphere_transparency", number, 0.0, atom, 0.0, 1.0),
      definition(173, "sphere_color", color, std::string{"-1"}, atom),
      definition(203, "sphere_solvent", boolean, false, object_state),
      definition(421, "sphere_mode", integer, std::int64_t{9}, object_state,
                 -1.0, 11.0),
      definition(422, "sphere_point_max_size", number, 18.0, object_state,
                 0.0, std::nullopt, true, "pixel"),
      definition(423, "sphere_point_size", number, 1.0, global, 0.0,
                 std::nullopt, true, "pixel"),
      definition(646, "sphere_use_shader", boolean, true, global),
      definition(45, "half_bonds", boolean, false, object_state),
      definition(560, "hide_long_bonds", boolean, false, object_state),
      definition(64, "valence", boolean, true, bond),
      definition(135, "valence_size", number, 0.060, object_state, 0.0),
      definition(616, "valence_mode", integer, std::int64_t{1}, object_state,
                 0.0, 1.0),
      definition(752, "valence_zero_scale", number, 0.2, object_state, 0.0),
      definition(753, "valence_zero_mode", integer, std::int64_t{1},
                 object_state, 0.0, 2.0),
      definition(51, "auto_show_lines", boolean, true, global),
      definition(72, "auto_show_nonbonded", boolean, true, global),
      definition(420, "auto_show_spheres", boolean, false, global),
  };
  return definitions;
}

operation::Result<RenderSettingStore> RenderSettingStore::create() {
  const auto &definitions = p0_render_setting_definitions();
  if (definitions.size() != 43U)
    return operation::Result<RenderSettingStore>::failure(
        invalid("P0 render setting catalog size mismatch"));
  for (std::size_t index = 0; index < definitions.size(); ++index) {
    if (definitions[index].name.empty() || definitions[index].stable_id == 0U ||
        validate_value(definitions[index], definitions[index].default_value)
            .has_value()) {
      return operation::Result<RenderSettingStore>::failure(
          invalid("invalid P0 render setting definition"));
    }
    for (std::size_t other = index + 1U; other < definitions.size(); ++other) {
      if (definitions[index].name == definitions[other].name ||
          definitions[index].stable_id == definitions[other].stable_id) {
        return operation::Result<RenderSettingStore>::failure(
            invalid("duplicate P0 render setting definition"));
      }
    }
  }
  return operation::Result<RenderSettingStore>::success(RenderSettingStore{});
}

const RenderSettingDefinition *
RenderSettingStore::definition(std::string_view name) const noexcept {
  const auto &definitions = p0_render_setting_definitions();
  const auto found = std::find_if(definitions.begin(), definitions.end(),
                                  [name](const auto &item) {
                                    return item.name == name;
                                  });
  return found == definitions.end() ? nullptr : &*found;
}

std::optional<operation::Error>
RenderSettingStore::set(std::string_view name, const RenderSettingScope &scope,
                        RenderSettingValue value) {
  const auto *item = definition(name);
  if (item == nullptr)
    return invalid("unknown render setting: " + std::string{name});
  if (!valid_scope_shape(scope) ||
      rank(scope.level) > rank(item->maximum_scope))
    return invalid("render setting scope is not allowed: " + std::string{name});
  if (const auto error = validate_value(*item, value); error.has_value())
    return error;
  auto next = overrides_;
  next.insert_or_assign(Key{std::string{name}, scope}, std::move(value));
  overrides_ = std::move(next);
  return std::nullopt;
}

operation::Result<bool>
RenderSettingStore::unset(std::string_view name,
                          const RenderSettingScope &scope) {
  if (definition(name) == nullptr)
    return operation::Result<bool>::failure(
        invalid("unknown render setting: " + std::string{name}));
  if (!valid_scope_shape(scope))
    return operation::Result<bool>::failure(invalid("invalid render setting scope"));
  return operation::Result<bool>::success(
      overrides_.erase(Key{std::string{name}, scope}) != 0U);
}

operation::Result<std::size_t>
RenderSettingStore::reset_scope(const RenderSettingScope &scope) {
  if (!valid_scope_shape(scope))
    return operation::Result<std::size_t>::failure(
        invalid("invalid render setting scope"));
  const auto previous = overrides_.size();
  std::erase_if(overrides_, [&scope](const auto &item) {
    return item.first.second == scope;
  });
  return operation::Result<std::size_t>::success(previous - overrides_.size());
}

operation::Result<ResolvedRenderSetting>
RenderSettingStore::resolve(std::string_view name,
                            const RenderSettingContext &context) const {
  const auto *item = definition(name);
  if (item == nullptr)
    return operation::Result<ResolvedRenderSetting>::failure(
        invalid("unknown render setting: " + std::string{name}));
  std::vector<RenderSettingScope> scopes;
  if (context.bond_id != 0U)
    scopes.push_back({RenderSettingScopeLevel::bond, context.object_id,
                      context.state_index, 0U, context.bond_id});
  if (context.atom_id != 0U)
    scopes.push_back({RenderSettingScopeLevel::atom, context.object_id,
                      context.state_index, context.atom_id, 0U});
  if (context.object_id != 0U) {
    scopes.push_back({RenderSettingScopeLevel::object_state, context.object_id,
                      context.state_index, 0U, 0U});
    scopes.push_back(
        {RenderSettingScopeLevel::object, context.object_id, 0U, 0U, 0U});
  }
  scopes.push_back({});
  for (const auto &scope : scopes) {
    if (rank(scope.level) > rank(item->maximum_scope))
      continue;
    const auto found = overrides_.find(Key{std::string{name}, scope});
    if (found != overrides_.end())
      return operation::Result<ResolvedRenderSetting>::success(
          {found->second, scope});
  }
  return operation::Result<ResolvedRenderSetting>::success(
      {item->default_value, std::nullopt});
}

RenderSettingSnapshot RenderSettingStore::snapshot() const {
  RenderSettingSnapshot snapshot;
  snapshot.overrides.reserve(overrides_.size());
  for (const auto &[key, value] : overrides_)
    snapshot.overrides.push_back({key.first, key.second, value});
  return snapshot;
}

operation::Result<RenderSettingStore>
RenderSettingStore::restore(const RenderSettingSnapshot &snapshot) {
  if (snapshot.schema_version != kRenderSettingSnapshotSchemaVersion ||
      snapshot.catalog_revision != kRenderSettingCatalogRevision)
    return operation::Result<RenderSettingStore>::failure(
        invalid("unsupported render setting snapshot schema or catalog"));
  auto created = create();
  if (!created.has_value())
    return created;
  auto store = std::move(created.value());
  for (const auto &item : snapshot.overrides) {
    const auto key = Key{item.name, item.scope};
    if (store.overrides_.contains(key))
      return operation::Result<RenderSettingStore>::failure(
          invalid("duplicate render setting override"));
    if (const auto error = store.set(item.name, item.scope, item.value);
        error.has_value())
      return operation::Result<RenderSettingStore>::failure(*error);
  }
  return operation::Result<RenderSettingStore>::success(std::move(store));
}

operation::Result<std::string>
serialize_render_settings(const RenderSettingSnapshot &snapshot) {
  const auto restored = RenderSettingStore::restore(snapshot);
  if (!restored.has_value())
    return operation::Result<std::string>::failure(restored.error());
  const auto canonical = restored.value().snapshot();
  std::ostringstream output;
  output << "molshredder-render-settings " << canonical.schema_version << '\n';
  output << "catalog " << std::quoted(canonical.catalog_revision) << '\n';
  output << "count " << canonical.overrides.size() << '\n';
  output << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (const auto &item : canonical.overrides) {
    const auto *definition = restored.value().definition(item.name);
    output << "entry " << std::quoted(item.name) << ' '
           << scope_name(item.scope.level) << ' ' << item.scope.object_id << ' '
           << item.scope.state_index << ' ' << item.scope.atom_id << ' '
           << item.scope.bond_id << ' ' << value_tag(definition->value_type)
           << ' ';
    std::visit(
        [&output](const auto &value) {
          using Value = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<Value, bool>)
            output << (value ? "true" : "false");
          else if constexpr (std::is_same_v<Value, std::string>)
            output << std::quoted(value);
          else
            output << value;
        },
        item.value);
    output << '\n';
  }
  return operation::Result<std::string>::success(output.str());
}

operation::Result<RenderSettingSnapshot>
parse_render_settings(std::string_view text) {
  std::istringstream input{std::string{text}};
  std::string label;
  unsigned int schema{};
  if (!(input >> label >> schema) || label != "molshredder-render-settings")
    return operation::Result<RenderSettingSnapshot>::failure(
        invalid("invalid render setting snapshot header"));
  std::string revision;
  if (!(input >> label >> std::quoted(revision)) || label != "catalog")
    return operation::Result<RenderSettingSnapshot>::failure(
        invalid("missing render setting catalog revision"));
  std::size_t count{};
  if (!(input >> label >> count) || label != "count")
    return operation::Result<RenderSettingSnapshot>::failure(
        invalid("missing render setting override count"));
  RenderSettingSnapshot snapshot{schema, std::move(revision), {}};
  snapshot.overrides.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    std::string name;
    std::string scope_token;
    RenderSettingScope scope;
    char tag{};
    if (!(input >> label >> std::quoted(name) >> scope_token >> scope.object_id >>
          scope.state_index >> scope.atom_id >> scope.bond_id >> tag) ||
        label != "entry")
      return operation::Result<RenderSettingSnapshot>::failure(
          invalid("truncated render setting override"));
    const auto level = parse_scope(scope_token);
    if (!level.has_value())
      return operation::Result<RenderSettingSnapshot>::failure(
          invalid("invalid render setting scope token"));
    scope.level = *level;
    RenderSettingValue value;
    if (tag == 'b') {
      std::string token;
      if (!(input >> token) || (token != "true" && token != "false"))
        return operation::Result<RenderSettingSnapshot>::failure(
            invalid("invalid boolean render setting value"));
      value = token == "true";
    } else if (tag == 'i') {
      std::int64_t integer{};
      if (!(input >> integer))
        return operation::Result<RenderSettingSnapshot>::failure(
            invalid("invalid integer render setting value"));
      value = integer;
    } else if (tag == 'f') {
      double number{};
      if (!(input >> number))
        return operation::Result<RenderSettingSnapshot>::failure(
            invalid("invalid numeric render setting value"));
      value = number;
    } else if (tag == 'c') {
      std::string color;
      if (!(input >> std::quoted(color)))
        return operation::Result<RenderSettingSnapshot>::failure(
            invalid("invalid color render setting value"));
      value = std::move(color);
    } else {
      return operation::Result<RenderSettingSnapshot>::failure(
          invalid("invalid render setting value tag"));
    }
    snapshot.overrides.push_back({std::move(name), scope, std::move(value)});
  }
  if (input >> label)
    return operation::Result<RenderSettingSnapshot>::failure(
        invalid("unexpected trailing render setting snapshot data"));
  const auto restored = RenderSettingStore::restore(snapshot);
  if (!restored.has_value())
    return operation::Result<RenderSettingSnapshot>::failure(restored.error());
  return operation::Result<RenderSettingSnapshot>::success(
      restored.value().snapshot());
}

}  // namespace molshredder::render
