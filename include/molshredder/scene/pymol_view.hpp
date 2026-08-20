#pragma once

#include <array>
#include <string>
#include <string_view>

#include "molshredder/operation/result.hpp"
#include "molshredder/scene/camera.hpp"

namespace molshredder::scene {

// Public 18-value layout returned by PyMOL cmd.get_view() and accepted by
// cmd.set_view(). This is a behavioral compatibility type; no PyMOL code is
// incorporated into this implementation.
struct PyMolView18 {
  std::array<double, 18U> values{};

  friend bool operator==(const PyMolView18&, const PyMolView18&) = default;
};

[[nodiscard]] operation::Result<PyMolView18> parse_pymol_view(
    std::string_view text);
[[nodiscard]] std::string format_pymol_view(const PyMolView18& view);

[[nodiscard]] operation::Result<PyMolView18> to_pymol_view(
    const Camera& camera);
[[nodiscard]] operation::Result<Camera> from_pymol_view(
    const PyMolView18& view, const CameraParameters& current = {});

// PyMOL's view animation uses a symmetric power-2 ease curve. The
// interpolation function expects a linear [0, 1] time fraction and applies
// that easing before interpolating the public camera channels.
[[nodiscard]] double pymol_animation_fraction(double linear_fraction) noexcept;
[[nodiscard]] operation::Result<PyMolView18> interpolate_pymol_view(
    const PyMolView18& first, const PyMolView18& last,
    double linear_fraction, int hand = 1);
[[nodiscard]] operation::Result<Camera> interpolate_pymol_camera(
    const Camera& first, const Camera& last, double linear_fraction,
    int hand = 1);

}  // namespace molshredder::scene
