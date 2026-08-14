#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/model/molecular_system.hpp"
#include "molshredder/model/volume.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/scene/math.hpp"

namespace molshredder::scene {

struct NodeId {
  std::uint64_t value{};

  friend bool operator==(const NodeId &, const NodeId &) = default;
  friend auto operator<=>(const NodeId &, const NodeId &) = default;
};

enum class NodeKind { root, group, molecular_system, volume };

class SceneNode {
public:
  [[nodiscard]] NodeId id() const noexcept { return id_; }
  [[nodiscard]] NodeKind kind() const noexcept { return kind_; }
  [[nodiscard]] const std::string &name() const noexcept { return name_; }
  [[nodiscard]] std::optional<NodeId> parent() const noexcept {
    return parent_;
  }
  [[nodiscard]] const std::vector<NodeId> &children() const noexcept {
    return children_;
  }
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] const Transform &local_transform() const noexcept {
    return local_transform_;
  }
  [[nodiscard]] const std::shared_ptr<const model::MolecularSystem> &
  system() const noexcept {
    return system_;
  }
  [[nodiscard]] const std::shared_ptr<const model::VolumeGrid> &
  volume() const noexcept {
    return volume_;
  }

private:
  friend class SceneBuilder;

  NodeId id_{};
  NodeKind kind_{NodeKind::group};
  std::string name_;
  std::optional<NodeId> parent_;
  std::vector<NodeId> children_;
  bool visible_{true};
  Transform local_transform_{};
  std::shared_ptr<const model::MolecularSystem> system_;
  std::shared_ptr<const model::VolumeGrid> volume_;
};

class Scene {
public:
  [[nodiscard]] std::uint64_t version() const noexcept { return version_; }
  [[nodiscard]] NodeId root() const noexcept { return NodeId{0}; }
  [[nodiscard]] std::size_t node_count() const noexcept {
    return nodes_.size();
  }
  [[nodiscard]] const SceneNode *find(NodeId id) const noexcept;
  [[nodiscard]] std::vector<NodeId> preorder() const;
  [[nodiscard]] const std::set<NodeId> &selection() const noexcept {
    return selection_;
  }
  [[nodiscard]] bool effectively_visible(NodeId id) const noexcept;
  [[nodiscard]] operation::Result<Matrix4d> world_transform(NodeId id) const;

private:
  friend class SceneBuilder;

  std::uint64_t version_{};
  std::uint64_t next_id_{1};
  std::map<NodeId, SceneNode> nodes_;
  std::set<NodeId> selection_;
};

class SceneBuilder {
public:
  SceneBuilder();

  [[nodiscard]] static SceneBuilder from(const Scene &scene);

  [[nodiscard]] operation::Result<NodeId>
  add_group(NodeId parent, std::string name, Transform transform = {});
  [[nodiscard]] operation::Result<NodeId>
  add_system(NodeId parent, std::string name,
             std::shared_ptr<const model::MolecularSystem> system,
             Transform transform = {});
  [[nodiscard]] operation::Result<NodeId>
  add_volume(NodeId parent, std::string name,
             std::shared_ptr<const model::VolumeGrid> volume,
             Transform transform = {});

  [[nodiscard]] std::optional<operation::Error> rename(NodeId id,
                                                       std::string name);
  [[nodiscard]] std::optional<operation::Error> set_visible(NodeId id,
                                                            bool visible);
  [[nodiscard]] std::optional<operation::Error>
  set_transform(NodeId id, Transform transform);
  [[nodiscard]] std::optional<operation::Error>
  replace_system(NodeId id,
                 std::shared_ptr<const model::MolecularSystem> system);
  [[nodiscard]] std::optional<operation::Error>
  reparent(NodeId id, NodeId new_parent,
           std::optional<std::size_t> position = {});
  [[nodiscard]] std::optional<operation::Error> remove_subtree(NodeId id);
  [[nodiscard]] std::optional<operation::Error>
  set_selection(std::vector<NodeId> ids);

  [[nodiscard]] operation::Result<std::shared_ptr<const Scene>> build() const;

private:
  [[nodiscard]] SceneNode *find(NodeId id) noexcept;
  [[nodiscard]] const SceneNode *find(NodeId id) const noexcept;
  [[nodiscard]] bool is_descendant(NodeId possible_descendant,
                                   NodeId ancestor) const noexcept;
  [[nodiscard]] operation::Result<NodeId>
  add_node(NodeId parent, NodeKind kind, std::string name, Transform transform,
           std::shared_ptr<const model::MolecularSystem> system,
           std::shared_ptr<const model::VolumeGrid> volume = {});

  std::uint64_t base_version_{};
  std::uint64_t next_id_{1};
  std::map<NodeId, SceneNode> nodes_;
  std::set<NodeId> selection_;
};

} // namespace molshredder::scene
