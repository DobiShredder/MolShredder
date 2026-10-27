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
  // Per atom temperature factor (empty: all zero); the putty profile scales the tube by it.
  std::span<const double> b_factor;
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
  // PyMOL cartoon_flat_sheets / cartoon_smooth_loops (+ smooth_first/last/cycles).
  bool flat_sheets{true};
  bool smooth_loops{false};
  int smooth_first{1};
  int smooth_last{1};
  int smooth_cycles{2};
  // cartoon_fancy_helices draws automatic helices as dumbbells; cartoon_fancy_sheets (on by default)
  // draws automatic sheets with arrow heads, otherwise as rectangles.
  bool fancy_helices{false};
  bool fancy_sheets{true};
  // Dumbbell: a thin web with a rod along each edge.
  double dumbbell_length{1.6};
  double dumbbell_width{0.17};
  double dumbbell_radius{0.16};
  // Nucleic acid base ladder (cartoon_ladder_mode / cartoon_ladder_radius).
  int ladder_mode{1};
  double ladder_radius{0.25};
  int ladder_quality{12};
  // Putty: a tube whose radius follows the B-factor (cartoon_putty_*).
  double putty_radius{0.4};
  int putty_quality{9};
  double putty_scale_min{0.6};
  double putty_scale_max{4.0};
  double putty_scale_power{1.5};
  double putty_range{2.0};
  int putty_transform{0};
};

struct PymolRibbonParameters {
  int sampling{1};
  double width_pixels{3.0};
  double power{2.0};
  double power_b{0.5};
  double throw_factor{1.35};
};

// Appends cartoon mesh triangles; returns PyMOL type codes that were drawn
// with a fallback profile (cylinder).
std::vector<std::int32_t> append_pymol_cartoon(const PymolBackboneInput &input,
                                               const PymolCartoonParameters &parameters,
                                               RenderPacket &packet);

// Appends ribbon line segments (half colored, width in pixels).
void append_pymol_ribbon(const PymolBackboneInput &input,
                         const PymolRibbonParameters &parameters,
                         RenderPacket &packet);

} // namespace molshredder::render::detail
