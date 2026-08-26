#include "bindings.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <pybind11/stl.h>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/automation/python_script.hpp"
#include "molshredder/command/result.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/version.hpp"

namespace py = pybind11;

namespace molshredder::python {
namespace {

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

command::Registry& fallback_registry() {
  static auto registry = [] {
    auto value = application::make_default_registry();
    if (const auto error = automation::register_python_script_command(value);
        error.has_value()) {
      std::terminate();
    }
    return value;
  }();
  return registry;
}

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
  return envelope_to_python(outcome.envelope);
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
}

}  // namespace molshredder::python
