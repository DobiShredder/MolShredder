#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "molshredder/command/result.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/plugin/manifest.hpp"
#include "molshredder/plugin/trust_store.hpp"

namespace molshredder::plugin {

struct InvocationOutput {
  std::string summary;
  command::Value::Object fields;
  std::optional<command::Table> table;
};

struct InvokeOptions {
  std::chrono::milliseconds timeout{60'000};
  std::size_t max_message_bytes{8U * 1024U * 1024U};
};

// Data the host lends to a plugin while it runs (capability-gated, read-only).
struct HostServices {
  std::function<operation::Result<command::Value>(std::string_view method,
                                                  const command::Value::Object& params)>
      request;
};

class PluginRuntime {
 public:
  virtual ~PluginRuntime() = default;
  [[nodiscard]] virtual operation::Result<InvocationOutput> invoke(
      const std::string& operation, const command::Value::Object& arguments,
      operation::TaskContext& context, const InvokeOptions& options,
      const HostServices& services) = 0;
  // Starts the worker or loads the library and completes the handshake without
  // invoking anything (used by the compatibility kit).
  [[nodiscard]] virtual operation::Result<bool> start(const InvokeOptions& options) = 0;
  virtual void shutdown() = 0;
  [[nodiscard]] virtual std::string kind() const = 0;
  [[nodiscard]] virtual bool active() const = 0;
};

// Starts nothing until the first invocation. The pinned hashes are verified
// again immediately before every start.
[[nodiscard]] std::unique_ptr<PluginRuntime> make_worker_runtime(
    VerifiedPlugin plugin, Negotiation negotiated, HostInfo host);

// Loads the approved library on first use (trusted, in-process, no isolation).
[[nodiscard]] std::unique_ptr<PluginRuntime> make_in_process_runtime(
    VerifiedPlugin plugin, Negotiation negotiated, HostInfo host);

// Shared by both runtimes: converts a plugin's result object.
[[nodiscard]] operation::Result<InvocationOutput> parse_invocation_result(
    const command::Value::Object& message);

}  // namespace molshredder::plugin
