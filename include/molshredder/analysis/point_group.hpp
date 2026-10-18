#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/result.hpp"

// Molecular point-group detection (VMD `measure symmetry` equivalent, SEQ-321; definition: Q-039).
namespace molshredder::analysis {

struct PointGroupRequest {
  std::span<const model::Vec3d> positions;  // angstrom
  std::span<const int> labels;              // atoms with different labels are never exchanged (atomic numbers)
  std::span<const double> masses;           // optional; centre of mass is the origin of the symmetry elements
  double tolerance{0.1};                    // angstrom: maximum distance between an atom and its image
  std::size_t max_atoms{300};
};

struct PointGroupResult {
  // Schoenflies symbol in ASCII: C1, Cs, Ci, Cn, Cnv, Cnh, Dn, Dnh, Dnd, S2n, T, Td, Th, O, Oh, I, Ih, Cinfv, Dinfh, Kh.
  std::string point_group;
  bool linear{};
  bool inversion{};
  // Number of distinct rotation axes by their highest order n >= 2 (only axes through the centre of mass).
  std::map<int, std::size_t> rotation_axes;
  std::size_t mirror_planes{};
  // Improper rotation axes S_n (n >= 3) by order.
  std::map<int, std::size_t> improper_axes;
  model::Vec3d center;
  std::optional<model::Vec3d> principal_axis;
  double tolerance{};
};

[[nodiscard]] operation::Result<PointGroupResult> detect_point_group(const PointGroupRequest &request);

} // namespace molshredder::analysis
