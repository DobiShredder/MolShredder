#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/gui/action_adapter.hpp"
#include "molshredder/operation/task_context.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  if (argc != 2) {
    std::cerr << "molecular surface workspace test requires a PDB fixture\n";
    return 1;
  }
  bool passed = true;
  auto workspace = std::make_shared<application::Workspace>();
  auto registry = application::make_default_registry(workspace);
  const application::Dispatcher dispatcher{registry};
  const gui::ActionAdapter actions{dispatcher};
  operation::TaskContext context;
  const auto loaded = actions.trigger(
      {"load", {{"path", std::filesystem::path{argv[1]}.string()}}}, context);
  passed &= expect(loaded.succeeded(), "surface fixture must load");

  const auto shown = actions.trigger(
      {"surface show",
       {{"kind", "vdw"},
        {"selection", "all"},
        {"probe-radius", "0"},
        {"grid-spacing", "0.5"},
        {"color", "cyan"},
        {"opacity", "0.72"},
        {"voxel-budget", "100000"},
        {"memory-budget-bytes", "8388608"}}},
      context);
  const auto *object = workspace->active_object();
  passed &= expect(shown.succeeded() && object != nullptr &&
                       object->molecular_surface.has_value() &&
                       !object->molecular_surface->mesh_triangles.empty() &&
                       object->molecular_surface->pick_targets.size() == 1U,
                   "shared action must commit a pickable VDW surface");
  const auto before = object == nullptr || !object->molecular_surface.has_value()
                          ? std::vector<render::MeshTriangle>{}
                          : object->molecular_surface->mesh_triangles;
  const auto exhausted = actions.trigger(
      {"surface show",
       {{"kind", "sas"},
        {"probe-radius", "1.4"},
        {"grid-spacing", "0.1"},
        {"voxel-budget", "1"}}},
      context);
  object = workspace->active_object();
  passed &= expect(!exhausted.succeeded() && object != nullptr &&
                       object->molecular_surface.has_value() &&
                       object->molecular_surface->mesh_triangles == before,
                   "failed replacement must preserve the previous surface");
  return passed ? 0 : 1;
}
