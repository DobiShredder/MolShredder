#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <string_view>

#include "molshredder/model/chemical_perception.hpp"

namespace {
bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}
}  // namespace

int main() {
  using namespace molshredder::model;
  bool passed = true;
  TopologyBuilder builder;
  const auto residue = builder.add_residue({"LIG", 1, "", "A", ""});
  const auto explicit_solvent = builder.add_residue(
      {"HOH", 2, "", "W", "", ResidueKind::solvent, PolymerType::none,
       ChemicalAnnotationOrigin::user_override});
  const auto first = builder.add_atom(
      {"C1", 6, residue.value(), "", 0, std::nullopt, std::nullopt,
       AtomStereoParity::unspecified, RadicalState::none, false,
       ChemicalAnnotationOrigin::explicit_input});
  const auto second = builder.add_atom({"C2", 6, residue.value(), "", 0});
  const auto oxygen = builder.add_atom({"O1", 8, residue.value(), "", 0});
  passed &= expect(!builder.add_bond(
                        {second.value(), oxygen.value(), BondOrder::double_bond,
                         BondQuery::none, BondStereo::none,
                         ChemicalAnnotationOrigin::user_override})
                        .has_value(),
                   "explicit/user bond setup failed");
  const auto topology = builder.build();
  const auto frame = CoordinateFrame::create(CoordinateBuffer{
      std::vector<Vec3d>{{0.0, 0.0, 0.0}, {1.50, 0.0, 0.0},
                         {2.72, 0.0, 0.0}}});
  const auto report = perceive_chemistry(*topology.value(), *frame.value());
  passed &= expect(report.has_value() && report.value().rule_version == 1U &&
                       report.value().proposed_bonds.size() == 1U &&
                       report.value().proposed_bonds[0].first == first.value() &&
                       report.value().proposed_bonds[0].second == second.value(),
                   "distance inference must propose only missing connectivity");
  passed &= expect(report.has_value() && report.value().residues.size() == 2U &&
                       report.value().residues[0].proposed &&
                       report.value().residues[0].kind == ResidueKind::ligand &&
                       !report.value().residues[1].proposed &&
                       report.value().residues[1].residue ==
                           explicit_solvent.value(),
                   "residue classification must propose unknown and preserve user input");
  const auto applied = report.has_value()
                           ? apply_chemical_perception(*topology.value(),
                                                       report.value())
                           : molshredder::operation::Result<
                                 std::shared_ptr<const Topology>>::failure(
                                 report.error());
  passed &= expect(applied.has_value() && applied.value()->bonds().size() == 2U &&
                       applied.value()->bonds()[0].order ==
                           BondOrder::double_bond &&
                       applied.value()->bonds()[0].order_origin ==
                           ChemicalAnnotationOrigin::user_override &&
                       applied.value()->bonds()[1].order_origin ==
                           ChemicalAnnotationOrigin::inferred &&
                       applied.value()->residues()[0].kind ==
                           ResidueKind::ligand &&
                       applied.value()->residues()[0].chemical_origin ==
                           ChemicalAnnotationOrigin::inferred &&
                       applied.value()->residues()[1].chemical_origin ==
                           ChemicalAnnotationOrigin::user_override,
                   "apply must preserve user annotation and add inferred bond");
  auto stale = report.value();
  ++stale.topology_version;
  passed &= expect(!apply_chemical_perception(*topology.value(), stale).has_value(),
                   "stale perception report must be rejected");
  const auto budget = perceive_chemistry(
      *topology.value(), *frame.value(), {true, 1.2, 0U});
  passed &= expect(!budget.has_value() &&
                       budget.error().code ==
                           molshredder::operation::ErrorCode::resource_exhausted,
                   "pair budget exhaustion must be explicit");

  TopologyBuilder aromatic_builder;
  const auto aromatic_residue =
      aromatic_builder.add_residue({"BEN", 1, "", "L", ""});
  std::vector<Vec3d> aromatic_positions;
  for (std::size_t index = 0; index < 6U; ++index) {
    const auto angle = std::numbers::pi * static_cast<double>(index) / 3.0;
    aromatic_positions.push_back({1.4 * std::cos(angle),
                                  1.4 * std::sin(angle), 0.0});
    static_cast<void>(aromatic_builder.add_atom(
        {"C" + std::to_string(index + 1U), 6, aromatic_residue.value(), "",
         0}));
  }
  const auto aromatic_topology = aromatic_builder.build();
  const auto aromatic_frame =
      CoordinateFrame::create(CoordinateBuffer{aromatic_positions});
  const auto aromatic_report = perceive_chemistry(
      *aromatic_topology.value(), *aromatic_frame.value());
  const auto aromatic_applied =
      aromatic_report.has_value()
          ? apply_chemical_perception(*aromatic_topology.value(),
                                      aromatic_report.value())
          : molshredder::operation::Result<
                std::shared_ptr<const Topology>>::failure(
                aromatic_report.error());
  passed &= expect(
      aromatic_report.has_value() &&
          aromatic_report.value().aromatic_ring_count == 1U &&
          aromatic_report.value().proposed_bonds.size() == 6U &&
          std::ranges::all_of(aromatic_report.value().proposed_bonds,
                              [](const auto &bond) {
                                return bond.order == BondOrder::aromatic;
                              }) &&
          aromatic_applied.has_value() &&
          std::ranges::all_of(aromatic_applied.value()->bonds(),
                              [](const auto &bond) {
                                return bond.order == BondOrder::aromatic &&
                                       bond.order_origin ==
                                           ChemicalAnnotationOrigin::inferred;
                              }),
      "planar 1.4-angstrom six-carbon ring must infer aromatic bonds");

  auto unknown_ring_builder =
      TopologyBuilder::from(*aromatic_topology.value());
  auto explicit_ring_builder =
      TopologyBuilder::from(*aromatic_topology.value());
  for (std::size_t index = 0; index < 6U; ++index) {
    const auto next = (index + 1U) % 6U;
    static_cast<void>(unknown_ring_builder.add_bond(
        {{index}, {next}, BondOrder::unknown}));
    static_cast<void>(explicit_ring_builder.add_bond(
        {{index}, {next}, BondOrder::single, BondQuery::none,
         BondStereo::none, ChemicalAnnotationOrigin::explicit_input}));
  }
  const auto unknown_ring = unknown_ring_builder.build();
  const auto explicit_ring = explicit_ring_builder.build();
  const auto unknown_report =
      perceive_chemistry(*unknown_ring.value(), *aromatic_frame.value());
  const auto explicit_report =
      perceive_chemistry(*explicit_ring.value(), *aromatic_frame.value());
  const auto unknown_applied =
      apply_chemical_perception(*unknown_ring.value(), unknown_report.value());
  passed &= expect(
      unknown_report.has_value() &&
          unknown_report.value().proposed_bonds.empty() &&
          unknown_report.value().proposed_bond_order_changes.size() == 6U &&
          unknown_applied.has_value() &&
          std::ranges::all_of(unknown_applied.value()->bonds(),
                              [](const auto &bond) {
                                return bond.order == BondOrder::aromatic &&
                                       bond.order_origin ==
                                           ChemicalAnnotationOrigin::inferred;
                              }) &&
          explicit_report.has_value() &&
          explicit_report.value().aromatic_ring_count == 0U &&
          explicit_report.value().proposed_bond_order_changes.empty(),
      "aromatic perception must update only unknown non-explicit order and preserve explicit single bonds");
  return passed ? 0 : 1;
}
