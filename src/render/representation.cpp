#include "molshredder/render/representation.hpp"

#include "pymol_backbone.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "molshredder/operation/error.hpp"
#include "molshredder/scene/math.hpp"

namespace molshredder::render {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

std::vector<model::Vec3d> positions_as_double(
    const model::CoordinateBuffer& coordinates) {
  return std::visit(
      [](const auto& values) {
        std::vector<model::Vec3d> result;
        result.reserve(values.size());
        for (const auto& value : values) {
          result.push_back(model::Vec3d{static_cast<double>(value.x),
                                        static_cast<double>(value.y),
                                        static_cast<double>(value.z)});
        }
        return result;
      },
      coordinates.values());
}

bool atom_selected(const RepresentationRequest& request,
                   std::size_t index) noexcept {
  return request.selected.empty() || request.selected[index] != 0U;
}

RenderSettingContext setting_context(const RepresentationRequest& request,
                                     std::uint64_t atom_id = 0U,
                                     std::uint64_t bond_id = 0U) {
  return {request.object_id, request.frame_index,
          atom_id, bond_id};
}

template <typename Value>
operation::Result<Value> setting_value(const RepresentationRequest& request,
                                       std::string_view name,
                                       const RenderSettingContext& context,
                                       Value fallback) {
  if (request.settings == nullptr)
    return operation::Result<Value>::success(std::move(fallback));
  const auto resolved = request.settings->resolve(name, context);
  if (!resolved.has_value())
    return operation::Result<Value>::failure(resolved.error());
  const auto* value = std::get_if<Value>(&resolved.value().value);
  if (value == nullptr)
    return operation::Result<Value>::failure(
        invalid("resolved render setting has the wrong value type: " +
                std::string{name}));
  return operation::Result<Value>::success(*value);
}

operation::Result<ColorRgba> setting_color(
    const RepresentationRequest& request, std::string_view name,
    const RenderSettingContext& context, ColorRgba atomic_color,
    float alpha) {
  const auto value = setting_value(request, name, context, std::string{"-1"});
  if (!value.has_value())
    return operation::Result<ColorRgba>::failure(value.error());
  auto color = atomic_color;
  color.alpha = alpha;
  const auto token = std::string_view{value.value()};
  if (token == "-1")
    return operation::Result<ColorRgba>::success(color);
  static const ColorTable builtin_colors;
  const auto& table =
      request.colors != nullptr ? *request.colors : builtin_colors;
  const auto index = table.resolve(token);
  if (!index.has_value())
    return operation::Result<ColorRgba>::failure(
        invalid("unsupported render setting color token: " + value.value(),
                "use atomic, object, a color name or index, 0xRRGGBB or "
                "#RRGGBB"));
  auto resolved = index.value();
  if (resolved == kColorObject)
    resolved = request.object_color;
  if (resolved == kColorDefault || resolved == kColorAtomic)
    return operation::Result<ColorRgba>::success(color);
  if (const auto rgba = table.rgba(resolved, alpha); rgba.has_value())
    return operation::Result<ColorRgba>::success(*rgba);
  if (resolved == kColorFront || resolved == kColorBack) {
    const auto rgb =
        resolved == kColorFront ? table.front_rgb() : table.background_rgb();
    return operation::Result<ColorRgba>::success(
        ColorRgba{rgb.red, rgb.green, rgb.blue, alpha});
  }
  return operation::Result<ColorRgba>::success(color);
}

double coordinate_scale(const RepresentationRequest& request) noexcept {
  return request.frame->metadata().coordinate_unit ==
                 operation::LengthUnit::nanometer
             ? 0.1
             : 1.0;
}

model::Vec3d stable_perpendicular(model::Vec3d direction) {
  const std::array axes{model::Vec3d{1.0, 0.0, 0.0},
                        model::Vec3d{0.0, 1.0, 0.0},
                        model::Vec3d{0.0, 0.0, 1.0}};
  const auto axis = *std::min_element(
      axes.begin(), axes.end(), [&direction](const auto& first,
                                             const auto& second) {
        return std::abs(scene::dot(direction, first)) <
               std::abs(scene::dot(direction, second));
      });
  return scene::normalized(scene::cross(direction, axis));
}

std::pair<model::Vec3d, bool> valence_perpendicular(
    const RepresentationRequest& request,
    const std::vector<model::Vec3d>& positions, std::size_t bond_index,
    model::Vec3d direction, std::int64_t mode) {
  using scene::operator-;
  using scene::operator*;
  if (mode == 1) {
    const auto& target = request.topology->bonds()[bond_index];
    const std::array endpoints{target.first.value, target.second.value};
    // Heavy-atom neighbors define the valence plane before hydrogens, so
    // ring double bonds draw their inner line toward the ring like PyMOL.
    for (const auto heavy_only : {true, false}) {
      for (const auto endpoint : endpoints) {
        for (std::size_t index = 0; index < request.topology->bonds().size();
             ++index) {
          if (index == bond_index)
            continue;
          const auto& candidate = request.topology->bonds()[index];
          std::optional<std::size_t> neighbor;
          if (candidate.first.value == endpoint)
            neighbor = candidate.second.value;
          else if (candidate.second.value == endpoint)
            neighbor = candidate.first.value;
          if (!neighbor.has_value() ||
              !request.frame->atom_present(*neighbor) ||
              (heavy_only &&
               request.topology->atoms()[*neighbor].atomic_number <= 1U))
            continue;
          auto projected = positions[*neighbor] - positions[endpoint];
          projected = projected - direction * scene::dot(projected, direction);
          if (scene::length(projected) > 1.0e-12)
            return {scene::normalized(projected), false};
        }
      }
    }
    return {stable_perpendicular(direction), true};
  }
  return {stable_perpendicular(direction), false};
}

std::size_t valence_order(model::BondOrder order) noexcept {
  switch (order) {
  case model::BondOrder::double_bond:
  case model::BondOrder::aromatic:
    return 2U;
  case model::BondOrder::triple:
    return 3U;
  case model::BondOrder::single:
  case model::BondOrder::amide:
  case model::BondOrder::unknown:
  case model::BondOrder::query:
    return 1U;
  case model::BondOrder::zero:
    return 0U;
  }
  return 0U;
}

std::uint64_t next_pick_id(RenderPacket& packet, PickTarget target) {
  const auto id = static_cast<std::uint64_t>(packet.pick_targets.size()) + 1U;
  packet.pick_targets.emplace(id, std::move(target));
  return id;
}

bool is_sheet(analysis::SecondaryStructureState state) noexcept {
  return state == analysis::SecondaryStructureState::extended_strand ||
         state == analysis::SecondaryStructureState::beta_bridge;
}

bool is_helix(analysis::SecondaryStructureState state) noexcept {
  return state == analysis::SecondaryStructureState::alpha_helix ||
         state == analysis::SecondaryStructureState::helix_310 ||
         state == analysis::SecondaryStructureState::pi_helix;
}

// PyMOL cartoon_*_quality / cartoon_sampling: -1 adapts to the atom count.
int adaptive_quality(std::int64_t value, std::size_t atoms,
                     std::array<int, 4> levels, int minimum) {
  if (value == -1) {
    return atoms < 100000U ? levels[0]
           : atoms < 500000U ? levels[1]
           : atoms < 999999U ? levels[2]
                             : levels[3];
  }
  return std::max(static_cast<int>(value), minimum);
}

operation::Result<RenderPacket> build_backbone_representation(
    const RepresentationRequest& request, const std::vector<model::Vec3d>& positions,
    RenderPacket packet) {
  const auto residue_count = request.topology->residue_count();
  const auto atom_count = request.topology->atom_count();
  // Secondary structure: PDB HELIX/SHEET codes (the `ss` atom property, as
  // PyMOL reads them) when present, otherwise caller-supplied or assigned
  // states.
  std::vector<char> residue_ss(residue_count, 'L');
  const auto* ss_property = request.topology->properties().find("ss");
  const auto* ss_codes =
      ss_property == nullptr
          ? nullptr
          : std::get_if<std::vector<std::string>>(&ss_property->values);
  if (ss_codes != nullptr) {
    for (std::size_t atom = 0; atom < atom_count && atom < ss_codes->size(); ++atom) {
      const auto& record = request.topology->atoms()[atom];
      if (record.name == "CA" && !(*ss_codes)[atom].empty())
        residue_ss[record.residue.value] = (*ss_codes)[atom].front();
    }
    packet.provenance.emplace("secondary_structure_method", "pdb-helix-sheet");
  } else {
    std::vector<analysis::SecondaryStructureState> calculated_states;
    auto states = request.secondary_structure;
    if (!states.empty() && states.size() != residue_count) {
      return operation::Result<RenderPacket>::failure(
          invalid("secondary-structure state count must match residue count"));
    }
    if (states.empty()) {
      const auto assigned = analysis::assign_secondary_structure(
          *request.topology, *request.frame);
      if (!assigned.has_value()) {
        return operation::Result<RenderPacket>::failure(assigned.error());
      }
      for (const auto& residue : assigned.value().residues)
        calculated_states.push_back(residue.state);
      states = calculated_states;
      packet.provenance.emplace("secondary_structure_method",
                                assigned.value().method);
      packet.provenance.emplace("exact_stride_parity", "false");
    } else {
      packet.provenance.emplace("secondary_structure_method", "caller-supplied");
    }
    for (std::size_t residue = 0; residue < residue_count; ++residue)
      residue_ss[residue] = is_helix(states[residue])   ? 'H'
                            : is_sheet(states[residue]) ? 'S'
                                                        : 'L';
  }

  std::vector<std::int32_t> atom_type;
  if (request.style.kind == RepresentationKind::cartoon &&
      request.cartoon_types != nullptr) {
    atom_type.assign(atom_count, 0);
    for (const auto& [atom_id, code] : *request.cartoon_types) {
      const auto index = request.topology->atom_index(model::AtomId{atom_id});
      if (index.has_value() && index->value < atom_type.size())
        atom_type[index->value] = code;
    }
  }
  std::vector<std::uint8_t> present(atom_count, 0U);
  std::vector<std::uint8_t> visible(atom_count, 0U);
  for (std::size_t atom = 0; atom < atom_count; ++atom) {
    present[atom] = request.frame->atom_present(atom) ? 1U : 0U;
    visible[atom] = present[atom] != 0U && atom_selected(request, atom) ? 1U : 0U;
  }
  std::vector<std::uint64_t> residue_pick(residue_count, 0U);
  detail::PymolBackboneInput input{
      request.topology,
      positions,
      present,
      visible,
      request.atom_visuals,
      residue_ss,
      atom_type,
      coordinate_scale(request),
      [&](model::ResidueIndex residue) {
        auto& pick = residue_pick[residue.value];
        if (pick == 0U)
          pick = next_pick_id(packet, PickTarget{PickKind::residue,
                                                 request.scene_node_id, std::nullopt,
                                                 std::nullopt, residue});
        return pick;
      }};
  const auto context = setting_context(request);
  if (request.style.kind == RepresentationKind::ribbon) {
    const auto sampling = setting_value(request, "ribbon_sampling", context, std::int64_t{1});
    const auto width = setting_value(request, "ribbon_width", context, 3.0);
    if (!sampling.has_value() || !width.has_value())
      return operation::Result<RenderPacket>::failure(
          !sampling.has_value() ? sampling.error() : width.error());
    detail::PymolRibbonParameters parameters;
    parameters.sampling = std::max(1, static_cast<int>(sampling.value()));
    parameters.width_pixels = width.value();
    detail::append_pymol_ribbon(input, parameters, packet);
    packet.provenance.emplace("backbone_method", "pymol-3.1.0-ribbon");
    return operation::Result<RenderPacket>::success(std::move(packet));
  }
  const auto sampling = setting_value(request, "cartoon_sampling", context, std::int64_t{-1});
  const auto rect_length = setting_value(request, "cartoon_rect_length", context, 1.4);
  const auto rect_width = setting_value(request, "cartoon_rect_width", context, 0.4);
  const auto gap_cutoff = setting_value(request, "cartoon_gap_cutoff", context, std::int64_t{10});
  if (!sampling.has_value() || !rect_length.has_value() || !rect_width.has_value() ||
      !gap_cutoff.has_value()) {
    const auto* error = !sampling.has_value()      ? &sampling.error()
                        : !rect_length.has_value() ? &rect_length.error()
                        : !rect_width.has_value()  ? &rect_width.error()
                                                   : &gap_cutoff.error();
    return operation::Result<RenderPacket>::failure(*error);
  }
  detail::PymolCartoonParameters parameters;
  parameters.sampling = adaptive_quality(sampling.value(), atom_count, {7, 5, 3, 2}, 1);
  parameters.loop_quality = adaptive_quality(-1, atom_count, {6, 6, 5, 4}, 3);
  parameters.oval_quality = adaptive_quality(-1, atom_count, {10, 8, 7, 6}, 3);
  parameters.tube_quality = adaptive_quality(-1, atom_count, {9, 7, 6, 5}, 3);
  parameters.rect_length = rect_length.value();
  parameters.rect_width = rect_width.value();
  parameters.gap_cutoff = static_cast<int>(gap_cutoff.value());
  const auto fallbacks = detail::append_pymol_cartoon(input, parameters, packet);
  packet.provenance.emplace("backbone_method", "pymol-3.1.0-cartoon");
  if (!fallbacks.empty()) {
    std::string text;
    for (const auto code : fallbacks)
      text += (text.empty() ? "" : ",") + std::to_string(code);
    packet.provenance.emplace("cartoon_type_fallback", text);
  }
  if (packet.mesh_vertices.size() >
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    return operation::Result<RenderPacket>::failure(
        invalid("backbone mesh exceeds 32-bit index capacity"));
  }
  return operation::Result<RenderPacket>::success(std::move(packet));
}

}  // namespace

