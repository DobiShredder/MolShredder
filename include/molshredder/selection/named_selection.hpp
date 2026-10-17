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
      const model::Topology& topology,
      const EvaluationContext& context = {});

  // Unions a new membership into an existing selection of the same update
  // mode (PyMOL select merge=1). A missing name behaves like set(). A dynamic
  // merge becomes the expression "(old) or (new)"; a static merge keeps the
  // old frozen membership and adds the new one.
  [[nodiscard]] std::optional<operation::Error> merge(
      std::string name, Expression expression, bool dynamic,
      const model::Topology& topology, const EvaluationContext& context = {});

  // Static selection whose frozen membership is the union of the expression
  // evaluated against every given context (PyMOL select state=-1).
  [[nodiscard]] std::optional<operation::Error> set_state_union(
      std::string name, Expression expression, const model::Topology& topology,
      const std::vector<EvaluationContext>& contexts);

  [[nodiscard]] std::optional<operation::Error> erase(std::string_view name);

  [[nodiscard]] operation::Result<Mask> evaluate(
      std::string_view name, const model::Topology& topology,
      const EvaluationContext& context = {}) const;

  [[nodiscard]] std::vector<NamedSelectionInfo> list() const;

  [[nodiscard]] operation::Result<NamedSelections> remap(
      const model::Topology& source, const model::Topology& target,
      const model::TopologyRemap& remap) const;

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
      const EvaluationContext& context,
      std::vector<std::string>& stack) const;

  std::map<std::string, Entry, std::less<>> entries_;
};

}  // namespace molshredder::selection
