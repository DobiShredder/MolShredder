#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "molshredder/operation/result.hpp"
#include "molshredder/scene/camera.hpp"

namespace molshredder::scene {

enum class StereoMode {
  side_by_side,
  crosseye,
  walleye,
  anaglyph,
  quad_buffer,
  row_interleaved,
  column_interleaved,
  checkerboard,
  openvr,
};

enum class StereoEye { left, right };

enum class AnaglyphMode {
  true_anaglyph,
  gray,
  color,
  half_color,
  optimized,
};

[[nodiscard]] std::string_view to_string(StereoMode mode) noexcept;
[[nodiscard]] operation::Result<StereoMode>
stereo_mode_from_string(std::string_view value);
[[nodiscard]] std::string_view to_string(AnaglyphMode mode) noexcept;
[[nodiscard]] operation::Result<AnaglyphMode>
anaglyph_mode_from_string(std::string_view value);

struct StereoParameters {
  bool enabled{};
  StereoMode mode{StereoMode::side_by_side};
  bool swap_eyes{};
  // Separation between camera positions, expressed as a percentage of the
  // objective distance. This follows PyMOL's public stereo_shift contract.
  double shift_percent{2.0};
  // Scale applied to the natural angular convergence of both eye cameras.
  // This follows PyMOL's public stereo_angle contract.
  double angle_scale{2.1};
  AnaglyphMode anaglyph_mode{AnaglyphMode::optimized};

  friend bool operator==(const StereoParameters &, const StereoParameters &) =
      default;
};

struct StereoView {
  StereoEye eye{StereoEye::left};
  Camera camera;
};

struct StereoPair {
  // Physical eye cameras, independent of presentation order.
  StereoView left;
  StereoView right;
  // Eye shown in the left and right adjacent viewport respectively.
  std::array<StereoEye, 2> presentation_order{StereoEye::left,
                                               StereoEye::right};
};

[[nodiscard]] operation::Result<StereoParameters>
validate_stereo_parameters(StereoParameters parameters);

[[nodiscard]] operation::Result<StereoPair>
make_stereo_pair(const Camera &camera, StereoParameters parameters);

// Odd global display rows/columns/cells use the left eye. Passing swap_eyes
// reverses the hardware phase without changing camera geometry.
[[nodiscard]] operation::Result<StereoEye>
interleaved_eye_at(StereoMode mode, std::uint64_t global_x,
                   std::uint64_t global_y, bool swap_eyes = false);

} // namespace molshredder::scene
