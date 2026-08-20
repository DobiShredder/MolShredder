#include "molshredder/scene/stereo.hpp"

#include <cmath>
#include <string>
#include <utility>

#include "molshredder/operation/error.hpp"
#include "molshredder/scene/math.hpp"

namespace molshredder::scene {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

operation::Result<Camera> eye_camera(const Camera &source, double side,
                                     const StereoParameters &parameters) {
  const auto distance = source.parameters().distance;
  const auto offset = distance * parameters.shift_percent / 100.0;
  const auto yaw = side * parameters.angle_scale *
                   std::atan(offset / distance) * 0.5;
  auto updated = source.parameters();
  updated.orientation = normalized(
      updated.orientation * quaternion_from_axis_angle({0.0, 1.0, 0.0}, yaw));
  const auto eye_position = source.position() + source.right() * (side * offset);
  const auto forward =
      rotate(updated.orientation, model::Vec3d{0.0, 0.0, -1.0});
  updated.target = eye_position + forward * distance;
  return Camera::create(updated);
}

} // namespace

std::string_view to_string(StereoMode mode) noexcept {
  switch (mode) {
  case StereoMode::side_by_side: return "side_by_side";
  case StereoMode::crosseye: return "crosseye";
  case StereoMode::walleye: return "walleye";
  case StereoMode::anaglyph: return "anaglyph";
  case StereoMode::quad_buffer: return "quad_buffer";
  case StereoMode::row_interleaved: return "row_interleaved";
  case StereoMode::column_interleaved: return "column_interleaved";
  case StereoMode::checkerboard: return "checkerboard";
  case StereoMode::openvr: return "openvr";
  }
  return "side_by_side";
}

operation::Result<StereoMode>
stereo_mode_from_string(std::string_view value) {
  if (value == "side_by_side" || value == "sidebyside")
    return operation::Result<StereoMode>::success(StereoMode::side_by_side);
  if (value == "crosseye")
    return operation::Result<StereoMode>::success(StereoMode::crosseye);
  if (value == "walleye")
    return operation::Result<StereoMode>::success(StereoMode::walleye);
  if (value == "anaglyph")
    return operation::Result<StereoMode>::success(StereoMode::anaglyph);
  if (value == "quad_buffer" || value == "quadbuffer")
    return operation::Result<StereoMode>::success(StereoMode::quad_buffer);
  if (value == "row_interleaved" || value == "byrow")
    return operation::Result<StereoMode>::success(StereoMode::row_interleaved);
  if (value == "column_interleaved" || value == "bycolumn")
    return operation::Result<StereoMode>::success(
        StereoMode::column_interleaved);
  if (value == "checkerboard")
    return operation::Result<StereoMode>::success(StereoMode::checkerboard);
  if (value == "openvr")
    return operation::Result<StereoMode>::success(StereoMode::openvr);
  return operation::Result<StereoMode>::failure(invalid(
      "unknown stereo mode: " + std::string{value},
      "Use stereo modes to inspect recognized and renderer-supported modes."));
}

std::string_view to_string(AnaglyphMode mode) noexcept {
  switch (mode) {
  case AnaglyphMode::true_anaglyph: return "true";
  case AnaglyphMode::gray: return "gray";
  case AnaglyphMode::color: return "color";
  case AnaglyphMode::half_color: return "half_color";
  case AnaglyphMode::optimized: return "optimized";
  }
  return "optimized";
}

operation::Result<AnaglyphMode>
anaglyph_mode_from_string(std::string_view value) {
  if (value == "true" || value == "true_anaglyph" || value == "0")
    return operation::Result<AnaglyphMode>::success(
        AnaglyphMode::true_anaglyph);
  if (value == "gray" || value == "1")
    return operation::Result<AnaglyphMode>::success(AnaglyphMode::gray);
  if (value == "color" || value == "2")
    return operation::Result<AnaglyphMode>::success(AnaglyphMode::color);
  if (value == "half_color" || value == "half-color" || value == "3")
    return operation::Result<AnaglyphMode>::success(AnaglyphMode::half_color);
  if (value == "optimized" || value == "4")
    return operation::Result<AnaglyphMode>::success(AnaglyphMode::optimized);
  return operation::Result<AnaglyphMode>::failure(invalid(
      "unknown anaglyph mode: " + std::string{value},
      "Use true, gray, color, half_color, or optimized."));
}

operation::Result<StereoParameters>
validate_stereo_parameters(StereoParameters parameters) {
  if (!std::isfinite(parameters.shift_percent) ||
      parameters.shift_percent < 0.0 || parameters.shift_percent > 100.0) {
    return operation::Result<StereoParameters>::failure(invalid(
        "stereo shift must be between 0 and 100 percent"));
  }
  if (!std::isfinite(parameters.angle_scale) ||
      parameters.angle_scale < 0.0 || parameters.angle_scale > 20.0) {
    return operation::Result<StereoParameters>::failure(invalid(
        "stereo angle scale must be between 0 and 20"));
  }
  return operation::Result<StereoParameters>::success(parameters);
}

operation::Result<StereoPair>
make_stereo_pair(const Camera &camera, StereoParameters parameters) {
  const auto validated = validate_stereo_parameters(parameters);
  if (!validated.has_value())
    return operation::Result<StereoPair>::failure(validated.error());
  const auto left = eye_camera(camera, -1.0, parameters);
  if (!left.has_value())
    return operation::Result<StereoPair>::failure(left.error());
  const auto right = eye_camera(camera, 1.0, parameters);
  if (!right.has_value())
    return operation::Result<StereoPair>::failure(right.error());

  std::array<StereoEye, 2> order{StereoEye::left, StereoEye::right};
  if (parameters.mode == StereoMode::crosseye)
    order = {StereoEye::right, StereoEye::left};
  if (parameters.swap_eyes)
    std::swap(order[0], order[1]);
  return operation::Result<StereoPair>::success(
      StereoPair{{StereoEye::left, left.value()},
                 {StereoEye::right, right.value()}, order});
}

} // namespace molshredder::scene
