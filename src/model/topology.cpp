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

TopologyBuilder TopologyBuilder::from(const Topology& topology) {
  TopologyBuilder builder;
  builder.atoms_ = topology.atoms_;
  builder.residues_ = topology.residues_;
  builder.bonds_ = topology.bonds_;
  builder.angles_ = topology.angles_;
  builder.dihedrals_ = topology.dihedrals_;
  builder.impropers_ = topology.impropers_;
  builder.properties_ = topology.properties_.columns_;
  builder.source_metadata_ = topology.source_metadata_;
  builder.base_version_ = topology.version_;
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
  const AtomIndex index{atoms_.size()};
  atoms_.push_back(std::move(atom));
  return operation::Result<AtomIndex>::success(index);
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
  bonds_.push_back(bond);
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
    Dihedral dihedral) {
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
  if (duplicate != dihedrals_.end()) {
    return invalid("duplicate dihedral atoms");
  }
  dihedrals_.push_back(dihedral);
  return std::nullopt;
}

std::optional<operation::Error> TopologyBuilder::add_improper(
    Improper improper) {
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
  if (duplicate != impropers_.end()) {
    return invalid("duplicate improper atoms");
  }
  impropers_.push_back(improper);
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

  auto topology = std::make_shared<Topology>();
  topology->version_ = base_version_ + 1U;
  topology->atoms_ = atoms_;
  topology->residues_ = residues_;
  topology->bonds_ = bonds_;
  topology->angles_ = angles_;
  topology->dihedrals_ = dihedrals_;
  topology->impropers_ = impropers_;
  topology->properties_.row_count_ = atoms_.size();
  topology->properties_.columns_ = properties_;
  topology->source_metadata_ = source_metadata_;
  return operation::Result<std::shared_ptr<const Topology>>::success(
      std::move(topology));
}

}  // namespace molshredder::model
