#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/render/packet.hpp"
#include "molshredder/render/representation.hpp"

namespace molshredder::application {

// VMD-style representation instance (Q-038 a): one entry of an object's
// ordered representation list, independent of the PyMOL show/hide masks.
struct RepInstance {
  std::string name;                  // "rep0", "rep1", ... unless renamed
  std::string style{"Lines"};        // canonical VMD style name
  std::vector<double> params;        // style parameters (size, resolution, ...)
  std::string selection{"all"};
  std::string color{"Name"};         // canonical VMD coloring method
  std::int32_t color_id{1};          // for ColorID
  std::string material{"Opaque"};
  bool visible{true};
  bool selection_update{false};      // re-evaluate the selection on every frame
  bool color_update{false};          // recolor on every frame
  std::int64_t smooth_window{0};
  std::string draw_frames{"now"};
  std::optional<std::pair<double, double>> scale_range;  // value color scale
  // Atoms captured when the selection is not updated per frame (sorted ids).
  std::vector<std::uint64_t> frozen_atoms;
  bool frozen{false};
  struct ClipPlane {
    std::array<double, 3> center{0.0, 0.0, 0.0};
    std::array<double, 3> normal{0.0, 0.0, 1.0};
    std::array<double, 3> color{1.0, 1.0, 1.0};
    bool enabled{false};
  };
  std::vector<ClipPlane> clip_planes;  // VMD allows several per representation
};

// Defaults used by `rep add` (VMD `mol default`).
struct RepDefaults {
  std::string style{"Lines"};
  std::string color{"Name"};
  std::int32_t color_id{1};
  std::string selection{"all"};
  std::string material{"Opaque"};
};

struct VmdStyleInfo {
  std::string name;                       // canonical spelling
  bool renderable{};                      // drawn by MolShredder
  std::vector<render::RepresentationKind> kinds;  // packets built for it
};

// Canonical style/coloring/material names (case-insensitive lookup).
[[nodiscard]] std::optional<VmdStyleInfo> find_vmd_style(std::string_view name);
[[nodiscard]] std::optional<std::string> canonical_vmd_coloring(std::string_view name);
[[nodiscard]] bool coloring_renderable(std::string_view canonical);
[[nodiscard]] std::optional<std::string> canonical_vmd_material(std::string_view name);
[[nodiscard]] std::vector<std::string> vmd_style_names();
[[nodiscard]] std::vector<std::string> vmd_coloring_names();
[[nodiscard]] std::vector<std::string> vmd_material_names();
// VMD color id (0-32) to color; nullopt when out of range.
[[nodiscard]] std::optional<render::ColorRgba> vmd_color_id_rgba(std::int32_t id) noexcept;

// Per-atom colors for a coloring method (renderable methods only; the others
// fall back to the molecule color id). `scale_range` overrides min/max of the
// value-based methods.
[[nodiscard]] std::vector<render::ColorRgba> vmd_atom_colors(
    const RepInstance &instance, const model::Topology &topology,
    const model::CoordinateFrame &frame, std::int32_t molecule_color_id);

// Style parameters applied to the render style of one packet kind.
void apply_vmd_style_parameters(const RepInstance &instance,
                                render::RepresentationKind kind,
                                render::RepresentationStyle &style,
                                double &sphere_scale_out);

} // namespace molshredder::application
