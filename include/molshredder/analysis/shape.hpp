#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "molshredder/analysis/basic.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::analysis {

// Weighted second-moment shape of a selection. Weights are 1 for the centroid
// mode and per-atom masses for the center-of-mass mode.
struct ShapeRequest {
  const model::CoordinateFrame* frame{};
  std::span<const std::uint8_t> selected;
  CenterMode mode{CenterMode::centroid};
  std::span<const double> masses;
  std::string_view mass_unit;
  std::string_view mass_source;
  bool masses_estimated{};
  MissingAtomPolicy missing_atom_policy{MissingAtomPolicy::error};
};

struct ShapeResult {
  model::Vec3d center;
  // sqrt(sum(w * |r - center|^2) / sum(w)) in the coordinate unit.
  double radius_of_gyration{};
  // Eigenvalues of the inertia tensor about the center, smallest first, in
  // weight * coordinate_unit^2.
  std::array<double, 3U> principal_moments{};
  // Unit eigenvectors matching principal_moments. The basis is right-handed
  // (cross(axes[0], axes[1]) == axes[2]) and each axis has a positive
  // largest-magnitude component so the result is deterministic.
  std::array<model::Vec3d, 3U> principal_axes{};
  bool degenerate{};
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::size_t selected_atom_count{};
  std::size_t used_atom_count{};
  std::size_t skipped_missing_atom_count{};
  std::optional<double> total_mass;
  std::optional<std::string> mass_unit;
  std::string mass_source;
  bool masses_estimated{};
};

[[nodiscard]] operation::Result<ShapeResult>
calculate_shape(const ShapeRequest& request);

}  // namespace molshredder::analysis
