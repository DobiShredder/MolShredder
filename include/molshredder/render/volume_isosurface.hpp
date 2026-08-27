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

struct IsosurfaceRequest {
  const model::VolumeGrid* grid{};
  std::uint64_t scene_node_id{};
  IsosurfaceStyle style;
  operation::TaskContext* context{};
  std::size_t memory_budget_bytes{std::numeric_limits<std::size_t>::max()};
};

[[nodiscard]] operation::Result<RenderPacket> build_isosurface(
    const IsosurfaceRequest& request);

}  // namespace molshredder::render
