#include <iostream>
#include <string_view>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/gui/action_adapter.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/operation/task_context.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  bool passed = true;
  auto registry = molshredder::application::make_default_registry();
  const molshredder::application::Dispatcher dispatcher{registry};
  const molshredder::gui::ActionAdapter gui{dispatcher};

  molshredder::operation::TaskContext version_context;
  const auto version = gui.trigger(
      molshredder::gui::Action{"version", {}}, version_context);
  passed &= expect(version.succeeded(),
                   "GUI version action must invoke the shared handler");
  passed &= expect(version.envelope.canonical_command ==
                       "invoke \"system version\"",
                   "GUI action must store the expanded canonical command");

  molshredder::operation::TaskContext center_context;
  const auto center = gui.trigger(
      molshredder::gui::Action{"com", {{"selection", "protein"}}},
      center_context);
  passed &= expect(!center.succeeded(),
                   "GUI analysis without a loaded object must fail honestly");
  passed &= expect(center.canonical_invocation.has_value() &&
                       center.envelope.canonical_command ==
                           "invoke \"analyze center\" --mode \"com\" "
                           "--precision \"6\" --selection \"protein\" "
                           "--unit \"angstrom\"",
                   "GUI action must share alias/default normalization");
  const auto& center_error =
      std::get<molshredder::operation::Error>(center.envelope.payload);
  passed &= expect(center_error.code ==
                       molshredder::operation::ErrorCode::not_found,
                   "GUI action must preserve the shared workspace error code");

  molshredder::operation::TaskContext invalid_context;
  const auto invalid = gui.trigger(
      molshredder::gui::Action{"show", {{"representation", "surface"}}},
      invalid_context);
  passed &= expect(!invalid.succeeded() &&
                       !invalid.canonical_invocation.has_value(),
                   "invalid GUI action must fail before handler execution");
  const auto& invalid_error =
      std::get<molshredder::operation::Error>(invalid.envelope.payload);
  passed &= expect(invalid_error.code ==
                       molshredder::operation::ErrorCode::invalid_argument,
                   "GUI validation must use the registry error model");

  return passed ? 0 : 1;
}
