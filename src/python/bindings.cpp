#include "bindings.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <pybind11/stl.h>
#include <pybind11/eval.h>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/automation/python_script.hpp"
#include "molshredder/command/result.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/version.hpp"

namespace py = pybind11;

namespace molshredder::python {
namespace {

struct RuntimeState {
  explicit RuntimeState(std::uint64_t next_generation)
      : workspace{std::make_shared<application::Workspace>()},
        registry{application::make_default_registry(workspace)},
        generation{next_generation} {
    if (const auto error =
            automation::register_python_script_command(registry, workspace);
        error.has_value()) {
      throw std::runtime_error{error->message};
    }
  }

  std::shared_ptr<application::Workspace> workspace;
  command::Registry registry;
  std::uint64_t generation{};
};

std::shared_ptr<RuntimeState>& fallback_runtime_holder() {
  static auto* holder = new std::shared_ptr<RuntimeState>{
      std::make_shared<RuntimeState>(1U)};
  return *holder;
}

RuntimeState& fallback_runtime() { return *fallback_runtime_holder(); }

std::shared_ptr<application::Workspace> current_workspace() {
  if (auto active = automation::detail::active_workspace(); active) {
    return active;
  }
  return fallback_runtime().workspace;
}

class CoordinateView {
 public:
  CoordinateView(std::shared_ptr<const model::CoordinateFrame> frame,
                 std::uint64_t object_id,
                 std::uint64_t coordinate_source_revision,
                 std::uint64_t coordinate_revision)
      : frame_{std::move(frame)},
        object_id_{object_id},
        coordinate_source_revision_{coordinate_source_revision},
        coordinate_revision_{coordinate_revision} {}

  [[nodiscard]] py::buffer_info buffer() const {
    return std::visit(
        [](const auto& values) {
          using Vector = std::decay_t<decltype(values)>;
          using Vec = typename Vector::value_type;
          using Scalar = decltype(Vec::x);
          return py::buffer_info{
              const_cast<Scalar*>(values.empty() ? nullptr : &values[0].x),
              static_cast<py::ssize_t>(sizeof(Scalar)),
              py::format_descriptor<Scalar>::format(), 2,
              std::vector<py::ssize_t>{
                  static_cast<py::ssize_t>(values.size()), 3},
              std::vector<py::ssize_t>{
                  static_cast<py::ssize_t>(sizeof(Vec)),
                  static_cast<py::ssize_t>(sizeof(Scalar))},
              true};
        },
        frame_->positions().values());
  }

  [[nodiscard]] std::size_t atom_count() const noexcept {
    return frame_->atom_count();
  }
  [[nodiscard]] std::uint64_t object_id() const noexcept { return object_id_; }
  [[nodiscard]] std::uint64_t coordinate_source_revision() const noexcept {
    return coordinate_source_revision_;
  }
  [[nodiscard]] std::uint64_t coordinate_revision() const noexcept {
    return coordinate_revision_;
  }
  [[nodiscard]] std::string precision() const {
    return frame_->positions().precision() == model::CoordinatePrecision::float32
               ? "float32"
               : "float64";
  }

