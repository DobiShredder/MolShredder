#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "molshredder/operation/error.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::model {

struct AtomIndex {
  std::size_t value{};

  friend bool operator==(const AtomIndex&, const AtomIndex&) = default;
  friend auto operator<=>(const AtomIndex&, const AtomIndex&) = default;
};

struct AtomId {
  std::uint64_t value{};

  friend bool operator==(const AtomId&, const AtomId&) = default;
  friend auto operator<=>(const AtomId&, const AtomId&) = default;
};

struct BondId {
  std::uint64_t value{};

  friend bool operator==(const BondId&, const BondId&) = default;
  friend auto operator<=>(const BondId&, const BondId&) = default;
};

inline constexpr unsigned int kTopologyReferenceSchemaVersion = 1U;

struct TopologySnapshotReference {
  unsigned int schema_version{kTopologyReferenceSchemaVersion};
  std::uint64_t object_id{};
  std::uint64_t topology_version{};
};

struct AtomReference {
  TopologySnapshotReference snapshot;
  AtomId atom_id;
};

struct BondReference {
  TopologySnapshotReference snapshot;
  BondId bond_id;
};

struct ResolvedAtomReference {
  AtomIndex index;
  std::uint64_t current_topology_version{};
  bool remapped{};
};

struct ResolvedBondReference {
  std::size_t index{};
  std::uint64_t current_topology_version{};
  bool remapped{};
};

struct ResidueIndex {
  std::size_t value{};

  friend bool operator==(const ResidueIndex&, const ResidueIndex&) = default;
  friend auto operator<=>(const ResidueIndex&, const ResidueIndex&) = default;
};

enum class ChemicalAnnotationOrigin : std::uint8_t {
  unspecified,
  explicit_input,
  inferred,
  user_override,
};

enum class ResidueKind : std::uint8_t {
  unknown,
  amino_acid,
  nucleic_acid,
  carbohydrate,
  solvent,
  ion,
  ligand,
};

enum class PolymerType : std::uint8_t {
  none,
  protein,
  dna,
  rna,
  carbohydrate,
  other,
};

struct ResidueRecord {
  std::string name;
  std::int64_t sequence_number{};
  std::string insertion_code;
  std::string chain_id;
  std::string segment_id;
  ResidueKind kind{ResidueKind::unknown};
  PolymerType polymer_type{PolymerType::none};
  ChemicalAnnotationOrigin chemical_origin{
      ChemicalAnnotationOrigin::unspecified};

  ResidueRecord() = default;
  ResidueRecord(std::string residue_name, std::int64_t sequence,
                std::string insertion, std::string chain,
                std::string segment,
                ResidueKind residue_kind = ResidueKind::unknown,
                PolymerType polymer = PolymerType::none,
                ChemicalAnnotationOrigin origin =
                    ChemicalAnnotationOrigin::unspecified)
      : name{std::move(residue_name)},
        sequence_number{sequence},
        insertion_code{std::move(insertion)},
        chain_id{std::move(chain)},
        segment_id{std::move(segment)},
        kind{residue_kind},
        polymer_type{polymer},
        chemical_origin{origin} {}
};

enum class AtomStereoParity : std::uint8_t {
  unspecified,
  odd,
  even,
  either,
};

enum class RadicalState : std::uint8_t {
  none,
  singlet,
  doublet,
  triplet,
};

struct AtomRecord {
  std::string name;
  std::uint8_t atomic_number{};
  ResidueIndex residue;
  std::string alternate_location;
  std::int32_t formal_charge{};
  std::optional<std::int64_t> source_serial;
  std::optional<std::uint16_t> isotope_mass_number;
  AtomStereoParity stereo_parity{AtomStereoParity::unspecified};
  RadicalState radical{RadicalState::none};
  bool formal_charge_present{};
  ChemicalAnnotationOrigin chemical_origin{
      ChemicalAnnotationOrigin::unspecified};

  AtomRecord() = default;
  AtomRecord(std::string atom_name, std::uint8_t element,
             ResidueIndex residue_index, std::string alt_location,
             std::int32_t charge,
             std::optional<std::int64_t> serial = std::nullopt,
             std::optional<std::uint16_t> isotope = std::nullopt,
             AtomStereoParity parity = AtomStereoParity::unspecified,
             RadicalState radical_state = RadicalState::none,
             bool charge_present = false,
             ChemicalAnnotationOrigin origin =
                 ChemicalAnnotationOrigin::unspecified)
      : name{std::move(atom_name)},
        atomic_number{element},
        residue{residue_index},
        alternate_location{std::move(alt_location)},
        formal_charge{charge},
        source_serial{serial},
        isotope_mass_number{isotope},
        stereo_parity{parity},
        radical{radical_state},
        formal_charge_present{charge_present},
        chemical_origin{origin} {}
};

