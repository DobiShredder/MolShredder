#include "molshredder/scene/math.hpp"

#include <algorithm>
#include <cmath>

namespace molshredder::scene {
namespace {

constexpr double quaternion_tolerance = 1.0e-9;

}  // namespace

bool is_finite(model::Vec3d value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

bool is_valid(Quaterniond value) noexcept {
  if (!std::isfinite(value.w) || !std::isfinite(value.x) ||
      !std::isfinite(value.y) || !std::isfinite(value.z)) {
    return false;
  }
  const auto norm = value.w * value.w + value.x * value.x +
                    value.y * value.y + value.z * value.z;
  return std::abs(norm - 1.0) <= quaternion_tolerance;
}

bool is_valid(const Transform& value) noexcept {
  return is_finite(value.translation) && is_valid(value.rotation) &&
         is_finite(value.scale) && value.scale.x > 0.0 && value.scale.y > 0.0 &&
         value.scale.z > 0.0;
}

model::Vec3d operator+(model::Vec3d left, model::Vec3d right) noexcept {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

model::Vec3d operator-(model::Vec3d left, model::Vec3d right) noexcept {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

model::Vec3d operator*(model::Vec3d value, double scalar) noexcept {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}

model::Vec3d operator/(model::Vec3d value, double scalar) noexcept {
  return {value.x / scalar, value.y / scalar, value.z / scalar};
}

double dot(model::Vec3d left, model::Vec3d right) noexcept {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

model::Vec3d cross(model::Vec3d left, model::Vec3d right) noexcept {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

double length(model::Vec3d value) noexcept {
  return std::sqrt(dot(value, value));
}

model::Vec3d normalized(model::Vec3d value) noexcept {
  const auto magnitude = length(value);
  return magnitude > 0.0 ? value / magnitude : model::Vec3d{};
}

Quaterniond normalized(Quaterniond value) noexcept {
  const auto magnitude = std::sqrt(value.w * value.w + value.x * value.x +
                                   value.y * value.y + value.z * value.z);
  if (!(magnitude > 0.0) || !std::isfinite(magnitude)) {
    return {};
  }
  return {value.w / magnitude, value.x / magnitude, value.y / magnitude,
          value.z / magnitude};
}

Quaterniond conjugate(Quaterniond value) noexcept {
  return {value.w, -value.x, -value.y, -value.z};
}

Quaterniond operator*(Quaterniond left, Quaterniond right) noexcept {
  return {left.w * right.w - left.x * right.x - left.y * right.y -
              left.z * right.z,
          left.w * right.x + left.x * right.w + left.y * right.z -
              left.z * right.y,
          left.w * right.y - left.x * right.z + left.y * right.w +
              left.z * right.x,
          left.w * right.z + left.x * right.y - left.y * right.x +
              left.z * right.w};
}

Quaterniond quaternion_from_axis_angle(model::Vec3d axis,
                                       double radians) noexcept {
  const auto unit_axis = normalized(axis);
  const auto half = radians * 0.5;
  const auto sine = std::sin(half);
  return normalized(Quaterniond{std::cos(half), unit_axis.x * sine,
                                unit_axis.y * sine, unit_axis.z * sine});
}

model::Vec3d rotate(Quaterniond rotation, model::Vec3d value) noexcept {
  const auto unit = normalized(rotation);
  const auto vector = Quaterniond{0.0, value.x, value.y, value.z};
  const auto result = unit * vector * conjugate(unit);
  return {result.x, result.y, result.z};
}

Matrix4d matrix(const Transform& transform) noexcept {
  const auto rotation = normalized(transform.rotation);
  const auto xx = rotation.x * rotation.x;
  const auto yy = rotation.y * rotation.y;
  const auto zz = rotation.z * rotation.z;
  const auto xy = rotation.x * rotation.y;
  const auto xz = rotation.x * rotation.z;
  const auto yz = rotation.y * rotation.z;
  const auto wx = rotation.w * rotation.x;
  const auto wy = rotation.w * rotation.y;
  const auto wz = rotation.w * rotation.z;

  Matrix4d result;
  result.values = {
      (1.0 - 2.0 * (yy + zz)) * transform.scale.x,
      (2.0 * (xy + wz)) * transform.scale.x,
      (2.0 * (xz - wy)) * transform.scale.x,
      0.0,
      (2.0 * (xy - wz)) * transform.scale.y,
      (1.0 - 2.0 * (xx + zz)) * transform.scale.y,
      (2.0 * (yz + wx)) * transform.scale.y,
      0.0,
      (2.0 * (xz + wy)) * transform.scale.z,
      (2.0 * (yz - wx)) * transform.scale.z,
      (1.0 - 2.0 * (xx + yy)) * transform.scale.z,
      0.0,
      transform.translation.x,
      transform.translation.y,
      transform.translation.z,
      1.0};
  return result;
}

Matrix4d operator*(const Matrix4d& left, const Matrix4d& right) noexcept {
  Matrix4d result;
  result.values.fill(0.0);
  for (std::size_t column = 0; column < 4; ++column) {
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t inner = 0; inner < 4; ++inner) {
        result.values[column * 4 + row] +=
            left(row, inner) * right(inner, column);
      }
    }
  }
  return result;
}

model::Vec3d transform_point(const Matrix4d& transform,
                             model::Vec3d point) noexcept {
  return {transform(0, 0) * point.x + transform(0, 1) * point.y +
              transform(0, 2) * point.z + transform(0, 3),
          transform(1, 0) * point.x + transform(1, 1) * point.y +
              transform(1, 2) * point.z + transform(1, 3),
          transform(2, 0) * point.x + transform(2, 1) * point.y +
              transform(2, 2) * point.z + transform(2, 3)};
}

}  // namespace molshredder::scene
