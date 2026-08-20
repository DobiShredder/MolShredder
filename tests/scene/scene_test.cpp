#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <string_view>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/molecular_system.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/model/volume.hpp"
#include "molshredder/scene/math.hpp"
#include "molshredder/scene/scene.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool near(double left, double right, double tolerance = 1.0e-10) {
  return std::abs(left - right) <= tolerance;
}

std::shared_ptr<const molshredder::model::MolecularSystem> make_system() {
  using namespace molshredder::model;
  TopologyBuilder topology_builder;
  const auto topology = topology_builder.build();
  const auto coordinates = InMemoryCoordinateSource::create(0, {});
  return MolecularSystem::create(77, "empty", topology.value(),
                                 coordinates.value())
      .value();
}

} // namespace

int main() {
  using namespace molshredder;
  using scene::NodeId;
  using scene::NodeKind;
  using scene::Quaterniond;
  using scene::Transform;
  using scene::operator-;

  bool passed = true;
  const auto quarter_turn = scene::quaternion_from_axis_angle(
      model::Vec3d{0.0, 0.0, 1.0}, std::acos(-1.0) * 0.5);
  const auto rotated = scene::rotate(quarter_turn, model::Vec3d{1.0, 0.0, 0.0});
  passed &= expect(near(rotated.x, 0.0) && near(rotated.y, 1.0),
                   "quaternion rotation must be right handed");

  scene::SceneBuilder builder;
  const auto group =
      builder.add_group(NodeId{0}, "protein group",
                        Transform{{10.0, 0.0, 0.0}, {}, {1.0, 1.0, 1.0}});
  const auto system =
      builder.add_system(group.value(), "molecule", make_system(),
                         Transform{{2.0, 0.0, 0.0}, {}, {1.0, 1.0, 1.0}});
  const auto overlay = builder.add_group(NodeId{0}, "overlay");
  passed &= expect(group.has_value() && group.value().value == 1 &&
                       system.has_value() && system.value().value == 2 &&
                       overlay.has_value() && overlay.value().value == 3,
                   "scene node IDs must initially be dense and monotonic");
  const auto orphan = builder.add_group(NodeId{999}, "orphan");
  passed &= expect(!orphan.has_value() &&
                       orphan.error().code == operation::ErrorCode::not_found &&
                       !builder.add_group(NodeId{0}, "").has_value(),
                   "scene nodes require an existing parent and a name");
  passed &= expect(
      !builder.set_selection({system.value(), overlay.value(), system.value()})
              .has_value() &&
          !builder.set_visible(group.value(), false).has_value(),
      "selection must deduplicate and visibility must update");

  const auto first = builder.build();
  passed &= expect(first.has_value() && first.value()->version() == 1 &&
                       first.value()->node_count() == 4,
                   "first immutable scene snapshot must have version one");
  if (first.has_value()) {
    passed &= expect(first.value()->preorder() ==
                         std::vector<NodeId>{NodeId{0}, group.value(),
                                             system.value(), overlay.value()},
                     "preorder must preserve child order");
    const auto *molecule = first.value()->find(system.value());
    passed &= expect(
        molecule != nullptr && molecule->kind() == NodeKind::molecular_system &&
            molecule->system() != nullptr && molecule->system()->id() == 77,
        "system node must retain immutable molecular system");
    passed &= expect(!first.value()->effectively_visible(system.value()) &&
                         first.value()->effectively_visible(overlay.value()),
                     "ancestor visibility must propagate to descendants");
    passed &= expect(first.value()->selection() ==
                         std::set<NodeId>{system.value(), overlay.value()},
                     "selection snapshot must be sorted and deterministic");
    const auto world = first.value()->world_transform(system.value());
    passed &=
        expect(world.has_value() && scene::transform_point(world.value(), {}) ==
                                        model::Vec3d{12.0, 0.0, 0.0},
               "world transform must compose parent before child");
  }

  passed &= expect(builder.reparent(group.value(), system.value()).has_value(),
                   "reparent under a descendant must reject cycles");
  passed &=
      expect(builder.reparent(overlay.value(), NodeId{0}, 2).has_value() &&
                 !builder.reparent(overlay.value(), NodeId{0}, 0).has_value(),
             "same-parent reorder must validate against final child count");
  passed &= expect(builder.reparent(NodeId{0}, group.value()).has_value() &&
                       builder.remove_subtree(NodeId{0}).has_value(),
                   "scene root cannot be reparented or removed");
  passed &= expect(
      builder.set_transform(system.value(),
                            Transform{{}, Quaterniond{}, {1.0, 0.0, 1.0}})
              .has_value() &&
          builder.set_selection({NodeId{999}}).has_value(),
      "invalid transform and unknown selection must fail");

  if (first.has_value()) {
    auto next = scene::SceneBuilder::from(*first.value());
    passed &=
        expect(!next.reparent(overlay.value(), group.value(), 0).has_value() &&
                   !next.set_visible(group.value(), true).has_value() &&
                   !next.remove_subtree(system.value()).has_value(),
               "next builder must support reparent, visibility and removal");
    const auto replacement = next.add_group(group.value(), "replacement");
    const auto second = next.build();
    passed &= expect(replacement.has_value() && replacement.value().value == 4,
                     "removed IDs must not be reused");
    passed &=
        expect(second.has_value() && second.value()->version() == 2 &&
                   second.value()->find(system.value()) == nullptr &&
                   !second.value()->selection().contains(system.value()),
               "subtree removal must clear stale selection in new version");
    passed &= expect(first.value()->version() == 1 &&
                         first.value()->find(system.value()) != nullptr &&
                         !first.value()->effectively_visible(system.value()),
                     "new builder must not mutate the prior scene snapshot");
  }

  passed &= expect(first.has_value() &&
                       !first.value()->world_transform(NodeId{999}).has_value(),
                   "unknown world-transform lookup must fail");

  const auto pivot_quarter_turn = scene::quaternion_from_axis_angle(
      {0.0, 0.0, 1.0}, std::acos(-1.0) * 0.5);
  const scene::Transform pivoted{{}, pivot_quarter_turn, {1.0, 1.0, 1.0},
                                 {1.0, 0.0, 0.0}};
  const auto pivot_matrix = scene::matrix(pivoted);
  passed &= expect(
      scene::length(scene::transform_point(pivot_matrix, {1.0, 0.0, 0.0}) -
                    model::Vec3d{1.0, 0.0, 0.0}) < 1.0e-12 &&
          scene::length(scene::transform_point(pivot_matrix, {2.0, 0.0, 0.0}) -
                        model::Vec3d{1.0, 1.0, 0.0}) < 1.0e-12,
      "pivoted transform must keep its pivot fixed during rotation");

  const auto volume = model::VolumeGrid::create(
      {1U, 1U, 1U}, {},
      std::array<model::Vec3d, 3U>{
          {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}},
      model::VolumeScalarBuffer{std::vector<double>{2.0}});
  scene::SceneBuilder volume_builder;
  const auto volume_node =
      volume_builder.add_volume(NodeId{0}, "potential", volume.value());
  const auto volume_scene = volume_builder.build();
  passed &=
      expect(volume_node.has_value() && volume_scene.has_value() &&
                 volume_scene.value()->find(volume_node.value())->kind() ==
                     NodeKind::volume &&
                 volume_scene.value()->find(volume_node.value())->volume() ==
                     volume.value() &&
                 !volume_builder.add_volume(NodeId{0}, "null", {}).has_value(),
             "scene must retain typed volume nodes and reject null grids");
  return passed ? 0 : 1;
}
