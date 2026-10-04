#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/analysis/secondary_structure.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/render/color_table.hpp"
#include "molshredder/render/packet.hpp"
#include "molshredder/render/setting_store.hpp"

namespace molshredder::render {

enum class RepresentationKind { lines, sticks, spheres, ribbon, cartoon, labels };

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

struct SphereBackendCapabilities {
  bool analytic_impostors{true};
  bool triangle_spheres{};
  bool point_sprites{};
  bool low_polyhedra{};
};

struct SphereModeResolution {
  std::int64_t requested{};
  std::int64_t effective{};
  bool fallback{};
  std::string reason;
};

[[nodiscard]] SphereModeResolution resolve_sphere_mode(
    std::int64_t requested, bool shader_enabled,
    SphereBackendCapabilities capabilities = {});

struct RepresentationRequest {
  const model::Topology* topology{};
  const model::CoordinateFrame* frame{};
  std::size_t frame_index{};
  std::uint64_t scene_node_id{};
  std::span<const AtomVisual> atom_visuals;
  std::span<const std::uint8_t> selected;
  RepresentationStyle style;
  std::span<const analysis::SecondaryStructureState> secondary_structure;
  const RenderSettingStore* settings{};
  std::uint64_t object_id{};
  SphereBackendCapabilities sphere_backend;
  // Resolves color-valued render settings; null limits them to atomic,
  // #RRGGBB and 0xRRGGBB tokens.
  const ColorTable* colors{};
  std::int32_t object_color{kColorDefault};
  // Atom label text keyed by persistent AtomId (sorted); used by labels.
  const std::vector<std::pair<std::uint64_t, std::string>>* atom_labels{};
  // PyMOL cartoon type codes keyed by persistent AtomId (sorted); the CA
  // atom's code selects the residue profile. Absent means automatic (0).
  const std::vector<std::pair<std::uint64_t, std::int32_t>>* cartoon_types{};
};

// PyMOL 3.1.0 cartoon type codes (cmd.cartoon TYPE).
struct CartoonTypeName {
  std::string_view name;
  std::int32_t code;
};
inline constexpr std::array<CartoonTypeName, 11> kCartoonTypes{{
    {"skip", -1}, {"automatic", 0}, {"loop", 1}, {"rectangle", 2},
    {"oval", 3}, {"tube", 4}, {"arrow", 5}, {"dumbbell", 6}, {"putty", 7},
    {"dash", 8}, {"cylinder", 9}}};

[[nodiscard]] operation::Result<RenderPacket> build_representation(
    const RepresentationRequest& request);

// Validates the explicit host-uint64 to GPU-uint32 mesh index boundary.
[[nodiscard]] operation::Result<std::uint32_t> checked_gpu_mesh_index(
    std::uint64_t index);

}  // namespace molshredder::render
