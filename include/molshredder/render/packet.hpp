#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "molshredder/model/topology.hpp"
#include "molshredder/scene/math.hpp"

namespace molshredder::render {

struct ColorRgba {
  float red{1.0F};
  float green{1.0F};
  float blue{1.0F};
  float alpha{1.0F};

  friend bool operator==(const ColorRgba&, const ColorRgba&) = default;
};

enum class PickKind { atom, bond, residue, volume };

struct PickTarget {
  PickTarget() = default;
  PickTarget(PickKind target_kind, std::uint64_t target_scene_node_id,
             std::optional<model::AtomIndex> target_atom = std::nullopt,
             std::optional<std::size_t> target_bond_index = std::nullopt,
             std::optional<model::ResidueIndex> target_residue = std::nullopt,
             std::optional<std::array<std::size_t, 3U>> target_volume_sample =
                 std::nullopt)
      : kind{target_kind}, scene_node_id{target_scene_node_id},
        atom{target_atom}, bond_index{target_bond_index},
        residue{target_residue}, volume_sample{target_volume_sample} {}

  PickKind kind{PickKind::atom};
  std::uint64_t scene_node_id{};
  std::optional<model::AtomIndex> atom;
  std::optional<std::size_t> bond_index;
  std::optional<model::ResidueIndex> residue;
  std::optional<std::array<std::size_t, 3U>> volume_sample;

  friend bool operator==(const PickTarget&, const PickTarget&) = default;
};

struct LineInstance {
  model::Vec3d start;
  model::Vec3d end;
  ColorRgba start_color;
  ColorRgba end_color;
  float width_pixels{1.0F};
  std::uint64_t pick_id{};
};

struct CylinderInstance {
  model::Vec3d start;
  model::Vec3d end;
  double radius{0.15};
  ColorRgba color;
  std::uint64_t pick_id{};
};

struct SphereInstance {
  model::Vec3d center;
  double radius{1.0};
  ColorRgba color;
  std::uint64_t pick_id{};
};

// Screen-facing text is retained as typed scene data. GPU and reference
// frontends may choose their own deterministic glyph backend.
struct TextLabelInstance {
  model::Vec3d anchor;
  std::string text;
  ColorRgba color;
  std::uint64_t pick_id{};
};

struct MeshVertex {
  model::Vec3d position;
  model::Vec3d normal;
  ColorRgba color;

  friend bool operator==(const MeshVertex &, const MeshVertex &) = default;
};

struct MeshTriangle {
  std::uint32_t first{};
  std::uint32_t second{};
  std::uint32_t third{};
  std::uint64_t pick_id{};

  friend bool operator==(const MeshTriangle &, const MeshTriangle &) = default;
};

struct Bounds3d {
  model::Vec3d minimum;
  model::Vec3d maximum;
  bool empty{true};
};

struct RenderPacket {
  std::uint64_t topology_version{};
  std::size_t frame_index{};
  std::uint64_t scene_node_id{};
  std::vector<LineInstance> lines;
  std::vector<CylinderInstance> cylinders;
  std::vector<SphereInstance> spheres;
  std::vector<TextLabelInstance> labels;
  std::vector<MeshVertex> mesh_vertices;
  std::vector<MeshTriangle> mesh_triangles;
  std::map<std::uint64_t, PickTarget> pick_targets;
  std::map<std::string, std::string, std::less<>> provenance;
  Bounds3d bounds;
};

[[nodiscard]] bool is_valid(ColorRgba color) noexcept;
void include(Bounds3d& bounds, model::Vec3d point, double radius = 0.0);

}  // namespace molshredder::render
