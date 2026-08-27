#pragma once

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/operation/result.hpp"

namespace molshredder::selection {

enum class Field {
  atom_name,
  element,
  alternate_location,
  residue_name,
  residue_id,
  chain,
  segment,
  object,
  rank,
  peptide_sequence,
  index,
  source_id,
  state,
  property,
};

struct Predicate {
  Field field{Field::atom_name};
  std::string value;
  bool literal{};
  std::string property;
};

enum class NumericField {
  index,
  source_id,
  formal_charge,
  coordinate_x,
  coordinate_y,
  coordinate_z,
  property,
};

enum class NumericNodeKind {
  literal,
  field,
  addition,
  subtraction,
  multiplication,
  division,
  negation,
};

struct NumericNode {
  NumericNodeKind kind{NumericNodeKind::literal};
  double literal{};
  NumericField field{NumericField::index};
  std::string property;
  std::shared_ptr<const NumericNode> left;
  std::shared_ptr<const NumericNode> right;
};

enum class ComparisonOperator {
  equal,
  not_equal,
  less,
  less_equal,
  greater,
  greater_equal,
  range,
};

struct NumericInterval {
  double first{};
  double last{};
};

struct NumericComparison {
  ComparisonOperator operation{ComparisonOperator::equal};
  std::shared_ptr<const NumericNode> left;
  std::shared_ptr<const NumericNode> right;
  std::vector<NumericInterval> intervals;
};

enum class ExpansionOperator {
  residue,
  chain,
  segment,
  object,
  molecule,
  calpha,
  neighbor,
  bound_to,
  bond_steps,
  ring,
  unit_cell,
};

struct Expansion {
  ExpansionOperator operation{ExpansionOperator::residue};
  std::size_t steps{1U};
};

enum class SpatialOperator {
  around,
  expand,
  within,
  near_to,
  beyond,
  vdw_gap,
};

struct SpatialRelation {
  SpatialOperator operation{SpatialOperator::within};
  double distance{};
};

enum class MatchOperator { identifiers, name_residue };

enum class ChemicalClass {
  hetero,
  hydrogen,
  donor,
  acceptor,
  polymer,
  protein,
  nucleic,
  organic,
  inorganic,
  solvent,
  metal,
  backbone,
  sidechain,
  guide,
};

enum class NodeKind {
  all,
  none,
  present,
  bonded,
  boolean_property,
  scene_center,
  rotation_origin,
  chemical,
  predicate,
  numeric_comparison,
  expansion,
  spatial,
  match,
  named,
  conjunction,
  disjunction,
  subtraction,
  negation,
  first,
  last,
};

struct Node {
  NodeKind kind{NodeKind::none};
  Predicate predicate;
  NumericComparison numeric_comparison;
  Expansion expansion;
  SpatialRelation spatial;
  MatchOperator match{MatchOperator::identifiers};
  ChemicalClass chemical{ChemicalClass::polymer};
  std::string name;
  std::shared_ptr<const Node> left;
  std::shared_ptr<const Node> right;
};

class Expression {
 public:
  [[nodiscard]] static operation::Result<Expression> parse(
      std::string_view source);

  [[nodiscard]] const std::string& source() const noexcept { return source_; }
  [[nodiscard]] const std::shared_ptr<const Node>& root() const noexcept {
    return root_;
  }
  [[nodiscard]] std::set<std::string, std::less<>> named_references() const;

 private:
  Expression(std::string source, std::shared_ptr<const Node> root)
      : source_{std::move(source)}, root_{std::move(root)} {}

  std::string source_;
  std::shared_ptr<const Node> root_;
};

}  // namespace molshredder::selection
