#include "molshredder/render/representation.hpp"

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
  const auto token = std::string_view{value.value()};
  if (token == "-1" || token == "atomic" || token == "atom") {
    color.alpha = alpha;
    return operation::Result<ColorRgba>::success(color);
  }
  constexpr std::array named{
      std::pair<std::string_view, ColorRgba>{"black", {0.F, 0.F, 0.F, 1.F}},
      std::pair<std::string_view, ColorRgba>{"blue", {0.2F, 0.35F, 1.F, 1.F}},
      std::pair<std::string_view, ColorRgba>{"carbon", {0.35F, 0.35F, 0.35F, 1.F}},
      std::pair<std::string_view, ColorRgba>{"cyan", {0.F, 1.F, 1.F, 1.F}},
      std::pair<std::string_view, ColorRgba>{"green", {0.2F, 0.8F, 0.35F, 1.F}},
      std::pair<std::string_view, ColorRgba>{"hydrogen", {0.95F, 0.95F, 0.95F, 1.F}},
      std::pair<std::string_view, ColorRgba>{"magenta", {0.9F, 0.25F, 0.8F, 1.F}},
      std::pair<std::string_view, ColorRgba>{"nitrogen", {0.2F, 0.35F, 0.95F, 1.F}},
      std::pair<std::string_view, ColorRgba>{"orange", {1.F, 0.5F, 0.15F, 1.F}},
      std::pair<std::string_view, ColorRgba>{"oxygen", {0.95F, 0.15F, 0.15F, 1.F}},
      std::pair<std::string_view, ColorRgba>{"red", {0.95F, 0.2F, 0.2F, 1.F}},
      std::pair<std::string_view, ColorRgba>{"sulfur", {0.95F, 0.8F, 0.15F, 1.F}},
      std::pair<std::string_view, ColorRgba>{"white", {1.F, 1.F, 1.F, 1.F}},
      std::pair<std::string_view, ColorRgba>{"yellow", {1.F, 0.85F, 0.2F, 1.F}}};
  const auto found = std::find_if(named.begin(), named.end(),
                                  [token](const auto& item) {
                                    return item.first == token;
                                  });
  if (found != named.end()) {
    color = found->second;
    color.alpha = alpha;
    return operation::Result<ColorRgba>::success(color);
  }
  if (token.size() == 7U && token.front() == '#') {
    unsigned int rgb{};
    std::istringstream input{std::string{token.substr(1U)}};
    if ((input >> std::hex >> rgb) && input.eof()) {
      color = {static_cast<float>((rgb >> 16U) & 0xffU) / 255.F,
               static_cast<float>((rgb >> 8U) & 0xffU) / 255.F,
               static_cast<float>(rgb & 0xffU) / 255.F, alpha};
      return operation::Result<ColorRgba>::success(color);
    }
  }
  return operation::Result<ColorRgba>::failure(
      invalid("unsupported render setting color token: " + value.value(),
              "use atomic, a built-in basic color, or #RRGGBB"));
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
            !request.frame->atom_present(*neighbor))
          continue;
        auto projected = positions[*neighbor] - positions[endpoint];
        projected = projected - direction * scene::dot(projected, direction);
        if (scene::length(projected) > 1.0e-12)
          return {scene::normalized(projected), false};
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

struct BackbonePoint {
  model::ResidueIndex residue;
  model::AtomIndex ca;
  model::Vec3d position;
  std::optional<model::Vec3d> guide;
  ColorRgba color;
  analysis::SecondaryStructureState state{
      analysis::SecondaryStructureState::coil};
};

struct RibbonRing {
  model::Vec3d center;
  model::Vec3d tangent;
  model::Vec3d normal;
  model::Vec3d binormal;
  double half_width{};
  double half_thickness{};
  ColorRgba color;
  model::ResidueIndex residue;
};

