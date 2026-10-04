#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/session.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/version.hpp"

namespace {

using namespace molshredder;

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

struct Harness {
  std::shared_ptr<application::Workspace> workspace =
      std::make_shared<application::Workspace>();
  command::Registry registry = application::make_default_registry(workspace);
  application::Dispatcher dispatcher{registry};
  operation::TaskContext context;

  application::DispatchOutcome run(std::string name,
                                   command::Arguments arguments) {
    return dispatcher.dispatch({std::move(name), std::move(arguments)},
                               context);
  }
};

const command::Response *response(const application::DispatchOutcome &outcome) {
  return std::get_if<command::Response>(&outcome.envelope.payload);
}

std::int64_t int_field(const application::DispatchOutcome &outcome,
                       std::string_view key) {
  const auto *value = response(outcome);
  if (value == nullptr)
    return -1000;
  const auto found = value->fields.find(key);
  if (found == value->fields.end())
    return -1000;
  if (const auto *signed_value = std::get_if<std::int64_t>(&found->second.data))
    return *signed_value;
  if (const auto *unsigned_value =
          std::get_if<std::uint64_t>(&found->second.data))
    return static_cast<std::int64_t>(*unsigned_value);
  return -1000;
}

std::vector<render::ColorRgba>
sphere_colors(const application::Workspace &workspace) {
  std::vector<render::ColorRgba> colors;
  const auto *object = workspace.active_object();
  if (object == nullptr)
    return colors;
  for (const auto &record : object->representations)
    if (record.kind == render::RepresentationKind::spheres)
      for (const auto &sphere : record.packet.spheres)
        colors.push_back(sphere.color);
  return colors;
}

bool near(const render::ColorRgba &color, float red, float green, float blue) {
  return std::abs(color.red - red) < 1.0e-6F &&
         std::abs(color.green - green) < 1.0e-6F &&
         std::abs(color.blue - blue) < 1.0e-6F;
}

} // namespace

