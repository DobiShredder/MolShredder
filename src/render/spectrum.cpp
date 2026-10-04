#include "molshredder/render/spectrum.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

namespace molshredder::render {
namespace {

#include "spectrum_palettes.inc"

// Palettes whose value interpolation uses an explicit color sequence instead
// of their underscore-separated name (PyMOL 3.1.0 behavior).
struct PaletteColorList {
  std::string_view palette;
  std::string_view colors;
};

constexpr std::array kPaletteColorLists{
    PaletteColorList{"rainbow_cycle",
                     "magenta blue cyan green yellow orange red magenta"},
    PaletteColorList{"rainbow_cycle_rev",
                     "magenta red orange yellow green cyan blue magenta"},
    PaletteColorList{"rainbow", "blue cyan green yellow orange red"},
    PaletteColorList{"rainbow_rev", "red orange yellow green cyan blue"},
    PaletteColorList{"rainbow2", "blue cyan green yellow orange red"},
    PaletteColorList{"rainbow2_rev", "red orange yellow green cyan blue"},
    PaletteColorList{"gcbmry", "green cyan blue magenta red yellow"},
    PaletteColorList{"yrmbcg", "yellow red magenta blue cyan green"},
    PaletteColorList{"cbmr", "cyan blue magenta red"},
    PaletteColorList{"rmbc", "red magenta blue cyan"},
};

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

// Floor modulo matching Python's float `%` for a positive divisor.
double python_mod1(double value) {
  auto result = std::fmod(value, 1.0);
  if (result != 0.0 && result < 0.0)
    result += 1.0;
  return result;
}

constexpr double kOneThird = 1.0 / 3.0;
constexpr double kOneSixth = 1.0 / 6.0;
constexpr double kTwoThird = 2.0 / 3.0;

using Triple = std::array<double, 3>;

double hue_from(double maxc, double rangec, Triple rgb) {
  const auto rc = (maxc - rgb[0]) / rangec;
  const auto gc = (maxc - rgb[1]) / rangec;
  const auto bc = (maxc - rgb[2]) / rangec;
  double hue{};
  if (rgb[0] == maxc)
    hue = bc - gc;
  else if (rgb[1] == maxc)
    hue = 2.0 + rc - bc;
  else
    hue = 4.0 + gc - rc;
  return python_mod1(hue / 6.0);
}

Triple rgb_to_hls(Triple rgb) {
  const auto maxc = std::max({rgb[0], rgb[1], rgb[2]});
  const auto minc = std::min({rgb[0], rgb[1], rgb[2]});
  const auto sumc = maxc + minc;
  const auto rangec = maxc - minc;
  const auto lightness = sumc / 2.0;
  if (minc == maxc)
    return {0.0, lightness, 0.0};
  const auto saturation = lightness <= 0.5 ? rangec / sumc
                                           : rangec / (2.0 - maxc - minc);
  return {hue_from(maxc, rangec, rgb), lightness, saturation};
}

double hls_channel(double m1, double m2, double hue) {
  hue = python_mod1(hue);
  if (hue < kOneSixth)
    return m1 + (m2 - m1) * hue * 6.0;
  if (hue < 0.5)
    return m2;
  if (hue < kTwoThird)
    return m1 + (m2 - m1) * (kTwoThird - hue) * 6.0;
  return m1;
}

Triple hls_to_rgb(Triple hls) {
  const auto [hue, lightness, saturation] = hls;
  if (saturation == 0.0)
    return {lightness, lightness, lightness};
  const auto m2 = lightness <= 0.5 ? lightness * (1.0 + saturation)
                                   : lightness + saturation - (lightness * saturation);
  const auto m1 = 2.0 * lightness - m2;
  return {hls_channel(m1, m2, hue + kOneThird), hls_channel(m1, m2, hue),
          hls_channel(m1, m2, hue - kOneThird)};
}

Triple rgb_to_hsv(Triple rgb) {
  const auto maxc = std::max({rgb[0], rgb[1], rgb[2]});
  const auto minc = std::min({rgb[0], rgb[1], rgb[2]});
  const auto rangec = maxc - minc;
  if (minc == maxc)
    return {0.0, 0.0, maxc};
  return {hue_from(maxc, rangec, rgb), rangec / maxc, maxc};
}

Triple hsv_to_rgb(Triple hsv) {
  const auto [hue, saturation, value] = hsv;
  if (saturation == 0.0)
    return {value, value, value};
  auto sector = static_cast<long long>(hue * 6.0);
  const auto fraction = (hue * 6.0) - static_cast<double>(sector);
  const auto p = value * (1.0 - saturation);
  const auto q = value * (1.0 - saturation * fraction);
  const auto t = value * (1.0 - saturation * (1.0 - fraction));
  sector %= 6;
  if (sector < 0)
    sector += 6;
  switch (sector) {
  case 0:
    return {value, t, p};
  case 1:
    return {q, value, p};
  case 2:
    return {p, value, t};
  case 3:
    return {p, q, value};
  case 4:
    return {t, p, value};
  default:
    return {value, p, q};
  }
}

Triple from_rgb(Triple rgb, SpectrumInterpolation interpolation) {
  switch (interpolation) {
  case SpectrumInterpolation::hls:
    return rgb_to_hls(rgb);
  case SpectrumInterpolation::hsv:
    return rgb_to_hsv(rgb);
  case SpectrumInterpolation::rgb:
    break;
  }
  return rgb;
}

Triple to_rgb(Triple value, SpectrumInterpolation interpolation) {
  switch (interpolation) {
  case SpectrumInterpolation::hls:
    return hls_to_rgb(value);
  case SpectrumInterpolation::hsv:
    return hsv_to_rgb(value);
  case SpectrumInterpolation::rgb:
    break;
  }
  return value;
}

std::int32_t byte_of(double component) {
  const auto scaled = static_cast<long long>(255.0 * component);
  return static_cast<std::int32_t>(std::clamp(scaled, 0LL, 255LL));
}

} // namespace

std::span<const SpectrumPalette> spectrum_palettes() noexcept {
  return kSpectrumPalettes;
}

const SpectrumPalette *find_spectrum_palette(std::string_view name) noexcept {
  if (name.empty())
    return nullptr;
  const SpectrumPalette *prefix_match = nullptr;
  std::size_t prefix_count{};
  for (const auto &palette : kSpectrumPalettes) {
    if (palette.name == name)
      return &palette;
    if (palette.name.starts_with(name)) {
      prefix_match = &palette;
      ++prefix_count;
    }
  }
  return prefix_count == 1U ? prefix_match : nullptr;
}

std::string_view
spectrum_palette_color_list(std::string_view palette) noexcept {
  for (const auto &entry : kPaletteColorLists)
    if (entry.palette == palette)
      return entry.colors;
  return {};
}

operation::Result<SpectrumInterpolation>
parse_spectrum_interpolation(std::string_view name) {
  if (name == "rgb")
    return operation::Result<SpectrumInterpolation>::success(
        SpectrumInterpolation::rgb);
  if (name == "hls")
    return operation::Result<SpectrumInterpolation>::success(
        SpectrumInterpolation::hls);
  if (name == "hsv")
    return operation::Result<SpectrumInterpolation>::success(
        SpectrumInterpolation::hsv);
  return operation::Result<SpectrumInterpolation>::failure(
      invalid("interpolation must be one of hls, hsv, rgb"));
}

operation::Result<PaletteSpectrumResult>
palette_spectrum(std::span<const float> values, std::optional<double> minimum,
                 std::optional<double> maximum, const SpectrumPalette &palette,
                 const ColorTable &table) {
  using R = operation::Result<PaletteSpectrumResult>;
  const auto step = palette.last >= palette.first ? 1 : -1;
  const auto span = std::abs(palette.last - palette.first);
  if (span < 1)
    return R::failure(invalid("spectrum palette needs at least two colors"));
  std::vector<std::int32_t> ramp;
  ramp.reserve(static_cast<std::size_t>(span));
  for (int position = 0; position < span; ++position) {
    std::array<char, 16> name{};
    std::snprintf(name.data(), name.size(), "%c%03d", palette.prefix,
                  palette.first + step * position);
    const auto index = table.resolve(name.data());
    if (!index.has_value())
      return R::failure(index.error());
    ramp.push_back(index.value());
  }
  PaletteSpectrumResult result;
  if (values.empty())
    return R::success(std::move(result));
  auto low = minimum.has_value() ? static_cast<float>(*minimum) : 0.0F;
  auto high = maximum.has_value() ? static_cast<float>(*maximum) : -1.0F;
  if (!minimum.has_value() || !maximum.has_value() || high < low) {
    low = high = values.front();
    for (const auto value : values) {
      low = std::min(low, value);
      high = std::max(high, value);
    }
  }
  auto range = high - low;
  if (range == 0.0F)
    range = 1.0F;
  result.range = {low, high};
  result.colors.reserve(values.size());
  const auto count = static_cast<float>(span);
  for (const auto value : values) {
    const auto fraction = (value - low) / range;
    const auto position = static_cast<long long>(
        std::floor(static_cast<double>(fraction * count) + 0.5));
    result.colors.push_back(
        ramp[static_cast<std::size_t>(std::clamp(position, 0LL,
                                                 static_cast<long long>(span) - 1))]);
  }
  return R::success(std::move(result));
}

operation::Result<ColorListSpectrumResult>
color_list_spectrum(std::span<const std::optional<double>> values,
                    std::optional<double> minimum,
                    std::optional<double> maximum,
                    std::span<const std::int32_t> color_indices,
                    SpectrumInterpolation interpolation,
                    const ColorTable &table) {
  using R = operation::Result<ColorListSpectrumResult>;
  if (color_indices.size() < 2U)
    return R::failure(invalid("please provide at least 2 colors"));
  std::vector<Triple> stops;
  stops.reserve(color_indices.size());
  for (const auto index : color_indices) {
    const auto rgb = table.rgb(index);
    if (!rgb.has_value())
      return R::failure(invalid("unknown color"));
    stops.push_back(from_rgb({static_cast<double>(rgb->red),
                              static_cast<double>(rgb->green),
                              static_cast<double>(rgb->blue)},
                             interpolation));
  }
  ColorListSpectrumResult result;
  result.colors.assign(values.size(), std::nullopt);
  std::optional<double> low_seen;
  std::optional<double> high_seen;
  for (const auto &value : values) {
    if (!value.has_value())
      continue;
    low_seen = low_seen.has_value() ? std::min(*low_seen, *value) : *value;
    high_seen = high_seen.has_value() ? std::max(*high_seen, *value) : *value;
  }
  if (!low_seen.has_value())
    return R::success(std::move(result));
  const auto low = minimum.value_or(*low_seen);
  const auto high = maximum.value_or(*high_seen);
  result.range = {low, high};
  const auto range = high - low;
  if (range == 0.0) {
    result.colors.assign(values.size(), color_indices.front());
    return R::success(std::move(result));
  }
  const auto segments = static_cast<double>(stops.size() - 1U);
  for (std::size_t atom = 0; atom < values.size(); ++atom) {
    if (!values[atom].has_value())
      continue;
    const auto scaled =
        std::min(1.0, std::max(0.0, (*values[atom] - low) / range)) * segments;
    const auto lower = std::min(static_cast<std::size_t>(scaled),
                                stops.size() - 2U);
    const auto weight = scaled - static_cast<double>(lower);
    Triple mixed{};
    for (std::size_t channel = 0; channel < 3U; ++channel)
      mixed[channel] = stops[lower + 1U][channel] * weight +
                       stops[lower][channel] * (1.0 - weight);
    const auto rgb = to_rgb(mixed, interpolation);
    result.colors[atom] = kColorHexFlag + byte_of(rgb[0]) * 0x10000 +
                          byte_of(rgb[1]) * 0x100 + byte_of(rgb[2]);
  }
  return R::success(std::move(result));
}

} // namespace molshredder::render
