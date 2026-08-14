#include <iostream>
#include <string_view>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/cli/adapter.hpp"
#include "molshredder/version.hpp"

int main(int argc, char* argv[]) {
  if (argc == 2 && std::string_view{argv[1]} == "--version") {
    std::cout << "MolShredder " << molshredder::version() << '\n';
    return 0;
  }
  auto registry = molshredder::application::make_default_registry();
  molshredder::cli::Adapter adapter{registry};
  return adapter.run(argc, argv, std::cin, std::cout, std::cerr);
}
