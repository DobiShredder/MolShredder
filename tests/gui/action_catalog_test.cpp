#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <string_view>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/gui/action_catalog.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

}  // namespace

int main() {
  using molshredder::gui::ActionMetadata;
  using molshredder::gui::ActionSurface;
  using molshredder::gui::surface_mask;
  bool passed = true;
  std::string error;
  const auto catalog = molshredder::gui::default_action_catalog();
  passed &= expect(molshredder::gui::validate_action_catalog(catalog, &error),
                   error);

  const auto* open = molshredder::gui::find_action_metadata("file.open");
  passed &= expect(open != nullptr, "file.open metadata must exist");
  if (open != nullptr) {
    passed &= expect(open->command_name == "load" && open->menu == "file" &&
                         open->label_source == "Open" &&
                         open->shortcut == "standard.open",
                     "file.open identity and canonical route drifted");
    const auto required_surfaces = surface_mask(ActionSurface::menu) |
                                   surface_mask(ActionSurface::toolbar) |
                                   surface_mask(ActionSurface::command_palette);
    passed &= expect(open->surfaces == required_surfaces,
                     "file.open must remain discoverable on all P0 surfaces");

    const auto registry = molshredder::application::make_default_registry();
    const auto descriptors = registry.descriptors();
    passed &= expect(
        std::ranges::any_of(descriptors, [open](const auto& descriptor) {
          return descriptor.canonical_name == open->command_name;
        }),
        "file.open command must exist in the canonical registry");
  }

  const std::array duplicate{catalog.front(), catalog.front()};
  passed &= expect(!molshredder::gui::validate_action_catalog(duplicate, &error) &&
                       error == "duplicate action id: file.open",
                   "duplicate action IDs must fail deterministically");

  ActionMetadata invalid_surface = catalog.front();
  invalid_surface.surfaces = 0U;
  passed &= expect(!molshredder::gui::validate_action_catalog(
                       std::span{&invalid_surface, 1U}, &error),
                   "actions without a discoverable surface must fail");
  return passed ? 0 : 1;
}
