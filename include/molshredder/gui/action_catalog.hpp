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
};

using ActionSurfaces = std::uint8_t;

[[nodiscard]] constexpr ActionSurfaces surface_mask(ActionSurface surface) {
  return static_cast<ActionSurfaces>(surface);
}

struct ActionMetadata {
  std::string_view id;
  std::string_view command_name;
  std::string_view menu;
  std::string_view label_source;
  std::string_view status_source;
  std::string_view shortcut;
  std::uint16_t order{};
  ActionSurfaces surfaces{};
  bool checkable{};
  bool requires_workspace{};
};

[[nodiscard]] std::span<const ActionMetadata> default_action_catalog();
[[nodiscard]] const ActionMetadata* find_action_metadata(std::string_view id);
[[nodiscard]] bool validate_action_catalog(
    std::span<const ActionMetadata> actions, std::string* error = nullptr);

}  // namespace molshredder::gui
