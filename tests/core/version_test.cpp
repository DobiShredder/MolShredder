#include <iostream>
#include <string_view>

#include "molshredder/version.hpp"

int main() {
  constexpr std::string_view kExpectedVersion = "0.1.0";
  if (molshredder::version() != kExpectedVersion) {
    std::cerr << "expected version " << kExpectedVersion << ", got "
              << molshredder::version() << '\n';
    return 1;
  }
  return 0;
}
