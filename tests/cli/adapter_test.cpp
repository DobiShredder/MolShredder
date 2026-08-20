#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/cli/adapter.hpp"
#include "molshredder/command/registry.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/error.hpp"

namespace {

using molshredder::command::Arguments;
using molshredder::command::AliasSpec;
using molshredder::command::Descriptor;
using molshredder::command::ParameterSpec;
using molshredder::command::ParameterType;
using molshredder::command::Registry;
using molshredder::command::Response;
using molshredder::operation::Result;
using molshredder::operation::Error;
using molshredder::operation::ErrorCode;
using molshredder::operation::TaskContext;

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

int run(molshredder::cli::Adapter& adapter,
        std::vector<std::string> arguments, std::ostringstream& output,
        std::ostringstream& error, std::string_view input_text = {}) {
  std::vector<char*> argv;
  argv.reserve(arguments.size());
  for (auto& argument : arguments) {
    argv.push_back(argument.data());
  }
  std::istringstream input{std::string{input_text}};
  return adapter.run(static_cast<int>(argv.size()), argv.data(), input, output,
                     error);
}

}  // namespace

int main() {
  bool passed = true;
  Registry registry;
  const auto registration = registry.add(
      Descriptor{"analyze center",
                 "Calculate a center",
                 {ParameterSpec{"selection", ParameterType::text, true}},
                 {}},
      [](const Arguments& arguments, TaskContext&) {
        return Result<Response>::success(
            {"center calculated", {{"selection", arguments.at("selection")}}});
      });
  passed &= expect(!registration.has_value(), "command must register");
  const auto alias_registration = registry.add_alias(
      AliasSpec{"center", "analyze center", {}});
  passed &= expect(!alias_registration.has_value(), "alias must register");
  const auto failure_registration = registry.add(
      Descriptor{"analyze fail", "Return a test failure", {}, {}},
      [](const Arguments&, TaskContext&) {
        return Result<Response>::failure(
            Error{ErrorCode::invalid_selection, "selection failed",
                  "choose a valid selection"});
      });
  passed &= expect(!failure_registration.has_value(),
                   "failure command must register");

  molshredder::cli::Adapter adapter{registry};
  std::ostringstream output;
  std::ostringstream error;
  const int success = run(adapter,
                          {"molshredder", "analyze", "center", "--selection",
                           "chain A"},
                          output, error);
  passed &= expect(success == 0, "valid CLI command must succeed");
  passed &= expect(output.str().find("selection=chain A") != std::string::npos,
                   "CLI must print typed response fields");
  passed &= expect(error.str().empty(), "valid CLI must not print errors");

  output.str({});
  error.str({});
  const int alias = run(adapter,
                        {"molshredder", "center", "--selection", "chain B"},
                        output, error);
  passed &= expect(alias == 0, "CLI alias must succeed");
  passed &= expect(output.str().find("selection=chain B") != std::string::npos,
                   "CLI alias must invoke the canonical handler");
  passed &= expect(error.str().empty(), "valid CLI alias must not print errors");

  output.str({});
  error.str({});
  const int json = run(adapter,
                       {"molshredder", "analyze", "center", "--selection",
                        "chain C", "--format", "json"},
                       output, error);
  passed &= expect(json == 0, "JSON CLI result must succeed");
  passed &= expect(output.str().find("\"schema_version\":2") !=
                           std::string::npos &&
                       output.str().find("\"selection\":\"chain C\"") !=
                           std::string::npos,
                   "CLI must preserve typed response fields in JSON");
  passed &= expect(error.str().empty(),
                   "successful JSON CLI result must not print errors");

  output.str({});
  error.str({});
  const int json_error =
      run(adapter,
          {"molshredder", "--format", "json", "analyze", "fail"}, output,
          error);
  passed &= expect(json_error != 0, "JSON operation error must fail");
  passed &= expect(output.str().empty(),
                   "JSON operation errors must not use standard output");
  passed &= expect(error.str().find("\"status\":\"error\"") !=
                           std::string::npos &&
                       error.str().find("\"code\":\"invalid_selection\"") !=
                           std::string::npos,
                   "CLI operation errors must use the JSON error envelope");

  output.str({});
  error.str({});
  const int missing = run(
      adapter, {"molshredder", "analyze", "center", "--format", "json"},
      output, error);
  passed &= expect(missing != 0, "missing required CLI option must fail");
  passed &= expect(
      error.str().find("\"code\":\"invalid_argument\"") !=
              std::string::npos &&
          error.str().find("missing required parameter: selection") !=
              std::string::npos,
      "registry validation errors must use the selected JSON envelope");

  output.str({});
  error.str({});
  const int help = run(adapter, {"molshredder", "--help"}, output, error);
  passed &= expect(help == 0, "CLI help must succeed");
  passed &= expect(output.str().find("analyze") != std::string::npos,
                   "help must derive subcommands from registry descriptors");

  output.str({});
  error.str({});
  const int console = run(
      adapter, {"molshredder", "console"}, output, error,
      "invoke \"analyze center\" --selection \"chain A\"\nhistory\nexit\n");
  passed &= expect(console == 0, "console CLI mode must succeed");
  passed &= expect(output.str().find("center calculated") != std::string::npos,
                   "console mode must execute registered commands");
  passed &= expect(
      output.str().find(
          "1  invoke \"analyze center\" --selection \"chain A\"") !=
          std::string::npos,
      "console mode must expose canonical history");
  passed &= expect(error.str().empty(),
                   "valid console session must not print errors");

  return passed ? 0 : 1;
}
