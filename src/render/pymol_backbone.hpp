#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "molshredder/model/topology.hpp"
#include "molshredder/render/packet.hpp"
#include "molshredder/render/representation.hpp"

// Backbone cartoon and ribbon geometry that follows the observable PyMOL
// 3.1.0 construction (guide atoms, orientation refinement, sheet flattening,
// Hermite sampling and profile extrusion). Independently written from
// black-box exports and the read-only pinned source; no PyMOL code copied.
namespace molshredder::render::detail {

struct PymolBackboneInput {
  const model::Topology *topology{};
  std::span<const model::Vec3d> positions;
  // Atom has coordinates in the frame / is shown by this representation.
  std::span<const std::uint8_t> present;
  std::span<const std::uint8_t> visible;
  std::span<const AtomVisual> visuals;
  // Per residue: 'H' helix, 'S' sheet, anything else loop.
  std::span<const char> residue_ss;
  // Per atom PyMOL cartoon type code (empty: all automatic).
  std::span<const std::int32_t> atom_cartoon_type;
  // Packet length unit per Angstrom (0.1 for nanometer frames).
  double unit_scale{1.0};
  // Residue pick id provider.
  std::function<std::uint64_t(model::ResidueIndex)> residue_pick;
};

struct PymolCartoonParameters {
  int sampling{7};
  int gap_cutoff{10};
  double rect_length{1.4};
  double rect_width{0.4};
  double oval_length{1.35};
  double oval_width{0.25};
  double loop_radius{0.2};
  double tube_radius{0.5};
  int loop_quality{6};
  int oval_quality{10};
  int tube_quality{9};
  double power{2.0};
  double power_b{0.52};
  double throw_factor{1.35};
  int refine{5};
  int flat_cycles{4};
  double refine_tips{10.0};
};

struct PymolRibbonParameters {
  int sampling{1};
  double width_pixels{3.0};
  double power{2.0};
  double power_b{0.5};
  double throw_factor{1.35};
};

// Appends cartoon mesh triangles; returns PyMOL type codes that were drawn
// with a fallback profile (dumbbell, putty, cylinder).
std::vector<std::int32_t> append_pymol_cartoon(const PymolBackboneInput &input,
                                               const PymolCartoonParameters &parameters,
                                               RenderPacket &packet);

// Appends ribbon line segments (half colored, width in pixels).
void append_pymol_ribbon(const PymolBackboneInput &input,
                         const PymolRibbonParameters &parameters,
                         RenderPacket &packet);

} // namespace molshredder::render::detail
