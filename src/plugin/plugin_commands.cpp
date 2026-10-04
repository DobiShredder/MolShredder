#include <algorithm>
#include <utility>

#include "molshredder/command/json.hpp"
#include "molshredder/plugin/host.hpp"

namespace molshredder::plugin {
namespace {

using command::Value;
using operation::Error;
using operation::ErrorCode;
using operation::Result;

Error invalid(std::string message, std::string suggestion = {}) {
  return {ErrorCode::invalid_argument, std::move(message), std::move(suggestion)};
}

std::filesystem::path store_path(const command::Arguments& arguments) {
  const auto found = arguments.find("trust-store");
  if (found != arguments.end() && !found->second.empty()) return found->second;
  return default_trust_store_path();
}

Value strings(const std::vector<std::string>& items) {
  Value::Array array;
  for (const auto& item : items) array.emplace_back(item);
  return array;
}

std::string timestamp() {
  const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm parts{};
#if defined(_WIN32)
  gmtime_s(&parts, &now);
#else
  gmtime_r(&now, &parts);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts);
  return buffer;
}

Value operations_value(const PluginManifest& manifest) {
  Value::Array operations;
  for (const auto& operation : manifest.operations) {
    operations.emplace_back(canonical_operation_name(manifest, operation));
  }
  return operations;
}

Result<command::Response> list(const command::Arguments& arguments) {
  const auto path = store_path(arguments);
  const auto store = path.empty() ? Result<TrustStore>::success(TrustStore{}) : TrustStore::load(path);
  if (!store.has_value()) return Result<command::Response>::failure(store.error());
  Value::Array plugins;
  if (const auto directory = arguments.find("directory");
      directory != arguments.end() && !directory->second.empty()) {
    const auto found = discover(directory->second, store.value());
    if (!found.has_value()) return Result<command::Response>::failure(found.error());
    for (const auto& item : found.value()) {
      plugins.emplace_back(Value::Object{{"detail", item.detail},
                                         {"id", item.id},
                                         {"manifest_path", item.manifest_path.string()},
                                         {"mode", item.mode},
                                         {"state", to_string(item.state)},
                                         {"version", item.version}});
    }
  } else {
    for (const auto& entry : store.value().entries()) {
      const auto verified = verify_approved(entry);
      plugins.emplace_back(Value::Object{
          {"approved_at", entry.approved_at},
          {"detail", verified.has_value() ? "approved" : verified.error().message},
          {"id", entry.id},
          {"manifest_path", entry.manifest_path},
          {"mode", entry.mode == ExecutionMode::worker ? "worker" : "in_process"},
          {"state", verified.has_value() ? "approved" : "changed"},
          {"version", entry.version}});
    }
  }
  const auto count = plugins.size();
  return Result<command::Response>::success(
      {"listed " + std::to_string(count) + " plugins; nothing was executed",
       {{"count", static_cast<std::uint64_t>(count)},
        {"plugins", std::move(plugins)},
        {"trust_store", path.string()}}});
}

Result<command::Response> approve(const std::shared_ptr<PluginHost>& host, const command::Arguments& arguments) {
  const auto path = store_path(arguments);
  const auto manifest_path = arguments.at("manifest");
  const auto manifest = load_manifest_file(manifest_path);
  if (!manifest.has_value()) return Result<command::Response>::failure(manifest.error());
  const auto negotiated = negotiate(manifest.value(), default_host_info());
  if (!negotiated.has_value()) return Result<command::Response>::failure(negotiated.error());
  auto store = TrustStore::load(path);
  if (!store.has_value()) return Result<command::Response>::failure(store.error());
  const auto entry = store.value().approve(manifest.value(), manifest_path,
                                           arguments.at("allow-in-process") == "true", timestamp());
  if (!entry.has_value()) return Result<command::Response>::failure(entry.error());
  const auto saved = store.value().save();
  if (!saved.has_value()) return Result<command::Response>::failure(saved.error());
  host->forget(entry.value().id);
  return Result<command::Response>::success(
      {"approved plugin " + entry.value().id + " " + entry.value().version,
       {{"allow_in_process", entry.value().allow_in_process},
        {"entry_path", entry.value().entry_path},
        {"entry_sha256", entry.value().entry_sha256},
        {"id", entry.value().id},
        {"manifest_sha256", entry.value().manifest_sha256},
        {"mode", manifest.value().mode == ExecutionMode::worker ? "worker" : "in_process"},
        {"negotiated_capabilities",
         strings({negotiated.value().capabilities.begin(), negotiated.value().capabilities.end()})},
        {"operations", operations_value(manifest.value())},
        {"trust_store", path.string()},
        {"typed_operations_available", "after the next start; use `plugin invoke` now"}}});
}

Result<command::Response> revoke(const std::shared_ptr<PluginHost>& host, const command::Arguments& arguments) {
  const auto path = store_path(arguments);
  auto store = TrustStore::load(path);
  if (!store.has_value()) return Result<command::Response>::failure(store.error());
  const auto id = arguments.at("id");
  if (!store.value().revoke(id)) {
    return Result<command::Response>::failure(Error{ErrorCode::not_found, "plugin '" + id + "' is not approved", {}, {}});
  }
  const auto saved = store.value().save();
  if (!saved.has_value()) return Result<command::Response>::failure(saved.error());
  host->forget(id);
  return Result<command::Response>::success(
      {"revoked plugin " + id, {{"id", id}, {"trust_store", path.string()}}});
}

Result<command::Response> status(const std::shared_ptr<PluginHost>& host) {
  Value::Array startup;
  for (const auto& entry : host->startup_report()) {
    startup.emplace_back(Value::Object{{"detail", entry.detail},
                                       {"id", entry.id},
                                       {"operations", strings(entry.operations)},
                                       {"status", entry.status}});
  }
  Value::Array running;
  for (const auto& info : host->loaded()) {
    running.emplace_back(Value::Object{{"active", info.active}, {"id", info.id}, {"kind", info.kind}});
  }
  const auto info = default_host_info();
  return Result<command::Response>::success(
      {"plugin host ready",
       {{"capabilities", strings({info.capabilities.begin(), info.capabilities.end()})},
        {"loaded", std::move(running)},
        {"sdk_version", std::to_string(info.abi_major) + "." + std::to_string(info.abi_minor)},
        {"startup", std::move(startup)}}});
}

Result<command::Response> invoke_generic(const std::shared_ptr<PluginHost>& host,
                                         const command::Arguments& arguments,
                                         operation::TaskContext& context) {
  const auto parsed = command::parse_json(arguments.at("arguments-json"), command::JsonLimits{1024U * 1024U, 16U});
  if (!parsed.has_value()) return Result<command::Response>::failure(parsed.error());
  const auto* object = std::get_if<Value::Object>(&parsed.value().data);
  if (object == nullptr) return Result<command::Response>::failure(invalid("--arguments-json must be a JSON object"));
  InvokeOptions options;
  options.timeout = std::chrono::milliseconds{std::stoll(arguments.at("timeout-ms"))};
  if (options.timeout.count() <= 0 || options.timeout.count() > 3'600'000) {
    return Result<command::Response>::failure(invalid("--timeout-ms must be from 1 to 3600000"));
  }
  const auto path = store_path(arguments);
  auto output = host->invoke(path, arguments.at("plugin"), arguments.at("operation"), *object, context, options);
  if (!output.has_value()) return Result<command::Response>::failure(output.error());
  command::Response response;
  response.summary = output.value().summary.empty() ? "plugin operation finished" : output.value().summary;
  response.fields = std::move(output.value().fields);
  response.fields.emplace("plugin", Value::Object{{"id", arguments.at("plugin")}});
  response.table = std::move(output.value().table);
  return Result<command::Response>::success(std::move(response));
}

}  // namespace

