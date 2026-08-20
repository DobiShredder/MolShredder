#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

#include "molshredder/scene/stereo.hpp"

namespace {
bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}
bool near(double left, double right, double tolerance = 1.0e-10) {
  return std::abs(left - right) <= tolerance;
}
} // namespace

int main() {
  using namespace molshredder::scene;
  bool passed = true;
  const auto camera = Camera::create();
  const auto pair = make_stereo_pair(camera.value(), {});
  passed &= expect(pair.has_value(), "default stereo pair must be valid");
  if (!pair.has_value()) return 1;

  const auto expected_offset = camera.value().parameters().distance * 0.02;
  const auto left_position = pair.value().left.camera.position();
  const auto right_position = pair.value().right.camera.position();
  passed &= expect(near(left_position.x, -expected_offset) &&
                       near(right_position.x, expected_offset) &&
                       near(left_position.y, right_position.y) &&
                       near(left_position.z, right_position.z),
                   "stereo_shift must separate physical eyes by objective-distance percent");
  passed &= expect(pair.value().left.camera.forward().x > 0.0 &&
                       pair.value().right.camera.forward().x < 0.0,
                   "stereo_angle must converge both physical eye cameras");

  StereoParameters crossed;
  crossed.mode = StereoMode::crosseye;
  const auto cross_pair = make_stereo_pair(camera.value(), crossed);
  crossed.swap_eyes = true;
  const auto swapped_cross_pair = make_stereo_pair(camera.value(), crossed);
  passed &= expect(cross_pair.value().presentation_order[0] == StereoEye::right &&
                       cross_pair.value().presentation_order[1] == StereoEye::left &&
                       swapped_cross_pair.value().presentation_order[0] == StereoEye::left,
                   "crosseye and explicit swap must deterministically order adjacent views");

  const auto alias = stereo_mode_from_string("sidebyside");
  passed &= expect(alias.has_value() && alias.value() == StereoMode::side_by_side &&
                       to_string(alias.value()) == "side_by_side" &&
                       !stereo_mode_from_string("unknown").has_value(),
                   "stereo mode parsing must canonicalize public aliases");
  const auto optimized = anaglyph_mode_from_string("4");
  passed &= expect(
      optimized.has_value() &&
          optimized.value() == AnaglyphMode::optimized &&
          to_string(AnaglyphMode::half_color) == "half_color" &&
          !anaglyph_mode_from_string("amber_blue").has_value(),
      "anaglyph mode parsing must cover PyMOL's public modes 0 through 4");

  StereoParameters invalid;
  invalid.shift_percent = std::numeric_limits<double>::infinity();
  passed &= expect(!validate_stereo_parameters(invalid).has_value(),
                   "non-finite stereo parameters must be rejected");
  invalid.shift_percent = 2.0;
  invalid.angle_scale = -1.0;
  passed &= expect(!make_stereo_pair(camera.value(), invalid).has_value(),
                   "out-of-range stereo angle scale must be rejected");
  return passed ? 0 : 1;
}
