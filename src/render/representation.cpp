#include "molshredder/render/representation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
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

  if (request.style.kind == RepresentationKind::spheres) {
    packet.spheres.reserve(atom_count);
    for (std::size_t index = 0; index < atom_count; ++index) {
      if (!request.frame->atom_present(index) ||
          !atom_selected(request, index)) {
        continue;
      }
      const auto radius =
          request.atom_visuals[index].sphere_radius * request.style.sphere_scale;
      const auto pick_id = next_pick_id(
          packet, PickTarget{PickKind::atom, request.scene_node_id,
                             model::AtomIndex{index}, std::nullopt,
                             std::nullopt});
      packet.spheres.push_back(SphereInstance{
          positions[index], radius, request.atom_visuals[index].color, pick_id});
      include(packet.bounds, positions[index], radius);
    }
    return operation::Result<RenderPacket>::success(std::move(packet));
  }

  const auto& bonds = request.topology->bonds();
  if (request.style.kind == RepresentationKind::lines) {
    packet.lines.reserve(bonds.size() * 2U);
  } else {
    packet.cylinders.reserve(bonds.size() * 2U);
  }
  for (std::size_t bond_index = 0; bond_index < bonds.size(); ++bond_index) {
    const auto& bond = bonds[bond_index];
    const auto first = bond.first.value;
    const auto second = bond.second.value;
    if (!request.frame->atom_present(first) ||
        !request.frame->atom_present(second) ||
        !atom_selected(request, first) || !atom_selected(request, second)) {
      continue;
    }
    const auto midpoint = (positions[first] + positions[second]) * 0.5;
    const auto pick_id = next_pick_id(
        packet, PickTarget{PickKind::bond, request.scene_node_id, std::nullopt,
                           bond_index, std::nullopt});
    if (request.style.kind == RepresentationKind::lines) {
      packet.lines.push_back(LineInstance{
          positions[first], midpoint, request.atom_visuals[first].color,
          request.atom_visuals[first].color, request.style.line_width_pixels,
          pick_id});
      packet.lines.push_back(LineInstance{
          midpoint, positions[second], request.atom_visuals[second].color,
          request.atom_visuals[second].color, request.style.line_width_pixels,
          pick_id});
      include(packet.bounds, positions[first]);
      include(packet.bounds, positions[second]);
    } else {
      packet.cylinders.push_back(CylinderInstance{
          positions[first], midpoint, request.style.stick_radius,
          request.atom_visuals[first].color, pick_id});
      packet.cylinders.push_back(CylinderInstance{
          midpoint, positions[second], request.style.stick_radius,
          request.atom_visuals[second].color, pick_id});
      include(packet.bounds, positions[first], request.style.stick_radius);
      include(packet.bounds, positions[second], request.style.stick_radius);
    }
  }
  return operation::Result<RenderPacket>::success(std::move(packet));
}

}  // namespace molshredder::render
