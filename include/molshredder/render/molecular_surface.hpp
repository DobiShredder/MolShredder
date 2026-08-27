#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/render/packet.hpp"
#include "molshredder/render/representation.hpp"

namespace molshredder::render {

enum class MolecularSurfaceKind { van_der_waals, solvent_accessible };

[[nodiscard]] constexpr std::string_view
to_string(MolecularSurfaceKind kind) noexcept {
  switch (kind) {
  case MolecularSurfaceKind::van_der_waals:
    return "van-der-waals";
  case MolecularSurfaceKind::solvent_accessible:
    return "solvent-accessible";
  }
  return "van-der-waals";
}

struct MolecularSurfaceStyle {
  MolecularSurfaceKind kind{MolecularSurfaceKind::solvent_accessible};
  double probe_radius_angstrom{1.4};
  double grid_spacing_angstrom{0.7};
  ColorRgba color{0.2F, 0.65F, 1.0F, 0.72F};
  std::size_t voxel_budget{8U * 1024U * 1024U};
  std::size_t memory_budget_bytes{512U * 1024U * 1024U};
};

struct MolecularSurfaceRequest {
  const model::Topology *topology{};
  const model::CoordinateFrame *frame{};
  std::size_t frame_index{};
  std::uint64_t scene_node_id{};
  std::span<const AtomVisual> atom_visuals;
  std::span<const std::uint8_t> selected;
  MolecularSurfaceStyle style;
  operation::TaskContext *context{};
};

[[nodiscard]] operation::Result<RenderPacket>
build_molecular_surface(const MolecularSurfaceRequest &request);

} // namespace molshredder::render
