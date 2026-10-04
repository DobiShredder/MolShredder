#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <utility>

#include "molshredder/command/json.hpp"
#include "molshredder/plugin/runtime.hpp"
#include "process.hpp"

namespace molshredder::plugin {
namespace {

using command::Value;
using operation::Error;
using operation::ErrorCode;
using operation::Result;

Error failure(ErrorCode code, std::string message, std::string suggestion = {}) {
  return {code, std::move(message), std::move(suggestion)};
}

const Value* find(const Value::Object& object, std::string_view key) {
  const auto found = object.find(key);
  return found == object.end() ? nullptr : &found->second;
}

const std::string* text_of(const Value::Object& object, std::string_view key) {
  const auto* value = find(object, key);
  return value == nullptr ? nullptr : std::get_if<std::string>(&value->data);
}

ErrorCode map_code(std::string_view code) {
  if (code == "invalid_argument") return ErrorCode::invalid_argument;
  if (code == "not_found") return ErrorCode::not_found;
  if (code == "unsupported") return ErrorCode::unsupported;
  if (code == "cancelled") return ErrorCode::cancelled;
  return ErrorCode::script_failed;
}

class WorkerRuntime final : public PluginRuntime {
 public:
  WorkerRuntime(VerifiedPlugin plugin, Negotiation negotiated, HostInfo host)
      : plugin_{std::move(plugin)}, negotiated_{std::move(negotiated)}, host_{std::move(host)} {}

  ~WorkerRuntime() override { shutdown(); }

  std::string kind() const override { return "worker"; }
  bool active() const override { return process_ != nullptr; }

  void shutdown() override {
    const std::lock_guard<std::mutex> lock{mutex_};
    stop_locked();
  }

  Result<bool> start(const InvokeOptions& options) override {
    const std::lock_guard<std::mutex> lock{mutex_};
    return ensure_started_locked(options);
  }

