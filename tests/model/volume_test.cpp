#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

#include "molshredder/model/volume.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

bool near(double left, double right, double tolerance = 1.0e-12) {
  return std::abs(left - right) <= tolerance;
}

} // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  model::VolumeMetadata metadata;
  metadata.coordinate_unit = operation::LengthUnit::nanometer;
  metadata.scalar_unit = "kT/e";
  const auto grid = model::VolumeGrid::create(
      {2U, 2U, 2U}, {-1.0, 2.0, 0.5},
      std::array<model::Vec3d, 3U>{
          {{0.5, 0.0, 0.0}, {0.1, 1.0, 0.0}, {0.0, 0.2, 1.5}}},
      model::VolumeScalarBuffer{
          std::vector<float>{-2.0F, -1.0F, 0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F}},
      metadata);
  passed &= expect(grid.has_value(), "valid skewed volume grid must build");
  if (grid.has_value()) {
    const auto range = grid.value()->scalars().range();
    const auto position = grid.value()->position(1U, 1U, 1U);
    passed &=
        expect(grid.value()->value_count() == 8U &&
                   grid.value()->scalars().precision() ==
                       model::VolumePrecision::float32 &&
                   grid.value()->linear_index(1U, 0U, 1U) == 5U &&
                   grid.value()->value(1U, 0U, 1U) == 3.0 &&
                   range == std::pair<double, double>{-2.0, 5.0} &&
                   near(position.x, -0.4) && near(position.y, 3.2) &&
                   near(position.z, 2.0) &&
                   grid.value()->metadata().scalar_unit == "kT/e",
               "volume must preserve z-fastest values, geometry and metadata");
  }

  const auto wrong_count = model::VolumeGrid::create(
      {2U, 2U, 2U}, {},
      std::array<model::Vec3d, 3U>{
          {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}},
      model::VolumeScalarBuffer{std::vector<double>(7U)});
  const auto degenerate = model::VolumeGrid::create(
      {1U, 1U, 1U}, {},
      std::array<model::Vec3d, 3U>{
          {{1.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 0.0, 1.0}}},
      model::VolumeScalarBuffer{std::vector<double>{0.0}});
  const auto non_finite = model::VolumeGrid::create(
      {1U, 1U, 1U}, {},
      std::array<model::Vec3d, 3U>{
          {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}},
      model::VolumeScalarBuffer{
          std::vector<double>{std::numeric_limits<double>::infinity()}});
  passed &=
      expect(!wrong_count.has_value() && !degenerate.has_value() &&
                 !non_finite.has_value(),
             "volume must reject count, basis and finite-value violations");
  return passed ? 0 : 1;
}
