#pragma once

#include <charconv>
#include <concepts>
#include <cstddef>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>

namespace molshredder::core {
namespace detail {

template <std::floating_point Number>
inline std::from_chars_result floating_from_chars_fallback(
    const char* first, const char* last, Number& value) {
  std::istringstream stream{std::string{first, last}};
  stream.imbue(std::locale::classic());
  stream >> std::noskipws >> value;
  if (stream.fail()) {
    return {first, std::errc::invalid_argument};
  }
  if (stream.eof()) {
    return {last, {}};
  }
  const auto position = stream.tellg();
  if (position < std::streampos{0}) {
    return {last, {}};
  }
  const auto offset = position - std::streampos{0};
  return {first + static_cast<std::ptrdiff_t>(offset), {}};
}

}  // namespace detail

// Apple libc++ shipped with Xcode 15 exposes integer from_chars but not the
// C++17 floating-point overloads. Keep integer parsing on the native fast path
// and provide a locale-independent fallback only when the requested overload
// is unavailable in the active standard library.
template <typename Number>
  requires std::integral<Number> || std::floating_point<Number>
inline std::from_chars_result from_chars(const char* first, const char* last,
                                         Number& value) {
  if constexpr (requires { std::from_chars(first, last, value); }) {
    return std::from_chars(first, last, value);
  } else {
    static_assert(std::floating_point<Number>);
    return detail::floating_from_chars_fallback(first, last, value);
  }
}

template <std::integral Number>
inline std::from_chars_result from_chars(const char* first, const char* last,
                                         Number& value, int base) {
  return std::from_chars(first, last, value, base);
}

}  // namespace molshredder::core
