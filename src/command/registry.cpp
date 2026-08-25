#include "molshredder/command/registry.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <set>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::command {
namespace {

using operation::Error;
using operation::ErrorCode;

bool parses_integer(std::string_view text) {
  long long value{};
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parses_unsigned_integer(std::string_view text) {
  std::uint64_t value{};
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parses_number(std::string_view text) {
  double value{};
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size() &&
         std::isfinite(value);
}

bool parses_boolean(std::string_view text) {
  return text == "true" || text == "false";
}

bool matches(ParameterType type, std::string_view value) {
  switch (type) {
    case ParameterType::text:
      return true;
    case ParameterType::integer:
      return parses_integer(value);
    case ParameterType::unsigned_integer:
      return parses_unsigned_integer(value);
    case ParameterType::number:
      return parses_number(value);
    case ParameterType::boolean:
      return parses_boolean(value);
  }
  return false;
}

std::string join_allowed_values(const std::vector<std::string>& values) {
  std::string result;
  for (const auto& value : values) {
    if (!result.empty()) {
      result += ", ";
    }
    result += value;
  }
  return result;
}

std::optional<Error> validate_descriptor(const Descriptor& descriptor) {
  if (descriptor.canonical_name.empty()) {
    return Error{ErrorCode::invalid_argument,
                 "command canonical name must not be empty", {}};
  }
  std::set<std::string, std::less<>> parameter_names;
  for (const auto& parameter : descriptor.parameters) {
    if (parameter.name.empty()) {
      return Error{ErrorCode::invalid_argument,
                   "command parameter name must not be empty", {}};
    }
    if (!parameter_names.insert(parameter.name).second) {
      return Error{ErrorCode::invalid_argument,
                   "duplicate command parameter: " + parameter.name,
                   "declare each parameter exactly once"};
    }
    if (parameter.required && parameter.default_value.has_value()) {
      return Error{ErrorCode::invalid_argument,
                   "required parameter has a default: " + parameter.name,
                   "make the parameter optional or remove its default"};
    }
    std::set<std::string, std::less<>> allowed_values;
    for (const auto& value : parameter.allowed_values) {
      if (!matches(parameter.type, value)) {
        return Error{ErrorCode::invalid_argument,
                     "allowed value has the wrong type: " + parameter.name,
                     "use values matching the parameter type"};
      }
      if (!allowed_values.insert(value).second) {
        return Error{ErrorCode::invalid_argument,
                     "duplicate allowed value for parameter: " +
                         parameter.name,
                     "declare each allowed value exactly once"};
      }
    }
    if (parameter.default_value.has_value()) {
      if (!matches(parameter.type, *parameter.default_value)) {
        return Error{ErrorCode::invalid_argument,
                     "parameter default has the wrong type: " +
                         parameter.name,
                     "use a default matching the parameter type"};
      }
      if (!parameter.allowed_values.empty() &&
          !allowed_values.contains(*parameter.default_value)) {
        return Error{ErrorCode::invalid_argument,
                     "parameter default is not an allowed value: " +
                         parameter.name,
                     "choose a default from the allowed values"};
      }
    }
  }
  return std::nullopt;
}

std::optional<Error> validate_arguments(const Descriptor& descriptor,
                                        const Arguments& arguments,
                                        bool require_required = true) {
  for (const auto& [name, value] : arguments) {
    const auto parameter = std::find_if(
        descriptor.parameters.begin(), descriptor.parameters.end(),
        [&name](const ParameterSpec& candidate) {
          return candidate.name == name;
        });
    if (parameter == descriptor.parameters.end()) {
      return Error{ErrorCode::invalid_argument,
                   "unknown parameter: " + name,
                   "use command help to list accepted parameters"};
    }
    if (!matches(parameter->type, value)) {
      return Error{ErrorCode::invalid_argument,
                   "invalid value for parameter: " + name,
                   "provide a value matching the parameter type"};
    }
    if (!parameter->allowed_values.empty() &&
        std::find(parameter->allowed_values.begin(),
                  parameter->allowed_values.end(), value) ==
            parameter->allowed_values.end()) {
      return Error{ErrorCode::invalid_argument,
                   "value is not allowed for parameter: " + name,
                   "choose one of: " +
                       join_allowed_values(parameter->allowed_values)};
    }
  }

  if (require_required) {
    for (const auto& parameter : descriptor.parameters) {
      if (parameter.required && !arguments.contains(parameter.name)) {
        return Error{ErrorCode::invalid_argument,
                     "missing required parameter: " + parameter.name,
                     "provide the required parameter"};
      }
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<operation::Error> Registry::add(Descriptor descriptor,
                                               Handler handler) {
  if (const auto error = validate_descriptor(descriptor); error.has_value()) {
    return error;
  }
  if (!handler) {
    return Error{ErrorCode::invalid_argument,
                 "command handler must not be empty", {}};
  }
  if (entries_.contains(descriptor.canonical_name)) {
    return Error{ErrorCode::invalid_argument,
                 "duplicate command: " + descriptor.canonical_name,
                 "register each canonical command exactly once"};
  }
  if (aliases_.contains(descriptor.canonical_name)) {
    return Error{ErrorCode::invalid_argument,
                 "command conflicts with alias: " + descriptor.canonical_name,
                 "use a unique canonical command name"};
  }

  const auto name = descriptor.canonical_name;
  entries_.emplace(name, Entry{std::move(descriptor), std::move(handler)});
  return std::nullopt;
}

std::optional<operation::Error> Registry::add_alias(AliasSpec alias) {
  if (alias.name.empty()) {
    return Error{ErrorCode::invalid_argument, "alias name must not be empty",
                 {}};
  }
  if (entries_.contains(alias.name)) {
    return Error{ErrorCode::invalid_argument,
                 "alias conflicts with command: " + alias.name,
                 "use a unique alias name"};
  }
  if (aliases_.contains(alias.name)) {
    return Error{ErrorCode::invalid_argument,
                 "duplicate alias: " + alias.name,
                 "register each alias exactly once"};
  }
  const auto target = entries_.find(alias.canonical_name);
  if (target == entries_.end()) {
    return Error{ErrorCode::not_found,
                 "alias target not found: " + alias.canonical_name,
                 "register the canonical command before its aliases"};
  }
  if (const auto error =
          validate_arguments(target->second.descriptor, alias.defaults, false);
      error.has_value()) {
    return error;
  }
  const auto name = alias.name;
  aliases_.emplace(name, std::move(alias));
  return std::nullopt;
}

operation::Result<Invocation> Registry::expand(
    const Invocation& invocation) const {
  if (entries_.contains(invocation.canonical_name)) {
    return operation::Result<Invocation>::success(invocation);
  }
  const auto alias = aliases_.find(invocation.canonical_name);
  if (alias == aliases_.end()) {
    return operation::Result<Invocation>::failure(
        Error{ErrorCode::not_found,
              "command or alias not found: " + invocation.canonical_name,
              "use help to list registered commands and aliases"});
  }
  Arguments arguments = alias->second.defaults;
  for (const auto& [name, value] : invocation.arguments) {
    arguments.insert_or_assign(name, value);
  }
  return operation::Result<Invocation>::success(
      Invocation{alias->second.canonical_name, std::move(arguments)});
}

operation::Result<Invocation> Registry::normalize(
    const Invocation& invocation) const {
  auto expanded = expand(invocation);
  if (!expanded.has_value()) {
    return operation::Result<Invocation>::failure(expanded.error());
  }
  const auto entry = entries_.find(expanded.value().canonical_name);
  if (entry == entries_.end()) {
    return operation::Result<Invocation>::failure(
        Error{ErrorCode::internal, "expanded command target is not registered",
              {}});
  }
  auto normalized = expanded.value();
  for (const auto& parameter : entry->second.descriptor.parameters) {
    if (!normalized.arguments.contains(parameter.name) &&
        parameter.default_value.has_value()) {
      normalized.arguments.emplace(parameter.name, *parameter.default_value);
    }
  }
  if (const auto failure =
          validate_arguments(entry->second.descriptor, normalized.arguments);
      failure.has_value()) {
    return operation::Result<Invocation>::failure(*failure);
  }
  return operation::Result<Invocation>::success(std::move(normalized));
}

operation::Result<Response> Registry::invoke(
    std::string_view canonical_name, const Arguments& arguments,
    operation::TaskContext& context) const {
  const auto normalized = normalize(
      Invocation{std::string{canonical_name}, arguments});
  if (!normalized.has_value()) {
    return operation::Result<Response>::failure(
        normalized.error());
  }
  const auto entry = entries_.find(normalized.value().canonical_name);
  if (context.cancellation.is_cancelled()) {
    return operation::Result<Response>::failure(
        Error{ErrorCode::cancelled, "command cancelled", {}});
  }
  return entry->second.handler(normalized.value().arguments, context);
}

operation::Result<Response> Registry::invoke(
    const Invocation& invocation, operation::TaskContext& context) const {
  return invoke(invocation.canonical_name, invocation.arguments, context);
}

std::vector<Descriptor> Registry::descriptors() const {
  std::vector<Descriptor> result;
  result.reserve(entries_.size());
  for (const auto& [name, entry] : entries_) {
    static_cast<void>(name);
    result.push_back(entry.descriptor);
  }
  return result;
}

std::vector<AliasSpec> Registry::aliases() const {
  std::vector<AliasSpec> result;
  result.reserve(aliases_.size());
  for (const auto& [name, alias] : aliases_) {
    static_cast<void>(name);
    result.push_back(alias);
  }
  return result;
}

}  // namespace molshredder::command
