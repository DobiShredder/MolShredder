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

  // tube thickens coil residues; dumbbell is reported as a fallback profile.
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
  passed &= expect(cartoon(*workspace)->provenance.contains("cartoon_type_fallback"),
                   "types without native profiles must be reported as fallbacks");
  run("cartoon", {{"type", "rect"}, {"selection", "all"}});
  passed &= expect(cartoon(*workspace)->mesh_vertices != baseline.mesh_vertices &&
                       !cartoon(*workspace)->provenance.contains(
                           "cartoon_type_fallback"),
                   "a unique prefix must select rectangle");
  return passed ? 0 : 1;
}
