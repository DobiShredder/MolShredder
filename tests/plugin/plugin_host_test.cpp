#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <thread>

#include "molshredder/plugin/host.hpp"

namespace {

using molshredder::command::Value;
using molshredder::operation::ErrorCode;
using namespace molshredder::plugin;

bool expect(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAILED: " << message << '\n';
  return condition;
}

std::string manifest(const std::string& id, const std::string& ns, const std::string& extra_args,
                     const std::string& capabilities) {
  return R"({"schema_version":1,"id":")" + id + R"(","namespace":")" + ns + R"(","version":"1.0.0","license":"MIT",
    "sdk":{"abi_major":1,"abi_minor":0},"execution":{"mode":"worker","command":["worker")" + extra_args + R"(]},)" +
         capabilities +
         R"("operations":[
      {"name":"echo","parameters":[{"name":"text","type":"text","required":true},{"name":"times","type":"unsigned_integer","default":"1"}]},
      {"name":"sum","parameters":[{"name":"count","type":"unsigned_integer","default":"4"}]},
      {"name":"centroid","parameters":[{"name":"selection","type":"text","default":"all"}]},
      {"name":"fail"},{"name":"crash"},{"name":"hang"},{"name":"flood"},{"name":"badjson"},{"name":"wait-cancel"},
      {"name":"badid"},{"name":"table"},{"name":"environment"},{"name":"directory"},{"name":"nested"}]})";
}

// Creates a plugin directory holding a copy of the test worker and approves it.
struct Fixture {
  std::filesystem::path directory;
  std::filesystem::path manifest_path;
  std::filesystem::path marker;
  std::filesystem::path store_path;
};

Fixture make_plugin(const std::filesystem::path& root, const std::filesystem::path& worker,
                    const std::string& name, const std::string& mode, bool with_capabilities = true) {
  Fixture fixture;
  fixture.directory = root / name;
  std::filesystem::create_directories(fixture.directory);
  std::filesystem::copy_file(worker, fixture.directory / "worker", std::filesystem::copy_options::overwrite_existing);
  fixture.marker = fixture.directory / "started.log";
  fixture.manifest_path = fixture.directory / kManifestFileName;
  const std::string extra = R"(,"--mode=)" + mode + R"(","--marker=)" + fixture.marker.string() + R"(")";
  const std::string capabilities = with_capabilities
      ? R"("required_capabilities":["progress"],"optional_capabilities":["cancellation","host.selection.atoms"],)"
      : R"("required_capabilities":[],"optional_capabilities":["progress"],)";
  std::ofstream out{fixture.manifest_path};
  out << manifest(mode == "badid-hello" ? "org.example.worker" : "org.example.worker", "sample", extra, capabilities);
  fixture.store_path = root / (name + "-trust.json");
  return fixture;
}

void approve(const Fixture& fixture) {
  auto store = TrustStore::load(fixture.store_path);
  const auto manifest = load_manifest_file(fixture.manifest_path);
  if (!manifest.has_value()) {
    std::cerr << "manifest error: " << manifest.error().message << '\n';
    return;
  }
  const auto entry = store.value().approve(manifest.value(), fixture.manifest_path, false, "t");
  if (!entry.has_value()) std::cerr << entry.error().message << '\n';
  static_cast<void>(store.value().save());
}

std::size_t starts(const Fixture& fixture) {
  std::ifstream in{fixture.marker};
  std::size_t count = 0;
  for (std::string line; std::getline(in, line);) ++count;
  return count;
}

Value text(const char* value) { return Value{value}; }

