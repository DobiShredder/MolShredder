#include "molshredder/render/volume_slice.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "molshredder/operation/error.hpp"
#include "molshredder/scene/math.hpp"

namespace molshredder::render {
namespace {

operation::Error invalid(std::string message, std::string remediation = {}) {
  return {operation::ErrorCode::invalid_argument, std::move(message),
          std::move(remediation)};
}

operation::Error cancelled() {
  return {operation::ErrorCode::cancelled,
          "volume slice generation was cancelled",
          "retry the operation when the volume is ready"};
}

operation::Error exhausted(std::size_t required, std::size_t budget) {
  return {operation::ErrorCode::resource_exhausted,
          "volume slice requires " + std::to_string(required) +
              " bytes but the memory budget is " + std::to_string(budget),
          "increase --memory-budget-bytes or choose a smaller grid"};
}

bool cancellation_requested(const VolumeSliceRequest &request) noexcept {
  return request.context != nullptr &&
         request.context->cancellation.is_cancelled();
}

std::size_t axis_extent(const model::VolumeShape &shape,
                        VolumeSliceAxis axis) noexcept {
  switch (axis) {
  case VolumeSliceAxis::x:
    return shape.x;
  case VolumeSliceAxis::y:
    return shape.y;
  case VolumeSliceAxis::z:
    return shape.z;
  }
  return shape.x;
}

std::array<std::size_t, 2U>
plane_shape(const model::VolumeShape &shape, VolumeSliceAxis axis) noexcept {
  switch (axis) {
  case VolumeSliceAxis::x:
    return {shape.y, shape.z};
  case VolumeSliceAxis::y:
    return {shape.x, shape.z};
  case VolumeSliceAxis::z:
    return {shape.x, shape.y};
  }
  return {shape.y, shape.z};
}

std::array<std::size_t, 3U> grid_coordinate(VolumeSliceAxis axis,
                                           std::size_t index,
                                           std::size_t u,
                                           std::size_t v) noexcept {
  switch (axis) {
  case VolumeSliceAxis::x:
    return {index, u, v};
  case VolumeSliceAxis::y:
    return {u, index, v};
  case VolumeSliceAxis::z:
    return {u, v, index};
  }
  return {index, u, v};
}

model::Vec3d plane_normal(const model::VolumeGrid &grid,
                          VolumeSliceAxis axis) {
  const auto &deltas = grid.deltas();
  model::Vec3d normal;
  switch (axis) {
  case VolumeSliceAxis::x:
    normal = scene::cross(deltas[1], deltas[2]);
    break;
  case VolumeSliceAxis::y:
    normal = scene::cross(deltas[2], deltas[0]);
    break;
  case VolumeSliceAxis::z:
    normal = scene::cross(deltas[0], deltas[1]);
    break;
  }
  return scene::normalized(normal);
}

ColorRgba interpolate(ColorRgba minimum, ColorRgba maximum,
                      double fraction) noexcept {
  const auto mix = [fraction](float first, float second) {
    return static_cast<float>(static_cast<double>(first) +
                              (static_cast<double>(second) - first) * fraction);
  };
  return {mix(minimum.red, maximum.red),
          mix(minimum.green, maximum.green),
          mix(minimum.blue, maximum.blue),
          mix(minimum.alpha, maximum.alpha)};
}

bool multiply_overflows(std::size_t left, std::size_t right) noexcept {
  return right != 0U && left > std::numeric_limits<std::size_t>::max() / right;
}

operation::Result<std::size_t> required_bytes(std::size_t vertices,
                                              std::size_t triangles) {
  if (multiply_overflows(vertices, sizeof(MeshVertex)) ||
      multiply_overflows(triangles, sizeof(MeshTriangle))) {
    return operation::Result<std::size_t>::failure(
        invalid("volume slice allocation size overflow"));
  }
  const auto vertex_bytes = vertices * sizeof(MeshVertex);
  const auto triangle_bytes = triangles * sizeof(MeshTriangle);
  if (vertex_bytes > std::numeric_limits<std::size_t>::max() - triangle_bytes) {
    return operation::Result<std::size_t>::failure(
        invalid("volume slice allocation size overflow"));
  }
  return operation::Result<std::size_t>::success(vertex_bytes + triangle_bytes);
}

void add_oriented_triangle(RenderPacket &packet, std::uint32_t first,
                           std::uint32_t second, std::uint32_t third,
                           model::Vec3d expected_normal,
                           std::uint64_t pick_id) {
  using scene::operator-;
  const auto &a = packet.mesh_vertices[first].position;
  const auto &b = packet.mesh_vertices[second].position;
  const auto &c = packet.mesh_vertices[third].position;
  if (scene::dot(scene::cross(b - a, c - a), expected_normal) < 0.0) {
    std::swap(second, third);
  }
  packet.mesh_triangles.push_back({first, second, third, pick_id});
}

} // namespace

operation::Result<RenderPacket>
build_volume_slice(const VolumeSliceRequest &request) {
  if (request.grid == nullptr) {
    return operation::Result<RenderPacket>::failure(
        invalid("volume slice requires a volume grid"));
  }
  if (!is_valid(request.style.minimum_color) ||
      !is_valid(request.style.maximum_color)) {
    return operation::Result<RenderPacket>::failure(
        invalid("volume slice colors must contain finite values in [0, 1]"));
  }
  if (request.style.memory_budget_bytes == 0U) {
    return operation::Result<RenderPacket>::failure(
        invalid("volume slice memory budget must be positive"));
  }
  if (cancellation_requested(request)) {
    return operation::Result<RenderPacket>::failure(cancelled());
  }

  const auto &grid = *request.grid;
  const auto &shape = grid.shape();
  const auto extent = axis_extent(shape, request.style.axis);
  if (request.style.index >= extent) {
    return operation::Result<RenderPacket>::failure(invalid(
        "volume slice index is outside the selected axis",
        "choose an index in [0, " + std::to_string(extent - 1U) + "]"));
  }
  const auto plane = plane_shape(shape, request.style.axis);
  if (plane[0] < 2U || plane[1] < 2U) {
    return operation::Result<RenderPacket>::failure(invalid(
        "volume slice plane requires at least 2 x 2 samples"));
  }
  if (multiply_overflows(plane[0], plane[1]) ||
      multiply_overflows(plane[0] - 1U, plane[1] - 1U) ||
      multiply_overflows((plane[0] - 1U) * (plane[1] - 1U), 2U)) {
    return operation::Result<RenderPacket>::failure(
        invalid("volume slice primitive count overflow"));
  }
  const auto vertex_count = plane[0] * plane[1];
  const auto triangle_count = (plane[0] - 1U) * (plane[1] - 1U) * 2U;
  const auto bytes = required_bytes(vertex_count, triangle_count);
  if (!bytes.has_value()) {
    return operation::Result<RenderPacket>::failure(bytes.error());
  }
  if (bytes.value() > request.style.memory_budget_bytes) {
    return operation::Result<RenderPacket>::failure(
        exhausted(bytes.value(), request.style.memory_budget_bytes));
  }

  RenderPacket packet;
  packet.scene_node_id = request.scene_node_id;
  packet.provenance.emplace("algorithm", "orthogonal-grid-slice");
  packet.provenance.emplace("algorithm_version", "1");
  packet.provenance.emplace("axis", std::string{to_string(request.style.axis)});
  packet.provenance.emplace("index", std::to_string(request.style.index));
  packet.provenance.emplace("interpolation", "grid-sample-linear-color");
  packet.provenance.emplace("memory_budget_bytes",
                            std::to_string(request.style.memory_budget_bytes));
  packet.provenance.emplace("required_bytes", std::to_string(bytes.value()));
  packet.mesh_vertices.reserve(vertex_count);
  packet.mesh_triangles.reserve(triangle_count);

  const auto normal = plane_normal(grid, request.style.axis);
  const auto [minimum, maximum] = grid.scalars().range();
  const auto range = maximum - minimum;
  for (std::size_t u = 0; u < plane[0]; ++u) {
    if (cancellation_requested(request)) {
      return operation::Result<RenderPacket>::failure(cancelled());
    }
    if (request.context != nullptr && request.context->report_progress) {
      request.context->report_progress(
          {static_cast<double>(u) / static_cast<double>(plane[0]),
           "volume-slice"});
    }
    for (std::size_t v = 0; v < plane[1]; ++v) {
      const auto coordinate =
          grid_coordinate(request.style.axis, request.style.index, u, v);
      const auto value = grid.value(coordinate[0], coordinate[1], coordinate[2]);
      const auto fraction = range == 0.0
                                ? 0.5
                                : std::clamp((value - minimum) / range, 0.0, 1.0);
      const auto position =
          grid.position(coordinate[0], coordinate[1], coordinate[2]);
      packet.mesh_vertices.push_back(
          {position, normal,
           interpolate(request.style.minimum_color,
                       request.style.maximum_color, fraction)});
      include(packet.bounds, position);
    }
  }

  constexpr std::uint64_t kSlicePickId = 1U;
  packet.pick_targets.emplace(
      kSlicePickId,
      PickTarget{PickKind::volume, request.scene_node_id, std::nullopt,
                 std::nullopt, std::nullopt,
                 grid_coordinate(request.style.axis, request.style.index, 0U,
                                 0U)});
  const auto vertex = [plane](std::size_t u, std::size_t v) {
    return static_cast<std::uint32_t>(u * plane[1] + v);
  };
  for (std::size_t u = 0; u + 1U < plane[0]; ++u) {
    for (std::size_t v = 0; v + 1U < plane[1]; ++v) {
      const auto a = vertex(u, v);
      const auto b = vertex(u + 1U, v);
      const auto c = vertex(u + 1U, v + 1U);
      const auto d = vertex(u, v + 1U);
      add_oriented_triangle(packet, a, b, c, normal, kSlicePickId);
      add_oriented_triangle(packet, a, c, d, normal, kSlicePickId);
    }
  }
  if (request.context != nullptr && request.context->report_progress) {
    request.context->report_progress({1.0, "volume-slice"});
  }
  return operation::Result<RenderPacket>::success(std::move(packet));
}

} // namespace molshredder::render
