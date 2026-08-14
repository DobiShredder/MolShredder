#include <iostream>
#include <string>
#include <string_view>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/command/result.hpp"
#include "molshredder/gui/action_adapter.hpp"
#include "molshredder/operation/common_types.hpp"
#include "molshredder/operation/task_context.hpp"

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "usage: gui_probe version|com\n";
    return 2;
  }

  molshredder::gui::Action action;
  const std::string_view scenario{argv[1]};
  if (scenario == "version") {
    action.command_name = "version";
  } else if (scenario == "com") {
    action.command_name = "com";
    action.parameters.emplace("selection", "protein");
  } else {
    std::cerr << "unsupported probe scenario\n";
    return 2;
  }

  auto registry = molshredder::application::make_default_registry();
  const molshredder::application::Dispatcher dispatcher{registry};
  const molshredder::gui::ActionAdapter gui{dispatcher};
  molshredder::operation::TaskContext context;
  const auto outcome = gui.trigger(action, context);
  const auto rendered = molshredder::command::render(
      outcome.envelope, molshredder::operation::OutputFormat::json);
  if (!rendered.has_value()) {
    std::cerr << rendered.error().message << '\n';
    return 2;
  }
  std::cout << rendered.value();
  return 0;
}
