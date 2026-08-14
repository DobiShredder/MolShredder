#include "molshredder/scene/camera.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::scene {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

bool finite_positive(double value) {
  return std::isfinite(value) && value > 0.0;
}

std::optional<operation::Error> validate(const CameraParameters& parameters) {
  if (!is_finite(parameters.target) || !is_valid(parameters.orientation)) {
    return invalid("camera target must be finite and orientation normalized");
  }
  if (!finite_positive(parameters.distance)) {
    return invalid("camera distance must be finite and positive");
  }
  if (!finite_positive(parameters.vertical_field_of_view_radians) ||
      parameters.vertical_field_of_view_radians >= std::numbers::pi) {
    return invalid("camera vertical field of view must be between 0 and pi");
  }
  if (!finite_positive(parameters.orthographic_height) ||
      !finite_positive(parameters.aspect_ratio)) {
    return invalid("camera orthographic height and aspect ratio must be positive");
  }
  if (!finite_positive(parameters.near_clip) ||
      !std::isfinite(parameters.far_clip) ||
      parameters.far_clip <= parameters.near_clip) {
    return invalid("camera clip range must satisfy 0 < near < far");
  }
  return std::nullopt;
}

std::optional<operation::Error> validate(
    const CameraInteractionConfig& config) {
  if (!finite_positive(config.orbit_radians_per_pixel) ||
      !finite_positive(config.dolly_log_scale_per_unit) ||
      !finite_positive(config.minimum_distance) ||
      !finite_positive(config.maximum_distance) ||
      config.minimum_distance > config.maximum_distance ||
      !finite_positive(config.minimum_orthographic_height) ||
      !finite_positive(config.maximum_orthographic_height) ||
      config.minimum_orthographic_height >
          config.maximum_orthographic_height) {
    return invalid("camera interaction limits and sensitivities are invalid");
  }
  return std::nullopt;
}

}  // namespace

operation::Result<Camera> Camera::create(CameraParameters parameters) {
  if (const auto error = validate(parameters); error.has_value()) {
    return operation::Result<Camera>::failure(*error);
  }
  return operation::Result<Camera>::success(Camera{parameters});
}

model::Vec3d Camera::position() const noexcept {
  return parameters_.target +
         rotate(parameters_.orientation,
                model::Vec3d{0.0, 0.0, parameters_.distance});
}

model::Vec3d Camera::forward() const noexcept {
  return rotate(parameters_.orientation, model::Vec3d{0.0, 0.0, -1.0});
}

model::Vec3d Camera::right() const noexcept {
  return rotate(parameters_.orientation, model::Vec3d{1.0, 0.0, 0.0});
}

model::Vec3d Camera::up() const noexcept {
  return rotate(parameters_.orientation, model::Vec3d{0.0, 1.0, 0.0});
}

Matrix4d Camera::view_matrix() const noexcept {
  const auto camera_position = position();
  const auto camera_right = right();
  const auto camera_up = up();
  const auto backward = forward() * -1.0;
  Matrix4d result;
  result.values = {
      camera_right.x,
      camera_up.x,
      backward.x,
      0.0,
      camera_right.y,
      camera_up.y,
      backward.y,
      0.0,
      camera_right.z,
      camera_up.z,
      backward.z,
      0.0,
      -dot(camera_right, camera_position),
      -dot(camera_up, camera_position),
      -dot(backward, camera_position),
      1.0};
  return result;
}

double Camera::vertical_span_at_target() const noexcept {
  if (parameters_.projection == ProjectionMode::orthographic) {
    return parameters_.orthographic_height;
  }
  return 2.0 * parameters_.distance *
         std::tan(parameters_.vertical_field_of_view_radians * 0.5);
}

operation::Result<Camera> Camera::with_viewport(double width_pixels,
                                                double height_pixels) const {
  if (!finite_positive(width_pixels) || !finite_positive(height_pixels)) {
    return operation::Result<Camera>::failure(
        invalid("camera viewport dimensions must be finite and positive"));
  }
  auto updated = parameters_;
  updated.aspect_ratio = width_pixels / height_pixels;
  return create(updated);
}

