#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "molshredder/model/volume.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/render/packet.hpp"

namespace molshredder::render {

enum class VolumeSliceAxis { x, y, z };

[[nodiscard]] constexpr std::string_view
to_string(VolumeSliceAxis axis) noexcept {
  switch (axis) {
  case VolumeSliceAxis::x:
    return "x";
  case VolumeSliceAxis::y:
    return "y";
  case VolumeSliceAxis::z:
    return "z";
  }
  return "x";
}

struct VolumeSliceStyle {
  VolumeSliceAxis axis{VolumeSliceAxis::z};
  std::size_t index{};
  ColorRgba minimum_color{0.08F, 0.12F, 0.35F, 1.0F};
  ColorRgba maximum_color{1.0F, 0.35F, 0.05F, 1.0F};
  std::size_t memory_budget_bytes{64U * 1024U * 1024U};
};

struct VolumeSliceRequest {
  const model::VolumeGrid *grid{};
  std::uint64_t scene_node_id{};
  VolumeSliceStyle style;
  operation::TaskContext *context{};
};

[[nodiscard]] operation::Result<RenderPacket>
build_volume_slice(const VolumeSliceRequest &request);

} // namespace molshredder::render
