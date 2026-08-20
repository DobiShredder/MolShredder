#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/runtime_diagnostics.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/command/registry.hpp"
#include "molshredder/operation/task_context.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

const molshredder::command::Value::Object *object_field(
    const molshredder::command::Value::Object &object, std::string_view name) {
  const auto found = object.find(name);
  return found == object.end()
             ? nullptr
             : std::get_if<molshredder::command::Value::Object>(
                   &found->second.data);
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  auto diagnostics =
      std::make_shared<application::RuntimeDiagnostics>();
  const auto initial = diagnostics->graphics();
  passed &= expect(
      initial.status == application::RuntimeStatus::unavailable &&
          initial.failure_reason.has_value(),
      "headless diagnostics must explicitly report unavailable graphics");

  application::GraphicsRuntimeInfo ready;
  ready.status = application::RuntimeStatus::ready;
  ready.api = "vulkan";
  ready.backend = "Vulkan";
  ready.rhi_based = true;
  ready.device_name = "Synthetic GPU";
  ready.device_id = 42U;
  ready.vendor_id = 7U;
  ready.device_type = "discrete";
  ready.failure_reason.reset();
  diagnostics->set_graphics(ready);
  passed &= expect(diagnostics->graphics() == ready,
                   "runtime diagnostics snapshot must preserve typed fields");

  auto registry = application::make_default_registry(
      std::make_shared<application::Workspace>(), diagnostics);
  operation::TaskContext context;
  const auto result = registry.invoke("system info", {}, context);
  passed &= expect(result.has_value(), "system info must invoke successfully");
  if (result.has_value()) {
    const auto *runtime = object_field(result.value().fields, "runtime");
    const auto *graphics =
        runtime == nullptr ? nullptr : object_field(*runtime, "graphics");
    if (graphics == nullptr) {
      passed &= expect(false, "system info must contain runtime graphics");
    } else {
      const auto status = graphics->find("status");
      passed &= expect(
          status != graphics->end() &&
              std::get<std::string>(status->second.data) == "ready" &&
              std::get<std::string>(graphics->at("device_name").data) ==
                  "Synthetic GPU" &&
              std::get<std::uint64_t>(graphics->at("device_id").data) == 42U,
          "system info must read the live shared diagnostics snapshot");
    }
  }

  application::GraphicsRuntimeInfo failed;
  failed.status = application::RuntimeStatus::failed;
  failed.api = "vulkan";
  failed.backend = "Vulkan";
  failed.rhi_based = true;
  failed.failure_reason = "synthetic device loss";
  diagnostics->set_graphics(failed);
  const auto failed_result = registry.invoke("system info", {}, context);
  if (!failed_result.has_value()) {
    passed &= expect(false, "updated system info must invoke successfully");
  } else {
    const auto *runtime = object_field(failed_result.value().fields, "runtime");
    const auto *graphics =
        runtime == nullptr ? nullptr : object_field(*runtime, "graphics");
    passed &= expect(
        graphics != nullptr &&
            std::get<std::string>(graphics->at("status").data) == "failed" &&
            std::get<std::string>(graphics->at("failure_reason").data) ==
                "synthetic device loss" &&
            std::holds_alternative<std::nullptr_t>(
                graphics->at("device_name").data),
        "registry must discard stale device fields after runtime failure");
  }

  passed &= expect(application::to_string(application::RuntimeStatus::failed) ==
                       "failed",
                   "runtime status serialization must remain stable");
  return passed ? 0 : 1;
}
