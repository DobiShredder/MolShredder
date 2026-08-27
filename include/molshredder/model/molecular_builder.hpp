#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::model {

struct BuilderAtom {
  AtomRecord atom;
  Vec3d position;
};

struct BuilderBond {
  std::size_t first_atom{};
  std::size_t second_atom{};
  BondOrder order{BondOrder::single};
};

struct MoleculeBuildRequest {
  std::vector<ResidueRecord> residues;
  std::vector<BuilderAtom> atoms;
  std::vector<BuilderBond> bonds;
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::size_t memory_budget_bytes{256U * 1024U * 1024U};
  std::function<bool()> cancellation_requested;
};

struct MoleculeBuildResult {
  std::shared_ptr<const Topology> topology;
  std::shared_ptr<const CoordinateSource> coordinates;
  std::vector<AtomId> atom_ids;
  std::vector<BondId> bond_ids;
  std::size_t reserved_bytes{};
};

// Builds a one-state immutable topology/coordinate pair without publishing
// partial state. Atom and bond references are zero-based request ordinals;
// stable IDs are allocated by TopologyBuilder and returned explicitly.
[[nodiscard]] operation::Result<MoleculeBuildResult> build_molecule(
    const MoleculeBuildRequest &request);

}  // namespace molshredder::model
