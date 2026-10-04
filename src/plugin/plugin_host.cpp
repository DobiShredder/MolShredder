#include "molshredder/plugin/host.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <utility>

#include "molshredder/application/workspace.hpp"
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

Result<Value::Object> typed_arguments(const OperationDeclaration& operation,
                                      const command::Arguments& arguments) {
  Value::Object typed;
  for (const auto& parameter : operation.parameters) {
    const auto found = arguments.find(parameter.name);
    if (found == arguments.end()) continue;
    const auto& text = found->second;
    switch (parameter.type) {
      case command::ParameterType::text: typed.emplace(parameter.name, text); break;
      case command::ParameterType::boolean: {
        if (text != "true" && text != "false") {
          return Result<Value::Object>::failure(invalid("argument '" + parameter.name + "' must be true or false"));
        }
        typed.emplace(parameter.name, text == "true");
        break;
      }
      case command::ParameterType::integer: {
        std::int64_t value{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
          return Result<Value::Object>::failure(invalid("argument '" + parameter.name + "' must be an integer"));
        }
        typed.emplace(parameter.name, value);
        break;
      }
      case command::ParameterType::unsigned_integer: {
        std::uint64_t value{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
          return Result<Value::Object>::failure(invalid("argument '" + parameter.name + "' must be a non-negative integer"));
        }
        typed.emplace(parameter.name, value);
        break;
      }
      case command::ParameterType::number: {
        double value{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
          return Result<Value::Object>::failure(invalid("argument '" + parameter.name + "' must be a number"));
        }
        typed.emplace(parameter.name, value);
        break;
      }
    }
  }
  return Result<Value::Object>::success(std::move(typed));
}

command::Response to_response(const PluginManifest& manifest, InvocationOutput output) {
  command::Response response;
  response.summary = output.summary.empty() ? "plugin " + manifest.id + " finished" : std::move(output.summary);
  response.fields = std::move(output.fields);
  response.fields.emplace("plugin", Value::Object{{"id", manifest.id}, {"version", manifest.version}});
  response.table = std::move(output.table);
  return response;
}

}  // namespace

PluginHost::~PluginHost() {
  const std::lock_guard<std::mutex> lock{mutex_};
  for (auto& [id, slot] : slots_) {
    if (slot.runtime) slot.runtime->shutdown();
  }
}

HostServices PluginHost::services() const {
  HostServices services;
  const auto workspace = workspace_;
  services.request = [workspace](std::string_view method, const Value::Object& params) -> Result<Value> {
    if (method != "selection.atoms") {
      return Result<Value>::failure(Error{ErrorCode::unsupported, "unknown host method '" + std::string{method} + "'", {}});
    }
    if (workspace == nullptr) {
      return Result<Value>::failure(Error{ErrorCode::not_found, "no workspace is attached to this host", {}});
    }
    std::string expression = "all";
    if (const auto found = params.find("selection"); found != params.end()) {
      const auto* text = std::get_if<std::string>(&found->second.data);
      if (text == nullptr) return Result<Value>::failure(invalid("'selection' must be a string"));
      expression = *text;
    }
    std::size_t limit = 1'000'000U;
    if (const auto found = params.find("limit"); found != params.end()) {
      const auto* value = std::get_if<std::int64_t>(&found->second.data);
      if (value == nullptr || *value <= 0 || *value > 1'000'000) {
        return Result<Value>::failure(invalid("'limit' must be an integer from 1 to 1000000"));
      }
      limit = static_cast<std::size_t>(*value);
    }
    const auto snapshot = workspace->selection_snapshot(expression, limit);
    if (!snapshot.has_value()) return Result<Value>::failure(snapshot.error());
    Value::Array ids, names, numbers, positions;
    for (std::size_t i = 0; i < snapshot.value().atom_ids.size(); ++i) {
      ids.emplace_back(snapshot.value().atom_ids[i]);
      names.emplace_back(snapshot.value().atom_names[i]);
      numbers.emplace_back(static_cast<std::uint64_t>(snapshot.value().atomic_numbers[i]));
      const auto& p = snapshot.value().positions[i];
      positions.emplace_back(Value::Array{Value{p.x}, Value{p.y}, Value{p.z}});
    }
    return Result<Value>::success(Value{Value::Object{
        {"atomic_numbers", std::move(numbers)},
        {"count", static_cast<std::uint64_t>(snapshot.value().atom_ids.size())},
        {"frame_index", static_cast<std::uint64_t>(snapshot.value().frame_index)},
        {"ids", std::move(ids)},
        {"names", std::move(names)},
        {"object", snapshot.value().object_name},
        {"positions", std::move(positions)},
        {"unit", snapshot.value().coordinate_unit == operation::LengthUnit::angstrom ? "angstrom" : "nanometer"}}});
  };
  return services;
}

