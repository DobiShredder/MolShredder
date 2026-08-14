#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/model/topology.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/selection/evaluator.hpp"
#include "molshredder/selection/expression.hpp"

namespace molshredder::selection {

struct NamedSelectionInfo {
  std::string name;
  std::string expression;
  bool dynamic{};
};

class NamedSelections {
 public:
  [[nodiscard]] std::optional<operation::Error> set(
      std::string name, Expression expression, bool dynamic,
      const model::Topology& topology);

  [[nodiscard]] std::optional<operation::Error> erase(std::string_view name);

  [[nodiscard]] operation::Result<Mask> evaluate(
      std::string_view name, const model::Topology& topology) const;

  [[nodiscard]] std::vector<NamedSelectionInfo> list() const;

 private:
  struct Entry {
    Expression expression;
    bool dynamic{};
    const model::Topology* static_topology{};
    std::uint64_t static_topology_version{};
    Mask static_mask;
  };

  [[nodiscard]] operation::Result<Mask> evaluate_impl(
      std::string_view name, const model::Topology& topology,
      std::vector<std::string>& stack) const;

  std::map<std::string, Entry, std::less<>> entries_;
};

}  // namespace molshredder::selection
