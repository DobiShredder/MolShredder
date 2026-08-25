#include "molshredder/model/topology.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <tuple>
#include <type_traits>
#include <utility>

namespace molshredder::model {
namespace {

using operation::Error;
using operation::ErrorCode;

Error invalid(std::string message, std::string suggestion = {}) {
  return Error{ErrorCode::invalid_argument, std::move(message),
               std::move(suggestion)};
}

bool distinct(AtomIndex first, AtomIndex second) {
  return first != second;
}

bool distinct(AtomIndex first, AtomIndex second, AtomIndex third) {
  return first != second && first != third && second != third;
}

bool distinct(AtomIndex first, AtomIndex second, AtomIndex third,
              AtomIndex fourth) {
  return distinct(first, second, third) && first != fourth &&
         second != fourth && third != fourth;
}

bool valid_boolean_column(const BooleanColumn& column) {
  return std::all_of(column.values.begin(), column.values.end(),
                     [](std::uint8_t value) { return value <= 1U; });
}

}  // namespace

std::size_t column_size(const AtomPropertyColumn& column) {
  return std::visit(
      [](const auto& values) {
        using Values = std::decay_t<decltype(values)>;
        if constexpr (std::is_same_v<Values, BooleanColumn>) {
          return values.values.size();
        } else {
          return values.size();
        }
      },
      column);
}

PropertyKind property_kind(const AtomPropertyColumn& column) {
  return std::visit(
      [](const auto& values) {
        using Values = std::decay_t<decltype(values)>;
        if constexpr (std::is_same_v<Values, BooleanColumn>) {
          return PropertyKind::boolean;
        } else if constexpr (std::is_same_v<Values,
                                            std::vector<std::int64_t>>) {
          return PropertyKind::integer;
        } else if constexpr (std::is_same_v<Values,
                                            std::vector<std::uint64_t>>) {
          return PropertyKind::unsigned_integer;
        } else if constexpr (std::is_same_v<Values, std::vector<float>>) {
          return PropertyKind::float32;
        } else if constexpr (std::is_same_v<Values, std::vector<double>>) {
          return PropertyKind::float64;
        } else {
          return PropertyKind::text;
        }
      },
      column);
}

const AtomProperty* AtomPropertyTable::find(
    std::string_view name) const {
  const auto column = columns_.find(name);
  return column == columns_.end() ? nullptr : &column->second;
}

std::optional<PropertyKind> AtomPropertyTable::kind(
    std::string_view name) const {
  const auto* column = find(name);
  if (column == nullptr) {
    return std::nullopt;
  }
  return property_kind(column->values);
}

std::vector<std::string> AtomPropertyTable::names() const {
  std::vector<std::string> result;
  result.reserve(columns_.size());
  for (const auto& [name, column] : columns_) {
    static_cast<void>(column);
    result.push_back(name);
  }
  return result;
}

std::optional<AtomId> Topology::atom_id(AtomIndex index) const noexcept {
  return index.value < atom_ids_.size()
             ? std::optional<AtomId>{atom_ids_[index.value]}
             : std::nullopt;
}

std::optional<AtomIndex> Topology::atom_index(AtomId id) const noexcept {
  const auto found = std::find(atom_ids_.begin(), atom_ids_.end(), id);
  return found == atom_ids_.end()
             ? std::nullopt
             : std::optional<AtomIndex>{AtomIndex{static_cast<std::size_t>(
                   found - atom_ids_.begin())}};
}

std::optional<BondId> Topology::bond_id(std::size_t index) const noexcept {
  return index < bond_ids_.size() ? std::optional<BondId>{bond_ids_[index]}
                                  : std::nullopt;
}

std::optional<std::size_t> Topology::bond_index(BondId id) const noexcept {
  const auto found = std::find(bond_ids_.begin(), bond_ids_.end(), id);
  return found == bond_ids_.end()
             ? std::nullopt
             : std::optional<std::size_t>{static_cast<std::size_t>(
                   found - bond_ids_.begin())};
}

TopologyBuilder TopologyBuilder::from(const Topology& topology) {
  TopologyBuilder builder;
  builder.atoms_ = topology.atoms_;
  builder.atom_ids_ = topology.atom_ids_;
  builder.residues_ = topology.residues_;
  builder.bonds_ = topology.bonds_;
  builder.bond_ids_ = topology.bond_ids_;
  builder.angles_ = topology.angles_;
  builder.dihedrals_ = topology.dihedrals_;
  builder.impropers_ = topology.impropers_;
  builder.cmap_terms_ = topology.cmap_terms_;
  builder.properties_ = topology.properties_.columns_;
  builder.source_metadata_ = topology.source_metadata_;
  builder.base_version_ = topology.version_;
  builder.next_atom_id_ = topology.next_atom_id_;
  builder.next_bond_id_ = topology.next_bond_id_;
  return builder;
}

operation::Result<ResidueIndex> TopologyBuilder::add_residue(
    ResidueRecord residue) {
  if (residue.name.empty()) {
    return operation::Result<ResidueIndex>::failure(
        invalid("residue name must not be empty", "use UNK when unknown"));
  }
  const ResidueIndex index{residues_.size()};
  residues_.push_back(std::move(residue));
  return operation::Result<ResidueIndex>::success(index);
}

operation::Result<AtomIndex> TopologyBuilder::add_atom(AtomRecord atom) {
  if (atom.name.empty()) {
    return operation::Result<AtomIndex>::failure(
        invalid("atom name must not be empty"));
  }
  if (atom.atomic_number > 118U) {
    return operation::Result<AtomIndex>::failure(
        invalid("atomic number must be in the range 0-118",
                "use zero for an unknown element"));
  }
  if (atom.residue.value >= residues_.size()) {
    return operation::Result<AtomIndex>::failure(
        invalid("atom references an unknown residue",
                "add the residue before its atoms"));
  }
  if (next_atom_id_ == 0U ||
      next_atom_id_ == std::numeric_limits<std::uint64_t>::max()) {
    return operation::Result<AtomIndex>::failure(
        invalid("atom identity space is exhausted"));
  }
  const AtomIndex index{atoms_.size()};
  atoms_.push_back(std::move(atom));
  atom_ids_.push_back(AtomId{next_atom_id_++});
  return operation::Result<AtomIndex>::success(index);
}

std::optional<operation::Error>
TopologyBuilder::retain_atoms(std::span<const AtomId> ordered_atom_ids) {
  if (std::any_of(properties_.begin(), properties_.end(),
                  [this](const auto &entry) {
                    return column_size(entry.second.values) != atoms_.size();
                  }))
    return invalid("cannot remap atoms while property row counts are stale");
  std::vector<std::size_t> source_indices;
  source_indices.reserve(ordered_atom_ids.size());
  std::set<AtomId> unique;
  for (const auto id : ordered_atom_ids) {
    if (id.value == 0U || !unique.insert(id).second)
      return invalid("retained atom IDs must be non-zero and unique");
    const auto found = std::find(atom_ids_.begin(), atom_ids_.end(), id);
    if (found == atom_ids_.end())
      return invalid("retained atom ID does not exist: " +
                     std::to_string(id.value));
    source_indices.push_back(
        static_cast<std::size_t>(found - atom_ids_.begin()));
  }
  std::vector<std::optional<AtomIndex>> old_to_new(atoms_.size());
  for (std::size_t target = 0; target < source_indices.size(); ++target)
    old_to_new[source_indices[target]] = AtomIndex{target};

  std::vector<AtomRecord> next_atoms;
  std::vector<AtomId> next_atom_ids;
  next_atoms.reserve(source_indices.size());
  next_atom_ids.reserve(source_indices.size());
  for (const auto source : source_indices) {
    next_atoms.push_back(atoms_[source]);
    next_atom_ids.push_back(atom_ids_[source]);
  }
  auto next_properties = properties_;
  for (auto &[name, property] : next_properties) {
    static_cast<void>(name);
    property.values = std::visit(
        [&source_indices](const auto &values) -> AtomPropertyColumn {
          using Values = std::decay_t<decltype(values)>;
          if constexpr (std::is_same_v<Values, BooleanColumn>) {
            BooleanColumn result;
            result.values.reserve(source_indices.size());
            for (const auto source : source_indices)
              result.values.push_back(values.values[source]);
            return result;
          } else {
            Values result;
            result.reserve(source_indices.size());
            for (const auto source : source_indices)
              result.push_back(values[source]);
            return result;
          }
        },
        property.values);
  }

  std::vector<Bond> next_bonds;
  std::vector<BondId> next_bond_ids;
  for (std::size_t index = 0; index < bonds_.size(); ++index) {
    const auto &bond = bonds_[index];
    if (!old_to_new[bond.first.value].has_value() ||
        !old_to_new[bond.second.value].has_value())
      continue;
    auto first = *old_to_new[bond.first.value];
    auto second = *old_to_new[bond.second.value];
    if (second < first)
      std::swap(first, second);
    next_bonds.push_back(Bond{first, second, bond.order});
    next_bond_ids.push_back(bond_ids_[index]);
  }
  const auto remap_atom = [&old_to_new](AtomIndex atom) {
    return old_to_new[atom.value];
  };
  std::vector<Angle> next_angles;
  for (const auto &angle : angles_) {
    const auto first = remap_atom(angle.first);
    const auto center = remap_atom(angle.center);
    const auto third = remap_atom(angle.third);
    if (first.has_value() && center.has_value() && third.has_value())
      next_angles.push_back(Angle{*first, *center, *third});
  }
  std::vector<Dihedral> next_dihedrals;
  for (const auto &term : dihedrals_) {
    const auto first = remap_atom(term.first);
    const auto second = remap_atom(term.second);
    const auto third = remap_atom(term.third);
    const auto fourth = remap_atom(term.fourth);
    if (first && second && third && fourth)
      next_dihedrals.push_back(Dihedral{*first, *second, *third, *fourth});
  }
  std::vector<Improper> next_impropers;
  for (const auto &term : impropers_) {
    const auto center = remap_atom(term.center);
    const auto first = remap_atom(term.first);
    const auto second = remap_atom(term.second);
    const auto third = remap_atom(term.third);
    if (center && first && second && third)
      next_impropers.push_back(Improper{*center, *first, *second, *third});
  }
  std::vector<CmapTerm> next_cmap_terms;
  for (const auto &term : cmap_terms_) {
    CmapTerm remapped;
    bool retained = true;
    for (std::size_t index = 0; index < term.atoms.size(); ++index) {
      const auto atom = remap_atom(term.atoms[index]);
      if (!atom.has_value()) {
        retained = false;
        break;
      }
      remapped.atoms[index] = *atom;
    }
    if (retained)
      next_cmap_terms.push_back(remapped);
  }

  atoms_ = std::move(next_atoms);
  atom_ids_ = std::move(next_atom_ids);
  bonds_ = std::move(next_bonds);
  bond_ids_ = std::move(next_bond_ids);
  angles_ = std::move(next_angles);
  dihedrals_ = std::move(next_dihedrals);
  impropers_ = std::move(next_impropers);
  cmap_terms_ = std::move(next_cmap_terms);
  properties_ = std::move(next_properties);
  return std::nullopt;
}

bool TopologyBuilder::has_atom(AtomIndex atom) const noexcept {
  return atom.value < atoms_.size();
}

std::optional<operation::Error> TopologyBuilder::add_bond(Bond bond) {
  if (!has_atom(bond.first) || !has_atom(bond.second)) {
    return invalid("bond references an unknown atom");
  }
  if (!distinct(bond.first, bond.second)) {
    return invalid("bond endpoints must be different atoms");
  }
  if (bond.second < bond.first) {
    std::swap(bond.first, bond.second);
  }
  const auto duplicate = std::find_if(
      bonds_.begin(), bonds_.end(), [&bond](const Bond& candidate) {
        return candidate.first == bond.first && candidate.second == bond.second;
      });
  if (duplicate != bonds_.end()) {
    return invalid("duplicate bond endpoints",
                   "store one bond order per atom pair");
  }
  if (next_bond_id_ == 0U ||
      next_bond_id_ == std::numeric_limits<std::uint64_t>::max())
    return invalid("bond identity space is exhausted");
  bonds_.push_back(bond);
  bond_ids_.push_back(BondId{next_bond_id_++});
  return std::nullopt;
}

std::optional<operation::Error> TopologyBuilder::add_angle(Angle angle) {
  if (!has_atom(angle.first) || !has_atom(angle.center) ||
      !has_atom(angle.third)) {
    return invalid("angle references an unknown atom");
  }
  if (!distinct(angle.first, angle.center, angle.third)) {
    return invalid("angle atoms must be distinct");
  }
  if (angle.third < angle.first) {
    std::swap(angle.first, angle.third);
  }
  const auto duplicate = std::find_if(
      angles_.begin(), angles_.end(), [&angle](const Angle& candidate) {
        return candidate.first == angle.first &&
               candidate.center == angle.center &&
               candidate.third == angle.third;
      });
  if (duplicate != angles_.end()) {
    return invalid("duplicate angle atoms");
  }
  angles_.push_back(angle);
  return std::nullopt;
}

std::optional<operation::Error> TopologyBuilder::add_dihedral(
    Dihedral dihedral, bool allow_duplicate_term) {
  if (!has_atom(dihedral.first) || !has_atom(dihedral.second) ||
      !has_atom(dihedral.third) || !has_atom(dihedral.fourth)) {
    return invalid("dihedral references an unknown atom");
  }
  if (!distinct(dihedral.first, dihedral.second, dihedral.third,
                dihedral.fourth)) {
    return invalid("dihedral atoms must be distinct");
  }
  const auto forward = std::tuple{dihedral.first, dihedral.second,
                                  dihedral.third, dihedral.fourth};
  const auto reverse = std::tuple{dihedral.fourth, dihedral.third,
                                  dihedral.second, dihedral.first};
  if (reverse < forward) {
    std::swap(dihedral.first, dihedral.fourth);
    std::swap(dihedral.second, dihedral.third);
  }
  const auto duplicate = std::find_if(
      dihedrals_.begin(), dihedrals_.end(),
      [&dihedral](const Dihedral& candidate) {
        return candidate.first == dihedral.first &&
               candidate.second == dihedral.second &&
               candidate.third == dihedral.third &&
               candidate.fourth == dihedral.fourth;
      });
  if (!allow_duplicate_term && duplicate != dihedrals_.end()) {
    return invalid("duplicate dihedral atoms");
  }
  dihedrals_.push_back(dihedral);
  return std::nullopt;
}

std::optional<operation::Error> TopologyBuilder::add_improper(
    Improper improper, bool allow_duplicate_term) {
  if (!has_atom(improper.center) || !has_atom(improper.first) ||
      !has_atom(improper.second) || !has_atom(improper.third)) {
    return invalid("improper references an unknown atom");
  }
  if (!distinct(improper.center, improper.first, improper.second,
                improper.third)) {
    return invalid("improper atoms must be distinct");
  }
  const auto duplicate = std::find_if(
      impropers_.begin(), impropers_.end(),
      [&improper](const Improper& candidate) {
        return candidate.center == improper.center &&
               candidate.first == improper.first &&
               candidate.second == improper.second &&
               candidate.third == improper.third;
      });
  if (!allow_duplicate_term && duplicate != impropers_.end()) {
    return invalid("duplicate improper atoms");
  }
  impropers_.push_back(improper);
  return std::nullopt;
}

std::optional<operation::Error> TopologyBuilder::add_cmap_term(
    CmapTerm term, bool allow_duplicate_term) {
  if (std::any_of(term.atoms.begin(), term.atoms.end(),
                  [this](AtomIndex atom) { return !has_atom(atom); })) {
    return invalid("CMAP term references an unknown atom");
  }
  if (!distinct(term.atoms[0], term.atoms[1], term.atoms[2], term.atoms[3]) ||
      !distinct(term.atoms[4], term.atoms[5], term.atoms[6], term.atoms[7])) {
    return invalid("each CMAP dihedral must contain four distinct atoms");
  }
  const auto duplicate =
      std::find_if(cmap_terms_.begin(), cmap_terms_.end(),
                   [&term](const CmapTerm& candidate) {
                     return candidate.atoms == term.atoms;
                   });
  if (!allow_duplicate_term && duplicate != cmap_terms_.end()) {
    return invalid("duplicate CMAP term atoms");
  }
  cmap_terms_.push_back(std::move(term));
  return std::nullopt;
}

std::optional<operation::Error> TopologyBuilder::add_property(
    std::string name, AtomPropertyColumn column, PropertyMetadata metadata) {
  if (name.empty()) {
    return invalid("atom property name must not be empty");
  }
  if (properties_.contains(name)) {
    return invalid("duplicate atom property: " + name,
                   "use a unique property name");
  }
  if (column_size(column) != atoms_.size()) {
    return invalid("atom property row count does not match atom count: " + name,
                   "provide exactly one property value per atom");
  }
  if (const auto* boolean = std::get_if<BooleanColumn>(&column);
      boolean != nullptr && !valid_boolean_column(*boolean)) {
    return invalid("boolean atom property contains a value other than 0 or 1: " +
                   name);
  }
  properties_.emplace(
      std::move(name),
      AtomProperty{std::move(column), std::move(metadata)});
  return std::nullopt;
}

void TopologyBuilder::set_source_metadata(std::string name,
                                          std::string value) {
  source_metadata_.insert_or_assign(std::move(name), std::move(value));
}

operation::Result<std::shared_ptr<const Topology>> TopologyBuilder::build()
    const {
  if (base_version_ == std::numeric_limits<std::uint64_t>::max()) {
    return operation::Result<std::shared_ptr<const Topology>>::failure(
        invalid("topology version overflow"));
  }
  for (const auto& [name, property] : properties_) {
    if (column_size(property.values) != atoms_.size()) {
      return operation::Result<std::shared_ptr<const Topology>>::failure(
          invalid("atom property row count changed after registration: " + name,
                  "add all atoms before adding property columns"));
    }
  }
  if (atom_ids_.size() != atoms_.size() || bond_ids_.size() != bonds_.size())
    return operation::Result<std::shared_ptr<const Topology>>::failure(
        invalid("topology stable identity table is inconsistent"));

  auto topology = std::make_shared<Topology>();
  topology->version_ = base_version_ + 1U;
  topology->atoms_ = atoms_;
  topology->atom_ids_ = atom_ids_;
  topology->residues_ = residues_;
  topology->bonds_ = bonds_;
  topology->bond_ids_ = bond_ids_;
  topology->angles_ = angles_;
  topology->dihedrals_ = dihedrals_;
  topology->impropers_ = impropers_;
  topology->cmap_terms_ = cmap_terms_;
  topology->properties_.row_count_ = atoms_.size();
  topology->properties_.columns_ = properties_;
  topology->source_metadata_ = source_metadata_;
  topology->next_atom_id_ = next_atom_id_;
  topology->next_bond_id_ = next_bond_id_;
  return operation::Result<std::shared_ptr<const Topology>>::success(
      std::move(topology));
}

TopologyRemap remap_topology(const Topology& source, const Topology& target) {
  TopologyRemap result;
  result.source_version = source.version();
  result.target_version = target.version();
  result.source_atoms.reserve(source.atom_count());
  for (const auto id : source.atom_ids())
    result.source_atoms.push_back(target.atom_index(id));
  result.source_bonds.reserve(source.bonds().size());
  for (const auto id : source.bond_ids())
    result.source_bonds.push_back(target.bond_index(id));
  result.target_atoms.reserve(target.atom_count());
  for (const auto id : target.atom_ids())
    result.target_atoms.push_back(source.atom_index(id));
  result.target_bonds.reserve(target.bonds().size());
  for (const auto id : target.bond_ids())
    result.target_bonds.push_back(source.bond_index(id));
  return result;
}

std::optional<operation::Error> validate_topology_snapshot(
    const TopologySnapshotReference& reference, std::uint64_t object_id,
    const Topology& topology) {
  if (reference.schema_version != kTopologyReferenceSchemaVersion)
    return invalid("unsupported topology reference schema version");
  if (reference.object_id == 0U || reference.object_id != object_id)
    return invalid("topology reference belongs to a different object");
  if (reference.topology_version != topology.version())
    return invalid("topology snapshot reference is stale",
                   "refresh the snapshot before applying the operation");
  return std::nullopt;
}

operation::Result<ResolvedAtomReference> resolve_atom_reference(
    const AtomReference& reference, std::uint64_t object_id,
    const Topology& topology) {
  if (reference.snapshot.schema_version != kTopologyReferenceSchemaVersion ||
      reference.snapshot.object_id == 0U ||
      reference.snapshot.object_id != object_id ||
      reference.snapshot.topology_version == 0U ||
      reference.snapshot.topology_version > topology.version()) {
    return operation::Result<ResolvedAtomReference>::failure(
        invalid("atom reference has incompatible object, schema, or revision"));
  }
  const auto index = topology.atom_index(reference.atom_id);
  if (!index.has_value()) {
    return operation::Result<ResolvedAtomReference>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "atom identity was deleted from the topology", {},
                         {{"atom_id", std::to_string(reference.atom_id.value)}}});
  }
  return operation::Result<ResolvedAtomReference>::success(
      {*index, topology.version(),
       reference.snapshot.topology_version != topology.version()});
}

operation::Result<ResolvedBondReference> resolve_bond_reference(
    const BondReference& reference, std::uint64_t object_id,
    const Topology& topology) {
  if (reference.snapshot.schema_version != kTopologyReferenceSchemaVersion ||
      reference.snapshot.object_id == 0U ||
      reference.snapshot.object_id != object_id ||
      reference.snapshot.topology_version == 0U ||
      reference.snapshot.topology_version > topology.version()) {
    return operation::Result<ResolvedBondReference>::failure(
        invalid("bond reference has incompatible object, schema, or revision"));
  }
  const auto index = topology.bond_index(reference.bond_id);
  if (!index.has_value()) {
    return operation::Result<ResolvedBondReference>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "bond identity was deleted from the topology", {},
                         {{"bond_id", std::to_string(reference.bond_id.value)}}});
  }
  return operation::Result<ResolvedBondReference>::success(
      {*index, topology.version(),
       reference.snapshot.topology_version != topology.version()});
}

}  // namespace molshredder::model
