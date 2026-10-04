#include "molshredder/render/color_table.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>

namespace molshredder::render {
namespace {

struct BuiltinColor {
  std::string_view name;
  float red;
  float green;
  float blue;
  bool named;
};

#include "color_catalog.inc"

constexpr std::size_t kMaximumCustomColors = 1U << 20U;

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

char lower(char value) noexcept {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a')
                                      : value;
}

bool iequals(std::string_view left, std::string_view right) noexcept {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](char a, char b) { return lower(a) == lower(b); });
}

bool istarts_with(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() &&
         iequals(text.substr(0U, prefix.size()), prefix);
}

std::string lowercase(std::string_view text) {
  std::string result{text};
  std::transform(result.begin(), result.end(), result.begin(), lower);
  return result;
}

// Built-in names are lowercase; this index sorts table positions by name so
// exact and prefix lookups stay logarithmic on representation hot paths.
const std::array<std::uint16_t, kBuiltinColors.size()> &sorted_builtin() {
  static const auto sorted = [] {
    std::array<std::uint16_t, kBuiltinColors.size()> order{};
    for (std::size_t index = 0; index < order.size(); ++index)
      order[index] = static_cast<std::uint16_t>(index);
    std::sort(order.begin(), order.end(), [](auto left, auto right) {
      return kBuiltinColors[left].name < kBuiltinColors[right].name;
    });
    return order;
  }();
  return sorted;
}

std::optional<std::int32_t> builtin_exact(std::string_view lowered) {
  const auto &order = sorted_builtin();
  const auto found = std::lower_bound(
      order.begin(), order.end(), lowered, [](auto index, std::string_view key) {
        return kBuiltinColors[index].name < key;
      });
  if (found != order.end() && kBuiltinColors[*found].name == lowered)
    return static_cast<std::int32_t>(*found);
  return std::nullopt;
}

std::optional<std::int32_t> builtin_prefix(std::string_view lowered) {
  const auto &order = sorted_builtin();
  auto found = std::lower_bound(
      order.begin(), order.end(), lowered, [](auto index, std::string_view key) {
        return kBuiltinColors[index].name < key;
      });
  std::optional<std::int32_t> best;
  for (; found != order.end() &&
         kBuiltinColors[*found].name.starts_with(lowered);
       ++found) {
    if (!best.has_value() || *found < *best)
      best = static_cast<std::int32_t>(*found);
  }
  return best;
}

struct SpecialToken {
  std::string_view name;
  std::int32_t code;
};

constexpr std::array kSpecialTokens{
    SpecialToken{"default", kColorDefault}, SpecialToken{"atomic", kColorAtomic},
    SpecialToken{"atom", kColorAtomic},     SpecialToken{"object", kColorObject},
    SpecialToken{"front", kColorFront},     SpecialToken{"back", kColorBack}};

std::optional<std::int32_t> special_code(std::string_view token) noexcept {
  for (const auto &special : kSpecialTokens)
    if (iequals(special.name, token))
      return special.code;
  return std::nullopt;
}

std::optional<std::uint32_t> parse_hex24(std::string_view digits) noexcept {
  if (digits.size() != 6U)
    return std::nullopt;
  std::uint32_t value{};
  const auto parsed =
      std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
  if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size())
    return std::nullopt;
  return value;
}

bool valid_name(std::string_view name) noexcept {
  if (name.empty() || name.size() > 64U)
    return false;
  const auto first = name.front();
  if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') ||
        first == '_'))
    return false;
  return std::all_of(name.begin(), name.end(), [](char item) {
    return (item >= 'A' && item <= 'Z') || (item >= 'a' && item <= 'z') ||
           (item >= '0' && item <= '9') || item == '_' || item == '.' ||
           item == '+' || item == '-';
  });
}

bool is_hex_code(std::int32_t index) noexcept {
  return index >= kColorHexFlag && index <= (kColorHexFlag | 0xFFFFFF);
}

} // namespace

bool is_special_color_token(std::string_view token) noexcept {
  return special_code(token).has_value() || token == "-1";
}

std::size_t ColorTable::builtin_size() noexcept {
  return kBuiltinColors.size();
}

std::size_t ColorTable::size() const noexcept {
  return kBuiltinColors.size() + custom_.size();
}

