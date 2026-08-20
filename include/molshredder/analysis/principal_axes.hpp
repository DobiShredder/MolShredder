#pragma once

#include <array>
#include <cstddef>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::analysis {

// Numerically stable, merge-free online covariance accumulator. The six
// centered products are stored as xx, xy, xz, yy, yz, zz.
struct PrincipalMoments {
  std::size_t sample_count{};
  model::Vec3d centroid{};
  std::array<double, 6U> centered_products{};
};

struct PrincipalAxesResult {
  model::Vec3d centroid{};
  // Population covariance eigenvalues, greatest variance first.
  std::array<double, 3U> variances{};
  // Right, up and backward world-space axes. The basis is orthonormal and
  // right-handed: cross(axes[0], axes[1]) == axes[2].
  std::array<model::Vec3d, 3U> axes{};
  std::size_t sample_count{};
  bool primary_secondary_degenerate{};
  bool secondary_tertiary_degenerate{};
};

[[nodiscard]] bool accumulate(PrincipalMoments& moments,
                              model::Vec3d coordinate) noexcept;

[[nodiscard]] operation::Result<PrincipalAxesResult> calculate_principal_axes(
    const PrincipalMoments& moments,
    std::array<model::Vec3d, 3U> preferred_axes);

}  // namespace molshredder::analysis
