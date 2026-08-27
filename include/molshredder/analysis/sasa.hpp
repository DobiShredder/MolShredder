#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::analysis {

inline constexpr auto kSasaAlgorithmVersion =
    "molshredder-shrake-rupley-fibonacci-v1";

struct SasaAtomResult {
  model::AtomIndex atom;
  double expanded_radius_angstrom{};
  std::size_t accessible_sample_count{};
  double area_square_angstrom{};
};

struct SasaResult {
  std::vector<SasaAtomResult> atoms;
  double total_area_square_angstrom{};
  double probe_radius_angstrom{};
  std::size_t samples_per_atom{};
  std::size_t point_occluder_evaluations{};
  std::size_t ignored_missing_occluders{};
  double sampling_area_quantum_square_angstrom{};
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
};

struct SasaRequest {
  const model::CoordinateFrame *frame{};
  std::span<const double> vdw_radii_angstrom;
  std::span<const std::uint8_t> selected;
  double probe_radius_angstrom{1.4};
  std::size_t samples_per_atom{960U};
  std::size_t evaluation_budget{100'000'000U};
  operation::TaskContext *context{};
};

[[nodiscard]] operation::Result<SasaResult>
solvent_accessible_surface_area(const SasaRequest &request);

} // namespace molshredder::analysis