operation::Result<std::int32_t>
ColorTable::resolve(std::string_view token) const {
  using R = operation::Result<std::int32_t>;
  if (token.empty())
    return R::failure(invalid("color must not be empty"));
  const auto lowered = lowercase(token);
  if (const auto found = builtin_exact(lowered); found.has_value())
    return R::success(*found);
  for (std::size_t index = 0; index < custom_.size(); ++index)
    if (iequals(custom_[index].first, token))
      return R::success(
          static_cast<std::int32_t>(kBuiltinColors.size() + index));
  const auto numeric_start = token.front() == '-' ? 1U : 0U;
  if (token.size() > numeric_start &&
      std::all_of(token.begin() + numeric_start, token.end(),
                  [](char item) { return item >= '0' && item <= '9'; })) {
    std::int64_t value{};
    const auto parsed =
        std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size()) {
      if (value == kColorDefault || value == kColorAtomic ||
          value == kColorObject || value == kColorFront ||
          value == kColorBack ||
          (value >= 0 && static_cast<std::uint64_t>(value) < size()) ||
          (value <= std::numeric_limits<std::int32_t>::max() &&
           is_hex_code(static_cast<std::int32_t>(value))))
        return R::success(static_cast<std::int32_t>(value));
    }
    return R::failure(invalid("unknown color index: " + std::string{token},
                              "use color list --all for valid indices"));
  }
  if (lowered.starts_with("0x"))
    if (const auto rgb = parse_hex24(std::string_view{lowered}.substr(2U)))
      return R::success(kColorHexFlag | static_cast<std::int32_t>(*rgb));
  if (lowered.starts_with("#"))
    if (const auto rgb = parse_hex24(std::string_view{lowered}.substr(1U)))
      return R::success(kColorHexFlag | static_cast<std::int32_t>(*rgb));
  if (const auto code = special_code(token); code.has_value())
    return R::success(*code);
  std::optional<std::int32_t> best = builtin_prefix(lowered);
  if (!best.has_value())
    for (std::size_t index = 0; index < custom_.size(); ++index)
      if (istarts_with(custom_[index].first, token)) {
        best = static_cast<std::int32_t>(kBuiltinColors.size() + index);
        break;
      }
  if (best.has_value())
    return R::success(*best);
  return R::failure(
      invalid("unknown color: " + std::string{token},
              "use a color name, index, 0xRRGGBB or #RRGGBB; see color list"));
}

operation::Result<std::int32_t>
ColorTable::resolve_assignment(std::string_view token) const {
  using R = operation::Result<std::int32_t>;
  auto resolved = resolve(token);
  if (!resolved.has_value())
    return resolved;
  const auto index = resolved.value();
  // Exact names, numeric/hex literals and special tokens are final.
  if (index < 0 || index >= static_cast<std::int32_t>(size()))
    return resolved;
  const auto exact = name(index);
  if (exact.has_value() && iequals(*exact, token))
    return resolved;
  if (std::all_of(token.begin(), token.end(),
                  [](char item) { return item >= '0' && item <= '9'; }))
    return resolved;
  std::vector<std::int32_t> named_matches;
  for (std::size_t position = 0; position < kBuiltinColors.size(); ++position)
    if (kBuiltinColors[position].named &&
        istarts_with(kBuiltinColors[position].name, token))
      named_matches.push_back(static_cast<std::int32_t>(position));
  for (std::size_t position = 0; position < custom_.size(); ++position)
    if (istarts_with(custom_[position].first, token))
      named_matches.push_back(
          static_cast<std::int32_t>(kBuiltinColors.size() + position));
  if (named_matches.size() == 1U)
    return R::success(named_matches.front());
  if (named_matches.size() > 1U) {
    std::string candidates;
    for (const auto match : named_matches)
      candidates += (candidates.empty() ? "" : " ") + name(match).value_or("");
    return R::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "ambiguous color: " + std::string{token},
        "use one of: " + candidates, {{"candidates", candidates}}});
  }
  return resolved;
}

std::optional<ColorRgb> ColorTable::rgb(std::int32_t index) const noexcept {
  if (is_hex_code(index)) {
    const auto value = static_cast<std::uint32_t>(index) & 0xFFFFFFU;
    return ColorRgb{static_cast<float>(((value >> 16U) & 0xFFU) / 255.0),
                    static_cast<float>(((value >> 8U) & 0xFFU) / 255.0),
                    static_cast<float>((value & 0xFFU) / 255.0)};
  }
  if (index < 0)
    return std::nullopt;
  const auto position = static_cast<std::size_t>(index);
  if (position < kBuiltinColors.size()) {
    const auto replaced = std::lower_bound(
        replaced_.begin(), replaced_.end(), index,
        [](const auto &item, std::int32_t key) { return item.first < key; });
    if (replaced != replaced_.end() && replaced->first == index)
      return replaced->second;
    const auto &builtin = kBuiltinColors[position];
    return ColorRgb{builtin.red, builtin.green, builtin.blue};
  }
  if (position < size())
    return custom_[position - kBuiltinColors.size()].second;
  return std::nullopt;
}