int main(int argc, char **argv) {
  bool passed = true;
  if (argc != 2) {
    std::cerr << "expected PDB fixture path\n";
    return 1;
  }
  const std::filesystem::path fixture{argv[1]};
  Harness harness;
  auto &workspace = *harness.workspace;
  const auto load = [&](std::string name) {
    return harness.run("load", {{"file-format", "pdb"},
                                {"name", std::move(name)},
                                {"path", fixture.string()}});
  };

  passed &= expect(load("first").succeeded() && load("second").succeeded(),
                   "fixture objects must load");
  passed &= expect(workspace.objects()[0].object_color == 26 &&
                       workspace.objects()[1].object_color == 5,
                   "object colors must follow the reference auto cycle");
  passed &= expect(harness.run("show", {{"representation", "spheres"},
                                        {"selection", "all"}})
                       .succeeded(),
                   "spheres must be shown");
  const auto defaults = sphere_colors(workspace);
  passed &= expect(defaults.size() == 3U &&
                       near(defaults[0], 0.20F, 0.35F, 0.95F),
                   "uncolored atoms keep the MolShredder default palette");

  const auto red = harness.run("color", {{"color", "red"},
                                        {"selection", "index 1"}});
  auto colors = sphere_colors(workspace);
  passed &= expect(red.succeeded() && int_field(red, "color_index") == 4 &&
                       int_field(red, "affected_atom_count") == 1 &&
                       near(colors[0], 1.0F, 0.0F, 0.0F) &&
                       colors[1] == defaults[1],
                   "named color must recolor only the selection");

  const auto atomic = harness.run("color", {{"color", "atomic"},
                                           {"selection", "all"}});
  colors = sphere_colors(workspace);
  passed &= expect(atomic.succeeded() &&
                       int_field(atomic, "colored_atom_count") == 3 &&
                       near(colors[0], 0.2F, 0.2F, 1.0F) &&
                       near(colors[1], 0.9F, 0.9F, 0.9F) &&
                       near(colors[2], 1.0F, 0.3F, 0.3F),
                   "atomic must apply reference element colors");

  const auto automatic = harness.run("color", {{"color", "auto"},
                                              {"selection", "index 2"}});
  passed &= expect(automatic.succeeded() &&
                       int_field(automatic, "color_index") == 154,
                   "auto must continue the shared cycle after object loads");
  const auto object_token = harness.run("color", {{"color", "object"},
                                                 {"selection", "index 2"}});
  colors = sphere_colors(workspace);
  passed &= expect(object_token.succeeded() && near(colors[1], 0.0F, 1.0F, 1.0F),
                   "object token must resolve to the object color");

  const auto defined = harness.run("color define", {{"blue", "0"},
                                                   {"green", "128"},
                                                   {"name", "MyColor"},
                                                   {"red", "255"}});
  const auto custom = harness.run("color", {{"color", "mycolor"},
                                           {"selection", "index 3"}});
  colors = sphere_colors(workspace);
  passed &= expect(defined.succeeded() &&
                       int_field(defined, "color_index") == 5388 &&
                       custom.succeeded() &&
                       near(colors[2], 1.0F, 128.0F / 255.0F, 0.0F),
                   "custom colors must use 0-255 scaling and new indices");
  const auto replaced = harness.run("color define", {{"blue", "1"},
                                                    {"green", "0"},
                                                    {"name", "mycolor"},
                                                    {"red", "0"}});
  colors = sphere_colors(workspace);
  passed &= expect(replaced.succeeded() &&
                       int_field(replaced, "color_index") == 5388 &&
                       near(colors[2], 0.0F, 0.0F, 1.0F),
                   "redefinition must keep the index and rebuild geometry");

  const auto before_failure = sphere_colors(workspace);
  const auto unknown = harness.run("color", {{"color", "notacolor"},
                                            {"selection", "all"}});
  const auto front = harness.run("color", {{"color", "front"},
                                          {"selection", "all"}});
  const auto bad_selection =
      harness.run("color", {{"color", "red"}, {"selection", "unknown X"}});
  passed &= expect(!unknown.succeeded() && !front.succeeded() &&
                       !bad_selection.succeeded() &&
                       sphere_colors(workspace) == before_failure,
                   "failed color assignments must not mutate state");

  const auto setting = harness.run("setting set", {{"name", "sphere_color"},
                                                  {"scope", "atom"},
                                                  {"target", "1"},
                                                  {"value", "mycolor"}});
  const auto bad_setting =
      harness.run("setting set", {{"name", "sphere_color"},
                                  {"scope", "atom"},
                                  {"target", "1"},
                                  {"value", "not-a-color"}});
  colors = sphere_colors(workspace);
  passed &= expect(setting.succeeded() && !bad_setting.succeeded() &&
                       near(colors[0], 0.0F, 0.0F, 1.0F),
                   "render setting colors must resolve custom table names");

  const auto hex = harness.run("color get", {{"color", "0xff8000"}});
  const auto listed = harness.run("color list", {{"scope", "custom"}});
  const auto *listed_response = response(listed);
  passed &= expect(hex.succeeded() &&
                       int_field(hex, "color_index") == 1090486272 &&
                       listed_response != nullptr &&
                       listed_response->table.has_value() &&
                       listed_response->table->rows.size() == 1U,
                   "color get/list must report table state");
  passed &= expect(harness.run("recolor", {}).succeeded() &&
                       sphere_colors(workspace) == colors,
                   "recolor must preserve assigned colors");

  const auto reset = harness.run("color", {{"color", "default"},
                                          {"selection", "index 2 or index 3"}});
  passed &= expect(reset.succeeded() &&
                       int_field(reset, "colored_atom_count") == 1 &&
                       sphere_colors(workspace)[1] == defaults[1],
                   "default must restore the MolShredder palette");

  // Session journal round-trip reproduces table, atom colors and the cycle.
  const auto document = application::capture_session_document(
      workspace, std::string{version()});
  Harness restored;
  const auto replay =
      document.has_value()
          ? application::replay_session(document.value(), restored.dispatcher,
                                        restored.context)
          : operation::Result<application::SessionReplayResult>::failure(
                document.error());
  passed &= expect(replay.has_value() &&
                       sphere_colors(*restored.workspace) ==
                           sphere_colors(workspace) &&
                       restored.workspace->color_table() ==
                           workspace.color_table(),
                   "session replay must restore colors");
  const auto next_auto = restored.run("color", {{"color", "auto"},
                                               {"selection", "all"}});
  passed &= expect(int_field(next_auto, "color_index") == 6,
                   "session replay must restore the auto color cursor");
  return passed ? 0 : 1;
}
