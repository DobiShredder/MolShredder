#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

#include "molshredder/model/volume.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/render/reference_renderer.hpp"
#include "molshredder/render/volume_slice.hpp"
#include "molshredder/scene/math.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

std::shared_ptr<const molshredder::model::VolumeGrid> make_grid() {
  using namespace molshredder;
  std::vector<double> values;
  for (std::size_t x = 0; x < 3U; ++x) {
    for (std::size_t y = 0; y < 4U; ++y) {
      for (std::size_t z = 0; z < 5U; ++z) {
        values.push_back(static_cast<double>(100U * x + 10U * y + z));
      }
    }
  }
  return model::VolumeGrid::create(
             {3U, 4U, 5U}, {1.0, -2.0, 0.5},
             {{{2.0, 0.0, 0.0}, {0.5, 1.0, 0.0}, {0.0, 0.25, 1.0}}},
             model::VolumeScalarBuffer{std::move(values)})
      .value();
}

} // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  const auto grid = make_grid();
  render::VolumeSliceRequest request;
  request.grid = grid.get();
  request.scene_node_id = 23U;
  request.style.axis = render::VolumeSliceAxis::x;
  request.style.index = 1U;
  request.style.minimum_color = {0.0F, 0.0F, 0.0F, 0.25F};
  request.style.maximum_color = {1.0F, 0.5F, 0.25F, 0.75F};
  request.style.memory_budget_bytes = 1U * 1024U * 1024U;

  operation::TaskContext context;
  double progress{};
  context.report_progress = [&progress](const operation::ProgressUpdate &update) {
    progress = update.fraction;
  };
  request.context = &context;
  const auto slice = render::build_volume_slice(request);
  passed &= expect(slice.has_value(), "valid volume slice must succeed");
  if (slice.has_value()) {
    const auto &packet = slice.value();
    passed &= expect(packet.scene_node_id == 23U &&
                         packet.mesh_vertices.size() == 20U &&
                         packet.mesh_triangles.size() == 24U &&
                         packet.pick_targets.size() == 1U &&
                         packet.provenance.at("algorithm") ==
                             "orthogonal-grid-slice" &&
                         packet.provenance.at("axis") == "x" &&
                         packet.provenance.at("index") == "1" &&
                         progress == 1.0,
                     "slice must expose deterministic geometry and provenance");
    const auto &pick = packet.pick_targets.at(1U);
    passed &= expect(pick.kind == render::PickKind::volume &&
                         pick.scene_node_id == 23U &&
                         pick.volume_sample ==
                             std::optional<std::array<std::size_t, 3U>>{
                                 std::array<std::size_t, 3U>{1U, 0U, 0U}},
                     "slice mesh must retain volume pick identity");
    const auto reciprocal_x = scene::cross(grid->deltas()[1], grid->deltas()[2]);
    const auto determinant = scene::dot(grid->deltas()[0], reciprocal_x);
    using scene::operator-;
    using scene::operator/;
    const auto logical_x_axis = reciprocal_x / determinant;
    const auto expected_normal = scene::normalized(reciprocal_x);
    for (const auto &vertex : packet.mesh_vertices) {
      passed &= expect(
          std::abs(scene::dot(vertex.position - grid->origin(), logical_x_axis) -
                   1.0) < 1.0e-12,
          "slice positions must follow the physical skewed grid basis");
      passed &= expect(scene::dot(vertex.normal, expected_normal) > 0.999999,
                       "slice normal must follow the selected physical axis");
    }
    for (const auto &triangle : packet.mesh_triangles) {
      const auto &a = packet.mesh_vertices[triangle.first].position;
      const auto &b = packet.mesh_vertices[triangle.second].position;
      const auto &c = packet.mesh_vertices[triangle.third].position;
      passed &= expect(scene::dot(scene::cross(b - a, c - a), expected_normal) >
                           0.0 &&
                           triangle.pick_id == 1U,
                       "slice winding and pick ID must be deterministic");
    }
  }

  const auto repeated = render::build_volume_slice(request);
  passed &= expect(repeated.has_value() && slice.has_value() &&
                       repeated.value().mesh_vertices ==
                           slice.value().mesh_vertices &&
                       repeated.value().mesh_triangles ==
                           slice.value().mesh_triangles,
                   "volume slice generation must be deterministic");

  request.style.axis = render::VolumeSliceAxis::z;
  request.style.index = 2U;
  const auto image_slice = render::build_volume_slice(request);
  auto camera_parameters = scene::Camera::create().value().parameters();
  camera_parameters.target = {3.5, 0.375, 2.5};
  camera_parameters.distance = 10.0;
  camera_parameters.projection = scene::ProjectionMode::orthographic;
  camera_parameters.orthographic_height = 8.0;
  camera_parameters.aspect_ratio = 1.0;
  camera_parameters.near_clip = 0.1;
  camera_parameters.far_clip = 100.0;
  const auto camera = scene::Camera::create(camera_parameters);
  render::ReferenceRenderSettings render_settings;
  render_settings.width = 64U;
  render_settings.height = 64U;
  const auto first_image =
      image_slice.has_value() && camera.has_value()
          ? render::render_reference(image_slice.value(), camera.value(),
                                     render_settings)
          : operation::Result<render::ImageRgba8>::failure(
                {operation::ErrorCode::internal, "slice setup failed", {}});
  const auto second_image =
      image_slice.has_value() && camera.has_value()
          ? render::render_reference(image_slice.value(), camera.value(),
                                     render_settings)
          : operation::Result<render::ImageRgba8>::failure(
                {operation::ErrorCode::internal, "slice setup failed", {}});
  bool found_volume_pick{};
  if (first_image.has_value()) {
    for (const auto pick_id : first_image.value().pick_ids)
      found_volume_pick = found_volume_pick || pick_id == 1U;
  }
  passed &= expect(
      first_image.has_value() && second_image.has_value() &&
          render::image_checksum(first_image.value()) != 0U &&
          render::image_checksum(first_image.value()) ==
              render::image_checksum(second_image.value()) &&
          first_image.value().pixels == second_image.value().pixels &&
          first_image.value().pick_ids == second_image.value().pick_ids &&
          found_volume_pick,
      "fixed-camera slice image and picking must be deterministic");

  request.style.axis = render::VolumeSliceAxis::x;
  request.style.index = 3U;
  const auto outside = render::build_volume_slice(request);
  passed &= expect(!outside.has_value() &&
                       outside.error().code ==
                           operation::ErrorCode::invalid_argument,
                   "out-of-range slice index must fail explicitly");
  request.style.index = 1U;
  request.style.memory_budget_bytes = 1U;
  const auto budget = render::build_volume_slice(request);
  passed &= expect(!budget.has_value() &&
                       budget.error().code ==
                           operation::ErrorCode::resource_exhausted,
                   "slice allocation must respect its explicit memory budget");
  request.style.memory_budget_bytes = 1U * 1024U * 1024U;
  context.cancellation.request_cancel();
  const auto cancelled = render::build_volume_slice(request);
  passed &= expect(!cancelled.has_value() &&
                       cancelled.error().code == operation::ErrorCode::cancelled,
                   "pre-cancelled slice must preserve caller state");
  request.grid = nullptr;
  const auto missing = render::build_volume_slice(request);
  passed &= expect(!missing.has_value(), "null volume grid must fail validation");
  return passed ? 0 : 1;
}
