#include <array>
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
  const auto invalid_isotope = builder.add_atom(AtomRecord{
      "X", 6, residue.value(), "", 0, std::nullopt, std::uint16_t{0}});
  passed &= expect(!invalid_isotope.has_value(),
                   "present isotope mass number zero must fail");

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
                             BondOrder::single, BondQuery::none,
                             BondStereo::up,
                             ChemicalAnnotationOrigin::explicit_input})
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
  const auto malformed_query = builder.add_bond(
      Bond{atom_indices[2], atom_indices[3], BondOrder::query});
  passed &= expect(
      malformed_query.has_value() &&
          malformed_query->message.find("query") != std::string::npos,
      "query bond order without an exact query constraint must fail");
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
                         topology.bonds()[0].second.value == 1 &&
                         topology.bonds()[0].stereo == BondStereo::down &&
                         topology.bonds()[0].order_origin ==
                             ChemicalAnnotationOrigin::explicit_input,
                     "bond canonicalization must preserve directional stereo and origin");
    passed &= expect(
        topology.atom_ids() ==
                std::vector<AtomId>{{1U}, {2U}, {3U}, {4U}} &&
            topology.bond_ids() == std::vector<BondId>{{1U}} &&
            topology.atom_index(AtomId{4U}) == AtomIndex{3U} &&
            topology.atom_id(AtomIndex{1U}) == AtomId{2U} &&
            topology.bond_index(BondId{1U}) == 0U,
        "topology snapshots must expose non-zero 64-bit atom/bond identities independently of dense indices");
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

    auto mutation = TopologyBuilder::from(*built.value());
    const std::array retained{AtomId{4U}, AtomId{2U}, AtomId{1U}};
    const auto retained_error = mutation.retain_atoms(retained);
    const auto remapped = mutation.build();
    passed &= expect(!retained_error.has_value() && remapped.has_value(),
                     "stable-ID retain/reorder mutation must build");
    if (remapped.has_value()) {
      const auto mapping = remap_topology(*built.value(), *remapped.value());
      const auto *mass = remapped.value()->properties().find("mass");
      const auto *mass_values =
          mass == nullptr
              ? nullptr
              : std::get_if<std::vector<double>>(&mass->values);
      passed &= expect(
          remapped.value()->version() == 2U &&
              remapped.value()->atom_ids() ==
                  std::vector<AtomId>{{4U}, {2U}, {1U}} &&
              remapped.value()->bond_ids() == std::vector<BondId>{{1U}} &&
              remapped.value()->bonds()[0].first == AtomIndex{1U} &&
              remapped.value()->bonds()[0].second == AtomIndex{2U} &&
              remapped.value()->bonds()[0].stereo == BondStereo::up &&
              remapped.value()->bonds()[0].order_origin ==
                  ChemicalAnnotationOrigin::explicit_input &&
              remapped.value()->angles().empty() &&
              remapped.value()->dihedrals().empty() &&
              remapped.value()->impropers().empty() &&
              remapped.value()->cmap_terms().empty() &&
              mass_values != nullptr &&
              *mass_values == std::vector<double>{15.999, 12.011, 14.007} &&
              mapping.source_atoms ==
                  std::vector<std::optional<AtomIndex>>{
                      AtomIndex{2U}, AtomIndex{1U}, std::nullopt,
                      AtomIndex{0U}} &&
              mapping.source_bonds ==
                  std::vector<std::optional<std::size_t>>{0U} &&
              !remapped.value()->atom_index(AtomId{3U}).has_value(),
          "mutation must preserve surviving identity/property/bond state and expose exact deleted/reordered remap");

      const AtomReference surviving{{kTopologyReferenceSchemaVersion, 42U,
                                     built.value()->version()},
                                    AtomId{4U}};
      const AtomReference deleted{{kTopologyReferenceSchemaVersion, 42U,
                                   built.value()->version()},
                                  AtomId{3U}};
      const auto resolved =
          resolve_atom_reference(surviving, 42U, *remapped.value());
      passed &= expect(
          resolved.has_value() && resolved.value().index == AtomIndex{0U} &&
              resolved.value().remapped &&
              !resolve_atom_reference(deleted, 42U, *remapped.value())
                   .has_value() &&
              !resolve_atom_reference(surviving, 7U, *remapped.value())
                   .has_value() &&
              validate_topology_snapshot(surviving.snapshot, 42U,
                                         *remapped.value())
                  .has_value(),
          "persistent references must follow surviving IDs while deleted, cross-object and stale snapshots fail explicitly");
    }
    const auto duplicate_retain =
        mutation.retain_atoms(std::array{AtomId{1U}, AtomId{1U}});
    const auto unknown_retain = mutation.retain_atoms(std::array{AtomId{99U}});
    passed &= expect(duplicate_retain.has_value() && unknown_retain.has_value(),
                     "duplicate and stale stable IDs must fail without changing the builder snapshot");
  }


  TopologyBuilder insertion_builder;
  const auto insertion_residue = insertion_builder.add_residue(
      ResidueRecord{"UNK", 1, "", "A", ""});
  static_cast<void>(insertion_builder.add_atom(
      AtomRecord{"A", 6U, insertion_residue.value(), "", 0, std::nullopt}));
  const auto insertion_source = insertion_builder.build();
  auto insertion_next = TopologyBuilder::from(*insertion_source.value());
  static_cast<void>(insertion_next.add_atom(
      AtomRecord{"B", 7U, insertion_residue.value(), "", 0, std::nullopt}));
  const auto insertion_target = insertion_next.build();
  const auto insertion_remap =
      remap_topology(*insertion_source.value(), *insertion_target.value());
  passed &= expect(
      insertion_target.has_value() &&
          insertion_target.value()->atom_ids() ==
              std::vector<AtomId>{{1U}, {2U}} &&
          insertion_remap.target_atoms ==
              std::vector<std::optional<AtomIndex>>{AtomIndex{0U},
                                                     std::nullopt},
      "inserted atoms must receive new stable IDs and an explicit missing source ordinal");
  auto deletion_builder = TopologyBuilder::from(*insertion_target.value());
  static_cast<void>(deletion_builder.retain_atoms(std::array{AtomId{1U}}));
  const auto after_deletion = deletion_builder.build();
  auto reinsertion_builder = TopologyBuilder::from(*after_deletion.value());
  static_cast<void>(reinsertion_builder.add_atom(
      AtomRecord{"C", 8U, insertion_residue.value(), "", 0, std::nullopt}));
  const auto after_reinsertion = reinsertion_builder.build();
  passed &= expect(
      after_reinsertion.has_value() &&
          after_reinsertion.value()->atom_ids() ==
              std::vector<AtomId>{{1U}, {3U}},
      "deleted atom identities must never be reused by a later insertion");

  auto property_editor = TopologyBuilder::from(*insertion_target.value());
  auto edited_atom = insertion_target.value()->atoms().front();
  edited_atom.name = "CA";
  edited_atom.formal_charge = 1;
  edited_atom.formal_charge_present = true;
  edited_atom.chemical_origin = ChemicalAnnotationOrigin::user_override;
  auto edited_residue = insertion_target.value()->residues().front();
  edited_residue.name = "LIG";
  edited_residue.kind = ResidueKind::ligand;
  edited_residue.chemical_origin = ChemicalAnnotationOrigin::user_override;
  const auto atom_edit_error =
      property_editor.set_atom(AtomIndex{0U}, edited_atom);
  const auto residue_edit_error =
      property_editor.set_residue(ResidueIndex{0U}, edited_residue);
  const auto property_edited = property_editor.build();
  passed &= expect(
      !atom_edit_error.has_value() && !residue_edit_error.has_value() &&
          property_edited.has_value() &&
          property_edited.value()->atom_ids() ==
              insertion_target.value()->atom_ids() &&
          property_edited.value()->atoms().front().name == "CA" &&
          property_edited.value()->atoms().front().formal_charge == 1 &&
          property_edited.value()->residues().front().name == "LIG",
      "atom/residue property edits must preserve stable identity and validate chemistry before build");

  return passed ? 0 : 1;
}
