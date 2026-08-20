#include <iostream>
#include <string_view>

#include "embedded_module.hpp"
#include "molshredder/application/default_registry.hpp"
#include "molshredder/automation/python_script.hpp"
#include "molshredder/cli/adapter.hpp"
#include "molshredder/version.hpp"

int main(int argc, char* argv[]) {
  if (argc == 2 && std::string_view{argv[1]} == "--version") {
    std::cout << "MolShredder " << molshredder::version() << '\n';
    return 0;
  }
  molshredder::python::link_embedded_module();
  auto registry = molshredder::application::make_default_registry();
  if (molshredder::automation::register_python_script_command(registry)
          .has_value()) {
    std::cerr << "failed to register Python script command\n";
    return 2;
  }
  molshredder::cli::Adapter adapter{registry};
  return adapter.run(argc, argv, std::cin, std::cout, std::cerr);
}
