#include "molshredder/automation/python_script.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include "molshredder/application/dispatcher.hpp"

namespace py = pybind11;

namespace molshredder::automation {
namespace {

namespace fs = std::filesystem;

struct ExecutionTrace {
  std::vector<std::string> invocations;
  std::size_t mutation_count{};
};

thread_local const command::Registry* current_registry = nullptr;
thread_local ExecutionTrace* current_trace = nullptr;
thread_local bool executing_script = false;

std::mutex& execution_mutex() {
  static std::mutex mutex;
  return mutex;
}

void ensure_interpreter() {
  if (Py_IsInitialized() != 0) return;
  // The CLI owns the process and intentionally leaves finalization to process
  // teardown. Finalizing CPython from a static destructor races other static
  // registries and extension objects on several supported runtimes.
  py::initialize_interpreter();
}

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return {operation::ErrorCode::invalid_argument, std::move(message),
          std::move(suggestion)};
}

operation::Result<std::pair<fs::path, std::string>> read_source(
    const PythonScriptRequest& request) {
  using Result = operation::Result<std::pair<fs::path, std::string>>;
  if (!request.trusted) {
    return Result::failure(invalid(
        "Python script execution requires explicit trust",
        "review the local script and pass --trust true"));
  }
  if (request.path.empty()) {
    return Result::failure(
        invalid("script path must not be empty", "provide a local .py file"));
  }
  std::error_code error;
  auto path = fs::canonical(fs::path{request.path}, error);
  if (error || !fs::is_regular_file(path, error) || error) {
    return Result::failure(invalid(
        "script path is not a readable regular file: " + request.path,
        "provide an existing local .py file"));
  }
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  if (extension != ".py") {
    return Result::failure(invalid(
        "first script runtime accepts only .py files",
        "use a .py file; PML support is tracked separately"));
  }
  const auto size = fs::file_size(path, error);
  if (error || size > request.max_source_bytes) {
    return Result::failure(invalid(
        "script exceeds the configured source-size budget",
        "use a script no larger than " +
            std::to_string(request.max_source_bytes) + " bytes"));
  }
  std::ifstream stream{path, std::ios::binary};
  if (!stream) {
    return Result::failure(invalid("failed to open script: " + path.string(),
                                   "check file permissions"));
  }
  std::string source{std::istreambuf_iterator<char>{stream},
                     std::istreambuf_iterator<char>{}};
  if (stream.bad()) {
    return Result::failure(invalid("failed while reading script: " +
                                       path.string(),
                                   "check the file and storage device"));
  }
  return Result::success({std::move(path), std::move(source)});
}

fs::path resolve_working_directory(const PythonScriptRequest& request,
                                   const fs::path& script_path) {
  const auto requested = request.working_directory.has_value()
                             ? fs::path{*request.working_directory}
                             : script_path.parent_path();
  std::error_code error;
  const auto canonical = fs::canonical(requested, error);
  if (error || !fs::is_directory(canonical, error) || error) return {};
  return canonical;
}

std::string joined_invocations(const ExecutionTrace& trace) {
  std::string result;
  for (const auto& invocation : trace.invocations) {
    if (!result.empty()) result += '\n';
    result += invocation;
  }
  return result;
}

operation::Error script_error(
    operation::ErrorCode code, std::string message, std::string suggestion,
    const fs::path& path, const fs::path& working_directory,
    std::string hash, std::string runtime, std::string stdout_text,
    std::string stderr_text, std::uint64_t duration_ms,
    const ExecutionTrace& trace) {
  return {code,
          std::move(message),
          std::move(suggestion),
          {{"duration_ms", std::to_string(duration_ms)},
           {"interpreter", std::move(runtime)},
           {"invocation_count", std::to_string(trace.invocations.size())},
           {"mutations_committed", std::to_string(trace.mutation_count)},
           {"nested_invocations", joined_invocations(trace)},
           {"partial_mutation", trace.mutation_count == 0U ? "false" : "true"},
           {"source_path", path.string()},
           {"source_sha256", std::move(hash)},
           {"stderr", std::move(stderr_text)},
           {"stdout", std::move(stdout_text)},
           {"working_directory", working_directory.string()}}};
}

struct ActiveExecution {
  ActiveExecution(const command::Registry& registry, ExecutionTrace& trace)
      : previous_registry{current_registry},
        previous_trace{current_trace},
        previous_executing{executing_script} {
    current_registry = &registry;
    current_trace = &trace;
    executing_script = true;
  }
  ~ActiveExecution() {
    current_registry = previous_registry;
    current_trace = previous_trace;
    executing_script = previous_executing;
  }
  const command::Registry* previous_registry;
  ExecutionTrace* previous_trace;
  bool previous_executing;
};

}  // namespace