std::vector<StartupEntry> PluginHost::startup_report() const {
  const std::lock_guard<std::mutex> lock{mutex_};
  return startup_;
}

std::vector<LoadedInfo> PluginHost::loaded() const {
  const std::lock_guard<std::mutex> lock{mutex_};
  std::vector<LoadedInfo> info;
  for (const auto& [id, slot] : slots_) {
    info.push_back({id, slot.runtime->kind(), slot.runtime->active()});
  }
  return info;
}

void PluginHost::forget(const std::string& plugin_id) {
  const std::lock_guard<std::mutex> lock{mutex_};
  const auto found = slots_.find(plugin_id);
  if (found == slots_.end()) return;
  if (found->second.runtime) found->second.runtime->shutdown();
  slots_.erase(found);
}

Result<PluginRuntime*> PluginHost::acquire(const std::filesystem::path& trust_store_path,
                                           const std::string& plugin_id, const std::string& operation) {
  const std::lock_guard<std::mutex> lock{mutex_};
  const auto store = TrustStore::load(trust_store_path);
  if (!store.has_value()) return Result<PluginRuntime*>::failure(store.error());
  const auto* entry = store.value().find(plugin_id);
  if (entry == nullptr) {
    const auto stale = slots_.find(plugin_id);
    if (stale != slots_.end()) {
      if (stale->second.runtime) stale->second.runtime->shutdown();
      slots_.erase(stale);
    }
    return Result<PluginRuntime*>::failure(Error{
        ErrorCode::not_found, "plugin '" + plugin_id + "' is not approved",
        "approve it with: plugin approve --manifest <path>", {}});
  }
  auto slot = slots_.find(plugin_id);
  const bool pinned_changed = slot != slots_.end() &&
                              (slot->second.pinned.manifest_sha256 != entry->manifest_sha256 ||
                               slot->second.pinned.entry_sha256 != entry->entry_sha256 ||
                               slot->second.pinned.allow_in_process != entry->allow_in_process);
  if (pinned_changed) {
    if (slot->second.runtime) slot->second.runtime->shutdown();
    slots_.erase(slot);
    slot = slots_.end();
  }
  if (slot == slots_.end()) {
    auto verified = verify_approved(*entry);
    if (!verified.has_value()) return Result<PluginRuntime*>::failure(verified.error());
    const auto host = default_host_info();
    const auto negotiated = negotiate(verified.value().manifest, host);
    if (!negotiated.has_value()) return Result<PluginRuntime*>::failure(negotiated.error());
    if (!operation.empty()) {
      const auto declared = std::any_of(
          verified.value().manifest.operations.begin(), verified.value().manifest.operations.end(),
          [&](const OperationDeclaration& declaration) { return declaration.name == operation; });
      if (!declared) {
        return Result<PluginRuntime*>::failure(Error{
            ErrorCode::not_found, "plugin '" + plugin_id + "' has no operation '" + operation + "'", {}, {}});
      }
    }
    Slot created;
    created.pinned = *entry;
    const auto mode = verified.value().manifest.mode;
    created.runtime = mode == ExecutionMode::worker
                          ? make_worker_runtime(verified.value(), negotiated.value(), host)
                          : make_in_process_runtime(verified.value(), negotiated.value(), host);
    slot = slots_.emplace(plugin_id, std::move(created)).first;
  } else if (!operation.empty()) {
    // A cached runtime was verified when it was created; still refuse
    // operations the manifest does not declare.
    const auto verified = load_manifest_file(entry->manifest_path);
    if (verified.has_value()) {
      const auto declared = std::any_of(
          verified.value().operations.begin(), verified.value().operations.end(),
          [&](const OperationDeclaration& declaration) { return declaration.name == operation; });
      if (!declared) {
        return Result<PluginRuntime*>::failure(Error{
            ErrorCode::not_found, "plugin '" + plugin_id + "' has no operation '" + operation + "'", {}, {}});
      }
    }
  }
  return Result<PluginRuntime*>::success(slot->second.runtime.get());
}

