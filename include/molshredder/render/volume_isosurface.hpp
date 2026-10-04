#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "molshredder/model/volume.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/render/packet.hpp"

namespace molshredder::render {

struct IsosurfaceStyle {
  double level{};
  ColorRgba color{0.2F, 0.65F, 1.0F, 1.0F};
};

// Cell triangulation. Tetrahedra split every cell into six tetrahedra;
// cubes use the 256-case table observed from PyMOL 3.1.0 so mesh topology
// matches its contours (SEQ-311 S14).
enum class IsosurfaceTessellation { marching_tetrahedra, marching_cubes };

struct IsosurfaceRequest {
  const model::VolumeGrid* grid{};
  std::uint64_t scene_node_id{};
  IsosurfaceStyle style;
  operation::TaskContext* context{};
  std::size_t memory_budget_bytes{std::numeric_limits<std::size_t>::max()};
  IsosurfaceTessellation tessellation{IsosurfaceTessellation::marching_tetrahedra};
  // Marching cubes only: also emit, as packet lines, the triangle edges that lie
  // in grid planes (PyMOL isomesh draws the contour in each grid plane).
  bool plane_edges{false};
};

[[nodiscard]] operation::Result<RenderPacket> build_isosurface(
    const IsosurfaceRequest& request);

}  // namespace molshredder::render
