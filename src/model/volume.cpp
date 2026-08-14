#include "molshredder/model/volume.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::model {
namespace {

operation::Error invalid(std::string message) {
  return operation::Error{
      operation::ErrorCode::invalid_argument, std::move(message), {}};
}

bool finite(Vec3d value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

Vec3d cross(Vec3d left, Vec3d right) noexcept {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

double dot(Vec3d left, Vec3d right) noexcept {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

double length(Vec3d value) noexcept { return std::sqrt(dot(value, value)); }

} // namespace

std::size_t VolumeScalarBuffer::size() const noexcept {
  return std::visit([](const auto &values) { return values.size(); }, values_);
}

VolumePrecision VolumeScalarBuffer::precision() const noexcept {
  return std::holds_alternative<std::vector<float>>(values_)
             ? VolumePrecision::float32
             : VolumePrecision::float64;
}

bool VolumeScalarBuffer::all_finite() const noexcept {
  return std::visit(
      [](const auto &values) {
        return std::all_of(values.begin(), values.end(), [](const auto value) {
          return std::isfinite(value);
        });
      },
      values_);
}

double VolumeScalarBuffer::value(std::size_t index) const noexcept {
  return std::visit(
      [index](const auto &values) {
        return static_cast<double>(values[index]);
      },
      values_);
}

std::pair<double, double> VolumeScalarBuffer::range() const noexcept {
  return std::visit(
      [](const auto &values) {
        const auto [minimum, maximum] =
            std::minmax_element(values.begin(), values.end());
        return std::pair<double, double>{static_cast<double>(*minimum),
                                         static_cast<double>(*maximum)};
      },
      values_);
}

operation::Result<std::shared_ptr<const VolumeGrid>>
VolumeGrid::create(VolumeShape shape, Vec3d origin,
                   std::array<Vec3d, 3U> deltas, VolumeScalarBuffer scalars,
                   VolumeMetadata metadata) {
  if (shape.x == 0U || shape.y == 0U || shape.z == 0U) {
    return operation::Result<std::shared_ptr<const VolumeGrid>>::failure(
        invalid("volume dimensions must all be positive"));
  }
  if (shape.x > std::numeric_limits<std::size_t>::max() / shape.y ||
      shape.x * shape.y > std::numeric_limits<std::size_t>::max() / shape.z) {
    return operation::Result<std::shared_ptr<const VolumeGrid>>::failure(
        invalid("volume dimensions overflow the address space"));
  }
  const auto expected = shape.x * shape.y * shape.z;
  if (scalars.size() != expected) {
    return operation::Result<std::shared_ptr<const VolumeGrid>>::failure(
        invalid("volume scalar count does not match its dimensions"));
  }
  if (!finite(origin) || !std::all_of(deltas.begin(), deltas.end(), finite)) {
    return operation::Result<std::shared_ptr<const VolumeGrid>>::failure(
        invalid("volume origin and delta vectors must be finite"));
  }
  const auto scale = length(deltas[0]) * length(deltas[1]) * length(deltas[2]);
  const auto determinant = dot(deltas[0], cross(deltas[1], deltas[2]));
  if (!std::isfinite(scale) || scale <= 0.0 ||
      std::abs(determinant) <= scale * 1.0e-12) {
    return operation::Result<std::shared_ptr<const VolumeGrid>>::failure(
        invalid("volume delta vectors must form a non-degenerate basis"));
  }
  if (!scalars.all_finite()) {
    return operation::Result<std::shared_ptr<const VolumeGrid>>::failure(
        invalid("volume scalar values must be finite"));
  }
  return operation::Result<std::shared_ptr<const VolumeGrid>>::success(
      std::shared_ptr<const VolumeGrid>{new VolumeGrid{
          shape, origin, deltas, std::move(scalars), std::move(metadata)}});
}

std::size_t VolumeGrid::linear_index(std::size_t x, std::size_t y,
                                     std::size_t z) const noexcept {
  return (x * shape_.y + y) * shape_.z + z;
}

double VolumeGrid::value(std::size_t x, std::size_t y,
                         std::size_t z) const noexcept {
  return scalars_.value(linear_index(x, y, z));
}

Vec3d VolumeGrid::position(std::size_t x, std::size_t y,
                           std::size_t z) const noexcept {
  const auto sx = static_cast<double>(x);
  const auto sy = static_cast<double>(y);
  const auto sz = static_cast<double>(z);
  return {origin_.x + deltas_[0].x * sx + deltas_[1].x * sy + deltas_[2].x * sz,
          origin_.y + deltas_[0].y * sx + deltas_[1].y * sy + deltas_[2].y * sz,
          origin_.z + deltas_[0].z * sx + deltas_[1].z * sy +
              deltas_[2].z * sz};
}

} // namespace molshredder::model
