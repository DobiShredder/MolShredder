#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "molshredder/model/volume.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/render/direct_volume.hpp"

namespace {
bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}
} // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  std::vector<float> values(27U, 0.0F);
  values[(1U * 3U + 1U) * 3U + 1U] = 1.0F;
  const auto grid = model::VolumeGrid::create(
      {3U, 3U, 3U}, {-1.0, -1.0, -1.0},
      {{{1.0, 0.0, 0.0}, {0.2, 1.0, 0.0}, {0.0, 0.1, 1.0}}},
      model::VolumeScalarBuffer{std::move(values)});
  const auto transfer = render::TransferFunction::create(
      {{0.0, {0.0F, 0.0F, 0.0F, 0.0F}},
       {1.0, {1.0F, 0.4F, 0.1F, 1.0F}}})
                            .value();
  render::DirectVolumeRequest request;
  request.grid = grid.value();
  request.transfer_function = &transfer;
  request.scene_node_id = 51U;
  request.style.sampling_step = 0.25;
  request.style.maximum_steps = 128U;
  request.style.lookup_table_samples = 64U;
  request.style.texture_budget_bytes = 1024U * 1024U;
  const auto volume = render::build_direct_volume(request);
  passed &= expect(volume.has_value() &&
                       volume.value().normalized_scalars.size() == 27U &&
                       volume.value().transfer_lookup.size() == 64U &&
                       volume.value().required_texture_bytes ==
                           27U * sizeof(float) +
                               64U * sizeof(render::ColorRgba),
                   "direct volume must prepare bounded scalar and transfer textures");

  auto parameters = scene::Camera::create().value().parameters();
  parameters.target = {0.2, 0.1, 0.0};
  parameters.distance = 6.0;
  parameters.projection = scene::ProjectionMode::orthographic;
  parameters.orthographic_height = 4.0;
  parameters.aspect_ratio = 1.0;
  parameters.near_clip = 0.1;
  parameters.far_clip = 100.0;
  const auto camera = scene::Camera::create(parameters);
  render::ReferenceRenderSettings settings;
  settings.width = 64U;
  settings.height = 64U;
  const auto first = volume.has_value() && camera.has_value()
                         ? render::render_direct_volume_reference(
                               volume.value(), camera.value(), settings, 7U)
                         : operation::Result<render::ImageRgba8>::failure(
                               {operation::ErrorCode::internal,
                                "direct volume setup failed", {}});
  const auto second = volume.has_value() && camera.has_value()
                          ? render::render_direct_volume_reference(
                                volume.value(), camera.value(), settings, 7U)
                          : operation::Result<render::ImageRgba8>::failure(
                                {operation::ErrorCode::internal,
                                 "direct volume setup failed", {}});
  bool picked{};
  if (first.has_value()) {
    for (const auto id : first.value().pick_ids)
      picked = picked || id == 7U;
  }
  passed &= expect(first.has_value() && second.has_value() && picked &&
                       render::image_checksum(first.value()) != 0U &&
                       render::image_checksum(first.value()) ==
                           render::image_checksum(second.value()) &&
                       first.value().pixels == second.value().pixels &&
                       first.value().pick_ids == second.value().pick_ids,
                   "fixed-camera post-classified ray integration must be deterministic and pickable");

  request.style.texture_budget_bytes = 1U;
  const auto budget = render::build_direct_volume(request);
  passed &= expect(!budget.has_value() &&
                       budget.error().code ==
                           operation::ErrorCode::resource_exhausted,
                   "direct volume texture budget must fail explicitly");
  request.style.texture_budget_bytes = 1024U * 1024U;
  double progress{};
  operation::TaskContext progress_context{
      {}, [&](const auto &update) { progress = update.fraction; }};
  request.context = &progress_context;
  const auto progressed = render::build_direct_volume(request);
  passed &= expect(progressed.has_value() && progress == 1.0,
                   "direct-volume preparation must report bounded progress");
  request.style.mode = render::VolumeClassificationMode::pre_integrated;
  passed &= expect(!render::build_direct_volume(request).has_value(),
                   "unsupported pre-integrated mode must fail instead of falling back silently");
  operation::TaskContext context;
  context.cancellation.request_cancel();
  request.style.mode = render::VolumeClassificationMode::post_classified;
  request.context = &context;
  const auto cancelled = render::build_direct_volume(request);
  passed &= expect(!cancelled.has_value() &&
                       cancelled.error().code == operation::ErrorCode::cancelled,
                   "cancelled preparation must not publish partial texture data");
  return passed ? 0 : 1;
}