bool is_valid(ColorRgba color) noexcept {
  const auto channel = [](float value) {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
  };
  return channel(color.red) && channel(color.green) && channel(color.blue) &&
         channel(color.alpha);
}

operation::Result<std::uint32_t> checked_gpu_mesh_index(std::uint64_t index) {
  if (index > std::numeric_limits<std::uint32_t>::max()) {
    return operation::Result<std::uint32_t>::failure(
        invalid("mesh index exceeds the uint32 GPU index contract",
                "partition the geometry into bounded draw packets"));
  }
  return operation::Result<std::uint32_t>::success(
      static_cast<std::uint32_t>(index));
}

SphereModeResolution resolve_sphere_mode(
    std::int64_t requested, bool shader_enabled,
    SphereBackendCapabilities capabilities) {
  const auto fallback = [&](std::string reason) {
    if (shader_enabled && capabilities.analytic_impostors)
      return SphereModeResolution{requested, 9, true, std::move(reason)};
    if (capabilities.triangle_spheres)
      return SphereModeResolution{requested, 0, true, std::move(reason)};
    return SphereModeResolution{requested, 9, true,
                                std::move(reason) +
                                    "; using canonical analytic CPU/GPU sphere"};
  };
  auto normalized = requested;
  if (requested == -1 || requested == 4 || requested == 5)
    normalized = 9;
  if (normalized == 9) {
    if (shader_enabled && capabilities.analytic_impostors) {
      return {requested, 9, requested != 9,
              requested == 4 || requested == 5
                  ? "removed legacy sphere mode resolved to analytic impostor"
              : requested == -1 ? "automatic sphere mode resolved to analytic impostor"
                                : ""};
    }
    if (capabilities.triangle_spheres)
      return {requested, 0, true,
              "analytic impostor unavailable; using triangle sphere"};
    return fallback("requested analytic sphere backend is unavailable");
  }
  if (normalized == 0 && capabilities.triangle_spheres)
    return {requested, 0, false, {}};
  if ((normalized == 1 || normalized == 2 || normalized == 3 ||
       normalized == 6 || normalized == 7 || normalized == 8) &&
      capabilities.point_sprites)
    return {requested, normalized, false, {}};
  if ((normalized == 10 || normalized == 11) && capabilities.low_polyhedra)
    return {requested, normalized, false, {}};
  return fallback("requested sphere mode is unsupported by this backend");
}

