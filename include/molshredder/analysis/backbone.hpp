#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::analysis {

// Backbone torsions of one residue. phi needs the preceding residue and psi the
// following one; both are absent at chain breaks and termini.
struct BackboneAngles {
  model::ResidueIndex residue;
  std::string chain;
  std::string residue_name;
  std::int64_t sequence_number{};
  std::string insertion_code;
  std::optional<double> phi_degrees;
  std::optional<double> psi_degrees;
};

struct BackboneRequest {
  const model::Topology* topology{};
  const model::CoordinateFrame* frame{};
  // Residues with at least one selected N, CA or C atom are reported; an empty
  // mask selects every residue.
  std::span<const std::uint8_t> selected;
  // Two residues are linked when C(i-1) to N(i) is shorter than this many
  // coordinate-unit lengths converted to angstrom (default 2.0 A, peptide bond
  // 1.33 A).
  double link_distance_angstrom{2.0};
};

// Residues that have N, CA and C atoms (first altloc wins) in residue order.
[[nodiscard]] operation::Result<std::vector<BackboneAngles>>
calculate_backbone_angles(const BackboneRequest& request);

}  // namespace molshredder::analysis
