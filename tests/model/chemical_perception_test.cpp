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
  {
    // infer_connectivity: load-time distance bonds for files without polymer bonds.
    TopologyBuilder chain;
    const auto res = chain.add_residue({"GLY", 1, "", "A", ""});
    const auto a1 = chain.add_atom({"N", 7, res.value(), "", 0});
    const auto a2 = chain.add_atom({"CA", 6, res.value(), "", 0});
    const auto a3 = chain.add_atom({"C", 6, res.value(), "", 0});
    const auto far_atom = chain.add_atom({"O", 8, res.value(), "", 0});
    const auto alt_a = chain.add_atom({"CB", 6, res.value(), "A", 0});
    const auto alt_b = chain.add_atom({"CB", 6, res.value(), "B", 0});
    passed &= expect(!chain.add_bond({a1.value(), a2.value(), BondOrder::double_bond,
                                      BondQuery::none, BondStereo::none,
                                      ChemicalAnnotationOrigin::explicit_input})
                          .has_value(),
                     "explicit bond setup failed");
    const auto source = chain.build();
    const auto chain_frame = CoordinateFrame::create(CoordinateBuffer{std::vector<Vec3d>{
        {0.0, 0.0, 0.0}, {1.46, 0.0, 0.0}, {2.0, 1.4, 0.0}, {30.0, 0.0, 0.0},
        {40.0, 0.0, 0.0}, {40.5, 0.0, 0.0}}});
    const auto connected = infer_connectivity(source.value(), *chain_frame.value());
    const auto &bonds = connected.value()->bonds();
    const auto has_bond = [&](AtomIndex x, AtomIndex y) {
      return std::ranges::any_of(bonds, [&](const Bond &bond) {
        return (bond.first == x && bond.second == y) || (bond.first == y && bond.second == x);
      });
    };
    passed &= expect(connected.has_value() && bonds.size() == 2U &&
                         has_bond(a1.value(), a2.value()) && has_bond(a2.value(), a3.value()),
                     "inference must add CA-C and keep the explicit N-CA bond only");
    passed &= expect(bonds.size() == 2U && bonds[0].order == BondOrder::double_bond &&
                         bonds[0].order_origin == ChemicalAnnotationOrigin::explicit_input &&
                         bonds[1].order == BondOrder::single &&
                         bonds[1].order_origin == ChemicalAnnotationOrigin::inferred,
                     "inferred bonds are single and marked inferred; explicit bonds are untouched");
    passed &= expect(!has_bond(far_atom.value(), a3.value()) &&
                         !has_bond(alt_a.value(), alt_b.value()),
                     "distant atoms and different alternate locations must not be bonded");
    passed &= expect(connected.value()->version() == source.value()->version(),
                     "load-time inference keeps the topology version");
    const auto again = infer_connectivity(connected.value(), *chain_frame.value());
    passed &= expect(again.has_value() && again.value() == connected.value(),
                     "a second run adds nothing and returns the same topology");
    // Linear-time grid: 20000 atoms on a 3 A lattice has no neighbors within cutoff,
    // 20000 atoms 1.5 A apart form one chain of 19999 bonds.
    TopologyBuilder big;
    const auto big_res = big.add_residue({"LIG", 1, "", "A", ""});
    std::vector<Vec3d> line;
    for (int i = 0; i < 20000; ++i) {
      (void)big.add_atom({"C", 6, big_res.value(), "", 0});
      line.push_back({1.5 * i, 0.0, 0.0});
    }
    const auto big_topology = big.build();
    const auto big_frame = CoordinateFrame::create(CoordinateBuffer{line});
    const auto big_result = infer_connectivity(big_topology.value(), *big_frame.value());
    passed &= expect(big_result.has_value() && big_result.value()->bonds().size() == 19999U,
                     "grid inference must bond a 20000-atom chain");
  }
  return passed ? 0 : 1;
}