  Result<InvocationOutput> invoke(const std::string& operation, const Value::Object& arguments,
                                  operation::TaskContext& context, const InvokeOptions& options,
                                  const HostServices& services) override {
    const std::lock_guard<std::mutex> lock{mutex_};
    if (context.cancellation.is_cancelled()) {
      return Result<InvocationOutput>::failure(failure(ErrorCode::cancelled, "invocation cancelled before start"));
    }
    if (auto started = ensure_started_locked(options); !started.has_value()) {
      return Result<InvocationOutput>::failure(started.error());
    }
    const auto id = ++next_id_;
    const auto request = command::to_json(Value::Object{{"arguments", arguments},
                                                        {"id", static_cast<std::uint64_t>(id)},
                                                        {"operation", operation},
                                                        {"type", "invoke"}});
    if (!request.has_value()) return Result<InvocationOutput>::failure(request.error());
    if (!process_->write_line(request.value())) {
      return Result<InvocationOutput>::failure(dead_locked("the worker closed its input"));
    }
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    std::optional<std::chrono::steady_clock::time_point> cancel_deadline;
    while (true) {
      const auto now = std::chrono::steady_clock::now();
      if (context.cancellation.is_cancelled()) {
        if (!cancel_deadline.has_value()) {
          cancel_deadline = now + std::chrono::seconds{2};
          const auto cancel = command::to_json(
              Value::Object{{"id", static_cast<std::uint64_t>(id)}, {"type", "cancel"}});
          if (cancel.has_value()) static_cast<void>(process_->write_line(cancel.value()));
        } else if (now > *cancel_deadline) {
          stop_locked();
          return Result<InvocationOutput>::failure(
              failure(ErrorCode::cancelled, "invocation cancelled; the worker was stopped"));
        }
      }
      if (now > deadline) {
        const auto tail = process_->stderr_tail();
        stop_locked();
        return Result<InvocationOutput>::failure(failure(
            ErrorCode::script_failed,
            "plugin '" + plugin_.manifest.id + "' timed out after " +
                std::to_string(options.timeout.count()) + " ms and was stopped" +
                (tail.empty() ? std::string{} : "; stderr: " + tail),
            "raise --timeout-ms or fix the plugin"));
      }
      const auto message = process_->read(std::chrono::milliseconds{20});
      if (message.event == detail::ChildProcess::Event::timeout) continue;
      if (message.event == detail::ChildProcess::Event::closed) {
        if (process_->line_too_long()) {
          stop_locked();
          return Result<InvocationOutput>::failure(failure(
              ErrorCode::script_failed, "the worker sent a message over the size limit and was stopped"));
        }
        return Result<InvocationOutput>::failure(dead_locked("the worker exited"));
      }
      const auto parsed = command::parse_json(
          message.line, command::JsonLimits{options.max_message_bytes, 64U});
      const auto* object = parsed.has_value() ? std::get_if<Value::Object>(&parsed.value().data) : nullptr;
      const auto* type = object != nullptr ? text_of(*object, "type") : nullptr;
      if (type == nullptr) {
        stop_locked();
        return Result<InvocationOutput>::failure(failure(
            ErrorCode::script_failed, "plugin protocol error: a message was not a JSON object with a type",
            "the worker was stopped"));
      }
      const auto* message_id = find(*object, "id");
      const auto numeric_id = [&]() -> std::optional<std::uint64_t> {
        if (message_id == nullptr) return std::nullopt;
        if (const auto* u = std::get_if<std::uint64_t>(&message_id->data)) return *u;
        if (const auto* i = std::get_if<std::int64_t>(&message_id->data); i != nullptr && *i >= 0) {
          return static_cast<std::uint64_t>(*i);
        }
        return std::nullopt;
      }();
      if (numeric_id != id) {
        stop_locked();
        return Result<InvocationOutput>::failure(failure(
            ErrorCode::script_failed, "plugin protocol error: message for an unknown invocation id",
            "the worker was stopped"));
      }
      if (*type == "progress") {
        if (negotiated_.capabilities.count("progress") != 0U && context.report_progress) {
          const auto* fraction = find(*object, "fraction");
          double value = 0.0;
          if (fraction != nullptr) {
            if (const auto* d = std::get_if<double>(&fraction->data)) value = *d;
            else if (const auto* i = std::get_if<std::int64_t>(&fraction->data)) value = static_cast<double>(*i);
          }
          const auto* stage = text_of(*object, "message");
          context.report_progress({std::clamp(value, 0.0, 1.0), stage != nullptr ? std::string_view{*stage} : std::string_view{}});
        }
        continue;
      }
      if (*type == "host_request") {
        answer_host_request_locked(*object, services);
        continue;
      }
      if (*type == "result") {
        auto output = parse_invocation_result(*object);
        if (!output.has_value()) {
          stop_locked();
          return Result<InvocationOutput>::failure(output.error());
        }
        return output;
      }
      if (*type == "error") {
        const auto* code = text_of(*object, "code");
        const auto* text = text_of(*object, "message");
        const auto* suggestion = text_of(*object, "suggestion");
        return Result<InvocationOutput>::failure(failure(
            map_code(code != nullptr ? *code : "failed"),
            text != nullptr ? *text : "the plugin reported an error",
            suggestion != nullptr ? *suggestion : std::string{}));
      }
      stop_locked();
      return Result<InvocationOutput>::failure(failure(
          ErrorCode::script_failed, "plugin protocol error: unexpected message type '" + *type + "'",
          "the worker was stopped"));
    }
  }

 private:
  Error dead_locked(const std::string& what) {
    const auto tail = process_ != nullptr ? process_->stderr_tail() : std::string{};
    stop_locked();
    return failure(ErrorCode::script_failed,
                   "plugin '" + plugin_.manifest.id + "' failed: " + what +
                       (tail.empty() ? std::string{} : "; stderr: " + tail),
                   "the worker is restarted on the next invocation");
  }

  void stop_locked() {
    if (process_ == nullptr) return;
    const auto bye = command::to_json(Value::Object{{"type", "shutdown"}});
    if (bye.has_value() && process_->running()) static_cast<void>(process_->write_line(bye.value()));
    process_->stop(std::chrono::milliseconds{500});
    process_.reset();
  }

