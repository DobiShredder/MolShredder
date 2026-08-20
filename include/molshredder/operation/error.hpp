#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace molshredder::operation {

enum class ErrorCode {
  invalid_argument,
  invalid_selection,
  not_found,
  cancelled,
  script_failed,
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
    case ErrorCode::script_failed:
      return "script_failed";
    case ErrorCode::unsupported:
      return "unsupported";
    case ErrorCode::internal:
      return "internal";
  }
  return "internal";
}

struct Error {
  Error() = default;
  Error(ErrorCode error_code, std::string error_message,
        std::string error_suggestion,
        std::map<std::string, std::string, std::less<>> error_details = {})
      : code{error_code},
        message{std::move(error_message)},
        suggestion{std::move(error_suggestion)},
        details{std::move(error_details)} {}

  ErrorCode code{ErrorCode::internal};
  std::string message;
  std::string suggestion;
  std::map<std::string, std::string, std::less<>> details;

  friend bool operator==(const Error&, const Error&) = default;
};

}  // namespace molshredder::operation
