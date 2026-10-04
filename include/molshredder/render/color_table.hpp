#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/model/volume.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/render/packet.hpp"

namespace molshredder::render {

// PyMOL-compatible color index space. Indices in [0, ColorTable::size())
// address table entries, kColorHexFlag | 0xRRGGBB encodes a literal 24-bit
// color and negative values are symbolic tokens resolved by the consumer.
inline constexpr std::int32_t kColorDefault = -1;
inline constexpr std::int32_t kColorAtomic = -4;
inline constexpr std::int32_t kColorObject = -5;
inline constexpr std::int32_t kColorFront = -6;
inline constexpr std::int32_t kColorBack = -7;
inline constexpr std::int32_t kColorHexFlag = 0x40000000;
// Color ramps occupy codes kColorRampFirst, kColorRampFirst - 1, ... in
// creation order (PyMOL 3.1.0 assigns -10, -11, ...).
inline constexpr std::int32_t kColorRampFirst = -10;
inline constexpr std::string_view kColorCatalogRevision =
    "pymol-oss-3.1.0-colors-v1";

struct ColorRgb {
  float red{};
  float green{};
  float blue{};

  friend bool operator==(const ColorRgb &, const ColorRgb &) = default;
};

struct ColorEntry {
  std::int32_t index{};
  std::string name;
  ColorRgb rgb;
  bool builtin{};
  bool named{};
  bool modified{};

  friend bool operator==(const ColorEntry &, const ColorEntry &) = default;
};

enum class ColorListScope { named, custom, all };

// PyMOL ramp: value levels mapped to colors, evaluated at a position from a
// map (trilinear, clamped to the grid) or from the distance to the nearest
// atom of a source structure (cutoff = last level). spectrum 1..8 selects a
// built-in formula (traditional, sludge, ocean, hot, grayable, rainbow,
// afmhot, grayscale) over the first..last level range.
struct ColorRamp {
  std::string name;
  std::shared_ptr<const model::VolumeGrid> map;
  std::vector<model::Vec3d> atoms;
  // Workspace object id of a proximity source (0 for map ramps).
  std::uint64_t source_object{};
  std::vector<float> levels;
  std::vector<ColorRgb> colors;
  int spectrum{};
};

// MolShredder viewport background used until a background color is set.
inline constexpr ColorRgb kDefaultBackgroundRgb{0.018F, 0.025F, 0.04F};

// Immutable-by-value color table: the built-in PyMOL 3.1.0 catalog plus
// user definitions. Copies are cheap; definitions never remove an index.
class ColorTable {
public:
  [[nodiscard]] static std::size_t builtin_size() noexcept;
  [[nodiscard]] std::size_t size() const noexcept;

  // Lookup order: exact case-insensitive name, decimal index or special code,
  // 0xRRGGBB, #RRGGBB (MolShredder extension), special token, then the
  // lowest-index case-insensitive prefix match.
  [[nodiscard]] operation::Result<std::int32_t>
  resolve(std::string_view token) const;
  // Assignment lookup used by `color` and `bg_color`: like resolve(), but a
  // prefix shared by several named colors is an "ambiguous color" error; only
  // when no named color matches does the lowest-index table prefix apply.
  [[nodiscard]] operation::Result<std::int32_t>
  resolve_assignment(std::string_view token) const;
  // RGB for a table index or literal hex code; nullopt for symbolic codes.
  [[nodiscard]] std::optional<ColorRgb> rgb(std::int32_t index) const noexcept;
  [[nodiscard]] std::optional<ColorRgba> rgba(std::int32_t index,
                                              float alpha) const noexcept;
  // Canonical display name: table name, 0xrrggbb or special token.
  [[nodiscard]] std::optional<std::string> name(std::int32_t index) const;

  // Creates or replaces a named color. Components above 1 switch the whole
  // triple to the 0-255 scale. Replacements keep their index.
  [[nodiscard]] operation::Result<std::int32_t>
  define(std::string_view name, std::array<double, 3> components);

  [[nodiscard]] std::vector<ColorEntry> list(ColorListScope scope) const;

  // Background color index (table or literal hex); nullopt keeps the
  // MolShredder default. `front` resolves to 1 - background per channel and
  // `back` to the background itself.
  [[nodiscard]] std::optional<std::int32_t> background() const noexcept {
    return background_;
  }
  [[nodiscard]] std::optional<std::string>
  set_background(std::optional<std::int32_t> index);
  [[nodiscard]] ColorRgb background_rgb() const noexcept;
  [[nodiscard]] ColorRgb front_rgb() const noexcept;

  // Adds or replaces a ramp by name and returns its color code.
  [[nodiscard]] std::int32_t define_ramp(ColorRamp ramp);
  [[nodiscard]] const ColorRamp *ramp(std::int32_t code) const noexcept;
  [[nodiscard]] std::optional<std::int32_t>
  ramp_code(std::string_view name) const noexcept;
  // Color of a ramp code at a position (packet units); nullopt otherwise.
  [[nodiscard]] std::optional<ColorRgb>
  ramp_rgb(std::int32_t code, model::Vec3d position) const;

  [[nodiscard]] static std::span<const std::int32_t> auto_cycle() noexcept;
  // PyMOL `color atomic` index for an atomic number; kColorDefault when the
  // reference leaves the atom unchanged. Unknown elements map to white (0).
  [[nodiscard]] static std::int32_t
  element_color(unsigned int atomic_number) noexcept;

  friend bool operator==(const ColorTable &, const ColorTable &) = default;

private:
  std::vector<std::pair<std::int32_t, ColorRgb>> replaced_;
  std::vector<std::pair<std::string, ColorRgb>> custom_;
  std::optional<std::int32_t> background_;
  std::vector<std::shared_ptr<const ColorRamp>> ramps_;
};

[[nodiscard]] bool is_special_color_token(std::string_view token) noexcept;

} // namespace molshredder::render
