#pragma once

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

enum class PickKind { atom, bond, residue };

struct PickTarget {
  PickKind kind{PickKind::atom};
  std::uint64_t scene_node_id{};
  std::optional<model::AtomIndex> atom;
  std::optional<std::size_t> bond_index;
  std::optional<model::ResidueIndex> residue;

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
};

struct MeshTriangle {
  std::uint32_t first{};
  std::uint32_t second{};
  std::uint32_t third{};
  std::uint64_t pick_id{};
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