 private:
  std::shared_ptr<const model::CoordinateFrame> frame_;
  std::uint64_t object_id_{};
  std::uint64_t coordinate_source_revision_{};
  std::uint64_t coordinate_revision_{};
};

py::object to_python(const command::Value& value);

py::dict object_to_python(const command::Value::Object& object) {
  py::dict result;
  for (const auto& [name, value] : object) {
    result[py::str{name}] = to_python(value);
  }
  return result;
}

py::object to_python(const command::Value& value) {
  return std::visit(
      [](const auto& item) -> py::object {
        using Item = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Item, std::nullptr_t>) {
          return py::none();
        } else if constexpr (std::is_same_v<Item, bool> ||
                             std::is_same_v<Item, std::int64_t> ||
                             std::is_same_v<Item, std::uint64_t> ||
                             std::is_same_v<Item, double> ||
                             std::is_same_v<Item, std::string>) {
          return py::cast(item);
        } else if constexpr (std::is_same_v<Item, command::Number>) {
          return py::cast(item.value);
        } else if constexpr (std::is_same_v<Item, command::Value::Array>) {
          py::list result;
          for (const auto& nested : item) result.append(to_python(nested));
          return result;
        } else {
          return object_to_python(item);
        }
      },
      value.data);
}

py::dict table_to_python(const command::Table& table) {
  py::dict result;
  result["columns"] = table.columns;
  py::list rows;
  for (const auto& row : table.rows) {
    py::list converted;
    for (const auto& value : row) converted.append(to_python(value));
    rows.append(std::move(converted));
  }
  result["rows"] = std::move(rows);
  return result;
}

py::dict envelope_to_python(const command::ResultEnvelope& envelope) {
  py::dict result;
  result["schema_version"] = envelope.schema_version;
  result["status"] = envelope.succeeded() ? "ok" : "error";
  result["command"] = envelope.canonical_command;
  if (envelope.succeeded()) {
    const auto& response = std::get<command::Response>(envelope.payload);
    result["summary"] = response.summary;
    auto data = object_to_python(response.fields);
    if (response.table.has_value()) {
      data["table"] = table_to_python(*response.table);
    }
    result["data"] = std::move(data);
  } else {
    const auto& failure = std::get<operation::Error>(envelope.payload);
    py::dict error;
    error["code"] = operation::to_string(failure.code);
    error["message"] = failure.message;
    error["suggestion"] = failure.suggestion;
    error["details"] = failure.details;
    result["error"] = std::move(error);
  }
  return result;
}

void emit_operation_event(const command::ResultEnvelope& envelope,
                          const py::dict& result) {
  py::dict event;
  event["command"] = envelope.canonical_command;
  event["status"] = envelope.succeeded() ? "ok" : "error";
  event["result"] = py::dict{result};
  py::module_::import("molshredder").attr("_emit_operation")(event);
}

class AsyncIsolatedScriptTask {
 public:
  AsyncIsolatedScriptTask(std::shared_ptr<RuntimeState> runtime,
                          command::Arguments arguments)
      : runtime_{std::move(runtime)} {
    worker_ = std::thread{
        [this, arguments = std::move(arguments)]() mutable {
          operation::TaskContext context{
              cancellation_, [this](const operation::ProgressUpdate& update) {
                progress_.store(update.fraction);
              }};
          const application::Dispatcher dispatcher{runtime_->registry};
          auto outcome = dispatcher.dispatch(
              command::Invocation{"script run-isolated",
                                  std::move(arguments)},
              context);
          {
            std::scoped_lock lock{mutex_};
            outcome_ = std::move(outcome);
          }
          completion_.notify_all();
        }};
  }

  ~AsyncIsolatedScriptTask() {
    cancellation_.request_cancel();
    join_worker();
  }

  AsyncIsolatedScriptTask(const AsyncIsolatedScriptTask&) = delete;
  AsyncIsolatedScriptTask& operator=(const AsyncIsolatedScriptTask&) = delete;

  [[nodiscard]] bool done() const {
    std::scoped_lock lock{mutex_};
    return outcome_.has_value();
  }

  [[nodiscard]] double progress() const noexcept { return progress_.load(); }

  bool cancel() {
    const auto already_done = done();
    if (!already_done) cancellation_.request_cancel();
    return !already_done;
  }

  py::dict result(std::optional<std::uint64_t> timeout_ms) {
    std::unique_lock lock{mutex_};
    bool ready = outcome_.has_value();
    if (!ready) {
      py::gil_scoped_release release;
      if (timeout_ms.has_value()) {
        ready = completion_.wait_for(
            lock, std::chrono::milliseconds{*timeout_ms},
            [this] { return outcome_.has_value(); });
      } else {
        completion_.wait(lock, [this] { return outcome_.has_value(); });
        ready = true;
      }
    }
    if (!ready) {
      throw std::runtime_error{"asynchronous operation result timed out"};
    }
    const auto envelope = outcome_->envelope;
    lock.unlock();
    join_worker();
    auto converted = envelope_to_python(envelope);
    if (!event_emitted_) {
      emit_operation_event(envelope, converted);
      event_emitted_ = true;
    }
    return converted;
  }

  void close() {
    cancellation_.request_cancel();
    join_worker();
  }

 private:
  void join_worker() {
    if (!worker_.joinable()) return;
    if (Py_IsInitialized() != 0 && PyGILState_Check() != 0) {
      py::gil_scoped_release release;
      worker_.join();
      return;
    }
    worker_.join();
  }

