#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::model {

inline constexpr const char *kChemicalPerceptionRuleSet =
    "molshredder-conservative-chemistry";
inline constexpr unsigned int kChemicalPerceptionRuleVersion = 1U;

struct ChemicalPerceptionOptions {
  bool infer_connectivity{true};
  double covalent_radius_scale{1.20};
  std::size_t pair_budget{2'000'000U};
};

struct PerceivedBond {
  AtomIndex first;
  AtomIndex second;
  BondOrder order{BondOrder::single};
  double distance_angstrom{};
};

struct PerceivedBondOrderChange {
  std::size_t bond_index{};
  BondOrder source_order{BondOrder::unknown};
  BondOrder proposed_order{BondOrder::aromatic};
};

struct AtomChemicalAssessment {
  AtomIndex atom;
  double valence{};
  std::size_t implicit_hydrogen_count{};
  bool in_ring{};
  bool valence_supported{};
};

struct ResidueChemicalAssessment {
  ResidueIndex residue;
  ResidueKind kind{ResidueKind::unknown};
  PolymerType polymer_type{PolymerType::none};
  bool proposed{};
};

struct ChemicalPerceptionReport {
  std::string rule_set{kChemicalPerceptionRuleSet};
  unsigned int rule_version{kChemicalPerceptionRuleVersion};
  std::uint64_t topology_version{};
  std::size_t evaluated_pair_count{};
  std::size_t ring_bond_count{};
  std::size_t aromatic_ring_count{};
  std::vector<PerceivedBond> proposed_bonds;
  std::vector<PerceivedBondOrderChange> proposed_bond_order_changes;
  std::vector<AtomChemicalAssessment> atoms;
  std::vector<ResidueChemicalAssessment> residues;
  std::vector<std::string> assumptions;
  std::vector<std::string> warnings;
};

[[nodiscard]] operation::Result<ChemicalPerceptionReport> perceive_chemistry(
    const Topology &topology, const CoordinateFrame &frame,
    ChemicalPerceptionOptions options = {},
    operation::TaskContext *context = nullptr);

// Applies only report-proposed inferred bonds. Explicit input and user
// annotations in the source snapshot are never rewritten.
[[nodiscard]] operation::Result<std::shared_ptr<const Topology>>
apply_chemical_perception(const Topology &source,
                          const ChemicalPerceptionReport &report);

}  // namespace molshredder::model