enum class BondOrder : std::uint8_t {
  unknown,
  single,
  double_bond,
  triple,
  aromatic,
  amide,
  zero,
  query,
};

enum class BondQuery : std::uint8_t {
  none,
  single_or_double,
  single_or_aromatic,
  double_or_aromatic,
  any,
};

enum class BondStereo : std::uint8_t {
  none,
  up,
  cis_or_trans,
  either,
  down,
};

struct Bond {
  AtomIndex first;
  AtomIndex second;
  BondOrder order{BondOrder::unknown};
  BondQuery query{BondQuery::none};
  BondStereo stereo{BondStereo::none};
  ChemicalAnnotationOrigin order_origin{
      ChemicalAnnotationOrigin::unspecified};

  Bond() = default;
  Bond(AtomIndex first_atom, AtomIndex second_atom, BondOrder bond_order,
       BondQuery bond_query = BondQuery::none,
       BondStereo bond_stereo = BondStereo::none,
       ChemicalAnnotationOrigin origin =
           ChemicalAnnotationOrigin::unspecified)
      : first{first_atom},
        second{second_atom},
        order{bond_order},
        query{bond_query},
        stereo{bond_stereo},
        order_origin{origin} {}
};

struct Angle {
  AtomIndex first;
  AtomIndex center;
  AtomIndex third;
};

struct Dihedral {
  AtomIndex first;
  AtomIndex second;
  AtomIndex third;
  AtomIndex fourth;
};

struct Improper {
  AtomIndex center;
  AtomIndex first;
  AtomIndex second;
  AtomIndex third;
};

struct CmapTerm {
  std::array<AtomIndex, 8> atoms;
};

struct BooleanColumn {
  std::vector<std::uint8_t> values;
};

using AtomPropertyColumn =
    std::variant<BooleanColumn, std::vector<std::int64_t>,
                 std::vector<std::uint64_t>, std::vector<float>,
                 std::vector<double>, std::vector<std::string>>;

struct PropertyMetadata {
  std::optional<std::string> unit;
  std::string source;
  std::map<std::string, std::string, std::less<>> annotations;
};

struct AtomProperty {
  AtomPropertyColumn values;
  PropertyMetadata metadata;
};

enum class PropertyKind {
  boolean,
  integer,
  unsigned_integer,
  float32,
  float64,
  text,
};

class AtomPropertyTable {
 public:
  [[nodiscard]] std::size_t row_count() const noexcept { return row_count_; }

  [[nodiscard]] const AtomProperty* find(std::string_view name) const;

  [[nodiscard]] std::optional<PropertyKind> kind(
      std::string_view name) const;

  [[nodiscard]] std::vector<std::string> names() const;

 private:
  friend class TopologyBuilder;

  std::size_t row_count_{};
  std::map<std::string, AtomProperty, std::less<>> columns_;
};

class Topology {
 public:
  [[nodiscard]] std::uint64_t version() const noexcept { return version_; }
  [[nodiscard]] std::size_t atom_count() const noexcept { return atoms_.size(); }
  [[nodiscard]] std::size_t residue_count() const noexcept {
    return residues_.size();
  }

  [[nodiscard]] const std::vector<AtomRecord>& atoms() const noexcept {
    return atoms_;
  }
  [[nodiscard]] const std::vector<AtomId>& atom_ids() const noexcept {
    return atom_ids_;
  }
  [[nodiscard]] const std::vector<BondId>& bond_ids() const noexcept {
    return bond_ids_;
  }
  [[nodiscard]] std::optional<AtomId> atom_id(AtomIndex index) const noexcept;
  [[nodiscard]] std::optional<AtomIndex> atom_index(AtomId id) const noexcept;
  [[nodiscard]] std::optional<BondId> bond_id(std::size_t index) const noexcept;
  [[nodiscard]] std::optional<std::size_t> bond_index(BondId id) const noexcept;
  [[nodiscard]] const std::vector<ResidueRecord>& residues() const noexcept {
    return residues_;
  }
  [[nodiscard]] const std::vector<Bond>& bonds() const noexcept {
    return bonds_;
  }
  [[nodiscard]] const std::vector<Angle>& angles() const noexcept {
    return angles_;
  }
  [[nodiscard]] const std::vector<Dihedral>& dihedrals() const noexcept {
    return dihedrals_;
  }
  [[nodiscard]] const std::vector<Improper>& impropers() const noexcept {
    return impropers_;
  }
  [[nodiscard]] const std::vector<CmapTerm>& cmap_terms() const noexcept {
    return cmap_terms_;
  }
  [[nodiscard]] const AtomPropertyTable& properties() const noexcept {
    return properties_;
  }
  [[nodiscard]] const std::map<std::string, std::string, std::less<>>&
  source_metadata() const noexcept {
    return source_metadata_;
  }

