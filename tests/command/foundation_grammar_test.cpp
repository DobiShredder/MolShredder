#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/command/foundation_grammar.hpp"
#include "molshredder/command/serialization.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/operation/result.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main(int argc, char *argv[]) {
  using molshredder::command::Arguments;
  using molshredder::command::Invocation;
  using molshredder::command::Registry;
  using molshredder::command::Response;
  using molshredder::operation::ErrorCode;
  using molshredder::operation::Result;
  using molshredder::operation::TaskContext;

  bool passed = true;
  passed &= expect(molshredder::command::kFoundationGrammarVersion == 1U,
                   "foundation command grammar must be pinned at version 1");
  passed &=
      expect(argc == 2, "foundation grammar test requires one golden fixture");
  if (argc != 2) {
    return 1;
  }

  const auto descriptors =
      molshredder::command::foundation_command_descriptors();
  const auto aliases = molshredder::command::foundation_command_aliases();
  const auto file_descriptors =
      molshredder::command::file_command_descriptors();
  passed &= expect(descriptors.size() == 8,
                   "foundation grammar must define exactly eight commands");
  passed &= expect(aliases.size() == 9,
                   "foundation grammar v1 must expose nine shorthand aliases");
  passed &= expect(
      file_descriptors.size() == 4U &&
          file_descriptors.front().canonical_name == "format list" &&
          file_descriptors.back().canonical_name == "save",
      "additive file grammar must expose format, volume and save commands");

  Registry registry;
  for (auto descriptor : descriptors) {
    const auto command_name = descriptor.canonical_name;
    const auto failure =
        registry.add(std::move(descriptor),
                     [command_name](const Arguments &arguments, TaskContext &) {
                       molshredder::command::Value::Object fields;
                       for (const auto &[name, value] : arguments) {
                         fields.emplace(name, value);
                       }
                       return Result<Response>::success(
                           {command_name + " accepted", std::move(fields)});
                     });
    passed &= expect(!failure.has_value(), "foundation command must register");
  }
  for (auto alias : aliases) {
    passed &= expect(!registry.add_alias(std::move(alias)).has_value(),
                     "foundation alias must register");
  }

  const std::vector<Invocation> inputs{
      {"open", {{"path", "model.pdb"}, {"file-format", "pdb"}}},
      {"select", {{"name", "protein"}, {"expression", "polymer.protein"}}},
      {"show", {{"representation", "sticks"}}},
      {"center", {}},
      {"com", {{"selection", "chain A"}, {"precision", "4"}}},
      {"dist", {{"from", "id 1"}, {"to", "id 2"}}},
  };
  std::string actual;
  for (const auto &input : inputs) {
    const auto normalized = registry.normalize(input);
    passed &= expect(normalized.has_value(),
                     "valid foundation invocation must normalize");
    if (normalized.has_value()) {
      actual += molshredder::command::serialize(normalized.value()) + '\n';
    }
  }

  std::ifstream golden_stream{argv[1]};
  const std::string expected{std::istreambuf_iterator<char>{golden_stream},
                             std::istreambuf_iterator<char>{}};
  passed &= expect(actual == expected,
                   "normalized grammar must match the canonical golden file");

  const auto invalid_representation =
      registry.normalize(Invocation{"show", {{"representation", "surface"}}});
  passed &= expect(!invalid_representation.has_value() &&
                       invalid_representation.error().code ==
                           ErrorCode::invalid_argument,
                   "representation choices must be registry-validated");
  const auto invalid_precision =
      registry.normalize(Invocation{"analyze center", {{"precision", "16"}}});
  passed &= expect(!invalid_precision.has_value() &&
                       invalid_precision.error().message.find("not allowed") !=
                           std::string::npos,
                   "precision must remain in the common 0-15 range");
  const auto missing_path = registry.normalize(Invocation{"load", {}});
  passed &= expect(!missing_path.has_value() &&
                       missing_path.error().message ==
                           "missing required parameter: path",
                   "load must require an explicit path");

  TaskContext context;
  const auto invoked =
      registry.invoke(Invocation{"com", {{"selection", "protein"}}}, context);
  passed &= expect(invoked.has_value(),
                   "registry invoke must normalize aliases and defaults");
  passed &= expect(
      invoked.has_value() &&
          std::get<std::string>(invoked.value().fields.at("mode").data) ==
              "com" &&
          std::get<std::string>(invoked.value().fields.at("unit").data) ==
              "angstrom",
      "handler must receive normalized default arguments");

  return passed ? 0 : 1;
}
