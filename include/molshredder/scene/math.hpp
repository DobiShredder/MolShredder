#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include "molshredder/model/coordinates.hpp"

namespace molshredder::scene {

struct Quaterniond {
  double w{1.0};
  double x{};
  double y{};
  double z{};

  friend bool operator==(const Quaterniond&, const Quaterniond&) = default;
};

struct Matrix4d {
  std::array<double, 16> values{
      1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
      0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};

  [[nodiscard]] double operator()(std::size_t row,
                                  std::size_t column) const noexcept {
    return values[column * 4 + row];
  }

  friend bool operator==(const Matrix4d&, const Matrix4d&) = default;
};

struct Transform {
  model::Vec3d translation{};
  Quaterniond rotation{};
  model::Vec3d scale{1.0, 1.0, 1.0};

  friend bool operator==(const Transform&, const Transform&) = default;
};

[[nodiscard]] bool is_finite(model::Vec3d value) noexcept;
[[nodiscard]] bool is_valid(Quaterniond value) noexcept;
[[nodiscard]] bool is_valid(const Transform& value) noexcept;

[[nodiscard]] model::Vec3d operator+(model::Vec3d left,
                                     model::Vec3d right) noexcept;
[[nodiscard]] model::Vec3d operator-(model::Vec3d left,
                                     model::Vec3d right) noexcept;
[[nodiscard]] model::Vec3d operator*(model::Vec3d value,
                                     double scalar) noexcept;
[[nodiscard]] model::Vec3d operator/(model::Vec3d value,
                                     double scalar) noexcept;
[[nodiscard]] double dot(model::Vec3d left, model::Vec3d right) noexcept;
[[nodiscard]] model::Vec3d cross(model::Vec3d left,
                                 model::Vec3d right) noexcept;
[[nodiscard]] double length(model::Vec3d value) noexcept;
[[nodiscard]] model::Vec3d normalized(model::Vec3d value) noexcept;

[[nodiscard]] Quaterniond normalized(Quaterniond value) noexcept;
[[nodiscard]] Quaterniond conjugate(Quaterniond value) noexcept;
[[nodiscard]] Quaterniond operator*(Quaterniond left,
                                    Quaterniond right) noexcept;
[[nodiscard]] Quaterniond quaternion_from_axis_angle(model::Vec3d axis,
                                                     double radians) noexcept;
[[nodiscard]] model::Vec3d rotate(Quaterniond rotation,
                                  model::Vec3d value) noexcept;

[[nodiscard]] Matrix4d matrix(const Transform& transform) noexcept;
[[nodiscard]] Matrix4d operator*(const Matrix4d& left,
                                 const Matrix4d& right) noexcept;
[[nodiscard]] model::Vec3d transform_point(const Matrix4d& transform,
                                           model::Vec3d point) noexcept;

}  // namespace molshredder::scene
