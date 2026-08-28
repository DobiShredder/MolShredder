#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace molshredder::gui {

enum class ActionSurface : std::uint8_t {
  menu = 1U << 0U,
  toolbar = 1U << 1U,
  command_palette = 1U << 2U,
  panel = 1U << 3U,
  context_menu = 1U << 4U,
};

using ActionSurfaces = std::uint8_t;

enum class ActionRequirement : std::uint8_t {
  workspace = 1U << 0U,
  selection = 1U << 1U,
  trajectory = 1U << 2U,
  volume = 1U << 3U,
};

using ActionRequirements = std::uint8_t;

[[nodiscard]] constexpr ActionSurfaces surface_mask(ActionSurface surface) {
  return static_cast<ActionSurfaces>(surface);
}

[[nodiscard]] constexpr ActionRequirements requirement_mask(
    ActionRequirement requirement) {
  return static_cast<ActionRequirements>(requirement);
}

struct ActionMetadata {
  std::string_view id;
  std::string_view command_name;
  std::string_view alternate_command_name{};
  std::string_view menu;
  std::string_view label_source;
  std::string_view status_source;
  std::string_view error_source;
  std::string_view keywords_source;
  std::string_view unavailable_source;
  std::string_view shortcut;
  std::string_view help_target;
  std::string_view parameter_group;
  std::uint16_t order{};
  ActionSurfaces surfaces{};
  ActionRequirements requirements{};
  bool checkable{};
};

[[nodiscard]] std::span<const ActionMetadata> default_action_catalog();
[[nodiscard]] const ActionMetadata* find_action_metadata(std::string_view id);
[[nodiscard]] bool validate_action_catalog(
    std::span<const ActionMetadata> actions, std::string* error = nullptr);

}  // namespace molshredder::gui
