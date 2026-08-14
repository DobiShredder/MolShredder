#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/command/registry.hpp"
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

}  // namespace

int main(int argc, char* argv[]) {
  using molshredder::command::AliasSpec;
  using molshredder::command::Arguments;
  using molshredder::command::Descriptor;
  using molshredder::command::Invocation;
  using molshredder::command::ParameterSpec;
  using molshredder::command::ParameterType;
  using molshredder::command::Registry;
  using molshredder::command::Response;
  using molshredder::operation::ErrorCode;
  using molshredder::operation::Result;
  using molshredder::operation::TaskContext;

  bool passed = true;
  passed &= expect(argc == 2, "alias test requires one golden fixture path");
  if (argc != 2) {
    return 1;
  }
  Registry registry;
  const auto early_alias =
      registry.add_alias(AliasSpec{"early", "analyze center", {}});
  passed &= expect(early_alias.has_value() &&
                       early_alias->code == ErrorCode::not_found,
                   "alias target must already be registered");

  const auto registration = registry.add(
      Descriptor{"analyze center",
                 "Calculate a center",
                 {ParameterSpec{"selection", ParameterType::text, true},
                  ParameterSpec{"mode", ParameterType::text, false},
                  ParameterSpec{"precision", ParameterType::integer, false}},
                 {}},
      [](const Arguments&, TaskContext&) {
        return Result<Response>::success({"center calculated", {}});
      });
  passed &= expect(!registration.has_value(), "command must register");
  passed &= expect(!registry.add_alias(
                        AliasSpec{"center", "analyze center", {}})
                        .has_value(),
                   "plain alias must register");
  passed &= expect(
      !registry
           .add_alias(AliasSpec{"com", "analyze center", {{"mode", "com"}}})
           .has_value(),
      "alias with defaults must register");

  const auto duplicate =
      registry.add_alias(AliasSpec{"com", "analyze center", {}});
  passed &= expect(duplicate.has_value() &&
                       duplicate->code == ErrorCode::invalid_argument,
                   "duplicate alias must fail");
  const auto invalid_default = registry.add_alias(
      AliasSpec{"bad", "analyze center", {{"precision", "many"}}});
  passed &= expect(invalid_default.has_value() &&
                       invalid_default->code == ErrorCode::invalid_argument,
                   "alias defaults must match the target schema");
  const auto command_collision = registry.add(
      Descriptor{"com", "Conflicting command", {}, {}},
      [](const Arguments&, TaskContext&) {
        return Result<Response>::success({"unexpected", {}});
      });
  passed &= expect(command_collision.has_value() &&
                       command_collision->code == ErrorCode::invalid_argument,
                   "canonical command must not collide with an alias");

  const std::vector<Invocation> inputs{
      {"analyze center", {{"selection", "protein"}, {"mode", "centroid"}}},
      {"center", {{"selection", "all"}}},
      {"com", {{"selection", "chain A"}}},
      {"com", {{"selection", "chain A"}, {"mode", "centroid"}}},
  };
  std::string actual;
  for (const auto& input : inputs) {
    const auto expanded = registry.expand(input);
    passed &= expect(expanded.has_value(), "known alias must expand");
    if (expanded.has_value()) {
      actual += molshredder::command::serialize(expanded.value()) + '\n';
    }
  }

  std::ifstream golden_stream{argv[1]};
  const std::string expected{std::istreambuf_iterator<char>{golden_stream},
                             std::istreambuf_iterator<char>{}};
  passed &= expect(golden_stream.good() || golden_stream.eof(),
                   "alias golden fixture must be readable");
  passed &= expect(actual == expected,
                   "alias expansion must match the canonical golden fixture");

  const auto unknown = registry.expand(Invocation{"missing", {}});
  passed &= expect(!unknown.has_value() &&
                       unknown.error().code == ErrorCode::not_found,
                   "unknown aliases must return a stable not-found error");

  const auto aliases = registry.aliases();
  passed &= expect(aliases.size() == 2 && aliases[0].name == "center" &&
                       aliases[1].name == "com",
                   "alias metadata must be deterministic and sorted");

  return passed ? 0 : 1;
}
