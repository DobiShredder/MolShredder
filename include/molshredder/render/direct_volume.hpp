#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "molshredder/model/volume.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/render/reference_renderer.hpp"
#include "molshredder/render/transfer_function.hpp"
#include "molshredder/scene/camera.hpp"

namespace molshredder::render {

enum class VolumeClassificationMode { post_classified, pre_integrated };

struct DirectVolumeStyle {
  VolumeClassificationMode mode{VolumeClassificationMode::post_classified};
  double sampling_step{0.5};
  std::size_t maximum_steps{4096U};
  std::size_t lookup_table_samples{256U};
  std::size_t texture_budget_bytes{512U * 1024U * 1024U};
};

struct DirectVolumeData {
  model::VolumeShape shape;
  model::Vec3d origin;
  std::array<model::Vec3d, 3U> deltas;
  std::vector<float> normalized_scalars;
  std::vector<ColorRgba> transfer_lookup;
  double scalar_minimum{};
  double scalar_maximum{};
  DirectVolumeStyle style;
  std::uint64_t scene_node_id{};
  std::size_t required_texture_bytes{};
};

struct DirectVolumeRequest {
  std::shared_ptr<const model::VolumeGrid> grid;
  const TransferFunction *transfer_function{};
  std::uint64_t scene_node_id{};
  DirectVolumeStyle style;
  operation::TaskContext *context{};
};

[[nodiscard]] operation::Result<std::size_t>
direct_volume_texture_bytes(const model::VolumeGrid &grid,
                            const DirectVolumeStyle &style);

[[nodiscard]] operation::Result<DirectVolumeData>
build_direct_volume(const DirectVolumeRequest &request);

[[nodiscard]] operation::Result<ImageRgba8> render_direct_volume_reference(
    const DirectVolumeData &volume, const scene::Camera &camera,
    ReferenceRenderSettings settings = {}, std::uint64_t pick_id = 1U);

} // namespace molshredder::render
