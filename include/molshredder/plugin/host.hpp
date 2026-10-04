#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "molshredder/command/registry.hpp"
#include "molshredder/plugin/runtime.hpp"

namespace molshredder::application {
class Workspace;
}

namespace molshredder::plugin {

struct StartupEntry {
  std::string id;
  // registered | skipped
  std::string status;
  std::string detail;
  std::vector<std::string> operations;
};

struct LoadedInfo {
  std::string id;
  std::string kind;
  bool active{};
};

// Owns the runtimes of approved plugins. Approved plugins add typed
// operations at startup without running any plugin code; code starts on the
// first invocation, after the pinned hashes are verified once more.
class PluginHost : public std::enable_shared_from_this<PluginHost> {
 public:
  explicit PluginHost(std::shared_ptr<application::Workspace> workspace)
      : workspace_{std::move(workspace)} {}
  ~PluginHost();

  void register_approved(command::Registry& registry, const std::filesystem::path& trust_store_path);

  [[nodiscard]] operation::Result<InvocationOutput> invoke(
      const std::filesystem::path& trust_store_path, const std::string& plugin_id,
      const std::string& operation, const command::Value::Object& arguments,
      operation::TaskContext& context, const InvokeOptions& options);

  // Like invoke, but with caller-supplied data services (the compatibility kit
  // feeds a synthetic molecule instead of a Workspace).
  [[nodiscard]] operation::Result<InvocationOutput> invoke_with_services(
      const std::filesystem::path& trust_store_path, const std::string& plugin_id,
      const std::string& operation, const command::Value::Object& arguments,
      operation::TaskContext& context, const InvokeOptions& options,
      std::function<operation::Result<command::Value>(std::string_view, const command::Value::Object&)> request);

  // Verifies, starts and handshakes an approved plugin without invoking it.
  [[nodiscard]] operation::Result<bool> check_start(const std::filesystem::path& trust_store_path,
                                                    const std::string& plugin_id,
                                                    const InvokeOptions& options);

  // Stops a running worker or drops a loaded library (after a revoke).
  void forget(const std::string& plugin_id);

  [[nodiscard]] std::vector<StartupEntry> startup_report() const;
  [[nodiscard]] std::vector<LoadedInfo> loaded() const;
  [[nodiscard]] HostServices services() const;

 private:
  [[nodiscard]] operation::Result<PluginRuntime*> acquire(
      const std::filesystem::path& trust_store_path, const std::string& plugin_id,
      const std::string& operation);

  struct Slot {
    TrustEntry pinned;
    std::unique_ptr<PluginRuntime> runtime;
  };

  std::shared_ptr<application::Workspace> workspace_;
  mutable std::mutex mutex_;
  std::map<std::string, Slot> slots_;
  std::vector<StartupEntry> startup_;
};

// Registers `plugin list|approve|revoke|status|invoke` and the typed
// operations of every approved plugin.
[[nodiscard]] std::optional<operation::Error> register_plugin_commands(
    command::Registry& registry, std::shared_ptr<application::Workspace> workspace);

}  // namespace molshredder::plugin
