#include <cmath>
#include <iostream>
#include <optional>
#include <string_view>

#include "molshredder/operation/common_types.hpp"
#include "molshredder/operation/operation.hpp"

namespace {

using molshredder::operation::Error;
using molshredder::operation::ErrorCode;
using molshredder::operation::Operation;
using molshredder::operation::Result;
using molshredder::operation::TaskContext;

struct ScaleRequest {
  double value{};
  double factor{};
};

struct ScaleResult {
  double value{};
};

class ScaleOperation final : public Operation<ScaleRequest, ScaleResult> {
 public:
  [[nodiscard]] std::string_view name() const noexcept override {
    return "test.scale";
  }

  [[nodiscard]] std::optional<Error> validate(
      const ScaleRequest& request) const override {
    if (!std::isfinite(request.value) || !std::isfinite(request.factor)) {
      return Error{ErrorCode::invalid_argument, "values must be finite",
                   "provide finite numeric values"};
    }
    return std::nullopt;
  }

  [[nodiscard]] Result<ScaleResult> execute(
      const ScaleRequest& request, TaskContext& context) const override {
    if (const auto error = validate(request); error.has_value()) {
      return Result<ScaleResult>::failure(*error);
    }
    if (context.cancellation.is_cancelled()) {
      return Result<ScaleResult>::failure(
          Error{ErrorCode::cancelled, "operation cancelled", {}});
    }
    if (context.report_progress) {
      context.report_progress({1.0, "complete"});
    }
    return Result<ScaleResult>::success({request.value * request.factor});
  }
};

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  bool passed = true;
  const ScaleOperation operation;

  double progress = 0.0;
  TaskContext context{
      {}, [&progress](const auto& update) { progress = update.fraction; }};
  const auto result = operation.execute({3.0, 2.0}, context);
  passed &= expect(result.has_value(), "valid request must succeed");
  passed &= expect(result.has_value() && result.value().value == 6.0,
                   "operation must return typed result");
  passed &= expect(progress == 1.0, "operation must report progress");

  const auto invalid = operation.execute({INFINITY, 2.0}, context);
  passed &= expect(!invalid.has_value(), "invalid request must fail");
  passed &= expect(!invalid.has_value() &&
                       invalid.error().code == ErrorCode::invalid_argument,
                   "invalid request must use stable error code");

  TaskContext cancelled;
  cancelled.cancellation.request_cancel();
  const auto cancelled_result = operation.execute({3.0, 2.0}, cancelled);
  passed &= expect(!cancelled_result.has_value(),
                   "cancelled operation must fail");
  passed &= expect(!cancelled_result.has_value() &&
                       cancelled_result.error().code == ErrorCode::cancelled,
                   "cancelled operation must use cancelled error code");

  const molshredder::operation::FrameRange invalid_range{
      molshredder::operation::FrameScope::range, 10, 5, 1};
  passed &= expect(!invalid_range.is_valid(),
                   "descending frame range must be invalid");

  return passed ? 0 : 1;
}
