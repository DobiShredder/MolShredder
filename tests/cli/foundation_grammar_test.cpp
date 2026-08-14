#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/cli/adapter.hpp"
#include "molshredder/command/foundation_grammar.hpp"
#include "molshredder/operation/result.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

int run(molshredder::cli::Adapter& adapter,
        std::vector<std::string> arguments, std::ostringstream& output,
        std::ostringstream& error) {
  std::vector<char*> argv;
  argv.reserve(arguments.size());
  for (auto& argument : arguments) {
    argv.push_back(argument.data());
  }
  std::istringstream input;
  return adapter.run(static_cast<int>(argv.size()), argv.data(), input, output,
                     error);
}

}  // namespace

int main() {
  using molshredder::command::Arguments;
  using molshredder::command::Registry;
  using molshredder::command::Response;
  using molshredder::operation::Result;
  using molshredder::operation::TaskContext;

  bool passed = true;
  Registry registry;
  for (auto descriptor :
       molshredder::command::foundation_command_descriptors()) {
    const auto command_name = descriptor.canonical_name;
    const auto failure = registry.add(
        std::move(descriptor),
        [command_name](const Arguments& arguments, TaskContext&) {
          molshredder::command::Value::Object fields;
          for (const auto& [name, value] : arguments) {
            fields.emplace(name, value);
          }
          return Result<Response>::success(
              {command_name + " accepted", std::move(fields)});
        });
    passed &= expect(!failure.has_value(), "foundation command must register");
  }
  for (auto alias : molshredder::command::foundation_command_aliases()) {
    passed &= expect(!registry.add_alias(std::move(alias)).has_value(),
                     "foundation alias must register");
  }

  molshredder::cli::Adapter adapter{registry};
  std::ostringstream output;
  std::ostringstream error;

  const int help = run(adapter, {"molshredder", "analyze", "center", "--help"},
                       output, error);
  passed &= expect(help == 0, "foundation command help must succeed");
  passed &= expect(output.str().find("default: centroid") != std::string::npos &&
                       output.str().find("choices: centroid, com") !=
                           std::string::npos,
                   "CLI help must derive defaults and choices from the registry");

  output.str({});
  error.str({});
  const int load = run(adapter,
                       {"molshredder", "open", "--path", "model.pdb",
                        "--file-format", "pdb", "--format", "json"},
                       output, error);
  passed &= expect(load == 0 &&
                       output.str().find("\"command\":\"invoke \\\"load") !=
                           std::string::npos,
                   "load alias must execute as a canonical JSON command");

  output.str({});
  error.str({});
  const int select = run(
      adapter,
      {"molshredder", "select", "--name", "protein", "--expression",
       "polymer.protein", "--format", "json"},
      output, error);
  passed &= expect(select == 0 &&
                       output.str().find("\"update\":\"false\"") !=
                           std::string::npos,
                   "select must apply its update default canonically");

  output.str({});
  error.str({});
  const int show = run(adapter,
                       {"molshredder", "show", "--representation", "sticks",
                        "--format", "json"},
                       output, error);
  passed &= expect(show == 0 &&
                       output.str().find("\"selection\":\"all\"") !=
                           std::string::npos,
                   "show must apply its selection default canonically");

  output.str({});
  error.str({});
  const int center = run(adapter,
                         {"molshredder", "com", "--selection", "chain A",
                          "--precision", "4", "--format", "json"},
                         output, error);
  passed &= expect(center == 0 &&
                       output.str().find("\"mode\":\"com\"") !=
                           std::string::npos &&
                       output.str().find("\"unit\":\"angstrom\"") !=
                           std::string::npos,
                   "COM shorthand must normalize alias and command defaults");

  output.str({});
  error.str({});
  const int distance = run(
      adapter,
      {"molshredder", "dist", "--from", "id 1", "--to", "id 2",
       "--format", "json"},
      output, error);
  passed &= expect(distance == 0 &&
                       output.str().find("\"pbc\":\"raw\"") !=
                           std::string::npos,
                   "distance shorthand must apply reproducible defaults");

  output.str({});
  error.str({});
  const int invalid = run(
      adapter,
      {"molshredder", "show", "--representation", "surface", "--format",
       "json"},
      output, error);
  passed &= expect(invalid != 0 && output.str().empty(),
                   "invalid grammar value must fail on stderr");
  passed &= expect(error.str().find("\"code\":\"invalid_argument\"") !=
                           std::string::npos &&
                       error.str().find("lines, sticks, spheres, ribbon, cartoon") !=
                           std::string::npos,
                   "invalid grammar value must return a repairable JSON error");

  return passed ? 0 : 1;
}
