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
  bool passed = true;
  passed &= expect(argc == 3,
                   "volume workspace test requires OpenDX and MRC fixtures");
  if (argc != 3)
    return 1;

  auto workspace = std::make_shared<application::Workspace>();
  auto registry = application::make_default_registry(workspace);
  const application::Dispatcher dispatcher{registry};
  const gui::ActionAdapter actions{dispatcher};
  operation::TaskContext context;
  const auto loaded =
      actions.trigger({"volume load",
                       {{"path", std::filesystem::path{argv[1]}.string()},
                        {"name", "electrostatic"},
                        {"file-format", "opendx"},
                        {"coordinate-unit", "nanometer"}}},
                      context);
  passed &= expect(loaded.succeeded() && workspace->volume_count() == 1U &&
                       workspace->object_count() == 0U &&
                       workspace->scene()->node_count() == 2U,
                   "shared action must load an OpenDX volume scene object");
  if (workspace->volume_count() == 1U) {
    const auto &volume = workspace->volumes().front();
    const auto *node = workspace->scene()->find(volume.scene_node);
    passed &=
        expect(volume.id == 1U && volume.name == "electrostatic" &&
                   volume.grid->shape() == model::VolumeShape{2U, 2U, 3U} &&
                   volume.grid->metadata().coordinate_unit ==
                       operation::LengthUnit::nanometer &&
                   node != nullptr && node->kind() == scene::NodeKind::volume &&
                   node->volume() == volume.grid &&
                   workspace->scene()->selection().contains(volume.scene_node),
               "volume load must preserve identity, unit and scene ownership");
  }

  const auto mrc_loaded =
      actions.trigger({"volume load",
                       {{"path", std::filesystem::path{argv[2]}.string()},
                        {"name", "density"},
                        {"file-format", "mrc"},
                        {"coordinate-unit", "angstrom"}}},
                      context);
  passed &=
      expect(mrc_loaded.succeeded() && workspace->volume_count() == 2U &&
                 workspace->scene()->node_count() == 3U,
             "shared action must add an MRC volume without replacing OpenDX");
  if (workspace->volume_count() == 2U) {
    const auto &volume = workspace->volumes().back();
    const auto *node = workspace->scene()->find(volume.scene_node);
    passed &= expect(
        volume.id == 2U && volume.name == "density" &&
            volume.grid->shape() == model::VolumeShape{2U, 2U, 3U} &&
            volume.grid->metadata().fields.at("format") == "mrc" &&
            node != nullptr && node->volume() == volume.grid &&
            workspace->scene()->selection().contains(volume.scene_node),
        "MRC volume must preserve shared identity and typed scene ownership");
  }

  const auto before_scene = workspace->scene();
  const auto duplicate = workspace->load_volume(
      argv[1], std::string{"electrostatic"}, io::VolumeFormat::opendx,
      operation::LengthUnit::angstrom);
  passed &= expect(!duplicate.has_value() && workspace->volume_count() == 2U &&
                       workspace->scene() == before_scene,
                   "duplicate volume load must be failure-atomic");

  const auto listed = actions.trigger({"volume list", {}}, context);
  passed &= expect(listed.succeeded(),
                   "volume list must share the GUI/CLI/Python registry");

  const auto isosurface = actions.trigger(
      {"volume isosurface",
       {{"level", "2.5"}, {"color", "orange"}, {"opacity", "0.5"}}},
      context);
  passed &= expect(isosurface.succeeded() &&
                       workspace->volumes().back().representations.size() == 1U &&
                       !workspace->volumes().back()
                            .representations.front()
                            .mesh_triangles.empty(),
                   "shared action must create an active-volume isosurface");
  const auto representation_count =
      workspace->volumes().back().representations.size();
  const auto invalid_isosurface = actions.trigger(
      {"volume isosurface", {{"level", "2.5"}, {"opacity", "1.5"}}},
      context);
  passed &= expect(!invalid_isosurface.succeeded() &&
                       workspace->volumes().back().representations.size() ==
                           representation_count,
                   "invalid isosurface actions must be failure-atomic");
  return passed ? 0 : 1;
}
