#pragma once

#include <string>
#include <string_view>

namespace molshredder::operation {

enum class ErrorCode {
  invalid_argument,
  invalid_selection,
  not_found,
  cancelled,
  unsupported,
  internal,
};

[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::invalid_argument:
      return "invalid_argument";
    case ErrorCode::invalid_selection:
      return "invalid_selection";
    case ErrorCode::not_found:
      return "not_found";
    case ErrorCode::cancelled:
      return "cancelled";
    case ErrorCode::unsupported:
      return "unsupported";
    case ErrorCode::internal:
      return "internal";
  }
  return "internal";
}

struct Error {
  ErrorCode code{ErrorCode::internal};
  std::string message;
  std::string suggestion;

  friend bool operator==(const Error&, const Error&) = default;
};

}  // namespace molshredder::operation
