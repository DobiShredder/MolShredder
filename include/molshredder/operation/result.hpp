#pragma once

#include <utility>
#include <variant>

#include "molshredder/operation/error.hpp"

namespace molshredder::operation {

template <typename Value>
class Result {
 public:
  [[nodiscard]] static Result success(Value value) {
    return Result{std::move(value)};
  }

  [[nodiscard]] static Result failure(Error error) {
    return Result{std::move(error)};
  }

  [[nodiscard]] bool has_value() const noexcept {
    return std::holds_alternative<Value>(storage_);
  }

  [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

  [[nodiscard]] const Value& value() const { return std::get<Value>(storage_); }
  [[nodiscard]] Value& value() { return std::get<Value>(storage_); }
  [[nodiscard]] const Error& error() const { return std::get<Error>(storage_); }

 private:
  explicit Result(Value value) : storage_{std::move(value)} {}
  explicit Result(Error error) : storage_{std::move(error)} {}

  std::variant<Value, Error> storage_;
};

}  // namespace molshredder::operation
