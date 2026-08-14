#include "molshredder/scene/scene.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::scene {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

operation::Error missing(NodeId id) {
  return operation::Error{
      operation::ErrorCode::not_found,
      "scene node does not exist: " + std::to_string(id.value),
      "use a node ID from the current scene snapshot"};
}

void append_preorder(const std::map<NodeId, SceneNode>& nodes, NodeId id,
                     std::vector<NodeId>& result) {
  const auto found = nodes.find(id);
  if (found == nodes.end()) {
    return;
  }
  result.push_back(id);
  for (const auto child : found->second.children()) {
    append_preorder(nodes, child, result);
  }
}

}  // namespace

const SceneNode* Scene::find(NodeId id) const noexcept {
  const auto found = nodes_.find(id);
  return found == nodes_.end() ? nullptr : &found->second;
}

std::vector<NodeId> Scene::preorder() const {
  std::vector<NodeId> result;
  result.reserve(nodes_.size());
  append_preorder(nodes_, root(), result);
  return result;
}

bool Scene::effectively_visible(NodeId id) const noexcept {
  const auto* node = find(id);
  while (node != nullptr) {
    if (!node->visible()) {
      return false;
    }
    if (!node->parent().has_value()) {
      return true;
    }
    node = find(*node->parent());
  }
  return false;
}

operation::Result<Matrix4d> Scene::world_transform(NodeId id) const {
  const auto* node = find(id);
  if (node == nullptr) {
    return operation::Result<Matrix4d>::failure(missing(id));
  }
  std::vector<const SceneNode*> ancestry;
  while (node != nullptr) {
    ancestry.push_back(node);
    node = node->parent().has_value() ? find(*node->parent()) : nullptr;
  }
  Matrix4d result;
  for (auto iterator = ancestry.rbegin(); iterator != ancestry.rend();
       ++iterator) {
    result = result * matrix((*iterator)->local_transform());
  }
  return operation::Result<Matrix4d>::success(result);
}

SceneBuilder::SceneBuilder() {
  SceneNode root;
  root.id_ = NodeId{0};
  root.kind_ = NodeKind::root;
  root.name_ = "Scene";
  root.parent_ = std::nullopt;
  nodes_.emplace(root.id_, std::move(root));
}

SceneBuilder SceneBuilder::from(const Scene& scene) {
  SceneBuilder builder;
  builder.base_version_ = scene.version_;
  builder.next_id_ = scene.next_id_;
  builder.nodes_ = scene.nodes_;
  builder.selection_ = scene.selection_;
  return builder;
}

SceneNode* SceneBuilder::find(NodeId id) noexcept {
  const auto found = nodes_.find(id);
  return found == nodes_.end() ? nullptr : &found->second;
}

const SceneNode* SceneBuilder::find(NodeId id) const noexcept {
  const auto found = nodes_.find(id);
  return found == nodes_.end() ? nullptr : &found->second;
}

operation::Result<NodeId> SceneBuilder::add_group(NodeId parent,
                                                   std::string name,
                                                   Transform transform) {
  return add_node(parent, NodeKind::group, std::move(name), transform, nullptr);
}

operation::Result<NodeId> SceneBuilder::add_system(
    NodeId parent, std::string name,
    std::shared_ptr<const model::MolecularSystem> system,
    Transform transform) {
  if (system == nullptr) {
    return operation::Result<NodeId>::failure(
        invalid("molecular-system scene node requires a system"));
  }
  return add_node(parent, NodeKind::molecular_system, std::move(name), transform,
                  std::move(system));
}

operation::Result<NodeId> SceneBuilder::add_node(
    NodeId parent, NodeKind kind, std::string name, Transform transform,
    std::shared_ptr<const model::MolecularSystem> system) {
  auto* parent_node = find(parent);
  if (parent_node == nullptr) {
    return operation::Result<NodeId>::failure(missing(parent));
  }
  if (name.empty()) {
    return operation::Result<NodeId>::failure(
        invalid("scene node name must not be empty"));
  }
  if (!is_valid(transform)) {
    return operation::Result<NodeId>::failure(invalid(
        "scene transform must contain finite translation, normalized rotation, "
        "and positive finite scale"));
  }
  if (next_id_ == std::numeric_limits<std::uint64_t>::max()) {
    return operation::Result<NodeId>::failure(
        invalid("scene node ID space is exhausted"));
  }
  const NodeId id{next_id_++};
  SceneNode node;
  node.id_ = id;
  node.kind_ = kind;
  node.name_ = std::move(name);
  node.parent_ = parent;
  node.local_transform_ = transform;
  node.system_ = std::move(system);
  nodes_.emplace(id, std::move(node));
  parent_node = find(parent);
  parent_node->children_.push_back(id);
  return operation::Result<NodeId>::success(id);
}

