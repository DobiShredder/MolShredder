#pragma once

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "molshredder/operation/result.hpp"

namespace molshredder::selection {

enum class Field {
  atom_name,
  element,
  residue_name,
  residue_id,
  chain,
  segment,
  index,
  source_id,
};

struct Predicate {
  Field field{Field::atom_name};
  std::string value;
};

enum class NodeKind {
  all,
  none,
  predicate,
  named,
  conjunction,
  disjunction,
  negation,
};

struct Node {
  NodeKind kind{NodeKind::none};
  Predicate predicate;
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
