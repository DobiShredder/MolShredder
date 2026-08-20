#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <string_view>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/scene/camera.hpp"
#include "molshredder/scene/math.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool near(double left, double right, double tolerance = 1.0e-9) {
  return std::abs(left - right) <= tolerance;
}

bool near(molshredder::model::Vec3d left,
          molshredder::model::Vec3d right,
          double tolerance = 1.0e-9) {
  return near(left.x, right.x, tolerance) &&
         near(left.y, right.y, tolerance) &&
         near(left.z, right.z, tolerance);
}

}  // namespace

int main() {
  using namespace molshredder;
  using scene::operator-;

  bool passed = true;
  const auto camera = scene::Camera::create();
  passed &= expect(camera.has_value(), "default camera must be valid");
  if (!camera.has_value()) {
    return 1;
  }
  passed &= expect(near(camera.value().position(), {0.0, 0.0, 100.0}) &&
                       near(camera.value().forward(), {0.0, 0.0, -1.0}) &&
                       near(camera.value().right(), {1.0, 0.0, 0.0}) &&
                       near(camera.value().up(), {0.0, 1.0, 0.0}),
                   "default camera basis must be right handed and view -Z");
  passed &= expect(near(scene::transform_point(camera.value().view_matrix(),
                                               camera.value().position()),
                        {}) &&
                       near(scene::transform_point(camera.value().view_matrix(),
                                                   {}),
                            {0.0, 0.0, -100.0}),
                   "view matrix must map eye to origin and target to -distance");

  const auto viewport = camera.value().with_viewport(1920.0, 1080.0);
  passed &= expect(viewport.has_value() &&
                       near(viewport.value().parameters().aspect_ratio,
                            1920.0 / 1080.0) &&
                       !camera.value().with_viewport(0.0, 100.0).has_value(),
                   "viewport must update aspect and reject zero extent");

  const auto orbit = camera.value().orbit_pixels(120.0, -40.0);
  passed &= expect(orbit.has_value() &&
                       scene::is_valid(orbit.value().parameters().orientation) &&
                       near(scene::length(orbit.value().position() -
                                          orbit.value().parameters().target),
                            camera.value().parameters().distance) &&
                       !near(orbit.value().position(), camera.value().position()),
                   "orbit must preserve distance and normalized orientation");

  const auto pan = camera.value().pan_pixels(100.0, 50.0, 1000.0);
  const auto units_per_pixel =
      camera.value().vertical_span_at_target() / 1000.0;
  passed &= expect(pan.has_value() &&
                       near(pan.value().parameters().target,
                            {-100.0 * units_per_pixel,
                             50.0 * units_per_pixel, 0.0}) &&
                       near(pan.value().position() -
                                pan.value().parameters().target,
                            camera.value().position() -
                                camera.value().parameters().target),
                   "pan must translate eye and target at viewport world scale");

  scene::CameraInteractionConfig bounded;
  bounded.minimum_distance = 10.0;
  bounded.maximum_distance = 200.0;
  const auto zoom_in = camera.value().dolly(-1000.0, bounded);
  const auto zoom_out = camera.value().dolly(1000.0, bounded);
  passed &= expect(zoom_in.has_value() && zoom_out.has_value() &&
                       near(zoom_in.value().parameters().distance, 10.0) &&
                       near(zoom_out.value().parameters().distance, 200.0),
                   "perspective dolly must clamp before crossing target");

  auto orthographic_parameters = camera.value().parameters();
  orthographic_parameters.projection = scene::ProjectionMode::orthographic;
  orthographic_parameters.orthographic_height = 80.0;
  const auto orthographic = scene::Camera::create(orthographic_parameters);
  const auto orthographic_zoom = orthographic.value().dolly(1.0);
  passed &= expect(orthographic_zoom.has_value() &&
                       orthographic_zoom.value().parameters().orthographic_height >
                           80.0 &&
                       near(orthographic_zoom.value().parameters().distance,
                            orthographic.value().parameters().distance),
                   "orthographic dolly must scale span but retain eye distance");

  const auto perspective_span = camera.value().vertical_span_at_target();
  const auto projected_orthographic = camera.value().with_projection(
      scene::ProjectionMode::orthographic);
  const auto projected_perspective =
      projected_orthographic.value().with_projection(
          scene::ProjectionMode::perspective, 60.0, true);
  passed &= expect(
      projected_orthographic.has_value() &&
          projected_perspective.has_value() &&
          near(projected_orthographic.value().parameters().orthographic_height,
               perspective_span) &&
          near(projected_orthographic.value().vertical_span_at_target(),
               perspective_span) &&
          near(projected_perspective.value().vertical_span_at_target(),
               perspective_span) &&
          near(projected_perspective.value()
                   .parameters()
                   .vertical_field_of_view_radians,
               std::numbers::pi / 3.0) &&
          near(projected_perspective.value().parameters().near_clip /
                   projected_perspective.value().parameters().distance,
               camera.value().parameters().near_clip /
                   camera.value().parameters().distance) &&
          near(projected_perspective.value().parameters().far_clip /
                   projected_perspective.value().parameters().distance,
               camera.value().parameters().far_clip /
                   camera.value().parameters().distance),
      "projection conversion must preserve target-plane scale and normalized clipping");
  const auto raw_orthographic = camera.value().with_projection(
      scene::ProjectionMode::orthographic, 30.0, false);
  passed &= expect(
      raw_orthographic.has_value() &&
          raw_orthographic.value().parameters().orthographic_height ==
              camera.value().parameters().orthographic_height &&
          raw_orthographic.value().parameters().distance ==
              camera.value().parameters().distance &&
          !camera.value()
               .with_projection(scene::ProjectionMode::perspective, 0.0)
               .has_value() &&
          !camera.value()
               .with_projection(scene::ProjectionMode::perspective, 180.0)
               .has_value(),
      "raw projection switching must preserve inactive parameters and reject invalid degree FOV");

  auto navigation_parameters = camera.value().parameters();
  navigation_parameters.target = {3.0, 4.0, 5.0};
  navigation_parameters.model_origin = {1.0, 2.0, 3.0};
  navigation_parameters.distance = 50.0;
  navigation_parameters.near_clip = 10.0;
  navigation_parameters.far_clip = 100.0;
  const auto navigation = scene::Camera::create(navigation_parameters);
  const auto moved_x = navigation.value().move_axis(scene::CameraAxis::x, 5.0);
  const auto moved_y = navigation.value().move_axis(scene::CameraAxis::y, -2.0);
  const auto moved_z = navigation.value().move_axis(scene::CameraAxis::z, 5.0);
  passed &= expect(
      moved_x.has_value() && moved_y.has_value() && moved_z.has_value() &&
          near(moved_x.value().parameters().target, {-2.0, 4.0, 5.0}) &&
          near(moved_y.value().parameters().target, {3.0, 6.0, 5.0}) &&
          moved_x.value().parameters().model_origin ==
              navigation_parameters.model_origin &&
          near(moved_z.value().parameters().distance, 45.0) &&
          near(moved_z.value().parameters().near_clip, 5.0) &&
          near(moved_z.value().parameters().far_clip, 95.0),
      "axis move must translate in camera space and preserve the model pivot");

  auto orthographic_move_parameters = orthographic.value().parameters();
  orthographic_move_parameters.near_clip = 20.0;
  orthographic_move_parameters.far_clip = 200.0;
  const auto orthographic_move =
      scene::Camera::create(orthographic_move_parameters)
          .value()
          .move_axis(scene::CameraAxis::z, 10.0);
  passed &= expect(
      orthographic_move.has_value() &&
          near(orthographic_move.value().parameters().distance, 90.0) &&
          near(orthographic_move.value().parameters().orthographic_height,
               72.0),
      "orthographic z move must preserve encoded FOV by scaling view height");

  auto turn_parameters = camera.value().parameters();
  turn_parameters.target = {2.0, 0.0, 0.0};
  turn_parameters.model_origin = {};
  turn_parameters.distance = 10.0;
  const auto turn_camera = scene::Camera::create(turn_parameters);
  const auto turned =
      turn_camera.value().turn_axis_degrees(scene::CameraAxis::z, 90.0);
  passed &= expect(
      turned.has_value() &&
          near(turned.value().parameters().target, {0.0, 2.0, 0.0}) &&
          near(turned.value().position(), {0.0, 2.0, 10.0}) &&
          turned.value().parameters().model_origin == model::Vec3d{} &&
          near(turned.value().parameters().distance, 10.0) &&
          scene::is_valid(turned.value().parameters().orientation),
      "axis turn must rotate camera target and eye around model origin");

  passed &= expect(
      !navigation.value()
           .move_axis(scene::CameraAxis::z, 45.0)
           .has_value() &&
          !navigation.value()
               .turn_axis_degrees(
                   scene::CameraAxis::x,
                   std::numeric_limits<double>::infinity())
               .has_value(),
      "axis navigation must reject clip crossing and non-finite angle");

  const auto framed = viewport.value().frame_sphere({4.0, 5.0, 6.0}, 10.0);
  passed &= expect(framed.has_value() &&
                       framed.value().parameters().target ==
                           model::Vec3d{4.0, 5.0, 6.0} &&
                       framed.value().parameters().distance > 10.0,
                   "perspective framing must center and fit bounding sphere");
  const auto orthographic_framed =
      orthographic.value().frame_sphere({1.0, 2.0, 3.0}, 5.0, 1.2);
  passed &= expect(orthographic_framed.has_value() &&
                       near(orthographic_framed.value()
                                .parameters()
                                .orthographic_height,
                            12.0),
                   "orthographic framing must fit padded diameter");
  const auto framed_box = viewport.value().frame_box(
      {4.0, -2.0, 1.0}, {10.0, 2.0, 1.0}, 1.0, 0.5);
  passed &= expect(
      framed_box.has_value() &&
          framed_box.value().parameters().target ==
              model::Vec3d{4.0, -2.0, 1.0} &&
          framed_box.value().parameters().model_origin ==
              model::Vec3d{4.0, -2.0, 1.0} &&
          framed_box.value().parameters().near_clip > 0.0 &&
          framed_box.value().parameters().far_clip >
              framed_box.value().parameters().near_clip,
      "box framing must fit oriented extents and preserve camera invariants");
  passed &= expect(
      !viewport.value().frame_box({}, {-1.0, 0.0, 0.0}).has_value(),
      "box framing must reject negative half extents");

  auto invalid_parameters = camera.value().parameters();
  invalid_parameters.orientation = {2.0, 0.0, 0.0, 0.0};
  passed &= expect(!scene::Camera::create(invalid_parameters).has_value(),
                   "camera must reject non-normalized orientation");
  invalid_parameters = camera.value().parameters();
  invalid_parameters.near_clip = 10.0;
  invalid_parameters.far_clip = 1.0;
  passed &= expect(!scene::Camera::create(invalid_parameters).has_value() &&
                       !camera.value().pan_pixels(0.0, 0.0, 0.0).has_value() &&
                       !camera.value()
                            .orbit_pixels(
                                std::numeric_limits<double>::quiet_NaN(), 0.0)
                            .has_value() &&
                       !camera.value().frame_sphere({}, 0.0).has_value(),
                   "camera boundaries must reject invalid numeric input");

  return passed ? 0 : 1;
}
