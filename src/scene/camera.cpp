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
  if (!is_finite(parameters.target) || !is_finite(parameters.model_origin) ||
      !is_valid(parameters.orientation)) {
    return invalid(
        "camera target and model origin must be finite and orientation "
        "normalized");
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

operation::Result<Camera> Camera::with_projection(
    ProjectionMode mode,
    std::optional<double> vertical_field_of_view_degrees,
    bool preserve_scale) const {
  auto updated = parameters_;
  const auto previous_span = vertical_span_at_target();
  if (vertical_field_of_view_degrees.has_value()) {
    const auto degrees = *vertical_field_of_view_degrees;
    if (!std::isfinite(degrees) || degrees <= 0.0 || degrees >= 180.0) {
      return operation::Result<Camera>::failure(invalid(
          "camera field of view must be between 0 and 180 degrees"));
    }
    updated.vertical_field_of_view_radians =
        degrees * std::numbers::pi / 180.0;
  }
  updated.projection = mode;
  if (preserve_scale) {
    if (mode == ProjectionMode::orthographic) {
      updated.orthographic_height = previous_span;
    } else {
      const auto next_distance =
          previous_span /
          (2.0 * std::tan(updated.vertical_field_of_view_radians * 0.5));
      const auto distance_scale = next_distance / parameters_.distance;
      updated.distance = next_distance;
      updated.near_clip *= distance_scale;
      updated.far_clip *= distance_scale;
    }
  }
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
  const auto translation = right() * (-delta_x * units_per_pixel) +
                           up() * (delta_y * units_per_pixel);
  updated.target = updated.target + translation;
  updated.model_origin = updated.model_origin + translation;
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

operation::Result<Camera> Camera::move_axis(CameraAxis axis,
                                            double distance) const {
  if (!std::isfinite(distance)) {
    return operation::Result<Camera>::failure(
        invalid("camera axis movement must be finite"));
  }
  auto updated = parameters_;
  switch (axis) {
  case CameraAxis::x:
    updated.target = updated.target - right() * distance;
    break;
  case CameraAxis::y:
    updated.target = updated.target - up() * distance;
    break;
  case CameraAxis::z: {
    const auto new_distance = updated.distance - distance;
    if (!finite_positive(new_distance)) {
      return operation::Result<Camera>::failure(invalid(
          "camera z movement must leave a positive camera distance"));
    }
    if (updated.projection == ProjectionMode::orthographic) {
      updated.orthographic_height *= new_distance / updated.distance;
    }
    updated.distance = new_distance;
    updated.near_clip -= distance;
    updated.far_clip -= distance;
    break;
  }
  }
  return create(updated);
}

operation::Result<Camera>
Camera::turn_axis_degrees(CameraAxis axis, double angle_degrees) const {
  if (!std::isfinite(angle_degrees)) {
    return operation::Result<Camera>::failure(
        invalid("camera turn angle must be finite"));
  }
  model::Vec3d local_axis;
  switch (axis) {
  case CameraAxis::x: local_axis.x = 1.0; break;
  case CameraAxis::y: local_axis.y = 1.0; break;
  case CameraAxis::z: local_axis.z = 1.0; break;
  }
  const auto reduced_degrees = std::remainder(angle_degrees, 360.0);
  const auto local_turn = quaternion_from_axis_angle(
      local_axis, reduced_degrees * std::numbers::pi / 180.0);
  auto updated = parameters_;
  updated.orientation = normalized(updated.orientation * local_turn);
  const auto local_target_offset = rotate(
      conjugate(parameters_.orientation),
      parameters_.target - parameters_.model_origin);
  updated.target = updated.model_origin +
                   rotate(updated.orientation, local_target_offset);
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
  updated.model_origin = center;
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
  const auto coverage = radius * padding;
  updated.near_clip = std::max(1.0e-4, updated.distance - coverage * 1.05);
  updated.far_clip =
      std::max(updated.near_clip + 1.0e-4,
               updated.distance + coverage * 1.05);
  return create(updated);
}

operation::Result<Camera> Camera::frame_box(model::Vec3d center,
                                            model::Vec3d half_extents,
                                            double padding,
                                            double minimum_radius) const {
  if (!is_finite(center) || !is_finite(half_extents) ||
      half_extents.x < 0.0 || half_extents.y < 0.0 ||
      half_extents.z < 0.0 || !std::isfinite(padding) || padding < 1.0 ||
      !finite_positive(minimum_radius)) {
    return operation::Result<Camera>::failure(invalid(
        "camera box framing requires finite non-negative half extents, "
        "padding of at least one, and a positive minimum radius"));
  }
  const auto padded = half_extents * padding;
  const auto vertical_half_field =
      parameters_.vertical_field_of_view_radians * 0.5;
  const auto horizontal_half_field = std::atan(
      std::tan(vertical_half_field) * parameters_.aspect_ratio);
  auto updated = parameters_;
  updated.target = center;
  updated.model_origin = center;
  if (updated.projection == ProjectionMode::perspective) {
    const auto box_distance = std::max(
        padded.z + padded.y / std::tan(vertical_half_field),
        padded.z + padded.x / std::tan(horizontal_half_field));
    const auto radius_distance = std::max(
        minimum_radius / std::sin(vertical_half_field),
        minimum_radius / std::sin(horizontal_half_field));
    updated.distance = std::max(box_distance, radius_distance);
  } else {
    updated.orthographic_height = std::max(
        {2.0 * padded.y, 2.0 * padded.x / updated.aspect_ratio,
         2.0 * minimum_radius / std::min(1.0, updated.aspect_ratio)});
    updated.distance =
        std::max(updated.distance, padded.z + minimum_radius);
  }
  const auto margin = std::max(
      1.0e-4,
      0.05 * std::max({padded.x, padded.y, padded.z, minimum_radius}));
  updated.near_clip =
      std::max(1.0e-4, updated.distance - padded.z - margin);
  updated.far_clip = std::max(updated.near_clip + 1.0e-4,
                              updated.distance + padded.z + margin);
  return create(updated);
}

}  // namespace molshredder::scene