std::optional<ColorRgba> ColorTable::rgba(std::int32_t index,
                                          float alpha) const noexcept {
  const auto value = rgb(index);
  if (!value.has_value())
    return std::nullopt;
  return ColorRgba{value->red, value->green, value->blue, alpha};
}

std::optional<std::string> ColorTable::name(std::int32_t index) const {
  if (is_hex_code(index)) {
    std::array<char, 9> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "0x%06x",
                  static_cast<unsigned int>(static_cast<std::uint32_t>(index) &
                                            0xFFFFFFU));
    return std::string{buffer.data()};
  }
  for (const auto &special : kSpecialTokens)
    if (special.code == index && special.name != "atom")
      return std::string{special.name};
  if (index < 0)
    return std::nullopt;
  const auto position = static_cast<std::size_t>(index);
  if (position < kBuiltinColors.size())
    return std::string{kBuiltinColors[position].name};
  if (position < size())
    return custom_[position - kBuiltinColors.size()].first;
  return std::nullopt;
}

operation::Result<std::int32_t>
ColorTable::define(std::string_view name, std::array<double, 3> components) {
  using R = operation::Result<std::int32_t>;
  if (!valid_name(name) || special_code(name).has_value())
    return R::failure(invalid(
        "invalid color name: " + std::string{name},
        "use 1-64 characters [A-Za-z0-9_.+-] starting with a letter or _; "
        "default/atomic/atom/object/front/back are reserved"));
  bool byte_scale = false;
  for (const auto component : components) {
    if (!std::isfinite(component) || component < 0.0)
      return R::failure(invalid("color components must be finite and >= 0"));
    byte_scale = byte_scale || component > 1.0;
  }
  ColorRgb value;
  std::array<float *, 3> targets{&value.red, &value.green, &value.blue};
  for (std::size_t index = 0; index < components.size(); ++index) {
    const auto scaled = byte_scale ? components[index] / 255.0 : components[index];
    if (scaled > 1.0)
      return R::failure(invalid(
          "color components must be within 0-1 or 0-255",
          "values above 1 select the 0-255 scale for all three components"));
    *targets[index] = static_cast<float>(scaled);
  }
  const auto lowered = lowercase(name);
  if (const auto builtin = builtin_exact(lowered); builtin.has_value()) {
    const auto found = std::lower_bound(
        replaced_.begin(), replaced_.end(), *builtin,
        [](const auto &item, std::int32_t key) { return item.first < key; });
    if (found != replaced_.end() && found->first == *builtin)
      found->second = value;
    else
      replaced_.insert(found, {*builtin, value});
    return R::success(*builtin);
  }
  for (std::size_t index = 0; index < custom_.size(); ++index)
    if (iequals(custom_[index].first, name)) {
      custom_[index].second = value;
      return R::success(
          static_cast<std::int32_t>(kBuiltinColors.size() + index));
    }
  if (custom_.size() >= kMaximumCustomColors)
    return R::failure(operation::Error{operation::ErrorCode::resource_exhausted,
                                       "custom color table is full", {}});
  custom_.emplace_back(std::string{name}, value);
  return R::success(static_cast<std::int32_t>(size() - 1U));
}

std::vector<ColorEntry> ColorTable::list(ColorListScope scope) const {
  std::vector<ColorEntry> entries;
  for (std::size_t index = 0; index < kBuiltinColors.size(); ++index) {
    const auto &builtin = kBuiltinColors[index];
    const auto signed_index = static_cast<std::int32_t>(index);
    const auto replaced = std::lower_bound(
        replaced_.begin(), replaced_.end(), signed_index,
        [](const auto &item, std::int32_t key) { return item.first < key; });
    const auto modified =
        replaced != replaced_.end() && replaced->first == signed_index;
    if (scope == ColorListScope::custom && !modified)
      continue;
    if (scope == ColorListScope::named && !builtin.named)
      continue;
    entries.push_back(
        {signed_index, std::string{builtin.name},
         modified ? replaced->second
                  : ColorRgb{builtin.red, builtin.green, builtin.blue},
         true, builtin.named, modified});
  }
  for (std::size_t index = 0; index < custom_.size(); ++index)
    entries.push_back(
        {static_cast<std::int32_t>(kBuiltinColors.size() + index),
         custom_[index].first, custom_[index].second, false, true, false});
  return entries;
}

