#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "molshredder/cli/console.hpp"
#include "molshredder/command/registry.hpp"
#include "molshredder/operation/result.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  using molshredder::command::Arguments;
  using molshredder::command::AliasSpec;
  using molshredder::command::Descriptor;
  using molshredder::command::ParameterSpec;
  using molshredder::command::ParameterType;
  using molshredder::command::Registry;
  using molshredder::command::Response;
  using molshredder::operation::Result;
  using molshredder::operation::TaskContext;

  bool passed = true;
  Registry registry;
  const auto registration = registry.add(
      Descriptor{"analyze center",
                 "Calculate a geometric center",
                 {ParameterSpec{"selection", ParameterType::text, true},
                  ParameterSpec{"precision", ParameterType::integer, false}},
                 {}},
      [](const Arguments& arguments, TaskContext&) {
        return Result<Response>::success(
            {"center calculated", {{"selection", arguments.at("selection")}}});
      });
  passed &= expect(!registration.has_value(), "command must register");
  const auto alias_registration = registry.add_alias(
      AliasSpec{"center", "analyze center", {{"precision", "4"}}});
  passed &= expect(!alias_registration.has_value(), "alias must register");

  molshredder::cli::Console console{registry};
  const auto command_candidates = console.complete("analyze");
  passed &= expect(command_candidates.size() == 1 &&
                       command_candidates.front() == "analyze center",
                   "completion must expose registry command names");
  const auto option_candidates = console.complete("analyze center --s");
  passed &= expect(option_candidates.size() == 1 &&
                       option_candidates.front() == "--selection",
                   "completion must expose descriptor parameter names");
  const auto alias_candidates = console.complete("cen");
  passed &= expect(alias_candidates.size() == 1 &&
                       alias_candidates.front() == "center",
                   "completion must expose registered aliases");

  std::istringstream input{
      "help analyze\n"
      "format json\n"
      "not canonical\n"
      "invoke \"center\" --selection \"protein\"\n"
      "history\n"
      "exit\n"};
  std::ostringstream output;
  std::ostringstream error;
  passed &= expect(console.run(input, output, error) == 0,
                   "console session must exit successfully");
  passed &= expect(output.str().find("analyze center - Calculate") !=
                       std::string::npos,
                   "help must derive content from registry descriptors");
  passed &= expect(output.str().find("\"status\":\"ok\"") !=
                           std::string::npos &&
                       output.str().find("\"selection\":\"protein\"") !=
                           std::string::npos,
                   "console must render successful commands as typed JSON");
  passed &= expect(output.str().find(
                       "1  invoke \"analyze center\" --precision \"4\" --selection \"protein\"") !=
                       std::string::npos,
                   "history must contain expanded canonical invocations");
  passed &= expect(error.str().find("\"status\":\"error\"") !=
                           std::string::npos &&
                       error.str().find("\"code\":\"invalid_argument\"") !=
                           std::string::npos,
                   "invalid console input must use the JSON error envelope");
  passed &= expect(console.history().size() == 1,
                   "failed invocations must not enter history");
  passed &= expect(console.history().front().provenance.sequence == 1,
                   "history provenance must assign a stable sequence");

  return passed ? 0 : 1;
}
