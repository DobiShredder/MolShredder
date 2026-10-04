#include <array>
#include <cctype>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/operation/task_context.hpp"

// Expected values: PyMOL 3.1.0 black-box captures
// docs/research/pymol-color-system-blackbox_20261002T090000Z (load colors,
// object auto colors, object-name patterns) and
// docs/research/pymol-color-scope-blackbox_20261003T210000Z (selection
// operations over two 1ubq objects).

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
  bool run(std::string name, command::Arguments arguments) {
    return dispatcher.dispatch({std::move(name), std::move(arguments)}, context)
        .succeeded();
  }
};

// Synthetic one-residue molecule written like the capture's read_pdbstr.
std::filesystem::path molecule(const std::string &name,
                               const std::vector<std::string> &elements) {
  const auto path = std::filesystem::temp_directory_path() /
                    ("molshredder_color_scope_" + name + ".pdb");
  std::ofstream output{path};
  for (std::size_t index = 0; index < elements.size(); ++index) {
    const auto &element = elements[index];
    const auto atom = (element + "1").substr(0, 4);
    std::string symbol = element;
    for (auto &character : symbol)
      character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    char line[96];
    std::snprintf(line, sizeof(line),
                  "HETATM%5zu %-4s LIG A   1    %8.3f%8.3f%8.3f  1.00  0.00          %2s",
                  index + 1U, atom.c_str(), 1.5 * static_cast<double>(index + 1U),
                  0.0, 0.0, symbol.c_str());
    output << line << '\n';
  }
  output << "END\n";
  return path;
}

// Color index of every atom of an object keyed by atom name.
std::map<std::string, std::int32_t> colors_by_name(const application::WorkspaceObject &object) {
  std::map<std::string, std::int32_t> colors;
  const auto &topology = *object.system->topology();
  if (object.atom_colors == nullptr)
    return colors;
  for (const auto &[id, color] : *object.atom_colors) {
    const auto index = topology.atom_index(model::AtomId{id});
    if (index.has_value())
      colors[topology.atoms()[index->value].name] = color;
  }
  return colors;
}

std::size_t visible(const application::WorkspaceObject &object,
                    render::RepresentationKind kind) {
  const auto count = object.representation_visibility.visible_count(kind);
  return count.has_value() ? count.value() : 9999U;
}

std::int32_t ca_color(const application::WorkspaceObject &object, std::int64_t residue) {
  const auto &topology = *object.system->topology();
  for (std::size_t atom = 0; atom < topology.atom_count(); ++atom) {
    const auto &record = topology.atoms()[atom];
    if (record.name != "CA" ||
        topology.residues()[record.residue.value].sequence_number != residue)
      continue;
    const auto id = topology.atom_id(model::AtomIndex{atom});
    if (object.atom_colors != nullptr)
      for (const auto &[atom_id, color] : *object.atom_colors)
        if (id.has_value() && atom_id == id->value)
          return color;
    return -1000;
  }
  return -2000;
}

} // namespace

