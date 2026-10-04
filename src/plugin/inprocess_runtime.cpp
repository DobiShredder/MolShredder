#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <set>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "molshredder/command/json.hpp"
#include "molshredder/plugin/molshredder_plugin.h"
#include "molshredder/plugin/runtime.hpp"

namespace molshredder::plugin {
namespace {

using command::Value;
using operation::Error;
using operation::ErrorCode;
using operation::Result;

Error failure(ErrorCode code, std::string message, std::string suggestion = {}) {
  return {code, std::move(message), std::move(suggestion)};
}

class Library {
 public:
  Library() = default;
  Library(const Library&) = delete;
  Library& operator=(const Library&) = delete;
  Library(Library&& other) noexcept : handle_{std::exchange(other.handle_, nullptr)} {}
  Library& operator=(Library&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }
  ~Library() { close(); }

  Result<bool> open(const std::filesystem::path& path) {
#if defined(_WIN32)
    handle_ = LoadLibraryW(path.c_str());
    if (handle_ == nullptr) {
      return Result<bool>::failure(failure(ErrorCode::script_failed, "cannot load " + path.string()));
    }
#else
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle_ == nullptr) {
      const char* reason = dlerror();
      return Result<bool>::failure(failure(ErrorCode::script_failed,
                                           "cannot load " + path.string() + ": " + (reason != nullptr ? reason : "unknown error")));
    }
#endif
    return Result<bool>::success(true);
  }

  void* symbol(const char* name) const {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
    return dlsym(handle_, name);
#endif
  }

  void close() {
    if (handle_ == nullptr) return;
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
  }

 private:
  void* handle_{};
};

// Callback context. The plugin only ever sees this while `invoke` runs.
struct ActiveInvocation {
  operation::TaskContext* context{};
  const HostServices* services{};
  const Negotiation* negotiated{};
};

thread_local ActiveInvocation* t_active = nullptr;

int host_is_cancelled(void*) {
  return t_active != nullptr && t_active->context != nullptr && t_active->context->cancellation.is_cancelled() ? 1 : 0;
}

void host_report_progress(void*, double fraction, const char* message) {
  if (t_active == nullptr || t_active->context == nullptr || !t_active->context->report_progress ||
      t_active->negotiated->capabilities.count("progress") == 0U) {
    return;
  }
  t_active->context->report_progress(
      {std::clamp(fraction, 0.0, 1.0), message != nullptr ? std::string_view{message} : std::string_view{}});
}

char* duplicate(const std::string& text) {
  char* copy = static_cast<char*>(std::malloc(text.size() + 1U));
  if (copy == nullptr) return nullptr;
  std::memcpy(copy, text.c_str(), text.size() + 1U);
  return copy;
}

int host_request(void*, const char* method, const char* params_json, char** result_json) {
  const auto fail = [&](const std::string& message) {
    if (result_json != nullptr) *result_json = duplicate(message);
    return 1;
  };
  if (result_json == nullptr || method == nullptr || params_json == nullptr) return 1;
  *result_json = nullptr;
  if (t_active == nullptr) return fail("host requests are only valid during an invocation");
  if (std::string_view{method} == "selection.atoms" &&
      t_active->negotiated->capabilities.count("host.selection.atoms") == 0U) {
    return fail("capability host.selection.atoms was not negotiated");
  }
  if (!t_active->services->request) return fail("this host offers no data services");
  const auto params = command::parse_json(params_json, command::JsonLimits{1024U * 1024U, 16U});
  const auto* object = params.has_value() ? std::get_if<Value::Object>(&params.value().data) : nullptr;
  if (object == nullptr) return fail("params must be a JSON object");
  const auto result = t_active->services->request(method, *object);
  if (!result.has_value()) return fail(result.error().message);
  const auto text = command::to_json(result.value());
  if (!text.has_value()) return fail("the host result could not be encoded");
  *result_json = duplicate(text.value());
  return *result_json != nullptr ? 0 : 1;
}

void host_free_string(char* text) { std::free(text); }

class InProcessRuntime final : public PluginRuntime {
 public:
  InProcessRuntime(VerifiedPlugin plugin, Negotiation negotiated, HostInfo host)
      : plugin_{std::move(plugin)}, negotiated_{std::move(negotiated)}, host_{std::move(host)} {}
  ~InProcessRuntime() override { shutdown(); }

