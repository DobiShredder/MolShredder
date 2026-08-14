#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/molecular_system.hpp"
#include "molshredder/model/topology.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  using namespace molshredder::model;

  bool passed = true;
  TopologyBuilder builder;
  const auto residue =
      builder.add_residue(ResidueRecord{"HOH", 1, "", "W", "SOLV"});
  passed &= expect(residue.has_value(), "residue must build");
  const auto oxygen = builder.add_atom(
      AtomRecord{"O", 8, residue.value(), "", 0, 1});
  const auto hydrogen1 = builder.add_atom(
      AtomRecord{"H1", 1, residue.value(), "", 0, 2});
  const auto hydrogen2 = builder.add_atom(
      AtomRecord{"H2", 1, residue.value(), "", 0, 3});
  passed &= expect(oxygen.has_value() && hydrogen1.has_value() &&
                       hydrogen2.has_value(),
                   "water atoms must build");
  passed &= expect(!builder.add_bond(
                        Bond{oxygen.value(), hydrogen1.value(), BondOrder::single})
                        .has_value() &&
                       !builder.add_bond(Bond{oxygen.value(), hydrogen2.value(),
                                              BondOrder::single})
                            .has_value(),
                   "water bonds must build");
  passed &= expect(!builder.add_property(
                        "mass", std::vector<double>{15.999, 1.008, 1.008})
                        .has_value(),
                   "water masses must build");
  const auto topology = builder.build();
  passed &= expect(topology.has_value(), "topology must build");

  const auto frame = CoordinateFrame::create(CoordinateBuffer{
      std::vector<Vec3d>{{0, 0, 0}, {0.9572, 0, 0},
                         {-0.2399872, 0.927297, 0}}});
  passed &= expect(frame.has_value(), "coordinate frame must build");
  const auto source = InMemoryCoordinateSource::create(3, {frame.value()});
  passed &= expect(source.has_value(), "coordinate source must build");

  const auto system = MolecularSystem::create(
      17, "water", topology.value(), source.value());
  passed &= expect(system.has_value(), "matching system must build");
  if (system.has_value()) {
    passed &= expect(system.value()->id() == 17 &&
                         system.value()->name() == "water" &&
                         system.value()->topology()->atom_count() == 3 &&
                         system.value()->coordinates()->frame_count() == 1,
                     "system must preserve stable identity and components");
  }

  const auto wrong_source = InMemoryCoordinateSource::create(2, {});
  passed &= expect(wrong_source.has_value(), "empty two-atom source may build");
  const auto mismatch = MolecularSystem::create(
      18, "bad", topology.value(), wrong_source.value());
  passed &= expect(!mismatch.has_value(),
                   "system must reject topology/source atom mismatch");
  const auto empty_name = MolecularSystem::create(
      19, "", topology.value(), source.value());
  passed &= expect(!empty_name.has_value(), "system name must not be empty");
  const auto null_topology = MolecularSystem::create(
      20, "null", {}, source.value());
  passed &= expect(!null_topology.has_value(), "null topology must fail");
  const auto null_source = MolecularSystem::create(
      21, "null", topology.value(), {});
  passed &= expect(!null_source.has_value(), "null coordinate source must fail");

  return passed ? 0 : 1;
}
