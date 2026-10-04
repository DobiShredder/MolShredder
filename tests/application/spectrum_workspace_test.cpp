#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

// Expected indices: docs/research/pymol-spectrum-background-blackbox_20261002T150000Z
// (Open-Source PyMOL 3.1.0, same 12-atom synthetic structure).

namespace {

using namespace molshredder;

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

constexpr std::string_view kStructure =
    "ATOM      1  N   ALA A   1       1.500   0.000   0.000  1.00 10.00           N\n"
    "ATOM      2  CA  ALA A   1       3.000   0.000   0.000  0.50 20.00           C1+\n"
    "ATOM      3  C   ALA A   1       4.500   0.000   0.000  0.25 30.00           C\n"
    "ATOM      4  N   GLY A   2       6.000   0.000   0.000  1.00 40.00           N\n"
    "ATOM      5  CA  GLY A   2       7.500   0.000   0.000  0.75  5.00           C1+\n"
    "ATOM      6  C   GLY A   2       9.000   0.000   0.000  0.10 60.00           C\n"
    "ATOM      7  N   SER B   3      10.500   0.000   0.000  1.00 70.00           N\n"
    "ATOM      8  CA  SER B   3      12.000   0.000   0.000  0.30 80.00           C\n"
    "ATOM      9  O   SER B   3      13.500   0.000   0.000  0.60 90.00           O\n"
    "ATOM     10  N   LYS B   4      15.000   0.000   0.000  1.00100.00           N\n"
    "ATOM     11  CA  LYS B   4      16.500   0.000   0.000  0.20110.00           C\n"
    "ATOM     12  O   LYS B   4      18.000   0.000   0.000  0.90120.00           O\n"
    "END\n";

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

struct Case {
  std::string_view expression;
  std::string_view palette;
  std::string_view minimum;
  std::string_view maximum;
  bool byres;
  std::string_view interpolation;
  std::array<std::int32_t, 12> expected;
};

std::vector<std::int32_t> atom_colors(const application::Workspace &workspace) {
  std::vector<std::int32_t> colors(12U, -1000);
  const auto *object = workspace.active_object();
  if (object == nullptr || object->atom_colors == nullptr)
    return colors;
  const auto &topology = *object->system->topology();
  for (const auto &[id, color] : *object->atom_colors) {
    const auto index = topology.atom_index(model::AtomId{id});
    if (index.has_value() && index->value < colors.size())
      colors[index->value] = color;
  }
  return colors;
}

} // namespace

