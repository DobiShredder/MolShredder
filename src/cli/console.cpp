#include "molshredder/cli/console.hpp"

#include <algorithm>
#include <cctype>
#include <istream>
#include <ostream>
#include <string>

#include "molshredder/application/dispatcher.hpp"
#include "molshredder/command/serialization.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::cli {
namespace {

std::string_view trim(std::string_view text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return text;
}

void print_error(const operation::Error& failure, std::ostream& error) {
  error << "error[" << operation::to_string(failure.code)
        << "]: " << failure.message << '\n';
  if (!failure.suggestion.empty()) {
    error << "hint: " << failure.suggestion << '\n';
  }
}

std::string_view format_name(operation::OutputFormat format) {
  switch (format) {
    case operation::OutputFormat::text: return "text";
    case operation::OutputFormat::json: return "json";
    case operation::OutputFormat::csv: return "csv";
  }
  return "unknown";
}

bool print_envelope(const command::ResultEnvelope& envelope,
                    operation::OutputFormat format, std::ostream& output,
                    std::ostream& error) {
  const auto rendered = command::render(envelope, format);
  if (!rendered.has_value()) {
    print_error(rendered.error(), error);
    return false;
  }
  (envelope.succeeded() ? output : error) << rendered.value();
  return envelope.succeeded();
}

}  // namespace

int Console::run(std::istream& input, std::ostream& output,
                 std::ostream& error) {
  output << "MolShredder interactive console\n"
            "Enter help, history, format, or a canonical invoke command; exit "
            "to quit.\n";
  const application::Dispatcher dispatcher{registry_};

  for (std::string line; output << "molshredder> " &&
                         std::getline(input, line);) {
    const auto command_text = trim(line);
    if (command_text.empty()) {
      continue;
    }
    if (command_text == "exit" || command_text == "quit") {
      return 0;
    }
    if (command_text == "history") {
      for (std::size_t index = 0; index < history_.size(); ++index) {
        output << index + 1 << "  "
               << history_[index].provenance.canonical_command << '\n';
      }
      continue;
    }
    if (command_text == "format") {
      output << "Result format: " << format_name(output_format_) << '\n';
      continue;
    }
    if (command_text.starts_with("format ")) {
      const auto parsed_format =
          command::parse_output_format(trim(command_text.substr(7)));
      if (!parsed_format.has_value()) {
        const auto failure = operation::Result<command::Response>::failure(
            parsed_format.error());
        static_cast<void>(print_envelope(
            command::make_envelope(std::string{command_text}, failure),
            output_format_, output, error));
      } else {
        output_format_ = parsed_format.value();
        output << "Result format set to " << format_name(output_format_)
               << '\n';
      }
      continue;
    }
    if (command_text == "help") {
      print_help({}, output);
      continue;
    }
    if (command_text.starts_with("help ")) {
      print_help(trim(command_text.substr(5)), output);
      continue;
    }

    const auto parsed = command::parse_canonical(command_text);
    if (!parsed.has_value()) {
      const auto failure =
          operation::Result<command::Response>::failure(parsed.error());
      static_cast<void>(print_envelope(
          command::make_envelope(std::string{command_text}, failure),
          output_format_, output, error));
      continue;
    }
    operation::TaskContext context;
    const auto outcome = dispatcher.dispatch(parsed.value(), context);
    if (!print_envelope(outcome.envelope, output_format_, output, error)) {
      continue;
    }
    if (!outcome.canonical_invocation.has_value()) {
      print_error(operation::Error{operation::ErrorCode::internal,
                                   "successful dispatch has no invocation", {}},
                  error);
      continue;
    }
    history_.push_back(command::make_record(
        *outcome.canonical_invocation, command::InvocationSource::cli,
        next_sequence_++));
  }
  return 0;
}

std::vector<std::string> Console::complete(std::string_view prefix) const {
  const auto query = trim(prefix);
  std::vector<std::string> candidates;
  for (const auto& descriptor : registry_.descriptors()) {
    if (descriptor.canonical_name.starts_with(query)) {
      candidates.push_back(descriptor.canonical_name);
    }
    const std::string option_prefix = descriptor.canonical_name + " ";
    if (query == descriptor.canonical_name || query.starts_with(option_prefix)) {
      const auto option_query = query == descriptor.canonical_name
                                    ? std::string_view{}
                                    : trim(query.substr(option_prefix.size()));
      for (const auto& parameter : descriptor.parameters) {
        const std::string option = "--" + parameter.name;
        if (option.starts_with(option_query)) {
          candidates.push_back(option);
        }
      }
    }
  }
  const auto descriptors = registry_.descriptors();
  for (const auto& alias : registry_.aliases()) {
    if (alias.name.starts_with(query)) {
      candidates.push_back(alias.name);
    }
    const std::string option_prefix = alias.name + " ";
    if (query != alias.name && !query.starts_with(option_prefix)) {
      continue;
    }
    const auto target = std::find_if(
        descriptors.begin(), descriptors.end(), [&alias](const auto& candidate) {
          return candidate.canonical_name == alias.canonical_name;
        });
    if (target == descriptors.end()) {
      continue;
    }
    const auto option_query = query == alias.name
                                  ? std::string_view{}
                                  : trim(query.substr(option_prefix.size()));
    for (const auto& parameter : target->parameters) {
      const std::string option = "--" + parameter.name;
      if (option.starts_with(option_query)) {
        candidates.push_back(option);
      }
    }
  }
  std::sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()),
                   candidates.end());
  return candidates;
}

void Console::print_help(std::string_view prefix, std::ostream& output) const {
  bool matched = false;
  for (const auto& descriptor : registry_.descriptors()) {
    if (!descriptor.canonical_name.starts_with(prefix)) {
      continue;
    }
    matched = true;
    output << descriptor.canonical_name << " - " << descriptor.summary << '\n';
    for (const auto& parameter : descriptor.parameters) {
      output << "  --" << parameter.name;
      if (parameter.required) {
        output << " (required)";
      } else if (parameter.default_value.has_value()) {
        output << " (default: " << *parameter.default_value << ')';
      }
      if (!parameter.allowed_values.empty()) {
        output << " [";
        for (std::size_t index = 0; index < parameter.allowed_values.size();
             ++index) {
          if (index != 0) {
            output << '|';
          }
          output << parameter.allowed_values[index];
        }
        output << ']';
      }
      output << '\n';
    }
  }
  for (const auto& alias : registry_.aliases()) {
    if (!alias.name.starts_with(prefix)) {
      continue;
    }
    matched = true;
    output << alias.name << " -> " << alias.canonical_name << " (alias)\n";
  }
  if (!matched) {
    output << "No commands match: " << prefix << '\n';
  }
}

}  // namespace molshredder::cli
