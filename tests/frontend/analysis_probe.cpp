#include <iostream>
#include <memory>
#include <string>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/command/result.hpp"
#include "molshredder/gui/action_adapter.hpp"
#include "molshredder/operation/common_types.hpp"
#include "molshredder/operation/task_context.hpp"

namespace {

molshredder::application::DispatchOutcome trigger(
    const molshredder::gui::ActionAdapter& gui, std::string command,
    molshredder::command::Arguments arguments) {
  molshredder::operation::TaskContext context;
  return gui.trigger({std::move(command), std::move(arguments)}, context);
}

bool print(const molshredder::application::DispatchOutcome& outcome) {
  const auto rendered = molshredder::command::render(
      outcome.envelope, molshredder::operation::OutputFormat::json);
  if (!rendered.has_value()) return false;
  std::cout << rendered.value();
  return outcome.succeeded();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  auto workspace = std::make_shared<molshredder::application::Workspace>();
  auto registry =
      molshredder::application::make_default_registry(workspace);
  const molshredder::application::Dispatcher dispatcher{registry};
  const molshredder::gui::ActionAdapter gui{dispatcher};
  if (!trigger(gui, "load", {{"path", argv[1]}, {"name", "fixture"}})
           .succeeded()) {
    return 1;
  }
  const auto center = trigger(
      gui, "analyze center",
      {{"selection", "chain A"}, {"mode", "centroid"},
       {"precision", "6"}, {"unit", "nanometer"}});
  const auto com = trigger(
      gui, "analyze center",
      {{"selection", "all"}, {"mode", "com"},
       {"precision", "6"}, {"unit", "angstrom"}});
  const auto distance = trigger(
      gui, "measure distance",
      {{"from", "index 1"}, {"to", "index 2"}, {"mode", "atom"},
       {"pbc", "minimum-image"}, {"precision", "6"},
       {"unit", "nanometer"}});
  return print(center) && print(com) && print(distance) ? 0 : 1;
}