  std::shared_ptr<RuntimeState> runtime_;
  operation::CancellationToken cancellation_;
  std::thread worker_;
  mutable std::mutex mutex_;
  std::condition_variable completion_;
  std::optional<application::DispatchOutcome> outcome_;
  std::atomic<double> progress_{0.0};
  bool event_emitted_{};
};

command::Registry& fallback_registry() { return fallback_runtime().registry; }

const command::Registry& current_registry() {
  if (const auto* active = automation::detail::active_registry();
      active != nullptr) {
    return *active;
  }
  return fallback_registry();
}

py::dict invoke(std::string command_name, command::Arguments arguments) {
  const auto& registry = current_registry();
  const application::Dispatcher dispatcher{registry};
  operation::TaskContext context;
  const auto outcome = dispatcher.dispatch(
      command::Invocation{std::move(command_name), std::move(arguments)},
      context);
  automation::detail::record_nested_invocation(registry, outcome);
  auto result = envelope_to_python(outcome.envelope);
  emit_operation_event(outcome.envelope, result);
  return result;
}

py::dict runtime_info() {
  py::dict result;
  const auto active = automation::detail::active_registry() != nullptr;
  result["mode"] = active ? "embedded" : "headless";
  result["generation"] = fallback_runtime().generation;
  result["workspace_active"] = current_workspace()->active_object() != nullptr;
  result["reset_allowed"] = !active;
  return result;
}

py::dict reset_runtime() {
  if (automation::detail::active_registry() != nullptr) {
    throw std::runtime_error{
        "the embedded host owns the active Workspace; reset is not allowed"};
  }
  const auto next_generation = fallback_runtime().generation + 1U;
  fallback_runtime_holder() = std::make_shared<RuntimeState>(next_generation);
  return runtime_info();
}

std::shared_ptr<CoordinateView> coordinate_view(std::size_t frame_index) {
  const auto workspace = current_workspace();
  const auto* object = workspace->active_object();
  if (object == nullptr || object->system == nullptr) {
    throw std::runtime_error{"coordinate_view requires an active molecular object"};
  }
  const auto frame = object->system->coordinates()->read_frame(frame_index);
  if (!frame.has_value()) {
    throw std::runtime_error{frame.error().message};
  }
  return std::make_shared<CoordinateView>(
      frame.value(), object->id, object->coordinate_source_revision,
      object->coordinate_revision);
}

py::dict run_script(const std::string& path,
                    const std::vector<std::string>& arguments,
                    const std::optional<std::string>& working_directory,
                    bool trusted) {
  const auto arguments_json =
      py::module_::import("json").attr("dumps")(arguments).cast<std::string>();
  command::Arguments request{{"path", path},
                             {"arguments-json", arguments_json},
                             {"trust", trusted ? "true" : "false"}};
  if (working_directory.has_value()) {
    request.emplace("working-directory", *working_directory);
  }
  return invoke("script run", std::move(request));
}

py::dict run_script_isolated(
    const std::string& path, const std::vector<std::string>& arguments,
    const std::optional<std::string>& working_directory, bool trusted,
    std::uint64_t timeout_ms, std::size_t max_output_bytes,
    const std::string& environment_policy) {
  const auto arguments_json =
      py::module_::import("json").attr("dumps")(arguments).cast<std::string>();
  command::Arguments request{
      {"path", path},
      {"arguments-json", arguments_json},
      {"trust", trusted ? "true" : "false"},
      {"timeout-ms", std::to_string(timeout_ms)},
      {"max-output-bytes", std::to_string(max_output_bytes)},
      {"environment-policy", environment_policy}};
  if (working_directory.has_value()) {
    request.emplace("working-directory", *working_directory);
  }
  return invoke("script run-isolated", std::move(request));
}

std::shared_ptr<AsyncIsolatedScriptTask> run_script_isolated_async(
    const std::string& path, const std::vector<std::string>& arguments,
    const std::optional<std::string>& working_directory, bool trusted,
    std::uint64_t timeout_ms, std::size_t max_output_bytes,
    const std::string& environment_policy) {
  if (automation::detail::active_registry() != nullptr) {
    throw std::runtime_error{
        "asynchronous tasks cannot outlive an embedded host invocation"};
  }
  const auto arguments_json =
      py::module_::import("json").attr("dumps")(arguments).cast<std::string>();
  command::Arguments request{
      {"path", path},
      {"arguments-json", arguments_json},
      {"trust", trusted ? "true" : "false"},
      {"timeout-ms", std::to_string(timeout_ms)},
      {"max-output-bytes", std::to_string(max_output_bytes)},
      {"environment-policy", environment_policy}};
  if (working_directory.has_value()) {
    request.emplace("working-directory", *working_directory);
  }
  return std::make_shared<AsyncIsolatedScriptTask>(fallback_runtime_holder(),
                                                    std::move(request));
}

}  // namespace

