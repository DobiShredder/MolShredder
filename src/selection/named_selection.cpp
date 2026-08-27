#include "molshredder/selection/named_selection.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <utility>

namespace molshredder::selection {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_selection,
                          std::move(message), std::move(suggestion)};
}

bool valid_name(std::string_view name) {
  if (name.empty() ||
      (std::isalpha(static_cast<unsigned char>(name.front())) == 0 &&
       name.front() != '_')) {
    return false;
  }
  return std::all_of(name.begin() + 1, name.end(), [](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return std::isalnum(byte) != 0 || character == '_' || character == '-' ||
           character == '.';
  });
}

}  // namespace

std::optional<operation::Error> NamedSelections::set(
    std::string name, Expression expression, bool dynamic,
    const model::Topology& topology, const EvaluationContext& context) {
  if (!valid_name(name) || name == "all" || name == "none") {
    return invalid(
        "named selection name is invalid or reserved",
        "start with a letter or underscore and use letters, digits, _, -, .");
  }

  const auto previous = entries_.find(name);
  std::optional<Entry> saved;
  if (previous != entries_.end()) saved = previous->second;
  entries_.insert_or_assign(
      name, Entry{std::move(expression), true, nullptr, 0U, {}});

  std::vector<std::string> stack;
  auto mask = evaluate_impl(name, topology, context, stack);
  if (!mask.has_value()) {
    if (saved.has_value()) {
      entries_.insert_or_assign(name, std::move(saved.value()));
    } else {
      entries_.erase(name);
    }
    return mask.error();
  }
  auto& entry = entries_.at(name);
  entry.dynamic = dynamic;
  if (!dynamic) {
    entry.static_topology = &topology;
    entry.static_topology_version = topology.version();
    entry.static_mask = std::move(mask.value());
  }
  return std::nullopt;
}

std::optional<operation::Error> NamedSelections::erase(std::string_view name) {
  if (!entries_.contains(name)) {
    return operation::Error{operation::ErrorCode::not_found,
                            "named selection '" + std::string{name} +
                                "' does not exist",
                            {}};
  }
  for (const auto& [other_name, entry] : entries_) {
    if (other_name != name && entry.expression.named_references().contains(name)) {
      return invalid("named selection is referenced by '@" + other_name + "'",
                     "replace or erase dependent selections first");
    }
  }
  entries_.erase(entries_.find(name));
  return std::nullopt;
}

operation::Result<Mask> NamedSelections::evaluate(
    std::string_view name, const model::Topology& topology,
    const EvaluationContext& context) const {
  std::vector<std::string> stack;
  return evaluate_impl(name, topology, context, stack);
}

operation::Result<Mask> NamedSelections::evaluate_impl(
    std::string_view name, const model::Topology& topology,
    const EvaluationContext& context, std::vector<std::string>& stack) const {
  const auto found = entries_.find(name);
  if (found == entries_.end()) {
    return operation::Result<Mask>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "named selection '" + std::string{name} + "' does not exist", {}});
  }
  if (std::find(stack.begin(), stack.end(), name) != stack.end()) {
    return operation::Result<Mask>::failure(
        invalid("named selection cycle contains '@" + std::string{name} +
                "'"));
  }
  const auto& entry = found->second;
  if (!entry.dynamic) {
    if (entry.static_topology != &topology ||
        entry.static_topology_version != topology.version() ||
        !mask_is_valid(entry.static_mask, topology.atom_count())) {
      return operation::Result<Mask>::failure(invalid(
          "static named selection belongs to a different topology snapshot",
          "redefine it for the current topology or use a dynamic selection"));
    }
    return operation::Result<Mask>::success(entry.static_mask);
  }

  stack.emplace_back(name);
  const auto resolved = selection::evaluate(
      entry.expression, topology,
      [&](std::string_view nested) {
        return evaluate_impl(nested, topology, context, stack);
      },
      context);
  stack.pop_back();
  return resolved;
}

std::vector<NamedSelectionInfo> NamedSelections::list() const {
  std::vector<NamedSelectionInfo> result;
  result.reserve(entries_.size());
  for (const auto& [name, entry] : entries_) {
    result.push_back({name, entry.expression.source(), entry.dynamic});
  }
  return result;
}

operation::Result<NamedSelections> NamedSelections::remap(
    const model::Topology& source, const model::Topology& target,
    const model::TopologyRemap& remap) const {
  if (remap.source_version != source.version() ||
      remap.target_version != target.version() ||
      remap.source_atoms.size() != source.atom_count() ||
      remap.target_atoms.size() != target.atom_count()) {
    return operation::Result<NamedSelections>::failure(
        invalid("named selection topology remap does not match snapshots"));
  }
  auto result = *this;
  for (auto& [name, entry] : result.entries_) {
    static_cast<void>(name);
    if (entry.dynamic)
      continue;
    if (entry.static_topology != &source ||
        entry.static_topology_version != source.version() ||
        !mask_is_valid(entry.static_mask, source.atom_count())) {
      return operation::Result<NamedSelections>::failure(invalid(
          "static named selection belongs to a stale topology snapshot"));
    }
    Mask target_mask(target.atom_count(), 0U);
    for (std::size_t source_index = 0;
         source_index < remap.source_atoms.size(); ++source_index) {
      if (entry.static_mask[source_index] == 0U ||
          !remap.source_atoms[source_index].has_value())
        continue;
      target_mask[remap.source_atoms[source_index]->value] = 1U;
    }
    entry.static_topology = &target;
    entry.static_topology_version = target.version();
    entry.static_mask = std::move(target_mask);
  }
  return operation::Result<NamedSelections>::success(std::move(result));
}

}  // namespace molshredder::selection
