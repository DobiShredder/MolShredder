#pragma once

#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::analysis {

enum class SecondaryStructureState {
  coil,
  alpha_helix,
  helix_310,
  pi_helix,
  extended_strand,
  beta_bridge,
  turn,
};

[[nodiscard]] std::string_view to_string(SecondaryStructureState state) noexcept;
[[nodiscard]] char stride_code(SecondaryStructureState state) noexcept;

struct BackboneAtoms {
  model::ResidueIndex residue;
  std::optional<model::AtomIndex> n;
  std::optional<model::AtomIndex> ca;
  std::optional<model::AtomIndex> c;
  std::optional<model::AtomIndex> o;
  std::optional<model::AtomIndex> h;
};

struct BackboneGeometry {
  BackboneAtoms atoms;
  std::optional<double> phi_degrees;
  std::optional<double> psi_degrees;
  double alpha_propensity{};
  double beta_propensity{};
};

struct BackboneHydrogenBond {
  model::ResidueIndex donor;
  model::ResidueIndex acceptor;
  double energy_kcal_per_mol{};
  bool inferred_hydrogen{};
};

struct SecondaryStructureResidue {
  model::ResidueIndex residue;
  SecondaryStructureState state{SecondaryStructureState::coil};
  std::optional<double> phi_degrees;
  std::optional<double> psi_degrees;
  bool backbone_complete{};
};

struct SecondaryStructureParameters {
  double hydrogen_bond_energy_cutoff{-0.5};
  double minimum_helix_propensity{0.05};
  double minimum_beta_propensity{0.02};
};

struct SecondaryStructureResult {
  std::string_view method{"molshredder-stride-method-v0"};
  std::vector<SecondaryStructureResidue> residues;
  std::vector<BackboneHydrogenBond> hydrogen_bonds;
};

[[nodiscard]] operation::Result<std::vector<BackboneGeometry>>
calculate_backbone_geometry(const model::Topology& topology,
                            const model::CoordinateFrame& frame);

// Independent paper-method implementation. It does not contain or link the
// restricted STRIDE program source and is not an exact STRIDE parity claim.
[[nodiscard]] operation::Result<SecondaryStructureResult>
assign_secondary_structure(
    const model::Topology& topology, const model::CoordinateFrame& frame,
    const SecondaryStructureParameters& parameters = {});

[[nodiscard]] operation::Result<double> stride_method_hbond_energy_v0(
    // Coordinates are expressed in angstrom.
    model::Vec3d donor_n, model::Vec3d donor_h,
    model::Vec3d acceptor_o, model::Vec3d acceptor_c);

}  // namespace molshredder::analysis
