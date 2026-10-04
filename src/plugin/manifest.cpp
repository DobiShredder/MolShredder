#include "molshredder/plugin/manifest.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <regex>
#include <utility>

#include "molshredder/command/json.hpp"

namespace molshredder::plugin {
namespace {

using command::Value;
using operation::Error;
using operation::ErrorCode;
using operation::Result;

Error invalid(std::string message, std::string suggestion = {}) {
  return {ErrorCode::invalid_argument, std::move(message), std::move(suggestion)};
}

template <typename T>
Result<T> fail(std::string message) {
  return Result<T>::failure(invalid("plugin manifest: " + std::move(message)));
}

const Value* find(const Value::Object& object, std::string_view key) {
  const auto found = object.find(key);
  return found == object.end() ? nullptr : &found->second;
}

const std::string* as_string(const Value* value) {
  return value == nullptr ? nullptr : std::get_if<std::string>(&value->data);
}

std::optional<std::uint64_t> as_unsigned(const Value* value) {
  if (value == nullptr) return std::nullopt;
  if (const auto* u = std::get_if<std::uint64_t>(&value->data)) return *u;
  if (const auto* i = std::get_if<std::int64_t>(&value->data); i != nullptr && *i >= 0) {
    return static_cast<std::uint64_t>(*i);
  }
  return std::nullopt;
}

bool matches(const std::string& text, const char* pattern) {
  return std::regex_match(text, std::regex{pattern});
}

const std::set<std::string, std::less<>>& host_capability_vocabulary() {
  static const std::set<std::string, std::less<>> names{
      "result.table", "progress", "cancellation", "host.selection.atoms"};
  return names;
}

Result<std::vector<std::string>> string_list(const Value::Object& object, std::string_view key,
                                             std::size_t limit) {
  std::vector<std::string> out;
  const auto* value = find(object, key);
  if (value == nullptr) return Result<std::vector<std::string>>::success(std::move(out));
  const auto* array = std::get_if<Value::Array>(&value->data);
  if (array == nullptr || array->size() > limit) {
    return fail<std::vector<std::string>>(std::string{key} + " must be an array of at most " +
                                          std::to_string(limit) + " strings");
  }
  for (const auto& element : *array) {
    const auto* text = as_string(&element);
    if (text == nullptr || text->empty() || text->size() > 256U) {
      return fail<std::vector<std::string>>(std::string{key} + " holds a non-string or empty entry");
    }
    out.push_back(*text);
  }
  return Result<std::vector<std::string>>::success(std::move(out));
}

bool relative_inside(const std::string& path) {
  const std::filesystem::path candidate{path};
  if (candidate.empty() || candidate.is_absolute() || candidate.has_root_name()) return false;
  for (const auto& part : candidate) {
    if (part == "..") return false;
  }
  return true;
}

Result<ParameterDeclaration> parse_parameter(const Value& value) {
  const auto* object = std::get_if<Value::Object>(&value.data);
  if (object == nullptr) return fail<ParameterDeclaration>("a parameter must be an object");
  ParameterDeclaration parameter;
  const auto* name = as_string(find(*object, "name"));
  const auto* type = as_string(find(*object, "type"));
  if (name == nullptr || !matches(*name, "[a-z][a-z0-9-]{0,31}")) {
    return fail<ParameterDeclaration>("parameter names are lowercase words like 'cutoff' or 'result-name'");
  }
  parameter.name = *name;
  if (type == nullptr) return fail<ParameterDeclaration>("parameter '" + *name + "' needs a type");
  if (*type == "text") parameter.type = command::ParameterType::text;
  else if (*type == "number") parameter.type = command::ParameterType::number;
  else if (*type == "integer") parameter.type = command::ParameterType::integer;
  else if (*type == "unsigned_integer") parameter.type = command::ParameterType::unsigned_integer;
  else if (*type == "boolean") parameter.type = command::ParameterType::boolean;
  else return fail<ParameterDeclaration>("parameter '" + *name + "' has unknown type '" + *type + "'");
  if (const auto* required = find(*object, "required")) {
    const auto* flag = std::get_if<bool>(&required->data);
    if (flag == nullptr) return fail<ParameterDeclaration>("'required' must be true or false");
    parameter.required = *flag;
  }
  if (const auto* def = find(*object, "default"); def != nullptr && !std::holds_alternative<std::nullptr_t>(def->data)) {
    const auto* text = as_string(def);
    if (text == nullptr || text->size() > 1024U) {
      return fail<ParameterDeclaration>("a default value must be a string");
    }
    parameter.default_value = *text;
  }
  if (parameter.required && parameter.default_value.has_value()) {
    return fail<ParameterDeclaration>("parameter '" + *name + "' cannot be required and have a default");
  }
  auto choices = string_list(*object, "choices", 64U);
  if (!choices.has_value()) return Result<ParameterDeclaration>::failure(choices.error());
  parameter.choices = std::move(choices.value());
  if (parameter.default_value.has_value() && !parameter.choices.empty() &&
      std::find(parameter.choices.begin(), parameter.choices.end(), *parameter.default_value) ==
          parameter.choices.end()) {
    return fail<ParameterDeclaration>("the default of '" + *name + "' is not one of its choices");
  }
  return Result<ParameterDeclaration>::success(std::move(parameter));
}

Result<OperationDeclaration> parse_operation(const Value& value) {
  const auto* object = std::get_if<Value::Object>(&value.data);
  if (object == nullptr) return fail<OperationDeclaration>("an operation must be an object");
  OperationDeclaration operation;
  const auto* name = as_string(find(*object, "name"));
  if (name == nullptr || !matches(*name, "[a-z][a-z0-9-]{0,47}")) {
    return fail<OperationDeclaration>("operation names are lowercase words like 'radius-of-gyration'");
  }
  operation.name = *name;
  if (const auto* summary = as_string(find(*object, "summary"))) {
    if (summary->size() > 512U) return fail<OperationDeclaration>("operation summary is too long");
    operation.summary = *summary;
  }
  if (const auto* undo = as_string(find(*object, "undo")); undo != nullptr && *undo != "not_applicable") {
    return fail<OperationDeclaration>("SDK v1 operations are read-only; 'undo' must be 'not_applicable'");
  }
  if (const auto* parameters = find(*object, "parameters")) {
    const auto* array = std::get_if<Value::Array>(&parameters->data);
    if (array == nullptr || array->size() > 32U) {
      return fail<OperationDeclaration>("parameters must be an array of at most 32 entries");
    }
    std::set<std::string> seen;
    for (const auto& element : *array) {
      auto parameter = parse_parameter(element);
      if (!parameter.has_value()) return Result<OperationDeclaration>::failure(parameter.error());
      if (!seen.insert(parameter.value().name).second) {
        return fail<OperationDeclaration>("parameter '" + parameter.value().name + "' is declared twice");
      }
      operation.parameters.push_back(std::move(parameter.value()));
    }
  }
  return Result<OperationDeclaration>::success(std::move(operation));
}

bool semver(const std::string& text) {
  return matches(text, "(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)");
}

}  // namespace

bool is_gpl_compatible_license(std::string_view license) {
  static const std::set<std::string, std::less<>> accepted{
      "GPL-3.0-or-later", "GPL-3.0-only", "AGPL-3.0-or-later", "LGPL-3.0-or-later",
      "LGPL-2.1-or-later", "LGPL-3.0-only", "MIT", "BSD-2-Clause", "BSD-3-Clause",
      "Apache-2.0", "ISC", "MPL-2.0", "Zlib", "BSL-1.0", "CC0-1.0", "Unlicense"};
  return accepted.count(license) != 0U;
}

HostInfo default_host_info() {
  HostInfo info;
  info.capabilities = host_capability_vocabulary();
  return info;
}

std::string canonical_operation_name(const PluginManifest& manifest,
                                     const OperationDeclaration& operation) {
  return "plugin " + manifest.name_space + " " + operation.name;
}

Result<PluginManifest> parse_manifest(std::string_view json, const std::filesystem::path& directory) {
  auto parsed = command::parse_json(json, command::JsonLimits{256U * 1024U, 16U});
  if (!parsed.has_value()) return Result<PluginManifest>::failure(parsed.error());
  const auto* root = std::get_if<Value::Object>(&parsed.value().data);
  if (root == nullptr) return fail<PluginManifest>("the top level must be an object");
  PluginManifest manifest;
  manifest.directory = directory;
  const auto schema = as_unsigned(find(*root, "schema_version"));
  if (!schema.has_value() || *schema != kManifestSchemaVersion) {
    return fail<PluginManifest>("schema_version must be " + std::to_string(kManifestSchemaVersion));
  }
  const auto text = [&](std::string_view key) { return as_string(find(*root, key)); };
  const auto* id = text("id");
  if (id == nullptr || !matches(*id, "[a-z][a-z0-9_.-]{2,63}")) {
    return fail<PluginManifest>("id must be 3-64 lowercase characters (letters, digits, '.', '_', '-')");
  }
  manifest.id = *id;
  const auto* name_space = text("namespace");
  static const std::set<std::string, std::less<>> reserved{"list", "approve", "revoke", "invoke", "status", "check"};
  if (name_space == nullptr || !matches(*name_space, "[a-z][a-z0-9-]{1,31}") ||
      reserved.count(*name_space) != 0U) {
    return fail<PluginManifest>("namespace must be a 2-32 character lowercase word and not a reserved name");
  }
  manifest.name_space = *name_space;
  manifest.display_name = text("name") != nullptr ? *text("name") : manifest.id;
  if (manifest.display_name.size() > 128U) return fail<PluginManifest>("name is too long");
  const auto* version = text("version");
  if (version == nullptr || !semver(*version)) return fail<PluginManifest>("version must look like 1.2.3");
  manifest.version = *version;
  const auto* license = text("license");
  if (license == nullptr || !matches(*license, "[A-Za-z0-9.+() -]{1,64}")) {
    return fail<PluginManifest>("license must be an SPDX-style identifier");
  }
  manifest.license = *license;

  const auto* sdk = find(*root, "sdk") != nullptr ? std::get_if<Value::Object>(&find(*root, "sdk")->data) : nullptr;
  if (sdk == nullptr) return fail<PluginManifest>("sdk must be an object with abi_major and abi_minor");
  const auto major = as_unsigned(find(*sdk, "abi_major"));
  const auto minor = as_unsigned(find(*sdk, "abi_minor"));
  if (!major.has_value() || !minor.has_value() || *major > 1000U || *minor > 1000U) {
    return fail<PluginManifest>("sdk.abi_major and sdk.abi_minor must be small non-negative integers");
  }
  manifest.sdk_major = static_cast<unsigned>(*major);
  manifest.sdk_minor = static_cast<unsigned>(*minor);

  const auto* execution = find(*root, "execution") != nullptr ? std::get_if<Value::Object>(&find(*root, "execution")->data) : nullptr;
  if (execution == nullptr) return fail<PluginManifest>("execution must be an object");
  const auto* mode = as_string(find(*execution, "mode"));
  if (mode == nullptr || (*mode != "worker" && *mode != "in_process")) {
    return fail<PluginManifest>("execution.mode must be 'worker' or 'in_process'");
  }
  if (*mode == "worker") {
    manifest.mode = ExecutionMode::worker;
    auto command_line = string_list(*execution, "command", 32U);
    if (!command_line.has_value()) return Result<PluginManifest>::failure(command_line.error());
    if (command_line.value().empty() || !relative_inside(command_line.value().front())) {
      return fail<PluginManifest>("execution.command[0] must be a relative path inside the plugin directory");
    }
    manifest.worker_command = std::move(command_line.value());
  } else {
    manifest.mode = ExecutionMode::in_process;
    const auto* library = as_string(find(*execution, "library"));
    if (library == nullptr || !relative_inside(*library)) {
      return fail<PluginManifest>("execution.library must be a relative path inside the plugin directory");
    }
    manifest.library = *library;
    if (!is_gpl_compatible_license(manifest.license)) {
      return fail<PluginManifest>(
          "in-process plugins must declare a GPL-3.0-or-later compatible license; use worker mode for '" +
          manifest.license + "'");
    }
  }

  auto pinned = string_list(*execution, "files", 16U);
  if (!pinned.has_value()) return Result<PluginManifest>::failure(pinned.error());
  for (const auto& file : pinned.value()) {
    if (!relative_inside(file)) {
      return fail<PluginManifest>("execution.files entries must be relative paths inside the plugin directory");
    }
  }
  manifest.pinned_files = std::move(pinned.value());

  auto required = string_list(*root, "required_capabilities", 32U);
  auto optional = string_list(*root, "optional_capabilities", 32U);
  if (!required.has_value()) return Result<PluginManifest>::failure(required.error());
  if (!optional.has_value()) return Result<PluginManifest>::failure(optional.error());
  manifest.required_capabilities = std::move(required.value());
  manifest.optional_capabilities = std::move(optional.value());

  const auto* operations = find(*root, "operations");
  const auto* array = operations != nullptr ? std::get_if<Value::Array>(&operations->data) : nullptr;
  if (array == nullptr || array->empty() || array->size() > 64U) {
    return fail<PluginManifest>("operations must be an array of 1 to 64 entries");
  }
  std::set<std::string> names;
  for (const auto& element : *array) {
    auto operation = parse_operation(element);
    if (!operation.has_value()) return Result<PluginManifest>::failure(operation.error());
    if (!names.insert(operation.value().name).second) {
      return fail<PluginManifest>("operation '" + operation.value().name + "' is declared twice");
    }
    manifest.operations.push_back(std::move(operation.value()));
  }

  if (const auto* examples = find(*root, "examples")) {
    const auto* list = std::get_if<Value::Array>(&examples->data);
    if (list == nullptr || list->size() > 64U) return fail<PluginManifest>("examples must be an array");
    for (const auto& element : *list) {
      const auto* object = std::get_if<Value::Object>(&element.data);
      const auto* operation_name = object != nullptr ? as_string(find(*object, "operation")) : nullptr;
      if (operation_name == nullptr || names.count(*operation_name) == 0U) {
        return fail<PluginManifest>("an example names an undeclared operation");
      }
      ExampleInvocation example;
      example.operation = *operation_name;
      if (const auto* args = find(*object, "arguments")) {
        const auto* arguments = std::get_if<Value::Object>(&args->data);
        if (arguments == nullptr) return fail<PluginManifest>("example arguments must be an object");
        for (const auto& [key, value] : *arguments) {
          const auto* argument = as_string(&value);
          if (argument == nullptr) return fail<PluginManifest>("example argument values must be strings");
          example.arguments.emplace(key, *argument);
        }
      }
      manifest.examples.push_back(std::move(example));
    }
  }
  return Result<PluginManifest>::success(std::move(manifest));
}

Result<PluginManifest> load_manifest_file(const std::filesystem::path& manifest_path) {
  std::error_code error;
  const auto canonical = std::filesystem::canonical(manifest_path, error);
  if (error || !std::filesystem::is_regular_file(canonical)) {
    return fail<PluginManifest>("not a regular file: " + manifest_path.string());
  }
  const auto size = std::filesystem::file_size(canonical, error);
  if (error || size > 256U * 1024U) return fail<PluginManifest>("file is missing or larger than 256 KiB");
  std::ifstream input{canonical, std::ios::binary};
  const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  return parse_manifest(text, canonical.parent_path());
}

Result<std::filesystem::path> resolve_entry_path(const PluginManifest& manifest) {
  const auto& relative = manifest.mode == ExecutionMode::worker
                             ? manifest.worker_command.front()
                             : manifest.library;
  std::error_code error;
  const auto base = std::filesystem::canonical(manifest.directory, error);
  const auto resolved = std::filesystem::canonical(base / relative, error);
  if (error || !std::filesystem::is_regular_file(resolved)) {
    return Result<std::filesystem::path>::failure(
        invalid("plugin entry does not exist: " + (manifest.directory / relative).string()));
  }
  const auto relation = std::filesystem::relative(resolved, base, error);
  if (error || relation.empty() || *relation.begin() == "..") {
    return Result<std::filesystem::path>::failure(
        invalid("plugin entry resolves outside the plugin directory: " + resolved.string()));
  }
  return Result<std::filesystem::path>::success(resolved);
}

Result<Negotiation> negotiate(const PluginManifest& manifest, const HostInfo& host) {
  if (manifest.sdk_major != host.abi_major) {
    return Result<Negotiation>::failure(Error{
        ErrorCode::unsupported,
        "plugin '" + manifest.id + "' targets SDK major version " + std::to_string(manifest.sdk_major) +
            " but this host provides " + std::to_string(host.abi_major),
        "rebuild the plugin against a matching SDK", {}});
  }
  if (manifest.sdk_minor > host.abi_minor) {
    return Result<Negotiation>::failure(Error{
        ErrorCode::unsupported,
        "plugin '" + manifest.id + "' needs SDK " + std::to_string(manifest.sdk_major) + "." +
            std::to_string(manifest.sdk_minor) + " but this host provides " +
            std::to_string(host.abi_major) + "." + std::to_string(host.abi_minor),
        "update MolShredder or use an older plugin build", {}});
  }
  Negotiation result;
  result.abi_major = host.abi_major;
  result.abi_minor = std::min(manifest.sdk_minor, host.abi_minor);
  std::vector<std::string> missing;
  for (const auto& capability : manifest.required_capabilities) {
    if (host.capabilities.count(capability) == 0U) {
      missing.push_back(capability);
    } else {
      result.capabilities.insert(capability);
    }
  }
  if (!missing.empty()) {
    std::string list;
    for (const auto& name : missing) list += (list.empty() ? "" : ", ") + name;
    return Result<Negotiation>::failure(Error{
        ErrorCode::unsupported,
        "plugin '" + manifest.id + "' requires capabilities this host lacks: " + list,
        "use a host that offers them", {}});
  }
  for (const auto& capability : manifest.optional_capabilities) {
    if (host.capabilities.count(capability) != 0U) result.capabilities.insert(capability);
  }
  return Result<Negotiation>::success(std::move(result));
}

}  // namespace molshredder::plugin
