#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

#include "molshredder/operation/error.hpp"
#include "molshredder/render/transfer_function.hpp"

namespace {
bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}
} // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  const auto transfer = render::TransferFunction::create(
      {{-1.0, {0.0F, 0.0F, 1.0F, 0.0F}},
       {1.0, {1.0F, 0.0F, 0.0F, 1.0F}}});
  passed &= expect(transfer.has_value(), "valid transfer function must build");
  if (transfer.has_value()) {
    const auto middle = transfer.value().sample(0.0);
    passed &= expect(middle == render::ColorRgba{0.5F, 0.0F, 0.5F, 0.5F} &&
                         transfer.value().sample(-10.0) ==
                             transfer.value().points().front().color &&
                         transfer.value().sample(10.0) ==
                             transfer.value().points().back().color,
                     "sampling must interpolate RGBA and clamp endpoints");
    double progress{};
    operation::TaskContext context;
    context.report_progress = [&progress](const operation::ProgressUpdate &update) {
      progress = update.fraction;
    };
    const auto lut = transfer.value().lookup_table(
        5U, 5U * sizeof(render::ColorRgba), &context);
    passed &= expect(lut.has_value() && lut.value().size() == 5U &&
                         lut.value()[2] == middle && progress == 1.0,
                     "lookup table must include deterministic endpoints and midpoint");
    const auto budget = transfer.value().lookup_table(5U, 1U);
    passed &= expect(!budget.has_value() &&
                         budget.error().code ==
                             operation::ErrorCode::resource_exhausted,
                     "lookup table budget exhaustion must be explicit");
    context.cancellation.request_cancel();
    const auto cancelled = transfer.value().lookup_table(5U, 1024U, &context);
    passed &= expect(!cancelled.has_value() &&
                         cancelled.error().code == operation::ErrorCode::cancelled,
                     "lookup table cancellation must not publish partial data");
  }
  passed &= expect(!render::TransferFunction::create({}).has_value(),
                   "empty transfer function must fail");
  passed &= expect(!render::TransferFunction::create(
                        {{0.0, {}}, {0.0, {}}})
                        .has_value(),
                   "duplicate scalar values must fail");
  passed &= expect(!render::TransferFunction::create(
                        {{0.0, {}},
                         {std::numeric_limits<double>::quiet_NaN(), {}}})
                        .has_value(),
                   "non-finite scalar values must fail");
  for (const auto preset : {render::TransferPreset::grayscale,
                            render::TransferPreset::density,
                            render::TransferPreset::fire,
                            render::TransferPreset::ice,
                            render::TransferPreset::spectrum}) {
    const auto builtin = render::TransferFunction::builtin(preset, -2.0, 3.0);
    passed &= expect(builtin.points().front().value == -2.0 &&
                         builtin.points().back().value == 3.0,
                     "all builtins must use the requested scalar domain");
  }
  return passed ? 0 : 1;
}
