#include "molshredder/scene/pymol_view.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "molshredder/operation/error.hpp"
#include "molshredder/scene/math.hpp"

namespace molshredder::scene {
namespace {

constexpr double kRotationTolerance = 1.0e-4;
constexpr double kPyMolMinimumDepth = 1.0e-4;

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

bool separator(char value) {
  switch (value) {
    case ' ':
    case '\t':
    case '\r':
    case '\n':
    case ',':
    case '(':
    case ')':
    case '[':
    case ']':
    case '\\':
      return true;
    default:
      return false;
  }
}

operation::Result<double> parse_number(std::string_view token) {
  double value{};
  const auto* first = token.data();
  const auto* last = first + token.size();
  const auto parsed = std::from_chars(first, last, value);
  if (parsed.ec != std::errc{} || parsed.ptr != last ||
      !std::isfinite(value)) {
    return operation::Result<double>::failure(
        invalid("PyMOL view contains an invalid or non-finite number"));
  }
  return operation::Result<double>::success(value);
}

bool approximately(double left, double right,
                   double tolerance = kRotationTolerance) {
  return std::abs(left - right) <= tolerance;
}

operation::Result<std::array<model::Vec3d, 3U>> camera_axes(
    const PyMolView18& view) {
  const auto& value = view.values;
  const model::Vec3d right{value[0], value[3], value[6]};
  const model::Vec3d up{value[1], value[4], value[7]};
  const model::Vec3d backward{value[2], value[5], value[8]};
  if (!is_finite(right) || !is_finite(up) || !is_finite(backward) ||
      !approximately(length(right), 1.0) ||
      !approximately(length(up), 1.0) ||
      !approximately(length(backward), 1.0) ||
      !approximately(dot(right, up), 0.0) ||
      !approximately(dot(right, backward), 0.0) ||
      !approximately(dot(up, backward), 0.0) ||
      !approximately(dot(cross(right, up), backward), 1.0)) {
    return operation::Result<std::array<model::Vec3d, 3U>>::failure(invalid(
        "PyMOL view rotation must be a finite right-handed orthonormal matrix",
        "Use the 18 values returned by PyMOL cmd.get_view()."));
  }

  // PyMOL prints single-precision rotations. Re-orthogonalize within the
  // accepted tolerance before encoding the rotation as a unit quaternion.
  const auto normalized_right = normalized(right);
  const auto normalized_up =
      normalized(up - normalized_right * dot(normalized_right, up));
  const auto normalized_backward = cross(normalized_right, normalized_up);
  return operation::Result<std::array<model::Vec3d, 3U>>::success(
      {normalized_right, normalized_up, normalized_backward});
}

Quaterniond quaternion_from_axes(model::Vec3d right, model::Vec3d up,
                                 model::Vec3d backward) {
  const double m00 = right.x;
  const double m01 = up.x;
  const double m02 = backward.x;
  const double m10 = right.y;
  const double m11 = up.y;
  const double m12 = backward.y;
  const double m20 = right.z;
  const double m21 = up.z;
  const double m22 = backward.z;

  Quaterniond result;
  const auto trace = m00 + m11 + m22;
  if (trace > 0.0) {
    const auto scale = std::sqrt(trace + 1.0) * 2.0;
    result = {0.25 * scale, (m21 - m12) / scale,
              (m02 - m20) / scale, (m10 - m01) / scale};
  } else if (m00 > m11 && m00 > m22) {
    const auto scale = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
    result = {(m21 - m12) / scale, 0.25 * scale,
              (m01 + m10) / scale, (m02 + m20) / scale};
  } else if (m11 > m22) {
    const auto scale = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
    result = {(m02 - m20) / scale, (m01 + m10) / scale,
              0.25 * scale, (m12 + m21) / scale};
  } else {
    const auto scale = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
    result = {(m10 - m01) / scale, (m02 + m20) / scale,
              (m12 + m21) / scale, 0.25 * scale};
  }
  result = normalized(result);
  if (result.w < 0.0)
    result = {-result.w, -result.x, -result.y, -result.z};
  return result;
}

double radians_to_degrees(double radians) {
  return radians * 180.0 / std::numbers::pi;
}

double degrees_to_radians(double degrees) {
  return degrees * std::numbers::pi / 180.0;
}

PyMolView18 view_with_rotation(PyMolView18 view, Quaterniond model_to_camera) {
  const auto camera_to_world = conjugate(normalized(model_to_camera));
  const auto right = rotate(camera_to_world, model::Vec3d{1.0, 0.0, 0.0});
  const auto up = rotate(camera_to_world, model::Vec3d{0.0, 1.0, 0.0});
  const auto backward =
      rotate(camera_to_world, model::Vec3d{0.0, 0.0, 1.0});
  view.values[0] = right.x;
  view.values[1] = up.x;
  view.values[2] = backward.x;
  view.values[3] = right.y;
  view.values[4] = up.y;
  view.values[5] = backward.y;
  view.values[6] = right.z;
  view.values[7] = up.z;
  view.values[8] = backward.z;
  return view;
}

}  // namespace

operation::Result<PyMolView18> parse_pymol_view(std::string_view text) {
  constexpr std::string_view prefix = "set_view";
  while (!text.empty() && separator(text.front()))
    text.remove_prefix(1U);
  if (text.starts_with(prefix)) {
    text.remove_prefix(prefix.size());
  }

  std::vector<double> values;
  std::size_t begin{};
  while (begin < text.size()) {
    while (begin < text.size() && separator(text[begin]))
      ++begin;
    if (begin == text.size())
      break;
    auto end = begin;
    while (end < text.size() && !separator(text[end]))
      ++end;
    const auto parsed = parse_number(text.substr(begin, end - begin));
    if (!parsed.has_value())
      return operation::Result<PyMolView18>::failure(parsed.error());
    values.push_back(parsed.value());
    begin = end;
  }
  if (values.size() != 18U) {
    return operation::Result<PyMolView18>::failure(invalid(
        "PyMOL view must contain exactly 18 numeric values",
        "Paste the output of PyMOL cmd.get_view() or get_view output=3."));
  }
  PyMolView18 result;
  std::copy(values.begin(), values.end(), result.values.begin());
  return operation::Result<PyMolView18>::success(result);
}

std::string format_pymol_view(const PyMolView18& view) {
  std::ostringstream output;
  output << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (std::size_t index = 0; index < view.values.size(); ++index) {
    if (index != 0U)
      output << ", ";
    output << (view.values[index] == 0.0 ? 0.0 : view.values[index]);
  }
  return output.str();
}

operation::Result<PyMolView18> to_pymol_view(const Camera& camera) {
  const auto& parameters = camera.parameters();
  const auto right = camera.right();
  const auto up = camera.up();
  const auto backward = camera.forward() * -1.0;
  const auto origin_to_eye = parameters.model_origin - camera.position();
  const model::Vec3d position{dot(right, origin_to_eye),
                             dot(up, origin_to_eye),
                             dot(backward, origin_to_eye)};

  auto field_of_view = parameters.vertical_field_of_view_radians;
  if (parameters.projection == ProjectionMode::orthographic) {
    const auto depth = std::max(kPyMolMinimumDepth, -position.z);
    field_of_view = 2.0 * std::atan(parameters.orthographic_height /
                                    (2.0 * depth));
  }
  if (!(field_of_view > 0.0) || field_of_view >= std::numbers::pi ||
      !std::isfinite(field_of_view)) {
    return operation::Result<PyMolView18>::failure(
        invalid("camera cannot be represented as a finite PyMOL view"));
  }
  const auto signed_degrees =
      radians_to_degrees(field_of_view) *
      (parameters.projection == ProjectionMode::orthographic ? 1.0 : -1.0);

  return operation::Result<PyMolView18>::success(PyMolView18{{
      right.x,
      up.x,
      backward.x,
      right.y,
      up.y,
      backward.y,
      right.z,
      up.z,
      backward.z,
      position.x,
      position.y,
      position.z,
      parameters.model_origin.x,
      parameters.model_origin.y,
      parameters.model_origin.z,
      parameters.near_clip,
      parameters.far_clip,
      signed_degrees,
  }});
}

operation::Result<Camera> from_pymol_view(
    const PyMolView18& view, const CameraParameters& current) {
  const auto axes = camera_axes(view);
  if (!axes.has_value())
    return operation::Result<Camera>::failure(axes.error());
  if (!std::all_of(view.values.begin(), view.values.end(),
                   [](double value) { return std::isfinite(value); })) {
    return operation::Result<Camera>::failure(
        invalid("PyMOL view values must all be finite"));
  }

  const auto& right = axes.value()[0];
  const auto& up = axes.value()[1];
  const auto& backward = axes.value()[2];
  const model::Vec3d position{view.values[9], view.values[10],
                             view.values[11]};
  const model::Vec3d origin{view.values[12], view.values[13],
                           view.values[14]};
  const auto eye = origin - right * position.x - up * position.y -
                   backward * position.z;

  auto parameters = current;
  parameters.orientation = quaternion_from_axes(right, up, backward);
  parameters.model_origin = origin;
  if (-position.z > 1.0e-9)
    parameters.distance = -position.z;
  parameters.target = eye - backward * parameters.distance;
  parameters.near_clip = view.values[15];
  parameters.far_clip = view.values[16];

  const auto projection_flag = view.values[17];
  const auto absolute_fov = std::abs(projection_flag);
  parameters.projection = projection_flag > 0.5
                              ? ProjectionMode::orthographic
                              : ProjectionMode::perspective;
  if (absolute_fov > 1.0)
    parameters.vertical_field_of_view_radians =
        degrees_to_radians(absolute_fov);
  if (parameters.projection == ProjectionMode::orthographic) {
    const auto depth = std::max(kPyMolMinimumDepth, -position.z);
    parameters.orthographic_height =
        2.0 * depth *
        std::tan(parameters.vertical_field_of_view_radians * 0.5);
  }
  return Camera::create(parameters);
}

double pymol_animation_fraction(double linear_fraction) noexcept {
  const auto fraction = std::clamp(linear_fraction, 0.0, 1.0);
  if (fraction < 0.5)
    return 2.0 * fraction * fraction;
  const auto remaining = 1.0 - fraction;
  return 1.0 - 2.0 * remaining * remaining;
}

operation::Result<PyMolView18> interpolate_pymol_view(
    const PyMolView18& first, const PyMolView18& last,
    double linear_fraction, int hand) {
  if (!std::isfinite(linear_fraction) || linear_fraction < 0.0 ||
      linear_fraction > 1.0) {
    return operation::Result<PyMolView18>::failure(
        invalid("camera animation fraction must be between zero and one"));
  }
  if (hand < -1 || hand > 1) {
    return operation::Result<PyMolView18>::failure(
        invalid("camera animation hand must be -1, 0, or 1"));
  }
  const auto first_axes = camera_axes(first);
  const auto last_axes = camera_axes(last);
  if (!first_axes.has_value())
    return operation::Result<PyMolView18>::failure(first_axes.error());
  if (!last_axes.has_value())
    return operation::Result<PyMolView18>::failure(last_axes.error());

  const auto first_camera_to_world = quaternion_from_axes(
      first_axes.value()[0], first_axes.value()[1], first_axes.value()[2]);
  const auto last_camera_to_world = quaternion_from_axes(
      last_axes.value()[0], last_axes.value()[1], last_axes.value()[2]);
  const auto first_model_to_camera = conjugate(first_camera_to_world);
  const auto last_model_to_camera = conjugate(last_camera_to_world);
  auto relative = normalized(conjugate(first_model_to_camera) *
                             last_model_to_camera);
  if (relative.w < 0.0)
    relative = {-relative.w, -relative.x, -relative.y, -relative.z};

  auto angle = 2.0 * std::acos(std::clamp(relative.w, -1.0, 1.0));
  const auto sine = std::sin(angle * 0.5);
  model::Vec3d axis = sine > 1.0e-12
                          ? model::Vec3d{relative.x / sine, relative.y / sine,
                                         relative.z / sine}
                          : model::Vec3d{1.0, 0.0, 0.0};
  const auto effective_hand = hand == 0 ? 1 : hand;
  if (std::abs(std::numbers::pi - std::abs(angle)) < 0.01 &&
      (axis.x * 0.7 + axis.y * 0.8 + axis.z * 0.9) *
              static_cast<double>(effective_hand) * angle >
          0.0) {
    axis = axis * -1.0;
    angle = 2.0 * std::numbers::pi - angle;
  }

  const auto fraction = pymol_animation_fraction(linear_fraction);
  const auto partial = quaternion_from_axis_angle(axis, angle * fraction);
  auto result = view_with_rotation(
      first, normalized(first_model_to_camera * partial));
  for (std::size_t index = 9U; index <= 16U; ++index) {
    result.values[index] = first.values[index] * (1.0 - fraction) +
                           last.values[index] * fraction;
  }

  const auto approximate_fov = first.values[17] * (1.0 - fraction) +
                               last.values[17] * fraction;
  const auto first_span =
      first.values[11] *
      std::tan(std::numbers::pi * std::abs(first.values[17]) / 360.0);
  const auto last_span =
      last.values[11] *
      std::tan(std::numbers::pi * std::abs(last.values[17]) / 360.0);
  const auto current_span =
      first_span * (1.0 - fraction) + last_span * fraction;
  if (std::abs(result.values[11]) > 1.0e-12) {
    result.values[17] =
        360.0 * std::atan(current_span / result.values[11]) /
        std::numbers::pi;
    if (result.values[17] * approximate_fov < 0.0)
      result.values[17] = -result.values[17];
  } else {
    result.values[17] = approximate_fov;
  }
  return operation::Result<PyMolView18>::success(result);
}

operation::Result<Camera> interpolate_pymol_camera(
    const Camera& first, const Camera& last, double linear_fraction,
    int hand) {
  const auto first_view = to_pymol_view(first);
  const auto last_view = to_pymol_view(last);
  if (!first_view.has_value())
    return operation::Result<Camera>::failure(first_view.error());
  if (!last_view.has_value())
    return operation::Result<Camera>::failure(last_view.error());
  const auto interpolated = interpolate_pymol_view(
      first_view.value(), last_view.value(), linear_fraction, hand);
  if (!interpolated.has_value())
    return operation::Result<Camera>::failure(interpolated.error());
  return from_pymol_view(interpolated.value(), first.parameters());
}

}  // namespace molshredder::scene
