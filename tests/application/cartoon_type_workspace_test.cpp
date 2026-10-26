#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/operation/task_context.hpp"

namespace {

using namespace molshredder;

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

const render::RenderPacket *cartoon(const application::Workspace &workspace) {
  for (const auto &record : workspace.active_object()->representations)
    if (record.kind == render::RepresentationKind::cartoon)
      return &record.packet;
  return nullptr;
}

} // namespace

int main(int argc, char **argv) {
  bool passed = true;
  if (argc != 2) {
    std::cerr << "expected PDB fixture path\n";
    return 1;
  }
  auto workspace = std::make_shared<application::Workspace>();
  auto registry = application::make_default_registry(workspace);
  const application::Dispatcher dispatcher{registry};
  operation::TaskContext context;
  const auto run = [&](std::string name, command::Arguments arguments) {
    return dispatcher.dispatch({std::move(name), std::move(arguments)}, context);
  };
  passed &= expect(run("load", {{"file-format", "pdb"},
                                {"path", std::filesystem::path{argv[1]}.string()}})
                           .succeeded() &&
                       run("show", {{"representation", "cartoon"},
                                    {"selection", "all"}})
                           .succeeded(),
                   "cartoon fixture must load");
  const auto baseline = *cartoon(*workspace);

  // skip removes residues from the cartoon.
  passed &= expect(run("cartoon", {{"type", "skip"}, {"selection", "resi 1-20"}})
                       .succeeded(),
                   "skip assignment must succeed");
  const auto skipped = *cartoon(*workspace);
  passed &= expect(skipped.mesh_triangles.size() < baseline.mesh_triangles.size(),
                   "skip must remove residues from the cartoon");

  // automatic restores the original geometry exactly.
  run("cartoon", {{"type", "automatic"}, {"selection", "all"}});
  passed &= expect(cartoon(*workspace)->mesh_vertices == baseline.mesh_vertices,
                   "automatic must restore secondary-structure profiles");

  // tube thickens coil residues; cylinder is still reported as a fallback profile.
  run("cartoon", {{"type", "tube"}, {"selection", "all"}});
  const auto tube = *cartoon(*workspace);
  const auto span = [](const render::RenderPacket &packet) {
    return (packet.bounds.maximum.x - packet.bounds.minimum.x) +
           (packet.bounds.maximum.y - packet.bounds.minimum.y) +
           (packet.bounds.maximum.z - packet.bounds.minimum.z);
  };
  passed &= expect(tube.mesh_vertices != baseline.mesh_vertices &&
                       span(tube) > 0.0,
                   "tube must change the cartoon profile");
  run("cartoon", {{"type", "dumbbell"}, {"selection", "resi 30-40"}});
  passed &= expect(!cartoon(*workspace)->provenance.contains("cartoon_type_fallback") &&
                       cartoon(*workspace)->mesh_vertices != tube.mesh_vertices,
                   "dumbbell is a native profile");
  run("cartoon", {{"type", "putty"}, {"selection", "resi 10-30"}});
  passed &= expect(!cartoon(*workspace)->provenance.contains("cartoon_type_fallback"),
                   "putty is a native profile");
  run("cartoon", {{"type", "cylinder"}, {"selection", "resi 30-40"}});
  passed &= expect(cartoon(*workspace)->provenance.contains("cartoon_type_fallback"),
                   "types without native profiles must be reported as fallbacks");
  run("cartoon", {{"type", "rect"}, {"selection", "all"}});
  passed &= expect(cartoon(*workspace)->mesh_vertices != baseline.mesh_vertices &&
                       !cartoon(*workspace)->provenance.contains(
                           "cartoon_type_fallback"),
                   "a unique prefix must select rectangle");
  // Cartoon profile dimensions are render settings the geometry engine reads.
  const auto tube_extent = [&](const std::string &radius) {
    run("setting set", {{"name", "cartoon_tube_radius"}, {"value", radius}});
    run("cartoon", {{"type", "tube"}, {"selection", "all"}});
    const auto &bounds = cartoon(*workspace)->bounds;
    return bounds.maximum.x - bounds.minimum.x;
  };
  const auto thin = tube_extent("0.2");
  const auto thick = tube_extent("1.0");
  passed &= expect(thick > thin + 1.0, "cartoon_tube_radius changes the tube thickness");
  run("setting unset", {{"name", "cartoon_tube_radius"}});
  run("setting set", {{"name", "cartoon_oval_length"}, {"value", "3.0"}});
  run("cartoon", {{"type", "oval"}, {"selection", "all"}});
  const auto wide_oval = cartoon(*workspace)->mesh_vertices;
  run("setting unset", {{"name", "cartoon_oval_length"}});
  run("cartoon", {{"type", "oval"}, {"selection", "all"}});
  passed &= expect(wide_oval != cartoon(*workspace)->mesh_vertices, "cartoon_oval_length changes the oval profile");
  return passed ? 0 : 1;
}
