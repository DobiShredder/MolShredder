#include <iostream>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/command/registry.hpp"
#include "molshredder/operation/error.hpp"

namespace {

using molshredder::command::Arguments;
using molshredder::command::Descriptor;
using molshredder::command::ParameterSpec;
using molshredder::command::ParameterType;
using molshredder::command::Registry;
using molshredder::command::Response;
using molshredder::command::UndoPolicy;
using molshredder::operation::ErrorCode;
using molshredder::operation::Result;
using molshredder::operation::TaskContext;

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

Descriptor center_descriptor() {
  return Descriptor{"analyze center",
                    "Calculate a geometric or mass-weighted center",
                    {ParameterSpec{"selection", ParameterType::text, true},
                     ParameterSpec{"mass_weighted", ParameterType::boolean,
                                   false},
                     ParameterSpec{"precision", ParameterType::integer,
                                   false}},
                    UndoPolicy::not_applicable};
}

}  // namespace

int main() {
  bool passed = true;
  Registry registry;

  const auto added = registry.add(
      center_descriptor(),
      [](const Arguments& arguments, TaskContext&) {
        return Result<Response>::success(
            {"center calculated", {{"selection", arguments.at("selection")}}});
      });
  passed &= expect(!added.has_value(), "valid command must register");

  const auto duplicate = registry.add(
      center_descriptor(), [](const Arguments&, TaskContext&) {
        return Result<Response>::success({});
      });
  passed &= expect(duplicate.has_value(), "duplicate command must fail");

  TaskContext context;
  const auto result = registry.invoke(
      "analyze center", {{"selection", "protein"}, {"precision", "6"}},
      context);
  passed &= expect(result.has_value(), "valid invocation must succeed");
  passed &= expect(result.has_value() &&
                       result.value().fields.at("selection") == "protein",
                   "handler must receive validated arguments");

  const auto missing = registry.invoke("analyze center", {}, context);
  passed &= expect(!missing.has_value(), "missing required value must fail");
  passed &= expect(!missing.has_value() &&
                       missing.error().code == ErrorCode::invalid_argument,
                   "validation must use stable error code");

  const auto invalid_type = registry.invoke(
      "analyze center", {{"selection", "protein"}, {"precision", "six"}},
      context);
  passed &= expect(!invalid_type.has_value(), "invalid type must fail");

  const auto unknown_parameter = registry.invoke(
      "analyze center", {{"selection", "protein"}, {"unknown", "x"}},
      context);
  passed &= expect(!unknown_parameter.has_value(),
                   "unknown parameter must fail");

  const auto missing_command = registry.invoke("unknown", {}, context);
  passed &= expect(!missing_command.has_value(), "unknown command must fail");
  passed &= expect(!missing_command.has_value() &&
                       missing_command.error().code == ErrorCode::not_found,
                   "unknown command must use not_found");

  TaskContext cancelled;
  cancelled.cancellation.request_cancel();
  const auto cancelled_result = registry.invoke(
      "analyze center", {{"selection", "protein"}}, cancelled);
  passed &= expect(!cancelled_result.has_value(),
                   "cancelled invocation must fail before handler");

  const auto descriptors = registry.descriptors();
  passed &= expect(descriptors.size() == 1,
                   "registry must expose one descriptor for help");
  passed &= expect(descriptors.front().canonical_name == "analyze center",
                   "descriptor list must be canonical and sorted");

  const auto expect_bad_descriptor =
      [&passed](Descriptor descriptor, std::string_view message) {
        Registry isolated;
        const auto failure = isolated.add(
            std::move(descriptor), [](const Arguments&, TaskContext&) {
              return Result<Response>::success({});
            });
        passed &= expect(failure.has_value() &&
                             failure->code == ErrorCode::invalid_argument,
                         message);
      };
  expect_bad_descriptor(
      Descriptor{"duplicate parameters",
                 "invalid",
                 {ParameterSpec{"value", ParameterType::text, false},
                  ParameterSpec{"value", ParameterType::text, false}},
                 {}},
      "duplicate parameter names must fail registration");
  expect_bad_descriptor(
      Descriptor{"required default",
                 "invalid",
                 {ParameterSpec{"value", ParameterType::text, true, "x"}},
                 {}},
      "required parameters must not declare defaults");
  expect_bad_descriptor(
      Descriptor{"wrong default type",
                 "invalid",
                 {ParameterSpec{"value", ParameterType::integer, false,
                                "many"}},
                 {}},
      "parameter defaults must match their declared type");
  expect_bad_descriptor(
      Descriptor{"wrong allowed type",
                 "invalid",
                 {ParameterSpec{"value", ParameterType::boolean, false,
                                std::nullopt, {"true", "maybe"}}},
                 {}},
      "allowed values must match their declared type");
  expect_bad_descriptor(
      Descriptor{"duplicate allowed",
                 "invalid",
                 {ParameterSpec{"value", ParameterType::text, false,
                                std::nullopt, {"x", "x"}}},
                 {}},
      "duplicate allowed values must fail registration");
  expect_bad_descriptor(
      Descriptor{"default outside allowed",
                 "invalid",
                 {ParameterSpec{"value", ParameterType::text, false, "z",
                                {"x", "y"}}},
                 {}},
      "defaults outside allowed values must fail registration");

  return passed ? 0 : 1;
}
