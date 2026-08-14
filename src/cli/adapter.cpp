#include "molshredder/cli/adapter.hpp"

#include <algorithm>
#include <istream>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <CLI/CLI.hpp>

#include "molshredder/application/dispatcher.hpp"
#include "molshredder/cli/console.hpp"
#include "molshredder/command/result.hpp"
#include "molshredder/command/serialization.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::cli {
namespace {

std::vector<std::string> split_name(std::string_view name) {
  std::vector<std::string> result;
  std::istringstream stream{std::string{name}};
  for (std::string part; stream >> part;) {
    result.push_back(std::move(part));
  }
  return result;
}

std::string parameter_description(const command::ParameterSpec& parameter) {
  std::string result = parameter.required ? "Required" : "Optional";
  if (parameter.default_value.has_value()) {
    result += "; default: " + *parameter.default_value;
  }
  if (!parameter.allowed_values.empty()) {
    result += "; choices: ";
    for (std::size_t index = 0; index < parameter.allowed_values.size();
         ++index) {
      if (index != 0) {
        result += ", ";
      }
      result += parameter.allowed_values[index];
    }
  }
  return result;
}

struct RuntimeCommand {
  CLI::App* leaf{};
  command::Invocation invocation;
  std::string output_format_name{"text"};
};

}  // namespace

int Adapter::run(int argc, char** argv, std::istream& input,
                 std::ostream& output,
                 std::ostream& error) const {
  CLI::App app{"High-performance molecular visualization and analysis",
               "molshredder"};
  app.require_subcommand(0, 1);
  std::string global_output_format_name{"text"};
  app.add_option("--format", global_output_format_name,
                 "Result format: text, json, or csv");
  auto* console_command = app.add_subcommand(
      "console", "Start the interactive command console");

  std::map<std::string, CLI::App*, std::less<>> nodes;
  std::vector<std::unique_ptr<RuntimeCommand>> commands;

  const auto add_runtime_command =
      [&app, &nodes, &commands](std::string_view command_name,
                               std::string description,
                               const std::vector<command::ParameterSpec>&
                                   parameters) {
    const auto parts = split_name(command_name);
    CLI::App* parent = &app;
    std::string path;
    for (const auto& part : parts) {
      if (!path.empty()) {
        path += ' ';
      }
      path += part;
      const auto existing = nodes.find(path);
      if (existing != nodes.end()) {
        parent = existing->second;
        continue;
      }
      parent = parent->add_subcommand(part);
      nodes.emplace(path, parent);
    }
    if (parent == &app) {
      return;
    }
    parent->description(std::move(description));

    auto runtime = std::make_unique<RuntimeCommand>();
    runtime->leaf = parent;
    runtime->invocation.canonical_name = command_name;
    for (const auto& parameter : parameters) {
      auto [argument, inserted] =
          runtime->invocation.arguments.emplace(parameter.name, std::string{});
      static_cast<void>(inserted);
      parent->add_option("--" + parameter.name, argument->second,
                         parameter_description(parameter));
    }
    parent
        ->add_option("--format", runtime->output_format_name,
                     "Result format: text, json, or csv");
    commands.push_back(std::move(runtime));
  };

  const auto descriptors = registry_.descriptors();
  for (const auto& descriptor : descriptors) {
    add_runtime_command(descriptor.canonical_name, descriptor.summary,
                        descriptor.parameters);
  }
  for (const auto& alias : registry_.aliases()) {
    const auto target = std::find_if(
        descriptors.begin(), descriptors.end(), [&alias](const auto& candidate) {
          return candidate.canonical_name == alias.canonical_name;
        });
    if (target != descriptors.end()) {
      add_runtime_command(alias.name, "Alias for " + alias.canonical_name,
                          target->parameters);
    }
  }

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& parse_error) {
    return app.exit(parse_error, output, error);
  }

  if (console_command->parsed()) {
    Console console{registry_};
    return console.run(input, output, error);
  }

  const application::Dispatcher dispatcher{registry_};

  for (const auto& runtime : commands) {
    if (!runtime->leaf->parsed()) {
      continue;
    }
    command::Arguments supplied;
    for (const auto& [name, value] : runtime->invocation.arguments) {
      const auto* option = runtime->leaf->get_option_no_throw("--" + name);
      if (option != nullptr && option->count() > 0) {
        supplied.emplace(name, value);
      }
    }
    command::Invocation invocation{runtime->invocation.canonical_name,
                                   std::move(supplied)};
    const auto* local_output_format =
        runtime->leaf->get_option_no_throw("--format");
    const std::string& selected_output_format =
        local_output_format != nullptr && local_output_format->count() > 0
            ? runtime->output_format_name
            : global_output_format_name;
    const auto output_format =
        command::parse_output_format(selected_output_format);
    if (!output_format.has_value()) {
      error << "error[" << operation::to_string(output_format.error().code)
            << "]: " << output_format.error().message << '\n';
      return 2;
    }
    operation::TaskContext context;
    const auto outcome = dispatcher.dispatch(invocation, context);
    const auto rendered =
        command::render(outcome.envelope, output_format.value());
    if (!rendered.has_value()) {
      error << "error[" << operation::to_string(rendered.error().code)
            << "]: " << rendered.error().message << '\n';
      if (!rendered.error().suggestion.empty()) {
        error << "hint: " << rendered.error().suggestion << '\n';
      }
      return 2;
    }
    (outcome.succeeded() ? output : error) << rendered.value();
    return outcome.succeeded() ? 0 : 2;
  }

  output << app.help();
  return 0;
}

}  // namespace molshredder::cli