std::optional<operation::Error> SceneBuilder::rename(NodeId id,
                                                     std::string name) {
  auto* node = find(id);
  if (node == nullptr) {
    return missing(id);
  }
  if (name.empty()) {
    return invalid("scene node name must not be empty");
  }
  node->name_ = std::move(name);
  return std::nullopt;
}

std::optional<operation::Error> SceneBuilder::set_visible(NodeId id,
                                                          bool visible) {
  auto* node = find(id);
  if (node == nullptr) {
    return missing(id);
  }
  node->visible_ = visible;
  return std::nullopt;
}

std::optional<operation::Error> SceneBuilder::set_transform(
    NodeId id, Transform transform) {
  auto* node = find(id);
  if (node == nullptr) {
    return missing(id);
  }
  if (!is_valid(transform)) {
    return invalid("scene transform is invalid");
  }
  node->local_transform_ = transform;
  return std::nullopt;
}

std::optional<operation::Error> SceneBuilder::replace_system(
    NodeId id, std::shared_ptr<const model::MolecularSystem> system) {
  auto* node = find(id);
  if (node == nullptr) return missing(id);
  if (node->kind_ != NodeKind::molecular_system) {
    return invalid("only molecular-system scene nodes can replace a system");
  }
  if (system == nullptr) {
    return invalid("replacement molecular system must not be null");
  }
  node->system_ = std::move(system);
  return std::nullopt;
}

bool SceneBuilder::is_descendant(NodeId possible_descendant,
                                 NodeId ancestor) const noexcept {
  const auto* node = find(possible_descendant);
  while (node != nullptr && node->parent().has_value()) {
    if (*node->parent() == ancestor) {
      return true;
    }
    node = find(*node->parent());
  }
  return false;
}

std::optional<operation::Error> SceneBuilder::reparent(
    NodeId id, NodeId new_parent, std::optional<std::size_t> position) {
  auto* node = find(id);
  auto* parent = find(new_parent);
  if (node == nullptr) {
    return missing(id);
  }
  if (parent == nullptr) {
    return missing(new_parent);
  }
  if (id.value == 0) {
    return invalid("scene root cannot be reparented");
  }
  if (id == new_parent || is_descendant(new_parent, id)) {
    return invalid("scene reparent would create a cycle");
  }
  const auto old_parent_id = *node->parent_;
  const auto future_child_count =
      parent->children_.size() - (old_parent_id == new_parent ? 1U : 0U);
  if (position.has_value() && *position > future_child_count) {
    return invalid("scene child insertion position is out of range");
  }
  auto* old_parent = find(old_parent_id);
  std::erase(old_parent->children_, id);
  parent = find(new_parent);
  const auto insertion = position.value_or(parent->children_.size());
  parent->children_.insert(
      parent->children_.begin() + static_cast<std::ptrdiff_t>(insertion), id);
  node = find(id);
  node->parent_ = new_parent;
  return std::nullopt;
}

std::optional<operation::Error> SceneBuilder::remove_subtree(NodeId id) {
  auto* node = find(id);
  if (node == nullptr) {
    return missing(id);
  }
  if (id.value == 0) {
    return invalid("scene root cannot be removed");
  }
  const auto parent_id = *node->parent_;
  std::vector<NodeId> subtree;
  append_preorder(nodes_, id, subtree);
  auto* parent = find(parent_id);
  std::erase(parent->children_, id);
  for (const auto child : subtree) {
    selection_.erase(child);
    nodes_.erase(child);
  }
  return std::nullopt;
}

std::optional<operation::Error> SceneBuilder::set_selection(
    std::vector<NodeId> ids) {
  std::set<NodeId> selection;
  for (const auto id : ids) {
    if (find(id) == nullptr) {
      return missing(id);
    }
    if (id.value == 0) {
      return invalid("scene root cannot be selected as a molecular object");
    }
    selection.insert(id);
  }
  selection_ = std::move(selection);
  return std::nullopt;
}

operation::Result<std::shared_ptr<const Scene>> SceneBuilder::build() const {
  if (base_version_ == std::numeric_limits<std::uint64_t>::max()) {
    return operation::Result<std::shared_ptr<const Scene>>::failure(
        invalid("scene version space is exhausted"));
  }
  auto scene = std::shared_ptr<Scene>(new Scene{});
  scene->version_ = base_version_ + 1;
  scene->next_id_ = next_id_;
  scene->nodes_ = nodes_;
  scene->selection_ = selection_;
  return operation::Result<std::shared_ptr<const Scene>>::success(
      std::move(scene));
}

}  // namespace molshredder::scene