  std::string kind() const override { return "in_process"; }
  bool active() const override { return loaded_; }

  void shutdown() override {
    const std::lock_guard<std::mutex> lock{mutex_};
    unload_locked();
  }

  Result<bool> start(const InvokeOptions&) override {
    const std::lock_guard<std::mutex> lock{mutex_};
    return ensure_loaded_locked();
  }

  Result<InvocationOutput> invoke(const std::string& operation, const Value::Object& arguments,
                                  operation::TaskContext& context, const InvokeOptions& options,
                                  const HostServices& services) override {
    const std::lock_guard<std::mutex> lock{mutex_};
    if (context.cancellation.is_cancelled()) {
      return Result<InvocationOutput>::failure(failure(ErrorCode::cancelled, "invocation cancelled before start"));
    }
    if (auto loaded = ensure_loaded_locked(); !loaded.has_value()) {
      return Result<InvocationOutput>::failure(loaded.error());
    }
    const auto request = command::to_json(arguments);
    if (!request.has_value()) return Result<InvocationOutput>::failure(request.error());
    ActiveInvocation active{&context, &services, &negotiated_};
    char* raw = nullptr;
    t_active = &active;
    const int code = api_.invoke(api_.plugin_state, operation.c_str(), request.value().c_str(), &raw);
    t_active = nullptr;
    std::string text;
    if (raw != nullptr) {
      text = raw;
      api_.free_string(api_.plugin_state, raw);
    }
    if (text.empty()) {
      return Result<InvocationOutput>::failure(failure(
          ErrorCode::script_failed, "plugin '" + plugin_.manifest.id + "' returned no result"));
    }
    const auto parsed = command::parse_json(text, command::JsonLimits{options.max_message_bytes, 64U});
    const auto* object = parsed.has_value() ? std::get_if<Value::Object>(&parsed.value().data) : nullptr;
    if (object == nullptr) {
      return Result<InvocationOutput>::failure(failure(
          ErrorCode::script_failed, "plugin result is invalid: the result is not a JSON object"));
    }
    if (code != 0) {
      const auto text_field = [&](const char* key) -> std::string {
        const auto found = object->find(key);
        const auto* value = found == object->end() ? nullptr : std::get_if<std::string>(&found->second.data);
        return value != nullptr ? *value : std::string{};
      };
      const auto name = text_field("code");
      const auto mapped = name == "invalid_argument" ? ErrorCode::invalid_argument
                          : name == "not_found"      ? ErrorCode::not_found
                          : name == "unsupported"    ? ErrorCode::unsupported
                          : name == "cancelled"      ? ErrorCode::cancelled
                                                     : ErrorCode::script_failed;
      auto message = text_field("message");
      return Result<InvocationOutput>::failure(failure(
          mapped, message.empty() ? "the plugin reported an error" : std::move(message), text_field("suggestion")));
    }
    return parse_invocation_result(*object);
  }

 private:
  void unload_locked() {
    if (!loaded_) return;
    if (api_.shutdown != nullptr) api_.shutdown(api_.plugin_state);
    library_.close();
    api_ = molshredder_plugin_api{};
    loaded_ = false;
  }