// JSON integers read back as int64 unless they exceed its range.
std::uint64_t number(const Value& value) {
  if (const auto* i = std::get_if<std::int64_t>(&value.data)) return static_cast<std::uint64_t>(*i);
  return std::get<std::uint64_t>(value.data);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const std::filesystem::path worker = argv[1];
  const auto root = std::filesystem::path{argv[2]} / "plugin-host-fixtures";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  bool passed = true;

  auto host = std::make_shared<PluginHost>(nullptr);
  const auto run = [&](const Fixture& f, const std::string& operation, const Value::Object& arguments,
                       molshredder::operation::TaskContext& context, InvokeOptions options = {}) {
    return host->invoke(f.store_path, "org.example.worker", operation, arguments, context, options);
  };
  molshredder::operation::TaskContext context;

  // 1. An unapproved plugin never starts.
  const auto plugin = make_plugin(root, worker, "main", "normal");
  const auto unapproved = run(plugin, "echo", {{"text", text("x")}}, context);
  passed &= expect(!unapproved.has_value() && unapproved.error().code == ErrorCode::not_found &&
                       !std::filesystem::exists(plugin.marker),
                   "an unapproved plugin is refused and not started");
  approve(plugin);

  // 2. Typed arguments, results and tables.
  const auto echoed = run(plugin, "echo", {{"text", text("ab")}, {"times", std::uint64_t{3}}}, context);
  passed &= expect(echoed.has_value() && std::get<std::string>(echoed.value().fields.at("text").data) == "ababab" &&
                       echoed.value().summary == "echoed",
                   "echo returns the typed result");
  const auto table = run(plugin, "table", {}, context);
  passed &= expect(table.has_value() && table.value().table.has_value() &&
                       table.value().table->rows.size() == 2U && table.value().table->columns.size() == 2U,
                   "tables are converted");
  const auto nested = run(plugin, "nested", {}, context);
  passed &= expect(nested.has_value() && nested.value().fields.count("deep") == 1U, "nested JSON survives");
  passed &= expect(starts(plugin) == 1U, "one worker serves several calls");

  // 3. Progress is forwarded.
  std::vector<double> fractions;
  molshredder::operation::TaskContext progress_context;
  progress_context.report_progress = [&fractions](const molshredder::operation::ProgressUpdate& update) {
    fractions.push_back(update.fraction);
  };
  const auto summed = run(plugin, "sum", {{"count", std::uint64_t{4}}}, progress_context);
  passed &= expect(summed.has_value() && number(summed.value().fields.at("total")) == 10U &&
                       fractions.size() == 4U && fractions.back() == 1.0,
                   "progress updates reach the task context");

  // 4. Plugin errors keep their code and message.
  const auto failed = run(plugin, "fail", {}, context);
  passed &= expect(!failed.has_value() && failed.error().code == ErrorCode::invalid_argument &&
                       failed.error().message == "boom" && failed.error().suggestion == "do not do that",
                   "plugin errors are mapped");
  passed &= expect(run(plugin, "echo", {{"text", text("again")}}, context).has_value() && starts(plugin) == 1U,
                   "a plugin error does not restart the worker");

  // 5. A crash is reported with stderr and the next call restarts the worker.
  const auto crashed = run(plugin, "crash", {}, context);
  passed &= expect(!crashed.has_value() && crashed.error().message.find("exited") != std::string::npos &&
                       crashed.error().message.find("crashing on purpose") != std::string::npos,
                   "a crash is reported with the stderr tail");
  passed &= expect(run(plugin, "echo", {{"text", text("back")}}, context).has_value() && starts(plugin) == 2U,
                   "the worker restarts after a crash");

  // 6. Timeout kills a hung worker.
  InvokeOptions short_timeout;
  short_timeout.timeout = std::chrono::milliseconds{300};
  const auto started_at = std::chrono::steady_clock::now();
  const auto timed_out = run(plugin, "hang", {}, context, short_timeout);
  passed &= expect(!timed_out.has_value() && timed_out.error().message.find("timed out") != std::string::npos &&
                       std::chrono::steady_clock::now() - started_at < std::chrono::seconds{10},
                   "a hung worker times out");
  passed &= expect(run(plugin, "echo", {{"text", text("alive")}}, context).has_value() && starts(plugin) == 3U,
                   "a new worker starts after a timeout");

  // 7. Cooperative cancellation.
  {
    molshredder::operation::TaskContext cancel_context;
    auto future = std::async(std::launch::async, [&] { return run(plugin, "wait-cancel", {}, cancel_context); });
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    cancel_context.cancellation.request_cancel();
    const auto cancelled = future.get();
    passed &= expect(!cancelled.has_value() && cancelled.error().code == ErrorCode::cancelled &&
                         cancelled.error().message == "stopped on request",
                     "a cooperative plugin stops on cancel");
  }
  {
    molshredder::operation::TaskContext cancel_context;
    auto future = std::async(std::launch::async, [&] { return run(plugin, "hang", {}, cancel_context); });
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    const auto before = std::chrono::steady_clock::now();
    cancel_context.cancellation.request_cancel();
    const auto cancelled = future.get();
    passed &= expect(!cancelled.has_value() && cancelled.error().code == ErrorCode::cancelled &&
                         std::chrono::steady_clock::now() - before < std::chrono::seconds{8},
                     "a plugin that ignores cancel is stopped after the grace period");
  }
  molshredder::operation::TaskContext pre_cancelled;
  pre_cancelled.cancellation.request_cancel();
  const auto before_count = starts(plugin);
  passed &= expect(!run(plugin, "echo", {{"text", text("x")}}, pre_cancelled).has_value() &&
                       starts(plugin) == before_count,
                   "a pre-cancelled call does not start anything");

  // 8. Protocol violations stop the worker; the host stays healthy.
  InvokeOptions small;
  small.max_message_bytes = 1024U * 1024U;
  const auto flooded = run(plugin, "flood", {}, context, small);
  passed &= expect(!flooded.has_value() && flooded.error().message.find("size limit") != std::string::npos,
                   "an over-long message is refused");
  const auto garbage = run(plugin, "badjson", {}, context);
  passed &= expect(!garbage.has_value() && garbage.error().message.find("protocol error") != std::string::npos,
                   "a non-JSON message is a protocol error");
  const auto wrong_id = run(plugin, "badid", {}, context);
  passed &= expect(!wrong_id.has_value() && wrong_id.error().message.find("protocol error") != std::string::npos,
                   "a result for another invocation is a protocol error");
  passed &= expect(run(plugin, "echo", {{"text", text("ok")}}, context).has_value(), "the plugin recovers after violations");

  // 9. The worker runs with a clean environment in its own directory.
  const auto environment = run(plugin, "environment", {}, context);
  passed &= expect(environment.has_value() &&
                       std::get<std::string>(environment.value().fields.at("home").data) == "absent" &&
                       !std::get<std::string>(environment.value().fields.at("plugin_dir").data).empty(),
                   "the host environment does not leak to the worker");
  const auto directory = run(plugin, "directory", {}, context);
  std::error_code ec;
  passed &= expect(directory.has_value() &&
                       std::filesystem::equivalent(std::get<std::string>(directory.value().fields.at("cwd").data),
                                                   plugin.directory, ec),
                   "the worker starts in its plugin directory");

  // 10. Concurrent callers are serialized, not interleaved.
  {
    auto first = std::async(std::launch::async, [&] { return run(plugin, "sum", {{"count", std::uint64_t{50}}}, context); });
    auto second = std::async(std::launch::async, [&] { return run(plugin, "echo", {{"text", text("c")}}, context); });
    passed &= expect(first.get().has_value() && second.get().has_value(), "concurrent calls both succeed");
  }

  // 11. Host data services are capability-gated.
  host->forget("org.example.worker");
  int served = 0;
  const auto data_plugin = make_plugin(root, worker, "data", "normal");
  approve(data_plugin);
  {
    // Without a workspace the host reports why; the plugin turns it into an error.
    const auto no_workspace = host->invoke(data_plugin.store_path, "org.example.worker", "centroid",
                                           {{"selection", text("all")}}, context, {});
    passed &= expect(!no_workspace.has_value() && no_workspace.error().message.find("workspace") != std::string::npos,
                     "host data without a workspace is an error the plugin can report");
    static_cast<void>(served);
  }
  host->forget("org.example.worker");
  const auto denied_plugin = make_plugin(root, worker, "denied", "normal", false);
  approve(denied_plugin);
  const auto denied = host->invoke(denied_plugin.store_path, "org.example.worker", "centroid",
                                   {{"selection", text("all")}}, context, {});
  passed &= expect(!denied.has_value() && denied.error().message.find("not negotiated") != std::string::npos,
                   "a plugin that did not negotiate host.selection.atoms is denied");

  // 12. Handshake faults.
  for (const char* mode : {"badid-hello", "badprotocol", "badops", "nohello"}) {
    host->forget("org.example.worker");
    const auto faulty = make_plugin(root, worker, std::string{"fault-"} + mode, mode);
    approve(faulty);
    const auto result = host->invoke(faulty.store_path, "org.example.worker", "echo", {{"text", text("x")}}, context, {});
    passed &= expect(!result.has_value() && result.error().message.find("handshake failed") != std::string::npos,
                     std::string{"handshake fault is rejected: "} + mode);
  }

  // 13. Tampering after approval is caught before the worker starts again.
  host->forget("org.example.worker");
  const auto tamper = make_plugin(root, worker, "tamper", "normal");
  approve(tamper);
  passed &= expect(host->invoke(tamper.store_path, "org.example.worker", "echo", {{"text", text("x")}}, context, {}).has_value(),
                   "the approved plugin runs");
  const auto starts_before = starts(tamper);
  {
    std::ofstream append{tamper.directory / "worker", std::ios::binary | std::ios::app};
    append << "tampered";
  }
  host->forget("org.example.worker");
  const auto tampered = host->invoke(tamper.store_path, "org.example.worker", "echo", {{"text", text("x")}}, context, {});
  passed &= expect(!tampered.has_value() && tampered.error().message.find("changed since approval") != std::string::npos &&
                       starts(tamper) == starts_before,
                   "a modified entry is refused and not started");

  // 14. Revocation stops the plugin and refuses further calls.
  host->forget("org.example.worker");
  const auto revoked = make_plugin(root, worker, "revoked", "normal");
  approve(revoked);
  passed &= expect(host->invoke(revoked.store_path, "org.example.worker", "echo", {{"text", text("x")}}, context, {}).has_value() &&
                       !host->loaded().empty(),
                   "a worker is loaded");
  auto store = TrustStore::load(revoked.store_path);
  static_cast<void>(store.value().revoke("org.example.worker"));
  static_cast<void>(store.value().save());
  const auto after_revoke = host->invoke(revoked.store_path, "org.example.worker", "echo", {{"text", text("x")}}, context, {});
  passed &= expect(!after_revoke.has_value() && after_revoke.error().code == ErrorCode::not_found && host->loaded().empty(),
                   "revocation stops the worker and refuses calls");

  // 15. Unknown operations are refused without starting the plugin.
  host->forget("org.example.worker");
  const auto unknown = make_plugin(root, worker, "unknown", "normal");
  approve(unknown);
  const auto missing = host->invoke(unknown.store_path, "org.example.worker", "teleport", {}, context, {});
  passed &= expect(!missing.has_value() && missing.error().code == ErrorCode::not_found && !std::filesystem::exists(unknown.marker),
                   "an undeclared operation never reaches the worker");
  host->forget("org.example.worker");
  return passed ? 0 : 1;
}