 private:
  friend class TopologyBuilder;

  std::uint64_t version_{};
  std::vector<AtomRecord> atoms_;
  std::vector<AtomId> atom_ids_;
  std::vector<ResidueRecord> residues_;
  std::vector<Bond> bonds_;
  std::vector<BondId> bond_ids_;
  std::vector<Angle> angles_;
  std::vector<Dihedral> dihedrals_;
  std::vector<Improper> impropers_;
  std::vector<CmapTerm> cmap_terms_;
  AtomPropertyTable properties_;
  std::map<std::string, std::string, std::less<>> source_metadata_;
  std::uint64_t next_atom_id_{1U};
  std::uint64_t next_bond_id_{1U};
};

class TopologyBuilder {
 public:
  [[nodiscard]] static TopologyBuilder from(const Topology& topology);

  [[nodiscard]] operation::Result<ResidueIndex> add_residue(
      ResidueRecord residue);

  [[nodiscard]] operation::Result<AtomIndex> add_atom(AtomRecord atom);
  [[nodiscard]] std::optional<operation::Error> set_atom(
      AtomIndex atom, AtomRecord replacement);
  [[nodiscard]] std::optional<operation::Error> set_residue(
      ResidueIndex residue, ResidueRecord replacement);

  // Retains the requested stable atom IDs in the supplied order. Connectivity
  // containing removed atoms is discarded; surviving atom/bond IDs persist.
  [[nodiscard]] std::optional<operation::Error>
  retain_atoms(std::span<const AtomId> ordered_atom_ids);

  [[nodiscard]] std::optional<operation::Error> add_bond(Bond bond);
  [[nodiscard]] std::optional<operation::Error> set_bond_semantics(
      std::size_t bond_index, BondOrder order, BondQuery query,
      ChemicalAnnotationOrigin origin);
  [[nodiscard]] std::optional<operation::Error> set_residue_semantics(
      ResidueIndex residue, ResidueKind kind, PolymerType polymer_type,
      ChemicalAnnotationOrigin origin);
  [[nodiscard]] std::optional<operation::Error> add_angle(Angle angle);
  [[nodiscard]] std::optional<operation::Error> add_dihedral(
      Dihedral dihedral, bool allow_duplicate_term = false);
  [[nodiscard]] std::optional<operation::Error> add_improper(
      Improper improper, bool allow_duplicate_term = false);
  [[nodiscard]] std::optional<operation::Error> add_cmap_term(
      CmapTerm term, bool allow_duplicate_term = false);

  [[nodiscard]] std::optional<operation::Error> add_property(
      std::string name, AtomPropertyColumn column,
      PropertyMetadata metadata = {});

  void set_source_metadata(std::string name, std::string value);

  [[nodiscard]] operation::Result<std::shared_ptr<const Topology>> build()
      const;

 private:
  [[nodiscard]] bool has_atom(AtomIndex atom) const noexcept;

  std::vector<AtomRecord> atoms_;
  std::vector<AtomId> atom_ids_;
  std::vector<ResidueRecord> residues_;
  std::vector<Bond> bonds_;
  std::vector<BondId> bond_ids_;
  std::vector<Angle> angles_;
  std::vector<Dihedral> dihedrals_;
  std::vector<Improper> impropers_;
  std::vector<CmapTerm> cmap_terms_;
  std::map<std::string, AtomProperty, std::less<>> properties_;
  std::map<std::string, std::string, std::less<>> source_metadata_;
  std::uint64_t base_version_{};
  std::uint64_t next_atom_id_{1U};
  std::uint64_t next_bond_id_{1U};
};

struct TopologyRemap {
  std::uint64_t source_version{};
  std::uint64_t target_version{};
  std::vector<std::optional<AtomIndex>> source_atoms;
  std::vector<std::optional<std::size_t>> source_bonds;
  std::vector<std::optional<AtomIndex>> target_atoms;
  std::vector<std::optional<std::size_t>> target_bonds;
};

[[nodiscard]] TopologyRemap remap_topology(const Topology& source,
                                           const Topology& target);

[[nodiscard]] operation::Result<ResolvedAtomReference> resolve_atom_reference(
    const AtomReference& reference, std::uint64_t object_id,
    const Topology& topology);
[[nodiscard]] operation::Result<ResolvedBondReference> resolve_bond_reference(
    const BondReference& reference, std::uint64_t object_id,
    const Topology& topology);
[[nodiscard]] std::optional<operation::Error> validate_topology_snapshot(
    const TopologySnapshotReference& reference, std::uint64_t object_id,
    const Topology& topology);

[[nodiscard]] std::size_t column_size(const AtomPropertyColumn& column);
[[nodiscard]] PropertyKind property_kind(const AtomPropertyColumn& column);

}  // namespace molshredder::model
