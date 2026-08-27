#include "molshredder/gui/action_catalog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <tuple>
#include <utility>

namespace molshredder::gui {
namespace {

constexpr ActionSurfaces kFileOpenSurfaces =
    surface_mask(ActionSurface::menu) |
    surface_mask(ActionSurface::toolbar) |
    surface_mask(ActionSurface::command_palette);

constexpr std::array<ActionMetadata, 1U> kActions{{
    {.id = "file.open",
     .command_name = "load",
     .menu = "file",
     .label_source = "Open",
     .status_source =
         "Open one or more molecular structures or scalar volumes",
     .shortcut = "standard.open",
     .order = 10U,
     .surfaces = kFileOpenSurfaces,
     .checkable = false,
     .requires_workspace = false},
}};

bool valid_identifier(std::string_view value, bool allow_dot) {
  if (value.empty()) return false;
  return std::all_of(value.begin(), value.end(), [allow_dot](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return std::islower(byte) != 0 || std::isdigit(byte) != 0 ||
           character == '-' || character == '_' ||
           (allow_dot && character == '.');
  });
}

}  // namespace

std::span<const ActionMetadata> default_action_catalog() { return kActions; }

const ActionMetadata* find_action_metadata(std::string_view id) {
  const auto found = std::ranges::find(kActions, id, &ActionMetadata::id);
  return found == kActions.end() ? nullptr : &*found;
}

bool validate_action_catalog(std::span<const ActionMetadata> actions,
                             std::string* error) {
  const auto fail = [error](std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
  };
  std::set<std::string_view> identifiers;
  std::set<std::tuple<std::string_view, std::uint16_t>> menu_positions;
  constexpr auto known_surfaces = surface_mask(ActionSurface::menu) |
                                  surface_mask(ActionSurface::toolbar) |
                                  surface_mask(ActionSurface::command_palette);
  for (const auto& action : actions) {
    if (!valid_identifier(action.id, true))
      return fail("invalid action id: " + std::string{action.id});
    if (!valid_identifier(action.menu, false))
      return fail("invalid action menu: " + std::string{action.menu});
    if (action.command_name.empty() || action.label_source.empty() ||
        action.status_source.empty()) {
      return fail("action metadata contains an empty required field: " +
                  std::string{action.id});
    }
    if (action.surfaces == 0U || (action.surfaces & ~known_surfaces) != 0U)
      return fail("invalid action surfaces: " + std::string{action.id});
    if (!identifiers.insert(action.id).second)
      return fail("duplicate action id: " + std::string{action.id});
    if ((action.surfaces & surface_mask(ActionSurface::menu)) != 0U &&
        !menu_positions.emplace(action.menu, action.order).second) {
      return fail("duplicate menu position: " + std::string{action.menu});
    }
  }
  if (error != nullptr) error->clear();
  return true;
}

}  // namespace molshredder::gui
