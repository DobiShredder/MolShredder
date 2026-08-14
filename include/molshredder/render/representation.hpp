#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/analysis/secondary_structure.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/render/packet.hpp"

namespace molshredder::render {

enum class RepresentationKind { lines, sticks, spheres, ribbon, cartoon };

struct AtomVisual {
  ColorRgba color;
  double sphere_radius{1.0};
};

struct RepresentationStyle {
  RepresentationKind kind{RepresentationKind::lines};
  float line_width_pixels{1.0F};
  double stick_radius{0.15};
  double sphere_scale{1.0};
  std::size_t backbone_samples_per_residue{4U};
  double ribbon_width{0.50};
  double cartoon_coil_width{0.35};
  double cartoon_helix_width{1.00};
  double cartoon_sheet_width{1.20};
  double cartoon_thickness{0.16};
  double cartoon_chain_break_distance{4.50};
};

struct RepresentationRequest {
  const model::Topology* topology{};
  const model::CoordinateFrame* frame{};
  std::size_t frame_index{};
  std::uint64_t scene_node_id{};
  std::span<const AtomVisual> atom_visuals;
  std::span<const std::uint8_t> selected;
  RepresentationStyle style;
  std::span<const analysis::SecondaryStructureState> secondary_structure;
};

[[nodiscard]] operation::Result<RenderPacket> build_representation(
    const RepresentationRequest& request);

}  // namespace molshredder::render
