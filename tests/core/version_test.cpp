#include <cmath>
#include <iostream>
#include <string>
#include <string_view>

#include "molshredder/core/parse_number.hpp"
#include "molshredder/version.hpp"

int main() {
  constexpr std::string_view kExpectedVersion = "0.1.0";
  if (molshredder::version() != kExpectedVersion) {
    std::cerr << "expected version " << kExpectedVersion << ", got "
              << molshredder::version() << '\n';
    return 1;
  }
  const std::string decimal{"-12.5e2"};
  double fallback_value{};
  const auto fallback =
      molshredder::core::detail::floating_from_chars_fallback(
          decimal.data(), decimal.data() + decimal.size(), fallback_value);
  if (fallback.ec != std::errc{} ||
      fallback.ptr != decimal.data() + decimal.size() ||
      std::abs(fallback_value + 1250.0) > 1.0e-12) {
    std::cerr << "portable floating parser rejected a valid decimal\n";
    return 1;
  }
  const std::string trailing{"1.25tail"};
  fallback_value = 0.0;
  const auto partial =
      molshredder::core::detail::floating_from_chars_fallback(
          trailing.data(), trailing.data() + trailing.size(), fallback_value);
  if (partial.ec != std::errc{} || partial.ptr != trailing.data() + 4 ||
      std::abs(fallback_value - 1.25) > 1.0e-12) {
    std::cerr << "portable floating parser lost partial-parse semantics\n";
    return 1;
  }
  const std::string invalid{" 1.0"};
  fallback_value = 0.0;
  const auto rejected =
      molshredder::core::detail::floating_from_chars_fallback(
          invalid.data(), invalid.data() + invalid.size(), fallback_value);
  if (rejected.ec != std::errc::invalid_argument ||
      rejected.ptr != invalid.data()) {
    std::cerr << "portable floating parser accepted leading whitespace\n";
    return 1;
  }
  return 0;
}
