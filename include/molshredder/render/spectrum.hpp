#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/operation/result.hpp"
#include "molshredder/render/color_table.hpp"

namespace molshredder::render {

// Built-in spectrum palette: a run of generated ramp colors named
// prefix + three digits from `first` toward `last` (PyMOL 3.1.0 catalog).
struct SpectrumPalette {
  std::string_view name;
  char prefix{};
  int first{};
  int last{};
};

enum class SpectrumInterpolation { rgb, hls, hsv };

[[nodiscard]] std::span<const SpectrumPalette> spectrum_palettes() noexcept;
// Exact name or unique prefix; nullptr when absent or ambiguous.
[[nodiscard]] const SpectrumPalette *
find_spectrum_palette(std::string_view name) noexcept;
// Space-separated color list used when a palette is interpolated by value
// (rainbow, gcbmry, ...); empty when the palette name is its own list.
[[nodiscard]] std::string_view
spectrum_palette_color_list(std::string_view palette) noexcept;
[[nodiscard]] operation::Result<SpectrumInterpolation>
parse_spectrum_interpolation(std::string_view name);

struct SpectrumRange {
  double minimum{};
  double maximum{};
};

// Named-ramp path. `values` are per selected atom in selection order and
// compared in float32 like the reference. A range is used only when both
// bounds are given and maximum >= minimum; otherwise it is computed from the
// values. Each value maps to ramp index
// clamp(floor((v - min) / range * n + 0.5), 0, n - 1) with n = |last-first|
// (a zero range counts as 1).
struct PaletteSpectrumResult {
  SpectrumRange range;
  std::vector<std::int32_t> colors;
};
[[nodiscard]] operation::Result<PaletteSpectrumResult>
palette_spectrum(std::span<const float> values,
                 std::optional<double> minimum, std::optional<double> maximum,
                 const SpectrumPalette &palette, const ColorTable &table);

// Color-list path: interpolates between explicit colors in RGB, HLS or HSV
// and encodes literal 0xRRGGBB colors (component = floor(255 * c)).
// Missing values keep their atom color (nullopt result). A zero range paints
// every value with the first color's table index.
struct ColorListSpectrumResult {
  SpectrumRange range;
  std::vector<std::optional<std::int32_t>> colors;
};
[[nodiscard]] operation::Result<ColorListSpectrumResult>
color_list_spectrum(std::span<const std::optional<double>> values,
                    std::optional<double> minimum,
                    std::optional<double> maximum,
                    std::span<const std::int32_t> color_indices,
                    SpectrumInterpolation interpolation,
                    const ColorTable &table);

} // namespace molshredder::render
