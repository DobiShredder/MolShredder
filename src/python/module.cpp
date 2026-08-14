#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <utility>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/command/registry.hpp"
#include "molshredder/command/result.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/version.hpp"

namespace py = pybind11;

namespace {

py::object to_python(const molshredder::command::Value& value);

py::dict object_to_python(const molshredder::command::Value::Object& object) {
  py::dict result;
  for (const auto& [name, value] : object) {
    result[py::str{name}] = to_python(value);
  }
  return result;
}

py::object to_python(const molshredder::command::Value& value) {
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
        } else if constexpr (std::is_same_v<
                                 Item, molshredder::command::Number>) {
          return py::cast(item.value);
        } else if constexpr (std::is_same_v<
                                 Item,
                                 molshredder::command::Value::Array>) {
          py::list result;
          for (const auto& value : item) {
            result.append(to_python(value));
          }
          return std::move(result);
        } else {
          return object_to_python(item);
        }
      },
      value.data);
}

py::dict table_to_python(const molshredder::command::Table& table) {
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

py::dict envelope_to_python(
    const molshredder::command::ResultEnvelope& envelope) {
  py::dict result;
  result["schema_version"] = envelope.schema_version;
  result["status"] = envelope.succeeded() ? "ok" : "error";
  result["command"] = envelope.canonical_command;
  if (envelope.succeeded()) {
    const auto& response =
        std::get<molshredder::command::Response>(envelope.payload);
    result["summary"] = response.summary;
    auto data = object_to_python(response.fields);
    if (response.table.has_value()) {
      data["table"] = table_to_python(*response.table);
    }
    result["data"] = std::move(data);
  } else {
    const auto& failure =
        std::get<molshredder::operation::Error>(envelope.payload);
    py::dict error;
    error["code"] = molshredder::operation::to_string(failure.code);
    error["message"] = failure.message;
    error["suggestion"] = failure.suggestion;
    result["error"] = std::move(error);
  }
  return result;
}

molshredder::command::Registry& default_registry() {
  static auto registry = molshredder::application::make_default_registry();
  return registry;
}

py::dict invoke(std::string command_name,
                molshredder::command::Arguments arguments) {
  const molshredder::application::Dispatcher dispatcher{default_registry()};
  molshredder::operation::TaskContext context;
  const auto outcome = dispatcher.dispatch(
      molshredder::command::Invocation{std::move(command_name),
                                      std::move(arguments)},
      context);
  return envelope_to_python(outcome.envelope);
}

}  // namespace

PYBIND11_MODULE(molshredder, module) {
  module.doc() =
      "MolShredder typed operation bindings (foundation prototype)";
  module.attr("__version__") = molshredder::version();
  module.def("invoke", &invoke, py::arg("command_name"),
             py::arg("arguments") = molshredder::command::Arguments{},
             "Invoke a canonical command or alias through the shared C++ "
             "dispatcher and return a result-envelope dictionary.");
}