Result<InvocationOutput> PluginHost::invoke(const std::filesystem::path& trust_store_path,
                                            const std::string& plugin_id, const std::string& operation,
                                            const Value::Object& arguments, operation::TaskContext& context,
                                            const InvokeOptions& options) {
  const auto runtime = acquire(trust_store_path, plugin_id, operation);
  if (!runtime.has_value()) return Result<InvocationOutput>::failure(runtime.error());
  // The runtime serializes its own callers; do not hold the host lock while a
  // plugin runs (a slow plugin must not block `plugin status`).
  return runtime.value()->invoke(operation, arguments, context, options, services());
}

Result<InvocationOutput> PluginHost::invoke_with_services(
    const std::filesystem::path& trust_store_path, const std::string& plugin_id,
    const std::string& operation, const Value::Object& arguments, operation::TaskContext& context,
    const InvokeOptions& options,
    std::function<Result<Value>(std::string_view, const Value::Object&)> request) {
  const auto runtime = acquire(trust_store_path, plugin_id, operation);
  if (!runtime.has_value()) return Result<InvocationOutput>::failure(runtime.error());
  HostServices services;
  services.request = std::move(request);
  return runtime.value()->invoke(operation, arguments, context, options, services);
}

Result<bool> PluginHost::check_start(const std::filesystem::path& trust_store_path,
                                     const std::string& plugin_id, const InvokeOptions& options) {
  const auto runtime = acquire(trust_store_path, plugin_id, {});
  if (!runtime.has_value()) return Result<bool>::failure(runtime.error());
  return runtime.value()->start(options);
}

void PluginHost::register_approved(command::Registry& registry,
                                   const std::filesystem::path& trust_store_path) {
  const std::lock_guard<std::mutex> lock{mutex_};
  startup_.clear();
  if (trust_store_path.empty()) return;
  const auto store = TrustStore::load(trust_store_path);
  if (!store.has_value()) {
    startup_.push_back({"", "skipped", store.error().message, {}});
    return;
  }
  for (const auto& entry : store.value().entries()) {
    StartupEntry report;
    report.id = entry.id;
    const auto verified = verify_approved(entry);
    if (!verified.has_value()) {
      report.status = "skipped";
      report.detail = verified.error().message;
      startup_.push_back(std::move(report));
      continue;
    }
    const auto negotiated = negotiate(verified.value().manifest, default_host_info());
    if (!negotiated.has_value()) {
      report.status = "skipped";
      report.detail = negotiated.error().message;
      startup_.push_back(std::move(report));
      continue;
    }
    const auto& manifest = verified.value().manifest;
    bool all_registered = true;
    for (const auto& declaration : manifest.operations) {
      std::vector<command::ParameterSpec> parameters;
      for (const auto& parameter : declaration.parameters) {
        parameters.emplace_back(parameter.name, parameter.type, parameter.required, parameter.default_value,
                                parameter.choices);
      }
      const command::Descriptor descriptor{canonical_operation_name(manifest, declaration),
                                           declaration.summary.empty() ? "Plugin operation" : declaration.summary,
                                           std::move(parameters), command::UndoPolicy::not_applicable};
      const auto host = shared_from_this();
      const auto path = trust_store_path;
      const auto id = manifest.id;
      const auto name = declaration.name;
      const auto declared = declaration;
      const auto plugin_manifest = manifest;
      const auto error = registry.add(
          descriptor, [host, path, id, name, declared, plugin_manifest](
                          const command::Arguments& arguments, operation::TaskContext& context)
                          -> Result<command::Response> {
            const auto typed = typed_arguments(declared, arguments);
            if (!typed.has_value()) return Result<command::Response>::failure(typed.error());
            auto output = host->invoke(path, id, name, typed.value(), context, InvokeOptions{});
            if (!output.has_value()) return Result<command::Response>::failure(output.error());
            return Result<command::Response>::success(to_response(plugin_manifest, std::move(output.value())));
          });
      if (error.has_value()) {
        all_registered = false;
        report.detail += (report.detail.empty() ? "" : "; ") + declaration.name + ": " + error->message;
      } else {
        report.operations.push_back(canonical_operation_name(manifest, declaration));
      }
    }
    report.status = all_registered ? "registered" : "skipped";
    startup_.push_back(std::move(report));
  }
}

}  // namespace molshredder::plugin