  Result<bool> ensure_loaded_locked() {
    if (loaded_) return Result<bool>::success(true);
    // The approval pins this exact file; check it again right before loading.
    const auto verified = verify_approved(plugin_.trust);
    if (!verified.has_value()) return Result<bool>::failure(verified.error());
    if (!plugin_.trust.allow_in_process) {
      return Result<bool>::failure(failure(
          ErrorCode::invalid_argument,
          "plugin '" + plugin_.manifest.id + "' was not approved to run in-process",
          "approve it with --allow-in-process true"));
    }
    Library library;
    if (auto opened = library.open(verified.value().entry); !opened.has_value()) {
      return Result<bool>::failure(opened.error());
    }
    using Entry = int (*)(const molshredder_host_api*, molshredder_plugin_api*);
    const auto entry = reinterpret_cast<Entry>(library.symbol(MOLSHREDDER_PLUGIN_ENTRY_SYMBOL));
    if (entry == nullptr) {
      return Result<bool>::failure(failure(ErrorCode::script_failed,
                                           "the library does not export " MOLSHREDDER_PLUGIN_ENTRY_SYMBOL));
    }
    molshredder_host_api host{};
    host.struct_size = sizeof(host);
    host.abi_major = host_.abi_major;
    host.abi_minor = host_.abi_minor;
    host.host_context = nullptr;
    host.is_cancelled = &host_is_cancelled;
    host.report_progress = &host_report_progress;
    host.request = &host_request;
    host.free_string = &host_free_string;
    host_api_ = host;
    molshredder_plugin_api api{};
    api.struct_size = sizeof(api);
    if (entry(&host_api_, &api) != 0) {
      return Result<bool>::failure(failure(ErrorCode::script_failed, "plugin entry reported a failure"));
    }
    const auto reject = [&](const std::string& why) {
      if (api.shutdown != nullptr) api.shutdown(api.plugin_state);
      return Result<bool>::failure(failure(
          ErrorCode::script_failed, "plugin '" + plugin_.manifest.id + "' handshake failed: " + why,
          "rebuild the plugin against this SDK"));
    };
    if (api.struct_size < offsetof(molshredder_plugin_api, shutdown) + sizeof(api.shutdown)) {
      return reject("the plugin API structure is too small");
    }
    if (api.abi_major != host_.abi_major || api.abi_minor > host_.abi_minor) {
      return reject("the plugin ABI version is not compatible with this host");
    }
    if (api.describe == nullptr || api.invoke == nullptr || api.free_string == nullptr || api.shutdown == nullptr) {
      return reject("a required function is missing");
    }
    const char* description = api.describe(api.plugin_state);
    const auto parsed = command::parse_json(description != nullptr ? description : "", command::JsonLimits{64U * 1024U, 16U});
    const auto* object = parsed.has_value() ? std::get_if<Value::Object>(&parsed.value().data) : nullptr;
    if (object == nullptr) return reject("describe did not return a JSON object");
    const auto plugin_it = object->find("plugin");
    const auto* plugin_object = plugin_it == object->end() ? nullptr : std::get_if<Value::Object>(&plugin_it->second.data);
    const auto text_of = [](const Value::Object* o, const char* key) -> const std::string* {
      if (o == nullptr) return nullptr;
      const auto found = o->find(key);
      return found == o->end() ? nullptr : std::get_if<std::string>(&found->second.data);
    };
    const auto* id = text_of(plugin_object, "id");
    const auto* version = text_of(plugin_object, "version");
    if (id == nullptr || *id != plugin_.manifest.id || version == nullptr || *version != plugin_.manifest.version) {
      return reject("the library's identity does not match its manifest");
    }
    std::set<std::string> announced;
    const auto operations = object->find("operations");
    if (const auto* list = operations == object->end() ? nullptr : std::get_if<Value::Array>(&operations->second.data)) {
      for (const auto& element : *list) {
        if (const auto* name = std::get_if<std::string>(&element.data)) announced.insert(*name);
      }
    }
    std::set<std::string> declared;
    for (const auto& operation : plugin_.manifest.operations) declared.insert(operation.name);
    if (announced != declared) return reject("the library's operations differ from its manifest");
    api_ = api;
    library_ = std::move(library);
    loaded_ = true;
    return Result<bool>::success(true);
  }

  VerifiedPlugin plugin_;
  Negotiation negotiated_;
  HostInfo host_;
  std::mutex mutex_;
  Library library_;
  molshredder_host_api host_api_{};
  molshredder_plugin_api api_{};
  bool loaded_{};
};

}  // namespace

std::unique_ptr<PluginRuntime> make_in_process_runtime(VerifiedPlugin plugin, Negotiation negotiated,
                                                       HostInfo host) {
  return std::make_unique<InProcessRuntime>(std::move(plugin), std::move(negotiated), std::move(host));
}

}  // namespace molshredder::plugin
