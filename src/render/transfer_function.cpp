#include "molshredder/render/transfer_function.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::render {
namespace {

operation::Error invalid(std::string message) {
  return {operation::ErrorCode::invalid_argument, std::move(message), {}};
}

ColorRgba interpolate(ColorRgba first, ColorRgba second, double fraction) {
  const auto mix = [fraction](float left, float right) {
    return static_cast<float>(static_cast<double>(left) +
                              (static_cast<double>(right) - left) * fraction);
  };
  return {mix(first.red, second.red), mix(first.green, second.green),
          mix(first.blue, second.blue), mix(first.alpha, second.alpha)};
}

TransferPoint point(double value, std::array<float, 4U> color) {
  return {value, {color[0], color[1], color[2], color[3]}};
}

} // namespace

operation::Result<TransferFunction>
TransferFunction::create(std::vector<TransferPoint> points) {
  if (points.size() < 2U)
    return operation::Result<TransferFunction>::failure(
        invalid("transfer function requires at least two points"));
  for (std::size_t index = 0; index < points.size(); ++index) {
    if (!std::isfinite(points[index].value) || !is_valid(points[index].color)) {
      return operation::Result<TransferFunction>::failure(
          invalid("transfer function points must contain finite values and valid RGBA colors"));
    }
    if (index > 0U && points[index - 1U].value >= points[index].value) {
      return operation::Result<TransferFunction>::failure(
          invalid("transfer function scalar values must be strictly increasing"));
    }
  }
  return operation::Result<TransferFunction>::success(
      TransferFunction{std::move(points)});
}

TransferFunction TransferFunction::builtin(TransferPreset preset,
                                           double minimum, double maximum) {
  if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum) {
    minimum = 0.0;
    maximum = 1.0;
  }
  const auto at = [minimum, maximum](double fraction) {
    return minimum + (maximum - minimum) * fraction;
  };
  std::vector<TransferPoint> points;
  switch (preset) {
  case TransferPreset::grayscale:
    points = {point(minimum, {0.0F, 0.0F, 0.0F, 0.0F}),
              point(maximum, {1.0F, 1.0F, 1.0F, 1.0F})};
    break;
  case TransferPreset::density:
    points = {point(minimum, {0.0F, 0.0F, 0.0F, 0.0F}),
              point(at(0.35), {0.0F, 0.25F, 0.8F, 0.08F}),
              point(at(0.7), {0.1F, 0.9F, 0.8F, 0.35F}),
              point(maximum, {1.0F, 1.0F, 1.0F, 0.9F})};
    break;
  case TransferPreset::fire:
    points = {point(minimum, {0.0F, 0.0F, 0.0F, 0.0F}),
              point(at(0.35), {0.55F, 0.0F, 0.0F, 0.12F}),
              point(at(0.7), {1.0F, 0.35F, 0.0F, 0.5F}),
              point(maximum, {1.0F, 1.0F, 0.75F, 1.0F})};
    break;
  case TransferPreset::ice:
    points = {point(minimum, {0.0F, 0.0F, 0.1F, 0.0F}),
              point(at(0.45), {0.0F, 0.35F, 0.85F, 0.18F}),
              point(maximum, {0.8F, 1.0F, 1.0F, 0.9F})};
    break;
  case TransferPreset::spectrum:
    points = {point(minimum, {0.2F, 0.0F, 0.5F, 0.0F}),
              point(at(0.25), {0.0F, 0.25F, 1.0F, 0.15F}),
              point(at(0.5), {0.0F, 0.9F, 0.35F, 0.35F}),
              point(at(0.75), {1.0F, 0.85F, 0.0F, 0.6F}),
              point(maximum, {0.9F, 0.0F, 0.0F, 1.0F})};
    break;
  }
  return TransferFunction{std::move(points)};
}

ColorRgba TransferFunction::sample(double value) const noexcept {
  if (!std::isfinite(value) || value <= points_.front().value)
    return points_.front().color;
  if (value >= points_.back().value)
    return points_.back().color;
  const auto upper = std::upper_bound(
      points_.begin(), points_.end(), value,
      [](double scalar, const TransferPoint &entry) { return scalar < entry.value; });
  const auto lower = upper - 1;
  const auto fraction =
      (value - lower->value) / (upper->value - lower->value);
  return interpolate(lower->color, upper->color, fraction);
}

operation::Result<std::vector<ColorRgba>> TransferFunction::lookup_table(
    std::size_t sample_count, std::size_t memory_budget_bytes,
    operation::TaskContext *context) const {
  if (sample_count < 2U)
    return operation::Result<std::vector<ColorRgba>>::failure(
        invalid("transfer-function lookup table requires at least two samples"));
  if (sample_count > memory_budget_bytes / sizeof(ColorRgba)) {
    return operation::Result<std::vector<ColorRgba>>::failure(
        {operation::ErrorCode::resource_exhausted,
         "transfer-function lookup table exceeds the memory budget",
         "reduce the sample count or increase the memory budget"});
  }
  std::vector<ColorRgba> result;
  result.reserve(sample_count);
  const auto minimum = points_.front().value;
  const auto span = points_.back().value - minimum;
  for (std::size_t index = 0; index < sample_count; ++index) {
    if (context != nullptr && context->cancellation.is_cancelled()) {
      return operation::Result<std::vector<ColorRgba>>::failure(
          {operation::ErrorCode::cancelled,
           "transfer-function lookup table generation was cancelled", {}});
    }
    result.push_back(sample(minimum + span * static_cast<double>(index) /
                                         static_cast<double>(sample_count - 1U)));
  }
  if (context != nullptr && context->report_progress)
    context->report_progress({1.0, "transfer-function-lut"});
  return operation::Result<std::vector<ColorRgba>>::success(std::move(result));
}

} // namespace molshredder::render
