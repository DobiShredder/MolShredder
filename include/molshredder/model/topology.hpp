#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

struct ResidueIndex {
  std::size_t value{};

  friend bool operator==(const ResidueIndex&, const ResidueIndex&) = default;
  friend auto operator<=>(const ResidueIndex&, const ResidueIndex&) = default;
};

struct ResidueRecord {
  std::string name;
  std::int64_t sequence_number{};
  std::string insertion_code;
  std::string chain_id;
  std::string segment_id;
};

struct AtomRecord {
  std::string name;
  std::uint8_t atomic_number{};
  ResidueIndex residue;
  std::string alternate_location;
  std::int32_t formal_charge{};
  std::optional<std::int64_t> source_serial;
};

enum class BondOrder : std::uint8_t {
  unknown,
  single,
  double_bond,
  triple,
  aromatic,
  amide,
};

struct Bond {
  AtomIndex first;
  AtomIndex second;
  BondOrder order{BondOrder::unknown};
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
  std::vector<ResidueRecord> residues_;
  std::vector<Bond> bonds_;
  std::vector<Angle> angles_;
  std::vector<Dihedral> dihedrals_;
  std::vector<Improper> impropers_;
  AtomPropertyTable properties_;
  std::map<std::string, std::string, std::less<>> source_metadata_;
};

class TopologyBuilder {
 public:
  [[nodiscard]] static TopologyBuilder from(const Topology& topology);

  [[nodiscard]] operation::Result<ResidueIndex> add_residue(
      ResidueRecord residue);

  [[nodiscard]] operation::Result<AtomIndex> add_atom(AtomRecord atom);

  [[nodiscard]] std::optional<operation::Error> add_bond(Bond bond);
  [[nodiscard]] std::optional<operation::Error> add_angle(Angle angle);
  [[nodiscard]] std::optional<operation::Error> add_dihedral(
      Dihedral dihedral, bool allow_duplicate_term = false);
  [[nodiscard]] std::optional<operation::Error> add_improper(
      Improper improper, bool allow_duplicate_term = false);

  [[nodiscard]] std::optional<operation::Error> add_property(
      std::string name, AtomPropertyColumn column,
      PropertyMetadata metadata = {});

  void set_source_metadata(std::string name, std::string value);

  [[nodiscard]] operation::Result<std::shared_ptr<const Topology>> build()
      const;

 private:
  [[nodiscard]] bool has_atom(AtomIndex atom) const noexcept;

  std::vector<AtomRecord> atoms_;
  std::vector<ResidueRecord> residues_;
  std::vector<Bond> bonds_;
  std::vector<Angle> angles_;
  std::vector<Dihedral> dihedrals_;
  std::vector<Improper> impropers_;
  std::map<std::string, AtomProperty, std::less<>> properties_;
  std::map<std::string, std::string, std::less<>> source_metadata_;
  std::uint64_t base_version_{};
};

[[nodiscard]] std::size_t column_size(const AtomPropertyColumn& column);
[[nodiscard]] PropertyKind property_kind(const AtomPropertyColumn& column);

}  // namespace molshredder::model