int main() {
  bool passed = true;
  const auto directory = std::filesystem::temp_directory_path() /
                         "molshredder-spectrum-workspace-test";
  std::filesystem::create_directories(directory);
  const auto path = directory / "spectrum.pdb";
  {
    std::ofstream output{path};
    output << kStructure;
  }
  Harness harness;
  passed &= expect(harness.run("load", {{"file-format", "pdb"},
                                        {"name", "m"},
                                        {"path", path.string()}})
                       .succeeded() &&
                       harness.run("show", {{"representation", "spheres"},
                                            {"selection", "all"}})
                           .succeeded(),
                   "spectrum fixture must load");

  const std::array<Case, 34> cases{{
      {"vdw", "blue_red", "", "", false, "rgb", {2252, 2321, 2321, 2252, 2321, 2321, 2252, 2321, 2238, 2252, 2321, 2238}},
      {"count", "blue_red", "", "", false, "rgb", {2238, 2246, 2253, 2261, 2269, 2276, 2284, 2291, 2299, 2307, 2314, 2321}},
      {"b", "blue_red", "", "", false, "rgb", {2242, 2249, 2256, 2264, 2238, 2278, 2285, 2293, 2300, 2307, 2315, 2321}},
      {"q", "blue_red", "", "", false, "rgb", {2321, 2275, 2252, 2321, 2299, 2238, 2321, 2257, 2285, 2321, 2247, 2313}},
      {"resi", "blue_red", "", "", false, "rgb", {2238, 2238, 2238, 2266, 2266, 2266, 2294, 2294, 2294, 2321, 2321, 2321}},
      {"index", "blue_red", "", "", false, "rgb", {2238, 2246, 2253, 2261, 2269, 2276, 2284, 2291, 2299, 2307, 2314, 2321}},
      {"rank", "blue_red", "", "", false, "rgb", {2238, 2246, 2253, 2261, 2269, 2276, 2284, 2291, 2299, 2307, 2314, 2321}},
      {"ID", "blue_red", "", "", false, "rgb", {2238, 2246, 2253, 2261, 2269, 2276, 2284, 2291, 2299, 2307, 2314, 2321}},
      {"formal_charge", "blue_red", "", "", false, "rgb", {2238, 2321, 2238, 2238, 2321, 2238, 2238, 2238, 2238, 2238, 2238, 2238}},
      {"chain", "blue_red", "", "", false, "rgb", {2238, 2238, 2238, 2238, 2238, 2238, 2321, 2321, 2321, 2321, 2321, 2321}},
      {"resn", "blue_red", "", "", false, "rgb", {2238, 2238, 2238, 2266, 2266, 2266, 2294, 2294, 2294, 2321, 2321, 2321}},
      {"name", "blue_red", "", "", false, "rgb", {2238, 2266, 2294, 2238, 2266, 2294, 2238, 2266, 2321, 2238, 2266, 2321}},
      {"elem", "blue_red", "", "", false, "rgb", {2238, 2280, 2280, 2238, 2280, 2280, 2238, 2280, 2321, 2238, 2280, 2321}},
      {"b*q", "blue_red", "", "", false, "rgb", {1074725103, 1074725103, 1074331893, 1079509158, 1073742079, 1074069753, 1084358748, 1076953293, 1081737348, 1089142803, 1076625618, 1090453504}},
      {"b+100", "blue_red", "", "", false, "rgb", {1074462963, 1075904733, 1077346503, 1078788273, 1073742079, 1081671813, 1083179118, 1084620888, 1086062658, 1087504428, 1088946198, 1090453504}},
      {"b", "blue_red", "", "", true, "rgb", {2242, 2242, 2242, 2264, 2264, 2264, 2285, 2285, 2285, 2307, 2307, 2307}},
      {"q", "blue_red", "", "", true, "rgb", {2321, 2321, 2321, 2321, 2321, 2321, 2321, 2321, 2321, 2321, 2321, 2321}},
      {"count", "blue_red", "", "", true, "rgb", {2238, 2238, 2238, 2261, 2261, 2261, 2284, 2284, 2284, 2307, 2307, 2307}},
      {"resi", "blue_red", "", "", true, "rgb", {2238, 2238, 2238, 2266, 2266, 2266, 2294, 2294, 2294, 2321, 2321, 2321}},
      {"b", "blue_red", "30", "90", false, "rgb", {2238, 2238, 2238, 2252, 2238, 2280, 2294, 2308, 2321, 2321, 2321, 2321}},
      {"b", "blue_red", "90", "30", false, "rgb", {2242, 2249, 2256, 2264, 2238, 2278, 2285, 2293, 2300, 2307, 2315, 2321}},
      {"b", "blue_red", "50", "", false, "rgb", {2242, 2249, 2256, 2264, 2238, 2278, 2285, 2293, 2300, 2307, 2315, 2321}},
      {"b", "blue_red", "", "50", false, "rgb", {2242, 2249, 2256, 2264, 2238, 2278, 2285, 2293, 2300, 2307, 2315, 2321}},
      {"b", "blue_red", "50", "50", false, "rgb", {2238, 2238, 2238, 2238, 2238, 2321, 2321, 2321, 2321, 2321, 2321, 2321}},
      {"count", "red blue", "", "", false, "hls", {1090453504, 1090477056, 1090500864, 1088945920, 1082916608, 1076821760, 1073807150, 1073807243, 1073807335, 1073789439, 1073765631, 1073742079}},
      {"count", "blue white red", "", "", false, "hsv", {1073742079, 1076807423, 1079836597, 1084293003, 1090060217, 1090515175, 1090512871, 1090501049, 1090489227, 1090477148, 1090465326, 1090453504}},
      {"count", "salmon wheat", "", "", false, "rgb", {1090492825, 1090428570, 1090429851, 1090431132, 1090432413, 1090368158, 1090369439, 1090370721, 1090372002, 1090307747, 1090309028, 1090310565}},
      {"count", "0xff0000 0x0000ff", "", "", false, "rgb", {1090453504, 1088880663, 1087373358, 1085866053, 1084358748, 1082851443, 1081278603, 1079771298, 1078263993, 1076756688, 1075249383, 1073742079}},
      {"b*1", "rainbow", "", "", false, "rgb", {1073756159, 1073784575, 1073807336, 1073807225, 1073742079, 1080295168, 1087569664, 1090510080, 1090496000, 1090481664, 1090467584, 1090453504}},
      {"b*1", "gcbmry", "", "", false, "rgb", {1073807159, 1073807270, 1073801471, 1073773055, 1073807104, 1080230143, 1087504639, 1090453692, 1090453581, 1090461952, 1090490368, 1090518784}},
      {"b*1", "yellow_cyan_white", "", "", false, "rgb", {1089011478, 1086127938, 1083244398, 1080295323, 1090518784, 1074528243, 1075970047, 1078853631, 1081737215, 1084686335, 1087569919, 1090519039}},
      {"count", "rainbow", "", "", false, "rgb", {4363, 4434, 4506, 4577, 4649, 4720, 4792, 4863, 4935, 5006, 5078, 5148}},
      {"count", "red_blue", "", "", false, "rgb", {2322, 2314, 2307, 2299, 2291, 2284, 2276, 2269, 2261, 2253, 2246, 2239}},
      {"count", "blue_white_yellow", "", "", false, "rgb", {3238, 3230, 3223, 3215, 3208, 3200, 3193, 3185, 3178, 3170, 3163, 3156}},
  }};
  for (const auto &item : cases) {
    passed &= expect(harness.run("color", {{"color", "white"}, {"selection", "all"}})
                         .succeeded(),
                     "reset color must succeed");
    command::Arguments arguments{{"byres", item.byres ? "true" : "false"},
                                 {"expression", std::string{item.expression}},
                                 {"interpolation", std::string{item.interpolation}},
                                 {"palette", std::string{item.palette}},
                                 {"selection", "all"}};
    if (!item.minimum.empty())
      arguments.emplace("minimum", std::string{item.minimum});
    if (!item.maximum.empty())
      arguments.emplace("maximum", std::string{item.maximum});
    const auto outcome = harness.run("spectrum", arguments);
    const auto colors = atom_colors(*harness.workspace);
    passed &= expect(outcome.succeeded() &&
                         std::equal(colors.begin(), colors.end(),
                                    item.expected.begin()),
                     std::string{"spectrum drifted: "} +
                         std::string{item.expression} + " / " +
                         std::string{item.palette});
  }

  // Subselection range and errors keep state unchanged.
  harness.run("color", {{"color", "white"}, {"selection", "all"}});
  const auto sub = harness.run("spectrum", {{"expression", "b"},
                                            {"palette", "rainbow"},
                                            {"selection", "chain B"}});
  const auto sub_colors = atom_colors(*harness.workspace);
  const std::array<std::int32_t, 12> sub_expected{0, 0, 0, 0, 0, 0, 4363,
                                                  4520, 4677, 4835, 4992, 5148};
  passed &= expect(sub.succeeded() &&
                       std::equal(sub_colors.begin(), sub_colors.end(),
                                  sub_expected.begin()),
                   "spectrum range must come from the selection");
  const auto before = atom_colors(*harness.workspace);
  passed &= expect(!harness.run("spectrum", {{"expression", "b"},
                                             {"palette", "no_such_palette"}})
                        .succeeded() &&
                       !harness.run("spectrum", {{"expression", "not_a_property"},
                                                 {"palette", "rainbow"}})
                            .succeeded() &&
                       !harness.run("spectrum", {{"expression", "count"},
                                                 {"palette", "red"}})
                            .succeeded() &&
                       !harness.run("spectrum", {{"expression", "count"},
                                                 {"palette", "notacolor blue"}})
                            .succeeded() &&
                       harness.run("spectrum", {{"expression", "b"},
                                                {"palette", "rainbow"},
                                                {"selection", "resi 99"}})
                           .succeeded() &&
                       atom_colors(*harness.workspace) == before,
                   "spectrum failures and empty selections must not mutate state");

  // Background, ambiguity and front/back.
  const auto ambiguous = harness.run("bg_color", {{"color", "wh"}});
  const auto unknown = harness.run("background set", {{"color", "notacolor"}});
  const auto set = harness.run("background set", {{"color", "0x336699"}});
  const auto got = harness.run("background get", {});
  passed &= expect(!ambiguous.succeeded() && !unknown.succeeded() &&
                       set.succeeded() && got.succeeded() &&
                       harness.workspace->background().color_index ==
                           std::optional<std::int32_t>{0x40336699},
                   "background set must resolve like color and fail explicitly");
  passed &= expect(!harness.run("color", {{"color", "bl"}, {"selection", "all"}})
                        .succeeded() &&
                       harness.run("setting set", {{"name", "sphere_color"},
                                                   {"value", "front"}})
                           .succeeded(),
                   "color rejects ambiguous prefixes; front is a setting token");
  const auto *object = harness.workspace->active_object();
  const auto &sphere = object->representations.front().packet.spheres.front();
  passed &= expect(std::abs(sphere.color.red - 0.8F) < 1.0e-6F &&
                       std::abs(sphere.color.green - 0.6F) < 1.0e-6F &&
                       std::abs(sphere.color.blue - 0.4F) < 1.0e-6F,
                   "front must be the inverse of the background");
  passed &= expect(harness.run("background set", {{"color", "white"}}).succeeded() &&
                       std::abs(harness.workspace->active_object()
                                    ->representations.front()
                                    .packet.spheres.front()
                                    .color.red) < 1.0e-6F,
                   "background changes must rebuild front/back colors");

  // Session journal replay restores spectrum colors and background.
  const auto document = application::capture_session_document(
      *harness.workspace, std::string{version()});
  Harness restored;
  const auto replay =
      document.has_value()
          ? application::replay_session(document.value(), restored.dispatcher,
                                        restored.context)
          : operation::Result<application::SessionReplayResult>::failure(
                document.error());
  passed &= expect(replay.has_value() &&
                       atom_colors(*restored.workspace) ==
                           atom_colors(*harness.workspace) &&
                       restored.workspace->color_table() ==
                           harness.workspace->color_table(),
                   "session replay must restore spectrum and background state");
  std::filesystem::remove_all(directory);
  return passed ? 0 : 1;
}