namespace detail {

const command::Registry* active_registry() noexcept { return current_registry; }

void record_nested_invocation(const command::Registry& registry,
                              const application::DispatchOutcome& outcome) {
  if (current_trace == nullptr || !outcome.canonical_invocation.has_value()) {
    return;
  }
  current_trace->invocations.push_back(
      std::string{outcome.succeeded() ? "ok\t" : "error\t"} +
      outcome.envelope.canonical_command);
  if (!outcome.succeeded()) return;
  const auto& canonical_name = outcome.canonical_invocation->canonical_name;
  const auto descriptors = registry.descriptors();
  const auto descriptor = std::find_if(
      descriptors.begin(), descriptors.end(), [&](const auto& candidate) {
        return candidate.canonical_name == canonical_name;
      });
  if (descriptor != descriptors.end() &&
      descriptor->undo_policy == command::UndoPolicy::undoable) {
    ++current_trace->mutation_count;
  }
}

}  // namespace detail

operation::Result<command::Response> PythonScriptService::run(
    const PythonScriptRequest& request, operation::TaskContext& context) const {
  if (executing_script) {
    return operation::Result<command::Response>::failure(
        {operation::ErrorCode::script_failed,
         "nested script execution is not supported by the in-process runtime",
         "invoke ordinary MolShredder operations from the running script"});
  }
  if (context.cancellation.is_cancelled()) {
    return operation::Result<command::Response>::failure(
        {operation::ErrorCode::cancelled, "script execution cancelled", {}});
  }
  const auto source = read_source(request);
  if (!source.has_value()) {
    return operation::Result<command::Response>::failure(source.error());
  }
  const auto& [script_path, source_text] = source.value();
  const auto working_directory =
      resolve_working_directory(request, script_path);
  if (working_directory.empty()) {
    return operation::Result<command::Response>::failure(
        invalid("script working directory is not an existing directory",
                "provide a valid --working-directory"));
  }

  std::scoped_lock execution_lock{execution_mutex()};
  ensure_interpreter();
  py::gil_scoped_acquire gil;
  ExecutionTrace trace;
  ActiveExecution active{registry_, trace};
  const auto started = std::chrono::steady_clock::now();
  std::string stdout_text;
  std::string stderr_text;
  std::string source_hash;
  std::string runtime;
  std::string exception_text;

  try {
    auto sys = py::module_::import("sys");
    auto os = py::module_::import("os");
    auto io = py::module_::import("io");
    auto json = py::module_::import("json");
    auto contextlib = py::module_::import("contextlib");
    auto runpy = py::module_::import("runpy");
    auto hashlib = py::module_::import("hashlib");

    const py::object parsed = json.attr("loads")(request.arguments_json);
    if (!py::isinstance<py::list>(parsed)) {
      return operation::Result<command::Response>::failure(invalid(
          "arguments-json must encode an array of strings",
          "pass JSON such as [\"--option\",\"value\"]"));
    }
    py::list arguments = parsed.cast<py::list>();
    for (const auto item : arguments) {
      if (!py::isinstance<py::str>(item)) {
        return operation::Result<command::Response>::failure(invalid(
            "arguments-json must contain only strings",
            "convert every script argument to a JSON string"));
      }
    }

    source_hash = hashlib.attr("sha256")(py::bytes{source_text})
                      .attr("hexdigest")()
                      .cast<std::string>();
    runtime = py::str(sys.attr("version")).cast<std::string>();
    py::object stdout_buffer = io.attr("StringIO")();
    py::object stderr_buffer = io.attr("StringIO")();
    py::object stack = contextlib.attr("ExitStack")();
    py::object old_argv = sys.attr("argv");
    const auto old_directory = os.attr("getcwd")().cast<std::string>();
    bool stack_entered = false;
    try {
      py::list argv;
      argv.append(script_path.string());
      for (const auto item : arguments) argv.append(item);
      sys.attr("argv") = std::move(argv);
      os.attr("chdir")(working_directory.string());
      stack.attr("__enter__")();
      stack_entered = true;
      stack.attr("enter_context")(
          contextlib.attr("redirect_stdout")(stdout_buffer));
      stack.attr("enter_context")(
          contextlib.attr("redirect_stderr")(stderr_buffer));
      runpy.attr("run_path")(script_path.string(),
                             py::arg("run_name") = "__main__");
    } catch (const py::error_already_set& error) {
      exception_text = error.what();
    }
    if (stack_entered) {
      try {
        stack.attr("__exit__")(py::none(), py::none(), py::none());
      } catch (const py::error_already_set& error) {
        if (exception_text.empty()) exception_text = error.what();
      }
    }
    stdout_text = stdout_buffer.attr("getvalue")().cast<std::string>();
    stderr_text = stderr_buffer.attr("getvalue")().cast<std::string>();
    try {
      sys.attr("argv") = old_argv;
      os.attr("chdir")(old_directory);
    } catch (const py::error_already_set& error) {
      if (!stderr_text.empty() && stderr_text.back() != '\n') stderr_text += '\n';
      stderr_text += "runtime restoration error: ";
      stderr_text += error.what();
      if (exception_text.empty()) exception_text = "runtime restoration failed";
    }
  } catch (const py::error_already_set& error) {
    exception_text = error.what();
  }
  if (!exception_text.empty()) {
    if (!stderr_text.empty() && stderr_text.back() != '\n') stderr_text += '\n';
    stderr_text += exception_text;
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);
  const auto duration_ms = static_cast<std::uint64_t>(elapsed.count());
  if (context.cancellation.is_cancelled()) {
    return operation::Result<command::Response>::failure(script_error(
        operation::ErrorCode::cancelled, "script execution cancelled",
        "review partial mutations before retrying", script_path,
        working_directory, std::move(source_hash), std::move(runtime),
        std::move(stdout_text), std::move(stderr_text), duration_ms, trace));
  }
  if (!exception_text.empty()) {
    return operation::Result<command::Response>::failure(script_error(
        operation::ErrorCode::script_failed, "Python script execution failed",
        "inspect stderr and the recorded nested invocations", script_path,
        working_directory, std::move(source_hash), std::move(runtime),
        std::move(stdout_text), std::move(stderr_text), duration_ms, trace));
  }

  command::Value::Array invocations;
  invocations.reserve(trace.invocations.size());
  for (const auto& invocation : trace.invocations) {
    invocations.emplace_back(invocation);
  }
  return operation::Result<command::Response>::success(
      {"Python script completed",
       {{"duration_ms", duration_ms},
        {"headless", true},
        {"interpreter", runtime},
        {"invocation_count",
         static_cast<std::uint64_t>(trace.invocations.size())},
        {"mutations_committed",
         static_cast<std::uint64_t>(trace.mutation_count)},
        {"nested_invocations", std::move(invocations)},
        {"source_path", script_path.string()},
        {"source_sha256", source_hash},
        {"stderr", stderr_text},
        {"stdout", stdout_text},
        {"trusted", true},
        {"working_directory", working_directory.string()}}});
}

std::optional<operation::Error> register_python_script_command(
    command::Registry& registry) {
  const command::Descriptor descriptor{
      "script run",
      "Explicitly execute a trusted local Python script",
      {command::ParameterSpec{"path", command::ParameterType::text, true},
       command::ParameterSpec{"arguments-json", command::ParameterType::text,
                              false, "[]"},
       command::ParameterSpec{"working-directory",
                              command::ParameterType::text, false},
       command::ParameterSpec{"trust", command::ParameterType::boolean, true,
                              std::nullopt, {"false", "true"}}},
      command::UndoPolicy::not_undoable};
  return registry.add(
      descriptor, [&registry](const command::Arguments& arguments,
                              operation::TaskContext& context) {
        PythonScriptRequest request;
        request.path = arguments.at("path");
        request.arguments_json = arguments.at("arguments-json");
        request.trusted = arguments.at("trust") == "true";
        if (const auto found = arguments.find("working-directory");
            found != arguments.end()) {
          request.working_directory = found->second;
        }
        return PythonScriptService{registry}.run(request, context);
      });
}

}  // namespace molshredder::automation