std::optional<std::string>
ColorTable::set_background(std::optional<std::int32_t> index) {
  if (index.has_value() && !rgb(*index).has_value())
    return std::string{"background requires a table or hex color"};
  background_ = index;
  return std::nullopt;
}

ColorRgb ColorTable::background_rgb() const noexcept {
  if (background_.has_value())
    if (const auto value = rgb(*background_); value.has_value())
      return *value;
  return kDefaultBackgroundRgb;
}

std::int32_t ColorTable::define_ramp(ColorRamp ramp) {
  auto stored = std::make_shared<const ColorRamp>(std::move(ramp));
  for (std::size_t index = 0; index < ramps_.size(); ++index)
    if (ramps_[index]->name == stored->name) {
      ramps_[index] = std::move(stored);
      return kColorRampFirst - static_cast<std::int32_t>(index);
    }
  ramps_.push_back(std::move(stored));
  return kColorRampFirst - static_cast<std::int32_t>(ramps_.size() - 1U);
}

const ColorRamp *ColorTable::ramp(std::int32_t code) const noexcept {
  if (code > kColorRampFirst)
    return nullptr;
  const auto index = static_cast<std::size_t>(kColorRampFirst - code);
  return index < ramps_.size() ? ramps_[index].get() : nullptr;
}

std::optional<std::int32_t>
ColorTable::ramp_code(std::string_view name) const noexcept {
  for (std::size_t index = 0; index < ramps_.size(); ++index)
    if (ramps_[index]->name == name)
      return kColorRampFirst - static_cast<std::int32_t>(index);
  return std::nullopt;
}

namespace {

// Trilinear map value at a position, clamped to the grid bounds.
double map_value(const model::VolumeGrid &grid, model::Vec3d position) {
  const auto &d = grid.deltas();
  const auto origin = grid.origin();
  const std::array<double, 9> m{d[0].x, d[1].x, d[2].x, d[0].y, d[1].y,
                                d[2].y, d[0].z, d[1].z, d[2].z};
  const auto det = m[0] * (m[4] * m[8] - m[5] * m[7]) -
                   m[1] * (m[3] * m[8] - m[5] * m[6]) +
                   m[2] * (m[3] * m[7] - m[4] * m[6]);
  const std::array<double, 3> r{position.x - origin.x, position.y - origin.y,
                                position.z - origin.z};
  const auto solve = [&](std::size_t column) {
    auto copy = m;
    for (std::size_t row = 0; row < 3U; ++row)
      copy[row * 3U + column] = r[row];
    return (copy[0] * (copy[4] * copy[8] - copy[5] * copy[7]) -
            copy[1] * (copy[3] * copy[8] - copy[5] * copy[6]) +
            copy[2] * (copy[3] * copy[7] - copy[4] * copy[6])) /
           det;
  };
  const auto shape = grid.shape();
  const std::array<std::size_t, 3> counts{shape.x, shape.y, shape.z};
  std::array<std::size_t, 3> low{};
  std::array<double, 3> weight{};
  for (std::size_t axis = 0; axis < 3U; ++axis) {
    const auto limit = static_cast<double>(counts[axis] - 1U);
    const auto value = std::clamp(det != 0.0 ? solve(axis) : 0.0, 0.0, limit);
    low[axis] = std::min(static_cast<std::size_t>(value),
                         counts[axis] > 1U ? counts[axis] - 2U : 0U);
    weight[axis] = counts[axis] > 1U ? value - static_cast<double>(low[axis]) : 0.0;
  }
  double sum{};
  for (std::size_t corner = 0; corner < 8U; ++corner) {
    double w = 1.0;
    std::array<std::size_t, 3> at{};
    for (std::size_t axis = 0; axis < 3U; ++axis) {
      const auto upper = (corner >> axis) & 1U;
      at[axis] = std::min(low[axis] + upper, counts[axis] - 1U);
      w *= upper != 0U ? weight[axis] : 1.0 - weight[axis];
    }
    if (w != 0.0)
      sum += w * grid.value(at[0], at[1], at[2]);
  }
  return sum;
}

ColorRgb clamp_rgb(std::array<float, 3> value) {
  for (auto &channel : value)
    channel = std::clamp(channel, 0.0F, 1.0F);
  return {value[0], value[1], value[2]};
}

ColorRgb spectrum_rgb(int mode, float v) {
  v = std::clamp(v, 0.0F, 1.0F);
  constexpr auto pi = 3.14159265358979323846F;
  switch (mode) {
  case 1: return clamp_rgb({std::sqrt(v), v * v * v, std::sin(v * 2.0F * pi)});
  case 2: return clamp_rgb({v, std::fabs(v - 0.5F), v * v * v * v});
  case 3: return clamp_rgb({3.0F * v - 2.0F, std::fabs((3.0F * v - 1.0F) / 2.0F), v});
  case 4: return clamp_rgb({3.0F * v, 3.0F * v - 1.0F, 3.0F * v - 2.0F});
  case 5: return clamp_rgb({v / 0.32F - 0.78125F, 2.0F * v - 0.84F, v / 0.08F - 11.5F});
  case 6: return clamp_rgb({std::fabs(2.0F * v - 0.5F), std::sin(v * pi),
                            std::cos(v * pi / 2.0F)});
  case 7: return clamp_rgb({2.0F * v, 2.0F * v - 0.5F, 2.0F * v - 1.0F});
  case 8: return {v, v, v};
  default: return {1.0F, 1.0F, 1.0F};
  }
}

} // namespace

