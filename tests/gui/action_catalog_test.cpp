#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <string_view>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/automation/python_script.hpp"
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

  auto registry = molshredder::application::make_default_registry();
  passed &= expect(
      !molshredder::automation::register_python_script_command(registry)
           .has_value(),
      "desktop automation command registration must succeed");
  const auto descriptors = registry.descriptors();
  const auto has_command = [&descriptors](const ActionMetadata* action) {
    return action != nullptr &&
           std::ranges::any_of(descriptors, [action](const auto& descriptor) {
             return descriptor.canonical_name == action->command_name;
           });
  };
  for (const auto id : {"file.save", "file.open-session",
                        "file.save-session", "trajectory.attach", "tools.run-script",
                        "edit.undo", "edit.redo", "edit.atom-position",
                        "edit.atom-properties", "edit.residue-properties",
                        "edit.bond-order",
                        "build.molecule",
                        "represent.lines", "represent.sticks",
                        "represent.spheres", "represent.ribbon",
                        "represent.cartoon", "represent.surface",
                        "represent.settings",
                        "represent.volume-slice",
                        "represent.volume-render",
                        "analyze.open-panel", "scene.views",
                        "scene.named-scenes", "scene.movie",
                        "help.system-information", "object.panel",
                        "object.chemistry",
                        "select.expression", "select.all",
                        "trajectory.play-pause"}) {
    const auto* action = molshredder::gui::find_action_metadata(id);
    passed &= expect(has_command(action),
                     std::string{id} + " must route to a canonical command");
  }
  const auto* save = molshredder::gui::find_action_metadata("file.save");
  const auto* attach =
      molshredder::gui::find_action_metadata("trajectory.attach");
  const auto* script =
      molshredder::gui::find_action_metadata("tools.run-script");
  const auto* open_session =
      molshredder::gui::find_action_metadata("file.open-session");
  const auto* save_session =
      molshredder::gui::find_action_metadata("file.save-session");
  const auto workspace = molshredder::gui::requirement_mask(
      molshredder::gui::ActionRequirement::workspace);
  const auto panel = surface_mask(ActionSurface::panel);
  passed &= expect(save != nullptr && save->command_name == "save" &&
                       save->requirements == workspace,
                   "file.save metadata or availability drifted");
  passed &= expect(attach != nullptr && attach->command_name == "traj load" &&
                       attach->requirements == workspace,
                   "trajectory.attach metadata or availability drifted");
  passed &= expect(script != nullptr && script->command_name == "script run" &&
                       script->surfaces ==
                           (surface_mask(ActionSurface::menu) |
                            surface_mask(ActionSurface::command_palette)),
                   "tools.run-script metadata or compact-toolbar policy drifted");
  passed &= expect(open_session != nullptr &&
                       open_session->command_name == "session load" &&
                       open_session->requirements == 0U &&
                       (open_session->surfaces & panel) != 0U,
                   "session load must remain available before a workspace exists");
  passed &= expect(save_session != nullptr &&
                       save_session->command_name == "session save" &&
                       save_session->requirements == workspace &&
                       (save_session->surfaces & panel) != 0U,
                   "session save must use the workspace-aware canonical route");
  const auto* ribbon =
      molshredder::gui::find_action_metadata("represent.ribbon");
  passed &= expect(ribbon != nullptr && ribbon->checkable &&
                       ribbon->requirements == workspace &&
                       ribbon->surfaces ==
                           (surface_mask(ActionSurface::menu) |
                            surface_mask(ActionSurface::command_palette)),
                   "represent.ribbon metadata or compact-toolbar policy drifted");
  const auto *surface =
      molshredder::gui::find_action_metadata("represent.surface");
  passed &= expect(surface != nullptr && surface->checkable &&
                       surface->command_name == "surface show" &&
                       surface->alternate_command_name == "surface hide" &&
                       std::ranges::any_of(
                           descriptors, [](const auto &descriptor) {
                             return descriptor.canonical_name == "surface hide";
                           }) &&
                       surface->requirements == workspace &&
                       (surface->surfaces &
                        surface_mask(ActionSurface::panel)) != 0U,
                   "represent.surface must expose the canonical parameter panel route");
  const auto* analyze =
      molshredder::gui::find_action_metadata("analyze.open-panel");
  passed &= expect(analyze != nullptr && analyze->command_name == "analyze center" &&
                       (analyze->surfaces & panel) != 0U &&
                       analyze->requirements == workspace,
                   "analyze panel route, surface, or availability drifted");
  const auto* named_scenes =
      molshredder::gui::find_action_metadata("scene.named-scenes");
  const auto* movie =
      molshredder::gui::find_action_metadata("scene.movie");
  passed &= expect(named_scenes != nullptr && movie != nullptr &&
                       named_scenes->command_name == "scene list" &&
                       movie->command_name == "movie status" &&
                       named_scenes->requirements == workspace &&
                       movie->requirements == workspace &&
                       (named_scenes->surfaces & panel) != 0U &&
                       (movie->surfaces & panel) != 0U,
                   "named-scene and movie actions must share canonical panel routes");
  const auto *volume_slice =
      molshredder::gui::find_action_metadata("represent.volume-slice");
  const auto volume = molshredder::gui::requirement_mask(
      molshredder::gui::ActionRequirement::volume);
  passed &= expect(volume_slice != nullptr &&
                       volume_slice->command_name == "volume slice" &&
                       (volume_slice->surfaces & panel) != 0U &&
                       volume_slice->requirements == volume,
                   "volume slice panel route or availability drifted");
  const auto* select_all =
      molshredder::gui::find_action_metadata("select.all");
  const auto* select_expression =
      molshredder::gui::find_action_metadata("select.expression");
  passed &= expect(select_expression != nullptr &&
                       select_expression->command_name == "select" &&
                       (select_expression->surfaces & panel) != 0U &&
                       select_expression->requirements == workspace,
                   "selection expression panel route or availability drifted");
  const auto context_menu = surface_mask(ActionSurface::context_menu);
  passed &= expect(select_all != nullptr && select_all->command_name == "select" &&
                       (select_all->surfaces & context_menu) != 0U &&
                       select_all->requirements == workspace,
                   "select-all context route or availability drifted");
  const auto* playback =
      molshredder::gui::find_action_metadata("trajectory.play-pause");
  const auto trajectory = molshredder::gui::requirement_mask(
      molshredder::gui::ActionRequirement::trajectory);
  passed &= expect(playback != nullptr && playback->command_name == "traj play" &&
                       playback->alternate_command_name == "traj pause" &&
                       std::ranges::any_of(
                           descriptors, [](const auto& descriptor) {
                             return descriptor.canonical_name == "traj pause";
                           }) &&
                       playback->checkable &&
                       playback->requirements == trajectory,
                   "trajectory playback context metadata drifted");
  for (const auto id : {"represent.show", "represent.hide", "represent.as",
                        "represent.toggle"}) {
    const auto* visibility = molshredder::gui::find_action_metadata(id);
    passed &= expect(visibility != nullptr && has_command(visibility) &&
                         (visibility->surfaces & context_menu) != 0U &&
                         visibility->requirements == workspace,
                     std::string{id} +
                         " must use a workspace-aware canonical context route");
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
  ActionMetadata missing_palette = catalog.front();
  missing_palette.surfaces = surface_mask(ActionSurface::menu);
  passed &= expect(
      !molshredder::gui::validate_action_catalog(
          std::span{&missing_palette, 1U}, &error) &&
          error ==
              "searchable action is missing command-palette surface: file.open",
      "every catalog action must remain searchable in the command palette");
  ActionMetadata missing_error = catalog.front();
  missing_error.error_source = "";
  passed &= expect(
      !molshredder::gui::validate_action_catalog(
          std::span{&missing_error, 1U}, &error) &&
          error == "action metadata contains an empty required field: file.open",
      "every action must provide a localizable failure summary");
  ActionMetadata invalid_alternate = catalog.front();
  invalid_alternate.alternate_command_name = "close";
  passed &= expect(
      !molshredder::gui::validate_action_catalog(
          std::span{&invalid_alternate, 1U}, &error) &&
          error == "alternate command requires checkable action: file.open",
      "alternate invocation metadata must describe a checkable action");
  ActionMetadata missing_reason = catalog[1];
  missing_reason.unavailable_source = "";
  passed &= expect(!molshredder::gui::validate_action_catalog(
                       std::span{&missing_reason, 1U}, &error) &&
                       error ==
                           "required action has no unavailable reason: file.save",
                   "required actions without remediation must fail");
  return passed ? 0 : 1;
}
