#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

#include "molshredder/model/volume.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/render/volume_isosurface.hpp"
#include "molshredder/scene/math.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

std::shared_ptr<const molshredder::model::VolumeGrid> make_grid() {
  using namespace molshredder;
  std::vector<double> values;
  for (std::size_t x = 0; x < 3U; ++x) {
    for (std::size_t y = 0; y < 3U; ++y) {
      for (std::size_t z = 0; z < 3U; ++z) {
        static_cast<void>(y);
        static_cast<void>(z);
        values.push_back(static_cast<double>(x));
      }
    }
  }
  return model::VolumeGrid::create(
             {3U, 3U, 3U}, {1.0, -2.0, 0.5},
             {{{2.0, 0.0, 0.0}, {0.5, 1.0, 0.0}, {0.0, 0.25, 1.0}}},
             model::VolumeScalarBuffer{std::move(values)})
      .value();
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  const auto grid = make_grid();
  render::IsosurfaceRequest request{grid.get(), 17U, {0.5, {}}, nullptr};
  request.style.color = {0.1F, 0.4F, 0.8F, 0.6F};
  const auto surface = render::build_isosurface(request);
  passed &= expect(surface.has_value() &&
                       !surface.value().mesh_vertices.empty() &&
                       !surface.value().mesh_triangles.empty(),
                   "plane volume must produce an isosurface mesh");
  if (surface.has_value()) {
    passed &= expect(surface.value().scene_node_id == 17U &&
                         surface.value().provenance.at("algorithm") ==
                             "marching-tetrahedra" &&
                         !surface.value().bounds.empty,
                     "packet must retain scene identity and provenance");
    const auto reciprocal_x = scene::cross(grid->deltas()[1], grid->deltas()[2]);
    const auto determinant =
        scene::dot(grid->deltas()[0], reciprocal_x);
    using scene::operator-;
    using scene::operator/;
    const auto logical_x_axis = reciprocal_x / determinant;
    const auto expected_normal = scene::normalized(logical_x_axis);
    for (const auto& vertex : surface.value().mesh_vertices) {
      passed &= expect(std::abs(scene::dot(vertex.position - grid->origin(),
                                           logical_x_axis) -
                                   0.5) <
                           1.0e-12,
                       "interpolated plane must use the physical grid basis");
      passed &= expect(std::abs(scene::length(vertex.normal) - 1.0) < 1.0e-12 &&
                           scene::dot(vertex.normal, expected_normal) < -0.999,
                       "normals must be unit physical gradients facing low values");
      passed &= expect(vertex.color == request.style.color,
                       "mesh vertices must retain the requested color");
    }
    for (const auto& triangle : surface.value().mesh_triangles) {
      passed &= expect(triangle.first < surface.value().mesh_vertices.size() &&
                           triangle.second < surface.value().mesh_vertices.size() &&
                           triangle.third < surface.value().mesh_vertices.size(),
                       "all triangle indices must reference emitted vertices");
    }
  }

  const auto repeated = render::build_isosurface(request);
  passed &= expect(repeated.has_value() && surface.has_value() &&
                       repeated.value().mesh_vertices.size() ==
                           surface.value().mesh_vertices.size() &&
                       repeated.value().mesh_triangles.size() ==
                           surface.value().mesh_triangles.size(),
                   "isosurface generation must be deterministic");

  request.style.level = 10.0;
  const auto empty = render::build_isosurface(request);
  passed &= expect(empty.has_value() && empty.value().mesh_vertices.empty() &&
                       empty.value().bounds.empty,
                   "out-of-range levels must produce a valid empty packet");

  request.style.level = 0.5;
  request.memory_budget_bytes = 1U;
  const auto budget = render::build_isosurface(request);
  passed &= expect(!budget.has_value() &&
                       budget.error().code ==
                           operation::ErrorCode::resource_exhausted,
                   "isosurface working memory budget must fail explicitly");
  request.memory_budget_bytes = std::numeric_limits<std::size_t>::max();

  request.style.level = std::numeric_limits<double>::quiet_NaN();
  passed &= expect(!render::build_isosurface(request).has_value(),
                   "non-finite levels must fail validation");
  request.style.level = 0.5;
  request.grid = nullptr;
  passed &= expect(!render::build_isosurface(request).has_value(),
                   "null grids must fail validation");

  operation::TaskContext context;
  context.cancellation.request_cancel();
  request.grid = grid.get();
  request.context = &context;
  const auto cancelled = render::build_isosurface(request);
  passed &= expect(!cancelled.has_value() &&
                       cancelled.error().code == operation::ErrorCode::cancelled,
                   "pre-cancelled work must return the cancelled error");
  return passed ? 0 : 1;
}
