#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "molshredder/model/topology.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/selection/evaluator.hpp"
#include "molshredder/selection/expression.hpp"
#include "molshredder/selection/named_selection.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

std::shared_ptr<const molshredder::model::Topology> make_topology() {
  using namespace molshredder::model;
  TopologyBuilder builder;
  const auto gly =
      builder.add_residue(ResidueRecord{"GLY", 10, "A", "A", "PROT"});
  const auto water =
      builder.add_residue(ResidueRecord{"HOH", 5, "", "W", "WAT"});
  static_cast<void>(builder.add_atom(
      AtomRecord{"N", 7U, gly.value(), "", 0, 101}));
  static_cast<void>(builder.add_atom(
      AtomRecord{"CA", 6U, gly.value(), "", 0, 102}));
  static_cast<void>(builder.add_atom(
      AtomRecord{"O", 8U, gly.value(), "", 0, 103}));
  static_cast<void>(builder.add_atom(
      AtomRecord{"O", 8U, water.value(), "", 0, 201}));
  return builder.build().value();
}

bool evaluates_to(const molshredder::model::Topology& topology,
                  std::string_view text,
                  const molshredder::selection::Mask& expected) {
  const auto expression = molshredder::selection::Expression::parse(text);
  if (!expression.has_value()) return false;
  const auto mask = molshredder::selection::evaluate(expression.value(),
                                                      topology);
  return mask.has_value() && mask.value() == expected;
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  const auto topology = make_topology();

  passed &= expect(evaluates_to(*topology, "all", {1U, 1U, 1U, 1U}) &&
                       evaluates_to(*topology, "none", {0U, 0U, 0U, 0U}),
                   "all and none must produce complete masks");
  passed &= expect(
      evaluates_to(*topology, "name CA or name O and chain A",
                   {0U, 1U, 1U, 0U}) &&
          evaluates_to(*topology, "(name CA or name O) and not resn HOH",
                       {0U, 1U, 1U, 0U}),
      "not/and/or precedence and parentheses must be deterministic");
  passed &= expect(evaluates_to(*topology, "name \"and\"", {0U, 0U, 0U, 0U}),
                   "quoted logical keywords must remain predicate values");
  passed &= expect(evaluates_to(*topology, "chain \"\"", {0U, 0U, 0U, 0U}),
                   "quoted empty identifier must remain a predicate value");
  passed &= expect(evaluates_to(*topology, "elem o", {0U, 0U, 1U, 1U}) &&
                       evaluates_to(*topology, "resi 10A", {1U, 1U, 1U, 0U}) &&
                       evaluates_to(*topology, "segi WAT", {0U, 0U, 0U, 1U}) &&
                       evaluates_to(*topology, "index 2:4", {0U, 1U, 1U, 1U}) &&
                       evaluates_to(*topology, "id 201", {0U, 0U, 0U, 1U}),
                   "field aliases, residue IDs and numeric ranges must work");

  const auto references =
      selection::Expression::parse("@first or (@second and @first)");
  passed &= expect(references.has_value() &&
                       references.value().named_references() ==
                           std::set<std::string, std::less<>>{"first", "second"},
                   "named references must be explicit and deduplicated");
  const auto resolved = selection::evaluate(
      references.value(), *topology,
      [](std::string_view name) {
        if (name == "first") {
          return operation::Result<selection::Mask>::success(
              {1U, 0U, 0U, 0U});
        }
        return operation::Result<selection::Mask>::success(
            {0U, 1U, 0U, 0U});
      });
  passed &= expect(resolved.has_value() &&
                       resolved.value() == selection::Mask({1U, 0U, 0U, 0U}),
                   "external named resolver must compose masks");

  for (const auto invalid : {"", "name", "name CA extra", "(name CA",
                             "name CA and", "unknown CA", "@", "not"}) {
    const auto parsed = selection::Expression::parse(invalid);
    passed &= expect(!parsed.has_value() &&
                         parsed.error().code ==
                             operation::ErrorCode::invalid_selection,
                     "malformed expression must return invalid_selection");
  }
  const auto invalid_range = selection::Expression::parse("index 0");
  passed &= expect(invalid_range.has_value() &&
                       !selection::evaluate(invalid_range.value(), *topology)
                            .has_value(),
                   "one-based index domain must be enforced at evaluation");
  const auto unresolved = selection::Expression::parse("@missing");
  passed &= expect(unresolved.has_value() &&
                       !selection::evaluate(unresolved.value(), *topology)
                            .has_value(),
                   "named term without a resolver must fail");

  selection::NamedSelections named;
  passed &= expect(!named.set("oxygen",
                              selection::Expression::parse("element O").value(),
                              true, *topology)
                        .has_value(),
                   "dynamic named selection must be created");
  passed &= expect(!named.set(
                        "protein_oxygen",
                        selection::Expression::parse(
                            "@oxygen and not resname HOH")
                            .value(),
                        true, *topology)
                        .has_value(),
                   "named selections must compose");
  passed &= expect(!named.set("first",
                              selection::Expression::parse("index 1").value(),
                              false, *topology)
                        .has_value(),
                   "static named selection must cache a mask");
  const auto protein_oxygen = named.evaluate("protein_oxygen", *topology);
  passed &= expect(protein_oxygen.has_value() &&
                       protein_oxygen.value() ==
                           selection::Mask({0U, 0U, 1U, 0U}),
                   "dynamic named selection must evaluate dependencies");

  const auto other_topology = make_topology();
  passed &= expect(named.evaluate("oxygen", *other_topology).has_value() &&
                       !named.evaluate("first", *other_topology).has_value(),
                   "dynamic selection may re-evaluate but static mask is snapshot-bound");
  auto target_builder = model::TopologyBuilder::from(*topology);
  const std::array retained{model::AtomId{4U}, model::AtomId{1U}};
  passed &= expect(!target_builder.retain_atoms(retained).has_value(),
                   "selection remap target must build");
  const auto target_topology = target_builder.build();
  const auto selection_remap = model::remap_topology(*topology,
                                                      *target_topology.value());
  const auto remapped_named = named.remap(*topology, *target_topology.value(),
                                         selection_remap);
  passed &= expect(
      remapped_named.has_value() &&
          remapped_named.value().evaluate("first", *target_topology.value())
                  .value() == selection::Mask({0U, 1U}) &&
          remapped_named.value().evaluate("oxygen", *target_topology.value())
                  .value() == selection::Mask({1U, 0U}),
      "static selections must follow stable identity and dynamic selections must re-evaluate on the target snapshot");
  passed &= expect(named.set(
                       "oxygen",
                       selection::Expression::parse("@protein_oxygen").value(),
                       true, *topology)
                       .has_value() &&
                       named.evaluate("oxygen", *topology).has_value(),
                   "cycle-producing replacement must roll back atomically");
  passed &= expect(named.erase("oxygen").has_value(),
                   "referenced named selection must not be erased");
  passed &= expect(named.erase("protein_oxygen") == std::nullopt &&
                       named.erase("oxygen") == std::nullopt &&
                       named.list().size() == 1U &&
                       named.list()[0].name == "first",
                   "dependency-order erase and deterministic list must work");
  passed &= expect(named.set("1bad", selection::Expression::parse("all").value(),
                             true, *topology)
                       .has_value() &&
                       named.erase("missing").has_value(),
                   "invalid names and missing erase must fail");

  return passed ? 0 : 1;
}