int main(int argc, char **argv) {
  bool passed = true;
  if (argc != 2) {
    std::cerr << "expected 1ubq fixture path\n";
    return 1;
  }

  // auto_color on: carbon = object auto color, others = element colors.
  // The capture's unknown element "X" is omitted: the PDB reader rejects it.
  {
    Harness harness;
    const std::vector<std::string> elements{"H", "C", "N", "O", "S", "P", "F", "Cl", "Br", "I",
                                            "Na", "K", "Ca", "Mg", "Fe", "Zn", "Cu", "Se"};
    const std::map<std::string, std::int32_t> expected{
        {"P1", 5291}, {"C1", 26}, {"N1", 27}, {"O1", 28}, {"S1", 31}, {"Ca1", 5295},
        {"Cl1", 5292}, {"Cu1", 5304}, {"Na1", 5287}, {"Se1", 5309}, {"Br1", 5310},
        {"F1", 5285}, {"Fe1", 5301}, {"I1", 5328}, {"K1", 5294}, {"Mg1", 5288},
        {"Zn1", 5305}, {"H1", 29}};
    const auto path = molecule("ligx", elements);
    const bool loaded =
        harness.run("setting set", {{"name", "auto_color"}, {"value", "true"}}) &&
        harness.run("load", {{"file-format", "pdb"}, {"name", "ligx"}, {"path", path.string()}});
    passed &= expect(loaded && colors_by_name(*harness.workspace->active_object()) == expected,
                     "auto_color load colors must match PyMOL");
    std::filesystem::remove(path);
  }

  // Object auto colors, object-name pattern and union in workspace scope.
  {
    Harness harness;
    bool ok = harness.run("setting set", {{"name", "auto_color"}, {"value", "true"}});
    std::vector<std::filesystem::path> paths;
    for (int index = 0; index < 4; ++index) {
      paths.push_back(molecule("obj" + std::to_string(index), {"C", "N"}));
      ok = ok && harness.run("load", {{"file-format", "pdb"},
                                      {"name", "obj" + std::to_string(index)},
                                      {"path", paths.back().string()}});
    }
    const auto carbon_colors = [&] {
      std::vector<std::int32_t> colors;
      for (const auto &object : harness.workspace->objects())
        colors.push_back(colors_by_name(object)["C1"]);
      return colors;
    };
    bool nitrogen = ok;
    for (const auto &object : harness.workspace->objects())
      nitrogen = nitrogen && colors_by_name(object)["N1"] == 27;
    passed &= expect(ok && nitrogen && carbon_colors() == std::vector<std::int32_t>{26, 5, 154, 6},
                     "new objects must take the PyMOL auto color cycle");
    passed &= expect(harness.run("color", {{"color", "blue"}, {"scope", "workspace"},
                                           {"selection", "model obj*"}}) &&
                         carbon_colors() == std::vector<std::int32_t>{2, 2, 2, 2},
                     "workspace color must reach every object matching obj*");
    passed &= expect(harness.run("color", {{"color", "yellow"}, {"scope", "workspace"},
                                           {"selection", "model obj1 or model obj3"}}) &&
                         carbon_colors() == std::vector<std::int32_t>{2, 6, 2, 6},
                     "workspace color must follow an object union");
    passed &= expect(harness.run("color", {{"color", "red"}, {"selection", "all"}}) &&
                         carbon_colors() == std::vector<std::int32_t>{2, 6, 2, 4},
                     "default scope must stay on the active object");
    for (const auto &path : paths)
      std::filesystem::remove(path);
  }

  // Selection operations over two 1ubq objects.
  {
    Harness harness;
    const auto source = std::filesystem::path{argv[1]}.string();
    bool ok = harness.run("setting set", {{"name", "auto_color"}, {"value", "true"}}) &&
              harness.run("load", {{"file-format", "pdb"}, {"name", "prota"}, {"path", source}}) &&
              harness.run("load", {{"file-format", "pdb"}, {"name", "protb"}, {"path", source}}) &&
              harness.run("hide", {{"representation", "everything"}, {"scope", "workspace"},
                                   {"selection", "all"}});
    passed &= expect(ok, "two 1ubq objects must load");
    // lines, sticks, spheres for prota then protb after each step.
    constexpr std::array<std::array<std::size_t, 6>, 5> expected{{
    {0, 78, 0, 0, 78, 0}, // show sticks, resi 1-10
    {0, 71, 0, 0, 71, 0}, // hide sticks, resi 5
    {0, 46, 25, 0, 46, 25}, // as spheres, resi 1-3
    {0, 0, 25, 0, 46, 25}, // toggle sticks, prota and resi 1-20
    {0, 0, 25, 0, 0, 25}, // toggle sticks, resi 1-20
    }};
    const std::array<std::array<std::string, 3>, 5> steps{{
        {"show", "sticks", "resi 1-10"},
        {"hide", "sticks", "resi 5"},
        {"as", "spheres", "resi 1-3"},
        {"toggle", "sticks", "model prota and resi 1-20"},
        {"toggle", "sticks", "resi 1-20"},
    }};
    for (std::size_t step = 0; step < steps.size(); ++step) {
      const bool applied = harness.run(
          steps[step][0], {{"representation", steps[step][1]}, {"scope", "workspace"},
                           {"selection", steps[step][2]}});
      const auto &objects = harness.workspace->objects();
      const std::array<std::size_t, 6> actual{
          visible(objects[0], render::RepresentationKind::lines),
          visible(objects[0], render::RepresentationKind::sticks),
          visible(objects[0], render::RepresentationKind::spheres),
          visible(objects[1], render::RepresentationKind::lines),
          visible(objects[1], render::RepresentationKind::sticks),
          visible(objects[1], render::RepresentationKind::spheres)};
      passed &= expect(applied && actual == expected[step],
                       "workspace visibility drifted at step " + steps[step][0] + " " +
                           steps[step][2]);
    }
    const bool colored =
        harness.run("color", {{"color", "red"}, {"scope", "workspace"}, {"selection", "resi 1"}}) &&
        harness.run("color", {{"color", "blue"}, {"scope", "workspace"},
                              {"selection", "model protb and resi 2"}}) &&
        harness.run("color", {{"color", "4"}, {"scope", "workspace"},
                              {"selection", "model prota* and resi 3"}});
    const auto &objects = harness.workspace->objects();
    const std::array<std::int32_t, 6> colors{
        ca_color(objects[0], 1), ca_color(objects[0], 2), ca_color(objects[0], 3),
        ca_color(objects[1], 1), ca_color(objects[1], 2), ca_color(objects[1], 3)};
    passed &= expect(colored && colors == std::array<std::int32_t, 6>{4, 26, 4, 4, 2, 5},
                     "workspace colors must match PyMOL over two objects");
  }
  return passed ? 0 : 1;
}
