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

// Distance-based connectivity for structure files that do not list the bonds
// of polymer residues (PDB, mmCIF, GRO, XYZ, ...), as PyMOL and VMD do on load.
// Uses the same radius rule as perceive_chemistry but a cell grid, so the cost
// is linear in the atom count. Atoms with different non-empty alternate
// locations are never bonded; existing bonds are kept. Returns `source`
// unchanged when no bond is added; added bonds are marked `inferred`. The
// result keeps the source's topology version (load-time use only).
[[nodiscard]] operation::Result<std::shared_ptr<const Topology>>
infer_connectivity(std::shared_ptr<const Topology> source,
                   const CoordinateFrame &frame,
                   double covalent_radius_scale = 1.20);

// Coarse class of every residue, used to choose what a freshly loaded object
// shows. Residue kinds a reader or the user stated explicitly win; otherwise
// the residue name decides, and a polymer needs two same-family residues in a
// chain (a lone amino acid is a small molecule).
enum class ResidueClass : std::uint8_t { protein, nucleic, organic, ion, solvent };

[[nodiscard]] std::vector<ResidueClass> classify_residues(const Topology &topology);

}  // namespace molshredder::model