void include(Bounds3d& bounds, model::Vec3d point, double radius) {
  const model::Vec3d lower{point.x - radius, point.y - radius,
                           point.z - radius};
  const model::Vec3d upper{point.x + radius, point.y + radius,
                           point.z + radius};
  if (bounds.empty) {
    bounds.minimum = lower;
    bounds.maximum = upper;
    bounds.empty = false;
    return;
  }
  bounds.minimum.x = std::min(bounds.minimum.x, lower.x);
  bounds.minimum.y = std::min(bounds.minimum.y, lower.y);
  bounds.minimum.z = std::min(bounds.minimum.z, lower.z);
  bounds.maximum.x = std::max(bounds.maximum.x, upper.x);
  bounds.maximum.y = std::max(bounds.maximum.y, upper.y);
  bounds.maximum.z = std::max(bounds.maximum.z, upper.z);
}

operation::Result<RenderPacket> build_representation(
    const RepresentationRequest& request) {
  using scene::operator+;
  using scene::operator-;
  using scene::operator*;

  if (request.topology == nullptr || request.frame == nullptr) {
    return operation::Result<RenderPacket>::failure(
        invalid("representation requires topology and coordinate frame"));
  }
  const auto atom_count = request.topology->atom_count();
  if (request.frame->atom_count() != atom_count ||
      request.atom_visuals.size() != atom_count ||
      (!request.selected.empty() && request.selected.size() != atom_count)) {
    return operation::Result<RenderPacket>::failure(
        invalid("representation topology, frame, visual, and selection counts "
                "must match"));
  }
  if (!std::isfinite(request.style.line_width_pixels) ||
      request.style.line_width_pixels <= 0.0F ||
      !std::isfinite(request.style.stick_radius) ||
      request.style.stick_radius <= 0.0 ||
      !std::isfinite(request.style.sphere_scale) ||
      request.style.sphere_scale <= 0.0 ||
      request.style.backbone_samples_per_residue == 0U ||
      request.style.backbone_samples_per_residue > 64U ||
      !std::isfinite(request.style.ribbon_width) ||
      request.style.ribbon_width <= 0.0 ||
      !std::isfinite(request.style.cartoon_coil_width) ||
      request.style.cartoon_coil_width <= 0.0 ||
      !std::isfinite(request.style.cartoon_helix_width) ||
      request.style.cartoon_helix_width <= 0.0 ||
      !std::isfinite(request.style.cartoon_sheet_width) ||
      request.style.cartoon_sheet_width <= 0.0 ||
      !std::isfinite(request.style.cartoon_thickness) ||
      request.style.cartoon_thickness <= 0.0 ||
      !std::isfinite(request.style.cartoon_chain_break_distance) ||
      request.style.cartoon_chain_break_distance <= 0.0) {
    return operation::Result<RenderPacket>::failure(
        invalid("representation widths, radii, and scales must be positive"));
  }
  for (std::size_t index = 0; index < atom_count; ++index) {
    if (!is_valid(request.atom_visuals[index].color) ||
        !std::isfinite(request.atom_visuals[index].sphere_radius) ||
        request.atom_visuals[index].sphere_radius <= 0.0) {
      return operation::Result<RenderPacket>::failure(
          invalid("atom visual contains invalid color or sphere radius"));
    }
    if (!request.selected.empty() && request.selected[index] > 1U) {
      return operation::Result<RenderPacket>::failure(
          invalid("representation selection mask must contain only 0 or 1"));
    }
  }

  RenderPacket packet;
  packet.topology_version = request.topology->version();
  packet.frame_index = request.frame_index;
  packet.scene_node_id = request.scene_node_id;
  const auto positions = positions_as_double(request.frame->positions());

  if (request.style.kind == RepresentationKind::ribbon ||
      request.style.kind == RepresentationKind::cartoon) {
    return build_backbone_representation(request, positions, std::move(packet));
  }

  const auto unit_scale = coordinate_scale(request);
  if (request.style.kind == RepresentationKind::labels) {
    packet.provenance.emplace("render_setting_catalog",
                              std::string{kRenderSettingCatalogRevision});
    if (request.atom_labels == nullptr)
      return operation::Result<RenderPacket>::success(std::move(packet));
    for (const auto& [atom_id, text] : *request.atom_labels) {
      const auto atom = request.topology->atom_index(model::AtomId{atom_id});
      if (!atom.has_value() || atom->value >= atom_count || text.empty())
        continue;
      const auto index = atom->value;
      if (!request.frame->atom_present(index) || !atom_selected(request, index))
        continue;
      const auto context = setting_context(request, atom_id);
      const auto color = setting_color(request, "label_color", context,
                                       request.atom_visuals[index].color, 1.0F);
      const auto size = setting_value(request, "label_size", context, 14.0);
      const auto font =
          setting_value(request, "label_font_id", context, std::int64_t{5});
      const auto position = setting_value(
          request, "label_position", context,
          RenderSettingVector3{0.0, 0.0, 1.75});
      const auto placement = setting_value(
          request, "label_placement_offset", context,
          RenderSettingVector3{0.0, 0.0, 0.0});
      if (!color.has_value())
        return operation::Result<RenderPacket>::failure(color.error());
      if (!size.has_value())
        return operation::Result<RenderPacket>::failure(size.error());
      if (!font.has_value())
        return operation::Result<RenderPacket>::failure(font.error());
      if (!position.has_value())
        return operation::Result<RenderPacket>::failure(position.error());
      if (!placement.has_value())
        return operation::Result<RenderPacket>::failure(placement.error());
      const auto& offset = position.value();
      const auto& extra = placement.value();
      packet.labels.push_back(TextLabelInstance{
          positions[index], text, color.value(), 0U, size.value(),
          font.value(),
          model::Vec3d{(offset[0] + extra[0]) * unit_scale,
                       (offset[1] + extra[1]) * unit_scale,
                       (offset[2] + extra[2]) * unit_scale},
          request.object_id, atom_id});
      include(packet.bounds, positions[index]);
    }
    return operation::Result<RenderPacket>::success(std::move(packet));
  }
  if (request.style.kind == RepresentationKind::spheres) {
    const auto object_context = setting_context(request);
    const auto sphere_mode = setting_value(
        request, "sphere_mode", object_context, std::int64_t{9});
    const auto use_shader =
        setting_value(request, "sphere_use_shader", object_context, true);
    if (!sphere_mode.has_value())
      return operation::Result<RenderPacket>::failure(sphere_mode.error());
    if (!use_shader.has_value())
      return operation::Result<RenderPacket>::failure(use_shader.error());
    const auto mode = resolve_sphere_mode(sphere_mode.value(),
                                          use_shader.value(),
                                          request.sphere_backend);
    packet.provenance.emplace("render_setting_catalog",
                              std::string{kRenderSettingCatalogRevision});
    packet.provenance.emplace("sphere_mode_requested",
                              std::to_string(mode.requested));
    packet.provenance.emplace("sphere_mode_effective",
                              std::to_string(mode.effective));
    packet.provenance.emplace("sphere_mode_fallback",
                              mode.fallback ? "true" : "false");
    if (!mode.reason.empty())
      packet.provenance.emplace("sphere_mode_reason", mode.reason);
    packet.spheres.reserve(atom_count);
    for (std::size_t index = 0; index < atom_count; ++index) {
      if (!request.frame->atom_present(index) ||
          !atom_selected(request, index)) {
        continue;
      }
      const auto atom_id = request.topology->atom_id(model::AtomIndex{index});
      if (!atom_id.has_value())
        return operation::Result<RenderPacket>::failure(
            invalid("topology atom identity is missing"));
      const auto context = setting_context(request, atom_id->value);
      const auto scale = setting_value(request, "sphere_scale", context,
                                       request.style.sphere_scale);
      const auto transparency =
          setting_value(request, "sphere_transparency", context, 0.0);
      if (!scale.has_value())
        return operation::Result<RenderPacket>::failure(scale.error());
      if (!transparency.has_value())
        return operation::Result<RenderPacket>::failure(transparency.error());
      const auto color = setting_color(
          request, "sphere_color", context, request.atom_visuals[index].color,
          static_cast<float>(1.0 - transparency.value()));
      if (!color.has_value())
        return operation::Result<RenderPacket>::failure(color.error());
      const auto radius = request.atom_visuals[index].sphere_radius *
                          scale.value() * unit_scale;
      const auto pick_id = next_pick_id(
          packet, PickTarget{PickKind::atom, request.scene_node_id,
                             model::AtomIndex{index}, std::nullopt,
                             std::nullopt});
      packet.spheres.push_back(SphereInstance{
          positions[index], radius, color.value(), pick_id});
      include(packet.bounds, positions[index], radius);
    }
    return operation::Result<RenderPacket>::success(std::move(packet));
  }

  const auto& bonds = request.topology->bonds();
  packet.provenance.emplace("render_setting_catalog",
                            std::string{kRenderSettingCatalogRevision});
  std::vector<double> ball_radius(atom_count, 0.0);
  std::vector<ColorRgba> ball_color(atom_count);
  std::size_t valence_fallback_count{};
  packet.lines.reserve(bonds.size() * 6U);
  packet.cylinders.reserve(bonds.size() * 6U);
  for (std::size_t bond_index = 0; bond_index < bonds.size(); ++bond_index) {
    const auto& bond = bonds[bond_index];
    const auto first = bond.first.value;
    const auto second = bond.second.value;
    if (!request.frame->atom_present(first) ||
        !request.frame->atom_present(second)) {
      continue;
    }
    bool first_visible = atom_selected(request, first);
    bool second_visible = atom_selected(request, second);
    const auto bond_id = request.topology->bond_id(bond_index);
    const auto first_id =
        request.topology->atom_id(model::AtomIndex{first});
    const auto second_id =
        request.topology->atom_id(model::AtomIndex{second});
    if (!bond_id.has_value() || !first_id.has_value() ||
        !second_id.has_value()) {
      return operation::Result<RenderPacket>::failure(
          invalid("topology atom or bond identity is missing"));
    }
    const auto context = setting_context(request, 0U, bond_id->value);
    const auto first_context =
        setting_context(request, first_id->value, bond_id->value);
    const auto second_context =
        setting_context(request, second_id->value, bond_id->value);
    const auto half_bonds = setting_value(request, "half_bonds", context, false);
    const auto hide_long =
        setting_value(request, "hide_long_bonds", context, false);
    const auto valence = setting_value(request, "valence", context, true);
    const auto zero_mode =
        setting_value(request, "valence_zero_mode", context, std::int64_t{1});
    const auto valence_mode =
        setting_value(request, "valence_mode", context, std::int64_t{1});
    if (!half_bonds.has_value() || !hide_long.has_value() ||
        !valence.has_value() || !zero_mode.has_value() ||
        !valence_mode.has_value()) {
      const auto* error = !half_bonds.has_value() ? &half_bonds.error()
                          : !hide_long.has_value() ? &hide_long.error()
                          : !valence.has_value() ? &valence.error()
                          : !zero_mode.has_value() ? &zero_mode.error()
                                                   : &valence_mode.error();
      return operation::Result<RenderPacket>::failure(*error);
    }
    if (!(first_visible && second_visible) && !half_bonds.value())
      continue;
    if (!(first_visible || second_visible))
      continue;
    const auto bond_vector = positions[second] - positions[first];
    const auto bond_length = scene::length(bond_vector);
    if (bond_length <= 1.0e-12)
      continue;
    if (hide_long.value()) {
      const auto cutoff = 0.9 *
                          (request.atom_visuals[first].sphere_radius +
                           request.atom_visuals[second].sphere_radius) *
                          unit_scale;
      if (bond_length > cutoff)
        continue;
    }
    auto order = valence_order(bond.order);
    const auto aromatic = bond.order == model::BondOrder::aromatic;
    const auto zero_order = order == 0U;
    if (zero_order && zero_mode.value() == 0)
      continue;
    if (zero_order && zero_mode.value() == 2)
      order = 1U;
    if (!valence.value() || order <= 1U)
      order = 1U;
    const auto midpoint = (positions[first] + positions[second]) * 0.5;
    const auto pick_id = next_pick_id(
        packet, PickTarget{PickKind::bond, request.scene_node_id, std::nullopt,
                           bond_index, std::nullopt});
    const auto [perpendicular, valence_fallback] = valence_perpendicular(
        request, positions, bond_index, bond_vector * (1.0 / bond_length),
        valence_mode.value());
    if (valence_fallback && order > 1U)
      ++valence_fallback_count;
    double first_radius{};
    double second_radius{};
    double spacing{};
    double stick_valence_scale{1.0};
    double valence_fraction{};
    float first_width{};
    float second_width{};
    ColorRgba first_color = request.atom_visuals[first].color;
    ColorRgba second_color = request.atom_visuals[second].color;
    if (request.style.kind == RepresentationKind::lines) {
      const auto first_resolved_width = setting_value(
          request, "line_width", first_context,
          static_cast<double>(request.style.line_width_pixels));
      const auto second_resolved_width = setting_value(
          request, "line_width", second_context,
          static_cast<double>(request.style.line_width_pixels));
      const auto first_resolved_radius =
          setting_value(request, "line_radius", first_context, 0.0);
      const auto second_resolved_radius =
          setting_value(request, "line_radius", second_context, 0.0);
      const auto valence_size =
          setting_value(request, "valence_size", context, 0.060);
      if (!first_resolved_width.has_value() ||
          !second_resolved_width.has_value() ||
          !first_resolved_radius.has_value() ||
          !second_resolved_radius.has_value() || !valence_size.has_value()) {
        const auto* error = !first_resolved_width.has_value()
                                ? &first_resolved_width.error()
                            : !second_resolved_width.has_value()
                                ? &second_resolved_width.error()
                            : !first_resolved_radius.has_value()
                                ? &first_resolved_radius.error()
                            : !second_resolved_radius.has_value()
                                ? &second_resolved_radius.error()
                                : &valence_size.error();
        return operation::Result<RenderPacket>::failure(*error);
      }
      first_width = static_cast<float>(first_resolved_width.value());
      second_width = static_cast<float>(second_resolved_width.value());
      first_radius = first_resolved_radius.value() * unit_scale;
      second_radius = second_resolved_radius.value() * unit_scale;
      spacing = valence_size.value() * unit_scale;
      valence_fraction = valence_size.value();
      const auto first_resolved = setting_color(
          request, "line_color", first_context, first_color, 1.0F);
      const auto second_resolved = setting_color(
          request, "line_color", second_context, second_color, 1.0F);
      if (!first_resolved.has_value() || !second_resolved.has_value())
        return operation::Result<RenderPacket>::failure(
            !first_resolved.has_value() ? first_resolved.error()
                                        : second_resolved.error());
      first_color = first_resolved.value();
      second_color = second_resolved.value();
    } else {
      const auto first_resolved_radius = setting_value(
          request, "stick_radius", first_context, request.style.stick_radius);
      const auto second_resolved_radius = setting_value(
          request, "stick_radius", second_context, request.style.stick_radius);
      const auto first_transparency =
          setting_value(request, "stick_transparency", first_context, 0.0);
      const auto second_transparency =
          setting_value(request, "stick_transparency", second_context, 0.0);
      const auto h_scale =
          setting_value(request, "stick_h_scale", context, 0.4);
      const auto valence_scale =
          setting_value(request, "stick_valence_scale", context, 1.0);
      const auto zero_scale =
          setting_value(request, "valence_zero_scale", context, 0.2);
      if (!first_resolved_radius.has_value() ||
          !second_resolved_radius.has_value() ||
          !first_transparency.has_value() ||
          !second_transparency.has_value() ||
          !h_scale.has_value() || !valence_scale.has_value() ||
          !zero_scale.has_value()) {
        const auto* error = !first_resolved_radius.has_value()
                                ? &first_resolved_radius.error()
                            : !second_resolved_radius.has_value()
                                ? &second_resolved_radius.error()
                            : !first_transparency.has_value()
                                ? &first_transparency.error()
                            : !second_transparency.has_value()
                                ? &second_transparency.error()
                            : !h_scale.has_value() ? &h_scale.error()
                            : !valence_scale.has_value()
                                ? &valence_scale.error()
                                : &zero_scale.error();
        return operation::Result<RenderPacket>::failure(*error);
      }
      first_radius = first_resolved_radius.value() * unit_scale;
      second_radius = second_resolved_radius.value() * unit_scale;
      // PyMOL scales the whole bond when either end is a hydrogen.
      if (request.topology->atoms()[first].atomic_number == 1U ||
          request.topology->atoms()[second].atomic_number == 1U) {
        first_radius *= h_scale.value();
        second_radius *= h_scale.value();
      }
      if (zero_order) {
        first_radius *= zero_scale.value();
        second_radius *= zero_scale.value();
      }
      spacing = std::max(first_radius, second_radius) *
                valence_scale.value() * 2.0;
      stick_valence_scale = valence_scale.value();
      const auto first_alpha =
          static_cast<float>(1.0 - first_transparency.value());
      const auto second_alpha =
          static_cast<float>(1.0 - second_transparency.value());
      const auto first_resolved = setting_color(
          request, "stick_color", first_context, first_color, first_alpha);
      const auto second_resolved = setting_color(
          request, "stick_color", second_context, second_color, second_alpha);
      if (!first_resolved.has_value() || !second_resolved.has_value())
        return operation::Result<RenderPacket>::failure(
            !first_resolved.has_value() ? first_resolved.error()
                                        : second_resolved.error());
      first_color = first_resolved.value();
      second_color = second_resolved.value();
    }
    const auto emit = [&](model::Vec3d start, model::Vec3d end,
                          ColorRgba color, double radius, float width) {
      if (request.style.kind == RepresentationKind::lines && radius == 0.0) {
        packet.lines.push_back(
            {start, end, color, color, width, pick_id});
        include(packet.bounds, start);
        include(packet.bounds, end);
      } else {
        packet.cylinders.push_back({start, end, radius, color, pick_id});
        include(packet.bounds, start, radius);
        include(packet.bounds, end, radius);
      }
    };
    const auto emit_half = [&](model::Vec3d start, model::Vec3d end,
                               ColorRgba color, double radius, float width) {
      emit(start, end, color, radius, width);
    };
    // Dashed zero-order bonds follow PyMOL: an even number (>= 2) of equal
    // dashes along the whole bond. Sticks use dashes of 3 and nominal gaps of
    // 6 zero-order radii with gaps also at both ends; lines use 0.15 A dashes
    // and gaps with dashes touching both atoms. Dashes before the midpoint
    // take the first atom's color, later ones the second's; a dash crossing
    // the midpoint is split.
    if (zero_order && zero_mode.value() == 1) {
      const auto sticks = request.style.kind == RepresentationKind::sticks;
      const auto zero_radius = std::max(first_radius, second_radius);
      const auto dash = sticks ? 3.0 * zero_radius : 0.15 * unit_scale;
      const auto nominal_gap = sticks ? 6.0 * zero_radius : 0.15 * unit_scale;
      auto dashes = static_cast<std::size_t>(bond_length / (dash + nominal_gap));
      if (dashes < 2U)
        dashes = 2U;
      else if (dashes % 2U != 0U)
        --dashes;
      const auto remaining = bond_length - static_cast<double>(dashes) * dash;
      const auto gap = sticks ? remaining / static_cast<double>(dashes + 1U)
                              : remaining / static_cast<double>(dashes - 1U);
      const auto unit = bond_vector * (1.0 / bond_length);
      const auto half = bond_length * 0.5;
      for (std::size_t index = 0; index < dashes; ++index) {
        const auto begin = (sticks ? gap : 0.0) +
                           static_cast<double>(index) * (dash + gap);
        const auto finish = begin + dash;
        const auto at = [&](double distance) {
          return positions[first] + unit * distance;
        };
        if (finish <= half) {
          if (first_visible)
            emit(at(begin), at(finish), first_color, first_radius, first_width);
        } else if (begin >= half) {
          if (second_visible)
            emit(at(begin), at(finish), second_color, second_radius, second_width);
        } else {
          if (first_visible)
            emit(at(begin), at(half), first_color, first_radius, first_width);
          if (second_visible)
            emit(at(half), at(finish), second_color, second_radius, second_width);
        }
      }
      continue;
    }
    // Line valence follows PyMOL: with valence_mode 1 a non-terminal multiple
    // bond keeps the full line and adds one inner line toward the plane atom,
    // shortened by valence_size at both ends and offset by 2 (double) or 4
    // (triple) valence sizes; aromatic bonds add inner dashes at 14-40% and
    // 60-86%. Otherwise lines sit at +-valence_size (triple: also center).
    if (request.style.kind == RepresentationKind::lines && order > 1U &&
        valence.value()) {
      const auto has_heavy_neighbor = [&](std::size_t atom, std::size_t other) {
        for (const auto &candidate : request.topology->bonds()) {
          const auto neighbor = candidate.first.value == atom ? candidate.second.value
                                : candidate.second.value == atom
                                    ? candidate.first.value
                                    : atom;
          if (neighbor != atom && neighbor != other &&
              request.topology->atoms()[neighbor].atomic_number > 1U)
            return true;
        }
        return false;
      };
      const auto terminal = !has_heavy_neighbor(first, second) ||
                            !has_heavy_neighbor(second, first);
      const auto fancy = valence_mode.value() == 1 && !terminal;
      const auto emit_split = [&](model::Vec3d start, model::Vec3d end) {
        const auto middle = (start + end) * 0.5;
        if (first_visible)
          emit(start, middle, first_color, first_radius, first_width);
        if (second_visible)
          emit(middle, end, second_color, second_radius, second_width);
      };
      const auto along = positions[second] - positions[first];
      if (aromatic) {
        emit_split(positions[first], positions[second]);
        const auto inward = perpendicular * (2.0 * spacing);
        if (first_visible)
          emit(positions[first] + along * 0.14 + inward,
               positions[first] + along * 0.4 + inward, first_color,
               first_radius, first_width);
        if (second_visible)
          emit(positions[first] + along * 0.6 + inward,
               positions[first] + along * 0.86 + inward, second_color,
               second_radius, second_width);
      } else if (fancy) {
        emit_split(positions[first], positions[second]);
        const auto inward =
            perpendicular * (2.0 * spacing * (order == 3U ? 2.0 : 1.0));
        emit_split(positions[first] + along * valence_fraction + inward,
                   positions[first] + along * (1.0 - valence_fraction) + inward);
      } else {
        const auto side = perpendicular * (spacing * (order == 3U ? 2.0 : 1.0));
        if (order == 3U)
          emit_split(positions[first], positions[second]);
        emit_split(positions[first] - side, positions[second] - side);
        emit_split(positions[first] + side, positions[second] + side);
      }
      continue;
    }
    // Stick valence follows PyMOL: a double bond is two tubes of radius
    // R*scale/2.5 offset by +-1.5 radii, a triple bond three tubes of radius
    // R*scale/3.5 spaced 2.5 radii apart (R = bond stick radius).
    double stroke_scale = 1.0;
    if (request.style.kind == RepresentationKind::sticks && order > 1U) {
      const auto divisor = order == 2U ? 2.5 : 3.5;
      const auto gap = order == 2U ? 3.0 : 2.5;
      stroke_scale = stick_valence_scale / divisor;
      spacing = std::max(first_radius, second_radius) * stroke_scale * gap;
    }
    for (std::size_t stroke = 0; stroke < order; ++stroke) {
      const auto centered = static_cast<double>(stroke) -
                            static_cast<double>(order - 1U) * 0.5;
      const auto offset = perpendicular * (centered * spacing);
      if (first_visible)
        emit_half(positions[first] + offset, midpoint + offset, first_color,
                  first_radius * stroke_scale, first_width);
      if (second_visible)
        emit_half(midpoint + offset, positions[second] + offset, second_color,
                  second_radius * stroke_scale, second_width);
    }
    if (request.style.kind == RepresentationKind::sticks) {
      const auto ratio =
          setting_value(request, "stick_ball_ratio", context, 1.0);
      const auto ball_setting =
          setting_value(request, "stick_ball",
                        setting_context(request, first_id->value), false);
      const auto second_ball_setting =
          setting_value(request, "stick_ball",
                        setting_context(request, second_id->value), false);
      if (!ratio.has_value() || !ball_setting.has_value() ||
          !second_ball_setting.has_value())
        return operation::Result<RenderPacket>::failure(
            !ratio.has_value() ? ratio.error()
            : !ball_setting.has_value() ? ball_setting.error()
                                        : second_ball_setting.error());
      const auto update_ball = [&](std::size_t atom, bool enabled,
                                   bool visible, ColorRgba atomic) ->
          std::optional<operation::Error> {
        if (!enabled || !visible)
          return std::nullopt;
        const auto ball = setting_color(request, "stick_ball_color", context,
                                        atomic, atomic.alpha);
        if (!ball.has_value())
          return ball.error();
        const auto candidate =
            (atom == first ? first_radius : second_radius) * ratio.value();
        if (candidate > ball_radius[atom]) {
          ball_radius[atom] = candidate;
          ball_color[atom] = ball.value();
        }
        return std::nullopt;
      };
      if (const auto error = update_ball(first, ball_setting.value(),
                                         first_visible, first_color);
          error.has_value())
        return operation::Result<RenderPacket>::failure(*error);
      if (const auto error = update_ball(second, second_ball_setting.value(),
                                         second_visible, second_color);
          error.has_value())
        return operation::Result<RenderPacket>::failure(*error);
    }
  }
  for (std::size_t atom = 0; atom < ball_radius.size(); ++atom) {
    if (ball_radius[atom] <= 0.0)
      continue;
    const auto pick_id = next_pick_id(
        packet, PickTarget{PickKind::atom, request.scene_node_id,
                           model::AtomIndex{atom}, std::nullopt, std::nullopt});
    packet.spheres.push_back(
        {positions[atom], ball_radius[atom], ball_color[atom], pick_id});
    include(packet.bounds, positions[atom], ball_radius[atom]);
  }
  packet.provenance.emplace("valence_mode_requested", "resolved-per-bond");
  packet.provenance.emplace("valence_fancy_fallback_count",
                            std::to_string(valence_fallback_count));
  return operation::Result<RenderPacket>::success(std::move(packet));
}

}  // namespace molshredder::render
