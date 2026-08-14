#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "molshredder/analysis/basic.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::analysis {

struct ContactPair {
  model::AtomIndex first;
  model::AtomIndex second;
  model::Vec3d displacement;
  double distance{};
};

struct ContactRequest {
  const model::CoordinateFrame& frame;
  std::span<const std::uint8_t> first;
  std::span<const std::uint8_t> second;
  std::span<const model::Bond> bonds;
  double cutoff{};
  DistanceBoundary boundary{DistanceBoundary::raw};
  bool same_selection{};
  bool exclude_bonded{true};
};

struct ContactResult {
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::vector<ContactPair> pairs;
};

struct HydrogenBond {
  model::AtomIndex donor;
  model::AtomIndex acceptor;
  model::AtomIndex hydrogen;
  double donor_acceptor_distance{};
  double angle_deviation_degrees{};
};

struct HydrogenBondRequest {
  const model::CoordinateFrame& frame;
  const model::Topology& topology;
  std::span<const std::uint8_t> donor_selection;
  std::span<const std::uint8_t> acceptor_selection;
  std::span<const std::uint8_t> donor_capable;
  std::span<const std::uint8_t> acceptor_capable;
  double distance_cutoff{};
  double maximum_angle_deviation_degrees{};
  DistanceBoundary boundary{DistanceBoundary::raw};
  bool same_selection{};
};

struct HydrogenBondResult {
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::vector<HydrogenBond> bonds;
};

struct HydrogenBondTyping {
  std::vector<std::uint8_t> donors;
  std::vector<std::uint8_t> acceptors;
  std::string donor_source;
  std::string acceptor_source;
  bool estimated{};
};

// Uses a Cartesian cell list for raw coordinates and a wrapped fractional
// cell list for arbitrary valid triclinic cells.
[[nodiscard]] operation::Result<ContactResult> find_contacts(
    const ContactRequest& request);

[[nodiscard]] operation::Result<HydrogenBondResult> find_hydrogen_bonds(
    const HydrogenBondRequest& request);

[[nodiscard]] operation::Result<HydrogenBondTyping>
resolve_hydrogen_bond_typing(const model::Topology& topology);

}  // namespace molshredder::analysis
