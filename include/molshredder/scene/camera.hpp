#pragma once

#include <optional>

#include "molshredder/operation/result.hpp"
#include "molshredder/scene/math.hpp"

namespace molshredder::scene {

enum class ProjectionMode { perspective, orthographic };

enum class CameraAxis { x, y, z };

struct CameraParameters {
  model::Vec3d target{};
  // Model-space pivot retained independently from the look-at target so that
  // PyMOL's public view tuple can be imported and exported without losing its
  // rotation origin or off-axis camera translation.
  model::Vec3d model_origin{};
  Quaterniond orientation{};
  double distance{100.0};
  ProjectionMode projection{ProjectionMode::perspective};
  double vertical_field_of_view_radians{0.7853981633974483};
  double orthographic_height{100.0};
  double aspect_ratio{1.0};
  double near_clip{0.1};
  double far_clip{10000.0};

  friend bool operator==(const CameraParameters&, const CameraParameters&) =
      default;
};

struct CameraInteractionConfig {
  double orbit_radians_per_pixel{0.005};
  double dolly_log_scale_per_unit{0.1};
  double minimum_distance{1.0e-6};
  double maximum_distance{1.0e12};
  double minimum_orthographic_height{1.0e-6};
  double maximum_orthographic_height{1.0e12};
};

class Camera {
 public:
  [[nodiscard]] static operation::Result<Camera> create(
      CameraParameters parameters = {});

  [[nodiscard]] const CameraParameters& parameters() const noexcept {
    return parameters_;
  }
  [[nodiscard]] model::Vec3d position() const noexcept;
  [[nodiscard]] model::Vec3d forward() const noexcept;
  [[nodiscard]] model::Vec3d right() const noexcept;
  [[nodiscard]] model::Vec3d up() const noexcept;
  [[nodiscard]] Matrix4d view_matrix() const noexcept;
  [[nodiscard]] double vertical_span_at_target() const noexcept;

  [[nodiscard]] operation::Result<Camera> with_viewport(
      double width_pixels, double height_pixels) const;
  [[nodiscard]] operation::Result<Camera> with_projection(
      ProjectionMode mode,
      std::optional<double> vertical_field_of_view_degrees = std::nullopt,
      bool preserve_scale = true) const;
  [[nodiscard]] operation::Result<Camera> orbit_pixels(
      double delta_x, double delta_y,
      CameraInteractionConfig config = {}) const;
  [[nodiscard]] operation::Result<Camera> pan_pixels(
      double delta_x, double delta_y, double viewport_height_pixels) const;
  [[nodiscard]] operation::Result<Camera> dolly(
      double delta, CameraInteractionConfig config = {}) const;
  [[nodiscard]] operation::Result<Camera> move_axis(
      CameraAxis axis, double distance) const;
  [[nodiscard]] operation::Result<Camera> turn_axis_degrees(
      CameraAxis axis, double angle_degrees) const;
  [[nodiscard]] operation::Result<Camera> frame_sphere(
      model::Vec3d center, double radius, double padding = 1.1) const;
  [[nodiscard]] operation::Result<Camera> frame_box(
      model::Vec3d center, model::Vec3d half_extents,
      double padding = 1.0, double minimum_radius = 2.0) const;

 private:
  explicit Camera(CameraParameters parameters)
      : parameters_{parameters} {}

  CameraParameters parameters_;
};

}  // namespace molshredder::scene
