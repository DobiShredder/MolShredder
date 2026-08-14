#include <cmath>
#include <iostream>
#include <limits>
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
