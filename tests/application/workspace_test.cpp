#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/cli/console.hpp"
#include "molshredder/gui/action_adapter.hpp"
#include "molshredder/gui/analysis_presenter.hpp"
#include "molshredder/operation/task_context.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

molshredder::application::DispatchOutcome trigger(
    const molshredder::gui::ActionAdapter& gui, std::string command,
    molshredder::command::Arguments arguments = {}) {
  molshredder::operation::TaskContext context;
  return gui.trigger({std::move(command), std::move(arguments)}, context);
}

}  // namespace

int main(int argc, char** argv) {
  using namespace molshredder;
  bool passed = true;
  if (argc != 4) {
    std::cerr << "expected general, PBC, and H-bond PDB fixture paths\n";
    return 1;
  }
  const std::filesystem::path fixture{argv[1]};
  const std::filesystem::path pbc_fixture{argv[2]};
  const std::filesystem::path hbond_fixture{argv[3]};

  auto workspace = std::make_shared<application::Workspace>();
  auto registry = application::make_default_registry(workspace);
  const application::Dispatcher dispatcher{registry};
  const gui::ActionAdapter gui{dispatcher};

  const auto no_object = trigger(
      gui, "show", {{"representation", "spheres"}, {"selection", "all"}});
  passed &= expect(!no_object.succeeded(),
                   "show before load must fail through shared state");

  const auto loaded = trigger(
      gui, "load", {{"path", fixture.string()}, {"name", "protein"}});
  passed &= expect(loaded.succeeded() && workspace->object_count() == 1U &&
                       workspace->active_object() != nullptr &&
                       workspace->active_object()->system->name() == "protein" &&
                       workspace->scene()->node_count() == 2U &&
                       workspace->scene()->selection().contains(
                           workspace->active_object()->scene_node),
                   "GUI load must create active object and scene node");
  const auto initial_objects = trigger(gui, "object list");
  const auto* initial_object_response = initial_objects.succeeded()
                                            ? std::get_if<command::Response>(
                                                  &initial_objects.envelope.payload)
                                            : nullptr;
  passed &= expect(initial_object_response != nullptr &&
                       initial_object_response->table.has_value() &&
                       initial_object_response->table->rows.size() == 1U,
                   "object list must expose active Workspace state");
  passed &= expect(loaded.envelope.canonical_command.find(
                       "--file-format \"auto\"") != std::string::npos,
                   "load must normalize the explicit format default");

  const auto selected = trigger(
      gui, "select", {{"name", "chain_a"},
                       {"expression", "chain A"},
                       {"update", "true"}});
  passed &= expect(selected.succeeded(),
                   "GUI select must create a dynamic named selection");
  const auto shown = trigger(gui, "show",
                             {{"representation", "spheres"},
                              {"selection", "@chain_a"}});
  passed &= expect(shown.succeeded() &&
                       workspace->active_object()->representations.size() == 1U &&
                       workspace->active_object()
                               ->representations[0]
                               .packet.spheres.size() == 2U &&
                       workspace->active_object()
                               ->representations[0]
                               .packet.pick_targets.size() == 2U,
                   "GUI show must evaluate selection and store render packet");
  const auto failed_replace =
      trigger(gui, "show", {{"representation", "sticks"},
                             {"selection", "unknown X"},
                             {"replace", "true"}});
  passed &= expect(!failed_replace.succeeded() &&
                       workspace->active_object()->representations.size() == 1U &&
                       workspace->active_object()
                               ->representations.front()
                               .packet.spheres.size() == 2U,
                   "failed replacement must preserve the existing packet");
  const auto replaced =
      trigger(gui, "show", {{"representation", "sticks"},
                             {"selection", "@chain_a"},
                             {"replace", "true"}});
  passed &= expect(replaced.succeeded() &&
                       workspace->active_object()->representations.size() == 1U &&
                       workspace->active_object()
                               ->representations.front()
                               .kind == render::RepresentationKind::sticks,
                   "replace show must atomically retain only the new packet");

  const auto center = trigger(
      gui, "analyze center",
      {{"selection", "@chain_a"}, {"mode", "centroid"},
       {"precision", "3"}, {"unit", "angstrom"}});
  const auto distance = trigger(
      gui, "measure distance",
      {{"from", "index 1"}, {"to", "index 2"}, {"mode", "atom"},
       {"pbc", "raw"}, {"precision", "6"}, {"unit", "angstrom"}});
  passed &= expect(center.succeeded() && distance.succeeded() &&
                       workspace->measurements().size() == 1U &&
                       workspace->measurements()[0].measurement_id == 1U,
                   "analysis must be pure while measurement persists once");
  const auto secondary=trigger(gui,"analyze secondary-structure",
      {{"selection","all"},{"energy-cutoff","-0.5"},
       {"helix-propensity","0.05"},{"beta-propensity","0.02"},
       {"precision","3"}});
  const auto* secondary_response=secondary.succeeded()
      ? std::get_if<command::Response>(&secondary.envelope.payload) : nullptr;
  passed &= expect(secondary_response!=nullptr && secondary_response->table.has_value() &&
      std::get<std::string>(secondary_response->fields.at("method").data)==
          "molshredder-stride-method-v0" &&
      !std::get<bool>(secondary_response->fields.at("exact_stride_parity").data),
      "secondary-structure command must expose independent-method provenance");
  const auto secondary_presentation=gui::make_analysis_presentation(secondary);
  passed &= expect(secondary_presentation.has_value() &&
      secondary_presentation.value().title=="Secondary-structure assignment",
      "GUI presenter must expose secondary-structure table");
  const auto minimum_image = trigger(
      gui, "measure distance",
      {{"from", "index 1"}, {"to", "index 2"}, {"mode", "atom"},
       {"pbc", "minimum-image"}, {"precision", "6"},
       {"unit", "angstrom"}});
  passed &= expect(
      minimum_image.succeeded() && workspace->measurements().size() == 2U &&
          workspace->measurements()[1].boundary ==
              analysis::DistanceBoundary::minimum_image,
      "minimum-image PBC must execute and persist its boundary mode");

  const auto duplicate = trigger(
      gui, "load", {{"path", fixture.string()}, {"name", "protein"}});
  passed &= expect(!duplicate.succeeded() && workspace->object_count() == 1U,
                   "duplicate load failure must leave workspace unchanged");
  const auto second = trigger(
      gui, "load", {{"path", fixture.string()}, {"name", "ligand"}});
  passed &= expect(second.succeeded() && workspace->object_count() == 2U &&
                       workspace->active_object()->id == 2U,
                   "second load must append and activate a distinct object");
  const auto hidden = trigger(
      gui, "object visibility", {{"id", "1"}, {"visible", "false"}});
  passed &= expect(hidden.succeeded() &&
                       !workspace->scene()->effectively_visible(
                           workspace->objects()[0].scene_node),
                   "object visibility must commit through immutable scene");
  const auto activated = trigger(gui, "object activate", {{"id", "1"}});
  passed &= expect(activated.succeeded() &&
                       workspace->active_object()->id == 1U &&
                       workspace->scene()->selection().contains(
                           workspace->active_object()->scene_node),
                   "object activation must update Workspace and scene selection");
  const auto previous_scene = workspace->scene();
  const auto missing_object =
      trigger(gui, "object activate", {{"id", "999"}});
  passed &= expect(!missing_object.succeeded() &&
                       workspace->active_object()->id == 1U &&
                       workspace->scene() == previous_scene,
                   "failed activation must preserve active object and scene");
  passed &= expect(trigger(gui, "object visibility",
                           {{"id", "1"}, {"visible", "true"}})
                       .succeeded(),
                   "hidden object must be showable without losing state");
  const auto bad_selection = trigger(
      gui, "select", {{"name", "bad"}, {"expression", "unknown X"}});
  passed &= expect(!bad_selection.succeeded(),
                   "invalid selection must fail through GUI action");

  auto console_workspace = std::make_shared<application::Workspace>();
  auto console_registry =
      application::make_default_registry(console_workspace);
  cli::Console console{console_registry};
  std::istringstream input{
      "format json\n"
      "invoke \"load\" --file-format \"pdb\" --name \"cli_object\" --path \"" +
      fixture.string() +
      "\"\n"
      "invoke \"select\" --expression \"chain A\" --name \"chain_a\" "
      "--update \"false\"\n"
      "invoke \"show\" --representation \"lines\" --selection "
      "\"@chain_a\"\n"
      "history\n"
      "exit\n"};
  std::ostringstream output;
  std::ostringstream error;
  passed &= expect(console.run(input, output, error) == 0 && error.str().empty() &&
                       console_workspace->object_count() == 1U &&
                       console_workspace->active_object()
                               ->representations.size() == 1U &&
                       console.history().size() == 3U &&
                       output.str().find("\"primitive_count\":2") !=
                           std::string::npos,
                   "native console must preserve load/select/show state");

  auto pbc_workspace = std::make_shared<application::Workspace>();
  auto pbc_registry = application::make_default_registry(pbc_workspace);
  const application::Dispatcher pbc_dispatcher{pbc_registry};
  const gui::ActionAdapter pbc_gui{pbc_dispatcher};
  passed &= expect(
      trigger(pbc_gui, "load",
              {{"path", pbc_fixture.string()}, {"name", "pbc_fixture"}})
          .succeeded(),
      "PBC distance fixture must load");
  const auto pbc_raw = trigger(
      pbc_gui, "measure distance",
      {{"from", "index 1"}, {"to", "index 2"}, {"mode", "atom"},
       {"pbc", "raw"}, {"precision", "6"}, {"unit", "angstrom"}});
  const auto pbc_minimum = trigger(
      pbc_gui, "measure distance",
      {{"from", "index 1"}, {"to", "index 2"}, {"mode", "atom"},
       {"pbc", "minimum-image"}, {"precision", "6"},
       {"unit", "angstrom"}});
  const auto pbc_contacts = trigger(
      pbc_gui, "analyze contacts",
      {{"first", "all"}, {"cutoff", "3.0"},
       {"pbc", "minimum-image"}, {"exclude-bonded", "false"},
       {"precision", "3"}, {"unit", "angstrom"}});
  const auto* contact_response = pbc_contacts.succeeded()
      ? &std::get<command::Response>(pbc_contacts.envelope.payload) : nullptr;
  passed &= expect(
      pbc_raw.succeeded() && pbc_minimum.succeeded() &&
          contact_response != nullptr && contact_response->table.has_value() &&
          contact_response->table->rows.size() == 1U &&
          pbc_workspace->measurements().size() == 2U &&
          pbc_workspace->measurements()[0].distance.distance == 8.0 &&
          pbc_workspace->measurements()[0].distance.displacement.x == -8.0 &&
          pbc_workspace->measurements()[1].distance.distance == 2.0 &&
          pbc_workspace->measurements()[1].distance.displacement.x == 2.0 &&
          pbc_workspace->measurements()[1].boundary ==
              analysis::DistanceBoundary::minimum_image,
      "shared command path must distinguish raw and minimum-image distance");

  auto hbond_workspace=std::make_shared<application::Workspace>();
  auto hbond_registry=application::make_default_registry(hbond_workspace);
  const application::Dispatcher hbond_dispatcher{hbond_registry};
  const gui::ActionAdapter hbond_gui{hbond_dispatcher};
  passed &= expect(trigger(hbond_gui,"load",{{"path",hbond_fixture.string()},
      {"name","hbond_fixture"}}).succeeded(),"H-bond fixture must load");
  const auto hbonds=trigger(hbond_gui,"analyze hbonds",
      {{"donors","chain A"},{"acceptors","chain B"},{"cutoff","3.5"},
       {"angle","30"},{"pbc","raw"},{"precision","3"},{"unit","angstrom"}});
  const auto* hbond_response=hbonds.succeeded()
      ? &std::get<command::Response>(hbonds.envelope.payload) : nullptr;
  passed &= expect(hbond_response!=nullptr && hbond_response->table.has_value() &&
      hbond_response->table->rows.size()==1U &&
      std::get<std::string>(hbond_response->fields.at("donor_typing_source").data)=="element-bond-v1" &&
      std::get<bool>(hbond_response->fields.at("typing_estimated").data),
      "GUI H-bond command must expose geometry table and typing provenance");

  return passed ? 0 : 1;
}
