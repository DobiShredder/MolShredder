#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "molshredder/analysis/basic.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::analysis {

enum class WeightMode { uniform, mass };

struct RigidTransform {
  std::array<double, 9> rotation{1.0, 0.0, 0.0, 0.0, 1.0,
                                 0.0, 0.0, 0.0, 1.0};
  model::Vec3d translation;
  double input_scale{1.0};

  [[nodiscard]] model::Vec3d apply(model::Vec3d point) const noexcept;
};

[[nodiscard]] bool is_valid(const RigidTransform& transform) noexcept;

struct RmsdRequest {
  const model::CoordinateFrame* reference{};
  const model::CoordinateFrame* mobile{};
  std::span<const std::uint8_t> selected;
  std::span<const double> weights;
  MissingAtomPolicy missing_atom_policy{MissingAtomPolicy::error};
  RigidTransform transform;
};

struct RmsdResult {
  double rmsd{};
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::size_t selected_atom_count{};
  std::size_t paired_atom_count{};
  std::size_t skipped_missing_atom_count{};
  std::size_t effective_atom_count{};
  double weight_sum{};
};

struct FitRequest {
  const model::CoordinateFrame* reference{};
  const model::CoordinateFrame* mobile{};
  std::span<const std::uint8_t> selected;
  std::span<const double> weights;
  MissingAtomPolicy missing_atom_policy{MissingAtomPolicy::error};
};

struct FitResult {
  RigidTransform transform;
  RmsdResult before;
  RmsdResult after;
};

[[nodiscard]] operation::Result<RmsdResult> calculate_rmsd(
    const RmsdRequest& request);

[[nodiscard]] operation::Result<FitResult> fit_rigid(
    const FitRequest& request);

}  // namespace molshredder::analysis