model::Vec3d catmull_rom(model::Vec3d p0, model::Vec3d p1,
                         model::Vec3d p2, model::Vec3d p3, double t) {
  using scene::operator+;
  using scene::operator-;
  using scene::operator*;
  const auto t2 = t * t;
  const auto t3 = t2 * t;
  return (p1 * 2.0 + (p2 - p0) * t +
          (p0 * 2.0 + p1 * -5.0 + p2 * 4.0 + p3 * -1.0) * t2 +
          (p0 * -1.0 + p1 * 3.0 + p2 * -3.0 + p3) * t3) * 0.5;
}

model::Vec3d catmull_tangent(model::Vec3d p0, model::Vec3d p1,
                             model::Vec3d p2, model::Vec3d p3, double t) {
  using scene::operator+;
  using scene::operator-;
  using scene::operator*;
  const auto t2 = t * t;
  return scene::normalized(
      ((p2 - p0) +
       (p0 * 2.0 + p1 * -5.0 + p2 * 4.0 + p3 * -1.0) * (2.0 * t) +
       (p0 * -1.0 + p1 * 3.0 + p2 * -3.0 + p3) * (3.0 * t2)) *
      0.5);
}

double profile_width(const RepresentationStyle& style,
                     RepresentationKind kind,
                     analysis::SecondaryStructureState state) noexcept {
  if (kind == RepresentationKind::ribbon) return style.ribbon_width;
  if (is_helix(state)) return style.cartoon_helix_width;
  if (is_sheet(state)) return style.cartoon_sheet_width;
  return style.cartoon_coil_width;
}

