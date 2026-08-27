#include "molshredder/render/volume_isosurface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "molshredder/operation/error.hpp"
#include "molshredder/scene/math.hpp"

namespace molshredder::render {
namespace {

using EdgeKey = std::pair<std::size_t, std::size_t>;

constexpr std::array<std::array<std::size_t, 3U>, 8U> kCornerOffsets{{
    {0U, 0U, 0U}, {1U, 0U, 0U}, {0U, 1U, 0U}, {1U, 1U, 0U},
    {0U, 0U, 1U}, {1U, 0U, 1U}, {0U, 1U, 1U}, {1U, 1U, 1U},
}};

constexpr std::array<std::array<std::size_t, 4U>, 6U> kTetrahedra{{
    {0U, 1U, 3U, 7U}, {0U, 3U, 2U, 7U}, {0U, 2U, 6U, 7U},
    {0U, 6U, 4U, 7U}, {0U, 4U, 5U, 7U}, {0U, 5U, 1U, 7U},
}};

operation::Error invalid(std::string message) {
  return {operation::ErrorCode::invalid_argument, std::move(message), {}};
}

operation::Error exhausted(std::string message) {
  return {operation::ErrorCode::resource_exhausted, std::move(message),
          "increase the memory budget or use a coarser grid"};
}

operation::Error cancelled() {
  return {operation::ErrorCode::cancelled, "isosurface generation was cancelled",
          "Retry the operation when the volume is ready."};
}

bool cancellation_requested(const IsosurfaceRequest& request) noexcept {
  return request.context != nullptr &&
         request.context->cancellation.is_cancelled();
}

model::Vec3d logical_gradient(const model::VolumeGrid& grid, std::size_t x,
                              std::size_t y, std::size_t z) {
  const auto& shape = grid.shape();
  const auto derivative = [](std::size_t coordinate, std::size_t extent,
                             const auto& sample) {
    if (extent == 1U) return 0.0;
    if (coordinate == 0U) return sample(1U) - sample(0U);
    if (coordinate + 1U == extent) {
      return sample(coordinate) - sample(coordinate - 1U);
    }
    return (sample(coordinate + 1U) - sample(coordinate - 1U)) * 0.5;
  };
  return {
      derivative(x, shape.x,
                 [&](std::size_t index) { return grid.value(index, y, z); }),
      derivative(y, shape.y,
                 [&](std::size_t index) { return grid.value(x, index, z); }),
      derivative(z, shape.z,
                 [&](std::size_t index) { return grid.value(x, y, index); }),
  };
}

model::Vec3d physical_gradient(model::Vec3d logical,
                               const std::array<model::Vec3d, 3U>& deltas) {
  using scene::operator+;
  using scene::operator*;
  const auto determinant = scene::dot(deltas[0], scene::cross(deltas[1], deltas[2]));
  return scene::cross(deltas[1], deltas[2]) * (logical.x / determinant) +
         scene::cross(deltas[2], deltas[0]) * (logical.y / determinant) +
         scene::cross(deltas[0], deltas[1]) * (logical.z / determinant);
}

void orient_and_add(RenderPacket& packet, std::uint32_t first,
                    std::uint32_t second, std::uint32_t third) {
  using scene::operator+;
  using scene::operator-;
  const auto& a = packet.mesh_vertices[first];
  const auto& b = packet.mesh_vertices[second];
  const auto& c = packet.mesh_vertices[third];
  const auto face = scene::cross(b.position - a.position, c.position - a.position);
  const auto expected = a.normal + b.normal + c.normal;
  if (scene::dot(face, expected) < 0.0) std::swap(second, third);
  packet.mesh_triangles.push_back({first, second, third, 0U});
}

}  // namespace

operation::Result<RenderPacket> build_isosurface(
    const IsosurfaceRequest& request) {
  if (request.grid == nullptr) {
    return operation::Result<RenderPacket>::failure(
        invalid("isosurface requires a volume grid"));
  }
  if (!std::isfinite(request.style.level)) {
    return operation::Result<RenderPacket>::failure(
        invalid("isosurface level must be finite"));
  }
  if (!is_valid(request.style.color)) {
    return operation::Result<RenderPacket>::failure(
        invalid("isosurface color must contain finite values in [0, 1]"));
  }
  if (request.memory_budget_bytes == 0U) {
    return operation::Result<RenderPacket>::failure(
        invalid("isosurface memory budget must be positive"));
  }
  if (cancellation_requested(request)) {
    return operation::Result<RenderPacket>::failure(cancelled());
  }

  RenderPacket packet;
  packet.scene_node_id = request.scene_node_id;
  packet.provenance.emplace("algorithm", "marching-tetrahedra");
  packet.provenance.emplace("algorithm_version", "1");
  packet.provenance.emplace("scalar_order", "x-major-y-middle-z-fastest");
  packet.provenance.emplace("level", std::to_string(request.style.level));

  const auto& grid = *request.grid;
  const auto& shape = grid.shape();
  const auto [minimum, maximum] = grid.scalars().range();
  if (shape.x < 2U || shape.y < 2U || shape.z < 2U ||
      request.style.level < minimum || request.style.level > maximum) {
    return operation::Result<RenderPacket>::success(std::move(packet));
  }

  constexpr auto kMapNodeBudgetBytes = 128U;
  constexpr auto kVertexBudgetBytes =
      sizeof(MeshVertex) * 2U + sizeof(EdgeKey) + sizeof(std::uint32_t) +
      kMapNodeBudgetBytes;
  constexpr auto kTriangleBudgetBytes = sizeof(MeshTriangle) * 2U;
  if (grid.value_count() >
      request.memory_budget_bytes / sizeof(model::Vec3d)) {
    return operation::Result<RenderPacket>::failure(
        exhausted("isosurface gradients exceed the memory budget"));
  }
  std::size_t budget_used = grid.value_count() * sizeof(model::Vec3d);
  std::vector<model::Vec3d> gradients(grid.value_count());
  for (std::size_t x = 0; x < shape.x; ++x) {
    for (std::size_t y = 0; y < shape.y; ++y) {
      for (std::size_t z = 0; z < shape.z; ++z) {
        gradients[grid.linear_index(x, y, z)] =
            physical_gradient(logical_gradient(grid, x, y, z), grid.deltas());
      }
    }
  }

  std::map<EdgeKey, std::uint32_t> edge_vertices;
  bool budget_exhausted{};
  auto edge_vertex = [&](std::size_t first, std::size_t second) {
    const EdgeKey key = std::minmax(first, second);
    if (const auto found = edge_vertices.find(key); found != edge_vertices.end()) {
      return found->second;
    }
    const auto first_value = grid.scalars().value(first);
    const auto second_value = grid.scalars().value(second);
    const auto difference = second_value - first_value;
    const auto fraction = difference == 0.0
                              ? 0.5
                              : std::clamp((request.style.level - first_value) /
                                               difference,
                                           0.0, 1.0);
    const auto coordinate = [shape](std::size_t index) {
      const auto x = index / (shape.y * shape.z);
      const auto remainder = index % (shape.y * shape.z);
      return std::array<std::size_t, 3U>{x, remainder / shape.z,
                                         remainder % shape.z};
    };
    const auto a = coordinate(first);
    const auto b = coordinate(second);
    const auto first_position = grid.position(a[0], a[1], a[2]);
    const auto second_position = grid.position(b[0], b[1], b[2]);
    using scene::operator+;
    using scene::operator-;
    using scene::operator*;
    const auto position = first_position + (second_position - first_position) * fraction;
    const auto gradient = gradients[first] + (gradients[second] - gradients[first]) * fraction;
    const auto normal = scene::normalized(gradient * -1.0);
    if (packet.mesh_vertices.size() >=
            std::numeric_limits<std::uint32_t>::max() ||
        kVertexBudgetBytes > request.memory_budget_bytes - budget_used) {
      budget_exhausted = true;
      return std::uint32_t{};
    }
    budget_used += kVertexBudgetBytes;
    const auto index = static_cast<std::uint32_t>(packet.mesh_vertices.size());
    packet.mesh_vertices.push_back({position, normal, request.style.color});
    include(packet.bounds, position);
    edge_vertices.emplace(key, index);
    return index;
  };
  const auto add_triangle = [&](std::uint32_t first, std::uint32_t second,
                                std::uint32_t third) {
    if (budget_exhausted)
      return;
    if (kTriangleBudgetBytes > request.memory_budget_bytes - budget_used) {
      budget_exhausted = true;
      return;
    }
    budget_used += kTriangleBudgetBytes;
    orient_and_add(packet, first, second, third);
  };

  for (std::size_t x = 0; x + 1U < shape.x; ++x) {
    if (cancellation_requested(request)) {
      return operation::Result<RenderPacket>::failure(cancelled());
    }
    if (budget_exhausted) {
      return operation::Result<RenderPacket>::failure(
          exhausted("isosurface mesh exceeds the memory budget"));
    }
    if (request.context != nullptr && request.context->report_progress) {
      request.context->report_progress(
          {static_cast<double>(x) / static_cast<double>(shape.x - 1U),
           "isosurface"});
    }
    for (std::size_t y = 0; y + 1U < shape.y; ++y) {
      for (std::size_t z = 0; z + 1U < shape.z; ++z) {
        std::array<std::size_t, 8U> corners{};
        for (std::size_t corner = 0; corner < corners.size(); ++corner) {
          const auto& offset = kCornerOffsets[corner];
          corners[corner] = grid.linear_index(x + offset[0], y + offset[1],
                                               z + offset[2]);
        }
        for (const auto& tetrahedron : kTetrahedra) {
          std::array<std::size_t, 4U> inside{};
          std::array<std::size_t, 4U> outside{};
          std::size_t inside_count = 0U;
          std::size_t outside_count = 0U;
          for (const auto local : tetrahedron) {
            const auto point = corners[local];
            if (grid.scalars().value(point) >= request.style.level) {
              inside[inside_count++] = point;
            } else {
              outside[outside_count++] = point;
            }
          }
          if (inside_count == 0U || inside_count == 4U) continue;
          if (inside_count == 1U || inside_count == 3U) {
            const auto& lone = inside_count == 1U ? inside : outside;
            const auto& others = inside_count == 1U ? outside : inside;
            const auto first = edge_vertex(lone[0], others[0]);
            const auto second = edge_vertex(lone[0], others[1]);
            const auto third = edge_vertex(lone[0], others[2]);
            add_triangle(first, second, third);
            continue;
          }
          const auto ac = edge_vertex(inside[0], outside[0]);
          const auto ad = edge_vertex(inside[0], outside[1]);
          const auto bc = edge_vertex(inside[1], outside[0]);
          const auto bd = edge_vertex(inside[1], outside[1]);
          add_triangle(ac, ad, bd);
          add_triangle(ac, bd, bc);
        }
      }
    }
  }
  if (budget_exhausted) {
    return operation::Result<RenderPacket>::failure(
        exhausted("isosurface mesh exceeds the memory budget"));
  }
  if (request.context != nullptr && request.context->report_progress) {
    request.context->report_progress({1.0, "isosurface"});
  }
  return operation::Result<RenderPacket>::success(std::move(packet));
}

}  // namespace molshredder::render