operation::Result<Camera> Camera::orbit_pixels(
    double delta_x, double delta_y, CameraInteractionConfig config) const {
  if (!std::isfinite(delta_x) || !std::isfinite(delta_y)) {
    return operation::Result<Camera>::failure(
        invalid("camera orbit deltas must be finite"));
  }
  if (const auto error = validate(config); error.has_value()) {
    return operation::Result<Camera>::failure(*error);
  }
  const auto yaw = quaternion_from_axis_angle(
      model::Vec3d{0.0, 1.0, 0.0},
      -delta_x * config.orbit_radians_per_pixel);
  const auto yawed = normalized(yaw * parameters_.orientation);
  const auto pitch_axis = rotate(yawed, model::Vec3d{1.0, 0.0, 0.0});
  const auto pitch = quaternion_from_axis_angle(
      pitch_axis, -delta_y * config.orbit_radians_per_pixel);
  auto updated = parameters_;
  updated.orientation = normalized(pitch * yawed);
  return create(updated);
}

operation::Result<Camera> Camera::pan_pixels(
    double delta_x, double delta_y, double viewport_height_pixels) const {
  if (!std::isfinite(delta_x) || !std::isfinite(delta_y) ||
      !finite_positive(viewport_height_pixels)) {
    return operation::Result<Camera>::failure(
        invalid("camera pan delta and viewport height must be finite"));
  }
  const auto units_per_pixel =
      vertical_span_at_target() / viewport_height_pixels;
  auto updated = parameters_;
  updated.target = updated.target - right() * (delta_x * units_per_pixel) +
                   up() * (delta_y * units_per_pixel);
  return create(updated);
}

operation::Result<Camera> Camera::dolly(
    double delta, CameraInteractionConfig config) const {
  if (!std::isfinite(delta)) {
    return operation::Result<Camera>::failure(
        invalid("camera dolly delta must be finite"));
  }
  if (const auto error = validate(config); error.has_value()) {
    return operation::Result<Camera>::failure(*error);
  }
  const auto scale = std::exp(delta * config.dolly_log_scale_per_unit);
  if (!std::isfinite(scale)) {
    return operation::Result<Camera>::failure(
        invalid("camera dolly delta exceeds numeric range"));
  }
  auto updated = parameters_;
  if (updated.projection == ProjectionMode::perspective) {
    updated.distance = std::clamp(updated.distance * scale,
                                  config.minimum_distance,
                                  config.maximum_distance);
  } else {
    updated.orthographic_height = std::clamp(
        updated.orthographic_height * scale,
        config.minimum_orthographic_height,
        config.maximum_orthographic_height);
  }
  return create(updated);
}

operation::Result<Camera> Camera::frame_sphere(model::Vec3d center,
                                               double radius,
                                               double padding) const {
  if (!is_finite(center) || !finite_positive(radius) ||
      !std::isfinite(padding) || padding < 1.0) {
    return operation::Result<Camera>::failure(invalid(
        "camera framing requires a finite center, positive radius, and padding "
        "of at least one"));
  }
  auto updated = parameters_;
  updated.target = center;
  if (updated.projection == ProjectionMode::perspective) {
    const auto vertical_distance =
        radius * padding /
        std::sin(updated.vertical_field_of_view_radians * 0.5);
    const auto horizontal_field =
        2.0 * std::atan(std::tan(updated.vertical_field_of_view_radians * 0.5) *
                        updated.aspect_ratio);
    const auto horizontal_distance =
        radius * padding / std::sin(horizontal_field * 0.5);
    updated.distance = std::max(vertical_distance, horizontal_distance);
  } else {
    updated.orthographic_height =
        2.0 * radius * padding /
        std::min(1.0, updated.aspect_ratio);
  }
  return create(updated);
}

}  // namespace molshredder::scene
