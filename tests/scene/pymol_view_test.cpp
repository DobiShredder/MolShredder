#include <cmath>
#include <iostream>
#include <numbers>
#include <string_view>

#include "molshredder/scene/camera.hpp"
#include "molshredder/scene/pymol_view.hpp"

namespace {

bool close(double left, double right, double tolerance = 1.0e-10) {
  return std::abs(left - right) <= tolerance;
}

bool close(molshredder::model::Vec3d left,
           molshredder::model::Vec3d right,
           double tolerance = 1.0e-10) {
  return close(left.x, right.x, tolerance) &&
         close(left.y, right.y, tolerance) &&
         close(left.z, right.z, tolerance);
}

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

bool close(const molshredder::scene::PyMolView18& left,
           const molshredder::scene::PyMolView18& right,
           double tolerance = 1.0e-10) {
  for (std::size_t index = 0; index < left.values.size(); ++index) {
    if (!close(left.values[index], right.values[index], tolerance))
      return false;
  }
  return true;
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;

  const auto default_camera = scene::Camera::create();
  const auto default_view = default_camera.has_value()
                                ? scene::to_pymol_view(default_camera.value())
                                : operation::Result<scene::PyMolView18>::failure(
                                      default_camera.error());
  passed &= expect(default_camera.has_value() && default_view.has_value() &&
                       default_view.value().values[0] == 1.0 &&
                       default_view.value().values[4] == 1.0 &&
                       default_view.value().values[8] == 1.0 &&
                       default_view.value().values[9] == 0.0 &&
                       default_view.value().values[10] == 0.0 &&
                       default_view.value().values[11] == -100.0 &&
                       close(default_view.value().values[17], -45.0),
                   "default camera must map to PyMOL's identity view");

  const scene::PyMolView18 fixture{{
      0.0, 1.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
      2.0, -3.0, -40.0, 10.0, 20.0, 30.0, 0.5, 100.0, -60.0}};
  const auto imported = scene::from_pymol_view(fixture);
  const auto exported = imported.has_value()
                            ? scene::to_pymol_view(imported.value())
                            : operation::Result<scene::PyMolView18>::failure(
                                  imported.error());
  passed &= expect(imported.has_value() && exported.has_value() &&
                       close(imported.value().position(),
                             model::Vec3d{13.0, 22.0, 70.0}) &&
                       close(imported.value().parameters().target,
                             model::Vec3d{13.0, 22.0, 30.0}) &&
                       imported.value().parameters().model_origin ==
                           model::Vec3d{10.0, 20.0, 30.0} &&
                       imported.value().parameters().projection ==
                           scene::ProjectionMode::perspective &&
                       close(imported.value()
                                 .parameters()
                                 .vertical_field_of_view_radians,
                             std::numbers::pi / 3.0) &&
                       close(exported.value(), fixture),
                   "asymmetric PyMOL position/origin view must round-trip");

  auto orthographic = fixture;
  orthographic.values[17] = 30.0;
  const auto imported_orthographic = scene::from_pymol_view(orthographic);
  const auto exported_orthographic =
      imported_orthographic.has_value()
          ? scene::to_pymol_view(imported_orthographic.value())
          : operation::Result<scene::PyMolView18>::failure(
                imported_orthographic.error());
  passed &= expect(
      imported_orthographic.has_value() && exported_orthographic.has_value() &&
          imported_orthographic.value().parameters().projection ==
              scene::ProjectionMode::orthographic &&
          close(imported_orthographic.value().parameters().orthographic_height,
                80.0 * std::tan(std::numbers::pi / 12.0)) &&
          close(exported_orthographic.value().values[17], 30.0),
      "orthographic PyMOL FOV must preserve its effective vertical span");

  const auto parsed = scene::parse_pymol_view(
      "set_view (\\\n 0, 1, 0, -1, 0, 0, 0, 0, 1, \\\n       2, -3, -40, 10, 20, 30, .5, 100, -60 )");
  const auto reparsed = parsed.has_value()
                            ? scene::parse_pymol_view(
                                  scene::format_pymol_view(parsed.value()))
                            : operation::Result<scene::PyMolView18>::failure(
                                  parsed.error());
  passed &= expect(parsed.has_value() && parsed.value() == fixture &&
                       reparsed.has_value() && reparsed.value() == fixture &&
                       !scene::parse_pymol_view("1, 2, 3").has_value() &&
                       !scene::parse_pymol_view(
                            "1,0,0,0,1,0,0,0,nan,0,0,-1,0,0,0,.1,10,-45")
                            .has_value(),
                   "parser must accept PyMOL text and reject bad arity/numbers");

  auto invalid_rotation = fixture;
  invalid_rotation.values[0] = 2.0;
  auto invalid_clip = fixture;
  invalid_clip.values[15] = 101.0;
  passed &= expect(!scene::from_pymol_view(invalid_rotation).has_value() &&
                       !scene::from_pymol_view(invalid_clip).has_value(),
                   "invalid rotation and clipping must be rejected");

  auto native_parameters = scene::CameraParameters{};
  native_parameters.projection = scene::ProjectionMode::orthographic;
  native_parameters.orthographic_height = 37.0;
  native_parameters.distance = 80.0;
  const auto native_camera = scene::Camera::create(native_parameters);
  const auto native_export =
      native_camera.has_value()
          ? scene::to_pymol_view(native_camera.value())
          : operation::Result<scene::PyMolView18>::failure(
                native_camera.error());
  const auto native_import =
      native_export.has_value()
          ? scene::from_pymol_view(native_export.value())
          : operation::Result<scene::Camera>::failure(native_export.error());
  passed &= expect(native_import.has_value() &&
                       close(native_import.value()
                                 .parameters()
                                 .orthographic_height,
                             37.0),
                   "native orthographic height must survive PyMOL conversion");

  const auto panned = default_camera.value().pan_pixels(20.0, -10.0, 500.0);
  passed &= expect(
      panned.has_value() &&
          close(panned.value().parameters().target,
                panned.value().parameters().model_origin),
      "native pan must translate target and model origin together");

  auto half_turn = default_view.value();
  half_turn.values[0] = 1.0;
  half_turn.values[1] = 0.0;
  half_turn.values[2] = 0.0;
  half_turn.values[3] = 0.0;
  half_turn.values[4] = -1.0;
  half_turn.values[5] = 0.0;
  half_turn.values[6] = 0.0;
  half_turn.values[7] = 0.0;
  half_turn.values[8] = -1.0;
  half_turn.values[9] = 8.0;
  const auto positive_hand = scene::interpolate_pymol_view(
      default_view.value(), half_turn, 0.5, 1);
  const auto negative_hand = scene::interpolate_pymol_view(
      default_view.value(), half_turn, 0.5, -1);
  const auto quarter = scene::interpolate_pymol_view(
      default_view.value(), half_turn, 0.25, 1);
  const auto endpoint = scene::interpolate_pymol_view(
      default_view.value(), half_turn, 1.0, 1);
  passed &= expect(
      close(scene::pymol_animation_fraction(0.25), 0.125) &&
          close(scene::pymol_animation_fraction(0.75), 0.875) &&
          positive_hand.has_value() && negative_hand.has_value() &&
          quarter.has_value() && endpoint.has_value() &&
          positive_hand.value().values[5] *
                  negative_hand.value().values[5] <
              0.0 &&
          close(quarter.value().values[9], 1.0) &&
          close(endpoint.value(), half_turn) &&
          !scene::interpolate_pymol_view(default_view.value(), half_turn,
                                         -0.1, 1)
               .has_value() &&
          !scene::interpolate_pymol_view(default_view.value(), half_turn,
                                         0.5, 2)
               .has_value(),
      "PyMOL power-2 animation and 180-degree hand must be deterministic");

  return passed ? 0 : 1;
}