operation::Result<RenderPacket> build_backbone_representation(
    const RepresentationRequest& request, const std::vector<model::Vec3d>& positions,
    RenderPacket packet) {
  using scene::operator-;
  using scene::operator+;
  using scene::operator*;
  const auto residue_count = request.topology->residue_count();
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
    calculated_states.reserve(residue_count);
    for (const auto& residue : assigned.value().residues) {
      calculated_states.push_back(residue.state);
    }
    states = calculated_states;
    packet.provenance.emplace("secondary_structure_method",
                              assigned.value().method);
    packet.provenance.emplace("exact_stride_parity", "false");
  } else {
    packet.provenance.emplace("secondary_structure_method",
                              "caller-supplied");
  }

  std::vector<std::optional<model::AtomIndex>> ca(residue_count);
  std::vector<std::optional<model::AtomIndex>> oxygen(residue_count);
  std::vector<std::uint8_t> residue_selected(residue_count, 0U);
  for (std::size_t atom_index = 0; atom_index < request.topology->atom_count();
       ++atom_index) {
    const auto& atom = request.topology->atoms()[atom_index];
    const auto residue = atom.residue.value;
    if (request.frame->atom_present(atom_index) &&
        atom_selected(request, atom_index)) {
      residue_selected[residue] = 1U;
    }
    if (!request.frame->atom_present(atom_index)) continue;
    if (atom.name == "CA" && !ca[residue].has_value()) {
      ca[residue] = model::AtomIndex{atom_index};
    } else if ((atom.name == "O" || atom.name == "OXT") &&
               !oxygen[residue].has_value()) {
      oxygen[residue] = model::AtomIndex{atom_index};
    }
  }

  const auto unit_scale = request.frame->metadata().coordinate_unit ==
                                  operation::LengthUnit::nanometer
                              ? 0.1
                              : 1.0;
  const auto break_distance =
      request.style.cartoon_chain_break_distance * unit_scale;
  std::vector<std::vector<BackbonePoint>> chains;
  for (std::size_t residue = 0; residue < residue_count; ++residue) {
    if (!ca[residue].has_value() || residue_selected[residue] == 0U) {
      if (!chains.empty() && !chains.back().empty()) chains.emplace_back();
      continue;
    }
    const auto ca_index = *ca[residue];
    const auto position = positions[ca_index.value];
    bool starts_chain = chains.empty() || chains.back().empty();
    if (!starts_chain) {
      const auto previous_residue = chains.back().back().residue.value;
      const auto& previous_record = request.topology->residues()[previous_residue];
      const auto& record = request.topology->residues()[residue];
      const auto separation =
          scene::length(position - chains.back().back().position);
      starts_chain = previous_record.chain_id != record.chain_id ||
                     previous_record.segment_id != record.segment_id ||
                     separation <= 1.0e-12 || separation > break_distance;
    }
    if (starts_chain) chains.emplace_back();
    std::optional<model::Vec3d> guide;
    if (oxygen[residue].has_value()) {
      guide = positions[oxygen[residue]->value] - position;
    }
    chains.back().push_back(BackbonePoint{
        model::ResidueIndex{residue}, ca_index, position, guide,
        request.atom_visuals[ca_index.value].color, states[residue]});
  }

  for (const auto& chain : chains) {
    if (chain.size() < 2U) continue;
    std::vector<RibbonRing> rings;
    rings.reserve((chain.size() - 1U) *
                      request.style.backbone_samples_per_residue +
                  1U);
    model::Vec3d previous_normal{};
    bool has_previous_normal = false;
    for (std::size_t span = 0; span + 1U < chain.size(); ++span) {
      const auto& p0 = chain[span == 0U ? 0U : span - 1U];
      const auto& p1 = chain[span];
      const auto& p2 = chain[span + 1U];
      const auto& p3 = chain[span + 2U < chain.size() ? span + 2U
                                                      : chain.size() - 1U];
      const auto sample_count = request.style.backbone_samples_per_residue;
      for (std::size_t sample = 0; sample < sample_count; ++sample) {
        const auto t = static_cast<double>(sample) /
                       static_cast<double>(sample_count);
        auto tangent = catmull_tangent(p0.position, p1.position,
                                       p2.position, p3.position, t);
        if (scene::length(tangent) <= 1.0e-12) {
          tangent = scene::normalized(p2.position - p1.position);
        }
        auto normal = model::Vec3d{};
        const auto guide = t < 0.5 ? p1.guide : p2.guide;
        if (guide.has_value()) {
          normal = *guide - tangent * scene::dot(*guide, tangent);
        }
        if (scene::length(normal) <= 1.0e-12 && has_previous_normal) {
          normal = previous_normal -
                   tangent * scene::dot(previous_normal, tangent);
        }
        if (scene::length(normal) <= 1.0e-12) {
          const model::Vec3d axis = std::abs(tangent.z) < 0.9
                                        ? model::Vec3d{0.0, 0.0, 1.0}
                                        : model::Vec3d{0.0, 1.0, 0.0};
          normal = scene::cross(tangent, axis);
        }
        normal = scene::normalized(normal);
        if (has_previous_normal && scene::dot(normal, previous_normal) < 0.0) {
          normal = normal * -1.0;
        }
        auto width = profile_width(request.style, request.style.kind, p1.state);
        if (request.style.kind == RepresentationKind::cartoon &&
            is_sheet(p1.state) && !is_sheet(p2.state)) {
          const auto arrow = t < 0.65 ? 1.0 + t * (0.7 / 0.65)
                                      : 1.7 - (t - 0.65) * (1.55 / 0.35);
          width *= arrow;
        }
        const auto binormal = scene::normalized(scene::cross(tangent, normal));
        rings.push_back(RibbonRing{
            catmull_rom(p0.position, p1.position, p2.position, p3.position, t),
            tangent, normal, binormal, width * unit_scale * 0.5,
            request.style.cartoon_thickness * unit_scale * 0.5,
            p1.color, p1.residue});
        previous_normal = normal;
        has_previous_normal = true;
      }
    }
    const auto& last = chain.back();
    const auto tangent = rings.back().tangent;
    auto normal = previous_normal - tangent * scene::dot(previous_normal, tangent);
    if (scene::length(normal) <= 1.0e-12) normal = rings.back().normal;
    normal = scene::normalized(normal);
    rings.push_back(RibbonRing{
        last.position, tangent, normal,
        scene::normalized(scene::cross(tangent, normal)),
        profile_width(request.style, request.style.kind, last.state) *
            unit_scale * 0.5,
        request.style.cartoon_thickness * unit_scale * 0.5, last.color,
        last.residue});

    std::vector<std::uint64_t> residue_pick(residue_count, 0U);
    const auto maximum_index =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    if (rings.size() > maximum_index / 4U ||
        packet.mesh_vertices.size() > maximum_index - rings.size() * 4U) {
      return operation::Result<RenderPacket>::failure(
          invalid("backbone mesh exceeds 32-bit index capacity"));
    }
    for (const auto& ring : rings) {
      if (residue_pick[ring.residue.value] == 0U) {
        residue_pick[ring.residue.value] = next_pick_id(
            packet, PickTarget{PickKind::residue, request.scene_node_id,
                               std::nullopt, std::nullopt, ring.residue});
      }
      const std::array<model::Vec3d, 4> offsets{
          ring.normal * ring.half_width + ring.binormal * ring.half_thickness,
          ring.normal * -ring.half_width + ring.binormal * ring.half_thickness,
          ring.normal * -ring.half_width + ring.binormal * -ring.half_thickness,
          ring.normal * ring.half_width + ring.binormal * -ring.half_thickness};
      for (const auto& offset : offsets) {
        packet.mesh_vertices.push_back(
            MeshVertex{ring.center + offset, scene::normalized(offset), ring.color});
        include(packet.bounds, ring.center + offset);
      }
    }
    const auto base = packet.mesh_vertices.size() - rings.size() * 4U;
    for (std::size_t ring = 0; ring + 1U < rings.size(); ++ring) {
      const auto first = base + ring * 4U;
      const auto second = first + 4U;
      const auto pick_id = residue_pick[rings[ring].residue.value];
      for (std::size_t side = 0; side < 4U; ++side) {
        const auto next = (side + 1U) % 4U;
        packet.mesh_triangles.push_back(MeshTriangle{
            static_cast<std::uint32_t>(first + side),
            static_cast<std::uint32_t>(second + side),
            static_cast<std::uint32_t>(second + next), pick_id});
        packet.mesh_triangles.push_back(MeshTriangle{
            static_cast<std::uint32_t>(first + side),
            static_cast<std::uint32_t>(second + next),
            static_cast<std::uint32_t>(first + next), pick_id});
      }
    }
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
      if (request.topology->atoms()[first].atomic_number == 1U)
        first_radius *= h_scale.value();
      if (request.topology->atoms()[second].atomic_number == 1U)
        second_radius *= h_scale.value();
      if (zero_order) {
        first_radius *= zero_scale.value();
        second_radius *= zero_scale.value();
      }
      spacing = std::max(first_radius, second_radius) *
                valence_scale.value() * 2.0;
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
      if (zero_order && zero_mode.value() == 1) {
        constexpr std::size_t dash_count = 4U;
        const auto delta = (end - start) * (1.0 / static_cast<double>(dash_count * 2U));
        for (std::size_t dash = 0; dash < dash_count; ++dash) {
          const auto offset = delta * static_cast<double>(dash * 2U);
          emit(start + offset, start + offset + delta, color, radius, width);
        }
      } else {
        emit(start, end, color, radius, width);
      }
    };
    for (std::size_t stroke = 0; stroke < order; ++stroke) {
      const auto centered = static_cast<double>(stroke) -
                            static_cast<double>(order - 1U) * 0.5;
      const auto offset = perpendicular * (centered * spacing);
      if (first_visible)
        emit_half(positions[first] + offset, midpoint + offset, first_color,
                  first_radius, first_width);
      if (second_visible)
        emit_half(midpoint + offset, positions[second] + offset, second_color,
                  second_radius, second_width);
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
