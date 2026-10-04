#include <array>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/session.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/version.hpp"

// Expected 8-bit colors: ray pixels of the same probes in
// docs/research/pymol-ramp-blackbox_20261003T120000Z (Open-Source PyMOL 3.1.0,
// ambient-only lighting); compared within 2/255 for pixel quantization.

namespace {

using namespace molshredder;

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

using Pixels = std::array<std::array<int, 3>, 8>;

struct Case {
  std::string_view name;
  std::string_view source;
  std::string_view range;
  std::string_view colors;
  Pixels expected;
};

struct Harness {
  std::shared_ptr<application::Workspace> workspace =
      std::make_shared<application::Workspace>();
  command::Registry registry = application::make_default_registry(workspace);
  application::Dispatcher dispatcher{registry};
  operation::TaskContext context;
  application::DispatchOutcome run(std::string name, command::Arguments arguments) {
    return dispatcher.dispatch({std::move(name), std::move(arguments)}, context);
  }
};

std::vector<render::ColorRgba> probe_colors(const application::Workspace &workspace) {
  std::vector<render::ColorRgba> colors;
  for (const auto &record : workspace.active_object()->representations)
    if (record.kind == render::RepresentationKind::spheres)
      for (const auto &sphere : record.packet.spheres)
        colors.push_back(sphere.color);
  return colors;
}

bool matches(const std::vector<render::ColorRgba> &actual, const Pixels &expected) {
  if (actual.size() != expected.size())
    return false;
  for (std::size_t index = 0; index < expected.size(); ++index) {
    const std::array<float, 3> rgb{actual[index].red, actual[index].green,
                                   actual[index].blue};
    for (std::size_t channel = 0; channel < 3U; ++channel)
      if (std::abs(rgb[channel] * 255.0F -
                   static_cast<float>(expected[index][channel])) > 2.0F) {
        std::cerr << "  probe " << index + 1U << " channel " << channel << ": "
                  << rgb[channel] * 255.0F << " vs " << expected[index][channel]
                  << '\n';
        return false;
      }
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  bool passed = true;
  if (argc != 2) {
    std::cerr << "expected contour fixture directory\n";
    return 1;
  }
  const std::filesystem::path fixtures{argv[1]};
  Harness harness;
  passed &= expect(
      harness.run("load", {{"file-format", "pdb"}, {"name", "lig"},
                           {"path", (fixtures / "contour.pdb").string()}})
              .succeeded() &&
          harness.run("volume load", {{"name", "dens"},
                                      {"path", (fixtures / "contour.ccp4").string()}})
              .succeeded() &&
          harness.run("load", {{"file-format", "pdb"}, {"name", "probe"},
                               {"path", (fixtures / "probe.pdb").string()}})
              .succeeded() &&
          harness.run("show", {{"representation", "spheres"}, {"selection", "all"}})
              .succeeded(),
      "ramp fixtures must load");

  const std::vector<Case> cases{
      {"r1", "dens", "0 0.5 1", "blue white red", {{{255, 0, 0}, {255, 0, 0}, {255, 0, 0}, {255, 252, 252}, {0, 0, 255}, {0, 0, 255}, {0, 0, 255}, {0, 0, 255}}}},
      {"r2", "dens", "0 1", "blue white red", {{{255, 0, 0}, {255, 0, 0}, {255, 0, 0}, {255, 252, 252}, {0, 0, 255}, {0, 0, 255}, {0, 0, 255}, {0, 0, 255}}}},
      {"r3", "dens", "0 1", "rainbow", {{{255, 0, 0}, {255, 0, 0}, {255, 0, 0}, {129, 254, 179}, {127, 0, 255}, {127, 0, 255}, {127, 0, 255}, {127, 0, 255}}}},
      {"r4", "dens", "0 1", "hot", {{{255, 255, 255}, {255, 255, 255}, {255, 255, 255}, {255, 130, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}},
      {"r5", "lig", "0 2 4", "red green blue", {{{255, 0, 0}, {185, 69, 0}, {112, 142, 0}, {63, 191, 0}, {0, 255, 0}, {0, 68, 186}, {0, 0, 255}, {0, 0, 255}}}},
      {"r6", "dens", "", "", {{{0, 0, 255}, {0, 0, 255}, {0, 0, 255}, {126, 126, 255}, {255, 206, 206}, {255, 186, 186}, {255, 186, 186}, {255, 186, 186}}}},
  };
  for (const auto &item : cases) {
    command::Arguments arguments{{"name", std::string{item.name}},
                                 {"source", std::string{item.source}}};
    if (!item.range.empty())
      arguments.emplace("range", std::string{item.range});
    if (!item.colors.empty())
      arguments.emplace("color", std::string{item.colors});
    const auto created = harness.run("ramp_new", arguments);
    const auto colored = harness.run(
        "color", {{"color", std::string{item.name}}, {"selection", "all"}});
    passed &= expect(created.succeeded() && colored.succeeded() &&
                         matches(probe_colors(*harness.workspace), item.expected),
                     std::string{"ramp colors drifted: "} + std::string{item.name});
  }

  // ramp_update recolors atoms already using the ramp.
  harness.run("color", {{"color", "r1"}, {"selection", "all"}});
  const auto updated = harness.run("ramp_update", {{"color", "green white magenta"},
                                                   {"name", "r1"},
                                                   {"range", "0 0.25 0.5"}});
  const Pixels update_expected{{{255, 0, 255}, {255, 0, 255}, {255, 0, 255}, {255, 0, 255}, {0, 255, 0}, {0, 255, 0}, {0, 255, 0}, {0, 255, 0}}};
  passed &= expect(updated.succeeded() &&
                       matches(probe_colors(*harness.workspace), update_expected),
                   "ramp_update must recolor ramp atoms");
  const auto mismatch = harness.run("ramp_new", {{"color", "red blue"},
                                                 {"name", "rv"},
                                                 {"range", "0 0.5 1"},
                                                 {"source", "dens"}});
  harness.run("color", {{"color", "rv"}, {"selection", "all"}});
  const Pixels mismatch_expected{{{0, 0, 255}, {0, 0, 255}, {0, 0, 255}, {0, 0, 255}, {255, 0, 0}, {255, 0, 0}, {255, 0, 0}, {255, 0, 0}}};
  // PyMOL 3.1.0 returns pure blue for the mid-range probe here (apparently
  // a padding artifact); MolShredder follows the documented even respacing
  // of levels, so only the saturated end probes are compared.
  {
    const auto colors = probe_colors(*harness.workspace);
    Pixels ends = mismatch_expected;
    bool same_ends = mismatch.succeeded() && colors.size() == 8U;
    for (const auto index : {0U, 1U, 2U, 4U, 5U, 6U, 7U})
      same_ends = same_ends &&
                  std::abs(colors[index].red * 255.0F -
                           static_cast<float>(ends[index][0])) <= 2.0F &&
                  std::abs(colors[index].blue * 255.0F -
                           static_cast<float>(ends[index][2])) <= 2.0F;
    passed &= expect(same_ends && colors[3].red > 0.3F && colors[3].blue > 0.3F,
                     "fewer colors than levels must respace levels evenly");
  }

  passed &= expect(
      !harness.run("ramp_new", {{"name", "rx"}, {"source", "nothing"}}).succeeded() &&
          !harness.run("ramp_new", {{"name", "ry"}, {"range", "[a, b]"},
                                    {"source", "dens"}})
               .succeeded() &&
          !harness.run("ramp_new", {{"color", "nosuchspectrum"}, {"name", "rz"},
                                    {"source", "dens"}})
               .succeeded() &&
          !harness.run("ramp_update", {{"name", "nothing"}, {"range", "0 1"}})
               .succeeded(),
      "ramp errors must be explicit");
  passed &= expect(harness.workspace->color_table().ramp_code("r1") ==
                       std::optional<std::int32_t>{-10} &&
                       harness.workspace->color_table().ramp_code("rv") ==
                           std::optional<std::int32_t>{-16},
                   "ramp color codes follow creation order from -10");

  // Proximity sources (S8 capture docs/research/pymol-spectrum-ramp-state-blackbox_20261003T180000Z):
  // PyMOL measures against state 1 of the source whatever the current frame,
  // and follows later coordinate edits of that state.
  {
    const auto two_state = std::filesystem::temp_directory_path() /
                           "molshredder_ramp_two_state_lig.pdb";
    {
      std::ifstream input{fixtures / "contour.pdb"};
      std::vector<std::string> atoms;
      for (std::string line; std::getline(input, line);)
        if (line.rfind("HETATM", 0) == 0)
          atoms.push_back(line);
      std::ofstream output{two_state};
      for (int model = 1; model <= 2; ++model) {
        output << "MODEL        " << model << '\n';
        for (auto line : atoms) {
          if (model == 2) {
            char field[16];
            std::snprintf(field, sizeof(field), "%8.3f",
                          std::stod(line.substr(30U, 8U)) + 2.0);
            line.replace(30U, 8U, field);
          }
          output << line << '\n';
        }
        output << "ENDMDL\n";
      }
      output << "END\n";
    }
    Harness sources;
    const auto load = [&](std::string name, const std::filesystem::path &path) {
      return sources.run("load", {{"file-format", "pdb"}, {"name", std::move(name)},
                                  {"path", path.string()}})
          .succeeded();
    };
    bool loaded = load("lig2", two_state) && load("lig1", fixtures / "contour.pdb") &&
                  load("probe", fixtures / "probe.pdb") &&
                  sources.run("show", {{"representation", "spheres"}, {"selection", "all"}})
                      .succeeded();
    const auto id = [&](std::size_t index) {
      return std::to_string(sources.workspace->objects()[index].id);
    };
    const Pixels state1_expected{{{255, 0, 0}, {185, 69, 0}, {112, 142, 0}, {63, 191, 0}, {0, 255, 0}, {0, 68, 186}, {0, 0, 255}, {0, 0, 255}}};
    loaded = loaded &&
             sources.run("object activate", {{"id", id(0)}}).succeeded() &&
             sources.run("traj frame", {{"frame", "1"}}).succeeded() &&
             sources.run("ramp_new", {{"color", "red green blue"}, {"name", "rs"},
                                      {"range", "0 2 4"}, {"source", "lig2"}})
                 .succeeded() &&
             sources.run("object activate", {{"id", id(2)}}).succeeded() &&
             sources.run("color", {{"color", "rs"}, {"selection", "all"}}).succeeded();
    passed &= expect(loaded && matches(probe_colors(*sources.workspace), state1_expected),
                     "proximity ramps must measure against source state 1");

    const Pixels moved_expected{{{0, 255, 0}, {61, 193, 0}, {112, 142, 0}, {98, 156, 0}, {112, 142, 0}, {0, 0, 255}, {0, 197, 57}, {0, 0, 255}}};
    bool moved =
        sources.run("ramp_new", {{"color", "red green blue"}, {"name", "rm"},
                                 {"range", "0 2 4"}, {"source", "lig1"}})
            .succeeded() &&
        sources.run("color", {{"color", "rm"}, {"selection", "all"}}).succeeded() &&
        sources.run("object activate", {{"id", id(1)}}).succeeded();
    const auto *lig1 = sources.workspace->active_object();
    if (moved && lig1 != nullptr) {
      const auto frame = lig1->system->coordinates()->read_frame(0U);
      const auto &topology = *lig1->system->topology();
      moved = frame.has_value();
      for (std::size_t atom = 0; moved && atom < topology.atom_count(); ++atom) {
        const auto *current = sources.workspace->active_object();
        const auto position = std::visit(
            [atom](const auto &values) {
              return model::Vec3d{static_cast<double>(values[atom].x) + 2.0,
                                  static_cast<double>(values[atom].y),
                                  static_cast<double>(values[atom].z)};
            },
            frame.value()->positions().values());
        moved = sources.workspace
                    ->set_active_atom_position(
                        *topology.atom_id(model::AtomIndex{atom}), position,
                        current->system->topology()->version(),
                        current->coordinate_source_revision)
                    .has_value();
      }
    }
    moved = moved && sources.run("object activate", {{"id", id(2)}}).succeeded();
    passed &= expect(moved && matches(probe_colors(*sources.workspace), moved_expected),
                     "proximity ramps must follow coordinate edits of the source");
    std::error_code removed;
    std::filesystem::remove(two_state, removed);
  }

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
                       probe_colors(*restored.workspace) ==
                           probe_colors(*harness.workspace),
                   "session replay must restore ramps and ramp colors");
  return passed ? 0 : 1;
}
