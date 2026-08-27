#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/render/packet.hpp"

namespace molshredder::render {

struct TransferPoint {
  double value{};
  ColorRgba color;

  friend bool operator==(const TransferPoint &, const TransferPoint &) = default;
};

enum class TransferPreset { grayscale, density, fire, ice, spectrum };

[[nodiscard]] constexpr std::string_view
to_string(TransferPreset preset) noexcept {
  switch (preset) {
  case TransferPreset::grayscale:
    return "grayscale";
  case TransferPreset::density:
    return "density";
  case TransferPreset::fire:
    return "fire";
  case TransferPreset::ice:
    return "ice";
  case TransferPreset::spectrum:
    return "spectrum";
  }
  return "grayscale";
}

class TransferFunction {
public:
  static constexpr std::string_view algorithm{"piecewise-linear-rgba"};
  static constexpr std::size_t version{1U};

  [[nodiscard]] static operation::Result<TransferFunction>
  create(std::vector<TransferPoint> points);
  [[nodiscard]] static TransferFunction builtin(TransferPreset preset,
                                                double minimum,
                                                double maximum);

  [[nodiscard]] ColorRgba sample(double value) const noexcept;
  [[nodiscard]] operation::Result<std::vector<ColorRgba>>
  lookup_table(std::size_t sample_count, std::size_t memory_budget_bytes,
               operation::TaskContext *context = nullptr) const;

  [[nodiscard]] std::span<const TransferPoint> points() const noexcept {
    return points_;
  }

private:
  explicit TransferFunction(std::vector<TransferPoint> points)
      : points_{std::move(points)} {}

  std::vector<TransferPoint> points_;
};

} // namespace molshredder::render