std::optional<ColorRgb> ColorTable::ramp_rgb(std::int32_t code,
                                             model::Vec3d position) const {
  const auto *definition = ramp(code);
  if (definition == nullptr || definition->levels.empty())
    return std::nullopt;
  float level{};
  if (definition->map != nullptr) {
    level = static_cast<float>(map_value(*definition->map, position));
  } else {
    const auto cutoff = static_cast<double>(definition->levels.back());
    std::optional<double> nearest;
    for (const auto &atom : definition->atoms) {
      const auto dx = position.x - atom.x;
      const auto dy = position.y - atom.y;
      const auto dz = position.z - atom.z;
      const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (distance <= cutoff && (!nearest.has_value() || distance < *nearest))
        nearest = distance;
    }
    level = static_cast<float>(nearest.value_or(cutoff + 1.0));
  }
  if (definition->spectrum != 0) {
    const auto base = definition->levels.front();
    auto range = definition->levels.back() - base;
    if (std::fabs(range) < 1.0e-8F)
      range = 1.0F;
    return spectrum_rgb(definition->spectrum, (level - base) / range);
  }
  // With a different number of colors than levels, levels are respaced
  // evenly between the first and last level.
  auto levels = definition->levels;
  const auto count = definition->colors.size();
  if (count >= 2U && count != levels.size()) {
    const auto first = levels.front();
    const auto last = levels.back();
    levels.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
      const auto a = static_cast<float>(index) / static_cast<float>(count - 1U);
      levels[index] = first * (1.0F - a) + last * a;
    }
  }
  const auto n = std::min(levels.size(), count);
  if (n == 0U)
    return std::nullopt;
  std::ptrdiff_t ge = -1;
  for (auto index = static_cast<std::ptrdiff_t>(n) - 1; index >= 0; --index)
    if (level >= levels[static_cast<std::size_t>(index)]) {
      ge = index;
      break;
    }
  auto le = static_cast<std::ptrdiff_t>(n);
  for (std::size_t index = 0; index < n; ++index)
    if (level <= levels[index]) {
      le = static_cast<std::ptrdiff_t>(index);
      break;
    }
  const auto &colors = definition->colors;
  if (ge == le)
    return colors[static_cast<std::size_t>(ge)];
  if (le == 0)
    return colors.front();
  if (ge == static_cast<std::ptrdiff_t>(n) - 1)
    return colors[n - 1U];
  const auto lo = colors[static_cast<std::size_t>(le)];
  const auto hi = colors[static_cast<std::size_t>(ge)];
  const auto d = levels[static_cast<std::size_t>(ge)] - levels[static_cast<std::size_t>(le)];
  if (std::fabs(d) <= 1.0e-8F)
    return hi;
  const auto x0 = (level - levels[static_cast<std::size_t>(le)]) / d;
  const auto x1 = 1.0F - x0;
  return clamp_rgb({x0 * hi.red + x1 * lo.red, x0 * hi.green + x1 * lo.green,
                    x0 * hi.blue + x1 * lo.blue});
}

ColorRgb ColorTable::front_rgb() const noexcept {
  const auto back = background_rgb();
  return {1.0F - back.red, 1.0F - back.green, 1.0F - back.blue};
}

std::span<const std::int32_t> ColorTable::auto_cycle() noexcept {
  return kAutoColorCycle;
}

std::int32_t ColorTable::element_color(unsigned int atomic_number) noexcept {
  if (atomic_number == 0U || atomic_number >= kElementColors.size())
    return 0;
  return kElementColors[atomic_number];
}

} // namespace molshredder::render