std::optional<operation::Error> register_plugin_commands(command::Registry& registry,
                                                         std::shared_ptr<application::Workspace> workspace) {
  const auto host = std::make_shared<PluginHost>(std::move(workspace));
  using command::ParameterSpec;
  using command::ParameterType;
  const auto store_option = ParameterSpec{"trust-store", ParameterType::text, false};
  if (const auto error = registry.add(
          {"plugin list", "List approved plugins, or the manifests found in a directory",
           {ParameterSpec{"directory", ParameterType::text, false}, store_option},
           command::UndoPolicy::not_applicable},
          [](const command::Arguments& arguments, operation::TaskContext&) { return list(arguments); });
      error.has_value()) {
    return error;
  }
  if (const auto error = registry.add(
          {"plugin approve", "Approve a plugin by pinning its manifest and entry file hashes",
           {ParameterSpec{"manifest", ParameterType::text, true},
            ParameterSpec{"allow-in-process", ParameterType::boolean, false, "false", {"false", "true"}},
            store_option},
           command::UndoPolicy::not_undoable},
          [host](const command::Arguments& arguments, operation::TaskContext&) { return approve(host, arguments); });
      error.has_value()) {
    return error;
  }
  if (const auto error = registry.add(
          {"plugin revoke", "Remove a plugin's approval and stop it",
           {ParameterSpec{"id", ParameterType::text, true}, store_option},
           command::UndoPolicy::not_undoable},
          [host](const command::Arguments& arguments, operation::TaskContext&) { return revoke(host, arguments); });
      error.has_value()) {
    return error;
  }
  if (const auto error = registry.add(
          {"plugin status", "Report the plugin host, registered plugins and running workers", {},
           command::UndoPolicy::not_applicable},
          [host](const command::Arguments&, operation::TaskContext&) { return status(host); });
      error.has_value()) {
    return error;
  }
  if (const auto error = registry.add(
          {"plugin invoke", "Run an operation of an approved plugin by plugin id",
           {ParameterSpec{"plugin", ParameterType::text, true},
            ParameterSpec{"operation", ParameterType::text, true},
            ParameterSpec{"arguments-json", ParameterType::text, false, "{}"},
            ParameterSpec{"timeout-ms", ParameterType::unsigned_integer, false, "60000"}, store_option},
           command::UndoPolicy::not_applicable},
          [host](const command::Arguments& arguments, operation::TaskContext& context) {
            return invoke_generic(host, arguments, context);
          });
      error.has_value()) {
    return error;
  }
  host->register_approved(registry, default_trust_store_path());
  return std::nullopt;
}

}  // namespace molshredder::plugin