void populate_module(py::module_& module) {
  module.doc() = "MolShredder typed operation and automation bindings";
  module.attr("__version__") = molshredder::version();
  module.def("invoke", &invoke, py::arg("command_name"),
             py::arg("arguments") = command::Arguments{},
             "Invoke a command through the active C++ dispatcher.");
  module.def("run_script", &run_script, py::arg("path"),
             py::arg("arguments") = std::vector<std::string>{},
             py::arg("working_directory") = py::none(),
             py::arg("trusted") = false,
             "Explicitly execute a trusted local Python script.");
  module.def("run_script_isolated", &run_script_isolated, py::arg("path"),
             py::arg("arguments") = std::vector<std::string>{},
             py::arg("working_directory") = py::none(),
             py::arg("trusted") = false, py::arg("timeout_ms") = 30'000U,
             py::arg("max_output_bytes") = 8U * 1024U * 1024U,
             py::arg("environment_policy") = "minimal",
             "Execute a trusted script in a killable isolated child process.");
  py::class_<AsyncIsolatedScriptTask,
             std::shared_ptr<AsyncIsolatedScriptTask>>(
      module, "OperationTask")
      .def_property_readonly("done", &AsyncIsolatedScriptTask::done)
      .def_property_readonly("progress", &AsyncIsolatedScriptTask::progress)
      .def("cancel", &AsyncIsolatedScriptTask::cancel)
      .def("result", &AsyncIsolatedScriptTask::result,
           py::arg("timeout_ms") = py::none())
      .def("close", &AsyncIsolatedScriptTask::close);
  module.def(
      "run_script_isolated_async", &run_script_isolated_async,
      py::arg("path"), py::arg("arguments") = std::vector<std::string>{},
      py::arg("working_directory") = py::none(), py::arg("trusted") = false,
      py::arg("timeout_ms") = 30'000U,
      py::arg("max_output_bytes") = 8U * 1024U * 1024U,
      py::arg("environment_policy") = "minimal",
      "Schedule an isolated script with progress and cancellation.");
  module.def("runtime_info", &runtime_info,
             "Return the current headless or embedded runtime lifecycle state.");
  module.def("reset_runtime", &reset_runtime,
             "Replace the module-owned headless Workspace deterministically.");
  py::class_<CoordinateView, std::shared_ptr<CoordinateView>>(
      module, "CoordinateView", py::buffer_protocol())
      .def_buffer(&CoordinateView::buffer)
      .def_property_readonly("atom_count", &CoordinateView::atom_count)
      .def_property_readonly("object_id", &CoordinateView::object_id)
      .def_property_readonly("coordinate_source_revision",
                             &CoordinateView::coordinate_source_revision)
      .def_property_readonly("coordinate_revision",
                             &CoordinateView::coordinate_revision)
      .def_property_readonly("precision", &CoordinateView::precision);
  module.def("coordinate_view", &coordinate_view,
             py::arg("frame_index") = 0U,
             "Return a read-only, NumPy-compatible coordinate buffer view.");
  py::exec(R"PY(
_operation_subscribers = {}
_operation_callback_errors = []
_next_operation_subscription = 1
_delivering_operation_event = False

def subscribe(callback):
    """Subscribe to completed canonical operations and return a stable token."""
    global _next_operation_subscription
    if not callable(callback):
        raise TypeError("callback must be callable")
    token = _next_operation_subscription
    _next_operation_subscription += 1
    _operation_subscribers[token] = callback
    return token

def unsubscribe(token):
    """Remove a subscription; return whether the token existed."""
    return _operation_subscribers.pop(token, None) is not None

def callback_errors(clear=True):
    """Return callback failures without changing operation results."""
    result = list(_operation_callback_errors)
    if clear:
        _operation_callback_errors.clear()
    return result

def _emit_operation(event):
    global _delivering_operation_event
    if _delivering_operation_event:
        return
    _delivering_operation_event = True
    try:
        for token, callback in tuple(_operation_subscribers.items()):
            if token not in _operation_subscribers:
                continue
            try:
                callback(dict(event))
            except BaseException as error:
                _operation_callback_errors.append({
                    "token": token,
                    "type": type(error).__name__,
                    "message": str(error),
                })
    finally:
        _delivering_operation_event = False
)PY",
           module.attr("__dict__"));
}

}  // namespace molshredder::python
