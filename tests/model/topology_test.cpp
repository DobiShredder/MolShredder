#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/model/topology.hpp"
#include "molshredder/operation/error.hpp"

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
  using molshredder::operation::ErrorCode;

  bool passed = true;
  TopologyBuilder builder;
  const auto residue = builder.add_residue(
      ResidueRecord{"GLY", 42, "A", "X", "PROT"});
  passed &= expect(residue.has_value() && residue.value().value == 0,
                   "first residue must receive stable index zero");

  const auto invalid_residue = builder.add_residue(ResidueRecord{});
  passed &= expect(!invalid_residue.has_value() &&
                       invalid_residue.error().code ==
                           ErrorCode::invalid_argument,
                   "empty residue names must fail");

  const auto unknown_residue_atom = builder.add_atom(
      AtomRecord{"X", 0, ResidueIndex{9}, "", 0, std::nullopt});
  passed &= expect(!unknown_residue_atom.has_value(),
                   "atom must reference an existing residue");
  const auto invalid_element = builder.add_atom(
      AtomRecord{"X", 119, residue.value(), "", 0, std::nullopt});
  passed &= expect(!invalid_element.has_value(),
                   "atomic number above 118 must fail");

  const std::vector<AtomRecord> atoms{
      {"N", 7, residue.value(), "", 0, 101},
      {"CA", 6, residue.value(), "", 0, 102},
      {"C", 6, residue.value(), "", 0, 103},
      {"O", 8, residue.value(), "", 0, 104},
  };
  std::vector<AtomIndex> atom_indices;
  for (const auto& atom : atoms) {
    const auto index = builder.add_atom(atom);
    passed &= expect(index.has_value(), "valid atom must be added");
    if (index.has_value()) {
      atom_indices.push_back(index.value());
    }
  }
  passed &= expect(atom_indices.size() == 4 &&
                       atom_indices[3].value == 3,
                   "atom indices must be dense and stable");

  passed &= expect(!builder.add_bond(
                        Bond{atom_indices[1], atom_indices[0],
                             BondOrder::single})
                        .has_value(),
                   "valid reversed bond must canonicalize and add");
  passed &= expect(builder.add_bond(
                       Bond{atom_indices[0], atom_indices[1],
                            BondOrder::double_bond})
                       .has_value(),
                   "same atom pair must not have duplicate bonds");
  passed &= expect(builder.add_bond(
                       Bond{atom_indices[0], atom_indices[0], BondOrder::single})
                       .has_value(),
                   "self bond must fail");
  passed &= expect(builder.add_bond(
                       Bond{atom_indices[0], AtomIndex{99}, BondOrder::single})
                       .has_value(),
                   "bond with unknown atom must fail");
  passed &= expect(!builder.add_angle(
                        Angle{atom_indices[0], atom_indices[1], atom_indices[2]})
                        .has_value(),
                   "valid angle must add");
  passed &= expect(builder.add_angle(
                       Angle{atom_indices[2], atom_indices[1], atom_indices[0]})
                       .has_value(),
                   "reversed duplicate angle must fail");
  passed &= expect(builder.add_angle(
                       Angle{atom_indices[0], atom_indices[1], atom_indices[0]})
                       .has_value(),
                   "angle atoms must be distinct");
  passed &= expect(!builder.add_dihedral(
                        Dihedral{atom_indices[0], atom_indices[1],
                                 atom_indices[2], atom_indices[3]})
                        .has_value(),
                   "valid dihedral must add");
  passed &= expect(builder.add_dihedral(
                       Dihedral{atom_indices[3], atom_indices[2],
                                atom_indices[1], atom_indices[0]})
                       .has_value(),
                   "reversed duplicate dihedral must fail");
  passed &= expect(builder.add_dihedral(
                       Dihedral{atom_indices[0], atom_indices[1],
                                atom_indices[2], atom_indices[0]})
                       .has_value(),
                   "dihedral atoms must be distinct");
  passed &= expect(!builder.add_improper(
                        Improper{atom_indices[1], atom_indices[0],
                                 atom_indices[2], atom_indices[3]})
                        .has_value(),
                   "valid improper must add");
  passed &= expect(builder.add_improper(
                       Improper{atom_indices[1], atom_indices[0],
                                atom_indices[2], atom_indices[3]})
                       .has_value(),
                   "exact duplicate improper must fail");
  const CmapTerm cmap{{atom_indices[0], atom_indices[1], atom_indices[2],
                       atom_indices[3], atom_indices[1], atom_indices[2],
                       atom_indices[3], atom_indices[0]}};
  passed &= expect(!builder.add_cmap_term(cmap).has_value(),
                   "valid two-dihedral CMAP term must add");
  passed &= expect(builder.add_cmap_term(cmap).has_value(),
                   "exact duplicate CMAP term must fail");

  passed &= expect(!builder.add_property(
                        "selected", BooleanColumn{{1, 1, 0, 1}})
                        .has_value(),
                   "boolean property must add");
  passed &= expect(!builder.add_property(
                        "residue_id", std::vector<std::int64_t>{42, 42, 42, 42})
                        .has_value(),
                   "signed integer property must add");
  passed &= expect(!builder.add_property(
                        "source_id",
                        std::vector<std::uint64_t>{101, 102, 103, 104})
                        .has_value(),
                   "unsigned integer property must add");
  passed &= expect(!builder.add_property(
                        "occupancy", std::vector<float>{1, 1, 1, 1})
                        .has_value(),
                   "float property must add");
  passed &= expect(!builder.add_property(
                        "mass", std::vector<double>{14.007, 12.011, 12.011,
                                                     15.999},
                        PropertyMetadata{"dalton", "periodic-table", {}})
                        .has_value(),
                   "double property must add");
  passed &= expect(!builder.add_property(
                        "type", std::vector<std::string>{"N", "CT", "C", "O"})
                        .has_value(),
                   "text property must add");
  passed &= expect(builder.add_property("short", std::vector<double>{1.0})
                       .has_value(),
                   "property row count mismatch must fail");
  passed &= expect(builder.add_property("bad_bool", BooleanColumn{{0, 1, 2, 0}})
                       .has_value(),
                   "boolean property values must be 0 or 1");
  passed &= expect(builder.add_property(
                       "mass", std::vector<double>{1, 1, 1, 1})
                       .has_value(),
                   "duplicate property names must fail");
  builder.set_source_metadata("format", "synthetic");
  builder.set_source_metadata("unknown_field", "preserved");

  const auto built = builder.build();
  passed &= expect(built.has_value(), "valid topology must build");
  if (built.has_value()) {
    const auto& topology = *built.value();
    passed &= expect(topology.version() == 1 && topology.atom_count() == 4 &&
                         topology.residue_count() == 1,
                     "topology version and counts must be preserved");
    passed &= expect(topology.bonds().size() == 1 &&
                         topology.bonds()[0].first.value == 0 &&
                         topology.bonds()[0].second.value == 1,
                     "bond endpoints must use canonical stable indices");
    passed &= expect(topology.angles().size() == 1 &&
                         topology.dihedrals().size() == 1 &&
                         topology.impropers().size() == 1 &&
                         topology.cmap_terms().size() == 1,
                     "higher-order connectivity must be preserved");
    passed &= expect(topology.properties().row_count() == 4 &&
                         topology.properties().names() ==
                             std::vector<std::string>{
                                 "mass", "occupancy", "residue_id", "selected",
                                 "source_id", "type"},
                     "property table names must be deterministic");
    passed &= expect(topology.properties().kind("mass") ==
                         PropertyKind::float64 &&
                         topology.properties().find("missing") == nullptr,
                     "property lookup must preserve type and absence");
    passed &= expect(topology.properties().find("mass") != nullptr &&
                         topology.properties()
                                 .find("mass")
                                 ->metadata.unit == "dalton" &&
                         topology.properties()
                                 .find("mass")
                                 ->metadata.source == "periodic-table",
                     "property units and provenance must be preserved");
    passed &= expect(topology.source_metadata().at("unknown_field") ==
                         "preserved",
                     "unknown source metadata must be retained");
  }

  const auto extra_atom = builder.add_atom(
      AtomRecord{"H", 1, residue.value(), "", 0, std::nullopt});
  passed &= expect(extra_atom.has_value(), "builder remains independently mutable");
  const auto stale_columns = builder.build();
  passed &= expect(!stale_columns.has_value(),
                   "adding atoms after property columns must invalidate build");
  passed &= expect(built.has_value() && built.value()->atom_count() == 4,
                   "built topology must remain immutable after builder changes");

  if (built.has_value()) {
    auto next_builder = TopologyBuilder::from(*built.value());
    next_builder.set_source_metadata("revision", "second");
    const auto next = next_builder.build();
    passed &= expect(next.has_value() && next.value()->version() == 2 &&
                         next.value()->atom_count() == 4 &&
                         next.value()->properties().kind("mass") ==
                             PropertyKind::float64,
                     "builder-from-snapshot must advance version and preserve data");
    passed &= expect(built.value()->version() == 1 &&
                         !built.value()->source_metadata().contains("revision"),
                     "new topology version must not mutate its predecessor");
  }

  return passed ? 0 : 1;
}