  void answer_host_request_locked(const Value::Object& request, const HostServices& services) {
    const auto* method = text_of(request, "method");
    const auto* request_id = find(request, "request_id");
    Value::Object response{{"type", "host_response"}};
    if (request_id != nullptr) response.emplace("request_id", *request_id);
    const auto* params_value = find(request, "params");
    const auto* params = params_value != nullptr ? std::get_if<Value::Object>(&params_value->data) : nullptr;
    if (method == nullptr || request_id == nullptr || params == nullptr) {
      response.emplace("ok", false);
      response.emplace("error", "a host request needs method, request_id and params");
    } else if (*method == "selection.atoms" &&
               negotiated_.capabilities.count("host.selection.atoms") == 0U) {
      response.emplace("ok", false);
      response.emplace("error", "capability host.selection.atoms was not negotiated");
    } else if (!services.request) {
      response.emplace("ok", false);
      response.emplace("error", "this host offers no data services");
    } else {
      auto result = services.request(*method, *params);
      if (result.has_value()) {
        response.emplace("ok", true);
        response.emplace("result", std::move(result.value()));
      } else {
        response.emplace("ok", false);
        response.emplace("error", result.error().message);
      }
    }
    auto text = command::to_json(response);
    if (!text.has_value()) {
      text = command::to_json(Value::Object{{"error", "the host result could not be encoded"},
                                            {"ok", false}, {"type", "host_response"},
                                            {"request_id", request_id != nullptr ? *request_id : Value{nullptr}}});
    }
    if (text.has_value()) static_cast<void>(process_->write_line(text.value()));
  }

