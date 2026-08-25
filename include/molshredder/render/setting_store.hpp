#pragma once

#include <cstddef>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "molshredder/operation/result.hpp"

namespace molshredder::render {

// Schema 2 interprets atom_id/bond_id as persistent 64-bit topology IDs.
inline constexpr unsigned int kRenderSettingSnapshotSchemaVersion = 2U;
inline constexpr std::string_view kRenderSettingCatalogRevision =
    "pymol-oss-3.1.0-p0-v1";

enum class RenderSettingValueType { boolean, integer, number, color };
enum class RenderSettingScopeLevel {
  global,
  object,
  object_state,
  atom,
  bond
};

using RenderSettingValue =
    std::variant<bool, std::int64_t, double, std::string>;

struct RenderSettingScope {
  RenderSettingScopeLevel level{RenderSettingScopeLevel::global};
  std::uint64_t object_id{};
  std::size_t state_index{};
  std::uint64_t atom_id{};
  std::uint64_t bond_id{};

  friend auto operator<=>(const RenderSettingScope &,
                          const RenderSettingScope &) = default;
};

struct RenderSettingContext {
  std::uint64_t object_id{};
  std::size_t state_index{};
  std::uint64_t atom_id{};
  std::uint64_t bond_id{};
};

struct RenderSettingDefinition {
  std::uint32_t stable_id{};
  std::string name;
  RenderSettingValueType value_type{RenderSettingValueType::number};
  RenderSettingValue default_value{0.0};
  std::optional<double> minimum;
  std::optional<double> maximum;
  bool minimum_exclusive{};
  RenderSettingScopeLevel maximum_scope{RenderSettingScopeLevel::global};
  std::string unit;
};

struct RenderSettingOverride {
  std::string name;
  RenderSettingScope scope;
  RenderSettingValue value;

  friend bool operator==(const RenderSettingOverride &,
                         const RenderSettingOverride &) = default;
};

struct RenderSettingSnapshot {
  unsigned int schema_version{kRenderSettingSnapshotSchemaVersion};
  std::string catalog_revision{std::string{kRenderSettingCatalogRevision}};
  std::vector<RenderSettingOverride> overrides;

  friend bool operator==(const RenderSettingSnapshot &,
                         const RenderSettingSnapshot &) = default;
};

struct ResolvedRenderSetting {
  RenderSettingValue value;
  std::optional<RenderSettingScope> source_scope;
};

[[nodiscard]] const std::vector<RenderSettingDefinition> &
p0_render_setting_definitions();

class RenderSettingStore {
public:
  [[nodiscard]] static operation::Result<RenderSettingStore> create();
  [[nodiscard]] static operation::Result<RenderSettingStore>
  restore(const RenderSettingSnapshot &snapshot);

  [[nodiscard]] const RenderSettingDefinition *
  definition(std::string_view name) const noexcept;
  [[nodiscard]] std::optional<operation::Error>
  set(std::string_view name, const RenderSettingScope &scope,
      RenderSettingValue value);
  [[nodiscard]] operation::Result<bool>
  unset(std::string_view name, const RenderSettingScope &scope);
  [[nodiscard]] operation::Result<std::size_t>
  reset_scope(const RenderSettingScope &scope);
  [[nodiscard]] operation::Result<ResolvedRenderSetting>
  resolve(std::string_view name, const RenderSettingContext &context) const;
  [[nodiscard]] RenderSettingSnapshot snapshot() const;
  [[nodiscard]] std::size_t override_count() const noexcept {
    return overrides_.size();
  }

private:
  using Key = std::pair<std::string, RenderSettingScope>;
  std::map<Key, RenderSettingValue> overrides_;
};

[[nodiscard]] operation::Result<std::string>
serialize_render_settings(const RenderSettingSnapshot &snapshot);
[[nodiscard]] operation::Result<RenderSettingSnapshot>
parse_render_settings(std::string_view text);

}  // namespace molshredder::render
