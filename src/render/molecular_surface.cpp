#include "molshredder/render/molecular_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "molshredder/model/volume.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/render/volume_isosurface.hpp"
#include "molshredder/scene/math.hpp"

namespace molshredder::render {
namespace {

constexpr std::uint32_t kNoOwner = std::numeric_limits<std::uint32_t>::max();

operation::Error invalid(std::string message, std::string remediation = {}) {
  return {operation::ErrorCode::invalid_argument, std::move(message),
          std::move(remediation)};
}

operation::Error exhausted(std::string message, std::string remediation) {
  return {operation::ErrorCode::resource_exhausted, std::move(message),
          std::move(remediation)};
}

operation::Error cancelled() {
  return {operation::ErrorCode::cancelled,
          "molecular surface generation was cancelled",
          "retry the operation when the molecular object is ready"};
}

bool cancelled(const MolecularSurfaceRequest &request) noexcept {
  return request.context != nullptr &&
         request.context->cancellation.is_cancelled();
}

std::vector<model::Vec3d>
positions_as_double(const model::CoordinateFrame &frame) {
  return std::visit(
      [](const auto &values) {
        std::vector<model::Vec3d> result;
        result.reserve(values.size());
        for (const auto &value : values) {
          result.push_back({static_cast<double>(value.x),
                            static_cast<double>(value.y),
                            static_cast<double>(value.z)});
        }
        return result;
      },
      frame.positions().values());
}

bool multiply_overflows(std::size_t left, std::size_t right) noexcept {
  return right != 0U && left > std::numeric_limits<std::size_t>::max() / right;
}

operation::Result<std::size_t> voxel_count(model::VolumeShape shape) {
  if (multiply_overflows(shape.x, shape.y) ||
      multiply_overflows(shape.x * shape.y, shape.z)) {
    return operation::Result<std::size_t>::failure(
        invalid("molecular surface voxel count overflow"));
  }
  return operation::Result<std::size_t>::success(shape.x * shape.y * shape.z);
}

std::size_t clamped_index(double value, std::size_t extent) noexcept {
  if (value <= 0.0)
    return 0U;
  const auto maximum = static_cast<double>(extent - 1U);
  return static_cast<std::size_t>(std::min(value, maximum));
}

std::uint32_t nearest_owner(const model::Vec3d &position,
                            const model::Vec3d &origin, double spacing,
                            model::VolumeShape shape,
                            const std::vector<std::uint32_t> &owners,
                            const std::vector<model::Vec3d> &positions,
                            const std::vector<double> &radii,
                            std::span<const std::uint8_t> selected) {
  const std::array logical{(position.x - origin.x) / spacing,
                           (position.y - origin.y) / spacing,
                           (position.z - origin.z) / spacing};
  const std::array extents{shape.x, shape.y, shape.z};
  std::array<std::array<std::size_t, 2U>, 3U> candidates{};
  for (std::size_t axis = 0; axis < 3U; ++axis) {
    const auto lower = clamped_index(std::floor(logical[axis]), extents[axis]);
    candidates[axis] = {lower, std::min(lower + 1U, extents[axis] - 1U)};
  }
  auto best = kNoOwner;
  auto best_distance = std::numeric_limits<double>::infinity();
  for (const auto x : candidates[0]) {
    for (const auto y : candidates[1]) {
      for (const auto z : candidates[2]) {
        const auto owner = owners[(x * shape.y + y) * shape.z + z];
        if (owner == kNoOwner)
          continue;
        using scene::operator-;
        const auto distance = scene::length(position - positions[owner]) -
                              radii[owner];
        if (distance < best_distance) {
          best = owner;
          best_distance = distance;
        }
      }
    }
  }
  if (best != kNoOwner)
    return best;
  for (std::size_t atom = 0; atom < positions.size(); ++atom) {
    if ((!selected.empty() && selected[atom] == 0U))
      continue;
    using scene::operator-;
    const auto distance = scene::length(position - positions[atom]) - radii[atom];
    if (distance < best_distance) {
      best = static_cast<std::uint32_t>(atom);
      best_distance = distance;
    }
  }
  return best;
}

} // namespace

operation::Result<RenderPacket>
build_molecular_surface(const MolecularSurfaceRequest &request) {
  if (request.topology == nullptr || request.frame == nullptr) {
    return operation::Result<RenderPacket>::failure(
        invalid("molecular surface requires topology and coordinates"));
  }
  const auto atom_count = request.topology->atom_count();
  if (request.frame->atom_count() != atom_count ||
      request.atom_visuals.size() != atom_count ||
      (!request.selected.empty() && request.selected.size() != atom_count)) {
    return operation::Result<RenderPacket>::failure(
        invalid("molecular surface topology, coordinate, visual and selection sizes must match"));
  }
  if (!std::isfinite(request.style.probe_radius_angstrom) ||
      request.style.probe_radius_angstrom < 0.0 ||
      !std::isfinite(request.style.grid_spacing_angstrom) ||
      request.style.grid_spacing_angstrom <= 0.0 ||
      !is_valid(request.style.color) || request.style.voxel_budget == 0U ||
      request.style.memory_budget_bytes == 0U) {
    return operation::Result<RenderPacket>::failure(
        invalid("molecular surface style contains an invalid radius, spacing, color or budget"));
  }
  if (request.style.kind == MolecularSurfaceKind::van_der_waals &&
      request.style.probe_radius_angstrom != 0.0) {
    return operation::Result<RenderPacket>::failure(invalid(
        "van der Waals surface requires a zero probe radius",
        "set --probe-radius 0 or choose solvent-accessible"));
  }
  if (cancelled(request))
    return operation::Result<RenderPacket>::failure(cancelled());

  const auto positions = positions_as_double(*request.frame);
  const auto native_per_angstrom =
      request.frame->metadata().coordinate_unit ==
              operation::LengthUnit::nanometer
          ? 0.1
          : 1.0;
  const auto probe = request.style.probe_radius_angstrom * native_per_angstrom;
  const auto spacing =
      request.style.grid_spacing_angstrom * native_per_angstrom;
  std::vector<double> radii(atom_count);
  model::Vec3d minimum{std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::infinity()};
  model::Vec3d maximum{-std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()};
  std::size_t selected_count{};
  for (std::size_t atom = 0; atom < atom_count; ++atom) {
    radii[atom] =
        request.atom_visuals[atom].sphere_radius * native_per_angstrom + probe;
    if (!std::isfinite(radii[atom]) || radii[atom] <= 0.0) {
      return operation::Result<RenderPacket>::failure(
          invalid("molecular surface atom radii must be finite and positive"));
    }
    if ((!request.selected.empty() && request.selected[atom] == 0U) ||
        !request.frame->atom_present(atom))
      continue;
    ++selected_count;
    const auto padding = radii[atom] + spacing * 2.0;
    minimum.x = std::min(minimum.x, positions[atom].x - padding);
    minimum.y = std::min(minimum.y, positions[atom].y - padding);
    minimum.z = std::min(minimum.z, positions[atom].z - padding);
    maximum.x = std::max(maximum.x, positions[atom].x + padding);
    maximum.y = std::max(maximum.y, positions[atom].y + padding);
    maximum.z = std::max(maximum.z, positions[atom].z + padding);
  }
  if (selected_count == 0U) {
    return operation::Result<RenderPacket>::failure(
        invalid("molecular surface selection contains no present atoms"));
  }
  const auto extent = [spacing](double lower, double upper) {
    return static_cast<std::size_t>(std::ceil((upper - lower) / spacing)) + 1U;
  };
  const model::VolumeShape shape{extent(minimum.x, maximum.x),
                                 extent(minimum.y, maximum.y),
                                 extent(minimum.z, maximum.z)};
  const auto count = voxel_count(shape);
  if (!count.has_value())
    return operation::Result<RenderPacket>::failure(count.error());
  if (count.value() > request.style.voxel_budget) {
    return operation::Result<RenderPacket>::failure(exhausted(
        "molecular surface requires " + std::to_string(count.value()) +
            " voxels but the budget is " +
            std::to_string(request.style.voxel_budget),
        "increase --voxel-budget or use a coarser --grid-spacing"));
  }
  constexpr auto kBytesPerFieldVoxel = sizeof(float) + sizeof(std::uint32_t);
  if (multiply_overflows(count.value(), kBytesPerFieldVoxel) ||
      count.value() * kBytesPerFieldVoxel >
          request.style.memory_budget_bytes) {
    return operation::Result<RenderPacket>::failure(exhausted(
        "molecular surface field exceeds the memory budget",
        "increase --memory-budget-bytes or use a coarser --grid-spacing"));
  }

  std::vector<float> field(count.value(), static_cast<float>(spacing * 2.0));
  std::vector<std::uint32_t> owners(count.value(), kNoOwner);
  const auto linear_index = [shape](std::size_t x, std::size_t y,
                                    std::size_t z) {
    return (x * shape.y + y) * shape.z + z;
  };
  for (std::size_t atom = 0; atom < atom_count; ++atom) {
    if ((!request.selected.empty() && request.selected[atom] == 0U) ||
        !request.frame->atom_present(atom))
      continue;
    if (cancelled(request))
      return operation::Result<RenderPacket>::failure(cancelled());
    if (request.context != nullptr && request.context->report_progress) {
      request.context->report_progress(
          {0.65 * static_cast<double>(atom) / static_cast<double>(atom_count),
           "molecular-surface-field"});
    }
    const auto cutoff = radii[atom] + spacing * 2.0;
    const auto lower = [&](double coordinate, double origin_value,
                           std::size_t axis_extent) {
      return clamped_index(std::floor((coordinate - cutoff - origin_value) /
                                      spacing),
                           axis_extent);
    };
    const auto upper = [&](double coordinate, double origin_value,
                           std::size_t axis_extent) {
      return clamped_index(std::ceil((coordinate + cutoff - origin_value) /
                                     spacing),
                           axis_extent);
    };
    const auto x0 = lower(positions[atom].x, minimum.x, shape.x);
    const auto y0 = lower(positions[atom].y, minimum.y, shape.y);
    const auto z0 = lower(positions[atom].z, minimum.z, shape.z);
    const auto x1 = upper(positions[atom].x, minimum.x, shape.x);
    const auto y1 = upper(positions[atom].y, minimum.y, shape.y);
    const auto z1 = upper(positions[atom].z, minimum.z, shape.z);
    for (std::size_t x = x0; x <= x1; ++x) {
      for (std::size_t y = y0; y <= y1; ++y) {
        for (std::size_t z = z0; z <= z1; ++z) {
          const model::Vec3d point{minimum.x + static_cast<double>(x) * spacing,
                                   minimum.y + static_cast<double>(y) * spacing,
                                   minimum.z + static_cast<double>(z) * spacing};
          using scene::operator-;
          const auto distance =
              scene::length(point - positions[atom]) - radii[atom];
          const auto index = linear_index(x, y, z);
          if (distance < static_cast<double>(field[index])) {
            field[index] = static_cast<float>(distance);
            owners[index] = static_cast<std::uint32_t>(atom);
          }
        }
      }
    }
  }

  model::VolumeMetadata metadata;
  metadata.coordinate_unit = request.frame->metadata().coordinate_unit;
  metadata.scalar_unit = request.frame->metadata().coordinate_unit ==
                                 operation::LengthUnit::nanometer
                             ? "nanometer"
                             : "angstrom";
  const auto grid = model::VolumeGrid::create(
      shape, minimum,
      {{{spacing, 0.0, 0.0}, {0.0, spacing, 0.0}, {0.0, 0.0, spacing}}},
      model::VolumeScalarBuffer{std::move(field)}, std::move(metadata));
  if (!grid.has_value())
    return operation::Result<RenderPacket>::failure(grid.error());
  const auto field_memory = count.value() * kBytesPerFieldVoxel;
  IsosurfaceRequest isosurface_request{
      grid.value().get(), request.scene_node_id, {0.0, request.style.color},
      request.context, request.style.memory_budget_bytes - field_memory};
  auto surface = build_isosurface(isosurface_request);
  if (!surface.has_value())
    return surface;

  auto packet = std::move(surface.value());
  packet.topology_version = request.topology->version();
  packet.frame_index = request.frame_index;
  packet.provenance.insert_or_assign("algorithm",
                                     "union-sphere-signed-distance");
  packet.provenance.insert_or_assign("algorithm_version", "1");
  packet.provenance.emplace("surface_kind",
                            std::string{to_string(request.style.kind)});
  packet.provenance.emplace("probe_radius_angstrom",
                            std::to_string(request.style.probe_radius_angstrom));
  packet.provenance.emplace("grid_spacing_angstrom",
                            std::to_string(request.style.grid_spacing_angstrom));
  packet.provenance.emplace("selected_atom_count",
                            std::to_string(selected_count));
  packet.provenance.emplace("voxel_count", std::to_string(count.value()));
  packet.provenance.emplace("voxel_budget",
                            std::to_string(request.style.voxel_budget));
  packet.provenance.emplace("memory_budget_bytes",
                            std::to_string(request.style.memory_budget_bytes));

  std::vector<std::uint32_t> vertex_owners;
  vertex_owners.reserve(packet.mesh_vertices.size());
  for (const auto &vertex : packet.mesh_vertices) {
    vertex_owners.push_back(nearest_owner(
        vertex.position, minimum, spacing, shape, owners, positions, radii,
        request.selected));
  }
  std::map<std::uint32_t, std::uint64_t> pick_ids;
  for (auto &triangle : packet.mesh_triangles) {
    std::array owners_for_triangle{vertex_owners[triangle.first],
                                   vertex_owners[triangle.second],
                                   vertex_owners[triangle.third]};
    auto owner = owners_for_triangle[0];
    if (owners_for_triangle[1] == owners_for_triangle[2])
      owner = owners_for_triangle[1];
    if (owner == kNoOwner)
      continue;
    auto found = pick_ids.find(owner);
    if (found == pick_ids.end()) {
      const auto pick_id =
          static_cast<std::uint64_t>(packet.pick_targets.size()) + 1U;
      packet.pick_targets.emplace(
          pick_id,
          PickTarget{PickKind::atom, request.scene_node_id,
                     model::AtomIndex{owner}});
      found = pick_ids.emplace(owner, pick_id).first;
    }
    triangle.pick_id = found->second;
  }
  if (request.context != nullptr && request.context->report_progress)
    request.context->report_progress({1.0, "molecular-surface"});
  return operation::Result<RenderPacket>::success(std::move(packet));
}

} // namespace molshredder::render