  Result<bool> ensure_started_locked(const InvokeOptions& options) {
    if (process_ != nullptr && process_->running()) return Result<bool>::success(true);
    process_.reset();
    // The files may have changed since approval: verify again before running.
    const auto verified = verify_approved(plugin_.trust);
    if (!verified.has_value()) return Result<bool>::failure(verified.error());
    std::vector<std::string> argv = plugin_.manifest.worker_command;
    argv.front() = verified.value().entry.string();
    auto started = detail::ChildProcess::start(argv, plugin_.manifest.directory, options.max_message_bytes);
    if (!started.has_value()) return Result<bool>::failure(started.error());
    process_ = std::move(started.value());
    std::vector<Value> capabilities;
    for (const auto& capability : negotiated_.capabilities) capabilities.emplace_back(capability);
    const auto hello = command::to_json(Value::Object{
        {"capabilities", Value::Array{capabilities.begin(), capabilities.end()}},
        {"host", Value::Object{{"name", "molshredder"}}},
        {"protocol", Value::Object{{"major", static_cast<std::uint64_t>(negotiated_.abi_major)},
                                   {"minor", static_cast<std::uint64_t>(negotiated_.abi_minor)}}},
        {"type", "hello"}});
    if (!hello.has_value() || !process_->write_line(hello.value())) {
      const auto error = dead_locked("the worker did not accept the handshake");
      return Result<bool>::failure(error);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (true) {
      const auto message = process_->read(std::chrono::milliseconds{20});
      if (message.event == detail::ChildProcess::Event::closed) {
        return Result<bool>::failure(dead_locked("the worker exited during the handshake"));
      }
      if (message.event == detail::ChildProcess::Event::timeout) {
        if (std::chrono::steady_clock::now() > deadline) {
          return Result<bool>::failure(dead_locked("the handshake timed out"));
        }
        continue;
      }
      const auto reply = command::parse_json(message.line, command::JsonLimits{options.max_message_bytes, 16U});
      const auto verdict = check_hello(reply);
      if (!verdict.has_value()) {
        stop_locked();
        return Result<bool>::failure(verdict.error());
      }
      return Result<bool>::success(true);
    }
  }

  Result<bool> check_hello(const Result<Value>& reply) const {
    const auto bad = [&](const std::string& why) {
      return Result<bool>::failure(failure(
          ErrorCode::script_failed, "plugin '" + plugin_.manifest.id + "' handshake failed: " + why,
          "rebuild the plugin against this SDK"));
    };
    const auto* object = reply.has_value() ? std::get_if<Value::Object>(&reply.value().data) : nullptr;
    const auto* type = object != nullptr ? text_of(*object, "type") : nullptr;
    if (type == nullptr || *type != "hello") return bad("the first message was not a hello");
    const auto* protocol = find(*object, "protocol");
    const auto* protocol_object = protocol != nullptr ? std::get_if<Value::Object>(&protocol->data) : nullptr;
    const auto unsigned_of = [](const Value::Object& o, const char* key) -> std::optional<std::uint64_t> {
      const auto it = o.find(key);
      if (it == o.end()) return std::nullopt;
      if (const auto* u = std::get_if<std::uint64_t>(&it->second.data)) return *u;
      if (const auto* i = std::get_if<std::int64_t>(&it->second.data); i != nullptr && *i >= 0) return static_cast<std::uint64_t>(*i);
      return std::nullopt;
    };
    if (protocol_object == nullptr) return bad("no protocol version");
    const auto major = unsigned_of(*protocol_object, "major");
    const auto minor = unsigned_of(*protocol_object, "minor");
    if (!major || !minor || *major != host_.abi_major || *minor > host_.abi_minor) {
      return bad("protocol version is not compatible with this host");
    }
    const auto* plugin = find(*object, "plugin");
    const auto* plugin_object = plugin != nullptr ? std::get_if<Value::Object>(&plugin->data) : nullptr;
    const auto* id = plugin_object != nullptr ? text_of(*plugin_object, "id") : nullptr;
    const auto* version = plugin_object != nullptr ? text_of(*plugin_object, "version") : nullptr;
    if (id == nullptr || *id != plugin_.manifest.id || version == nullptr || *version != plugin_.manifest.version) {
      return bad("the worker's identity does not match its manifest");
    }
    const auto* operations = find(*object, "operations");
    const auto* list = operations != nullptr ? std::get_if<Value::Array>(&operations->data) : nullptr;
    std::set<std::string> announced;
    if (list != nullptr) {
      for (const auto& element : *list) {
        if (const auto* name = std::get_if<std::string>(&element.data)) announced.insert(*name);
      }
    }
    std::set<std::string> declared;
    for (const auto& operation : plugin_.manifest.operations) declared.insert(operation.name);
    if (announced != declared) return bad("the worker's operations differ from its manifest");
    return Result<bool>::success(true);
  }

  VerifiedPlugin plugin_;
  Negotiation negotiated_;
  HostInfo host_;
  std::mutex mutex_;
  std::unique_ptr<detail::ChildProcess> process_;
  std::uint64_t next_id_{};
};

}  // namespace

std::unique_ptr<PluginRuntime> make_worker_runtime(VerifiedPlugin plugin, Negotiation negotiated,
                                                   HostInfo host) {
  return std::make_unique<WorkerRuntime>(std::move(plugin), std::move(negotiated), std::move(host));
}

Result<InvocationOutput> parse_invocation_result(const Value::Object& message) {
  const auto bad = [](const std::string& why) {
    return Result<InvocationOutput>::failure(Error{
        ErrorCode::script_failed, "plugin result is invalid: " + why, "the worker was stopped", {}});
  };
  InvocationOutput output;
  if (const auto* summary = text_of(message, "summary")) {
    if (summary->size() > 4096U) return bad("summary is longer than 4096 bytes");
    output.summary = *summary;
  }
  if (const auto* fields = find(message, "fields")) {
    const auto* object = std::get_if<Value::Object>(&fields->data);
    if (object == nullptr) return bad("fields must be an object");
    output.fields = *object;
  }
  if (const auto* table = find(message, "table")) {
    const auto* object = std::get_if<Value::Object>(&table->data);
    const auto* columns = object != nullptr ? find(*object, "columns") : nullptr;
    const auto* rows = object != nullptr ? find(*object, "rows") : nullptr;
    const auto* column_list = columns != nullptr ? std::get_if<Value::Array>(&columns->data) : nullptr;
    const auto* row_list = rows != nullptr ? std::get_if<Value::Array>(&rows->data) : nullptr;
    if (column_list == nullptr || row_list == nullptr || column_list->empty() ||
        column_list->size() > 64U || row_list->size() > 100'000U) {
      return bad("table needs 1-64 columns and at most 100000 rows");
    }
    command::Table parsed;
    for (const auto& column : *column_list) {
      const auto* name = std::get_if<std::string>(&column.data);
      if (name == nullptr) return bad("table column names must be strings");
      parsed.columns.push_back(*name);
    }
    for (const auto& row : *row_list) {
      const auto* cells = std::get_if<Value::Array>(&row.data);
      if (cells == nullptr || cells->size() != parsed.columns.size()) return bad("a table row has the wrong width");
      parsed.rows.push_back(*cells);
    }
    output.table = std::move(parsed);
  }
  return Result<InvocationOutput>::success(std::move(output));
}

}  // namespace molshredder::plugin
