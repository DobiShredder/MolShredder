#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/result.hpp"

// Pure computations behind the VMD `measure dipole|surface|cluster` equivalents (SEQ-321; definitions: Q-039).
namespace molshredder::analysis {

enum class DipoleReference { geometric, mass, origin };

struct DipoleResult {
  model::Vec3d moment;      // e * angstrom
  double magnitude{};       // e * angstrom
  model::Vec3d reference;   // origin used (angstrom)
  double total_charge{};    // elementary charges
};

inline constexpr double kDebyePerElementaryAngstrom = 4.80320471;

// mu = sum q_i (r_i - r0). Masses are only read for DipoleReference::mass.
[[nodiscard]] operation::Result<DipoleResult> dipole_moment(
    std::span<const model::Vec3d> positions, std::span<const double> charges,
    std::span<const double> masses, DipoleReference reference);

struct SurfaceAtomsRequest {
  double grid_spacing{1.0};  // angstrom
  double radius{1.4};        // angstrom: full grid points lie within this distance of an atom
  double depth{0.0};         // angstrom: atoms within radius + depth + one grid cell of an exterior point are surface atoms
  std::size_t max_grid_points{40'000'000U};
};

// Zero-based indices (into `positions`) of the surface atoms, ascending.
[[nodiscard]] operation::Result<std::vector<std::size_t>> surface_atoms(
    std::span<const model::Vec3d> positions, const SurfaceAtomsRequest &request);

struct QtCluster {
  std::size_t seed{};
  std::vector<std::size_t> members;  // ascending, includes the seed
  double diameter{};
};

// Quality-threshold clustering (Heyer et al. 1999). Clusters are returned largest first (ties: lowest seed).
[[nodiscard]] std::vector<QtCluster> quality_threshold_clusters(
    std::size_t count, const std::function<double(std::size_t, std::size_t)> &distance, double cutoff);

struct PbcImage {
  std::size_t source{};            // index into the input positions
  std::array<int, 3> shift{};      // lattice translation (a, b, c) that produced the image
  model::Vec3d position;           // image position (angstrom)
};

// Image atoms (lattice translations other than zero) that lie within `cutoff` of the cell: the cell, centred at
// `center`, expanded by `cutoff` along each pair of face normals. `cell` is in the same unit as the positions.
[[nodiscard]] operation::Result<std::vector<PbcImage>> pbc_neighbors(
    std::span<const model::Vec3d> positions, const model::UnitCell &cell, const model::Vec3d &center, double cutoff,
    std::size_t max_images = 5'000'000U);

// Row-major 4x4 affine matrix x' = center + diag(|a|,|b|,|c|) H^-1 (x - center) that maps the (possibly triclinic) cell
// onto an orthonormal cell with the same edge lengths and the same centre.
[[nodiscard]] operation::Result<std::array<double, 16>> pbc_to_orthonormal(const model::UnitCell &cell, const model::Vec3d &center);

} // namespace molshredder::analysis
