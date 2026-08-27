#include <array>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/command/result.hpp"
#include "molshredder/gui/action_adapter.hpp"
#include "molshredder/operation/common_types.hpp"
#include "molshredder/operation/task_context.hpp"

int main(int argc, char* argv[]) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: gui_probe version|info|com|view|view-origin-position|projection|view-reset|clip|"
                 "get-clip|move|turn|stereo|pymol-view|state-all|orient-all|representation-visibility|render-setting "
                 "|object-lifecycle|object-chemistry|object-perception|object-perception-apply|object-chemistry-after-perception|load-batch "
                 "|selection-numeric|selection-numeric-error|selection-spatial|session-workflow "
                 "[fixture]\n";
    return 2;
  }

  molshredder::gui::Action action;
  const std::string_view scenario{argv[1]};
  if (scenario == "version") {
    action.command_name = "version";
  } else if (scenario == "info") {
    action.command_name = "system info";
  } else if (scenario == "com") {
    action.command_name = "com";
    action.parameters.emplace("selection", "protein");
  } else if (scenario == "view") {
    action.command_name = "view set";
    action.parameters = {{"distance", "25"},
                         {"far-clip", "500"},
                         {"near-clip", "0.5"},
                         {"projection", "orthographic"},
                         {"orthographic-height", "50"},
                         {"target-x", "3"},
                         {"target-y", "-2"},
                         {"target-z", "1"}};
  } else if (scenario == "view-origin-position") {
    action.command_name = "view origin";
    action.parameters = {{"position", "1.5,-2,3.25"}};
  } else if (scenario == "projection") {
    action.command_name = "view projection";
    action.parameters = {{"field-of-view-degrees", "60"},
                         {"mode", "orthographic"},
                         {"preserve-scale", "true"}};
  } else if (scenario == "view-reset") {
    action.command_name = "view reset";
  } else if (scenario == "clip") {
    action.command_name = "view clip";
    action.parameters = {{"distance", "2"}, {"mode", "near-set"}};
  } else if (scenario == "get-clip") {
    action.command_name = "view get-clip";
  } else if (scenario == "move") {
    action.command_name = "view move";
    action.parameters = {{"axis", "x"}, {"distance", "3"}};
  } else if (scenario == "turn") {
    action.command_name = "view turn";
    action.parameters = {{"angle", "90"}, {"axis", "z"}};
  } else if (scenario == "stereo") {
    action.command_name = "stereo set";
    action.parameters = {{"angle-scale", "2.1"},
                         {"enabled", "true"},
                         {"mode", "crosseye"},
                         {"shift-percent", "2.5"},
                         {"swap-eyes", "true"}};
  } else if (scenario == "anaglyph") {
    action.command_name = "stereo set";
    action.parameters = {{"angle-scale", "2.1"},
                         {"anaglyph-mode", "half_color"},
                         {"enabled", "true"},
                         {"mode", "anaglyph"},
                         {"shift-percent", "2.5"},
                         {"swap-eyes", "true"}};
  } else if (scenario == "interleaved") {
    action.command_name = "stereo set";
    action.parameters = {{"angle-scale", "2.1"},
                         {"anaglyph-mode", "optimized"},
                         {"enabled", "true"},
                         {"mode", "checkerboard"},
                         {"shift-percent", "2.5"},
                         {"swap-eyes", "true"}};
  } else if (scenario == "pymol-view") {
    action.command_name = "view import-pymol";
    action.parameters.emplace(
        "values", "0,1,0,-1,0,0,0,0,1,2,-3,-40,10,20,30,.5,100,60");
  } else if (scenario == "state-all") {
    if (argc != 3) {
      std::cerr << "state-all requires a structure fixture\n";
      return 2;
    }
    action.command_name = "view center";
    action.parameters = {{"move-origin", "true"},
                         {"selection", "all"},
                         {"state", "all"}};
  } else if (scenario == "orient-all") {
    if (argc != 3) {
      std::cerr << "orient-all requires a structure fixture\n";
      return 2;
    }
    action.command_name = "view orient";
    action.parameters = {{"selection", "all"}, {"state", "all"}};
  } else if (scenario == "representation-visibility") {
    if (argc != 3) {
      std::cerr << "representation-visibility requires a structure fixture\n";
      return 2;
    }
    action.command_name = "toggle";
    action.parameters = {{"representation", "everything"},
                         {"selection", "index 2"}};
  } else if (scenario == "render-setting") {
    if (argc != 3) {
      std::cerr << "render-setting requires a structure fixture\n";
      return 2;
    }
    action.command_name = "setting get";
    action.parameters = {{"name", "sphere_scale"},
                         {"scope", "atom"},
                         {"target", "1"}};
  } else if (scenario == "object-lifecycle") {
    if (argc != 3) {
      std::cerr << "object-lifecycle requires a structure fixture\n";
      return 2;
    }
    action.command_name = "object list";
  } else if (scenario == "edit-workflow") {
    if (argc != 3) {
      std::cerr << "edit-workflow requires a structure fixture\n";
      return 2;
    }
    action.command_name = "edit history";
  } else if (scenario == "molecule-builder") {
    if (argc != 2) {
      std::cerr << "molecule-builder takes no fixture\n";
      return 2;
    }
    action.command_name = "edit history";
  } else if (scenario == "object-chemistry") {
    if (argc != 3) {
      std::cerr << "object-chemistry requires a structure fixture\n";
      return 2;
    }
    action.command_name = "object chemistry";
  } else if (scenario == "object-perception") {
    if (argc != 3) {
      std::cerr << "object-perception requires a structure fixture\n";
      return 2;
    }
    action.command_name = "object perceive-chemistry";
  } else if (scenario == "object-perception-apply") {
    if (argc != 3) {
      std::cerr << "object-perception-apply requires a structure fixture\n";
      return 2;
    }
    action.command_name = "object perceive-chemistry";
    action.parameters = {{"apply", "true"}};
  } else if (scenario == "object-chemistry-after-perception") {
    if (argc != 3) {
      std::cerr << "object-chemistry-after-perception requires a structure fixture\n";
      return 2;
    }
    action.command_name = "object chemistry";
  } else if (scenario == "load-batch") {
    if (argc != 3) {
      std::cerr << "load-batch requires a structure fixture\n";
      return 2;
    }
    action.command_name = "load batch";
    action.parameters = {{"file-format", "pdb"},
                         {"names", "batch-one;batch-two"},
                         {"paths", std::string{argv[2]} + ";" + argv[2]}};
  } else if (scenario == "selection-numeric") {
    if (argc != 3) {
      std::cerr << "selection-numeric requires a structure fixture\n";
      return 2;
    }
    action.command_name = "show";
    action.parameters = {{"representation", "spheres"},
                         {"selection", "@numeric_atoms"}};
  } else if (scenario == "selection-numeric-error") {
    if (argc != 3) {
      std::cerr << "selection-numeric-error requires a structure fixture\n";
      return 2;
    }
    action.command_name = "select";
    action.parameters = {{"expression", "formal_charge / 0 > 1"},
                         {"name", "bad_numeric"},
                         {"update", "true"}};
  } else if (scenario == "selection-spatial") {
    if (argc != 3) {
      std::cerr << "selection-spatial requires a structure fixture\n";
      return 2;
    }
    action.command_name = "show";
    action.parameters = {{"representation", "lines"},
                         {"selection", "@spatial_atoms"}};
  } else if (scenario == "session-workflow") {
    if (argc != 3) {
      std::cerr << "session-workflow requires a structure fixture\n";
      return 2;
    }
    action.command_name = "movie status";
  } else {
    std::cerr << "unsupported probe scenario\n";
    return 2;
  }

  auto registry = molshredder::application::make_default_registry();
  const molshredder::application::Dispatcher dispatcher{registry};
  const molshredder::gui::ActionAdapter gui{dispatcher};
  molshredder::operation::TaskContext context;
  std::vector<std::string> lifecycle_results;
  if (scenario == "state-all" || scenario == "orient-all" ||
      scenario == "representation-visibility" ||
      scenario == "render-setting" || scenario == "object-lifecycle" ||
      scenario == "edit-workflow" ||
      scenario == "session-workflow" ||
      scenario == "molecule-builder" ||
      scenario == "object-chemistry" || scenario == "object-perception" ||
      scenario == "object-perception-apply" ||
      scenario == "object-chemistry-after-perception") {
    if (scenario == "session-workflow") {
      const std::array<molshredder::gui::Action, 9U> setup{
          molshredder::gui::Action{"load", {{"name", "session-object"},
                                             {"path", argv[2]}}},
          molshredder::gui::Action{"show", {{"representation", "spheres"},
                                             {"selection", "all"}}},
          molshredder::gui::Action{"scene store", {{"name", "baseline"}}},
          molshredder::gui::Action{"movie configure", {{"fps", "24"},
                                                        {"frames", "3"},
                                                        {"loop", "true"}}},
          molshredder::gui::Action{"movie keyframe", {{"frame", "2"},
                                                       {"scene", "baseline"}}},
          molshredder::gui::Action{"object visibility", {{"id", "1"},
                                                          {"visible", "false"}}},
          molshredder::gui::Action{"movie seek", {{"frame", "2"}}},
          molshredder::gui::Action{"movie play", {}},
          molshredder::gui::Action{"movie pause", {}}};
      for (const auto &setup_action : setup) {
        const auto setup_outcome = gui.trigger(setup_action, context);
        if (!setup_outcome.succeeded()) {
          std::cerr << "session workflow setup failed\n";
          return 2;
        }
        const auto setup_rendered = molshredder::command::render(
            setup_outcome.envelope,
            molshredder::operation::OutputFormat::json);
        if (!setup_rendered.has_value()) return 2;
        lifecycle_results.push_back(setup_rendered.value());
      }
    } else if (scenario == "molecule-builder") {
      const std::array<molshredder::gui::Action, 9U> setup{
          molshredder::gui::Action{
              "build molecule",
              {{"name", "carbonyl"},
               {"atoms", "C,6,0,0,0,0;O,8,1.2,0,0,0"},
               {"bonds", "1,2,double"},
               {"residue-name", "LIG"}, {"chain", "A"},
               {"residue-number", "1"}, {"unit", "angstrom"},
               {"memory-budget-bytes", "1048576"}}},
          molshredder::gui::Action{"edit undo", {}},
          molshredder::gui::Action{"edit redo", {}},
          molshredder::gui::Action{
              "edit atom-properties",
              {{"atom-id", "1"}, {"name", "C1"},
               {"formal-charge", "1"},
               {"expected-topology-version", "1"},
               {"expected-coordinate-source-revision", "1"}}},
          molshredder::gui::Action{
              "edit residue-properties",
              {{"atom-id", "1"}, {"name", "CRB"}, {"chain", "B"},
               {"residue-number", "7"},
               {"expected-topology-version", "2"},
               {"expected-coordinate-source-revision", "2"}}},
          molshredder::gui::Action{
              "edit bond-order",
              {{"bond-id", "1"}, {"order", "single"},
               {"expected-topology-version", "3"},
               {"expected-coordinate-source-revision", "3"}}},
          molshredder::gui::Action{"edit undo", {}},
          molshredder::gui::Action{"edit redo", {}},
          molshredder::gui::Action{"object chemistry", {}}};
      for (const auto &setup_action : setup) {
        const auto setup_outcome = gui.trigger(setup_action, context);
        if (!setup_outcome.succeeded()) {
          std::cerr << "molecule builder workflow setup failed\n";
          return 2;
        }
        const auto setup_rendered = molshredder::command::render(
            setup_outcome.envelope,
            molshredder::operation::OutputFormat::json);
        if (!setup_rendered.has_value()) return 2;
        lifecycle_results.push_back(setup_rendered.value());
      }
    } else if (scenario == "edit-workflow") {
      const std::array<molshredder::gui::Action, 4U> setup{
          molshredder::gui::Action{"load", {{"name", "editable"},
                                             {"path", argv[2]}}},
          molshredder::gui::Action{
              "edit atom-position",
              {{"atom-id", "1"},
               {"x", "9"}, {"y", "8"}, {"z", "7"},
               {"expected-topology-version", "1"},
               {"expected-coordinate-source-revision", "1"},
               {"unit", "angstrom"}}},
          molshredder::gui::Action{"edit undo", {}},
          molshredder::gui::Action{"edit redo", {}}};
      for (const auto &setup_action : setup) {
        const auto setup_outcome = gui.trigger(setup_action, context);
        if (!setup_outcome.succeeded()) {
          std::cerr << "edit workflow setup failed\n";
          return 2;
        }
        const auto setup_rendered = molshredder::command::render(
            setup_outcome.envelope,
            molshredder::operation::OutputFormat::json);
        if (!setup_rendered.has_value()) return 2;
        lifecycle_results.push_back(setup_rendered.value());
      }
    } else if (scenario == "object-lifecycle") {
      const std::array<molshredder::gui::Action, 9U> setup{
          molshredder::gui::Action{"load", {{"name", "alpha"},
                                             {"path", argv[2]}}},
          molshredder::gui::Action{"load", {{"name", "beta"},
                                             {"path", argv[2]}}},
          molshredder::gui::Action{"load", {{"name", "gamma"},
                                             {"path", argv[2]}}},
          molshredder::gui::Action{"object visibility", {{"id", "2"},
                                                          {"visible", "false"}}},
          molshredder::gui::Action{"object activate", {{"id", "1"}}},
          molshredder::gui::Action{"object rename", {{"name", "delta"},
                                                      {"object", "2"}}},
          molshredder::gui::Action{"object reorder", {{"object", "3"},
                                                       {"position", "1"}}},
          molshredder::gui::Action{"object delete", {{"object", "current"}}},
          molshredder::gui::Action{"object topology-retain",
                                    {{"atom-ids", "3,1"},
                                     {"expected-version", "1"}}}};
      for (const auto &setup_action : setup) {
        const auto setup_outcome = gui.trigger(setup_action, context);
        if (!setup_outcome.succeeded()) {
          std::cerr << "object lifecycle setup failed\n";
          return 2;
        }
        const auto setup_rendered = molshredder::command::render(
            setup_outcome.envelope,
            molshredder::operation::OutputFormat::json);
        if (!setup_rendered.has_value()) {
          std::cerr << "object lifecycle result rendering failed\n";
          return 2;
        }
        lifecycle_results.push_back(setup_rendered.value());
      }
    } else if (scenario == "object-chemistry" ||
               scenario == "object-perception" ||
               scenario == "object-perception-apply" ||
               scenario == "object-chemistry-after-perception") {
      const auto loaded = gui.trigger(
          {"load", {{"file-format", "sdf"}, {"path", argv[2]}}}, context);
      const auto activated =
          gui.trigger({"object activate", {{"id", "1"}}}, context);
      if (!loaded.succeeded() || !activated.succeeded()) {
        std::cerr << "chemical semantics fixture setup failed\n";
        return 2;
      }
      if (scenario == "object-chemistry-after-perception") {
        const auto applied = gui.trigger(
            {"object perceive-chemistry", {{"apply", "true"}}}, context);
        if (!applied.succeeded()) {
          std::cerr << "chemical perception apply setup failed\n";
          return 2;
        }
      }
    } else {
    const auto loaded = gui.trigger(
        {"load", {{"file-format", "g96"}, {"path", argv[2]}}}, context);
    if (!loaded.succeeded()) {
      std::cerr << "camera state fixture load failed\n";
      return 2;
    }
    if (scenario == "representation-visibility") {
      const std::array<molshredder::gui::Action, 4U> setup{
          molshredder::gui::Action{"show", {{"representation", "wire"},
                                             {"selection", "all"}}},
          molshredder::gui::Action{"show", {{"representation", "spheres"},
                                             {"selection", "index 1"}}},
          molshredder::gui::Action{"hide", {{"representation", "lines"},
                                             {"selection", "index 1"}}},
          molshredder::gui::Action{"as", {{"representation", "licorice"},
                                           {"selection", "index 2"}}}};
      for (const auto &setup_action : setup) {
        if (!gui.trigger(setup_action, context).succeeded()) {
          std::cerr << "representation visibility setup failed\n";
          return 2;
        }
      }
    } else if (scenario == "render-setting") {
      const std::array<molshredder::gui::Action, 2U> setup{
          molshredder::gui::Action{"show", {{"representation", "spheres"},
                                             {"selection", "all"}}},
          molshredder::gui::Action{"setting set", {{"name", "sphere_scale"},
                                                    {"value", "2.5"},
                                                    {"scope", "atom"},
                                                    {"target", "1"}}}};
      for (const auto &setup_action : setup) {
        if (!gui.trigger(setup_action, context).succeeded()) {
          std::cerr << "render setting setup failed\n";
          return 2;
        }
      }
    } else {
      const auto reset = gui.trigger({"view reset", {}}, context);
      if (!reset.succeeded()) {
        std::cerr << "camera state reset failed\n";
        return 2;
      }
    }
    }
  }
  if (scenario == "selection-numeric" ||
      scenario == "selection-numeric-error" ||
      scenario == "selection-spatial") {
    const auto loaded = gui.trigger(
        {"load", {{"file-format", "pdb"}, {"path", argv[2]}}}, context);
    if (!loaded.succeeded()) {
      std::cerr << "numeric selection fixture load failed\n";
      return 2;
    }
    if (scenario == "selection-numeric") {
      const auto selected = gui.trigger(
          {"select",
           {{"expression", "formal_charge > 0 or index * 2 = 4"},
            {"name", "numeric_atoms"},
            {"update", "true"}}},
          context);
      if (!selected.succeeded()) {
        std::cerr << "numeric selection setup failed\n";
        return 2;
      }
    } else if (scenario == "selection-spatial") {
      const auto numeric_selected = gui.trigger(
          {"select",
           {{"expression", "formal_charge > 0 or index * 2 = 4"},
            {"name", "numeric_atoms"},
            {"update", "true"}}},
          context);
      const auto numeric_shown = gui.trigger(
          {"show", {{"representation", "spheres"},
                    {"selection", "@numeric_atoms"}}},
          context);
      const auto selected = gui.trigger(
          {"select",
           {{"expression", "index 1 around 2"},
            {"name", "spatial_atoms"},
            {"update", "true"}}},
          context);
      if (!numeric_selected.succeeded() || !numeric_shown.succeeded() ||
          !selected.succeeded()) {
        std::cerr << "spatial selection setup failed\n";
        return 2;
      }
    }
  }
  const auto outcome = gui.trigger(action, context);
  const auto rendered = molshredder::command::render(
      outcome.envelope, molshredder::operation::OutputFormat::json);
  if (!rendered.has_value()) {
    std::cerr << rendered.error().message << '\n';
    return 2;
  }
  for (const auto &result : lifecycle_results)
    std::cout << result << '\n';
  std::cout << rendered.value();
  return 0;
}
