#include "molshredder/application/workspace.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <map>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "molshredder/operation/error.hpp"
#include "molshredder/application/label_expression.hpp"
#include "molshredder/render/image_png.hpp"
#include "molshredder/render/reference_renderer.hpp"
#include "molshredder/render/spectrum.hpp"
#include "molshredder/selection/evaluator.hpp"
#include "molshredder/selection/expression.hpp"

namespace molshredder::application {
namespace {

static_assert(std::is_move_constructible_v<WorkspaceObject>);

using scene::operator+;
using scene::operator-;
using scene::operator*;

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

operation::Error missing_active() {
  return operation::Error{operation::ErrorCode::not_found,
                          "workspace has no active molecular object",
                          "load a structure first"};
}

std::optional<operation::Error> validate_render_setting_target(
    std::span<const WorkspaceObject> objects,
    const render::RenderSettingScope &scope) {
  if (scope.level == render::RenderSettingScopeLevel::global)
    return std::nullopt;
  const auto found = std::find_if(
      objects.begin(), objects.end(), [&scope](const auto &object) {
        return object.id == scope.object_id;
      });
  if (found == objects.end())
    return invalid("render setting target object does not exist");
  const auto frame_count = found->trajectory.has_value()
                               ? found->trajectory->cache->frame_count()
                               : found->system->coordinates()->frame_count();
  if (scope.level != render::RenderSettingScopeLevel::object &&
      (!frame_count.has_value() || scope.state_index >= *frame_count))
    return invalid("render setting target state does not exist");
  if (scope.level == render::RenderSettingScopeLevel::atom &&
      !found->system->topology()->atom_index(model::AtomId{scope.atom_id})
           .has_value())
    return invalid("render setting target atom does not exist");
  if (scope.level == render::RenderSettingScopeLevel::bond &&
      !found->system->topology()->bond_index(model::BondId{scope.bond_id})
           .has_value())
    return invalid("render setting target bond does not exist");
  return std::nullopt;
}

model::Vec3d coordinate_at(const model::CoordinateFrame &frame,
                           std::size_t index) {
  return std::visit(
      [index](const auto &values) {
        return model::Vec3d{static_cast<double>(values[index].x),
                            static_cast<double>(values[index].y),
                            static_cast<double>(values[index].z)};
      },
      frame.positions().values());
}

std::size_t saturated_add(std::size_t left, std::size_t right) {
  return right > std::numeric_limits<std::size_t>::max() - left
             ? std::numeric_limits<std::size_t>::max()
             : left + right;
}

std::size_t saturated_multiply(std::size_t left, std::size_t right) {
  return left != 0U &&
                 right > std::numeric_limits<std::size_t>::max() / left
             ? std::numeric_limits<std::size_t>::max()
             : left * right;
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
frame_with_coordinate(const model::CoordinateFrame &frame,
                      std::size_t atom_index, model::Vec3d position) {
  if (!scene::is_finite(position) || atom_index >= frame.atom_count())
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        invalid("coordinate edit target must be finite and in range"));
  model::CoordinateBuffer positions = std::visit(
      [&](const auto &source) -> model::CoordinateBuffer {
        using Vector = std::decay_t<decltype(source)>;
        using Scalar = typename Vector::value_type;
        auto values = source;
        using Component = decltype(Scalar{}.x);
        values[atom_index] = Scalar{static_cast<Component>(position.x),
                                    static_cast<Component>(position.y),
                                    static_cast<Component>(position.z)};
        return model::CoordinateBuffer{std::move(values)};
      },
      frame.positions().values());
  if (!positions.all_finite())
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        invalid("coordinate edit overflows the source precision",
                "use a finite position representable by the coordinate source"));
  return model::CoordinateFrame::create(
      std::move(positions), frame.velocities(), frame.presence(),
      frame.metadata());
}

std::size_t coordinate_source_payload_bytes(
    const model::CoordinateSource &source) {
  const auto count = source.frame_count();
  if (!count.has_value()) return std::numeric_limits<std::size_t>::max();
  std::size_t total{};
  for (std::size_t index = 0; index < *count; ++index) {
    const auto frame = source.read_frame(index);
    if (!frame.has_value()) return std::numeric_limits<std::size_t>::max();
    const auto scalar_bytes =
        frame.value()->positions().precision() == model::CoordinatePrecision::float32
            ? sizeof(float)
            : sizeof(double);
    const auto position_bytes = saturated_multiply(
        frame.value()->atom_count(), scalar_bytes * 3U);
    const auto velocity_bytes = frame.value()->velocities().has_value()
                                    ? position_bytes
                                    : 0U;
    total = saturated_add(total, saturated_add(position_bytes, velocity_bytes));
    total = saturated_add(total, frame.value()->presence().size());
  }
  return total;
}

void include_point(model::Vec3d point, model::Vec3d &minimum,
                   model::Vec3d &maximum, bool &empty) {
  if (empty) {
    minimum = point;
    maximum = point;
    empty = false;
    return;
  }
  minimum.x = std::min(minimum.x, point.x);
  minimum.y = std::min(minimum.y, point.y);
  minimum.z = std::min(minimum.z, point.z);
  maximum.x = std::max(maximum.x, point.x);
  maximum.y = std::max(maximum.y, point.y);
  maximum.z = std::max(maximum.z, point.z);
}

std::optional<operation::Error> validate_view_name(std::string_view name) {
  if (name.empty() || name.size() > 128U) {
    return invalid("view name must contain between 1 and 128 bytes",
                   "provide a short descriptive view name");
  }
  if (std::any_of(name.begin(), name.end(), [](char value) {
        return std::iscntrl(static_cast<unsigned char>(value)) != 0;
      })) {
    return invalid("view name must not contain control characters",
                   "use printable characters in the view name");
  }
  return std::nullopt;
}

render::AtomVisual default_visual(const model::AtomRecord &atom) {
  switch (atom.atomic_number) {
  case 1U:
    return {{0.95F, 0.95F, 0.95F, 1.0F}, 1.20};
  case 6U:
    return {{0.35F, 0.35F, 0.35F, 1.0F}, 1.70};
  case 7U:
    return {{0.20F, 0.35F, 0.95F, 1.0F}, 1.55};
  case 8U:
    return {{0.95F, 0.15F, 0.15F, 1.0F}, 1.52};
  case 16U:
    return {{0.95F, 0.80F, 0.15F, 1.0F}, 1.80};
  default:
    return {{0.65F, 0.65F, 0.70F, 1.0F}, 1.60};
  }
}

operation::Result<std::vector<render::AtomVisual>>
atom_visuals(const model::Topology &topology) {
  std::vector<render::AtomVisual> visuals;
  visuals.reserve(topology.atom_count());
  for (const auto &atom : topology.atoms()) {
    visuals.push_back(default_visual(atom));
  }
  const auto *property = topology.properties().find("pqr.radius");
  auto property_name = std::string_view{"pqr.radius"};
  if (property == nullptr) {
    property = topology.properties().find("vdw_radius");
    property_name = "vdw_radius";
  }
  if (property == nullptr) {
    return operation::Result<std::vector<render::AtomVisual>>::success(
        std::move(visuals));
  }
  const auto scale = !property->metadata.unit.has_value() ||
                             property->metadata.unit == "angstrom"
                         ? 1.0
                     : property->metadata.unit == "nanometer" ? 10.0
                                                              : 0.0;
  if (scale == 0.0) {
    return operation::Result<std::vector<render::AtomVisual>>::failure(
        invalid(std::string{property_name} +
                " requires angstrom or nanometer units for rendering"));
  }
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    double radius{};
    if (const auto *double_values =
            std::get_if<std::vector<double>>(&property->values)) {
      radius = (*double_values)[index];
    } else if (const auto *float_values =
                   std::get_if<std::vector<float>>(&property->values)) {
      radius = static_cast<double>((*float_values)[index]);
    } else {
      return operation::Result<std::vector<render::AtomVisual>>::failure(
          invalid(std::string{property_name} +
                  " must be a float32 or float64 property column"));
    }
    radius *= scale;
    if (!std::isfinite(radius) || radius <= 0.0) {
      return operation::Result<std::vector<render::AtomVisual>>::failure(
          invalid(std::string{property_name} +
                  " values must be finite and positive"));
    }
    visuals[index].sphere_radius = radius;
  }
  return operation::Result<std::vector<render::AtomVisual>>::success(
      std::move(visuals));
}

// Text atom fields that spectrum enumerates instead of interpolating.
std::optional<std::string> spectrum_text_field(const model::Topology &topology,
                                               std::size_t atom,
                                               std::string_view field) {
  const auto &record = topology.atoms()[atom];
  const auto &residue = topology.residues()[record.residue.value];
  if (field == "chain")
    return residue.chain_id;
  if (field == "resn")
    return residue.name;
  if (field == "name")
    return record.name;
  if (field == "elem")
    return selection::atom_element_symbol(record.atomic_number);
  if (field == "segi")
    return residue.segment_id;
  if (field == "alt")
    return record.alternate_location;
  if (field == "ss") {
    // PDB HELIX/SHEET codes; atoms without a record (or files without
    // secondary-structure records) enumerate as "".
    const auto *property = topology.properties().find("ss");
    const auto *codes = property == nullptr
                            ? nullptr
                            : std::get_if<std::vector<std::string>>(&property->values);
    return codes != nullptr && atom < codes->size() ? (*codes)[atom]
                                                    : std::string{};
  }
  if (field == "type") {
    const auto *property = topology.properties().find("pdb.is_hetero");
    const auto *hetero = property == nullptr
                             ? nullptr
                             : std::get_if<model::BooleanColumn>(&property->values);
    return hetero != nullptr && atom < hetero->values.size() &&
                   hetero->values[atom] != 0U
               ? std::string{"HETATM"}
               : std::string{"ATOM"};
  }
  if (field == "text_type")
    return std::string{};
  return std::nullopt;
}

// Per-atom spectrum values for every atom (nullopt = missing). Supports
// count (1-based or 0-based selection position), residue number, rank,
// index, ID, text enumeration and the selection numeric grammar.
operation::Result<std::vector<std::optional<double>>>
spectrum_values(const WorkspaceObject &object, const model::CoordinateFrame &frame,
                std::size_t frame_index, std::span<const std::size_t> selected,
                std::string_view expression, bool palette_path) {
  using R = operation::Result<std::vector<std::optional<double>>>;
  const auto &topology = *object.system->topology();
  std::vector<std::optional<double>> values(topology.atom_count());
  if (expression == "count") {
    for (std::size_t position = 0; position < selected.size(); ++position)
      values[selected[position]] =
          static_cast<double>(position + (palette_path ? 1U : 0U));
    return R::success(std::move(values));
  }
  if (expression == "resi" || expression == "resv" || expression == "rank" ||
      expression == "index" || expression == "ID") {
    for (const auto atom : selected) {
      const auto &record = topology.atoms()[atom];
      if (expression == "resi" || expression == "resv")
        values[atom] = static_cast<double>(
            topology.residues()[record.residue.value].sequence_number);
      else if (expression == "rank")
        values[atom] = static_cast<double>(atom);
      else if (expression == "index")
        values[atom] = static_cast<double>(atom + 1U);
      else if (record.source_serial.has_value())
        values[atom] = static_cast<double>(*record.source_serial);
    }
    return R::success(std::move(values));
  }
  if (expression == "partial_charge" &&
      topology.properties().find("partial_charge") == nullptr &&
      !frame.metadata().atom_properties.contains("partial_charge")) {
    // PyMOL atoms carry partial_charge 0 when the file has none.
    for (const auto atom : selected)
      values[atom] = 0.0;
    return R::success(std::move(values));
  }
  if (expression == "elec_radius" || expression == "numeric_type") {
    // Not modeled by MolShredder; PyMOL defaults are 0 for loaded files.
    for (const auto atom : selected)
      values[atom] = 0.0;
    return R::success(std::move(values));
  }
  if (expression == "cartoon") {
    for (const auto atom : selected)
      values[atom] = 0.0;
    if (object.cartoon_types != nullptr) {
      for (const auto &[id, code] : *object.cartoon_types) {
        const auto index = topology.atom_index(model::AtomId{id});
        if (index.has_value() && values[index->value].has_value())
          values[index->value] = static_cast<double>(code);
      }
    }
    return R::success(std::move(values));
  }
  if (expression == "chain" || expression == "resn" || expression == "name" ||
      expression == "elem" || expression == "segi" || expression == "alt" ||
      expression == "ss" || expression == "type" || expression == "text_type") {
    std::vector<std::string> text(topology.atom_count());
    for (const auto atom : selected)
      text[atom] = *spectrum_text_field(topology, atom, expression);
    std::vector<std::string> order;
    if (palette_path) {
      // First-appearance enumeration in selection order.
      for (const auto atom : selected)
        if (std::find(order.begin(), order.end(), text[atom]) == order.end())
          order.push_back(text[atom]);
    } else {
      for (const auto atom : selected)
        order.push_back(text[atom]);
      std::sort(order.begin(), order.end());
      order.erase(std::unique(order.begin(), order.end()), order.end());
    }
    for (const auto atom : selected)
      values[atom] = static_cast<double>(
          std::find(order.begin(), order.end(), text[atom]) - order.begin());
    return R::success(std::move(values));
  }
  if (expression == "vdw") {
    const auto radii = selection::atom_vdw_radii(topology);
    if (!radii.has_value())
      return R::failure(radii.error());
    // PyMOL stores vdw as float32.
    for (const auto atom : selected)
      values[atom] = static_cast<double>(static_cast<float>(radii.value()[atom]));
    return R::success(std::move(values));
  }
  if (palette_path && expression != "b" && expression != "q" &&
      expression != "pc" && expression != "partial_charge" &&
      expression != "formal_charge")
    return R::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "Unknown expression: " + std::string{expression},
        "use count, b, q, pc, partial_charge, formal_charge, vdw, resi, resv, "
        "index, rank, ID, chain, resn, name, elem, segi, alt, ss, type, "
        "text_type, elec_radius, numeric_type or cartoon with a built-in "
        "palette"});
  const auto parsed = selection::parse_numeric_expression(
      expression == "fc" ? std::string_view{"formal_charge"} : expression);
  if (!parsed.has_value())
    return R::failure(parsed.error());
  selection::EvaluationContext context{&frame, frame_index,
                                       object.system->coordinates().get(),
                                       object.system->name()};
  context.float32_field_values = true;
  auto evaluated =
      selection::evaluate_numeric(*parsed.value(), topology, context);
  if (!evaluated.has_value())
    return R::failure(evaluated.error());
  for (const auto atom : selected)
    values[atom] = evaluated.value()[atom];
  return R::success(std::move(values));
}

std::optional<operation::Error>
validate_setting_color(const render::RenderSettingValue &value,
                       const render::ColorTable &colors) {
  const auto *token = std::get_if<std::string>(&value);
  if (token == nullptr || *token == "-1")
    return std::nullopt;
  const auto index = colors.resolve(*token);
  if (!index.has_value())
    return invalid("unsupported render setting color token: " + *token,
                   "use atomic, object, a color name or index, 0xRRGGBB or "
                   "#RRGGBB");
  return std::nullopt;
}

// Builds contour geometry from the map isosurface and applies carve/buffer.
operation::Result<render::RenderPacket>
build_contour_packet(const WorkspaceVolume &volume, const ContourRecord &record,
                     const render::ColorTable &colors,
                     operation::TaskContext &context) {
  using R = operation::Result<render::RenderPacket>;
  const auto rgba = record.color == render::kColorDefault
                        ? render::ColorRgba{0.2F, 0.65F, 1.0F, 1.0F}
                        : colors.rgba(record.color, 1.0F)
                              .value_or(render::ColorRgba{0.2F, 0.65F, 1.0F, 1.0F});
  // Contours tessellate like PyMOL (marching cubes, SEQ-311 S14).
  render::IsosurfaceRequest request{volume.grid.get(), volume.scene_node.value,
                                    {static_cast<double>(record.level), rgba},
                                    &context};
  request.tessellation = render::IsosurfaceTessellation::marching_cubes;
  request.plane_edges = record.mode == ContourMode::mesh;
  auto surface = render::build_isosurface(request);
  if (!surface.has_value())
    return surface;
  auto &mesh = surface.value();
  const auto keep = [&](model::Vec3d point) {
    if (record.region_atoms.empty())
      return true;
    if (record.carve > 0.0) {
      const auto limit = record.carve * record.carve;
      return std::any_of(record.region_atoms.begin(), record.region_atoms.end(),
                         [&](model::Vec3d atom) {
                           const auto dx = point.x - atom.x;
                           const auto dy = point.y - atom.y;
                           const auto dz = point.z - atom.z;
                           return dx * dx + dy * dy + dz * dz <= limit;
                         });
    }
    // Grid-plane vertices sit exactly on the box faces; tolerate float32 noise.
    constexpr double kBoxEpsilon = 1.0e-4;
    return point.x >= record.extent.minimum.x - kBoxEpsilon &&
           point.x <= record.extent.maximum.x + kBoxEpsilon &&
           point.y >= record.extent.minimum.y - kBoxEpsilon &&
           point.y <= record.extent.maximum.y + kBoxEpsilon &&
           point.z >= record.extent.minimum.z - kBoxEpsilon &&
           point.z <= record.extent.maximum.z + kBoxEpsilon;
  };
  render::RenderPacket packet;
  packet.scene_node_id = mesh.scene_node_id;
  packet.provenance = mesh.provenance;
  packet.provenance.insert_or_assign(
      "contour_mode", record.mode == ContourMode::surface ? "surface"
                      : record.mode == ContourMode::mesh  ? "mesh"
                                                          : "dot");
  std::vector<std::uint8_t> kept(mesh.mesh_triangles.size(), 0U);
  for (std::size_t index = 0; index < mesh.mesh_triangles.size(); ++index) {
    const auto &triangle = mesh.mesh_triangles[index];
    // PyMOL drops a triangle unless all three vertices are inside the
    // carve distance or buffered box.
    kept[index] = keep(mesh.mesh_vertices[triangle.first].position) &&
                          keep(mesh.mesh_vertices[triangle.second].position) &&
                          keep(mesh.mesh_vertices[triangle.third].position)
                      ? 1U
                      : 0U;
  }
  if (record.mode == ContourMode::surface) {
    packet.mesh_vertices = mesh.mesh_vertices;
    for (std::size_t index = 0; index < kept.size(); ++index)
      if (kept[index] != 0U)
        packet.mesh_triangles.push_back(mesh.mesh_triangles[index]);
    for (const auto &triangle : packet.mesh_triangles)
      for (const auto vertex : {triangle.first, triangle.second, triangle.third})
        render::include(packet.bounds, packet.mesh_vertices[vertex].position);
    return R::success(std::move(packet));
  }
  if (record.mode == ContourMode::mesh) {
    // PyMOL isomesh: the contour lines in each grid plane.
    for (const auto &line : mesh.lines) {
      if (!keep(line.start) || !keep(line.end))
        continue;
      packet.lines.push_back({line.start, line.end, rgba, rgba, 1.0F, 0U});
      render::include(packet.bounds, line.start);
      render::include(packet.bounds, line.end);
    }
    return R::success(std::move(packet));
  }
  // Dots: one small sphere per kept vertex (PyMOL isodot is an explicit
  // fallback approximation; dot spacing follows the triangulation).
  std::vector<std::uint8_t> used(mesh.mesh_vertices.size(), 0U);
  for (std::size_t index = 0; index < kept.size(); ++index)
    if (kept[index] != 0U)
      for (const auto vertex : {mesh.mesh_triangles[index].first,
                                mesh.mesh_triangles[index].second,
                                mesh.mesh_triangles[index].third})
        used[vertex] = 1U;
  for (std::size_t vertex = 0; vertex < used.size(); ++vertex)
    if (used[vertex] != 0U) {
      packet.spheres.push_back({mesh.mesh_vertices[vertex].position, 0.05, rgba, 0U});
      render::include(packet.bounds, mesh.mesh_vertices[vertex].position, 0.05);
    }
  return R::success(std::move(packet));
}

render::Bounds3d grid_extent(const model::VolumeGrid &grid) {
  render::Bounds3d bounds;
  const auto shape = grid.shape();
  for (const auto x : {std::size_t{0}, shape.x - 1U})
    for (const auto y : {std::size_t{0}, shape.y - 1U})
      for (const auto z : {std::size_t{0}, shape.z - 1U})
        render::include(bounds, grid.position(x, y, z));
  return bounds;
}

// PyMOL weighted extent half size: per axis the larger distance from the
// mean center to the bounding box, maximized over axes.
double weighted_half_extent(const SpatialExtent &extent) {
  return std::max({std::max(extent.center.x - extent.minimum.x,
                            extent.maximum.x - extent.center.x),
                   std::max(extent.center.y - extent.minimum.y,
                            extent.maximum.y - extent.center.y),
                   std::max(extent.center.z - extent.minimum.z,
                            extent.maximum.z - extent.center.z)});
}

// Applies explicit atom color indices on top of the default palette. Entries
// whose atom no longer exists in the topology are ignored.
void apply_atom_colors(std::vector<render::AtomVisual> &visuals,
                       const WorkspaceObject &object,
                       const model::Topology &topology,
                       const render::ColorTable &colors,
                       const model::CoordinateFrame *frame = nullptr) {
  if (object.atom_colors == nullptr)
    return;
  for (const auto &[atom_id, index] : *object.atom_colors) {
    const auto atom = topology.atom_index(model::AtomId{atom_id});
    if (!atom.has_value() || atom->value >= visuals.size())
      continue;
    if (index <= render::kColorRampFirst) {
      // Ramp colors are evaluated at the atom position of this frame.
      if (frame == nullptr || !frame->atom_present(atom->value))
        continue;
      const auto position = std::visit(
          [&](const auto &values) {
            return model::Vec3d{static_cast<double>(values[atom->value].x),
                                static_cast<double>(values[atom->value].y),
                                static_cast<double>(values[atom->value].z)};
          },
          frame->positions().values());
      if (const auto rgb = colors.ramp_rgb(index, position); rgb.has_value())
        visuals[atom->value].color = {rgb->red, rgb->green, rgb->blue, 1.0F};
      continue;
    }
    const auto resolved =
        index == render::kColorObject ? object.object_color : index;
    if (const auto rgba = colors.rgba(resolved, 1.0F); rgba.has_value())
      visuals[atom->value].color = *rgba;
  }
}

operation::Result<std::vector<RepresentationRecord>> rebuild_representations(
    const WorkspaceObject &object,
    const std::shared_ptr<const model::MolecularSystem> &system,
    const model::CoordinateFrame &frame, std::size_t frame_index,
    const render::RenderSettingStore &settings,
    const render::ColorTable &colors,
    const RepresentationVisibilityState *visibility_override = nullptr) {
  auto visuals = atom_visuals(*system->topology());
  if (!visuals.has_value()) {
    return operation::Result<std::vector<RepresentationRecord>>::failure(
        visuals.error());
  }
  apply_atom_colors(visuals.value(), object, *system->topology(), colors,
                    &frame);
  std::vector<RepresentationRecord> rebuilt;
  constexpr std::array kinds{render::RepresentationKind::lines,
                             render::RepresentationKind::sticks,
                             render::RepresentationKind::spheres,
                             render::RepresentationKind::ribbon,
                             render::RepresentationKind::cartoon,
                             render::RepresentationKind::labels};
  rebuilt.reserve(kinds.size());
  const auto &visibility = visibility_override == nullptr
                               ? object.representation_visibility
                               : *visibility_override;
  for (const auto kind : kinds) {
    const auto mask = visibility.selection_mask(kind);
    if (!mask.has_value()) {
      return operation::Result<std::vector<RepresentationRecord>>::failure(
          mask.error());
    }
    if (std::none_of(mask.value().begin(), mask.value().end(),
                     [](std::uint8_t value) { return value != 0U; }))
      continue;
    render::RepresentationRequest request{system->topology().get(),
                                          &frame,
                                          frame_index,
                                          object.scene_node.value,
                                          visuals.value(),
                                          mask.value(),
                                          {},
                                          {},
                                          &settings,
                                          object.id,
                                          {},
                                          &colors,
                                          object.object_color,
                                          object.atom_labels.get(),
                                          object.cartoon_types.get()};
    request.style.kind = kind;
    const auto packet = render::build_representation(request);
    if (!packet.has_value()) {
      return operation::Result<std::vector<RepresentationRecord>>::failure(
          packet.error());
    }
    rebuilt.push_back(
        RepresentationRecord{kind, "<visibility-state>", packet.value()});
  }
  return operation::Result<std::vector<RepresentationRecord>>::success(
      std::move(rebuilt));
}

TrajectoryFrameResult frame_result(
    std::uint64_t object_id, const trajectory::PlaybackTimeline &timeline,
    const trajectory::PlaybackClock &clock, const model::CoordinateFrame &frame,
    std::size_t rebuilt_count, std::size_t transitions = 0U,
    std::size_t boundary_crossings = 0U, bool catch_up_limited = false,
    trajectory::PrefetchSnapshot prefetch = {}) {
  TrajectoryFrameResult result;
  result.object_id = object_id;
  result.playback = timeline.snapshot();
  result.source_step = frame.metadata().source_step;
  if (frame.metadata().physical_time.has_value()) {
    result.physical_time = frame.metadata().physical_time->value;
    result.physical_time_unit =
        frame.metadata().physical_time->unit == model::TimeUnit::picosecond
            ? "picosecond"
            : "femtosecond";
  }
  result.rebuilt_representation_count = rebuilt_count;
  result.transitions = transitions;
  result.boundary_crossings = boundary_crossings;
  result.frames_per_second = clock.frames_per_second();
  result.pending_transitions = clock.pending_transitions();
  result.catch_up_limited = catch_up_limited;
  result.prefetch = std::move(prefetch);
  return result;
}

trajectory::PrefetchSnapshot schedule_prefetch(TrajectoryState &state) {
  static_cast<void>(state.prefetch->schedule(
      state.timeline.prefetch_hint(state.prefetch_frame_count)));
  return state.prefetch->snapshot();
}

operation::Result<std::vector<std::uint8_t>>
selection_mask(const WorkspaceObject &object, std::string_view expression) {
  const auto parsed = selection::Expression::parse(expression);
  if (!parsed.has_value()) {
    return operation::Result<std::vector<std::uint8_t>>::failure(
        parsed.error());
  }
  const auto frame_index = object.trajectory.has_value()
                               ? object.trajectory->timeline.snapshot().frame
                               : 0U;
  const auto frame = object.system->coordinates()->read_frame(frame_index);
  if (!frame.has_value()) {
    return operation::Result<std::vector<std::uint8_t>>::failure(frame.error());
  }
  const selection::EvaluationContext selection_context{
      frame.value().get(), frame_index, object.system->coordinates().get(),
      object.system->name()};
  return selection::evaluate(
      parsed.value(), *object.system->topology(), [&](std::string_view name) {
        return object.selections.evaluate(name, *object.system->topology(),
                                          selection_context);
      },
      selection_context);
}

operation::Result<model::AtomIndex> one_atom(const WorkspaceObject &object,
                                             std::string_view expression) {
  const auto mask = selection_mask(object, expression);
  if (!mask.has_value()) {
    return operation::Result<model::AtomIndex>::failure(mask.error());
  }
  std::optional<std::size_t> selected;
  for (std::size_t index = 0; index < mask.value().size(); ++index) {
    if (mask.value()[index] == 0U)
      continue;
    if (selected.has_value()) {
      return operation::Result<model::AtomIndex>::failure(
          operation::Error{operation::ErrorCode::invalid_selection,
                           "analysis atom selection selects multiple atoms",
                           "use an expression selecting exactly one atom"});
    }
    selected = index;
  }
  if (!selected.has_value()) {
    return operation::Result<model::AtomIndex>::failure(
        operation::Error{operation::ErrorCode::invalid_selection,
                         "analysis atom selection selects no atoms",
                         "use an expression selecting exactly one atom"});
  }
  return operation::Result<model::AtomIndex>::success(
      model::AtomIndex{*selected});
}

struct ResolvedWeights {
  std::vector<double> values;
  AnalysisWeightProvenance provenance;
};

operation::Result<ResolvedWeights>
resolve_weights(const model::Topology &topology, analysis::WeightMode mode) {
  if (mode == analysis::WeightMode::uniform) {
    return operation::Result<ResolvedWeights>::success(ResolvedWeights{
        {}, {analysis::WeightMode::uniform, "uniform", {}, false}});
  }
  auto masses = analysis::masses_from_property(topology);
  if (!masses.has_value() &&
      masses.error().code == operation::ErrorCode::not_found) {
    masses = analysis::estimated_element_masses(topology);
  }
  if (!masses.has_value()) {
    return operation::Result<ResolvedWeights>::failure(masses.error());
  }
  return operation::Result<ResolvedWeights>::success(
      ResolvedWeights{std::move(masses.value().values),
                      {analysis::WeightMode::mass, masses.value().source,
                       masses.value().unit, masses.value().estimated}});
}

struct CameraFrameRange {
  std::size_t first{};
  std::size_t count{1U};
};

operation::Result<CameraFrameRange>
resolve_camera_frame_range(const WorkspaceObject &object,
                           CameraStateScope state_scope) {
  const auto frame_count = object.system->coordinates()->frame_count();
  CameraFrameRange range;
  switch (state_scope.kind) {
  case CameraStateScopeKind::current:
    range.first = object.trajectory.has_value()
                      ? object.trajectory->timeline.snapshot().frame
                      : 0U;
    break;
  case CameraStateScopeKind::all:
    if (!frame_count.has_value()) {
      return operation::Result<CameraFrameRange>::failure(operation::Error{
          operation::ErrorCode::unsupported,
          "all-state camera operation requires a known frame count",
          "use state=current or a one-based explicit state"});
    }
    range.count = *frame_count;
    break;
  case CameraStateScopeKind::explicit_state:
    range.first = state_scope.frame_index;
    break;
  }
  if (range.count == 0U) {
    return operation::Result<CameraFrameRange>::failure(operation::Error{
        operation::ErrorCode::invalid_selection,
        "camera state scope contains no coordinate frames",
        "load coordinates or choose a state containing coordinates"});
  }
  if (frame_count.has_value() && range.first >= *frame_count) {
    return operation::Result<CameraFrameRange>::failure(invalid(
        "camera state is outside the available frame range",
        "choose a one-based state between 1 and " +
            std::to_string(*frame_count)));
  }
  return operation::Result<CameraFrameRange>::success(range);
}

operation::Result<SpatialExtent> object_selection_extent(
    const WorkspaceObject &object, std::string_view selection_expression,
    CameraStateScope state_scope, operation::TaskContext *context) {
  const auto mask = selection_mask(object, selection_expression);
  if (!mask.has_value())
    return operation::Result<SpatialExtent>::failure(mask.error());
  const auto frame_range = resolve_camera_frame_range(object, state_scope);
  if (!frame_range.has_value())
    return operation::Result<SpatialExtent>::failure(frame_range.error());
  SpatialExtent extent;
  extent.evaluated_frame_count = frame_range.value().count;
  extent.selected_atom_count = static_cast<std::size_t>(std::count_if(
      mask.value().begin(), mask.value().end(),
      [](std::uint8_t selected) { return selected != 0U; }));
  bool empty = true;
  for (std::size_t frame_offset = 0; frame_offset < frame_range.value().count;
       ++frame_offset) {
    if (context != nullptr && context->cancellation.is_cancelled()) {
      return operation::Result<SpatialExtent>::failure(operation::Error{
          operation::ErrorCode::cancelled,
          "object origin extent cancelled after " +
              std::to_string(frame_offset) + " of " +
              std::to_string(frame_range.value().count) + " frames",
          {}});
    }
    const auto frame = object.system->coordinates()->read_frame(
        frame_range.value().first + frame_offset);
    if (!frame.has_value())
      return operation::Result<SpatialExtent>::failure(frame.error());
    for (std::size_t index = 0; index < mask.value().size(); ++index) {
      if (mask.value()[index] == 0U)
        continue;
      if (!frame.value()->atom_present(index)) {
        ++extent.skipped_missing_atom_count;
        continue;
      }
      include_point(coordinate_at(*frame.value(), index), extent.minimum,
                    extent.maximum, empty);
      ++extent.used_atom_count;
    }
    if (context != nullptr && context->report_progress) {
      context->report_progress(operation::ProgressUpdate{
          0.5 * static_cast<double>(frame_offset + 1U) /
              static_cast<double>(frame_range.value().count),
          "object origin bounds"});
    }
  }
  if (empty) {
    return operation::Result<SpatialExtent>::failure(operation::Error{
        operation::ErrorCode::invalid_selection,
        "object origin selection contains no present coordinates",
        "choose a non-empty selection and state scope"});
  }
  extent.center = (extent.minimum + extent.maximum) * 0.5;
  for (std::size_t frame_offset = 0; frame_offset < frame_range.value().count;
       ++frame_offset) {
    if (context != nullptr && context->cancellation.is_cancelled()) {
      return operation::Result<SpatialExtent>::failure(operation::Error{
          operation::ErrorCode::cancelled,
          "object origin radius cancelled after " +
              std::to_string(frame_offset) + " of " +
              std::to_string(frame_range.value().count) + " frames",
          {}});
    }
    const auto frame = object.system->coordinates()->read_frame(
        frame_range.value().first + frame_offset);
    if (!frame.has_value())
      return operation::Result<SpatialExtent>::failure(frame.error());
    for (std::size_t index = 0; index < mask.value().size(); ++index) {
      if (mask.value()[index] == 0U || !frame.value()->atom_present(index))
        continue;
      extent.maximum_radius =
          std::max(extent.maximum_radius,
                   scene::length(coordinate_at(*frame.value(), index) -
                                 extent.center));
    }
    if (context != nullptr && context->report_progress) {
      context->report_progress(operation::ProgressUpdate{
          0.5 + 0.5 * static_cast<double>(frame_offset + 1U) /
                    static_cast<double>(frame_range.value().count),
          "object origin radius"});
    }
  }
  return operation::Result<SpatialExtent>::success(extent);
}

} // namespace

std::string_view to_string(EditTransactionKind kind) noexcept {
  switch (kind) {
    case EditTransactionKind::atom_position:
      return "atom_position";
    case EditTransactionKind::atom_properties:
      return "atom_properties";
    case EditTransactionKind::residue_properties:
      return "residue_properties";
    case EditTransactionKind::bond_order:
      return "bond_order";
    case EditTransactionKind::molecule_build:
      return "molecule_build";
  }
  return "atom_position";
}

Workspace::Workspace() {
  const auto built = scene::SceneBuilder{}.build();
  if (!built.has_value())
    std::terminate();
  scene_ = built.value();
  auto camera = scene::Camera::create();
  if (!camera.has_value())
    std::terminate();
  camera_ = std::move(camera.value());
  auto settings = render::RenderSettingStore::create();
  if (!settings.has_value())
    std::terminate();
  render_settings_ = std::move(settings.value());
}

void Workspace::record_session_invocation(command::Invocation invocation) {
  session_invocations_.push_back(std::move(invocation));
}

operation::Result<scene::Camera>
Workspace::set_camera(scene::CameraParameters parameters) {
  auto camera = scene::Camera::create(std::move(parameters));
  if (!camera.has_value())
    return operation::Result<scene::Camera>::failure(camera.error());
  camera_ = camera.value();
  return operation::Result<scene::Camera>::success(camera.value());
}

operation::Result<StereoConfigurationResult>
Workspace::set_stereo(scene::StereoParameters parameters) {
  const auto validated = scene::validate_stereo_parameters(parameters);
  if (!validated.has_value())
    return operation::Result<StereoConfigurationResult>::failure(
        validated.error());
  const auto previous = stereo_;
  stereo_ = validated.value();
  return operation::Result<StereoConfigurationResult>::success(
      StereoConfigurationResult{previous, stereo_});
}

operation::Result<NamedViewStoreResult>
Workspace::store_named_view(std::string name) {
  if (const auto error = validate_view_name(name); error.has_value())
    return operation::Result<NamedViewStoreResult>::failure(*error);
  const auto replaced = named_views_.contains(name);
  named_views_.insert_or_assign(name, camera().parameters());
  return operation::Result<NamedViewStoreResult>::success(
      NamedViewStoreResult{{std::move(name), camera().parameters()},
                           named_views_.size(), replaced});
}

operation::Result<NamedViewRecord>
Workspace::recall_named_view(std::string_view name) {
  if (const auto error = validate_view_name(name); error.has_value())
    return operation::Result<NamedViewRecord>::failure(*error);
  const auto found = named_views_.find(name);
  if (found == named_views_.end()) {
    return operation::Result<NamedViewRecord>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "named view does not exist: " + std::string{name},
        "use view list to inspect stored view names"});
  }
  const auto updated = set_camera(found->second);
  if (!updated.has_value())
    return operation::Result<NamedViewRecord>::failure(updated.error());
  return operation::Result<NamedViewRecord>::success(
      NamedViewRecord{found->first, updated.value().parameters()});
}

operation::Result<NamedViewDeleteResult>
Workspace::delete_named_view(std::string_view name) {
  if (const auto error = validate_view_name(name); error.has_value())
    return operation::Result<NamedViewDeleteResult>::failure(*error);
  const auto found = named_views_.find(name);
  if (found == named_views_.end()) {
    return operation::Result<NamedViewDeleteResult>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "named view does not exist: " + std::string{name},
        "use view list to inspect stored view names"});
  }
  named_views_.erase(found);
  return operation::Result<NamedViewDeleteResult>::success(
      NamedViewDeleteResult{std::string{name}, named_views_.size(), false});
}

NamedViewDeleteResult Workspace::clear_named_views() {
  named_views_.clear();
  return NamedViewDeleteResult{"", 0U, true};
}

std::vector<NamedViewRecord> Workspace::list_named_views() const {
  std::vector<NamedViewRecord> result;
  result.reserve(named_views_.size());
  for (const auto &[name, camera] : named_views_)
    result.push_back(NamedViewRecord{name, camera});
  return result;
}

operation::Result<NamedSceneStoreResult>
Workspace::store_named_scene(std::string name) {
  if (const auto error = validate_view_name(name); error.has_value())
    return operation::Result<NamedSceneStoreResult>::failure(*error);
  auto snapshot = std::make_shared<Workspace>(*this);
  snapshot->named_scenes_.clear();
  snapshot->current_scene_name_.reset();
  const auto replaced = named_scenes_.contains(name);
  named_scenes_.insert_or_assign(name, std::move(snapshot));
  current_scene_name_ = name;
  return operation::Result<NamedSceneStoreResult>::success(
      NamedSceneStoreResult{{std::move(name), object_count(), volume_count(),
                             true},
                            named_scenes_.size(), replaced});
}

operation::Result<NamedSceneRecord>
Workspace::recall_named_scene(std::string_view name) {
  if (const auto error = validate_view_name(name); error.has_value())
    return operation::Result<NamedSceneRecord>::failure(*error);
  const auto found = named_scenes_.find(name);
  if (found == named_scenes_.end()) {
    return operation::Result<NamedSceneRecord>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "named scene does not exist: " + std::string{name},
        "use scene list to inspect stored scene names"});
  }
  Workspace candidate = *found->second;
  candidate.named_scenes_ = named_scenes_;
  candidate.current_scene_name_ = found->first;
  candidate.movie_ = movie_;
  candidate.session_invocations_ = session_invocations_;
  *this = std::move(candidate);
  return operation::Result<NamedSceneRecord>::success(
      NamedSceneRecord{std::string{name}, object_count(), volume_count(),
                       true});
}

operation::Result<NamedSceneDeleteResult>
Workspace::delete_named_scene(std::string_view name) {
  if (const auto error = validate_view_name(name); error.has_value())
    return operation::Result<NamedSceneDeleteResult>::failure(*error);
  const auto found = named_scenes_.find(name);
  if (found == named_scenes_.end()) {
    return operation::Result<NamedSceneDeleteResult>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "named scene does not exist: " + std::string{name},
        "use scene list to inspect stored scene names"});
  }
  named_scenes_.erase(found);
  if (current_scene_name_ == name) current_scene_name_.reset();
  return operation::Result<NamedSceneDeleteResult>::success(
      NamedSceneDeleteResult{std::string{name}, named_scenes_.size(), false});
}

NamedSceneDeleteResult Workspace::clear_named_scenes() {
  named_scenes_.clear();
  current_scene_name_.reset();
  return NamedSceneDeleteResult{"", 0U, true};
}

std::vector<NamedSceneRecord> Workspace::list_named_scenes() const {
  std::vector<NamedSceneRecord> result;
  result.reserve(named_scenes_.size());
  for (const auto &[name, snapshot] : named_scenes_) {
    result.push_back(NamedSceneRecord{
        name, snapshot->object_count(), snapshot->volume_count(),
        current_scene_name_.has_value() && *current_scene_name_ == name});
  }
  return result;
}

operation::Result<MovieTimelineResult>
Workspace::configure_movie(std::size_t frame_count, double frames_per_second,
                           bool loop) {
  if (frame_count == 0U || frame_count > 1'000'000U) {
    return operation::Result<MovieTimelineResult>::failure(invalid(
        "movie frame count must be between 1 and 1000000",
        "choose a bounded positive movie length"));
  }
  if (!std::isfinite(frames_per_second) || frames_per_second <= 0.0 ||
      frames_per_second > 240.0) {
    return operation::Result<MovieTimelineResult>::failure(invalid(
        "movie speed must be greater than 0 and at most 240 fps"));
  }
  movie_ = MovieTimelineState{frame_count, 1U, frames_per_second, loop, false,
                              {}};
  return operation::Result<MovieTimelineResult>::success(
      MovieTimelineResult{*movie_, 0U});
}

operation::Result<MovieTimelineResult>
Workspace::set_movie_keyframe(MovieFrameRecord keyframe) {
  if (!movie_.has_value()) {
    return operation::Result<MovieTimelineResult>::failure(operation::Error{
        operation::ErrorCode::not_found, "movie timeline is not configured",
        "run movie configure before adding keyframes"});
  }
  if (keyframe.frame == 0U || keyframe.frame > movie_->frame_count) {
    return operation::Result<MovieTimelineResult>::failure(invalid(
        "movie keyframe is outside the configured frame range"));
  }
  if (!keyframe.scene_name.has_value() &&
      !keyframe.trajectory_frame.has_value()) {
    return operation::Result<MovieTimelineResult>::failure(invalid(
        "movie keyframe requires a scene or trajectory frame"));
  }
  if (keyframe.scene_name.has_value() &&
      !named_scenes_.contains(*keyframe.scene_name)) {
    return operation::Result<MovieTimelineResult>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "movie keyframe scene does not exist: " + *keyframe.scene_name,
        "store the named scene before assigning the movie keyframe"});
  }

  // Validate the composed scene/trajectory key against a detached candidate.
  Workspace candidate = *this;
  if (keyframe.scene_name.has_value()) {
    const auto recalled = candidate.recall_named_scene(*keyframe.scene_name);
    if (!recalled.has_value())
      return operation::Result<MovieTimelineResult>::failure(recalled.error());
  }
  if (keyframe.trajectory_frame.has_value()) {
    const auto selected =
        candidate.set_trajectory_frame(*keyframe.trajectory_frame);
    if (!selected.has_value())
      return operation::Result<MovieTimelineResult>::failure(selected.error());
  }
  movie_->keyframes.insert_or_assign(keyframe.frame, std::move(keyframe));
  return operation::Result<MovieTimelineResult>::success(
      MovieTimelineResult{*movie_, 0U});
}

operation::Result<MovieTimelineResult> Workspace::seek_movie(
    std::size_t frame) {
  if (!movie_.has_value()) {
    return operation::Result<MovieTimelineResult>::failure(operation::Error{
        operation::ErrorCode::not_found, "movie timeline is not configured",
        "run movie configure before seeking"});
  }
  if (frame == 0U || frame > movie_->frame_count) {
    return operation::Result<MovieTimelineResult>::failure(
        invalid("movie frame is outside the configured frame range"));
  }
  Workspace candidate = *this;
  std::size_t applied{};
  const auto keyframe = candidate.movie_->keyframes.find(frame);
  if (keyframe != candidate.movie_->keyframes.end()) {
    if (keyframe->second.scene_name.has_value()) {
      const auto recalled =
          candidate.recall_named_scene(*keyframe->second.scene_name);
      if (!recalled.has_value())
        return operation::Result<MovieTimelineResult>::failure(
            recalled.error());
    }
    if (keyframe->second.trajectory_frame.has_value()) {
      const auto selected = candidate.set_trajectory_frame(
          *keyframe->second.trajectory_frame);
      if (!selected.has_value())
        return operation::Result<MovieTimelineResult>::failure(
            selected.error());
    }
    applied = 1U;
  }
  candidate.movie_->current_frame = frame;
  *this = std::move(candidate);
  return operation::Result<MovieTimelineResult>::success(
      MovieTimelineResult{*movie_, applied});
}

operation::Result<MovieTimelineResult> Workspace::play_movie(bool playing) {
  if (!movie_.has_value()) {
    return operation::Result<MovieTimelineResult>::failure(operation::Error{
        operation::ErrorCode::not_found, "movie timeline is not configured",
        "run movie configure before changing playback"});
  }
  movie_->playing = playing;
  return operation::Result<MovieTimelineResult>::success(
      MovieTimelineResult{*movie_, 0U});
}

operation::Result<MovieTimelineResult> Workspace::step_movie(
    std::size_t steps) {
  if (!movie_.has_value()) {
    return operation::Result<MovieTimelineResult>::failure(operation::Error{
        operation::ErrorCode::not_found, "movie timeline is not configured",
        "run movie configure before stepping"});
  }
  if (steps == 0U)
    return operation::Result<MovieTimelineResult>::failure(
        invalid("movie step count must be positive"));
  std::size_t target{};
  if (movie_->loop) {
    target = ((movie_->current_frame - 1U) +
              (steps % movie_->frame_count)) %
                 movie_->frame_count +
             1U;
  } else {
    const auto remaining = movie_->frame_count - movie_->current_frame;
    target = movie_->current_frame + std::min(steps, remaining);
  }
  auto selected = seek_movie(target);
  if (!selected.has_value()) return selected;
  if (!movie_->loop && target == movie_->frame_count)
    movie_->playing = false;
  selected.value().timeline = *movie_;
  return selected;
}

operation::Result<MovieTimelineResult> Workspace::clear_movie() {
  if (!movie_.has_value()) {
    return operation::Result<MovieTimelineResult>::failure(operation::Error{
        operation::ErrorCode::not_found, "movie timeline is not configured",
        "there is no movie to clear"});
  }
  const auto previous = *movie_;
  movie_.reset();
  return operation::Result<MovieTimelineResult>::success(
      MovieTimelineResult{previous, 0U});
}

WorkspaceObject *Workspace::mutable_active_object() noexcept {
  return active_index_.has_value() ? &objects_[active_index_.value()] : nullptr;
}

const WorkspaceObject *Workspace::active_object() const noexcept {
  return active_index_.has_value() ? &objects_[active_index_.value()] : nullptr;
}

const WorkspaceObject *
Workspace::object_by_scene_node(std::uint64_t scene_node_id) const noexcept {
  const auto found = std::find_if(
      objects_.begin(), objects_.end(), [scene_node_id](const auto &object) {
        return object.scene_node.value == scene_node_id;
      });
  return found == objects_.end() ? nullptr : &*found;
}

operation::Result<std::size_t>
Workspace::object_index_by_reference(std::string_view reference) const {
  if (reference.empty() || reference == "current") {
    if (!active_index_.has_value())
      return operation::Result<std::size_t>::failure(missing_active());
    return operation::Result<std::size_t>::success(*active_index_);
  }

  std::uint64_t requested_id{};
  const auto parsed = molshredder::core::from_chars(reference.data(),
                                      reference.data() + reference.size(),
                                      requested_id);
  if (parsed.ec == std::errc{} &&
      parsed.ptr == reference.data() + reference.size() && requested_id != 0U) {
    const auto found = std::find_if(
        objects_.begin(), objects_.end(), [requested_id](const auto &object) {
          return object.id == requested_id;
        });
    if (found != objects_.end()) {
      return operation::Result<std::size_t>::success(
          static_cast<std::size_t>(found - objects_.begin()));
    }
  } else {
    for (std::size_t index = 0; index < objects_.size(); ++index) {
      const auto *node = scene_->find(objects_[index].scene_node);
      if (node != nullptr && node->name() == reference)
        return operation::Result<std::size_t>::success(index);
    }
  }

  return operation::Result<std::size_t>::failure(operation::Error{
      operation::ErrorCode::not_found,
      "workspace object does not exist: " + std::string{reference},
      "use object list to obtain an object name or ID, or use current"});
}

std::vector<WorkspaceObjectInfo> Workspace::list_objects() const {
  std::vector<WorkspaceObjectInfo> result;
  result.reserve(objects_.size());
  for (std::size_t index = 0; index < objects_.size(); ++index) {
    const auto &object = objects_[index];
    const auto *node = scene_->find(object.scene_node);
    const auto frame_count = object.trajectory.has_value()
                                 ? object.trajectory->cache->frame_count()
                                 : object.system->coordinates()->frame_count();
    result.push_back(WorkspaceObjectInfo{
        object.id, object.scene_node.value, object.system->name(),
        object.system->topology()->atom_count(), object.representations.size(),
        frame_count, active_index_.has_value() && *active_index_ == index,
        node != nullptr && node->visible(),
        scene_->effectively_visible(object.scene_node),
        object.trajectory.has_value()});
  }
  return result;
}

std::vector<WorkspaceVolumeInfo> Workspace::list_volumes() const {
  std::vector<WorkspaceVolumeInfo> result;
  result.reserve(volumes_.size());
  for (std::size_t index = 0; index < volumes_.size(); ++index) {
    const auto &volume = volumes_[index];
    const auto *node = scene_->find(volume.scene_node);
    const auto [minimum, maximum] = volume.grid->scalars().range();
    result.push_back(WorkspaceVolumeInfo{
        volume.id, volume.scene_node.value, volume.name, volume.grid->shape(),
        volume.grid->value_count(), volume.grid->scalars().precision(), minimum,
        maximum, volume.grid->metadata().coordinate_unit,
        volume.representations.size(),
        active_volume_index_.has_value() && *active_volume_index_ == index,
        node != nullptr && node->visible(),
        scene_->effectively_visible(volume.scene_node)});
  }
  return result;
}

operation::Result<WorkspaceObjectInfo>
Workspace::activate_object(std::uint64_t object_id) {
  const auto found = std::find_if(
      objects_.begin(), objects_.end(),
      [object_id](const auto &object) { return object.id == object_id; });
  if (found == objects_.end()) {
    return operation::Result<WorkspaceObjectInfo>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "workspace object does not exist: " + std::to_string(object_id),
        "use object list to obtain a current object ID"});
  }
  const auto index = static_cast<std::size_t>(found - objects_.begin());
  auto scene_builder = scene::SceneBuilder::from(*scene_);
  if (const auto error = scene_builder.set_selection({found->scene_node});
      error.has_value()) {
    return operation::Result<WorkspaceObjectInfo>::failure(*error);
  }
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value()) {
    return operation::Result<WorkspaceObjectInfo>::failure(next_scene.error());
  }
  active_index_ = index;
  scene_ = next_scene.value();
  return operation::Result<WorkspaceObjectInfo>::success(list_objects()[index]);
}

operation::Result<WorkspaceObjectInfo>
Workspace::set_object_visibility(std::uint64_t object_id, bool visible) {
  const auto found = std::find_if(
      objects_.begin(), objects_.end(),
      [object_id](const auto &object) { return object.id == object_id; });
  if (found == objects_.end()) {
    return operation::Result<WorkspaceObjectInfo>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "workspace object does not exist: " + std::to_string(object_id),
        "use object list to obtain a current object ID"});
  }
  const auto index = static_cast<std::size_t>(found - objects_.begin());
  auto scene_builder = scene::SceneBuilder::from(*scene_);
  if (const auto error = scene_builder.set_visible(found->scene_node, visible);
      error.has_value()) {
    return operation::Result<WorkspaceObjectInfo>::failure(*error);
  }
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value()) {
    return operation::Result<WorkspaceObjectInfo>::failure(next_scene.error());
  }
  scene_ = next_scene.value();
  return operation::Result<WorkspaceObjectInfo>::success(list_objects()[index]);
}

operation::Result<ObjectLifecycleResult>
Workspace::rename_object(std::string_view object_reference, std::string name) {
  const auto index_result = object_index_by_reference(object_reference);
  if (!index_result.has_value())
    return operation::Result<ObjectLifecycleResult>::failure(
        index_result.error());
  if (name.empty() ||
      std::all_of(name.begin(), name.end(), [](unsigned char value) {
        return std::isspace(value) != 0;
      }) ||
      std::any_of(name.begin(), name.end(), [](unsigned char value) {
        return value < 0x20U || value == 0x7fU;
      })) {
    return operation::Result<ObjectLifecycleResult>::failure(invalid(
        "workspace object name must contain visible characters and no controls",
        "provide a non-empty unique object name"));
  }
  const auto index = index_result.value();
  const auto &current = objects_[index];
  const auto duplicate_molecule = std::any_of(
      objects_.begin(), objects_.end(), [&](const auto &candidate) {
        return candidate.id != current.id && candidate.system->name() == name;
      });
  const auto duplicate_volume =
      std::any_of(volumes_.begin(), volumes_.end(), [&](const auto &volume) {
        return volume.name == name;
      });
  if (duplicate_molecule || duplicate_volume) {
    return operation::Result<ObjectLifecycleResult>::failure(invalid(
        "workspace object name already exists: " + name,
        "choose a unique object name"));
  }

  const auto replacement = model::MolecularSystem::create(
      current.system->id(), name, current.system->topology(),
      current.system->coordinates());
  if (!replacement.has_value())
    return operation::Result<ObjectLifecycleResult>::failure(
        replacement.error());
  auto builder = scene::SceneBuilder::from(*scene_);
  if (const auto error = builder.rename(current.scene_node, name);
      error.has_value())
    return operation::Result<ObjectLifecycleResult>::failure(*error);
  if (const auto error =
          builder.replace_system(current.scene_node, replacement.value());
      error.has_value())
    return operation::Result<ObjectLifecycleResult>::failure(*error);
  const auto next_scene = builder.build();
  if (!next_scene.has_value())
    return operation::Result<ObjectLifecycleResult>::failure(
        next_scene.error());

  objects_[index].system = replacement.value();
  scene_ = next_scene.value();
  std::vector<std::uint64_t> order;
  order.reserve(objects_.size());
  for (const auto &object : objects_)
    order.push_back(object.id);
  return operation::Result<ObjectLifecycleResult>::success(
      ObjectLifecycleResult{current.id,
                            current.scene_node.value,
                            std::move(name),
                            index,
                            index,
                            objects_.size(),
                            false,
                            active_object() == nullptr
                                ? std::optional<std::uint64_t>{}
                                : active_object()->id,
                            std::move(order),
                            0U,
                            0U});
}

operation::Result<ObjectLifecycleResult>
Workspace::delete_object(std::string_view object_reference) {
  const auto index_result = object_index_by_reference(object_reference);
  if (!index_result.has_value())
    return operation::Result<ObjectLifecycleResult>::failure(
        index_result.error());
  const auto index = index_result.value();
  const auto deleted_id = objects_[index].id;
  const auto deleted_scene_node = objects_[index].scene_node;
  const auto deleted_name = objects_[index].system->name();

  auto builder = scene::SceneBuilder::from(*scene_);
  if (const auto error = builder.remove_subtree(deleted_scene_node);
      error.has_value())
    return operation::Result<ObjectLifecycleResult>::failure(*error);
  std::optional<std::size_t> next_active_index = active_index_;
  if (active_index_.has_value()) {
    if (*active_index_ == index) {
      if (objects_.size() == 1U)
        next_active_index.reset();
      else
        next_active_index = std::min(index, objects_.size() - 2U);
    } else if (*active_index_ > index) {
      next_active_index = *active_index_ - 1U;
    }
  }
  if (next_active_index.has_value()) {
    const auto source_index = *next_active_index >= index
                                  ? *next_active_index + 1U
                                  : *next_active_index;
    if (const auto error =
            builder.set_selection({objects_[source_index].scene_node});
        error.has_value())
      return operation::Result<ObjectLifecycleResult>::failure(*error);
  } else if (const auto error = builder.set_selection({}); error.has_value()) {
    return operation::Result<ObjectLifecycleResult>::failure(*error);
  }
  const auto next_scene = builder.build();
  if (!next_scene.has_value())
    return operation::Result<ObjectLifecycleResult>::failure(
        next_scene.error());

  auto setting_snapshot = render_settings_.snapshot();
  const auto old_setting_count = setting_snapshot.overrides.size();
  std::erase_if(setting_snapshot.overrides, [deleted_id](const auto &entry) {
    return entry.scope.level != render::RenderSettingScopeLevel::global &&
           entry.scope.object_id == deleted_id;
  });
  const auto settings = render::RenderSettingStore::restore(setting_snapshot);
  if (!settings.has_value())
    return operation::Result<ObjectLifecycleResult>::failure(settings.error());
  const auto old_measurement_count = measurements_.size();
  auto next_measurements = measurements_;
  std::erase_if(next_measurements, [deleted_id](const auto &measurement) {
    return measurement.object_id == deleted_id;
  });

  objects_.erase(objects_.begin() + static_cast<std::ptrdiff_t>(index));
  active_index_ = next_active_index;
  measurements_ = std::move(next_measurements);
  render_settings_ = settings.value();
  scene_ = next_scene.value();
  std::vector<std::uint64_t> order;
  order.reserve(objects_.size());
  for (const auto &object : objects_)
    order.push_back(object.id);
  return operation::Result<ObjectLifecycleResult>::success(
      ObjectLifecycleResult{deleted_id,
                            deleted_scene_node.value,
                            deleted_name,
                            index,
                            index,
                            objects_.size(),
                            true,
                            active_object() == nullptr
                                ? std::optional<std::uint64_t>{}
                                : active_object()->id,
                            std::move(order),
                            old_measurement_count - measurements_.size(),
                            old_setting_count -
                                setting_snapshot.overrides.size()});
}

operation::Result<ObjectLifecycleResult>
Workspace::reorder_object(std::string_view object_reference,
                          std::size_t new_position) {
  const auto index_result = object_index_by_reference(object_reference);
  if (!index_result.has_value())
    return operation::Result<ObjectLifecycleResult>::failure(
        index_result.error());
  if (new_position >= objects_.size()) {
    return operation::Result<ObjectLifecycleResult>::failure(invalid(
        "object position is out of range",
        "provide a 1-based position no greater than the object count"));
  }
  const auto old_position = index_result.value();
  const auto moved_id = objects_[old_position].id;
  const auto moved_node = objects_[old_position].scene_node;
  const auto moved_name = objects_[old_position].system->name();
  const auto active_id = active_object() == nullptr
                             ? std::optional<std::uint64_t>{}
                             : std::optional<std::uint64_t>{active_object()->id};

  auto builder = scene::SceneBuilder::from(*scene_);
  const auto *root = scene_->find(scene_->root());
  if (root == nullptr)
    return operation::Result<ObjectLifecycleResult>::failure(
        operation::Error{operation::ErrorCode::internal,
                         "workspace scene root is missing", {}});
  const auto target_node = objects_[new_position].scene_node;
  const auto target_child = std::find(root->children().begin(),
                                      root->children().end(), target_node);
  if (target_child == root->children().end())
    return operation::Result<ObjectLifecycleResult>::failure(
        operation::Error{operation::ErrorCode::internal,
                         "workspace object is not a root scene child", {}});
  auto insertion = static_cast<std::size_t>(target_child -
                                            root->children().begin());
  if (const auto error =
          builder.reparent(moved_node, scene_->root(), insertion);
      error.has_value())
    return operation::Result<ObjectLifecycleResult>::failure(*error);
  const auto next_scene = builder.build();
  if (!next_scene.has_value())
    return operation::Result<ObjectLifecycleResult>::failure(
        next_scene.error());

  if (old_position < new_position) {
    std::rotate(objects_.begin() + static_cast<std::ptrdiff_t>(old_position),
                objects_.begin() + static_cast<std::ptrdiff_t>(old_position + 1U),
                objects_.begin() + static_cast<std::ptrdiff_t>(new_position + 1U));
  } else if (new_position < old_position) {
    std::rotate(objects_.begin() + static_cast<std::ptrdiff_t>(new_position),
                objects_.begin() + static_cast<std::ptrdiff_t>(old_position),
                objects_.begin() + static_cast<std::ptrdiff_t>(old_position + 1U));
  }
  if (active_id.has_value()) {
    const auto found = std::find_if(objects_.begin(), objects_.end(),
                                    [&](const auto &object) {
                                      return object.id == *active_id;
                                    });
    active_index_ = static_cast<std::size_t>(found - objects_.begin());
  }
  scene_ = next_scene.value();
  std::vector<std::uint64_t> order;
  order.reserve(objects_.size());
  for (const auto &object : objects_)
    order.push_back(object.id);
  return operation::Result<ObjectLifecycleResult>::success(
      ObjectLifecycleResult{moved_id,
                            moved_node.value,
                            moved_name,
                            old_position,
                            new_position,
                            objects_.size(),
                            false,
                            active_id,
                            std::move(order),
                            0U,
                            0U});
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
Workspace::active_frame(const WorkspaceObject &object) const {
  const auto index = object.trajectory.has_value()
                         ? object.trajectory->timeline.snapshot().frame
                         : 0U;
  return object.system->coordinates()->read_frame(index);
}

operation::Result<TopologyMutationResult> Workspace::retain_active_atoms(
    std::span<const model::AtomId> ordered_atom_ids,
    std::uint64_t expected_topology_version) {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return operation::Result<TopologyMutationResult>::failure(missing_active());
  const auto source_topology = object->system->topology();
  const model::TopologySnapshotReference snapshot{
      model::kTopologyReferenceSchemaVersion, object->id,
      expected_topology_version};
  if (const auto error = model::validate_topology_snapshot(
          snapshot, object->id, *source_topology);
      error.has_value()) {
    return operation::Result<TopologyMutationResult>::failure(*error);
  }

  auto builder = model::TopologyBuilder::from(*source_topology);
  if (const auto error = builder.retain_atoms(ordered_atom_ids);
      error.has_value())
    return operation::Result<TopologyMutationResult>::failure(*error);
  const auto target_topology = builder.build();
  if (!target_topology.has_value())
    return operation::Result<TopologyMutationResult>::failure(
        target_topology.error());
  const auto remap =
      model::remap_topology(*source_topology, *target_topology.value());
  std::vector<std::optional<std::size_t>> coordinate_mapping;
  coordinate_mapping.reserve(remap.target_atoms.size());
  for (const auto source : remap.target_atoms)
    coordinate_mapping.push_back(source.has_value()
                                     ? std::optional<std::size_t>{source->value}
                                     : std::nullopt);
  const auto remapped_source = model::RemappedCoordinateSource::create(
      object->system->coordinates(), std::move(coordinate_mapping));
  if (!remapped_source.has_value())
    return operation::Result<TopologyMutationResult>::failure(
        remapped_source.error());

  std::shared_ptr<const model::CoordinateSource> coordinates =
      remapped_source.value();
  std::optional<TrajectoryState> next_trajectory;
  if (object->trajectory.has_value()) {
    const auto budget = object->trajectory->cache->stats().payload_budget_bytes;
    const auto cache = trajectory::FrameCache::create(coordinates, budget);
    if (!cache.has_value())
      return operation::Result<TopologyMutationResult>::failure(cache.error());
    const auto prefetch = trajectory::PrefetchScheduler::create(cache.value());
    if (!prefetch.has_value())
      return operation::Result<TopologyMutationResult>::failure(
          prefetch.error());
    coordinates = cache.value();
    auto mapping = object->trajectory->mapping;
    mapping.topology_version = target_topology.value()->version();
    mapping.identity_strength += "+topology-stable-id-remap";
    mapping.target_to_source.resize(target_topology.value()->atom_count());
    for (std::size_t index = 0; index < mapping.target_to_source.size();
         ++index)
      mapping.target_to_source[index] = index;
    next_trajectory = TrajectoryState{
        object->trajectory->path,
        object->trajectory->format,
        cache.value(),
        object->trajectory->timeline,
        object->trajectory->clock,
        prefetch.value(),
        object->trajectory->prefetch_frame_count,
        std::move(mapping),
        object->trajectory->semantics};
  }
  const auto target_system = model::MolecularSystem::create(
      object->id, object->system->name(), target_topology.value(), coordinates);
  if (!target_system.has_value())
    return operation::Result<TopologyMutationResult>::failure(
        target_system.error());
  const auto visibility = object->representation_visibility.remap(remap);
  if (!visibility.has_value())
    return operation::Result<TopologyMutationResult>::failure(
        visibility.error());
  const auto selections = object->selections.remap(
      *source_topology, *target_topology.value(), remap);
  if (!selections.has_value())
    return operation::Result<TopologyMutationResult>::failure(
        selections.error());

  auto setting_snapshot = render_settings_.snapshot();
  const auto before_setting_count = setting_snapshot.overrides.size();
  std::erase_if(setting_snapshot.overrides, [&](const auto &override) {
    if (override.scope.object_id != object->id)
      return false;
    if (override.scope.level == render::RenderSettingScopeLevel::atom)
      return !target_topology.value()
                  ->atom_index(model::AtomId{override.scope.atom_id})
                  .has_value();
    if (override.scope.level == render::RenderSettingScopeLevel::bond)
      return !target_topology.value()
                  ->bond_index(model::BondId{override.scope.bond_id})
                  .has_value();
    return false;
  });
  const auto settings = render::RenderSettingStore::restore(setting_snapshot);
  if (!settings.has_value())
    return operation::Result<TopologyMutationResult>::failure(settings.error());
  const auto frame_index = next_trajectory.has_value()
                               ? next_trajectory->timeline.snapshot().frame
                               : 0U;
  const auto frame = coordinates->read_frame(frame_index);
  if (!frame.has_value())
    return operation::Result<TopologyMutationResult>::failure(frame.error());
  WorkspaceObject candidate = *object;
  candidate.system = target_system.value();
  if (candidate.coordinate_revision ==
      std::numeric_limits<std::uint64_t>::max())
    return operation::Result<TopologyMutationResult>::failure(
        {operation::ErrorCode::resource_exhausted,
         "coordinate revision space is exhausted", {}});
  ++candidate.coordinate_revision;
  if (candidate.coordinate_source_revision ==
      std::numeric_limits<std::uint64_t>::max())
    return operation::Result<TopologyMutationResult>::failure(
        {operation::ErrorCode::resource_exhausted,
         "coordinate source revision space is exhausted", {}});
  ++candidate.coordinate_source_revision;
  candidate.selections = std::move(selections.value());
  candidate.representation_visibility = std::move(visibility.value());
  candidate.trajectory = std::move(next_trajectory);
  candidate.molecular_surface.reset();
  const auto rebuilt = rebuild_representations(
      candidate, target_system.value(), *frame.value(), frame_index,
      settings.value(), color_table_);
  if (!rebuilt.has_value())
    return operation::Result<TopologyMutationResult>::failure(rebuilt.error());
  candidate.representations = rebuilt.value();
  candidate.molecular_surface.reset();

  auto scene_builder = scene::SceneBuilder::from(*scene_);
  if (const auto error = scene_builder.replace_system(candidate.scene_node,
                                                       candidate.system);
      error.has_value())
    return operation::Result<TopologyMutationResult>::failure(*error);
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value())
    return operation::Result<TopologyMutationResult>::failure(
        next_scene.error());
  const auto previous_bond_count = source_topology->bonds().size();
  const auto invalidated = static_cast<std::size_t>(std::count_if(
      measurements_.begin(), measurements_.end(),
      [id = object->id](const auto &measurement) {
        return measurement.object_id == id;
      }));
  auto next_measurements = measurements_;
  std::erase_if(next_measurements, [id = object->id](const auto &measurement) {
    return measurement.object_id == id;
  });
  *object = std::move(candidate);
  render_settings_ = std::move(settings.value());
  measurements_.swap(next_measurements);
  scene_ = next_scene.value();
  if (object->trajectory.has_value())
    static_cast<void>(schedule_prefetch(*object->trajectory));
  refresh_proximity_ramps(object->id);

  std::vector<std::uint64_t> ids;
  ids.reserve(target_topology.value()->atom_ids().size());
  for (const auto id : target_topology.value()->atom_ids())
    ids.push_back(id.value);
  return operation::Result<TopologyMutationResult>::success(
      {object->id,
       source_topology->version(),
       target_topology.value()->version(),
       source_topology->atom_count(),
       target_topology.value()->atom_count(),
       source_topology->atom_count() - target_topology.value()->atom_count(),
       previous_bond_count - target_topology.value()->bonds().size(),
       invalidated,
       before_setting_count - setting_snapshot.overrides.size(),
      std::move(ids)});
}

operation::Result<SelectionCountResult>
Workspace::count_selection(std::string_view object_reference,
                           std::string selection_expression) const {
  using R = operation::Result<SelectionCountResult>;
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value())
    return R::failure(index.error());
  const auto &object = objects_[index.value()];
  const auto mask = selection_mask(object, selection_expression);
  if (!mask.has_value())
    return R::failure(mask.error());
  const auto selected = static_cast<std::size_t>(
      std::count_if(mask.value().begin(), mask.value().end(),
                    [](const auto value) { return value != 0U; }));
  return R::success(
      {object.id, object.system->name(), std::move(selection_expression),
       selected, object.system->topology()->atom_count(),
       object.trajectory.has_value()
           ? object.trajectory->cache->frame_count()
           : object.system->coordinates()->frame_count()});
}

namespace {

// Subset of a topology and its coordinate source for an ordered atom-ID list.
struct AtomSubset {
  std::shared_ptr<const model::Topology> topology;
  std::shared_ptr<const model::CoordinateSource> coordinates;
};

operation::Result<AtomSubset>
make_atom_subset(const model::MolecularSystem &system,
                 std::span<const model::AtomId> ordered_atom_ids) {
  using R = operation::Result<AtomSubset>;
  const auto source_topology = system.topology();
  auto builder = model::TopologyBuilder::from(*source_topology);
  if (const auto error = builder.retain_atoms(ordered_atom_ids);
      error.has_value())
    return R::failure(*error);
  const auto target = builder.build();
  if (!target.has_value())
    return R::failure(target.error());
  const auto remap = model::remap_topology(*source_topology, *target.value());
  std::vector<std::optional<std::size_t>> mapping;
  mapping.reserve(remap.target_atoms.size());
  for (const auto source : remap.target_atoms)
    mapping.push_back(source.has_value()
                          ? std::optional<std::size_t>{source->value}
                          : std::nullopt);
  const auto remapped =
      model::RemappedCoordinateSource::create(system.coordinates(),
                                              std::move(mapping));
  if (!remapped.has_value())
    return R::failure(remapped.error());
  return R::success({target.value(), remapped.value()});
}

std::vector<model::AtomId> atom_ids_from(const std::vector<std::uint64_t> &ids) {
  std::vector<model::AtomId> result;
  result.reserve(ids.size());
  for (const auto id : ids)
    result.push_back(model::AtomId{id});
  return result;
}

} // namespace

operation::Result<ObjectCreateResult> Workspace::create_object_from_selection(
    std::string_view source_reference, std::string name,
    std::string selection_expression, bool remove_from_source) {
  using R = operation::Result<ObjectCreateResult>;
  const auto index = object_index_by_reference(source_reference);
  if (!index.has_value())
    return R::failure(index.error());
  const auto source_id = objects_[index.value()].id;
  const auto source_name = objects_[index.value()].system->name();
  if (name.empty())
    return R::failure(invalid("new object name must not be empty",
                              "provide a unique object name"));
  const auto selected = selected_atom_ids(source_id, selection_expression);
  if (!selected.has_value())
    return R::failure(selected.error());
  if (selected.value().empty())
    return R::failure(invalid("selection matches no atoms",
                              "check the selection expression"));
  const auto source_atom_count =
      objects_[index.value()].system->topology()->atom_count();
  if (remove_from_source && selected.value().size() == source_atom_count)
    return R::failure(invalid(
        "extracting every atom would leave an empty source object",
        "use object copy, or rename the source object"));
  const auto ids = atom_ids_from(selected.value());
  const auto subset = make_atom_subset(*objects_[index.value()].system, ids);
  if (!subset.has_value())
    return R::failure(subset.error());
  const auto previous_active = active_index_;
  io::StructureDocument document;
  document.format = io::StructureFormat::auto_detect;
  document.source_name = "<object-" + std::to_string(source_id) + ">";
  document.structures.push_back(
      {name, subset.value().topology, subset.value().coordinates,
       {{"derived_from_object", std::to_string(source_id)},
        {"selection", selection_expression}}});
  const auto loaded = load_structure_document(std::move(document), {}, name);
  if (!loaded.has_value())
    return R::failure(loaded.error());
  const auto created_id = loaded.value().object_id;
  const auto created_index = object_index_by_reference(std::to_string(created_id));
  if (!created_index.has_value())
    return R::failure(created_index.error());
  const auto &created = objects_[created_index.value()];
  ObjectCreateResult result{
      source_id,
      source_name,
      created_id,
      created.system->name(),
      selection_expression,
      created.system->topology()->atom_count(),
      created.system->topology()->bonds().size(),
      created.system->coordinates()->frame_count(),
      0U};
  if (remove_from_source) {
    // Keep the new object active only after the source edit succeeds.
    const auto source_index = object_index_by_reference(std::to_string(source_id));
    if (!source_index.has_value())
      return R::failure(source_index.error());
    const auto activated = activate_object(source_id);
    if (!activated.has_value()) {
      static_cast<void>(delete_object(std::to_string(created_id)));
      return R::failure(activated.error());
    }
    const auto removed = remove_active_selection(selection_expression);
    if (!removed.has_value()) {
      static_cast<void>(delete_object(std::to_string(created_id)));
      if (previous_active.has_value() && *previous_active < objects_.size())
        static_cast<void>(activate_object(objects_[*previous_active].id));
      return R::failure(removed.error());
    }
    result.removed_from_source_atom_count = removed.value().removed_atom_count;
    static_cast<void>(activate_object(created_id));
  }
  return R::success(std::move(result));
}

operation::Result<TopologyMutationResult>
Workspace::remove_active_selection(std::string selection_expression) {
  using R = operation::Result<TopologyMutationResult>;
  const auto *object = active_object();
  if (object == nullptr)
    return R::failure(missing_active());
  const auto selected = selected_atom_ids(object->id, selection_expression);
  if (!selected.has_value())
    return R::failure(selected.error());
  if (selected.value().empty())
    return R::failure(invalid("selection matches no atoms",
                              "check the selection expression"));
  const auto &topology = *object->system->topology();
  if (selected.value().size() == topology.atom_count())
    return R::failure(invalid(
        "removing every atom would leave an empty object",
        "use object delete to remove the whole object"));
  std::set<std::uint64_t> removed(selected.value().begin(),
                                  selected.value().end());
  std::vector<model::AtomId> kept;
  kept.reserve(topology.atom_count() - removed.size());
  for (const auto id : topology.atom_ids())
    if (!removed.contains(id.value))
      kept.push_back(id);
  return retain_active_atoms(kept, topology.version());
}

operation::Result<CoordinateEditResult> Workspace::set_active_atom_position(
    model::AtomId atom_id, model::Vec3d position,
    std::uint64_t expected_topology_version,
    std::uint64_t expected_coordinate_source_revision) {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return operation::Result<CoordinateEditResult>::failure(missing_active());
  if (object->trajectory.has_value())
    return operation::Result<CoordinateEditResult>::failure(operation::Error{
        operation::ErrorCode::unsupported,
        "coordinate editing is not available while a trajectory is attached",
        "detach or create a static copy before editing"});
  if (!scene::is_finite(position))
    return operation::Result<CoordinateEditResult>::failure(
        invalid("atom position must be finite"));
  const auto &topology = *object->system->topology();
  if (topology.version() != expected_topology_version ||
      object->coordinate_source_revision !=
          expected_coordinate_source_revision)
    return operation::Result<CoordinateEditResult>::failure(operation::Error{
        operation::ErrorCode::stale_result,
        "coordinate edit targets a stale molecular snapshot",
        "refresh the object revision and retry"});
  const auto atom_index = topology.atom_index(atom_id);
  if (!atom_index.has_value())
    return operation::Result<CoordinateEditResult>::failure(
        invalid("coordinate edit atom ID does not exist"));
  const auto frame_count = object->system->coordinates()->frame_count();
  if (!frame_count.has_value() || *frame_count == 0U)
    return operation::Result<CoordinateEditResult>::failure(operation::Error{
        operation::ErrorCode::unsupported,
        "coordinate editing requires a finite in-memory frame set", {}});
  const auto memory_bytes = saturated_add(
      coordinate_source_payload_bytes(*object->system->coordinates()),
      sizeof(EditRecord));
  if (memory_bytes > edit_history_memory_budget_bytes_)
    return operation::Result<CoordinateEditResult>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "coordinate edit cannot retain an undo snapshot within the budget",
        "increase the edit history budget or edit a smaller object",
        {{"memory_required_bytes", std::to_string(memory_bytes)},
         {"memory_budget_bytes",
          std::to_string(edit_history_memory_budget_bytes_)}}});
  std::vector<std::shared_ptr<const model::CoordinateFrame>> frames;
  frames.reserve(*frame_count);
  model::Vec3d previous;
  for (std::size_t frame_index = 0; frame_index < *frame_count; ++frame_index) {
    const auto frame = object->system->coordinates()->read_frame(frame_index);
    if (!frame.has_value())
      return operation::Result<CoordinateEditResult>::failure(frame.error());
    if (!frame.value()->atom_present(atom_index->value))
      return operation::Result<CoordinateEditResult>::failure(operation::Error{
          operation::ErrorCode::invalid_argument,
          "coordinate edit atom is missing from one or more states",
          "choose an atom present in every state"});
    if (frame_index == 0U)
      previous = coordinate_at(*frame.value(), atom_index->value);
    const auto replaced =
        frame_with_coordinate(*frame.value(), atom_index->value, position);
    if (!replaced.has_value())
      return operation::Result<CoordinateEditResult>::failure(replaced.error());
    frames.push_back(replaced.value());
  }
  const auto coordinates = model::InMemoryCoordinateSource::create(
      topology.atom_count(), std::move(frames));
  if (!coordinates.has_value())
    return operation::Result<CoordinateEditResult>::failure(coordinates.error());
  const auto system = model::MolecularSystem::create(
      object->id, object->system->name(), object->system->topology(),
      coordinates.value());
  if (!system.has_value())
    return operation::Result<CoordinateEditResult>::failure(system.error());
  if (object->coordinate_source_revision ==
          std::numeric_limits<std::uint64_t>::max() ||
      object->coordinate_revision == std::numeric_limits<std::uint64_t>::max() ||
      next_edit_transaction_id_ == std::numeric_limits<std::uint64_t>::max())
    return operation::Result<CoordinateEditResult>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "coordinate edit revision or transaction space is exhausted", {}});

  WorkspaceObject candidate = *object;
  candidate.system = system.value();
  ++candidate.coordinate_source_revision;
  ++candidate.coordinate_revision;
  candidate.molecular_surface.reset();
  const auto frame = candidate.system->coordinates()->read_frame(0U);
  if (!frame.has_value())
    return operation::Result<CoordinateEditResult>::failure(frame.error());
  const auto rebuilt = rebuild_representations(
      candidate, candidate.system, *frame.value(), 0U, render_settings_,
      color_table_);
  if (!rebuilt.has_value())
    return operation::Result<CoordinateEditResult>::failure(rebuilt.error());
  candidate.representations = rebuilt.value();
  auto scene_builder = scene::SceneBuilder::from(*scene_);
  if (const auto error =
          scene_builder.replace_system(candidate.scene_node, candidate.system);
      error.has_value())
    return operation::Result<CoordinateEditResult>::failure(*error);
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value())
    return operation::Result<CoordinateEditResult>::failure(next_scene.error());

  const auto invalidated = static_cast<std::size_t>(std::count_if(
      measurements_.begin(), measurements_.end(),
      [id = object->id](const auto &record) { return record.object_id == id; }));
  auto next_measurements = measurements_;
  std::erase_if(next_measurements, [id = object->id](const auto &record) {
    return record.object_id == id;
  });
  const auto previous_source_revision = object->coordinate_source_revision;
  const auto previous_coordinate_revision = object->coordinate_revision;
  const auto transaction_id = next_edit_transaction_id_;
  EditRecord record{EditRecordKind::coordinate, transaction_id, object->id,
                    object->system, system.value(), std::nullopt,
                    memory_bytes};
  try {
    undo_history_.push_back(record);
  } catch (const std::bad_alloc &) {
    return operation::Result<CoordinateEditResult>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "coordinate edit undo allocation failed before commit", {}});
  }
  ++next_edit_transaction_id_;
  for (const auto &redo : redo_history_)
    edit_history_memory_used_bytes_ -= redo.memory_bytes;
  redo_history_.clear();
  edit_history_memory_used_bytes_ =
      saturated_add(edit_history_memory_used_bytes_, memory_bytes);
  const auto before_eviction = undo_history_.size();
  evict_edit_history_to_budget();
  const auto evicted = before_eviction - undo_history_.size();
  *object = std::move(candidate);
  scene_ = next_scene.value();
  measurements_.swap(next_measurements);
  refresh_proximity_ramps(object->id);
  return operation::Result<CoordinateEditResult>::success(
      {transaction_id, object->id, atom_id, 0U, previous, position,
       previous_source_revision, previous_coordinate_revision,
       object->coordinate_source_revision, object->coordinate_revision,
       *frame_count, invalidated, memory_bytes, evicted});
}

operation::Result<TopologyPropertyEditResult>
Workspace::commit_active_topology_edit(
    std::shared_ptr<const model::Topology> topology,
    std::uint64_t expected_topology_version,
    std::uint64_t expected_coordinate_source_revision,
    EditTransactionKind transaction_kind) {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return operation::Result<TopologyPropertyEditResult>::failure(
        missing_active());
  if (topology == nullptr)
    return operation::Result<TopologyPropertyEditResult>::failure(
        invalid("topology edit requires a candidate topology"));
  if (transaction_kind != EditTransactionKind::atom_properties &&
      transaction_kind != EditTransactionKind::residue_properties &&
      transaction_kind != EditTransactionKind::bond_order)
    return operation::Result<TopologyPropertyEditResult>::failure(
        invalid("topology edit transaction kind is incompatible"));
  if (object->trajectory.has_value())
    return operation::Result<TopologyPropertyEditResult>::failure(
        operation::Error{
            operation::ErrorCode::unsupported,
            "topology property editing is unavailable with an attached trajectory",
            "detach or create a static copy before editing topology"});
  const auto &before_topology = *object->system->topology();
  if (before_topology.version() != expected_topology_version ||
      object->coordinate_source_revision !=
          expected_coordinate_source_revision)
    return operation::Result<TopologyPropertyEditResult>::failure(
        operation::Error{operation::ErrorCode::stale_result,
                         "topology edit targets a stale molecular snapshot",
                         "refresh topology and coordinate revisions"});
  if (topology->version() != before_topology.version() + 1U ||
      topology->atom_ids() != before_topology.atom_ids() ||
      topology->bond_ids() != before_topology.bond_ids() ||
      topology->atom_count() != before_topology.atom_count())
    return operation::Result<TopologyPropertyEditResult>::failure(
        invalid("property edit must preserve atom/bond stable identity and cardinality"));
  if (object->coordinate_source_revision ==
          std::numeric_limits<std::uint64_t>::max() ||
      object->coordinate_revision == std::numeric_limits<std::uint64_t>::max() ||
      next_edit_transaction_id_ == std::numeric_limits<std::uint64_t>::max())
    return operation::Result<TopologyPropertyEditResult>::failure(
        operation::Error{operation::ErrorCode::resource_exhausted,
                         "topology edit revision or transaction space is exhausted",
                         {}});
  auto memory_bytes = saturated_add(
      coordinate_source_payload_bytes(*object->system->coordinates()),
      sizeof(EditRecord));
  memory_bytes = saturated_add(
      memory_bytes,
      saturated_multiply(topology->atom_count(), sizeof(model::AtomRecord)));
  memory_bytes = saturated_add(
      memory_bytes,
      saturated_multiply(topology->bonds().size(), sizeof(model::Bond)));
  if (memory_bytes > edit_history_memory_budget_bytes_)
    return operation::Result<TopologyPropertyEditResult>::failure(
        operation::Error{
            operation::ErrorCode::resource_exhausted,
            "topology edit cannot retain undo state within the budget",
            "increase the edit history budget or edit a smaller object"});
  const auto system = model::MolecularSystem::create(
      object->id, object->system->name(), topology,
      object->system->coordinates());
  if (!system.has_value())
    return operation::Result<TopologyPropertyEditResult>::failure(
        system.error());
  WorkspaceObject candidate = *object;
  candidate.system = system.value();
  const auto selection_remap =
      model::remap_topology(before_topology, *topology);
  const auto remapped_selections = candidate.selections.remap(
      before_topology, *topology, selection_remap);
  if (!remapped_selections.has_value())
    return operation::Result<TopologyPropertyEditResult>::failure(
        remapped_selections.error());
  candidate.selections = remapped_selections.value();
  ++candidate.coordinate_source_revision;
  ++candidate.coordinate_revision;
  candidate.molecular_surface.reset();
  const auto frame = candidate.system->coordinates()->read_frame(0U);
  if (!frame.has_value())
    return operation::Result<TopologyPropertyEditResult>::failure(
        frame.error());
  const auto rebuilt = rebuild_representations(
      candidate, candidate.system, *frame.value(), 0U, render_settings_,
      color_table_);
  if (!rebuilt.has_value())
    return operation::Result<TopologyPropertyEditResult>::failure(
        rebuilt.error());
  candidate.representations = rebuilt.value();
  auto scene_builder = scene::SceneBuilder::from(*scene_);
  if (const auto error =
          scene_builder.replace_system(candidate.scene_node, candidate.system);
      error.has_value())
    return operation::Result<TopologyPropertyEditResult>::failure(*error);
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value())
    return operation::Result<TopologyPropertyEditResult>::failure(
        next_scene.error());
  auto next_measurements = measurements_;
  const auto before_measurement_count = next_measurements.size();
  std::erase_if(next_measurements, [id = object->id](const auto &record) {
    return record.object_id == id;
  });
  const auto invalidated = before_measurement_count - next_measurements.size();
  const auto previous_source_revision = object->coordinate_source_revision;
  const auto transaction_id = next_edit_transaction_id_;
  EditRecord record{EditRecordKind::coordinate, transaction_id, object->id,
                    object->system, system.value(), std::nullopt,
                    memory_bytes, transaction_kind};
  try {
    undo_history_.push_back(record);
  } catch (const std::bad_alloc &) {
    return operation::Result<TopologyPropertyEditResult>::failure(
        operation::Error{operation::ErrorCode::resource_exhausted,
                         "topology edit undo allocation failed before commit",
                         {}});
  }
  ++next_edit_transaction_id_;
  for (const auto &redo : redo_history_)
    edit_history_memory_used_bytes_ -= redo.memory_bytes;
  redo_history_.clear();
  edit_history_memory_used_bytes_ =
      saturated_add(edit_history_memory_used_bytes_, memory_bytes);
  const auto before_eviction = undo_history_.size();
  evict_edit_history_to_budget();
  *object = std::move(candidate);
  scene_ = next_scene.value();
  measurements_.swap(next_measurements);
  return operation::Result<TopologyPropertyEditResult>::success(
      {transaction_id, object->id, expected_topology_version,
       topology->version(), previous_source_revision,
       object->coordinate_source_revision, object->coordinate_revision,
       invalidated, memory_bytes, before_eviction - undo_history_.size(),
       transaction_kind});
}

operation::Result<std::size_t> Workspace::apply_edit_record(
    const EditRecord &record, bool use_after) {
  if (record.kind == EditRecordKind::object_create) {
    if (!record.created_object.has_value())
      return operation::Result<std::size_t>::failure(operation::Error{
          operation::ErrorCode::internal,
          "object-create edit history is missing its object snapshot", {}});
    const auto &created = *record.created_object;
    if (!use_after) {
      const auto found = std::ranges::find(objects_, record.object_id,
                                           &WorkspaceObject::id);
      if (found == objects_.end() || found->system != created.system)
        return operation::Result<std::size_t>::failure(operation::Error{
            operation::ErrorCode::stale_result,
            "molecule builder history was invalidated by object mutation",
            "undo before modifying or deleting the built object"});
      const auto removed = delete_object(std::to_string(record.object_id));
      if (!removed.has_value())
        return operation::Result<std::size_t>::failure(removed.error());
      return operation::Result<std::size_t>::success(
          removed.value().removed_measurement_count);
    }
    if (std::ranges::any_of(objects_, [&](const auto &object) {
          return object.id == record.object_id ||
                 object.system->name() == created.system->name();
        }))
      return operation::Result<std::size_t>::failure(operation::Error{
          operation::ErrorCode::stale_result,
          "molecule builder redo target identity or name is occupied", {}});
    auto scene_builder = scene::SceneBuilder::from(*scene_);
    const auto node = scene_builder.add_system(
        scene_->root(), created.system->name(), created.system);
    if (!node.has_value())
      return operation::Result<std::size_t>::failure(node.error());
    if (const auto error = scene_builder.set_selection({node.value()});
        error.has_value())
      return operation::Result<std::size_t>::failure(*error);
    const auto next_scene = scene_builder.build();
    if (!next_scene.has_value())
      return operation::Result<std::size_t>::failure(next_scene.error());
    auto candidate = created;
    candidate.scene_node = node.value();
    try {
      objects_.reserve(objects_.size() + 1U);
      objects_.push_back(std::move(candidate));
    } catch (const std::bad_alloc &) {
      return operation::Result<std::size_t>::failure(operation::Error{
          operation::ErrorCode::resource_exhausted,
          "molecule builder redo allocation failed before commit", {}});
    }
    active_index_ = objects_.size() - 1U;
    scene_ = next_scene.value();
    return operation::Result<std::size_t>::success(0U);
  }
  auto *object = mutable_active_object();
  if (object == nullptr || object->id != record.object_id)
    return operation::Result<std::size_t>::failure(operation::Error{
        operation::ErrorCode::stale_result,
        "edit history target is not the active object",
        "activate the original object before undo or redo"});
  const auto expected = use_after ? record.before : record.after;
  const auto target = use_after ? record.after : record.before;
  if (object->system != expected)
    return operation::Result<std::size_t>::failure(operation::Error{
        operation::ErrorCode::stale_result,
        "edit history was invalidated by a newer object mutation",
        "start a new edit history from the current object"});
  if (object->coordinate_source_revision ==
          std::numeric_limits<std::uint64_t>::max() ||
      object->coordinate_revision == std::numeric_limits<std::uint64_t>::max())
    return operation::Result<std::size_t>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "coordinate revision space is exhausted", {}});
  WorkspaceObject candidate = *object;
  candidate.system = target;
  const auto selection_remap = model::remap_topology(
      *expected->topology(), *target->topology());
  const auto remapped_selections = candidate.selections.remap(
      *expected->topology(), *target->topology(), selection_remap);
  if (!remapped_selections.has_value())
    return operation::Result<std::size_t>::failure(
        remapped_selections.error());
  candidate.selections = remapped_selections.value();
  ++candidate.coordinate_source_revision;
  ++candidate.coordinate_revision;
  candidate.molecular_surface.reset();
  const auto frame = target->coordinates()->read_frame(0U);
  if (!frame.has_value())
    return operation::Result<std::size_t>::failure(frame.error());
  const auto rebuilt = rebuild_representations(
      candidate, target, *frame.value(), 0U, render_settings_, color_table_);
  if (!rebuilt.has_value())
    return operation::Result<std::size_t>::failure(rebuilt.error());
  candidate.representations = rebuilt.value();
  auto scene_builder = scene::SceneBuilder::from(*scene_);
  if (const auto error =
          scene_builder.replace_system(candidate.scene_node, target);
      error.has_value())
    return operation::Result<std::size_t>::failure(*error);
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value())
    return operation::Result<std::size_t>::failure(next_scene.error());
  const auto invalidated = static_cast<std::size_t>(std::count_if(
      measurements_.begin(), measurements_.end(),
      [id = object->id](const auto &item) { return item.object_id == id; }));
  auto next_measurements = measurements_;
  std::erase_if(next_measurements, [id = object->id](const auto &item) {
    return item.object_id == id;
  });
  *object = std::move(candidate);
  scene_ = next_scene.value();
  measurements_.swap(next_measurements);
  refresh_proximity_ramps(object->id);
  return operation::Result<std::size_t>::success(invalidated);
}

operation::Result<EditHistoryResult> Workspace::undo_edit() {
  if (undo_history_.empty())
    return operation::Result<EditHistoryResult>::failure(operation::Error{
        operation::ErrorCode::not_found, "edit undo history is empty", {}});
  const auto record = undo_history_.back();
  const auto applied = apply_edit_record(record, false);
  if (!applied.has_value())
    return operation::Result<EditHistoryResult>::failure(applied.error());
  undo_history_.pop_back();
  redo_history_.push_back(record);
  const auto found = std::ranges::find(objects_, record.object_id,
                                       &WorkspaceObject::id);
  const auto source_revision = found == objects_.end()
                                   ? 0U
                                   : found->coordinate_source_revision;
  const auto coordinate_revision = found == objects_.end()
                                       ? 0U
                                       : found->coordinate_revision;
  return operation::Result<EditHistoryResult>::success(
      {record.transaction_id, "undo", record.object_id,
       source_revision, coordinate_revision,
       applied.value(), edit_history_status(), record.transaction_kind});
}

operation::Result<EditHistoryResult> Workspace::redo_edit() {
  if (redo_history_.empty())
    return operation::Result<EditHistoryResult>::failure(operation::Error{
        operation::ErrorCode::not_found, "edit redo history is empty", {}});
  const auto record = redo_history_.back();
  const auto applied = apply_edit_record(record, true);
  if (!applied.has_value())
    return operation::Result<EditHistoryResult>::failure(applied.error());
  redo_history_.pop_back();
  undo_history_.push_back(record);
  const auto found = std::ranges::find(objects_, record.object_id,
                                       &WorkspaceObject::id);
  const auto source_revision = found == objects_.end()
                                   ? 0U
                                   : found->coordinate_source_revision;
  const auto coordinate_revision = found == objects_.end()
                                       ? 0U
                                       : found->coordinate_revision;
  return operation::Result<EditHistoryResult>::success(
      {record.transaction_id, "redo", record.object_id,
       source_revision, coordinate_revision,
       applied.value(), edit_history_status(), record.transaction_kind});
}

EditHistoryStatus Workspace::edit_history_status() const noexcept {
  EditHistoryStatus result{edit_history_memory_budget_bytes_,
                           edit_history_memory_used_bytes_,
                           undo_history_.size(), redo_history_.size(),
                           std::nullopt, std::nullopt};
  if (!undo_history_.empty())
    result.next_undo_transaction = undo_history_.back().transaction_id;
  if (!redo_history_.empty())
    result.next_redo_transaction = redo_history_.back().transaction_id;
  return result;
}

operation::Result<EditHistoryStatus> Workspace::set_edit_history_budget(
    std::size_t memory_budget_bytes) {
  if (memory_budget_bytes == 0U)
    return operation::Result<EditHistoryStatus>::failure(
        invalid("edit history memory budget must be positive"));
  edit_history_memory_budget_bytes_ = memory_budget_bytes;
  evict_edit_history_to_budget();
  return operation::Result<EditHistoryStatus>::success(edit_history_status());
}

operation::Result<MoleculeBuildCommitResult> Workspace::commit_built_molecule(
    std::string name, const model::MoleculeBuildResult &built) {
  if (name.empty())
    return operation::Result<MoleculeBuildCommitResult>::failure(
        invalid("built molecule name must not be empty"));
  if (built.topology == nullptr || built.coordinates == nullptr)
    return operation::Result<MoleculeBuildCommitResult>::failure(
        invalid("built molecule requires topology and coordinates"));
  const auto memory_bytes =
      saturated_add(built.reserved_bytes, sizeof(EditRecord));
  if (memory_bytes > edit_history_memory_budget_bytes_)
    return operation::Result<MoleculeBuildCommitResult>::failure(
        operation::Error{
            operation::ErrorCode::resource_exhausted,
            "molecule builder cannot retain undo state within the budget",
            "increase the edit history budget or build a smaller fragment",
            {{"memory_required_bytes", std::to_string(memory_bytes)},
             {"memory_budget_bytes",
              std::to_string(edit_history_memory_budget_bytes_)}}});
  if (next_edit_transaction_id_ ==
      std::numeric_limits<std::uint64_t>::max())
    return operation::Result<MoleculeBuildCommitResult>::failure(
        operation::Error{operation::ErrorCode::resource_exhausted,
                         "edit transaction identity space is exhausted", {}});

  const auto previous_next_object_id = next_object_id_;
  try {
    undo_history_.push_back(EditRecord{});
  } catch (const std::bad_alloc &) {
    return operation::Result<MoleculeBuildCommitResult>::failure(
        operation::Error{operation::ErrorCode::resource_exhausted,
                         "molecule builder undo allocation failed before commit",
                         {}});
  }
  io::StructureDocument document;
  document.format = io::StructureFormat::auto_detect;
  document.source_name = "<molecule-builder>";
  document.structures.push_back(
      {name, built.topology, built.coordinates,
       {{"builder", "molshredder-molecule-builder-v1"}}});
  auto loaded = load_structure_document(std::move(document), {}, name);
  if (!loaded.has_value()) {
    undo_history_.pop_back();
    return operation::Result<MoleculeBuildCommitResult>::failure(
        loaded.error());
  }
  const auto *created = active_object();
  if (created == nullptr || created->id != loaded.value().object_id) {
    undo_history_.pop_back();
    return operation::Result<MoleculeBuildCommitResult>::failure(
        operation::Error{operation::ErrorCode::internal,
                         "molecule builder commit lost the created object", {}});
  }
  const auto transaction_id = next_edit_transaction_id_;
  try {
    undo_history_.back() = EditRecord{
        EditRecordKind::object_create, transaction_id, created->id, nullptr,
        nullptr, *created, memory_bytes, EditTransactionKind::molecule_build};
  } catch (const std::bad_alloc &) {
    undo_history_.pop_back();
    const auto rollback = delete_object(std::to_string(created->id));
    next_object_id_ = previous_next_object_id;
    if (!rollback.has_value())
      return operation::Result<MoleculeBuildCommitResult>::failure(
          operation::Error{operation::ErrorCode::internal,
                           "molecule builder undo allocation and rollback failed",
                           {}});
    return operation::Result<MoleculeBuildCommitResult>::failure(
        operation::Error{operation::ErrorCode::resource_exhausted,
                         "molecule builder undo snapshot allocation failed",
                         {}});
  }
  ++next_edit_transaction_id_;
  for (const auto &redo : redo_history_)
    edit_history_memory_used_bytes_ -= redo.memory_bytes;
  redo_history_.clear();
  edit_history_memory_used_bytes_ =
      saturated_add(edit_history_memory_used_bytes_, memory_bytes);
  const auto before_eviction = undo_history_.size();
  evict_edit_history_to_budget();
  return operation::Result<MoleculeBuildCommitResult>::success(
      {std::move(loaded.value()), transaction_id, memory_bytes,
       before_eviction - undo_history_.size()});
}

void Workspace::evict_edit_history_to_budget() {
  while (edit_history_memory_used_bytes_ >
             edit_history_memory_budget_bytes_ &&
         (!undo_history_.empty() || !redo_history_.empty())) {
    auto &history = !undo_history_.empty() ? undo_history_ : redo_history_;
    edit_history_memory_used_bytes_ -= history.front().memory_bytes;
    history.pop_front();
  }
}

operation::Result<ChemicalPerceptionApplyResult>
Workspace::apply_active_chemical_perception(
    const model::ChemicalPerceptionReport &report) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<ChemicalPerceptionApplyResult>::failure(
        missing_active());
  }
  const auto source_topology = object->system->topology();
  const auto target_topology =
      model::apply_chemical_perception(*source_topology, report);
  if (!target_topology.has_value()) {
    return operation::Result<ChemicalPerceptionApplyResult>::failure(
        target_topology.error());
  }
  const auto remap =
      model::remap_topology(*source_topology, *target_topology.value());
  const auto visibility = object->representation_visibility.remap(remap);
  if (!visibility.has_value()) {
    return operation::Result<ChemicalPerceptionApplyResult>::failure(
        visibility.error());
  }
  const auto selections = object->selections.remap(
      *source_topology, *target_topology.value(), remap);
  if (!selections.has_value()) {
    return operation::Result<ChemicalPerceptionApplyResult>::failure(
        selections.error());
  }
  const auto target_system = model::MolecularSystem::create(
      object->id, object->system->name(), target_topology.value(),
      object->system->coordinates());
  if (!target_system.has_value()) {
    return operation::Result<ChemicalPerceptionApplyResult>::failure(
        target_system.error());
  }

  WorkspaceObject candidate = *object;
  candidate.system = target_system.value();
  candidate.selections = selections.value();
  candidate.representation_visibility = visibility.value();
  if (candidate.trajectory.has_value()) {
    candidate.trajectory->mapping.topology_version =
        target_topology.value()->version();
    candidate.trajectory->mapping.identity_strength +=
        "+chemical-perception-inferred-bonds";
  }
  const auto frame_index = candidate.trajectory.has_value()
                               ? candidate.trajectory->timeline.snapshot().frame
                               : 0U;
  const auto frame = candidate.system->coordinates()->read_frame(frame_index);
  if (!frame.has_value()) {
    return operation::Result<ChemicalPerceptionApplyResult>::failure(
        frame.error());
  }
  const auto rebuilt = rebuild_representations(
      candidate, candidate.system, *frame.value(), frame_index,
      render_settings_, color_table_);
  if (!rebuilt.has_value()) {
    return operation::Result<ChemicalPerceptionApplyResult>::failure(
        rebuilt.error());
  }
  candidate.representations = rebuilt.value();
  auto scene_builder = scene::SceneBuilder::from(*scene_);
  if (const auto error = scene_builder.replace_system(candidate.scene_node,
                                                       candidate.system);
      error.has_value()) {
    return operation::Result<ChemicalPerceptionApplyResult>::failure(*error);
  }
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value()) {
    return operation::Result<ChemicalPerceptionApplyResult>::failure(
        next_scene.error());
  }

  const auto previous_bond_count = source_topology->bonds().size();
  *object = std::move(candidate);
  scene_ = next_scene.value();
  return operation::Result<ChemicalPerceptionApplyResult>::success(
      {object->id, source_topology->version(),
       target_topology.value()->version(), previous_bond_count,
       target_topology.value()->bonds().size(),
       target_topology.value()->bonds().size() - previous_bond_count});
}

std::optional<operation::Error>
Workspace::commit_render_settings(render::RenderSettingStore candidate) {
  std::vector<std::vector<RepresentationRecord>> rebuilt;
  rebuilt.reserve(objects_.size());
  for (const auto &object : objects_) {
    const auto frame = active_frame(object);
    if (!frame.has_value())
      return frame.error();
    const auto frame_index = object.trajectory.has_value()
                                 ? object.trajectory->timeline.snapshot().frame
                                 : 0U;
    auto records = rebuild_representations(object, object.system,
                                           *frame.value(), frame_index,
                                           candidate, color_table_);
    if (!records.has_value())
      return records.error();
    rebuilt.push_back(std::move(records.value()));
  }
  render_settings_ = std::move(candidate);
  for (std::size_t index = 0; index < objects_.size(); ++index)
    objects_[index].representations = std::move(rebuilt[index]);
  return std::nullopt;
}

operation::Result<SpectrumResult>
Workspace::spectrum(const SpectrumRequest &request) {
  using R = operation::Result<SpectrumResult>;
  auto *object = mutable_active_object();
  if (object == nullptr)
    return R::failure(missing_active());
  const auto interpolation =
      render::parse_spectrum_interpolation(request.interpolation);
  if (!interpolation.has_value())
    return R::failure(interpolation.error());
  const auto mask = selection_mask(*object, request.selection);
  if (!mask.has_value())
    return R::failure(mask.error());
  std::vector<std::size_t> selected;
  for (std::size_t atom = 0; atom < mask.value().size(); ++atom)
    if (mask.value()[atom] != 0U)
      selected.push_back(atom);
  const auto frame = active_frame(*object);
  if (!frame.has_value())
    return R::failure(frame.error());
  const auto frame_index = object->trajectory.has_value()
                               ? object->trajectory->timeline.snapshot().frame
                               : 0U;
  const auto *palette = render::find_spectrum_palette(request.palette);
  auto stripped = request.expression;
  std::erase(stripped, '_');
  const auto alphabetic =
      !stripped.empty() &&
      std::all_of(stripped.begin(), stripped.end(), [](char item) {
        return std::isalpha(static_cast<unsigned char>(item)) != 0;
      });
  const auto palette_path = palette != nullptr && alphabetic;
  SpectrumResult result;
  result.object_id = object->id;
  result.mode = palette_path ? "palette" : "color-list";
  result.palette =
      palette != nullptr ? std::string{palette->name} : request.palette;

  std::vector<std::int32_t> color_indices;
  if (!palette_path) {
    std::string colors = result.palette;
    if (colors.find(' ') == std::string::npos) {
      const auto listed = render::spectrum_palette_color_list(colors);
      if (!listed.empty())
        colors = std::string{listed};
      else
        std::replace(colors.begin(), colors.end(), '_', ' ');
    }
    std::istringstream stream{colors};
    for (std::string token; stream >> token;) {
      const auto index = color_table_.resolve(token);
      if (!index.has_value() || !color_table_.rgb(index.value()).has_value())
        return R::failure(operation::Error{
            operation::ErrorCode::invalid_argument, "unknown color",
            "spectrum colors must be table names, indices or hex values",
            {{"color", token}}});
      color_indices.push_back(index.value());
    }
    if (color_indices.size() < 2U)
      return R::failure(invalid("please provide at least 2 colors"));
  }
  if (selected.empty())
    return R::success(std::move(result));

  const auto expression =
      request.expression == "pc" ? std::string{"partial_charge"}
      : !palette_path && request.expression == "fc"
          ? std::string{"formal_charge"}
          : request.expression;
  const auto values = spectrum_values(*object, *frame.value(), frame_index,
                                      selected, expression, palette_path);
  if (!values.has_value())
    return R::failure(values.error());
  const auto &topology = *object->system->topology();
  std::vector<std::pair<std::uint64_t, std::int32_t>> updates;
  const auto add = [&](std::size_t atom, std::int32_t color)
      -> std::optional<operation::Error> {
    const auto id = topology.atom_id(model::AtomIndex{atom});
    if (!id.has_value())
      return invalid("topology atom identity is missing");
    updates.emplace_back(id->value, color);
    return std::nullopt;
  };
  if (palette_path) {
    std::vector<float> numbers;
    numbers.reserve(selected.size());
    for (const auto atom : selected)
      numbers.push_back(static_cast<float>(values.value()[atom].value_or(0.0)));
    const auto mapped = render::palette_spectrum(
        numbers, request.minimum, request.maximum, *palette, color_table_);
    if (!mapped.has_value())
      return R::failure(mapped.error());
    result.minimum = mapped.value().range.minimum;
    result.maximum = mapped.value().range.maximum;
    std::map<std::size_t, std::int32_t> residue_color;
    for (std::size_t position = 0; position < selected.size(); ++position) {
      const auto atom = selected[position];
      auto color = mapped.value().colors[position];
      if (request.by_residue) {
        const auto residue = topology.atoms()[atom].residue.value;
        color = residue_color.try_emplace(residue, color).first->second;
      }
      if (const auto error = add(atom, color); error.has_value())
        return R::failure(*error);
    }
  } else {
    std::vector<std::optional<double>> selected_values;
    selected_values.reserve(selected.size());
    for (const auto atom : selected)
      selected_values.push_back(values.value()[atom]);
    const auto mapped = render::color_list_spectrum(
        selected_values, request.minimum, request.maximum, color_indices,
        interpolation.value(), color_table_);
    if (!mapped.has_value())
      return R::failure(mapped.error());
    result.minimum = mapped.value().range.minimum;
    result.maximum = mapped.value().range.maximum;
    for (std::size_t position = 0; position < selected.size(); ++position)
      if (const auto color = mapped.value().colors[position]; color.has_value())
        if (const auto error = add(selected[position], *color);
            error.has_value())
          return R::failure(*error);
  }
  result.affected_atom_count = updates.size();
  if (const auto error = commit_atom_colors(*object, std::move(updates));
      error.has_value())
    return R::failure(*error);
  result.colored_atom_count =
      object->atom_colors == nullptr ? 0U : object->atom_colors->size();
  result.representation_count = object->representations.size();
  return R::success(std::move(result));
}

operation::Result<LabelResult>
Workspace::label(std::string selection_expression, std::string_view expression,
                 bool literal) {
  using R = operation::Result<LabelResult>;
  auto *object = mutable_active_object();
  if (object == nullptr)
    return R::failure(missing_active());
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value())
    return R::failure(mask.error());
  std::optional<LabelExpression> program;
  if (!literal) {
    auto parsed = LabelExpression::parse(expression);
    if (!parsed.has_value())
      return R::failure(parsed.error());
    program = std::move(parsed.value());
  }
  const auto frame_index = object->trajectory.has_value()
                               ? object->trajectory->timeline.snapshot().frame
                               : 0U;
  const auto digits = render_settings_.resolve(
      "label_digits", {object->id, frame_index, 0U, 0U});
  if (!digits.has_value())
    return R::failure(digits.error());
  const auto &topology = *object->system->topology();
  const auto label_frame = active_frame(*object);
  if (!label_frame.has_value())
    return R::failure(label_frame.error());
  const LabelExpression::AtomContext context{
      &topology, object->system->name(),
      std::get<std::int64_t>(digits.value().value), label_frame.value().get()};
  std::vector<std::pair<std::uint64_t, std::string>> updates;
  for (std::size_t atom = 0; atom < mask.value().size(); ++atom) {
    if (mask.value()[atom] == 0U)
      continue;
    const auto id = topology.atom_id(model::AtomIndex{atom});
    if (!id.has_value())
      return R::failure(invalid("topology atom identity is missing"));
    std::string text{expression};
    if (program.has_value()) {
      auto evaluated = program->evaluate(context, atom);
      if (!evaluated.has_value())
        return R::failure(evaluated.error());
      text = std::move(evaluated.value());
    }
    updates.emplace_back(id->value, std::move(text));
  }
  LabelResult result;
  result.object_id = object->id;
  result.affected_atom_count = updates.size();
  if (!updates.empty())
    result.first_text = updates.front().second;
  std::sort(updates.begin(), updates.end());
  auto merged =
      std::make_shared<std::vector<std::pair<std::uint64_t, std::string>>>();
  const auto &existing =
      object->atom_labels != nullptr
          ? *object->atom_labels
          : std::vector<std::pair<std::uint64_t, std::string>>{};
  auto left = existing.begin();
  auto right = updates.begin();
  while (left != existing.end() || right != updates.end()) {
    std::pair<std::uint64_t, std::string> item;
    if (right == updates.end() ||
        (left != existing.end() && left->first < right->first)) {
      item = *left++;
    } else {
      if (left != existing.end() && left->first == right->first)
        ++left;
      item = *right++;
    }
    if (!item.second.empty())
      merged->push_back(std::move(item));
  }
  auto candidate = *object;
  candidate.atom_labels =
      merged->empty() ? nullptr
                      : std::shared_ptr<const std::vector<
                            std::pair<std::uint64_t, std::string>>>{merged};
  if (const auto error = candidate.representation_visibility.apply(
          render::RepresentationKind::labels, mask.value(),
          RepresentationVisibilityMutation::show);
      error.has_value())
    return R::failure(*error);
  const auto frame = active_frame(candidate);
  if (!frame.has_value())
    return R::failure(frame.error());
  auto rebuilt =
      rebuild_representations(candidate, candidate.system, *frame.value(),
                              frame_index, render_settings_, color_table_);
  if (!rebuilt.has_value())
    return R::failure(rebuilt.error());
  object->atom_labels = std::move(candidate.atom_labels);
  object->representation_visibility =
      std::move(candidate.representation_visibility);
  object->representations = std::move(rebuilt.value());
  result.labeled_atom_count =
      object->atom_labels == nullptr ? 0U : object->atom_labels->size();
  for (const auto &record : object->representations)
    if (record.kind == render::RepresentationKind::labels)
      result.visible_label_count = record.packet.labels.size();
  return R::success(std::move(result));
}

AnalysisOverlayStyle Workspace::analysis_overlay_style() const {
  const render::RenderSettingContext global{};
  const auto setting = [&](std::string_view name) {
    return render_settings_.resolve(name, global).value().value;
  };
  AnalysisOverlayStyle style;
  style.dash_length = std::get<double>(setting("dash_length"));
  style.dash_gap = std::get<double>(setting("dash_gap"));
  style.dash_width = static_cast<float>(std::get<double>(setting("dash_width")));
  style.angle_size = std::get<double>(setting("angle_size"));
  style.dihedral_size = std::get<double>(setting("dihedral_size"));
  if (const auto index =
          color_table_.resolve(std::get<std::string>(setting("dash_color")));
      index.has_value())
    style.dash_color = color_table_.rgba(index.value(), 1.0F);
  if (const auto *object = active_object(); object != nullptr) {
    const auto frame = active_frame(*object);
    if (frame.has_value() && frame.value()->metadata().coordinate_unit ==
                                 operation::LengthUnit::nanometer)
      style.units_per_angstrom = 0.1;
  }
  return style;
}

render::RenderPacket Workspace::analysis_overlay_packet() const {
  return build_analysis_overlay_packet(analysis_results_.records(),
                                       analysis_overlay_style());
}

operation::Result<ImageSaveResult>
Workspace::save_image(const ImageSaveRequest &request) const {
  using R = operation::Result<ImageSaveResult>;
  if (!camera_.has_value() || objects_.empty())
    return R::failure(operation::Error{
        operation::ErrorCode::not_found, "no scene is available to render",
        "load a structure before saving an image"});
  if (request.path.empty())
    return R::failure(invalid("image path must not be empty"));
  if (request.dpi.has_value() &&
      (!std::isfinite(*request.dpi) || *request.dpi <= 0.0))
    return R::failure(invalid("image dpi must be positive"));
  constexpr std::size_t kMaximumEdge = 8192U;
  auto parameters = camera_->parameters();
  const auto width = request.width == 0U ? std::size_t{640U} : request.width;
  const auto height =
      request.height != 0U
          ? request.height
          : std::max<std::size_t>(
                1U, static_cast<std::size_t>(std::lround(
                        static_cast<double>(width) /
                        std::max(1.0e-6, parameters.aspect_ratio))));
  if (width > kMaximumEdge || height > kMaximumEdge)
    return R::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "image dimensions exceed the 8192 pixel limit", {}});
  parameters.aspect_ratio =
      static_cast<double>(width) / static_cast<double>(height);
  const auto camera = scene::Camera::create(parameters);
  if (!camera.has_value())
    return R::failure(camera.error());

  render::RenderPacket scene;
  const auto append = [&scene](const render::RenderPacket &packet) {
    const auto base = static_cast<std::uint32_t>(scene.mesh_vertices.size());
    scene.lines.insert(scene.lines.end(), packet.lines.begin(), packet.lines.end());
    scene.cylinders.insert(scene.cylinders.end(), packet.cylinders.begin(),
                           packet.cylinders.end());
    scene.spheres.insert(scene.spheres.end(), packet.spheres.begin(),
                         packet.spheres.end());
    scene.labels.insert(scene.labels.end(), packet.labels.begin(),
                        packet.labels.end());
    scene.mesh_vertices.insert(scene.mesh_vertices.end(),
                               packet.mesh_vertices.begin(),
                               packet.mesh_vertices.end());
    for (auto triangle : packet.mesh_triangles) {
      triangle.first += base;
      triangle.second += base;
      triangle.third += base;
      scene.mesh_triangles.push_back(triangle);
    }
  };
  for (const auto &info : list_objects()) {
    if (!info.effectively_visible)
      continue;
    const auto found = std::ranges::find(objects_, info.id, &WorkspaceObject::id);
    if (found == objects_.end())
      continue;
    for (const auto &record : found->representations)
      append(record.packet);
    if (found->molecular_surface.has_value())
      append(*found->molecular_surface);
  }
  for (const auto &volume : volumes_) {
    if (!scene_->effectively_visible(volume.scene_node))
      continue;
    for (const auto &packet : volume.representations)
      append(packet);
    for (const auto &contour : volume.contours)
      append(contour.packet);
  }
  const render::RenderSettingContext global{};
  const auto setting = [&](std::string_view name) {
    return render_settings_.resolve(name, global).value().value;
  };
  append(analysis_overlay_packet());
  const auto opaque = std::get<bool>(setting("opaque_background"));
  const auto ray_opaque = std::get<std::int64_t>(setting("ray_opaque_background"));
  const auto effective_opaque =
      request.ray && ray_opaque >= 0 ? ray_opaque != 0 : opaque;
  const auto light =
      std::get<render::RenderSettingVector3>(setting("light"));
  render::ReferenceRenderSettings settings;
  settings.width = width;
  settings.height = height;
  const auto background = color_table_.background_rgb();
  settings.background = {background.red, background.green, background.blue,
                         effective_opaque ? 1.0F : 0.0F};
  settings.ambient_light =
      std::clamp(std::get<double>(setting("ambient")), 0.0, 1.0);
  settings.light_direction = {-light[0], -light[1], -light[2]};
  auto image = render::render_reference(scene, camera.value(), settings);
  if (!image.has_value())
    return R::failure(image.error());
  if (const auto error = render::write_png_atomic(request.path, image.value(),
                                                  request.dpi);
      error.has_value())
    return R::failure(*error);

  ImageSaveResult result;
  result.path = request.path;
  result.width = width;
  result.height = height;
  std::error_code size_error;
  result.bytes = static_cast<std::size_t>(
      std::filesystem::file_size(request.path, size_error));
  result.renderer = "molshredder-reference-raycast";
  result.opaque_background = effective_opaque;
  result.primitive_count = scene.lines.size() + scene.cylinders.size() +
                           scene.spheres.size() + scene.mesh_triangles.size();
  result.warnings.push_back(
      request.ray
          ? "PyMOL ray tracing is approximated by the reference ray caster"
          : "PyMOL OpenGL draw is approximated by the reference ray caster");
  if (const auto renderer = std::get<std::int64_t>(setting("ray_default_renderer"));
      request.ray && renderer != 0)
    result.warnings.push_back("ray_default_renderer " + std::to_string(renderer) +
                              " is not available; the reference ray caster was used");
  if (!scene.labels.empty())
    result.warnings.push_back(std::to_string(scene.labels.size()) +
                              " text labels are not rasterized");
  return R::success(std::move(result));
}

operation::Result<CartoonTypeResult>
Workspace::set_cartoon_type(std::string_view type,
                            std::string selection_expression) {
  using R = operation::Result<CartoonTypeResult>;
  auto *object = mutable_active_object();
  if (object == nullptr)
    return R::failure(missing_active());
  const render::CartoonTypeName *match = nullptr;
  std::size_t prefix_matches{};
  for (const auto &candidate : render::kCartoonTypes) {
    if (candidate.name == type) {
      match = &candidate;
      prefix_matches = 1U;
      break;
    }
    if (!type.empty() && candidate.name.starts_with(type)) {
      match = &candidate;
      ++prefix_matches;
    }
  }
  if (match == nullptr || prefix_matches != 1U)
    return R::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "unknown type: '" + std::string{type} + "'",
        "choices: arrow cylinder dumbbell oval rectangle tube automatic dash "
        "loop putty skip"});
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value())
    return R::failure(mask.error());
  const auto &topology = *object->system->topology();
  std::map<std::uint64_t, std::int32_t> types;
  if (object->cartoon_types != nullptr)
    types.insert(object->cartoon_types->begin(), object->cartoon_types->end());
  std::size_t affected{};
  for (std::size_t atom = 0; atom < mask.value().size(); ++atom) {
    if (mask.value()[atom] == 0U)
      continue;
    const auto id = topology.atom_id(model::AtomIndex{atom});
    if (!id.has_value())
      return R::failure(invalid("topology atom identity is missing"));
    ++affected;
    if (match->code == 0)
      types.erase(id->value);
    else
      types[id->value] = match->code;
  }
  auto candidate = *object;
  candidate.cartoon_types =
      types.empty()
          ? nullptr
          : std::make_shared<const std::vector<std::pair<std::uint64_t, std::int32_t>>>(
                types.begin(), types.end());
  const auto frame = active_frame(candidate);
  if (!frame.has_value())
    return R::failure(frame.error());
  const auto frame_index = candidate.trajectory.has_value()
                               ? candidate.trajectory->timeline.snapshot().frame
                               : 0U;
  auto rebuilt =
      rebuild_representations(candidate, candidate.system, *frame.value(),
                              frame_index, render_settings_, color_table_);
  if (!rebuilt.has_value())
    return R::failure(rebuilt.error());
  object->cartoon_types = std::move(candidate.cartoon_types);
  object->representations = std::move(rebuilt.value());
  return R::success({object->id, std::string{match->name}, match->code,
                     affected, object->representations.size()});
}

operation::Result<ContourResult>
Workspace::create_contour(const ContourRequest &request,
                          operation::TaskContext &context) {
  using R = operation::Result<ContourResult>;
  if (request.name.empty())
    return R::failure(invalid("contour name must not be empty"));
  if (!std::isfinite(request.level) || !std::isfinite(request.carve) ||
      !std::isfinite(request.buffer) || request.carve < 0.0 ||
      request.buffer < 0.0)
    return R::failure(invalid("contour level, carve and buffer must be finite "
                              "(carve and buffer non-negative)"));
  WorkspaceVolume *volume = nullptr;
  for (auto &candidate : volumes_)
    if ((request.map == "current" && active_volume() == &candidate) ||
        candidate.name == request.map ||
        std::to_string(candidate.id) == request.map)
      volume = &candidate;
  if (volume == nullptr)
    return R::failure(operation::Error{
        operation::ErrorCode::not_found,
        "Map object \"" + request.map + "\" not found",
        "load a volume map and pass its name with --map"});
  for (const auto &other : volumes_)
    for (const auto &contour : other.contours)
      if (contour.name == request.name && contour.mode != request.mode)
        return R::failure(operation::Error{
            operation::ErrorCode::invalid_argument,
            "Object name " + request.name +
                " already exists as another type. Cannot overwrite.",
            "choose another contour name"});
  ContourRecord record;
  record.name = request.name;
  record.mode = request.mode;
  record.level = static_cast<float>(request.level);
  if (request.color == "default") {
    record.color = render::kColorDefault;
  } else {
    const auto color = color_table_.resolve_assignment(request.color);
    if (!color.has_value())
      return R::failure(color.error());
    record.color = color.value();
  }
  record.extent = grid_extent(*volume->grid);
  if (!request.selection.empty()) {
    const auto *object = active_object();
    if (object == nullptr)
      return R::failure(missing_active());
    const auto mask = selection_mask(*object, request.selection);
    if (!mask.has_value())
      return R::failure(mask.error());
    const auto frame = active_frame(*object);
    if (!frame.has_value())
      return R::failure(frame.error());
    render::Bounds3d region;
    for (std::size_t atom = 0; atom < mask.value().size(); ++atom) {
      if (mask.value()[atom] == 0U || !frame.value()->atom_present(atom))
        continue;
      const auto point = std::visit(
          [atom](const auto &values) {
            return model::Vec3d{static_cast<double>(values[atom].x),
                                static_cast<double>(values[atom].y),
                                static_cast<double>(values[atom].z)};
          },
          frame.value()->positions().values());
      record.region_atoms.push_back(point);
      render::include(region, point);
    }
    if (record.region_atoms.empty())
      return R::failure(operation::Error{operation::ErrorCode::invalid_selection,
                                         "contour selection selects no atoms", {}});
    const auto pad = request.carve > 0.0 ? request.carve : request.buffer;
    record.carve = request.carve;
    record.buffer = request.carve > 0.0 ? 0.0 : request.buffer;
    record.extent = {{region.minimum.x - pad, region.minimum.y - pad, region.minimum.z - pad},
                     {region.maximum.x + pad, region.maximum.y + pad, region.maximum.z + pad},
                     false};
  }
  auto packet = build_contour_packet(*volume, record, color_table_, context);
  if (!packet.has_value())
    return R::failure(packet.error());
  record.packet = std::move(packet.value());
  ContourResult result{volume->id, record.name, record.mode,
                       static_cast<double>(record.level), false,
                       record.packet.lines.size() + record.packet.spheres.size() +
                           record.packet.mesh_triangles.size(),
                       record.extent,
                       {}};
  if (const auto algorithm =
          render_settings_.resolve("isosurface_algorithm", render::RenderSettingContext{});
      algorithm.has_value()) {
    if (const auto *code = std::get_if<std::int64_t>(&algorithm.value().value);
        code != nullptr && *code != 0)
      result.warnings.push_back(
          "isosurface_algorithm " + std::to_string(*code) +
          " is not implemented; the contour uses MolShredder marching tetrahedra");
  }
  for (auto &other : volumes_)
    std::erase_if(other.contours, [&](const auto &contour) {
      if (contour.name != record.name)
        return false;
      result.replaced = true;
      return true;
    });
  volume->contours.push_back(std::move(record));
  ++volume->presentation_revision;
  return R::success(std::move(result));
}

operation::Result<ContourResult>
Workspace::contour_level(std::string_view name, std::optional<double> level,
                         operation::TaskContext &context) {
  using R = operation::Result<ContourResult>;
  for (auto &volume : volumes_)
    for (auto &contour : volume.contours) {
      if (contour.name != name)
        continue;
      if (level.has_value()) {
        if (!std::isfinite(*level))
          return R::failure(invalid("contour level must be finite"));
        auto candidate = contour;
        candidate.level = static_cast<float>(*level);
        auto packet = build_contour_packet(volume, candidate, color_table_, context);
        if (!packet.has_value())
          return R::failure(packet.error());
        candidate.packet = std::move(packet.value());
        contour = std::move(candidate);
        ++volume.presentation_revision;
      }
      return R::success({volume.id, contour.name, contour.mode,
                         static_cast<double>(contour.level), false,
                         contour.packet.lines.size() + contour.packet.spheres.size() +
                             contour.packet.mesh_triangles.size(),
                         contour.extent, {}});
    }
  return R::failure(operation::Error{operation::ErrorCode::not_found,
                                     "Object not found", "use a contour name"});
}

namespace {

operation::Result<std::vector<float>> parse_ramp_levels(std::string_view text) {
  using R = operation::Result<std::vector<float>>;
  std::string normalized{text};
  for (auto &item : normalized)
    if (item == '[' || item == ']' || item == ',')
      item = ' ';
  std::istringstream stream{normalized};
  std::vector<float> levels;
  for (std::string token; stream >> token;) {
    double value{};
    const auto parsed = core::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() ||
        !std::isfinite(value))
      return R::failure(operation::Error{operation::ErrorCode::invalid_argument,
                                         "invalid range", "use numeric levels"});
    levels.push_back(static_cast<float>(value));
  }
  if (levels.empty())
    return R::failure(operation::Error{operation::ErrorCode::invalid_argument,
                                       "invalid range", "use numeric levels"});
  return R::success(std::move(levels));
}

struct RampColors {
  std::vector<render::ColorRgb> colors;
  int spectrum{};
};

operation::Result<RampColors> parse_ramp_colors(std::string_view text,
                                                const render::ColorTable &table) {
  using R = operation::Result<RampColors>;
  static constexpr std::array<std::string_view, 8> spectra{
      "traditional", "sludge", "ocean", "hot", "grayable", "rainbow", "afmhot",
      "grayscale"};
  std::string normalized{text};
  const auto looks_like_list = normalized.find_first_of("[, ") != std::string::npos;
  for (auto &item : normalized)
    if (item == '[' || item == ']' || item == ',')
      item = ' ';
  std::istringstream stream{normalized};
  std::vector<std::string> tokens;
  for (std::string token; stream >> token;)
    tokens.push_back(token);
  RampColors result;
  if (!looks_like_list && tokens.size() == 1U) {
    std::size_t matches{};
    for (std::size_t index = 0; index < spectra.size(); ++index)
      if (spectra[index] == tokens.front()) {
        result.spectrum = static_cast<int>(index) + 1;
        matches = 1U;
        break;
      } else if (spectra[index].starts_with(tokens.front())) {
        result.spectrum = static_cast<int>(index) + 1;
        ++matches;
      }
    if (matches != 1U)
      return R::failure(operation::Error{
          operation::ErrorCode::invalid_argument,
          "unknown ramp color spectrum: '" + tokens.front() + "'",
          "choices: afmhot grayscale rainbow traditional grayable hot ocean "
          "sludge, or a color list"});
    return R::success(std::move(result));
  }
  for (const auto &token : tokens) {
    const auto index = table.resolve(token);
    const auto rgb = index.has_value() ? table.rgb(index.value()) : std::nullopt;
    if (!rgb.has_value())
      return R::failure(operation::Error{operation::ErrorCode::invalid_argument,
                                         "unknown ramp color: " + token,
                                         "special ramp colors are not supported"});
    result.colors.push_back(*rgb);
  }
  if (result.colors.empty())
    return R::failure(invalid("ramp colors must not be empty"));
  return R::success(std::move(result));
}

// Proximity ramp source atoms: PyMOL measures against state 1 of the source
// regardless of the current frame, using its current coordinates.
operation::Result<std::vector<model::Vec3d>>
ramp_source_atoms(const WorkspaceObject &object) {
  using R = operation::Result<std::vector<model::Vec3d>>;
  const auto frame = object.system->coordinates()->read_frame(0U);
  if (!frame.has_value())
    return R::failure(frame.error());
  std::vector<model::Vec3d> atoms;
  for (std::size_t atom = 0; atom < frame.value()->atom_count(); ++atom) {
    if (!frame.value()->atom_present(atom))
      continue;
    atoms.push_back(std::visit(
        [atom](const auto &values) {
          return model::Vec3d{static_cast<double>(values[atom].x),
                              static_cast<double>(values[atom].y),
                              static_cast<double>(values[atom].z)};
        },
        frame.value()->positions().values()));
  }
  return R::success(std::move(atoms));
}

} // namespace

void Workspace::refresh_proximity_ramps(std::uint64_t object_id) {
  const auto object = std::find_if(
      objects_.begin(), objects_.end(),
      [object_id](const auto &item) { return item.id == object_id; });
  if (object == objects_.end())
    return;
  auto candidate = color_table_;
  bool changed = false;
  for (auto code = render::kColorRampFirst; color_table_.ramp(code) != nullptr;
       --code) {
    auto ramp = *color_table_.ramp(code);
    if (ramp.source_object != object_id)
      continue;
    auto atoms = ramp_source_atoms(*object);
    if (!atoms.has_value() || atoms.value() == ramp.atoms)
      continue;
    ramp.atoms = std::move(atoms.value());
    static_cast<void>(candidate.define_ramp(std::move(ramp)));
    changed = true;
  }
  // The coordinate edit is already committed; a failed recolor keeps the
  // previous ramp atoms rather than undoing the edit.
  if (changed)
    static_cast<void>(commit_color_table(std::move(candidate)));
}

operation::Result<RampResult>
Workspace::define_ramp(std::string name, std::string_view source,
                       std::string_view range, std::string_view colors) {
  using R = operation::Result<RampResult>;
  if (name.empty())
    return R::failure(invalid("ramp name must not be empty"));
  render::ColorRamp ramp;
  ramp.name = name;
  std::string kind;
  for (const auto &volume : volumes_)
    if (volume.name == source || std::to_string(volume.id) == source) {
      ramp.map = volume.grid;
      kind = "map";
    }
  if (ramp.map == nullptr) {
    for (const auto &object : objects_) {
      if (object.system->name() != source && std::to_string(object.id) != source)
        continue;
      auto atoms = ramp_source_atoms(object);
      if (!atoms.has_value())
        return R::failure(atoms.error());
      ramp.atoms = std::move(atoms.value());
      ramp.source_object = object.id;
      kind = "molecule";
    }
  }
  if (kind.empty())
    return R::failure(operation::Error{operation::ErrorCode::not_found,
                                       std::string{source} + " not found.",
                                       "use a loaded map or molecular object"});
  auto levels = parse_ramp_levels(range.empty() ? std::string_view{"-1 0 1"} : range);
  if (!levels.has_value())
    return R::failure(levels.error());
  auto parsed = parse_ramp_colors(colors.empty() ? std::string_view{"red white blue"}
                                                 : colors,
                                  color_table_);
  if (!parsed.has_value())
    return R::failure(parsed.error());
  ramp.levels = std::move(levels.value());
  ramp.colors = std::move(parsed.value().colors);
  ramp.spectrum = parsed.value().spectrum;
  auto candidate = color_table_;
  const auto existed = candidate.ramp_code(name).has_value();
  RampResult result{name, 0, kind, {}, ramp.colors.size(), ramp.spectrum, existed};
  for (const auto level : ramp.levels)
    result.levels.push_back(static_cast<double>(level));
  result.color_index = candidate.define_ramp(std::move(ramp));
  if (const auto error = commit_color_table(std::move(candidate)); error.has_value())
    return R::failure(*error);
  return R::success(std::move(result));
}

operation::Result<RampResult>
Workspace::update_ramp(std::string_view name, std::string_view range,
                       std::string_view colors) {
  using R = operation::Result<RampResult>;
  const auto code = color_table_.ramp_code(name);
  if (!code.has_value())
    return R::failure(operation::Error{
        operation::ErrorCode::not_found,
        "Missing 'range' or 'color' to create new ramp.",
        "create the ramp with ramp new first"});
  auto ramp = *color_table_.ramp(*code);
  if (!range.empty()) {
    auto levels = parse_ramp_levels(range);
    if (!levels.has_value())
      return R::failure(levels.error());
    ramp.levels = std::move(levels.value());
  }
  if (!colors.empty()) {
    auto parsed = parse_ramp_colors(colors, color_table_);
    if (!parsed.has_value())
      return R::failure(parsed.error());
    ramp.colors = std::move(parsed.value().colors);
    ramp.spectrum = parsed.value().spectrum;
  }
  RampResult result{ramp.name, *code, ramp.map != nullptr ? "map" : "molecule",
                    {}, ramp.colors.size(), ramp.spectrum, true};
  for (const auto level : ramp.levels)
    result.levels.push_back(static_cast<double>(level));
  auto candidate = color_table_;
  static_cast<void>(candidate.define_ramp(std::move(ramp)));
  if (const auto error = commit_color_table(std::move(candidate)); error.has_value())
    return R::failure(*error);
  return R::success(std::move(result));
}

operation::Result<BackgroundResult>
Workspace::set_background(std::string_view color) {
  using R = operation::Result<BackgroundResult>;
  std::optional<std::int32_t> index;
  const auto lowered_default =
      color.size() == 7U &&
      std::equal(color.begin(), color.end(), "default",
                 [](char left, char right) {
                   return std::tolower(static_cast<unsigned char>(left)) == right;
                 });
  if (!lowered_default) {
    const auto resolved = color_table_.resolve_assignment(color);
    if (!resolved.has_value())
      return R::failure(resolved.error());
    if (!color_table_.rgb(resolved.value()).has_value())
      return R::failure(operation::Error{
          operation::ErrorCode::invalid_argument,
          "background color must resolve to an RGB color: " + std::string{color},
          "use a color name, index, 0xRRGGBB, #RRGGBB or default"});
    index = resolved.value();
  }
  auto candidate = color_table_;
  if (const auto error = candidate.set_background(index); error.has_value())
    return R::failure(invalid(*error));
  if (const auto error = commit_color_table(std::move(candidate));
      error.has_value())
    return R::failure(*error);
  return R::success(background());
}

BackgroundResult Workspace::background() const {
  const auto index = color_table_.background();
  return {index,
          index.has_value() ? color_table_.name(*index).value_or("")
                            : std::string{"default"},
          color_table_.background_rgb()};
}

std::optional<operation::Error>
Workspace::commit_color_table(render::ColorTable candidate) {
  std::vector<std::vector<RepresentationRecord>> rebuilt;
  rebuilt.reserve(objects_.size());
  for (const auto &object : objects_) {
    const auto frame = active_frame(object);
    if (!frame.has_value())
      return frame.error();
    const auto frame_index = object.trajectory.has_value()
                                 ? object.trajectory->timeline.snapshot().frame
                                 : 0U;
    auto records =
        rebuild_representations(object, object.system, *frame.value(),
                                frame_index, render_settings_, candidate);
    if (!records.has_value())
      return records.error();
    rebuilt.push_back(std::move(records.value()));
  }
  color_table_ = std::move(candidate);
  for (std::size_t index = 0; index < objects_.size(); ++index)
    objects_[index].representations = std::move(rebuilt[index]);
  return std::nullopt;
}

operation::Result<LoadResult>
Workspace::load_structure(const std::filesystem::path &path,
                          std::optional<std::string> name,
                          io::StructureFormat format) {
  auto document = io::read_structure_file(path, format);
  if (!document.has_value()) {
    return operation::Result<LoadResult>::failure(document.error());
  }
  return load_structure_document(std::move(document.value()), path,
                                 std::move(name));
}

operation::Result<LoadResult> Workspace::load_structure_document(
    io::StructureDocument document, const std::filesystem::path &source_path,
    std::optional<std::string> name) {
  const auto format = document.format;
  operation::TaskContext context;
  auto loaded = load_structure_documents(
      {{std::move(document), source_path, std::move(name)}}, context);
  if (!loaded.has_value())
    return operation::Result<LoadResult>::failure(loaded.error());
  return operation::Result<LoadResult>::success(
      {loaded.value().object_id, loaded.value().object_name,
       loaded.value().atom_count, loaded.value().frame_count, format,
       std::move(loaded.value().objects)});
}

operation::Result<BatchLoadResult> Workspace::load_structure_batch(
    std::span<const StructureLoadRequest> requests,
    operation::TaskContext &context) {
  if (requests.empty()) {
    return operation::Result<BatchLoadResult>::failure(
        invalid("structure batch requires at least one input"));
  }
  std::vector<StructureDocumentLoadRequest> documents;
  try {
    documents.reserve(requests.size());
    for (std::size_t index = 0; index < requests.size(); ++index) {
      if (context.cancellation.is_cancelled()) {
        return operation::Result<BatchLoadResult>::failure(operation::Error{
            operation::ErrorCode::cancelled,
            "structure batch cancelled before input " +
                std::to_string(index + 1U),
            {}});
      }
      auto document =
          io::read_structure_file(requests[index].path, requests[index].format);
      if (!document.has_value()) {
        auto error = document.error();
        error.message = "structure batch input " +
                        std::to_string(index + 1U) + " failed: " +
                        error.message;
        error.details["input_index"] = std::to_string(index);
        error.details["path"] = requests[index].path.string();
        return operation::Result<BatchLoadResult>::failure(std::move(error));
      }
      documents.push_back({std::move(document.value()), requests[index].path,
                           requests[index].name});
      if (context.report_progress) {
        context.report_progress(
            {0.8 * static_cast<double>(index + 1U) /
                 static_cast<double>(requests.size()),
             "structure-batch-parse"});
      }
    }
    return load_structure_documents(std::move(documents), context);
  } catch (const std::bad_alloc &) {
    return operation::Result<BatchLoadResult>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "structure batch allocation failed before commit",
        "reduce the batch size or available structure data"});
  }
}

operation::Result<BatchLoadResult> Workspace::load_structure_documents(
    std::vector<StructureDocumentLoadRequest> requests,
    operation::TaskContext &context) {
  if (requests.empty()) {
    return operation::Result<BatchLoadResult>::failure(
        invalid("structure batch requires at least one document"));
  }
  std::size_t structure_count{};
  std::vector<io::StructureFormat> formats;
  formats.reserve(requests.size());
  for (const auto &request : requests) {
    if (request.document.structures.empty()) {
      return operation::Result<BatchLoadResult>::failure(
          invalid("structure document contains no data blocks"));
    }
    if (request.name.has_value() && request.name->empty()) {
      return operation::Result<BatchLoadResult>::failure(
          invalid("explicit workspace object name must not be empty",
                  "omit the name to use names from the file"));
    }
    if (request.document.structures.size() >
        std::numeric_limits<std::size_t>::max() - structure_count) {
      return operation::Result<BatchLoadResult>::failure(operation::Error{
          operation::ErrorCode::resource_exhausted,
          "structure batch size exceeds host index range", {}});
    }
    structure_count += request.document.structures.size();
    formats.push_back(request.document.format);
  }
  if (structure_count >
      std::numeric_limits<std::uint64_t>::max() - next_object_id_) {
    return operation::Result<BatchLoadResult>::failure(
        invalid("structure import would overflow the object identifier space"));
  }

  struct CandidateRecord {
    std::size_t input_index{};
    std::size_t record_index{};
    std::string name;
  };

  std::vector<CandidateRecord> candidates;
  std::vector<std::string> object_names;
  candidates.reserve(structure_count);
  object_names.reserve(structure_count);
  for (std::size_t input_index = 0; input_index < requests.size();
       ++input_index) {
    const auto &request = requests[input_index];
    for (std::size_t record_index = 0;
         record_index < request.document.structures.size(); ++record_index) {
      auto object_name = request.name.has_value()
                             ? request.name.value()
                             : request.document.structures[record_index].name;
      if (request.name.has_value() &&
          request.document.structures.size() > 1U) {
        object_name += "_" + std::to_string(record_index + 1U);
      }
      if (object_name.empty()) {
        object_name = request.source_path.stem().string();
        if (request.document.structures.size() > 1U) {
          object_name += "_" + std::to_string(record_index + 1U);
        }
      }
      if (object_name.empty()) {
        return operation::Result<BatchLoadResult>::failure(
            invalid("loaded object requires a non-empty name"));
      }
      const auto existing =
          std::any_of(objects_.begin(), objects_.end(),
                      [&](const auto &object) {
                        return object.system->name() == object_name;
                      }) ||
          std::any_of(volumes_.begin(), volumes_.end(), [&](const auto &volume) {
            return volume.name == object_name;
          });
      const auto duplicate_in_batch =
          std::find(object_names.begin(), object_names.end(), object_name) !=
          object_names.end();
      if (existing || duplicate_in_batch) {
        return operation::Result<BatchLoadResult>::failure(
            invalid("workspace object name already exists: " + object_name,
                    "choose a unique name"));
      }
      object_names.push_back(object_name);
      candidates.push_back(
          {input_index, record_index, std::move(object_name)});
    }
  }

  auto scene_builder = scene::SceneBuilder::from(*scene_);
  std::vector<WorkspaceObject> pending_objects;
  std::vector<LoadedObjectResult> loaded_objects;
  pending_objects.reserve(structure_count);
  loaded_objects.reserve(structure_count);
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    if (context.cancellation.is_cancelled()) {
      return operation::Result<BatchLoadResult>::failure(operation::Error{
          operation::ErrorCode::cancelled,
          "structure batch cancelled while building candidates", {}});
    }
    const auto object_id = next_object_id_ + index;
    const auto &candidate = candidates[index];
    const auto &request = requests[candidate.input_index];
    const auto &data = request.document.structures[candidate.record_index];
    auto coordinate_source = data.coordinates;
    std::optional<TrajectoryState> embedded_trajectory;
    const auto frame_count = data.coordinates->frame_count().value_or(0U);
    if (frame_count > 1U) {
      constexpr std::size_t kEmbeddedCacheBudgetBytes = 256U * 1024U * 1024U;
      const auto cache = trajectory::FrameCache::create(
          data.coordinates, kEmbeddedCacheBudgetBytes);
      if (!cache.has_value()) {
        return operation::Result<BatchLoadResult>::failure(cache.error());
      }
      auto timeline = trajectory::PlaybackTimeline::create(frame_count);
      if (!timeline.has_value()) {
        return operation::Result<BatchLoadResult>::failure(timeline.error());
      }
      auto clock = trajectory::PlaybackClock::create();
      if (!clock.has_value()) {
        return operation::Result<BatchLoadResult>::failure(clock.error());
      }
      auto prefetch = trajectory::PrefetchScheduler::create(cache.value());
      if (!prefetch.has_value()) {
        return operation::Result<BatchLoadResult>::failure(prefetch.error());
      }
      coordinate_source = cache.value();
      embedded_trajectory = TrajectoryState{request.source_path,
                                            io::TrajectoryFormat::auto_detect,
                                            cache.value(),
                                            std::move(timeline.value()),
                                            std::move(clock.value()),
                                            prefetch.value(),
                                            4U,
                                            trajectory::AtomMappingReport{
                                                trajectory::AtomMappingPolicy::
                                                    index_order,
                                                "embedded-structure-order",
                                                {},
                                                {"structure-record-order"},
                                                data.topology->version()},
                                            {}};
    }
    const auto system = model::MolecularSystem::create(
        object_id, candidate.name, data.topology, coordinate_source);
    if (!system.has_value()) {
      return operation::Result<BatchLoadResult>::failure(system.error());
    }
    const auto node = scene_builder.add_system(
        scene_->root(), candidate.name, system.value());
    if (!node.has_value()) {
      return operation::Result<BatchLoadResult>::failure(node.error());
    }
    pending_objects.push_back(WorkspaceObject{
        object_id, node.value(), system.value(), 1U, 1U, {},
        RepresentationVisibilityState::create(data.topology->atom_count())
            .value(),
        {}, std::move(embedded_trajectory), std::nullopt,
        render::kColorDefault, nullptr, nullptr, nullptr});
    loaded_objects.push_back(LoadedObjectResult{
        object_id, candidate.name, data.topology->atom_count(),
        data.coordinates->frame_count().value_or(0U), candidate.record_index});
    if (context.report_progress) {
      context.report_progress(
          {0.8 + 0.19 * static_cast<double>(index + 1U) /
                     static_cast<double>(candidates.size()),
           "structure-batch-build"});
    }
  }
  if (const auto error =
          scene_builder.set_selection({pending_objects.back().scene_node});
      error.has_value()) {
    return operation::Result<BatchLoadResult>::failure(*error);
  }
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value()) {
    return operation::Result<BatchLoadResult>::failure(next_scene.error());
  }
  if (context.cancellation.is_cancelled()) {
    return operation::Result<BatchLoadResult>::failure(operation::Error{
        operation::ErrorCode::cancelled,
        "structure batch cancelled before commit", {}});
  }
  // Object colors come from the auto cycle. With auto_color on (Q-022 b),
  // atoms also get PyMOL load colors: carbon = object color, other elements
  // = element color; representations are rebuilt before anything commits.
  const auto cycle = render::ColorTable::auto_cycle();
  auto cursor = auto_color_cursor_;
  const auto pymol_load_colors = render_settings_.resolve(
      "auto_color", render::RenderSettingContext{});
  const auto *pymol_load_flag =
      pymol_load_colors.has_value()
          ? std::get_if<bool>(&pymol_load_colors.value().value)
          : nullptr;
  for (auto &pending : pending_objects) {
    pending.object_color = cycle[cursor % cycle.size()];
    cursor = (cursor + 1U) % cycle.size();
    if (pymol_load_flag == nullptr || !*pymol_load_flag)
      continue;
    const auto &topology = *pending.system->topology();
    std::vector<std::pair<std::uint64_t, std::int32_t>> colors;
    colors.reserve(topology.atom_count());
    for (std::size_t atom = 0; atom < topology.atom_count(); ++atom) {
      const auto id = topology.atom_id(model::AtomIndex{atom});
      if (!id.has_value())
        return operation::Result<BatchLoadResult>::failure(
            invalid("topology atom identity is missing"));
      const auto element = topology.atoms()[atom].atomic_number;
      const auto color = element == 6U
                             ? pending.object_color
                             : render::ColorTable::element_color(element);
      // Unknown elements get index 0 like PyMOL.
      colors.emplace_back(id->value, color);
    }
    std::sort(colors.begin(), colors.end());
    pending.atom_colors =
        std::make_shared<const std::vector<std::pair<std::uint64_t, std::int32_t>>>(
            std::move(colors));
    const auto frame = active_frame(pending);
    if (!frame.has_value())
      return operation::Result<BatchLoadResult>::failure(frame.error());
    auto rebuilt = rebuild_representations(pending, pending.system,
                                           *frame.value(), 0U, render_settings_,
                                           color_table_);
    if (!rebuilt.has_value())
      return operation::Result<BatchLoadResult>::failure(rebuilt.error());
    pending.representations = std::move(rebuilt.value());
  }
  // Reserve before publishing either the object vector or scene. Candidate
  // creation and every potentially allocating validation step completed above.
  objects_.reserve(objects_.size() + pending_objects.size());
  auto_color_cursor_ = cursor;
  objects_.insert(objects_.end(),
                  std::make_move_iterator(pending_objects.begin()),
                  std::make_move_iterator(pending_objects.end()));
  next_object_id_ += structure_count;
  active_index_ = objects_.size() - 1U;
  scene_ = next_scene.value();
  if (context.report_progress)
    context.report_progress({1.0, "structure-batch-commit"});
  const auto &active = loaded_objects.back();
  return operation::Result<BatchLoadResult>::success(BatchLoadResult{
      active.object_id, active.object_name, active.atom_count,
      active.frame_count, requests.size(), std::move(formats),
      std::move(loaded_objects)});
}

operation::Result<VolumeLoadResult>
Workspace::load_volume(const std::filesystem::path &path,
                       std::optional<std::string> name, io::VolumeFormat format,
                       operation::LengthUnit coordinate_unit) {
  if (name.has_value() && name->empty()) {
    return operation::Result<VolumeLoadResult>::failure(
        invalid("explicit volume object name must not be empty",
                "omit --name to use the file name"));
  }
  io::VolumeReadOptions options;
  options.format = format;
  options.coordinate_unit = coordinate_unit;
  options.name = name;
  auto document = io::read_volume_file(path, std::move(options));
  if (!document.has_value()) {
    return operation::Result<VolumeLoadResult>::failure(document.error());
  }
  return load_volume_document(std::move(document.value()), path);
}

operation::Result<VolumeLoadResult>
Workspace::load_volume_document(io::VolumeDocument document,
                                const std::filesystem::path &path) {
  if (document.volumes.size() != 1U) {
    return operation::Result<VolumeLoadResult>::failure(
        invalid("volume import requires exactly one scalar field"));
  }
  const auto &data = document.volumes.front();
  const auto duplicate_molecule =
      std::any_of(objects_.begin(), objects_.end(), [&](const auto &object) {
        return object.system->name() == data.name;
      });
  const auto duplicate_volume =
      std::any_of(volumes_.begin(), volumes_.end(),
                  [&](const auto &volume) { return volume.name == data.name; });
  if (duplicate_molecule || duplicate_volume) {
    return operation::Result<VolumeLoadResult>::failure(
        invalid("workspace object name already exists: " + data.name,
                "choose a unique --name"));
  }
  if (next_object_id_ == std::numeric_limits<std::uint64_t>::max()) {
    return operation::Result<VolumeLoadResult>::failure(
        invalid("volume import would overflow the object identifier space"));
  }
  auto scene_builder = scene::SceneBuilder::from(*scene_);
  const auto node =
      scene_builder.add_volume(scene_->root(), data.name, data.grid);
  if (!node.has_value()) {
    return operation::Result<VolumeLoadResult>::failure(node.error());
  }
  if (const auto error = scene_builder.set_selection({node.value()});
      error.has_value()) {
    return operation::Result<VolumeLoadResult>::failure(*error);
  }
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value()) {
    return operation::Result<VolumeLoadResult>::failure(next_scene.error());
  }
  const auto object_id = next_object_id_++;
  volumes_.push_back(WorkspaceVolume{object_id,
                                     node.value(),
                                     data.name,
                                     path,
                                     document.format,
                                     data.grid,
                                     1U,
                                     {},
                                     std::nullopt,
                                     {},
                                     nullptr,
                                     {}});
  active_volume_index_ = volumes_.size() - 1U;
  scene_ = next_scene.value();
  const auto [minimum, maximum] = data.grid->scalars().range();
  return operation::Result<VolumeLoadResult>::success(VolumeLoadResult{
      object_id, data.name, document.format, data.grid->shape(),
      data.grid->value_count(), data.grid->scalars().precision(),
      data.grid->origin(), data.grid->deltas(), minimum, maximum,
      data.grid->metadata().coordinate_unit});
}

WorkspaceVolume *Workspace::mutable_active_volume() noexcept {
  return active_volume_index_.has_value()
             ? &volumes_[active_volume_index_.value()]
             : nullptr;
}

const WorkspaceVolume *Workspace::active_volume() const noexcept {
  return active_volume_index_.has_value()
             ? &volumes_[active_volume_index_.value()]
             : nullptr;
}

operation::Result<VolumeIsosurfaceResult> Workspace::show_volume_isosurface(
    double level, render::ColorRgba color, bool replace_existing,
    operation::TaskContext &context) {
  auto *volume = mutable_active_volume();
  if (volume == nullptr) {
    return operation::Result<VolumeIsosurfaceResult>::failure(operation::Error{
        operation::ErrorCode::not_found, "no active volume object",
        "load a volume with volume load first"});
  }
  const render::IsosurfaceRequest request{
      volume->grid.get(), volume->scene_node.value, {level, color}, &context};
  auto packet = render::build_isosurface(request);
  if (!packet.has_value()) {
    return operation::Result<VolumeIsosurfaceResult>::failure(packet.error());
  }
  if (replace_existing) volume->representations.clear();
  const auto index = volume->representations.size();
  const auto vertex_count = packet.value().mesh_vertices.size();
  const auto triangle_count = packet.value().mesh_triangles.size();
  const auto bounds = packet.value().bounds;
  volume->representations.push_back(std::move(packet.value()));
  ++volume->presentation_revision;
  return operation::Result<VolumeIsosurfaceResult>::success(
      {volume->id, index, level, color, vertex_count, triangle_count, bounds});
}

operation::Result<VolumeSliceResult> Workspace::show_volume_slice(
    render::VolumeSliceStyle style, bool replace_existing,
    operation::TaskContext &context) {
  auto *volume = mutable_active_volume();
  if (volume == nullptr) {
    return operation::Result<VolumeSliceResult>::failure(operation::Error{
        operation::ErrorCode::not_found, "no active volume object",
        "load a volume with volume load first"});
  }
  const render::VolumeSliceRequest request{volume->grid.get(),
                                           volume->scene_node.value, style,
                                           &context};
  auto packet = render::build_volume_slice(request);
  if (!packet.has_value()) {
    return operation::Result<VolumeSliceResult>::failure(packet.error());
  }
  if (replace_existing)
    volume->representations.clear();
  const auto representation_index = volume->representations.size();
  const auto vertex_count = packet.value().mesh_vertices.size();
  const auto triangle_count = packet.value().mesh_triangles.size();
  const auto required_bytes = vertex_count * sizeof(render::MeshVertex) +
                              triangle_count * sizeof(render::MeshTriangle);
  const auto pick_target_count = packet.value().pick_targets.size();
  const auto bounds = packet.value().bounds;
  volume->representations.push_back(std::move(packet.value()));
  ++volume->presentation_revision;
  return operation::Result<VolumeSliceResult>::success(
      {volume->id,
       representation_index,
       style.axis,
       style.index,
       style.minimum_color,
       style.maximum_color,
       style.memory_budget_bytes,
       required_bytes,
       vertex_count,
       triangle_count,
       pick_target_count,
      bounds});
}

operation::Result<VolumeRampResult>
Workspace::set_volume_ramp(render::TransferPreset preset) {
  auto *volume = mutable_active_volume();
  if (volume == nullptr) {
    return operation::Result<VolumeRampResult>::failure(operation::Error{
        operation::ErrorCode::not_found, "no active volume object",
        "load a volume with volume load first"});
  }
  const auto [minimum, maximum] = volume->grid->scalars().range();
  auto transfer = render::TransferFunction::builtin(preset, minimum, maximum);
  const auto points = std::vector<render::TransferPoint>{
      transfer.points().begin(), transfer.points().end()};
  volume->transfer_function = std::move(transfer);
  volume->transfer_function_name = std::string{render::to_string(preset)};
  volume->direct_volume.reset();
  ++volume->presentation_revision;
  return operation::Result<VolumeRampResult>::success(
      {volume->id, volume->transfer_function_name,
       std::string{render::TransferFunction::algorithm},
       render::TransferFunction::version, points});
}

operation::Result<VolumeRampResult> Workspace::define_volume_ramp(
    std::string name, render::TransferFunction transfer) {
  auto *volume = mutable_active_volume();
  if (volume == nullptr) {
    return operation::Result<VolumeRampResult>::failure(operation::Error{
        operation::ErrorCode::not_found, "no active volume object",
        "load a volume with volume load first"});
  }
  if (name.empty())
    return operation::Result<VolumeRampResult>::failure(
        invalid("volume transfer-function name must not be empty"));
  const auto points = std::vector<render::TransferPoint>{
      transfer.points().begin(), transfer.points().end()};
  volume->transfer_function = std::move(transfer);
  volume->transfer_function_name = std::move(name);
  volume->direct_volume.reset();
  ++volume->presentation_revision;
  return operation::Result<VolumeRampResult>::success(
      {volume->id, volume->transfer_function_name,
       std::string{render::TransferFunction::algorithm},
       render::TransferFunction::version, points});
}

operation::Result<VolumeRampResult> Workspace::volume_ramp() const {
  const auto *volume = active_volume();
  if (volume == nullptr) {
    return operation::Result<VolumeRampResult>::failure(operation::Error{
        operation::ErrorCode::not_found, "no active volume object",
        "load a volume with volume load first"});
  }
  const auto [minimum, maximum] = volume->grid->scalars().range();
  const auto fallback = render::TransferFunction::builtin(
      render::TransferPreset::density, minimum, maximum);
  const auto &transfer = volume->transfer_function.has_value()
                             ? *volume->transfer_function
                             : fallback;
  const auto name = volume->transfer_function.has_value()
                        ? volume->transfer_function_name
                        : std::string{"density"};
  return operation::Result<VolumeRampResult>::success(
      {volume->id, name, std::string{render::TransferFunction::algorithm},
       render::TransferFunction::version,
       {transfer.points().begin(), transfer.points().end()}});
}

operation::Result<DirectVolumeResult> Workspace::show_direct_volume(
    render::DirectVolumeStyle style,
    std::optional<render::TransferPreset> preset, bool replace_existing,
    operation::TaskContext &context) {
  auto plan = plan_direct_volume(style, preset, replace_existing);
  if (!plan.has_value()) {
    return operation::Result<DirectVolumeResult>::failure(plan.error());
  }
  auto candidate =
      build_direct_volume_candidate(std::move(plan.value()), context);
  if (!candidate.has_value()) {
    return operation::Result<DirectVolumeResult>::failure(candidate.error());
  }
  return commit_direct_volume(std::move(candidate.value()));
}

operation::Result<DirectVolumePlan> Workspace::plan_direct_volume(
    render::DirectVolumeStyle style,
    std::optional<render::TransferPreset> preset,
    bool replace_existing) const {
  const auto *volume = active_volume();
  if (volume == nullptr) {
    return operation::Result<DirectVolumePlan>::failure(operation::Error{
        operation::ErrorCode::not_found, "no active volume object",
        "load a volume with volume load first"});
  }
  const auto [minimum, maximum] = volume->grid->scalars().range();
  const auto candidate_preset = preset.value_or(render::TransferPreset::density);
  const auto fallback = render::TransferFunction::builtin(candidate_preset,
                                                           minimum, maximum);
  const auto use_current = !preset.has_value() &&
                           volume->transfer_function.has_value();
  auto transfer = use_current ? *volume->transfer_function : fallback;
  const auto required =
      render::direct_volume_texture_bytes(*volume->grid, style);
  if (!required.has_value()) {
    return operation::Result<DirectVolumePlan>::failure(required.error());
  }
  auto transfer_name = use_current
                           ? volume->transfer_function_name
                           : std::string{render::to_string(candidate_preset)};
  return operation::Result<DirectVolumePlan>::success(
      {volume->id,
       volume->scene_node,
       volume->grid,
       volume->presentation_revision,
       std::move(transfer),
       std::move(transfer_name),
       preset.has_value(),
       replace_existing,
       style,
       required.value()});
}

operation::Result<DirectVolumeCandidate>
Workspace::build_direct_volume_candidate(DirectVolumePlan plan,
                                         operation::TaskContext &context) {
  render::DirectVolumeRequest request{plan.grid, &plan.transfer_function,
                                      plan.scene_node.value, plan.style,
                                      &context};
  auto prepared = render::build_direct_volume(request);
  if (!prepared.has_value()) {
    return operation::Result<DirectVolumeCandidate>::failure(prepared.error());
  }
  return operation::Result<DirectVolumeCandidate>::success(
      {std::move(plan),
       std::make_shared<const render::DirectVolumeData>(
           std::move(prepared.value()))});
}

operation::Result<DirectVolumeResult> Workspace::commit_direct_volume(
    DirectVolumeCandidate candidate) {
  auto *volume = mutable_active_volume();
  if (volume == nullptr || volume->id != candidate.plan.object_id ||
      volume->grid != candidate.plan.grid ||
      volume->presentation_revision !=
          candidate.plan.expected_presentation_revision) {
    return operation::Result<DirectVolumeResult>::failure(operation::Error{
        operation::ErrorCode::stale_result,
        "direct-volume input changed before owner-thread commit",
        "retry volume render against the current volume state",
        {{"object_id", std::to_string(candidate.plan.object_id)},
         {"expected_presentation_revision",
          std::to_string(candidate.plan.expected_presentation_revision)},
         {"current_presentation_revision",
          volume == nullptr
              ? std::string{"missing"}
              : std::to_string(volume->presentation_revision)}}});
  }
  if (candidate.data == nullptr) {
    return operation::Result<DirectVolumeResult>::failure(
        invalid("direct-volume candidate contains no prepared data"));
  }
  volume->direct_volume = std::move(candidate.data);
  if (candidate.plan.commit_transfer_function) {
    volume->transfer_function = candidate.plan.transfer_function;
    volume->transfer_function_name = candidate.plan.transfer_function_name;
  }
  if (candidate.plan.replace_existing) {
    volume->representations.clear();
  }
  ++volume->presentation_revision;
  return operation::Result<DirectVolumeResult>::success(
      {volume->id,
       candidate.plan.transfer_function_name,
       candidate.plan.style.mode,
       candidate.plan.style.sampling_step,
       candidate.plan.style.maximum_steps,
       candidate.plan.style.lookup_table_samples,
       candidate.plan.style.texture_budget_bytes,
       candidate.plan.required_texture_bytes});
}

operation::Result<bool> Workspace::hide_direct_volume() {
  auto *volume = mutable_active_volume();
  if (volume == nullptr) {
    return operation::Result<bool>::failure(operation::Error{
        operation::ErrorCode::not_found, "no active volume object",
        "load a volume with volume load first"});
  }
  const auto removed = volume->direct_volume != nullptr;
  volume->direct_volume.reset();
  if (removed) ++volume->presentation_revision;
  return operation::Result<bool>::success(removed);
}

operation::Result<MolecularSurfaceResult> Workspace::show_molecular_surface(
    std::string selection_expression, render::MolecularSurfaceStyle style,
    operation::TaskContext &context) {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return operation::Result<MolecularSurfaceResult>::failure(missing_active());
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value())
    return operation::Result<MolecularSurfaceResult>::failure(mask.error());
  const auto frame = active_frame(*object);
  if (!frame.has_value())
    return operation::Result<MolecularSurfaceResult>::failure(frame.error());
  auto visuals = atom_visuals(*object->system->topology());
  if (!visuals.has_value())
    return operation::Result<MolecularSurfaceResult>::failure(visuals.error());
  apply_atom_colors(visuals.value(), *object, *object->system->topology(),
                    color_table_, frame.value().get());
  const auto frame_index = object->trajectory.has_value()
                               ? object->trajectory->timeline.snapshot().frame
                               : 0U;
  // Object-level PyMOL surface settings: transparency scales the requested
  // opacity and an explicit surface_color replaces the requested color.
  {
    const render::RenderSettingContext object_context{object->id, frame_index,
                                                      0U, 0U};
    const auto transparency =
        render_settings_.resolve("transparency", object_context);
    const auto surface_color =
        render_settings_.resolve("surface_color", object_context);
    if (transparency.has_value())
      style.color.alpha = static_cast<float>(
          static_cast<double>(style.color.alpha) *
          std::clamp(1.0 - std::get<double>(transparency.value().value), 0.0, 1.0));
    if (surface_color.has_value() && surface_color.value().source_scope.has_value()) {
      const auto index =
          color_table_.resolve(std::get<std::string>(surface_color.value().value));
      if (index.has_value())
        if (const auto rgb = color_table_.rgb(index.value()); rgb.has_value())
          style.color = {rgb->red, rgb->green, rgb->blue, style.color.alpha};
    }
  }
  const render::MolecularSurfaceRequest request{
      object->system->topology().get(), frame.value().get(), frame_index,
      object->scene_node.value, visuals.value(), mask.value(), style, &context};
  auto packet = render::build_molecular_surface(request);
  if (!packet.has_value())
    return operation::Result<MolecularSurfaceResult>::failure(packet.error());
  const auto &voxel_text = packet.value().provenance.at("voxel_count");
  std::size_t voxel_count{};
  const auto parsed_voxels = molshredder::core::from_chars(
      voxel_text.data(), voxel_text.data() + voxel_text.size(), voxel_count);
  if (parsed_voxels.ec != std::errc{} ||
      parsed_voxels.ptr != voxel_text.data() + voxel_text.size()) {
    return operation::Result<MolecularSurfaceResult>::failure(
        operation::Error{operation::ErrorCode::internal,
                         "molecular surface returned invalid voxel provenance",
                         {}});
  }
  const auto vertex_count = packet.value().mesh_vertices.size();
  const auto triangle_count = packet.value().mesh_triangles.size();
  const auto pick_target_count = packet.value().pick_targets.size();
  const auto bounds = packet.value().bounds;
  object->molecular_surface = std::move(packet.value());
  return operation::Result<MolecularSurfaceResult>::success(
      {object->id,
       style.kind,
       std::move(selection_expression),
       style.probe_radius_angstrom,
       style.grid_spacing_angstrom,
       voxel_count,
       style.voxel_budget,
       style.memory_budget_bytes,
       vertex_count,
       triangle_count,
       pick_target_count,
       bounds});
}

operation::Result<bool> Workspace::hide_molecular_surface() {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return operation::Result<bool>::failure(missing_active());
  const auto removed = object->molecular_surface.has_value();
  object->molecular_surface.reset();
  return operation::Result<bool>::success(removed);
}

operation::Result<VolumeSaveResult> Workspace::save_active_volume(
    const std::filesystem::path &path, io::VolumeFormat format, bool overwrite,
    operation::TaskContext &context) const {
  const auto *volume = active_volume();
  if (volume == nullptr) {
    return operation::Result<VolumeSaveResult>::failure(operation::Error{
        operation::ErrorCode::not_found, "workspace has no active volume",
        "load a volume with volume load first"});
  }
  io::VolumeWriteOptions options;
  options.format = format;
  options.name = volume->name;
  const auto written = io::write_volume_file(path, *volume->grid,
                                             std::move(options), overwrite,
                                             context);
  if (!written.has_value())
    return operation::Result<VolumeSaveResult>::failure(written.error());
  return operation::Result<VolumeSaveResult>::success(
      VolumeSaveResult{volume->id, path, written.value()});
}

operation::Result<SaveResult> Workspace::save_active_structure(
    const std::filesystem::path &path, io::StructureFormat format,
    bool all_frames, unsigned int decimal_places, std::string comment,
    bool overwrite, operation::TaskContext &context) const {
  const auto *object = active_object();
  if (object == nullptr) {
    return operation::Result<SaveResult>::failure(missing_active());
  }
  io::StructureWriteOptions options;
  options.format = format;
  options.decimal_places = decimal_places;
  options.comment = std::move(comment);
  if (!all_frames) {
    options.frame_indices.push_back(
        object->trajectory.has_value()
            ? object->trajectory->timeline.snapshot().frame
            : 0U);
  }
  const auto written = io::write_structure_file(
      path, *object->system->topology(), *object->system->coordinates(),
      std::move(options), overwrite, context);
  if (!written.has_value()) {
    return operation::Result<SaveResult>::failure(written.error());
  }
  return operation::Result<SaveResult>::success(
      SaveResult{object->id, path, written.value()});
}

operation::Result<TrajectorySaveResult>
Workspace::save_active_trajectory_frame(const std::filesystem::path &path,
                                        io::TrajectoryFormat format,
                                        std::string title, bool overwrite,
                                        operation::TaskContext &context) const {
  const auto *object = active_object();
  if (object == nullptr)
    return operation::Result<TrajectorySaveResult>::failure(missing_active());
  const auto frame_index = object->trajectory.has_value()
                               ? object->trajectory->timeline.snapshot().frame
                               : 0U;
  const auto frame = active_frame(*object);
  if (!frame.has_value()) {
    return operation::Result<TrajectorySaveResult>::failure(frame.error());
  }
  io::TrajectoryWriteOptions options;
  options.format = format;
  options.title = std::move(title);
  const auto written = io::write_trajectory_frame_file(
      path, *frame.value(), std::move(options), overwrite, context);
  if (!written.has_value()) {
    return operation::Result<TrajectorySaveResult>::failure(written.error());
  }
  return operation::Result<TrajectorySaveResult>::success(
      TrajectorySaveResult{object->id, frame_index, path, written.value()});
}

std::optional<operation::Error>
Workspace::set_named_selection(std::string name, std::string expression,
                               bool dynamic) {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return missing_active();
  const auto parsed = selection::Expression::parse(expression);
  if (!parsed.has_value())
    return parsed.error();
  const auto frame_index = object->trajectory.has_value()
                               ? object->trajectory->timeline.snapshot().frame
                               : 0U;
  const auto frame = object->system->coordinates()->read_frame(frame_index);
  if (!frame.has_value()) return frame.error();
  return object->selections.set(
      std::move(name), parsed.value(), dynamic, *object->system->topology(),
      selection::EvaluationContext{frame.value().get(), frame_index,
                                   object->system->coordinates().get(),
                                   object->system->name()});
}

std::optional<operation::Error> Workspace::set_render_setting(
    std::string_view name, const render::RenderSettingScope &scope,
    render::RenderSettingValue value) {
  if (const auto error = validate_render_setting_target(objects_, scope);
      error.has_value())
    return error;
  if (const auto error = validate_setting_color(value, color_table_);
      error.has_value())
    return error;
  auto candidate = render_settings_;
  if (const auto error = candidate.set(name, scope, std::move(value));
      error.has_value())
    return error;
  return commit_render_settings(std::move(candidate));
}

operation::Result<SelectionSnapshot>
Workspace::selection_snapshot(std::string_view selection_expression,
                              std::size_t max_atoms) const {
  using R = operation::Result<SelectionSnapshot>;
  const auto *object = active_object();
  if (object == nullptr) return R::failure(missing_active());
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value()) return R::failure(mask.error());
  const auto frame = active_frame(*object);
  if (!frame.has_value()) return R::failure(frame.error());
  const auto &topology = *object->system->topology();
  const auto &presence = frame.value()->presence();
  SelectionSnapshot snapshot;
  snapshot.object_id = object->id;
  snapshot.object_name = object->system->name();
  snapshot.coordinate_unit = frame.value()->metadata().coordinate_unit;
  snapshot.frame_index =
      object->trajectory.has_value() ? object->trajectory->timeline.snapshot().frame : 0U;
  for (std::size_t atom = 0; atom < mask.value().size(); ++atom) {
    if (mask.value()[atom] == 0U) continue;
    if (!presence.empty() && !frame.value()->atom_present(atom)) continue;
    if (snapshot.atom_ids.size() >= max_atoms) {
      return R::failure(invalid("the selection has more than " + std::to_string(max_atoms) +
                                " atoms",
                                "narrow the selection"));
    }
    const auto id = topology.atom_id(model::AtomIndex{atom});
    if (!id.has_value()) return R::failure(invalid("topology atom identity is missing"));
    snapshot.atom_ids.push_back(id->value);
    snapshot.atom_names.push_back(topology.atoms()[atom].name);
    snapshot.atomic_numbers.push_back(topology.atoms()[atom].atomic_number);
    snapshot.positions.push_back(std::visit(
        [atom](const auto &values) {
          const auto &point = values[atom];
          return model::Vec3d{static_cast<double>(point.x), static_cast<double>(point.y),
                              static_cast<double>(point.z)};
        },
        frame.value()->positions().values()));
  }
  return R::success(std::move(snapshot));
}

operation::Result<std::vector<std::uint64_t>>
Workspace::selected_atom_ids(std::uint64_t object_id,
                             std::string_view selection_expression) const {
  using R = operation::Result<std::vector<std::uint64_t>>;
  const auto object = std::find_if(
      objects_.begin(), objects_.end(),
      [object_id](const auto &item) { return item.id == object_id; });
  if (object == objects_.end())
    return R::failure(operation::Error{operation::ErrorCode::not_found,
                                       "render setting object was not found",
                                       {}});
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value())
    return R::failure(mask.error());
  const auto &topology = *object->system->topology();
  std::vector<std::uint64_t> ids;
  for (std::size_t atom = 0; atom < mask.value().size(); ++atom) {
    if (mask.value()[atom] == 0U)
      continue;
    const auto id = topology.atom_id(model::AtomIndex{atom});
    if (!id.has_value())
      return R::failure(invalid("topology atom identity is missing"));
    ids.push_back(id->value);
  }
  return R::success(std::move(ids));
}

operation::Result<std::vector<std::uint64_t>>
Workspace::selected_bond_ids(std::uint64_t object_id,
                             const std::vector<std::uint64_t> &atom_ids) const {
  using R = operation::Result<std::vector<std::uint64_t>>;
  const auto object = std::find_if(
      objects_.begin(), objects_.end(),
      [object_id](const auto &item) { return item.id == object_id; });
  if (object == objects_.end())
    return R::failure(operation::Error{operation::ErrorCode::not_found,
                                       "render setting object was not found",
                                       {}});
  const auto &topology = *object->system->topology();
  auto sorted = atom_ids;
  std::sort(sorted.begin(), sorted.end());
  std::vector<std::uint64_t> bonds;
  for (std::size_t index = 0; index < topology.bonds().size(); ++index) {
    const auto &bond = topology.bonds()[index];
    const auto first = topology.atom_id(bond.first);
    const auto second = topology.atom_id(bond.second);
    const auto id = topology.bond_id(index);
    if (!first.has_value() || !second.has_value() || !id.has_value())
      return R::failure(invalid("topology atom or bond identity is missing"));
    if (std::binary_search(sorted.begin(), sorted.end(), first->value) &&
        std::binary_search(sorted.begin(), sorted.end(), second->value))
      bonds.push_back(id->value);
  }
  return R::success(std::move(bonds));
}

operation::Result<std::size_t> Workspace::set_render_setting_selection(
    std::string_view name, const render::RenderSettingScope &scope,
    std::string_view selection_expression,
    std::optional<render::RenderSettingValue> value) {
  using R = operation::Result<std::size_t>;
  const auto bond_level = scope.level == render::RenderSettingScopeLevel::bond;
  if (scope.level != render::RenderSettingScopeLevel::atom && !bond_level)
    return R::failure(
        invalid("--selection applies to the atom and bond render setting scopes"));
  auto ids = selected_atom_ids(scope.object_id, selection_expression);
  if (!ids.has_value())
    return R::failure(ids.error());
  if (bond_level) {
    // Bond scope covers bonds whose two atoms are both selected (PyMOL).
    ids = selected_bond_ids(scope.object_id, ids.value());
    if (!ids.has_value())
      return R::failure(ids.error());
  }
  if (ids.value().empty())
    return R::failure(invalid(bond_level ? "render setting selection matched no bonds"
                                         : "render setting selection matched no atoms"));
  if (value.has_value()) {
    if (const auto error = validate_setting_color(*value, color_table_);
        error.has_value())
      return R::failure(*error);
  }
  auto candidate = render_settings_;
  for (const auto id : ids.value()) {
    auto atom_scope = scope;
    if (bond_level)
      atom_scope.bond_id = id;
    else
      atom_scope.atom_id = id;
    if (const auto error = validate_render_setting_target(objects_, atom_scope);
        error.has_value())
      return R::failure(*error);
    if (value.has_value()) {
      if (const auto error = candidate.set(name, atom_scope, *value);
          error.has_value())
        return R::failure(*error);
    } else {
      const auto removed = candidate.unset(name, atom_scope);
      if (!removed.has_value())
        return R::failure(removed.error());
    }
  }
  if (const auto error = commit_render_settings(std::move(candidate));
      error.has_value())
    return R::failure(*error);
  return R::success(ids.value().size());
}

operation::Result<bool> Workspace::unset_render_setting(
    std::string_view name, const render::RenderSettingScope &scope) {
  if (const auto error = validate_render_setting_target(objects_, scope);
      error.has_value())
    return operation::Result<bool>::failure(*error);
  auto candidate = render_settings_;
  const auto removed = candidate.unset(name, scope);
  if (!removed.has_value())
    return removed;
  if (const auto error = commit_render_settings(std::move(candidate));
      error.has_value())
    return operation::Result<bool>::failure(*error);
  return operation::Result<bool>::success(removed.value());
}

operation::Result<std::size_t> Workspace::reset_render_setting_scope(
    const render::RenderSettingScope &scope) {
  if (const auto error = validate_render_setting_target(objects_, scope);
      error.has_value())
    return operation::Result<std::size_t>::failure(*error);
  auto candidate = render_settings_;
  const auto removed = candidate.reset_scope(scope);
  if (!removed.has_value())
    return removed;
  if (const auto error = commit_render_settings(std::move(candidate));
      error.has_value())
    return operation::Result<std::size_t>::failure(*error);
  return operation::Result<std::size_t>::success(removed.value());
}

operation::Result<render::ResolvedRenderSetting>
Workspace::resolve_render_setting(
    std::string_view name,
    const render::RenderSettingContext &context) const {
  render::RenderSettingScope scope;
  if (context.bond_id != 0U)
    scope = {render::RenderSettingScopeLevel::bond, context.object_id,
             context.state_index, 0U, context.bond_id};
  else if (context.atom_id != 0U)
    scope = {render::RenderSettingScopeLevel::atom, context.object_id,
             context.state_index, context.atom_id, 0U};
  else if (context.object_id != 0U)
    scope = {render::RenderSettingScopeLevel::object_state, context.object_id,
             context.state_index, 0U, 0U};
  if (scope.level != render::RenderSettingScopeLevel::global) {
    if (const auto error = validate_render_setting_target(objects_, scope);
        error.has_value())
      return operation::Result<render::ResolvedRenderSetting>::failure(*error);
  }
  if (context.atom_id != 0U && context.bond_id != 0U) {
    const render::RenderSettingScope atom_scope{
        render::RenderSettingScopeLevel::atom, context.object_id,
        context.state_index, context.atom_id, 0U};
    if (const auto error = validate_render_setting_target(objects_, atom_scope);
        error.has_value())
      return operation::Result<render::ResolvedRenderSetting>::failure(*error);
  }
  return render_settings_.resolve(name, context);
}

render::RenderSettingSnapshot Workspace::render_setting_snapshot() const {
  return render_settings_.snapshot();
}

std::optional<operation::Error> Workspace::restore_render_settings(
    const render::RenderSettingSnapshot &snapshot) {
  const auto restored = render::RenderSettingStore::restore(snapshot);
  if (!restored.has_value())
    return restored.error();
  for (const auto &item : snapshot.overrides) {
    if (const auto error = validate_render_setting_target(objects_, item.scope);
        error.has_value())
      return error;
    if (const auto error = validate_setting_color(item.value, color_table_);
        error.has_value())
      return error;
  }
  return commit_render_settings(restored.value());
}

operation::Result<Workspace::AtomColorCandidate>
Workspace::prepare_atom_colors(
    const WorkspaceObject &target,
    std::vector<std::pair<std::uint64_t, std::int32_t>> updates) const {
  using R = operation::Result<AtomColorCandidate>;
  const auto *object = &target;
  std::sort(updates.begin(), updates.end());
  auto merged = std::make_shared<
      std::vector<std::pair<std::uint64_t, std::int32_t>>>();
  const auto &existing =
      object->atom_colors != nullptr
          ? *object->atom_colors
          : std::vector<std::pair<std::uint64_t, std::int32_t>>{};
  merged->reserve(existing.size() + updates.size());
  auto left = existing.begin();
  auto right = updates.begin();
  while (left != existing.end() || right != updates.end()) {
    std::pair<std::uint64_t, std::int32_t> item;
    if (right == updates.end() ||
        (left != existing.end() && left->first < right->first)) {
      item = *left++;
    } else {
      if (left != existing.end() && left->first == right->first)
        ++left;
      item = *right++;
    }
    if (item.second != render::kColorDefault)
      merged->push_back(item);
  }
  auto candidate = *object;
  candidate.atom_colors =
      merged->empty() ? nullptr
                      : std::shared_ptr<const std::vector<
                            std::pair<std::uint64_t, std::int32_t>>>{merged};
  const auto frame = active_frame(candidate);
  if (!frame.has_value())
    return R::failure(frame.error());
  const auto frame_index = candidate.trajectory.has_value()
                               ? candidate.trajectory->timeline.snapshot().frame
                               : 0U;
  auto rebuilt =
      rebuild_representations(candidate, candidate.system, *frame.value(),
                              frame_index, render_settings_, color_table_);
  if (!rebuilt.has_value())
    return R::failure(rebuilt.error());
  return R::success({std::move(candidate.atom_colors), std::move(rebuilt.value())});
}

std::optional<operation::Error> Workspace::commit_atom_colors(
    WorkspaceObject &target,
    std::vector<std::pair<std::uint64_t, std::int32_t>> updates) {
  auto prepared = prepare_atom_colors(target, std::move(updates));
  if (!prepared.has_value())
    return prepared.error();
  target.atom_colors = std::move(prepared.value().colors);
  target.representations = std::move(prepared.value().representations);
  return std::nullopt;
}

operation::Result<std::vector<WorkspaceObject *>>
Workspace::scope_targets(ObjectScope scope) {
  using R = operation::Result<std::vector<WorkspaceObject *>>;
  if (scope == ObjectScope::active) {
    auto *object = mutable_active_object();
    if (object == nullptr)
      return R::failure(missing_active());
    return R::success({object});
  }
  if (objects_.empty())
    return R::failure(missing_active());
  std::vector<WorkspaceObject *> targets;
  targets.reserve(objects_.size());
  for (auto &object : objects_)
    targets.push_back(&object);
  return R::success(std::move(targets));
}

operation::Result<ColorAssignResult>
Workspace::assign_color(std::string_view color,
                        std::string selection_expression, ObjectScope scope) {
  using R = operation::Result<ColorAssignResult>;
  const auto targets = scope_targets(scope);
  if (!targets.has_value())
    return R::failure(targets.error());
  const auto cycle = render::ColorTable::auto_cycle();
  auto next_cursor = auto_color_cursor_;
  std::int32_t index{};
  if (color.size() == 4U &&
      std::equal(color.begin(), color.end(), "auto", [](char left, char right) {
        return std::tolower(static_cast<unsigned char>(left)) == right;
      })) {
    index = cycle[auto_color_cursor_ % cycle.size()];
    next_cursor = (auto_color_cursor_ + 1U) % cycle.size();
  } else if (const auto ramp = color_table_.ramp_code(color); ramp.has_value()) {
    index = *ramp;
  } else {
    const auto resolved = color_table_.resolve_assignment(color);
    if (!resolved.has_value())
      return R::failure(resolved.error());
    index = resolved.value();
    if (index == render::kColorFront || index == render::kColorBack)
      return R::failure(operation::Error{
          operation::ErrorCode::unsupported,
          "front/back are render-setting color tokens, not atom colors",
          "use an explicit color name, index or hex value"});
  }
  // Every target is prepared before any commits (failure-atomic).
  std::vector<AtomColorCandidate> prepared;
  prepared.reserve(targets.value().size());
  std::size_t affected{};
  for (auto *object : targets.value()) {
    const auto mask = selection_mask(*object, selection_expression);
    if (!mask.has_value())
      return R::failure(mask.error());
    const auto &topology = *object->system->topology();
    std::vector<std::pair<std::uint64_t, std::int32_t>> updates;
    for (std::size_t atom = 0; atom < mask.value().size(); ++atom) {
      if (mask.value()[atom] == 0U)
        continue;
      const auto id = topology.atom_id(model::AtomIndex{atom});
      if (!id.has_value())
        return R::failure(invalid("topology atom identity is missing"));
      const auto value =
          index == render::kColorAtomic
              ? render::ColorTable::element_color(
                    topology.atoms()[atom].atomic_number)
              : index;
      updates.emplace_back(id->value, value);
    }
    affected += updates.size();
    auto candidate = prepare_atom_colors(*object, std::move(updates));
    if (!candidate.has_value())
      return R::failure(candidate.error());
    prepared.push_back(std::move(candidate.value()));
  }
  std::size_t colored{};
  std::size_t representations{};
  for (std::size_t target = 0; target < prepared.size(); ++target) {
    auto *object = targets.value()[target];
    object->atom_colors = std::move(prepared[target].colors);
    object->representations = std::move(prepared[target].representations);
    colored += object->atom_colors == nullptr ? 0U : object->atom_colors->size();
    representations += object->representations.size();
  }
  auto_color_cursor_ = next_cursor;
  const auto *active = active_object();
  return R::success(
      {active != nullptr ? active->id : targets.value().front()->id, index,
       color_table_.name(index).value_or(std::string{color}), affected, colored,
       representations});
}

operation::Result<ColorDefineResult>
Workspace::define_color(std::string_view name,
                        std::array<double, 3> components) {
  using R = operation::Result<ColorDefineResult>;
  auto candidate = color_table_;
  const auto previous_size = candidate.size();
  const auto index = candidate.define(name, components);
  if (!index.has_value())
    return R::failure(index.error());
  const auto rgb = candidate.rgb(index.value());
  const auto canonical = candidate.name(index.value());
  if (const auto error = commit_color_table(std::move(candidate));
      error.has_value())
    return R::failure(*error);
  return R::success({index.value(), canonical.value_or(std::string{name}),
                     rgb.value_or(render::ColorRgb{}),
                     static_cast<std::size_t>(index.value()) >= previous_size});
}

operation::Result<ColorQueryResult>
Workspace::query_color(std::string_view color) const {
  using R = operation::Result<ColorQueryResult>;
  const auto index = color_table_.resolve(color);
  if (!index.has_value())
    return R::failure(index.error());
  return R::success({index.value(),
                     color_table_.name(index.value()).value_or(std::string{color}),
                     color_table_.rgb(index.value())});
}

operation::Result<std::size_t> Workspace::recolor() {
  if (const auto error = commit_color_table(color_table_); error.has_value())
    return operation::Result<std::size_t>::failure(*error);
  std::size_t count{};
  for (const auto &object : objects_)
    count += object.representations.size();
  return operation::Result<std::size_t>::success(count);
}

operation::Result<ShowResult> Workspace::show(render::RepresentationKind kind,
                                              std::string selection_expression,
                                              bool replace_existing) {
  const auto mutated = mutate_representation_visibility(
      kind, std::move(selection_expression),
      replace_existing ? RepresentationVisibilityMutation::exclusive
                       : RepresentationVisibilityMutation::show);
  if (!mutated.has_value())
    return operation::Result<ShowResult>::failure(mutated.error());
  const auto *object = active_object();
  const auto found = std::find_if(
      object->representations.begin(), object->representations.end(),
      [kind](const auto &representation) { return representation.kind == kind; });
  const auto index = found == object->representations.end()
                         ? object->representations.size()
                         : static_cast<std::size_t>(
                               found - object->representations.begin());
  const auto primitive_count =
      found == object->representations.end()
          ? 0U
          : found->packet.lines.size() + found->packet.cylinders.size() +
                found->packet.spheres.size() +
                found->packet.mesh_triangles.size();
  return operation::Result<ShowResult>::success(ShowResult{
      object->id, index, primitive_count, mutated.value().affected_atom_count});
}

operation::Result<RepresentationVisibilityResult>
Workspace::mutate_representation_visibility(
    render::RepresentationKind kind, std::string selection_expression,
    RepresentationVisibilityMutation mutation) {
  const std::array kinds{kind};
  return mutate_representation_visibility(kinds, std::move(selection_expression),
                                          mutation);
}

operation::Result<RepresentationVisibilityResult>
Workspace::mutate_representation_visibility(
    std::span<const render::RepresentationKind> kinds,
    std::string selection_expression,
    RepresentationVisibilityMutation mutation, ObjectScope scope) {
  using R = operation::Result<RepresentationVisibilityResult>;
  const auto targets = scope_targets(scope);
  if (!targets.has_value())
    return R::failure(targets.error());
  if (kinds.empty())
    return R::failure(
        invalid("representation visibility mask must not be empty"));
  struct Plan {
    WorkspaceObject *object{};
    std::vector<std::uint8_t> mask;
    RepresentationVisibilityState candidate;
    std::vector<RepresentationRecord> rebuilt;
  };
  std::vector<Plan> plans;
  plans.reserve(targets.value().size());
  for (auto *object : targets.value()) {
    auto mask = selection_mask(*object, selection_expression);
    if (!mask.has_value())
      return R::failure(mask.error());
    plans.push_back({object, std::move(mask.value()),
                     object->representation_visibility, {}});
  }
  // Toggle hides when any selected atom (in any target) shows a requested
  // representation, otherwise shows.
  auto effective_mutation = mutation;
  if (mutation == RepresentationVisibilityMutation::toggle) {
    bool any_visible = false;
    for (const auto &plan : plans) {
      for (const auto kind : kinds) {
        const auto current = plan.candidate.selection_mask(kind);
        if (!current.has_value())
          return R::failure(current.error());
        for (std::size_t atom = 0; atom < plan.mask.size(); ++atom) {
          if (plan.mask[atom] != 0U && current.value()[atom] != 0U) {
            any_visible = true;
            break;
          }
        }
        if (any_visible)
          break;
      }
      if (any_visible)
        break;
    }
    effective_mutation = any_visible ? RepresentationVisibilityMutation::hide
                                     : RepresentationVisibilityMutation::show;
  }
  std::size_t affected{};
  std::size_t visible{};
  for (auto &plan : plans) {
    for (std::size_t index = 0; index < kinds.size(); ++index) {
      const auto current_mutation =
          mutation == RepresentationVisibilityMutation::exclusive
              ? (index == 0U ? RepresentationVisibilityMutation::exclusive
                             : RepresentationVisibilityMutation::show)
              : effective_mutation;
      if (const auto error =
              plan.candidate.apply(kinds[index], plan.mask, current_mutation);
          error.has_value())
        return R::failure(*error);
    }
    const auto frame = active_frame(*plan.object);
    if (!frame.has_value())
      return R::failure(frame.error());
    const auto frame_index =
        plan.object->trajectory.has_value()
            ? plan.object->trajectory->timeline.snapshot().frame
            : 0U;
    auto rebuilt = rebuild_representations(
        *plan.object, plan.object->system, *frame.value(), frame_index,
        render_settings_, color_table_, &plan.candidate);
    if (!rebuilt.has_value())
      return R::failure(rebuilt.error());
    plan.rebuilt = std::move(rebuilt.value());
    affected += static_cast<std::size_t>(
        std::count(plan.mask.begin(), plan.mask.end(), std::uint8_t{1U}));
    for (const auto kind : kinds) {
      const auto count = plan.candidate.visible_count(kind);
      if (!count.has_value())
        return R::failure(count.error());
      visible += count.value();
    }
  }
  std::size_t representations{};
  for (auto &plan : plans) {
    plan.object->representation_visibility = std::move(plan.candidate);
    plan.object->representations = std::move(plan.rebuilt);
    representations += plan.object->representations.size();
  }
  const auto *active = active_object();
  return R::success(
      {active != nullptr ? active->id : plans.front().object->id, kinds.front(),
       mutation, affected, visible, representations,
       std::vector<render::RepresentationKind>{kinds.begin(), kinds.end()}});
}

operation::Result<RepresentationVisibilitySnapshot>
Workspace::representation_visibility_snapshot() const {
  const auto *object = active_object();
  if (object == nullptr) {
    return operation::Result<RepresentationVisibilitySnapshot>::failure(
        missing_active());
  }
  return operation::Result<RepresentationVisibilitySnapshot>::success(
      object->representation_visibility.snapshot());
}

operation::Result<RepresentationVisibilitySessionSnapshot>
Workspace::representation_visibility_session_snapshot() const {
  const auto *object = active_object();
  if (object == nullptr) {
    return operation::Result<RepresentationVisibilitySessionSnapshot>::failure(
        missing_active());
  }
  return operation::Result<RepresentationVisibilitySessionSnapshot>::success(
      {kRepresentationVisibilitySessionSchemaVersion, object->id,
       object->system->topology()->version(),
       [&object] {
         std::vector<std::uint64_t> ids;
         ids.reserve(object->system->topology()->atom_ids().size());
         for (const auto id : object->system->topology()->atom_ids())
           ids.push_back(id.value);
         return ids;
       }(),
       object->representation_visibility.snapshot()});
}

operation::Result<RepresentationVisibilityResult>
Workspace::restore_representation_visibility(
    const RepresentationVisibilitySnapshot &snapshot) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<RepresentationVisibilityResult>::failure(
        missing_active());
  }
  const auto restored = RepresentationVisibilityState::restore(snapshot);
  if (!restored.has_value()) {
    return operation::Result<RepresentationVisibilityResult>::failure(
        restored.error());
  }
  if (restored.value().atom_count() != object->system->topology()->atom_count()) {
    return operation::Result<RepresentationVisibilityResult>::failure(
        invalid("representation visibility atom count does not match active object"));
  }
  const auto frame = active_frame(*object);
  if (!frame.has_value()) {
    return operation::Result<RepresentationVisibilityResult>::failure(
        frame.error());
  }
  const auto frame_index = object->trajectory.has_value()
                               ? object->trajectory->timeline.snapshot().frame
                               : 0U;
  const auto rebuilt = rebuild_representations(
      *object, object->system, *frame.value(), frame_index, render_settings_,
      color_table_, &restored.value());
  if (!rebuilt.has_value()) {
    return operation::Result<RepresentationVisibilityResult>::failure(
        rebuilt.error());
  }
  std::size_t visible{};
  constexpr std::array kinds{render::RepresentationKind::lines,
                             render::RepresentationKind::sticks,
                             render::RepresentationKind::spheres,
                             render::RepresentationKind::ribbon,
                             render::RepresentationKind::cartoon,
                             render::RepresentationKind::labels};
  for (const auto kind : kinds)
    visible += restored.value().visible_count(kind).value();
  object->representation_visibility = restored.value();
  object->representations = std::move(rebuilt.value());
  return operation::Result<RepresentationVisibilityResult>::success(
      {object->id, render::RepresentationKind::lines,
       RepresentationVisibilityMutation::show, 0U, visible,
       object->representations.size(), {}});
}

operation::Result<RepresentationVisibilityResult>
Workspace::restore_representation_visibility_session(
    const RepresentationVisibilitySessionSnapshot &snapshot) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<RepresentationVisibilityResult>::failure(
        missing_active());
  }
  std::vector<std::uint64_t> current_ids;
  current_ids.reserve(object->system->topology()->atom_ids().size());
  for (const auto id : object->system->topology()->atom_ids())
    current_ids.push_back(id.value);
  if (snapshot.schema_version !=
          kRepresentationVisibilitySessionSchemaVersion ||
      snapshot.object_id != object->id ||
      snapshot.topology_version != object->system->topology()->version() ||
      snapshot.atom_ids != current_ids) {
    return operation::Result<RepresentationVisibilityResult>::failure(
        invalid("representation visibility session object does not match active object"));
  }
  return restore_representation_visibility(snapshot.visibility);
}

operation::Result<CenterAnalysisResult>
Workspace::analyze_center(std::string selection_expression,
                          analysis::CenterMode mode) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<CenterAnalysisResult>::failure(missing_active());
  }
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value()) {
    return operation::Result<CenterAnalysisResult>::failure(mask.error());
  }
  const auto frame = active_frame(*object);
  if (!frame.has_value()) {
    return operation::Result<CenterAnalysisResult>::failure(frame.error());
  }
  analysis::MassValues masses;
  if (mode == analysis::CenterMode::center_of_mass) {
    auto resolved = analysis::masses_from_property(*object->system->topology());
    if (!resolved.has_value() &&
        resolved.error().code == operation::ErrorCode::not_found) {
      resolved =
          analysis::estimated_element_masses(*object->system->topology());
    }
    if (!resolved.has_value()) {
      return operation::Result<CenterAnalysisResult>::failure(resolved.error());
    }
    masses = std::move(resolved.value());
  }
  analysis::CenterRequest request;
  request.frame = frame.value().get();
  request.selected = mask.value();
  request.mode = mode;
  request.masses = masses.values;
  request.mass_unit = masses.unit.has_value()
                          ? std::string_view{masses.unit.value()}
                          : std::string_view{};
  request.mass_source = masses.source;
  request.masses_estimated = masses.estimated;
  const auto center = analysis::calculate_center(request);
  if (!center.has_value()) {
    return operation::Result<CenterAnalysisResult>::failure(center.error());
  }
  return operation::Result<CenterAnalysisResult>::success(CenterAnalysisResult{
      object->id, std::move(selection_expression), mode, center.value()});
}

operation::Result<ShapeAnalysisResult>
Workspace::analyze_shape(std::string selection_expression,
                         analysis::CenterMode mode) {
  using R = operation::Result<ShapeAnalysisResult>;
  auto *object = mutable_active_object();
  if (object == nullptr)
    return R::failure(missing_active());
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value())
    return R::failure(mask.error());
  const auto frame = active_frame(*object);
  if (!frame.has_value())
    return R::failure(frame.error());
  analysis::MassValues masses;
  if (mode == analysis::CenterMode::center_of_mass) {
    auto resolved = analysis::masses_from_property(*object->system->topology());
    if (!resolved.has_value() &&
        resolved.error().code == operation::ErrorCode::not_found)
      resolved = analysis::estimated_element_masses(*object->system->topology());
    if (!resolved.has_value())
      return R::failure(resolved.error());
    masses = std::move(resolved.value());
  }
  analysis::ShapeRequest request;
  request.frame = frame.value().get();
  request.selected = mask.value();
  request.mode = mode;
  request.masses = masses.values;
  request.mass_unit = masses.unit.has_value()
                          ? std::string_view{masses.unit.value()}
                          : std::string_view{};
  request.mass_source = masses.source;
  request.masses_estimated = masses.estimated;
  const auto shape = analysis::calculate_shape(request);
  if (!shape.has_value())
    return R::failure(shape.error());
  return R::success(ShapeAnalysisResult{
      object->id, std::move(selection_expression), mode, shape.value()});
}

operation::Result<PersistentAnalysisResult>
Workspace::store_analysis_result(AnalysisResultDraft draft) {
  const auto scientific =
      bind_scientific_result_contract(std::move(draft.provenance.scientific));
  if (!scientific.has_value())
    return operation::Result<PersistentAnalysisResult>::failure(
        scientific.error());
  draft.provenance.scientific = scientific.value();
  const auto *object = active_object();
  draft.provenance.object_name = object->system->name();
  return analysis_results_.add(std::move(draft));
}

operation::Result<ScientificResultContract>
Workspace::bind_scientific_result_contract(
    ScientificResultContract contract) const {
  const auto *object = active_object();
  if (object == nullptr)
    return operation::Result<ScientificResultContract>::failure(missing_active());
  contract.topology = {
      model::kTopologyReferenceSchemaVersion, object->id,
      object->system->topology()->version()};
  contract.coordinate_revision = object->coordinate_revision;
  contract.coordinate_source_revision =
      object->coordinate_source_revision;
  const auto fields = scientific_contract_fields(contract);
  if (!fields.has_value())
    return operation::Result<ScientificResultContract>::failure(
        fields.error());
  return operation::Result<ScientificResultContract>::success(
      std::move(contract));
}

std::optional<operation::Error> Workspace::validate_analysis_result_name(
    const std::optional<std::string> &name) const {
  return analysis_results_.validate_name(name);
}

operation::Result<PersistentAnalysisResult>
Workspace::analysis_result(std::uint64_t result_id) const {
  return analysis_results_.get(result_id);
}

AnalysisSourceStatus Workspace::analysis_source_status(
    const PersistentAnalysisResult &record) const noexcept {
  const auto found = std::find_if(objects_.begin(), objects_.end(),
                                  [&](const auto &object) {
                                    return object.id ==
                                           record.provenance.scientific.topology
                                               .object_id;
                                  });
  if (found == objects_.end()) return AnalysisSourceStatus::object_deleted;
  return assess_analysis_result(
      record,
      model::TopologySnapshotReference{
          model::kTopologyReferenceSchemaVersion, found->id,
          found->system->topology()->version()},
      found->coordinate_source_revision,
      found->coordinate_revision,
      current_analysis_algorithm_version(record.kind));
}

operation::Result<PersistentAnalysisResult>
Workspace::delete_analysis_result(std::uint64_t result_id) {
  return analysis_results_.erase(result_id);
}

operation::Result<PersistentAnalysisResult>
Workspace::set_analysis_overlay_visible(std::uint64_t result_id,
                                        bool visible) {
  return analysis_results_.set_overlay_visible(result_id, visible);
}

AnalysisResultStoreSnapshot Workspace::analysis_result_snapshot() const {
  return analysis_results_.snapshot();
}

std::optional<operation::Error> Workspace::restore_analysis_results(
    const AnalysisResultStoreSnapshot &snapshot) {
  return analysis_results_.restore(snapshot);
}

operation::Result<SpatialExtent>
Workspace::selection_extent(std::string_view selection_expression,
                            CameraStateScope state_scope,
                            operation::TaskContext *context) const {
  const auto depth_extent = selection_camera_depth_extent(
      selection_expression, state_scope, context);
  if (!depth_extent.has_value())
    return operation::Result<SpatialExtent>::failure(depth_extent.error());
  return operation::Result<SpatialExtent>::success(
      depth_extent.value().spatial);
}

operation::Result<CameraDepthExtent>
Workspace::selection_camera_depth_extent(
    std::string_view selection_expression, CameraStateScope state_scope,
    operation::TaskContext *context) const {
  const auto *object = active_object();
  if (object == nullptr)
    return operation::Result<CameraDepthExtent>::failure(missing_active());
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value())
    return operation::Result<CameraDepthExtent>::failure(mask.error());

  const auto frame_range = resolve_camera_frame_range(*object, state_scope);
  if (!frame_range.has_value())
    return operation::Result<CameraDepthExtent>::failure(frame_range.error());
  const auto first_frame_index = frame_range.value().first;
  const auto scoped_frame_count = frame_range.value().count;

  CameraDepthExtent result;
  auto &extent = result.spatial;
  model::Vec3d weighted_sum{};
  extent.evaluated_frame_count = scoped_frame_count;
  extent.selected_atom_count = static_cast<std::size_t>(std::count_if(
      mask.value().begin(), mask.value().end(),
      [](std::uint8_t selected) { return selected != 0U; }));
  bool empty = true;
  for (std::size_t frame_offset = 0; frame_offset < scoped_frame_count;
       ++frame_offset) {
    if (context != nullptr && context->cancellation.is_cancelled()) {
      return operation::Result<CameraDepthExtent>::failure(operation::Error{
          operation::ErrorCode::cancelled,
          "camera extent cancelled after " + std::to_string(frame_offset) +
              " of " + std::to_string(scoped_frame_count) + " frames",
          {}});
    }
    const auto frame = object->system->coordinates()->read_frame(
        first_frame_index + frame_offset);
    if (!frame.has_value())
      return operation::Result<CameraDepthExtent>::failure(frame.error());
    for (std::size_t index = 0; index < mask.value().size(); ++index) {
      if (mask.value()[index] == 0U)
        continue;
      if (!frame.value()->atom_present(index)) {
        ++extent.skipped_missing_atom_count;
        continue;
      }
      const auto coordinate = coordinate_at(*frame.value(), index);
      const auto depth = scene::dot(coordinate - camera().position(),
                                    camera().forward());
      if (empty) {
        result.minimum_depth = depth;
        result.maximum_depth = depth;
      } else {
        result.minimum_depth = std::min(result.minimum_depth, depth);
        result.maximum_depth = std::max(result.maximum_depth, depth);
      }
      include_point(coordinate, extent.minimum, extent.maximum, empty);
      weighted_sum = weighted_sum + coordinate;
      ++extent.used_atom_count;
    }
    if (context != nullptr && context->report_progress) {
      context->report_progress(operation::ProgressUpdate{
          0.5 * static_cast<double>(frame_offset + 1U) /
              static_cast<double>(scoped_frame_count),
          "camera extent bounds"});
    }
  }
  if (empty) {
    return operation::Result<CameraDepthExtent>::failure(operation::Error{
        operation::ErrorCode::invalid_selection,
        "camera selection does not contain present coordinates",
        "choose a non-empty selection and state scope"});
  }
  // PyMOL weighted extent: the center is the mean selected coordinate.
  extent.center = weighted_sum * (1.0 / static_cast<double>(extent.used_atom_count));
  for (std::size_t frame_offset = 0; frame_offset < scoped_frame_count;
       ++frame_offset) {
    if (context != nullptr && context->cancellation.is_cancelled()) {
      return operation::Result<CameraDepthExtent>::failure(operation::Error{
          operation::ErrorCode::cancelled,
          "camera extent radius calculation cancelled after " +
              std::to_string(frame_offset) + " of " +
              std::to_string(scoped_frame_count) + " frames",
          {}});
    }
    const auto frame = object->system->coordinates()->read_frame(
        first_frame_index + frame_offset);
    if (!frame.has_value())
      return operation::Result<CameraDepthExtent>::failure(frame.error());
    for (std::size_t index = 0; index < mask.value().size(); ++index) {
      if (mask.value()[index] == 0U || !frame.value()->atom_present(index))
        continue;
      extent.maximum_radius =
          std::max(extent.maximum_radius,
                   scene::length(coordinate_at(*frame.value(), index) -
                                 extent.center));
    }
    if (context != nullptr && context->report_progress) {
      context->report_progress(operation::ProgressUpdate{
          0.5 + 0.5 * static_cast<double>(frame_offset + 1U) /
                    static_cast<double>(scoped_frame_count),
          "camera extent radius"});
    }
  }
  return operation::Result<CameraDepthExtent>::success(result);
}

operation::Result<CameraSelectionResult>
Workspace::center_camera(std::string selection_expression,
                         bool move_origin, CameraStateScope state_scope,
                         operation::TaskContext *context) {
  const auto extent =
      selection_extent(selection_expression, state_scope, context);
  if (!extent.has_value())
    return operation::Result<CameraSelectionResult>::failure(extent.error());
  auto parameters = camera().parameters();
  if (!move_origin) {
    // PyMOL center(origin=0) re-centers the view on the selection without
    // changing the camera-space depth of the rotation origin.
    const auto origin_depth =
        scene::dot(parameters.model_origin - camera().position(), camera().forward());
    const auto center_offset =
        scene::dot(extent.value().center - parameters.model_origin, camera().forward());
    parameters.distance = origin_depth + center_offset;
  }
  parameters.target = extent.value().center;
  if (move_origin)
    parameters.model_origin = extent.value().center;
  const auto updated = set_camera(parameters);
  if (!updated.has_value())
    return operation::Result<CameraSelectionResult>::failure(updated.error());
  return operation::Result<CameraSelectionResult>::success(
      CameraSelectionResult{active_object()->id, std::move(selection_expression),
                            state_scope, extent.value(), updated.value()});
}

operation::Result<CameraSelectionResult>
Workspace::zoom_camera(std::string selection_expression, double buffer,
                       bool complete, CameraStateScope state_scope,
                       operation::TaskContext *context) {
  if (!std::isfinite(buffer)) {
    return operation::Result<CameraSelectionResult>::failure(
        invalid("camera zoom buffer must be finite"));
  }
  const auto extent =
      selection_extent(selection_expression, state_scope, context);
  if (!extent.has_value())
    return operation::Result<CameraSelectionResult>::failure(extent.error());
  auto radius = complete ? extent.value().maximum_radius
                        : weighted_half_extent(extent.value());
  radius += buffer;
  // PyMOL applies a MAX_VDW (2.5 A) floor for degenerate/small selections.
  radius = std::max(radius, 2.5);
  const auto framed = camera().frame_sphere_pymol(extent.value().center, radius);
  if (!framed.has_value())
    return operation::Result<CameraSelectionResult>::failure(framed.error());
  const auto updated = set_camera(framed.value().parameters());
  if (!updated.has_value())
    return operation::Result<CameraSelectionResult>::failure(updated.error());
  return operation::Result<CameraSelectionResult>::success(
      CameraSelectionResult{active_object()->id, std::move(selection_expression),
                            state_scope, extent.value(), updated.value()});
}

operation::Result<CameraSelectionResult>
Workspace::set_camera_origin(std::string selection_expression,
                             CameraStateScope state_scope,
                             operation::TaskContext *context) {
  const auto extent =
      selection_extent(selection_expression, state_scope, context);
  if (!extent.has_value())
    return operation::Result<CameraSelectionResult>::failure(extent.error());
  auto parameters = camera().parameters();
  parameters.model_origin = extent.value().center;
  const auto updated = set_camera(parameters);
  if (!updated.has_value())
    return operation::Result<CameraSelectionResult>::failure(updated.error());
  return operation::Result<CameraSelectionResult>::success(
      CameraSelectionResult{active_object()->id, std::move(selection_expression),
                            state_scope, extent.value(), updated.value()});
}

operation::Result<scene::Camera>
Workspace::set_camera_origin(model::Vec3d position) {
  if (!scene::is_finite(position)) {
    return operation::Result<scene::Camera>::failure(
        invalid("camera model origin position must be finite"));
  }
  auto parameters = camera().parameters();
  parameters.model_origin = position;
  return set_camera(parameters);
}

operation::Result<ObjectOriginResult>
Workspace::set_object_origin_from_selection(
    std::string object_reference, std::string selection_expression,
    CameraStateScope state_scope, operation::TaskContext *context) {
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value())
    return operation::Result<ObjectOriginResult>::failure(index.error());
  const auto extent = object_selection_extent(
      objects_[index.value()], selection_expression, state_scope, context);
  if (!extent.has_value())
    return operation::Result<ObjectOriginResult>::failure(extent.error());
  const auto updated = set_object_origin(object_reference, extent.value().center);
  if (!updated.has_value())
    return updated;
  auto result = updated.value();
  result.selection_expression = std::move(selection_expression);
  result.state_scope = state_scope;
  result.extent = extent.value();
  return operation::Result<ObjectOriginResult>::success(std::move(result));
}

operation::Result<ObjectOriginResult>
Workspace::set_object_origin(std::string object_reference,
                             model::Vec3d position) {
  if (!scene::is_finite(position)) {
    return operation::Result<ObjectOriginResult>::failure(
        invalid("object origin position must be finite"));
  }
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value())
    return operation::Result<ObjectOriginResult>::failure(index.error());
  const auto &object = objects_[index.value()];
  const auto *node = scene_->find(object.scene_node);
  if (node == nullptr) {
    return operation::Result<ObjectOriginResult>::failure(operation::Error{
        operation::ErrorCode::internal, "object scene node is missing", {}});
  }

  const auto object_name = node->name();
  auto transform = node->local_transform();
  const auto old_origin = scene::transform_point(scene::matrix(transform), {});
  transform.pivot = position;
  const auto new_origin = scene::transform_point(scene::matrix(transform), {});
  transform.translation = transform.translation + old_origin - new_origin;

  auto builder = scene::SceneBuilder::from(*scene_);
  if (const auto error = builder.set_transform(object.scene_node, transform);
      error.has_value()) {
    return operation::Result<ObjectOriginResult>::failure(*error);
  }
  const auto next_scene = builder.build();
  if (!next_scene.has_value())
    return operation::Result<ObjectOriginResult>::failure(next_scene.error());
  scene_ = next_scene.value();
  return operation::Result<ObjectOriginResult>::success(ObjectOriginResult{
      object.id, object_name, position, std::nullopt, std::nullopt,
      std::nullopt, transform, scene_->version()});
}

operation::Result<ObjectTransformResetResult>
Workspace::reset_object_transforms(std::string object_reference) {
  std::vector<std::size_t> indices;
  if (object_reference == "all") {
    indices.resize(objects_.size());
    for (std::size_t index = 0; index < indices.size(); ++index)
      indices[index] = index;
  } else {
    const auto index = object_index_by_reference(object_reference);
    if (!index.has_value()) {
      return operation::Result<ObjectTransformResetResult>::failure(
          index.error());
    }
    indices.push_back(index.value());
  }
  if (indices.empty()) {
    return operation::Result<ObjectTransformResetResult>::failure(
        missing_active());
  }

  auto builder = scene::SceneBuilder::from(*scene_);
  ObjectTransformResetResult result;
  result.object_reference = std::move(object_reference);
  result.object_ids.reserve(indices.size());
  for (const auto index : indices) {
    const auto &object = objects_[index];
    if (const auto error =
            builder.set_transform(object.scene_node, scene::Transform{});
        error.has_value()) {
      return operation::Result<ObjectTransformResetResult>::failure(*error);
    }
    result.object_ids.push_back(object.id);
  }
  const auto next_scene = builder.build();
  if (!next_scene.has_value()) {
    return operation::Result<ObjectTransformResetResult>::failure(
        next_scene.error());
  }
  scene_ = next_scene.value();
  result.scene_version = scene_->version();
  return operation::Result<ObjectTransformResetResult>::success(
      std::move(result));
}

operation::Result<CameraOrientResult>
Workspace::orient_camera(std::string selection_expression,
                         CameraStateScope state_scope,
                         operation::TaskContext *context) {
  const auto *object = active_object();
  if (object == nullptr)
    return operation::Result<CameraOrientResult>::failure(missing_active());
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value())
    return operation::Result<CameraOrientResult>::failure(mask.error());
  const auto frame_range = resolve_camera_frame_range(*object, state_scope);
  if (!frame_range.has_value())
    return operation::Result<CameraOrientResult>::failure(frame_range.error());

  SpatialExtent extent;
  extent.evaluated_frame_count = frame_range.value().count;
  extent.selected_atom_count = static_cast<std::size_t>(std::count_if(
      mask.value().begin(), mask.value().end(),
      [](std::uint8_t selected) { return selected != 0U; }));
  analysis::PrincipalMoments moments;
  model::Vec3d orient_sum{};
  bool empty = true;
  for (std::size_t frame_offset = 0; frame_offset < frame_range.value().count;
       ++frame_offset) {
    if (context != nullptr && context->cancellation.is_cancelled()) {
      return operation::Result<CameraOrientResult>::failure(operation::Error{
          operation::ErrorCode::cancelled,
          "camera orientation cancelled after " +
              std::to_string(frame_offset) + " of " +
              std::to_string(frame_range.value().count) + " frames",
          {}});
    }
    const auto frame = object->system->coordinates()->read_frame(
        frame_range.value().first + frame_offset);
    if (!frame.has_value())
      return operation::Result<CameraOrientResult>::failure(frame.error());
    for (std::size_t index = 0; index < mask.value().size(); ++index) {
      if (mask.value()[index] == 0U)
        continue;
      if (!frame.value()->atom_present(index)) {
        ++extent.skipped_missing_atom_count;
        continue;
      }
      const auto coordinate = coordinate_at(*frame.value(), index);
      orient_sum = orient_sum + coordinate;
      if (!analysis::accumulate(moments, coordinate)) {
        return operation::Result<CameraOrientResult>::failure(invalid(
            "principal-axis coordinate moments exceed the numeric range"));
      }
      include_point(coordinate, extent.minimum, extent.maximum, empty);
      ++extent.used_atom_count;
    }
    if (context != nullptr && context->report_progress) {
      context->report_progress(operation::ProgressUpdate{
          0.5 * static_cast<double>(frame_offset + 1U) /
              static_cast<double>(frame_range.value().count),
          "camera principal moments"});
    }
  }
  if (empty) {
    return operation::Result<CameraOrientResult>::failure(operation::Error{
        operation::ErrorCode::invalid_selection,
        "camera orientation selection contains no present coordinates",
        "choose a non-empty selection and state scope"});
  }
  // PyMOL orient zooms with the weighted (mean-centered) model-space extent.
  extent.center = orient_sum * (1.0 / static_cast<double>(extent.used_atom_count));
  const std::array<model::Vec3d, 3U> preferred_axes{
      camera().right(), camera().up(), camera().forward() * -1.0};
  const auto principal =
      analysis::calculate_principal_axes(moments, preferred_axes);
  if (!principal.has_value())
    return operation::Result<CameraOrientResult>::failure(principal.error());

  model::Vec3d projected_minimum;
  model::Vec3d projected_maximum;
  bool projected_empty = true;
  for (std::size_t frame_offset = 0; frame_offset < frame_range.value().count;
       ++frame_offset) {
    if (context != nullptr && context->cancellation.is_cancelled()) {
      return operation::Result<CameraOrientResult>::failure(operation::Error{
          operation::ErrorCode::cancelled,
          "camera orientation bounds cancelled after " +
              std::to_string(frame_offset) + " of " +
              std::to_string(frame_range.value().count) + " frames",
          {}});
    }
    const auto frame = object->system->coordinates()->read_frame(
        frame_range.value().first + frame_offset);
    if (!frame.has_value())
      return operation::Result<CameraOrientResult>::failure(frame.error());
    for (std::size_t index = 0; index < mask.value().size(); ++index) {
      if (mask.value()[index] == 0U || !frame.value()->atom_present(index))
        continue;
      const auto coordinate = coordinate_at(*frame.value(), index);
      include_point({scene::dot(coordinate, principal.value().axes[0]),
                     scene::dot(coordinate, principal.value().axes[1]),
                     scene::dot(coordinate, principal.value().axes[2])},
                    projected_minimum, projected_maximum, projected_empty);
      extent.maximum_radius =
          std::max(extent.maximum_radius,
                   scene::length(coordinate - extent.center));
    }
    if (context != nullptr && context->report_progress) {
      context->report_progress(operation::ProgressUpdate{
          0.5 + 0.5 * static_cast<double>(frame_offset + 1U) /
                    static_cast<double>(frame_range.value().count),
          "camera oriented bounds"});
    }
  }
  const auto local_center = (projected_minimum + projected_maximum) * 0.5;
  const auto half_extents = (projected_maximum - projected_minimum) * 0.5;
  const auto oriented_center =
      principal.value().axes[0] * local_center.x +
      principal.value().axes[1] * local_center.y +
      principal.value().axes[2] * local_center.z;
  auto parameters = camera().parameters();
  parameters.orientation = scene::quaternion_from_basis(
      principal.value().axes[0], principal.value().axes[1],
      principal.value().axes[2]);
  const auto oriented = scene::Camera::create(parameters);
  if (!oriented.has_value())
    return operation::Result<CameraOrientResult>::failure(oriented.error());
  // PyMOL orient frames the atom mean with the weighted extent; the
  // principal-frame bounding box is still reported for inspection.
  const auto framed = oriented.value().frame_sphere_pymol(
      extent.center, std::max(weighted_half_extent(extent), 2.5));
  if (!framed.has_value())
    return operation::Result<CameraOrientResult>::failure(framed.error());
  const auto updated = set_camera(framed.value().parameters());
  if (!updated.has_value())
    return operation::Result<CameraOrientResult>::failure(updated.error());
  return operation::Result<CameraOrientResult>::success(CameraOrientResult{
      object->id, std::move(selection_expression), state_scope, extent,
      principal.value(), oriented_center, half_extents, updated.value()});
}

operation::Result<CameraResetResult> Workspace::reset_camera() {
  SpatialExtent extent;
  model::Vec3d reset_sum{};
  bool empty = true;
  std::size_t molecular_object_count{};
  std::size_t volume_object_count{};
  for (const auto &object : objects_) {
    if (!scene_->effectively_visible(object.scene_node))
      continue;
    const auto frame = active_frame(object);
    if (!frame.has_value())
      continue;
    ++extent.evaluated_frame_count;
    auto contributed = false;
    for (std::size_t index = 0; index < frame.value()->atom_count(); ++index) {
      ++extent.selected_atom_count;
      if (!frame.value()->atom_present(index)) {
        ++extent.skipped_missing_atom_count;
        continue;
      }
      include_point(coordinate_at(*frame.value(), index), extent.minimum,
                    extent.maximum, empty);
      reset_sum = reset_sum + coordinate_at(*frame.value(), index);
      ++extent.used_atom_count;
      contributed = true;
    }
    if (contributed)
      ++molecular_object_count;
  }
  for (const auto &volume : volumes_) {
    if (!scene_->effectively_visible(volume.scene_node))
      continue;
    const auto shape = volume.grid->shape();
    for (const auto x : {std::size_t{0U}, shape.x - 1U}) {
      for (const auto y : {std::size_t{0U}, shape.y - 1U}) {
        for (const auto z : {std::size_t{0U}, shape.z - 1U}) {
          include_point(volume.grid->position(x, y, z), extent.minimum,
                        extent.maximum, empty);
        }
      }
    }
    ++volume_object_count;
  }

  // PyMOL reset: identity rotation, then zoom on everything with the
  // weighted extent. Projection mode and field of view are settings in
  // PyMOL and survive reset, as does the live viewport shape; every other
  // camera field returns to the application defaults.
  scene::CameraParameters parameters;
  parameters.aspect_ratio = camera().parameters().aspect_ratio;
  parameters.projection = camera().parameters().projection;
  parameters.vertical_field_of_view_radians =
      camera().parameters().vertical_field_of_view_radians;
  operation::Result<scene::Camera> reset = scene::Camera::create(parameters);
  if (!reset.has_value())
    return operation::Result<CameraResetResult>::failure(reset.error());
  std::optional<SpatialExtent> result_extent;
  if (!empty) {
    extent.center = extent.used_atom_count > 0U
                        ? reset_sum * (1.0 / static_cast<double>(extent.used_atom_count))
                        : (extent.minimum + extent.maximum) * 0.5;
    extent.maximum_radius = weighted_half_extent(extent);
    const auto radius = std::max(extent.maximum_radius, 2.5);
    parameters.model_origin = extent.center;
    reset = scene::Camera::create(parameters);
    if (!reset.has_value())
      return operation::Result<CameraResetResult>::failure(reset.error());
    reset = reset.value().frame_sphere_pymol(extent.center, radius);
    result_extent = extent;
  }
  if (!reset.has_value())
    return operation::Result<CameraResetResult>::failure(reset.error());
  const auto updated = set_camera(reset.value().parameters());
  if (!updated.has_value())
    return operation::Result<CameraResetResult>::failure(updated.error());
  return operation::Result<CameraResetResult>::success(CameraResetResult{
      updated.value(), result_extent, molecular_object_count,
      volume_object_count});
}

operation::Result<CameraClipResult>
Workspace::clip_camera(CameraClipMode mode, double distance,
                       std::optional<std::string> selection_expression,
                       CameraStateScope state_scope,
                       operation::TaskContext *context) {
  if (!std::isfinite(distance)) {
    return operation::Result<CameraClipResult>::failure(
        invalid("camera clip distance must be finite"));
  }

  auto parameters = camera().parameters();
  std::optional<CameraDepthExtent> depth_extent;
  std::optional<std::string> effective_selection;
  switch (mode) {
  case CameraClipMode::near_relative:
    parameters.near_clip -= distance;
    break;
  case CameraClipMode::far_relative:
    parameters.far_clip -= distance;
    break;
  case CameraClipMode::move:
    parameters.near_clip -= distance;
    parameters.far_clip -= distance;
    break;
  case CameraClipMode::slab: {
    if (!(distance > 0.0)) {
      return operation::Result<CameraClipResult>::failure(invalid(
          "camera clip slab thickness must be positive",
          "provide a positive slab thickness in angstrom"));
    }
    auto center_depth =
        (parameters.near_clip + parameters.far_clip) * 0.5;
    if (selection_expression.has_value() &&
        !selection_expression->empty()) {
      const auto selected = selection_camera_depth_extent(
          *selection_expression, state_scope, context);
      if (!selected.has_value())
        return operation::Result<CameraClipResult>::failure(selected.error());
      depth_extent = selected.value();
      effective_selection = std::move(selection_expression);
      center_depth = scene::dot(depth_extent->spatial.center -
                                    camera().position(),
                                camera().forward());
    }
    parameters.near_clip = center_depth - distance * 0.5;
    parameters.far_clip = center_depth + distance * 0.5;
    break;
  }
  case CameraClipMode::atoms: {
    effective_selection =
        selection_expression.has_value() && !selection_expression->empty()
            ? std::move(selection_expression)
            : std::optional<std::string>{"all"};
    const auto selected = selection_camera_depth_extent(
        *effective_selection, state_scope, context);
    if (!selected.has_value())
      return operation::Result<CameraClipResult>::failure(selected.error());
    depth_extent = selected.value();
    parameters.near_clip = depth_extent->minimum_depth - distance;
    parameters.far_clip = depth_extent->maximum_depth + distance;
    break;
  }
  case CameraClipMode::near_absolute:
    parameters.near_clip = distance;
    break;
  case CameraClipMode::far_absolute:
    parameters.far_clip = distance;
    break;
  }

  const auto updated = set_camera(parameters);
  if (!updated.has_value())
    return operation::Result<CameraClipResult>::failure(updated.error());
  return operation::Result<CameraClipResult>::success(CameraClipResult{
      mode, distance, std::move(effective_selection), std::move(depth_extent),
      state_scope, updated.value()});
}

operation::Result<CameraNavigationResult>
Workspace::move_camera(scene::CameraAxis axis, double distance) {
  const auto moved = camera().move_axis(axis, distance);
  if (!moved.has_value())
    return operation::Result<CameraNavigationResult>::failure(moved.error());
  const auto updated = set_camera(moved.value().parameters());
  if (!updated.has_value())
    return operation::Result<CameraNavigationResult>::failure(updated.error());
  return operation::Result<CameraNavigationResult>::success(
      CameraNavigationResult{axis, distance, updated.value()});
}

operation::Result<CameraNavigationResult>
Workspace::turn_camera(scene::CameraAxis axis, double angle_degrees) {
  // PyMOL turn rotates the scene by +angle, i.e. the camera by -angle.
  const auto turned = camera().turn_axis_degrees(axis, -angle_degrees);
  if (!turned.has_value())
    return operation::Result<CameraNavigationResult>::failure(turned.error());
  const auto updated = set_camera(turned.value().parameters());
  if (!updated.has_value())
    return operation::Result<CameraNavigationResult>::failure(updated.error());
  return operation::Result<CameraNavigationResult>::success(
      CameraNavigationResult{axis, angle_degrees, updated.value()});
}

operation::Result<CameraProjectionResult>
Workspace::set_camera_projection(
    scene::ProjectionMode mode,
    std::optional<double> field_of_view_degrees, bool preserve_scale) {
  const auto previous_mode = camera().parameters().projection;
  const auto previous_span = camera().vertical_span_at_target();
  const auto projected =
      camera().with_projection(mode, field_of_view_degrees, preserve_scale);
  if (!projected.has_value()) {
    return operation::Result<CameraProjectionResult>::failure(
        projected.error());
  }
  const auto updated = set_camera(projected.value().parameters());
  if (!updated.has_value()) {
    return operation::Result<CameraProjectionResult>::failure(updated.error());
  }
  return operation::Result<CameraProjectionResult>::success(
      CameraProjectionResult{
          previous_mode, mode, previous_span,
          updated.value().vertical_span_at_target(),
          updated.value().parameters().vertical_field_of_view_radians *
              180.0 / std::numbers::pi,
          preserve_scale, updated.value()});
}

operation::Result<DistanceMeasurementRecord>
Workspace::measure_distance(std::string from_expression,
                            std::string to_expression,
                            analysis::DistanceBoundary boundary) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<DistanceMeasurementRecord>::failure(
        missing_active());
  }
  const auto frame = active_frame(*object);
  if (!frame.has_value()) {
    return operation::Result<DistanceMeasurementRecord>::failure(frame.error());
  }
  const auto resolve_one =
      [&](std::string_view expression) -> operation::Result<model::AtomIndex> {
    return one_atom(*object, expression);
  };
  const auto first = resolve_one(from_expression);
  if (!first.has_value()) {
    return operation::Result<DistanceMeasurementRecord>::failure(first.error());
  }
  const auto second = resolve_one(to_expression);
  if (!second.has_value()) {
    return operation::Result<DistanceMeasurementRecord>::failure(
        second.error());
  }
  const auto distance = analysis::atom_distance(*frame.value(), first.value(),
                                                second.value(), boundary);
  if (!distance.has_value()) {
    return operation::Result<DistanceMeasurementRecord>::failure(
        distance.error());
  }
  DistanceMeasurementRecord record{
      next_measurement_id_,     object->id, std::move(from_expression),
      std::move(to_expression), boundary,   distance.value()};
  ++next_measurement_id_;
  measurements_.push_back(record);
  return operation::Result<DistanceMeasurementRecord>::success(
      std::move(record));
}

operation::Result<AngleAnalysisResult> Workspace::analyze_angle(
    std::string first_expression, std::string vertex_expression,
    std::string third_expression, analysis::DistanceBoundary boundary) {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return operation::Result<AngleAnalysisResult>::failure(missing_active());
  const auto frame = active_frame(*object);
  if (!frame.has_value())
    return operation::Result<AngleAnalysisResult>::failure(frame.error());
  const auto first = one_atom(*object, first_expression);
  if (!first.has_value())
    return operation::Result<AngleAnalysisResult>::failure(first.error());
  const auto vertex = one_atom(*object, vertex_expression);
  if (!vertex.has_value())
    return operation::Result<AngleAnalysisResult>::failure(vertex.error());
  const auto third = one_atom(*object, third_expression);
  if (!third.has_value())
    return operation::Result<AngleAnalysisResult>::failure(third.error());
  const auto measured = analysis::atom_angle(
      *frame.value(), first.value(), vertex.value(), third.value(), boundary);
  if (!measured.has_value())
    return operation::Result<AngleAnalysisResult>::failure(measured.error());
  return operation::Result<AngleAnalysisResult>::success(
      {object->id, std::move(first_expression), std::move(vertex_expression),
       std::move(third_expression), measured.value()});
}

operation::Result<DihedralAnalysisResult> Workspace::analyze_dihedral(
    std::string first_expression, std::string second_expression,
    std::string third_expression, std::string fourth_expression,
    analysis::DistanceBoundary boundary) {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return operation::Result<DihedralAnalysisResult>::failure(missing_active());
  const auto frame = active_frame(*object);
  if (!frame.has_value())
    return operation::Result<DihedralAnalysisResult>::failure(frame.error());
  const auto first = one_atom(*object, first_expression);
  if (!first.has_value())
    return operation::Result<DihedralAnalysisResult>::failure(first.error());
  const auto second = one_atom(*object, second_expression);
  if (!second.has_value())
    return operation::Result<DihedralAnalysisResult>::failure(second.error());
  const auto third = one_atom(*object, third_expression);
  if (!third.has_value())
    return operation::Result<DihedralAnalysisResult>::failure(third.error());
  const auto fourth = one_atom(*object, fourth_expression);
  if (!fourth.has_value())
    return operation::Result<DihedralAnalysisResult>::failure(fourth.error());
  const auto measured = analysis::atom_dihedral(
      *frame.value(), first.value(), second.value(), third.value(),
      fourth.value(), boundary);
  if (!measured.has_value())
    return operation::Result<DihedralAnalysisResult>::failure(measured.error());
  return operation::Result<DihedralAnalysisResult>::success(
      {object->id, std::move(first_expression), std::move(second_expression),
       std::move(third_expression), std::move(fourth_expression),
       measured.value()});
}

operation::Result<SasaAnalysisResult> Workspace::analyze_sasa(
    std::string selection_expression, double probe_radius_angstrom,
    std::size_t samples_per_atom, std::size_t evaluation_budget,
    operation::TaskContext *context) {
  auto plan = plan_sasa_analysis(std::move(selection_expression),
                                 probe_radius_angstrom, samples_per_atom,
                                 evaluation_budget);
  if (!plan.has_value())
    return operation::Result<SasaAnalysisResult>::failure(plan.error());
  operation::TaskContext fallback;
  auto candidate = build_sasa_analysis_candidate(
      std::move(plan.value()), context == nullptr ? fallback : *context);
  if (!candidate.has_value())
    return operation::Result<SasaAnalysisResult>::failure(candidate.error());
  return commit_sasa_analysis(std::move(candidate.value()));
}

operation::Result<SasaAnalysisPlan> Workspace::plan_sasa_analysis(
    std::string selection_expression, double probe_radius_angstrom,
    std::size_t samples_per_atom, std::size_t evaluation_budget) const {
  const auto *object = active_object();
  if (object == nullptr)
    return operation::Result<SasaAnalysisPlan>::failure(missing_active());
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value())
    return operation::Result<SasaAnalysisPlan>::failure(mask.error());
  const auto frame = active_frame(*object);
  if (!frame.has_value())
    return operation::Result<SasaAnalysisPlan>::failure(frame.error());
  const auto visuals = atom_visuals(*object->system->topology());
  if (!visuals.has_value())
    return operation::Result<SasaAnalysisPlan>::failure(visuals.error());
  std::vector<double> radii;
  radii.reserve(visuals.value().size());
  for (const auto &visual : visuals.value())
    radii.push_back(visual.sphere_radius);
  const auto &properties = object->system->topology()->properties();
  const auto radius_source =
      properties.find("pqr.radius") != nullptr
          ? std::string{"pqr.radius"}
      : properties.find("vdw_radius") != nullptr
          ? std::string{"vdw_radius"}
          : std::string{"molshredder-atom-visual-vdw-v1"};
  return operation::Result<SasaAnalysisPlan>::success(
      {{object->id, object->system, object->system->topology()->version(),
        object->coordinate_source_revision, object->coordinate_revision},
       frame.value(), std::move(selection_expression), radius_source,
       mask.value(), std::move(radii), probe_radius_angstrom,
       samples_per_atom, evaluation_budget});
}

operation::Result<SasaAnalysisCandidate>
Workspace::build_sasa_analysis_candidate(SasaAnalysisPlan plan,
                                         operation::TaskContext &context) {
  analysis::SasaRequest request;
  request.frame = plan.frame.get();
  request.vdw_radii_angstrom = plan.vdw_radii_angstrom;
  request.selected = plan.selected;
  request.probe_radius_angstrom = plan.probe_radius_angstrom;
  request.samples_per_atom = plan.samples_per_atom;
  request.evaluation_budget = plan.evaluation_budget;
  request.context = &context;
  const auto calculated = analysis::solvent_accessible_surface_area(request);
  if (!calculated.has_value())
    return operation::Result<SasaAnalysisCandidate>::failure(calculated.error());
  return operation::Result<SasaAnalysisCandidate>::success(
      {std::move(plan), calculated.value()});
}

operation::Result<SasaAnalysisResult> Workspace::commit_sasa_analysis(
    SasaAnalysisCandidate candidate) const {
  const auto *object = active_object();
  const auto &source = candidate.plan.source;
  if (object == nullptr || object->id != source.object_id ||
      object->system != source.system ||
      object->system->topology()->version() != source.topology_version ||
      object->coordinate_source_revision != source.coordinate_source_revision ||
      object->coordinate_revision != source.coordinate_revision) {
    return operation::Result<SasaAnalysisResult>::failure(operation::Error{
        operation::ErrorCode::stale_result,
        "SASA input changed before owner-thread commit",
        "retry SASA against the current active coordinates"});
  }
  return operation::Result<SasaAnalysisResult>::success(
      {source.object_id, std::move(candidate.plan.selection_expression),
       std::move(candidate.plan.radius_source), std::move(candidate.sasa)});
}

operation::Result<RdfAnalysisResult> Workspace::analyze_rdf(
    std::string first_expression, std::string second_expression,
    double maximum_radius, double bin_width,
    analysis::DistanceBoundary boundary,
    analysis::RdfNormalization normalization, bool same_selection,
    std::uint64_t evaluation_budget, operation::LengthUnit distance_unit,
    operation::TaskContext *context) {
  auto plan=plan_rdf_analysis(std::move(first_expression),std::move(second_expression),
      maximum_radius,bin_width,boundary,normalization,same_selection,
      evaluation_budget,distance_unit);
  if(!plan.has_value()) return operation::Result<RdfAnalysisResult>::failure(plan.error());
  operation::TaskContext fallback;
  auto candidate=build_rdf_analysis_candidate(std::move(plan.value()),context==nullptr?fallback:*context);
  if(!candidate.has_value()) return operation::Result<RdfAnalysisResult>::failure(candidate.error());
  return commit_rdf_analysis(std::move(candidate.value()));
}

operation::Result<RdfAnalysisPlan> Workspace::plan_rdf_analysis(
    std::string first_expression,std::string second_expression,
    double maximum_radius,double bin_width,analysis::DistanceBoundary boundary,
    analysis::RdfNormalization normalization,bool same_selection,
    std::uint64_t evaluation_budget,operation::LengthUnit distance_unit) const {
  const auto *object=active_object();
  if(object==nullptr) return operation::Result<RdfAnalysisPlan>::failure(missing_active());
  const auto first=selection_mask(*object,first_expression);
  if(!first.has_value()) return operation::Result<RdfAnalysisPlan>::failure(first.error());
  const auto second=selection_mask(*object,second_expression);
  if(!second.has_value()) return operation::Result<RdfAnalysisPlan>::failure(second.error());
  const auto frame=active_frame(*object);
  if(!frame.has_value()) return operation::Result<RdfAnalysisPlan>::failure(frame.error());
  if(distance_unit!=frame.value()->metadata().coordinate_unit) {
    const auto factor=distance_unit==operation::LengthUnit::angstrom?0.1:10.0;
    maximum_radius*=factor; bin_width*=factor;
  }
  return operation::Result<RdfAnalysisPlan>::success(
      {{object->id,object->system,object->system->topology()->version(),
        object->coordinate_source_revision,object->coordinate_revision},
       frame.value(),std::move(first_expression),std::move(second_expression),
       first.value(),second.value(),maximum_radius,bin_width,boundary,
       normalization,same_selection,evaluation_budget});
}

operation::Result<RdfAnalysisCandidate> Workspace::build_rdf_analysis_candidate(
    RdfAnalysisPlan plan,operation::TaskContext &context) {
  const auto calculated=analysis::radial_distribution_function(
      {plan.frame.get(),plan.first_selected,plan.second_selected,
       plan.maximum_radius,plan.bin_width,plan.boundary,plan.normalization,
       plan.same_selection,plan.evaluation_budget,&context});
  if(!calculated.has_value()) return operation::Result<RdfAnalysisCandidate>::failure(calculated.error());
  return operation::Result<RdfAnalysisCandidate>::success(
      {std::move(plan),calculated.value()});
}

operation::Result<RdfAnalysisResult> Workspace::commit_rdf_analysis(
    RdfAnalysisCandidate candidate) const {
  const auto *object=active_object();
  const auto &source=candidate.plan.source;
  if(object==nullptr||object->id!=source.object_id||object->system!=source.system||
     object->system->topology()->version()!=source.topology_version||
     object->coordinate_source_revision!=source.coordinate_source_revision||
     object->coordinate_revision!=source.coordinate_revision)
    return operation::Result<RdfAnalysisResult>::failure(operation::Error{
        operation::ErrorCode::stale_result,
        "RDF input changed before owner-thread commit",
        "retry RDF against the current active coordinates"});
  return operation::Result<RdfAnalysisResult>::success(
      {source.object_id,std::move(candidate.plan.first_expression),
       std::move(candidate.plan.second_expression),std::move(candidate.rdf)});
}

operation::Result<DistanceAnalysisOverlay>
Workspace::distance_analysis_overlay(const DistanceMeasurementRecord &record,
                                     std::string label) const {
  const auto found = std::find_if(objects_.begin(), objects_.end(),
                                  [&](const auto &object) {
                                    return object.id == record.object_id;
                                  });
  if (found == objects_.end())
    return operation::Result<DistanceAnalysisOverlay>::failure(
        {operation::ErrorCode::not_found,
         "distance source object no longer exists", {}});
  const auto frame = active_frame(*found);
  if (!frame.has_value())
    return operation::Result<DistanceAnalysisOverlay>::failure(frame.error());
  const auto &topology = *found->system->topology();
  const auto first_id = topology.atom_id(record.distance.first);
  const auto second_id = topology.atom_id(record.distance.second);
  if (!first_id.has_value() || !second_id.has_value())
    return operation::Result<DistanceAnalysisOverlay>::failure(
        {operation::ErrorCode::stale_result,
         "distance endpoint is stale for the source topology", {}});
  const model::TopologySnapshotReference snapshot{
      model::kTopologyReferenceSchemaVersion, found->id, topology.version()};
  return operation::Result<DistanceAnalysisOverlay>::success(
      DistanceAnalysisOverlay{{snapshot, *first_id},
                              {snapshot, *second_id},
                              coordinate_at(*frame.value(),
                                            record.distance.first.value),
                              coordinate_at(*frame.value(),
                                            record.distance.second.value),
                              std::move(label)});
}

operation::Result<ContactAnalysisResult> Workspace::analyze_contacts(
    std::string first_expression, std::string second_expression, double cutoff,
    analysis::DistanceBoundary boundary, bool same_selection,
    bool exclude_bonded, operation::LengthUnit cutoff_unit) {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return operation::Result<ContactAnalysisResult>::failure(missing_active());
  const auto frame = active_frame(*object);
  if (!frame.has_value())
    return operation::Result<ContactAnalysisResult>::failure(frame.error());
  const auto first = selection_mask(*object, first_expression);
  if (!first.has_value())
    return operation::Result<ContactAnalysisResult>::failure(first.error());
  const auto second =
      same_selection ? first : selection_mask(*object, second_expression);
  if (!second.has_value())
    return operation::Result<ContactAnalysisResult>::failure(second.error());
  if (cutoff_unit != frame.value()->metadata().coordinate_unit)
    cutoff *= cutoff_unit == operation::LengthUnit::angstrom ? 0.1 : 10.0;
  const auto contacts =
      analysis::find_contacts({*frame.value(), first.value(), second.value(),
                               object->system->topology()->bonds(), cutoff,
                               boundary, same_selection, exclude_bonded});
  if (!contacts.has_value())
    return operation::Result<ContactAnalysisResult>::failure(contacts.error());
  return operation::Result<ContactAnalysisResult>::success(
      {object->id, std::move(first_expression), std::move(second_expression),
       cutoff, boundary, contacts.value()});
}

operation::Result<HydrogenBondAnalysisResult> Workspace::analyze_hydrogen_bonds(
    std::string donor_expression, std::string acceptor_expression,
    double distance_cutoff, double maximum_angle_deviation_degrees,
    analysis::DistanceBoundary boundary, bool same_selection,
    operation::LengthUnit cutoff_unit) {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return operation::Result<HydrogenBondAnalysisResult>::failure(
        missing_active());
  const auto frame = active_frame(*object);
  if (!frame.has_value())
    return operation::Result<HydrogenBondAnalysisResult>::failure(
        frame.error());
  const auto donors = selection_mask(*object, donor_expression);
  if (!donors.has_value())
    return operation::Result<HydrogenBondAnalysisResult>::failure(
        donors.error());
  const auto acceptors =
      same_selection ? donors : selection_mask(*object, acceptor_expression);
  if (!acceptors.has_value())
    return operation::Result<HydrogenBondAnalysisResult>::failure(
        acceptors.error());
  const auto typing =
      analysis::resolve_hydrogen_bond_typing(*object->system->topology());
  if (!typing.has_value())
    return operation::Result<HydrogenBondAnalysisResult>::failure(
        typing.error());
  if (cutoff_unit != frame.value()->metadata().coordinate_unit)
    distance_cutoff *=
        cutoff_unit == operation::LengthUnit::angstrom ? 0.1 : 10.0;
  const auto bonds = analysis::find_hydrogen_bonds(
      {*frame.value(), *object->system->topology(), donors.value(),
       acceptors.value(), typing.value().donors, typing.value().acceptors,
       distance_cutoff, maximum_angle_deviation_degrees, boundary,
       same_selection});
  if (!bonds.has_value())
    return operation::Result<HydrogenBondAnalysisResult>::failure(
        bonds.error());
  return operation::Result<HydrogenBondAnalysisResult>::success(
      {object->id,
       std::move(donor_expression),
       std::move(acceptor_expression),
       distance_cutoff,
       maximum_angle_deviation_degrees,
       boundary,
       {typing.value().donor_source, typing.value().acceptor_source,
        typing.value().estimated},
       bonds.value()});
}

operation::Result<SecondaryStructureAnalysisResult>
Workspace::analyze_secondary_structure(
    std::string selection_expression,
    analysis::SecondaryStructureParameters parameters) {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return operation::Result<SecondaryStructureAnalysisResult>::failure(
        missing_active());
  const auto frame = active_frame(*object);
  if (!frame.has_value())
    return operation::Result<SecondaryStructureAnalysisResult>::failure(
        frame.error());
  const auto selected = selection_mask(*object, selection_expression);
  if (!selected.has_value())
    return operation::Result<SecondaryStructureAnalysisResult>::failure(
        selected.error());
  const auto assignment = analysis::assign_secondary_structure(
      *object->system->topology(), *frame.value(), parameters);
  if (!assignment.has_value())
    return operation::Result<SecondaryStructureAnalysisResult>::failure(
        assignment.error());
  std::vector<std::uint8_t> selected_residues(
      object->system->topology()->residue_count(), 0U);
  for (std::size_t atom = 0; atom < selected.value().size(); ++atom)
    if (selected.value()[atom])
      selected_residues
          [object->system->topology()->atoms()[atom].residue.value] = 1U;
  return operation::Result<SecondaryStructureAnalysisResult>::success(
      {object->id, std::move(selection_expression),
       frame.value()->metadata().coordinate_unit, parameters,
       assignment.value(), std::move(selected_residues)});
}

operation::Result<CenterTimeSeriesResult> Workspace::analyze_center_time_series(
    std::string selection_expression, analysis::CenterMode mode,
    analysis::SeriesRange range,
    analysis::MissingAtomPolicy missing_atom_policy,
    operation::TaskContext &context) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<CenterTimeSeriesResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<CenterTimeSeriesResult>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "active object has no attached trajectory",
                         "attach one with traj load first"});
  }
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value()) {
    return operation::Result<CenterTimeSeriesResult>::failure(mask.error());
  }
  analysis::MassValues masses;
  if (mode == analysis::CenterMode::center_of_mass) {
    auto resolved = analysis::masses_from_property(*object->system->topology());
    if (!resolved.has_value() &&
        resolved.error().code == operation::ErrorCode::not_found) {
      resolved =
          analysis::estimated_element_masses(*object->system->topology());
    }
    if (!resolved.has_value()) {
      return operation::Result<CenterTimeSeriesResult>::failure(
          resolved.error());
    }
    masses = std::move(resolved.value());
  }
  analysis::CenterSeriesRequest request;
  request.source = object->trajectory->cache;
  request.range = range;
  request.selected = mask.value();
  request.mode = mode;
  request.masses = masses.values;
  request.mass_unit = masses.unit.has_value() ? std::string_view{*masses.unit}
                                              : std::string_view{};
  request.mass_source = masses.source;
  request.masses_estimated = masses.estimated;
  request.missing_atom_policy = missing_atom_policy;
  const auto rows = analysis::center_series(request, context);
  if (!rows.has_value()) {
    return operation::Result<CenterTimeSeriesResult>::failure(rows.error());
  }
  return operation::Result<CenterTimeSeriesResult>::success(
      CenterTimeSeriesResult{object->id, std::move(selection_expression), mode,
                             range, rows.value()});
}

operation::Result<DistanceTimeSeriesResult>
Workspace::analyze_distance_time_series(std::string from_expression,
                                        std::string to_expression,
                                        analysis::DistanceBoundary boundary,
                                        analysis::SeriesRange range,
                                        operation::TaskContext &context) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<DistanceTimeSeriesResult>::failure(
        missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<DistanceTimeSeriesResult>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "active object has no attached trajectory",
                         "attach one with traj load first"});
  }
  const auto first = one_atom(*object, from_expression);
  if (!first.has_value()) {
    return operation::Result<DistanceTimeSeriesResult>::failure(first.error());
  }
  const auto second = one_atom(*object, to_expression);
  if (!second.has_value()) {
    return operation::Result<DistanceTimeSeriesResult>::failure(second.error());
  }
  const auto rows = analysis::distance_series(
      analysis::DistanceSeriesRequest{object->trajectory->cache, range,
                                      first.value(), second.value(), boundary},
      context);
  if (!rows.has_value()) {
    return operation::Result<DistanceTimeSeriesResult>::failure(rows.error());
  }
  return operation::Result<DistanceTimeSeriesResult>::success(
      DistanceTimeSeriesResult{object->id, std::move(from_expression),
                               std::move(to_expression), boundary, range,
                               rows.value()});
}

operation::Result<RmsdTimeSeriesResult> Workspace::analyze_rmsd_time_series(
    std::string selection_expression, std::string fit_selection_expression,
    std::size_t reference_frame, analysis::SeriesRange range,
    analysis::FitMode fit, analysis::WeightMode weight_mode,
    analysis::MissingAtomPolicy missing_atom_policy,
    operation::TaskContext &context) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<RmsdTimeSeriesResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<RmsdTimeSeriesResult>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "active object has no attached trajectory",
                         "attach one with traj load first"});
  }
  const auto selected = selection_mask(*object, selection_expression);
  if (!selected.has_value()) {
    return operation::Result<RmsdTimeSeriesResult>::failure(selected.error());
  }
  const auto fit_selected = selection_mask(*object, fit_selection_expression);
  if (!fit_selected.has_value()) {
    return operation::Result<RmsdTimeSeriesResult>::failure(
        fit_selected.error());
  }
  auto weights = resolve_weights(*object->system->topology(), weight_mode);
  if (!weights.has_value()) {
    return operation::Result<RmsdTimeSeriesResult>::failure(weights.error());
  }
  const auto rows = analysis::rmsd_series(
      analysis::RmsdSeriesRequest{object->trajectory->cache, reference_frame,
                                  range, selected.value(), fit_selected.value(),
                                  weights.value().values, fit,
                                  missing_atom_policy},
      context);
  if (!rows.has_value()) {
    return operation::Result<RmsdTimeSeriesResult>::failure(rows.error());
  }
  return operation::Result<RmsdTimeSeriesResult>::success(RmsdTimeSeriesResult{
      object->id, std::move(selection_expression),
      std::move(fit_selection_expression), reference_frame, range, fit,
      weights.value().provenance, rows.value()});
}

operation::Result<RmsdMatrixAnalysisResult> Workspace::analyze_rmsd_matrix(
    std::string selection_expression,std::string fit_selection_expression,
    analysis::SeriesRange range,analysis::FitMode fit,
    analysis::WeightMode weight_mode,
    analysis::MissingAtomPolicy missing_atom_policy,
    std::uint64_t frame_pair_budget,operation::TaskContext &context) {
  auto plan=plan_rmsd_matrix_analysis(
      std::move(selection_expression),std::move(fit_selection_expression),range,
      fit,weight_mode,missing_atom_policy,frame_pair_budget);
  if(!plan.has_value())
    return operation::Result<RmsdMatrixAnalysisResult>::failure(plan.error());
  auto candidate=build_rmsd_matrix_analysis_candidate(std::move(plan.value()),context);
  if(!candidate.has_value())
    return operation::Result<RmsdMatrixAnalysisResult>::failure(candidate.error());
  return commit_rmsd_matrix_analysis(std::move(candidate.value()));
}

operation::Result<RmsdMatrixAnalysisPlan> Workspace::plan_rmsd_matrix_analysis(
    std::string selection_expression,std::string fit_selection_expression,
    analysis::SeriesRange range,analysis::FitMode fit,
    analysis::WeightMode weight_mode,
    analysis::MissingAtomPolicy missing_atom_policy,
    std::uint64_t frame_pair_budget) const {
  const auto *object=active_object();
  if(object==nullptr) return operation::Result<RmsdMatrixAnalysisPlan>::failure(missing_active());
  if(!object->trajectory.has_value())
    return operation::Result<RmsdMatrixAnalysisPlan>::failure(operation::Error{
        operation::ErrorCode::not_found,"active object has no attached trajectory",
        "attach one with traj load first"});
  const auto selected=selection_mask(*object,selection_expression);
  if(!selected.has_value()) return operation::Result<RmsdMatrixAnalysisPlan>::failure(selected.error());
  const auto fit_selected=selection_mask(*object,fit_selection_expression);
  if(!fit_selected.has_value()) return operation::Result<RmsdMatrixAnalysisPlan>::failure(fit_selected.error());
  auto weights=resolve_weights(*object->system->topology(),weight_mode);
  if(!weights.has_value()) return operation::Result<RmsdMatrixAnalysisPlan>::failure(weights.error());
  return operation::Result<RmsdMatrixAnalysisPlan>::success(
      {{object->id,object->system,object->system->topology()->version(),
        object->coordinate_source_revision,object->coordinate_revision},
       object->trajectory->cache,std::move(selection_expression),
       std::move(fit_selection_expression),range,fit,weights.value().provenance,
       selected.value(),fit_selected.value(),std::move(weights.value().values),
       missing_atom_policy,frame_pair_budget});
}

operation::Result<RmsdMatrixAnalysisCandidate>
Workspace::build_rmsd_matrix_analysis_candidate(
    RmsdMatrixAnalysisPlan plan,operation::TaskContext &context) {
  const auto matrix=analysis::rmsd_matrix(
      {plan.cache,plan.range,plan.selected,plan.fit_selected,plan.weight_values,
       plan.fit,plan.missing_atom_policy,plan.frame_pair_budget},context);
  if(!matrix.has_value()) return operation::Result<RmsdMatrixAnalysisCandidate>::failure(matrix.error());
  return operation::Result<RmsdMatrixAnalysisCandidate>::success(
      {std::move(plan),matrix.value()});
}

operation::Result<RmsdMatrixAnalysisResult>
Workspace::commit_rmsd_matrix_analysis(RmsdMatrixAnalysisCandidate candidate) const {
  const auto *object=active_object();
  const auto &source=candidate.plan.source;
  if(object==nullptr||object->id!=source.object_id||object->system!=source.system||
     object->system->topology()->version()!=source.topology_version||
     object->coordinate_source_revision!=source.coordinate_source_revision||
     !object->trajectory.has_value()||object->trajectory->cache!=candidate.plan.cache)
    return operation::Result<RmsdMatrixAnalysisResult>::failure(operation::Error{
        operation::ErrorCode::stale_result,
        "RMSD matrix input changed before owner-thread commit",
        "retry the matrix against the current trajectory"});
  return operation::Result<RmsdMatrixAnalysisResult>::success(
      {source.object_id,std::move(candidate.plan.selection_expression),
       std::move(candidate.plan.fit_selection_expression),candidate.plan.range,
       candidate.plan.fit,std::move(candidate.plan.weights),
       std::move(candidate.matrix)});
}

operation::Result<RmsfTimeSeriesResult> Workspace::analyze_rmsf_time_series(
    std::string selection_expression, std::string fit_selection_expression,
    std::size_t reference_frame, analysis::SeriesRange range,
    analysis::FitMode fit, analysis::WeightMode weight_mode,
    analysis::MissingAtomPolicy missing_atom_policy,
    operation::TaskContext &context) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<RmsfTimeSeriesResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<RmsfTimeSeriesResult>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "active object has no attached trajectory",
                         "attach one with traj load first"});
  }
  const auto selected = selection_mask(*object, selection_expression);
  if (!selected.has_value()) {
    return operation::Result<RmsfTimeSeriesResult>::failure(selected.error());
  }
  const auto fit_selected = selection_mask(*object, fit_selection_expression);
  if (!fit_selected.has_value()) {
    return operation::Result<RmsfTimeSeriesResult>::failure(
        fit_selected.error());
  }
  auto weights = resolve_weights(*object->system->topology(), weight_mode);
  if (!weights.has_value()) {
    return operation::Result<RmsfTimeSeriesResult>::failure(weights.error());
  }
  const auto series = analysis::rmsf_series(
      analysis::RmsfSeriesRequest{object->trajectory->cache, reference_frame,
                                  range, selected.value(), fit_selected.value(),
                                  weights.value().values, fit,
                                  missing_atom_policy},
      context);
  if (!series.has_value()) {
    return operation::Result<RmsfTimeSeriesResult>::failure(series.error());
  }
  return operation::Result<RmsfTimeSeriesResult>::success(RmsfTimeSeriesResult{
      object->id, std::move(selection_expression),
      std::move(fit_selection_expression), reference_frame, range, fit,
      weights.value().provenance, series.value()});
}

operation::Result<ContactTimeSeriesResult>
Workspace::analyze_contact_time_series(
    std::string first_expression, std::string second_expression, double cutoff,
    operation::LengthUnit cutoff_unit, analysis::DistanceBoundary boundary,
    bool same_selection, bool exclude_bonded, bool collect_occupancy,
    analysis::SeriesRange range, operation::TaskContext &context) {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return operation::Result<ContactTimeSeriesResult>::failure(
        missing_active());
  if (!object->trajectory.has_value())
    return operation::Result<ContactTimeSeriesResult>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "active object has no attached trajectory",
                         "attach one with traj load first"});
  const auto first = selection_mask(*object, first_expression);
  if (!first.has_value())
    return operation::Result<ContactTimeSeriesResult>::failure(first.error());
  const auto second =
      same_selection ? first : selection_mask(*object, second_expression);
  if (!second.has_value())
    return operation::Result<ContactTimeSeriesResult>::failure(second.error());
  const auto series = analysis::contact_series(
      {object->trajectory->cache, object->system->topology().get(), range,
       first.value(), second.value(), cutoff, cutoff_unit, boundary,
       same_selection, exclude_bonded, collect_occupancy},
      context);
  if (!series.has_value())
    return operation::Result<ContactTimeSeriesResult>::failure(series.error());
  return operation::Result<ContactTimeSeriesResult>::success(
      {object->id, std::move(first_expression), std::move(second_expression),
       cutoff, boundary, range, series.value()});
}

operation::Result<HydrogenBondTimeSeriesResult>
Workspace::analyze_hydrogen_bond_time_series(
    std::string donor_expression, std::string acceptor_expression,
    double cutoff, operation::LengthUnit cutoff_unit,
    double maximum_angle_deviation_degrees, analysis::DistanceBoundary boundary,
    bool same_selection, bool collect_occupancy, analysis::SeriesRange range,
    operation::TaskContext &context) {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return operation::Result<HydrogenBondTimeSeriesResult>::failure(
        missing_active());
  if (!object->trajectory.has_value())
    return operation::Result<HydrogenBondTimeSeriesResult>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "active object has no attached trajectory",
                         "attach one with traj load first"});
  const auto donors = selection_mask(*object, donor_expression);
  if (!donors.has_value())
    return operation::Result<HydrogenBondTimeSeriesResult>::failure(
        donors.error());
  const auto acceptors =
      same_selection ? donors : selection_mask(*object, acceptor_expression);
  if (!acceptors.has_value())
    return operation::Result<HydrogenBondTimeSeriesResult>::failure(
        acceptors.error());
  const auto typing =
      analysis::resolve_hydrogen_bond_typing(*object->system->topology());
  if (!typing.has_value())
    return operation::Result<HydrogenBondTimeSeriesResult>::failure(
        typing.error());
  const auto series = analysis::hydrogen_bond_series(
      {object->trajectory->cache, object->system->topology().get(), range,
       donors.value(), acceptors.value(), typing.value().donors,
       typing.value().acceptors, cutoff, cutoff_unit,
       maximum_angle_deviation_degrees, boundary, same_selection,
       collect_occupancy},
      context);
  if (!series.has_value())
    return operation::Result<HydrogenBondTimeSeriesResult>::failure(
        series.error());
  return operation::Result<HydrogenBondTimeSeriesResult>::success(
      {object->id,
       std::move(donor_expression),
       std::move(acceptor_expression),
       cutoff,
       maximum_angle_deviation_degrees,
       boundary,
       range,
       {typing.value().donor_source, typing.value().acceptor_source,
        typing.value().estimated},
       series.value()});
}

operation::Result<TrajectoryLoadResult> Workspace::load_trajectory(
    const std::filesystem::path &path, io::TrajectoryFormat format,
    std::size_t cache_budget_bytes, std::size_t prefetch_frame_count,
    std::optional<operation::LengthUnit> coordinate_unit,
    std::optional<std::string> h5md_particle_group,
    trajectory::AtomMappingPolicy mapping_policy,
    std::span<const model::AtomId> source_to_target_atom_ids,
    std::optional<std::uint64_t> expected_topology_version,
    std::shared_ptr<const model::CoordinateSource> external_source) {
  auto plan = plan_trajectory_load(
      path, format, cache_budget_bytes, prefetch_frame_count, coordinate_unit,
      std::move(h5md_particle_group), mapping_policy,
      source_to_target_atom_ids, expected_topology_version,
      std::move(external_source));
  if (!plan.has_value())
    return operation::Result<TrajectoryLoadResult>::failure(plan.error());
  operation::TaskContext context;
  auto candidate =
      build_trajectory_load_candidate(std::move(plan.value()), context);
  if (!candidate.has_value())
    return operation::Result<TrajectoryLoadResult>::failure(
        candidate.error());
  return commit_trajectory_load(std::move(candidate.value()));
}

operation::Result<TrajectoryLoadPlan> Workspace::plan_trajectory_load(
    const std::filesystem::path &path, io::TrajectoryFormat format,
    std::size_t cache_budget_bytes, std::size_t prefetch_frame_count,
    std::optional<operation::LengthUnit> coordinate_unit,
    std::optional<std::string> h5md_particle_group,
    trajectory::AtomMappingPolicy mapping_policy,
    std::span<const model::AtomId> source_to_target_atom_ids,
    std::optional<std::uint64_t> expected_topology_version,
    std::shared_ptr<const model::CoordinateSource> external_source) const {
  const auto *object = active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryLoadPlan>::failure(missing_active());
  }
  if (expected_topology_version.has_value() &&
      *expected_topology_version != object->system->topology()->version()) {
    return operation::Result<TrajectoryLoadPlan>::failure(operation::Error{
        operation::ErrorCode::stale_result,
        "trajectory atom map targets a stale topology version",
        "refresh the topology identity and regenerate the atom map",
        {{"current_topology_version",
          std::to_string(object->system->topology()->version())},
         {"expected_topology_version",
          std::to_string(*expected_topology_version)}}});
  }
  return operation::Result<TrajectoryLoadPlan>::success(
      {*object,
       scene_,
       render_settings_,
       render_settings_.snapshot(),
       color_table_,
       path,
       format,
       cache_budget_bytes,
       prefetch_frame_count,
       coordinate_unit,
       std::move(h5md_particle_group),
       mapping_policy,
       std::vector<model::AtomId>{source_to_target_atom_ids.begin(),
                                  source_to_target_atom_ids.end()},
       expected_topology_version,
       std::move(external_source)});
}

operation::Result<TrajectoryLoadCandidate>
Workspace::build_trajectory_load_candidate(TrajectoryLoadPlan plan,
                                           operation::TaskContext &context) {
  auto fail = [](const operation::Error &error) {
    return operation::Result<TrajectoryLoadCandidate>::failure(error);
  };
  if (context.cancellation.is_cancelled())
    return fail(operation::Error{operation::ErrorCode::cancelled,
                                 "trajectory load was cancelled before open",
                                 {}});
  auto *object = &plan.object;
  std::optional<double> amber_box_angle;
  const auto &source_metadata = object->system->topology()->source_metadata();
  const auto angle = source_metadata.find("amber.box_angle_degrees");
  if (angle != source_metadata.end()) {
    double value{};
    const auto parsed =
        molshredder::core::from_chars(angle->second.data(),
                        angle->second.data() + angle->second.size(), value);
    if (parsed.ec == std::errc{} &&
        parsed.ptr == angle->second.data() + angle->second.size() &&
        std::isfinite(value)) {
      amber_box_angle = value;
    }
  }
  std::vector<std::int64_t> source_atom_ids;
  source_atom_ids.reserve(object->system->topology()->atom_count());
  for (const auto &atom : object->system->topology()->atoms()) {
    if (!atom.source_serial.has_value()) {
      source_atom_ids.clear();
      break;
    }
    source_atom_ids.push_back(*atom.source_serial);
  }
  const io::TrajectoryOpenContext open_context{
      object->system->topology()->atom_count(), std::move(source_atom_ids),
      amber_box_angle, plan.coordinate_unit,
      std::move(plan.h5md_particle_group), plan.external_source};
  const auto opened =
      io::open_trajectory(plan.path, plan.format, open_context);
  if (!opened.has_value()) {
    return fail(opened.error());
  }
  if (context.report_progress)
    context.report_progress({0.25, "trajectory-open-index"});
  if (context.cancellation.is_cancelled())
    return fail(operation::Error{operation::ErrorCode::cancelled,
                                 "trajectory load was cancelled after open",
                                 {}});
  std::vector<trajectory::TrajectoryAtomIdentity> trajectory_identities;
  trajectory_identities.reserve(opened.value().decoded_source_atom_ids.size());
  for (const auto source_id : opened.value().decoded_source_atom_ids) {
    trajectory::TrajectoryAtomIdentity identity;
    identity.source_serial = source_id;
    trajectory_identities.push_back(std::move(identity));
  }
  const auto mapping = trajectory::resolve_atom_mapping(
      {plan.mapping_policy, object->system->topology().get(),
       opened.value().source->atom_count(), trajectory_identities,
       plan.source_to_target_atom_ids});
  if (!mapping.has_value())
    return fail(mapping.error());
  std::shared_ptr<const model::CoordinateSource> mapped_source =
      opened.value().source;
  if (plan.mapping_policy == trajectory::AtomMappingPolicy::explicit_map) {
    const auto remapped = model::RemappedCoordinateSource::create(
        mapped_source, mapping.value().target_to_source);
    if (!remapped.has_value())
      return fail(remapped.error());
    mapped_source = remapped.value();
  }
  const auto semantic =
      trajectory::SemanticCoordinateSource::create(mapped_source);
  if (!semantic.has_value())
    return fail(semantic.error());
  const auto cache =
      trajectory::FrameCache::create(semantic.value(),
                                     plan.cache_budget_bytes);
  if (!cache.has_value()) {
    return fail(cache.error());
  }
  if (context.report_progress)
    context.report_progress({0.55, "trajectory-map-semantics"});
  if (context.cancellation.is_cancelled())
    return fail(operation::Error{operation::ErrorCode::cancelled,
                                 "trajectory load was cancelled after semantic "
                                 "validation",
                                 {}});
  const auto count = cache.value()->frame_count();
  if (!count.has_value() || count.value() == 0U) {
    return fail(
        invalid("trajectory attachment requires a known non-zero frame count"));
  }
  auto timeline = trajectory::PlaybackTimeline::create(count.value());
  if (!timeline.has_value()) {
    return fail(timeline.error());
  }
  const auto first_frame = cache.value()->read_frame(0U);
  if (!first_frame.has_value()) {
    return fail(first_frame.error());
  }
  const auto system =
      model::MolecularSystem::create(object->id, object->system->name(),
                                     object->system->topology(), cache.value());
  if (!system.has_value()) {
    return fail(system.error());
  }
  const auto rebuilt = rebuild_representations(*object, system.value(),
                                               *first_frame.value(), 0U,
                                               plan.render_settings,
                                               plan.color_table);
  if (!rebuilt.has_value()) {
    return fail(rebuilt.error());
  }
  if (context.report_progress)
    context.report_progress({0.85, "trajectory-first-frame-rebuild"});
  if (context.cancellation.is_cancelled())
    return fail(operation::Error{operation::ErrorCode::cancelled,
                                 "trajectory load was cancelled after first "
                                 "frame rebuild",
                                 {}});
  auto scene_builder = scene::SceneBuilder::from(*plan.scene);
  if (const auto error =
          scene_builder.replace_system(object->scene_node, system.value());
      error.has_value()) {
    return fail(*error);
  }
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value()) {
    return fail(next_scene.error());
  }
  auto clock = trajectory::PlaybackClock::create();
  if (!clock.has_value()) {
    return fail(clock.error());
  }
  auto prefetch = trajectory::PrefetchScheduler::create(cache.value());
  if (!prefetch.has_value()) {
    return fail(prefetch.error());
  }
  TrajectoryState trajectory{plan.path,
                             opened.value().format,
                             cache.value(),
                             std::move(timeline.value()),
                             std::move(clock.value()),
                             prefetch.value(),
                             plan.prefetch_frame_count,
                             mapping.value(),
                             semantic.value()->report()};
  TrajectoryLoadResult result{
      object->id,
      opened.value().format,
      opened.value().source->atom_count(),
      count.value(),
      plan.cache_budget_bytes,
      0U,
      plan.prefetch_frame_count,
      prefetch.value()->snapshot(),
      mapping.value(),
      semantic.value()->report()};
  return operation::Result<TrajectoryLoadCandidate>::success(
      {std::move(plan), system.value(), std::move(rebuilt.value()),
       std::move(trajectory), next_scene.value(), std::move(result)});
}

operation::Result<TrajectoryLoadResult>
Workspace::commit_trajectory_load(TrajectoryLoadCandidate candidate) {
  auto *object = mutable_active_object();
  const auto stale = [&](std::string message) {
    return operation::Result<TrajectoryLoadResult>::failure(operation::Error{
        operation::ErrorCode::stale_result, std::move(message),
        "retry trajectory attachment against the current Workspace state",
        {{"object_id", std::to_string(candidate.plan.object.id)}}});
  };
  if (object == nullptr || object->id != candidate.plan.object.id)
    return stale("trajectory load candidate targets a stale active object");
  if (object->system != candidate.plan.object.system ||
      object->coordinate_source_revision !=
          candidate.plan.object.coordinate_source_revision ||
      object->coordinate_revision !=
          candidate.plan.object.coordinate_revision ||
      scene_ != candidate.plan.scene ||
      render_settings_.snapshot() != candidate.plan.render_setting_snapshot ||
      color_table_ != candidate.plan.color_table ||
      object->object_color != candidate.plan.object.object_color ||
      object->atom_colors != candidate.plan.object.atom_colors ||
      object->atom_labels != candidate.plan.object.atom_labels ||
      object->cartoon_types != candidate.plan.object.cartoon_types ||
      object->representation_visibility.snapshot() !=
          candidate.plan.object.representation_visibility.snapshot())
    return stale("trajectory load candidate uses stale molecular or visual state");
  const auto current_selections = object->selections.list();
  const auto planned_selections = candidate.plan.object.selections.list();
  const auto same_selections =
      current_selections.size() == planned_selections.size() &&
      std::equal(current_selections.begin(), current_selections.end(),
                 planned_selections.begin(),
                 [](const auto &left, const auto &right) {
                   return left.name == right.name &&
                          left.expression == right.expression &&
                          left.dynamic == right.dynamic;
                 });
  const auto same_representations =
      object->representations.size() ==
          candidate.plan.object.representations.size() &&
      std::equal(object->representations.begin(), object->representations.end(),
                 candidate.plan.object.representations.begin(),
                 [](const auto &left, const auto &right) {
                   return left.kind == right.kind &&
                          left.selection_expression ==
                              right.selection_expression;
                 });
  if (!same_selections || !same_representations)
    return stale("trajectory load candidate uses stale representation inputs");
  if (object->coordinate_revision ==
      std::numeric_limits<std::uint64_t>::max())
    return operation::Result<TrajectoryLoadResult>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "coordinate revision space is exhausted", {}});
  if (object->coordinate_source_revision ==
      std::numeric_limits<std::uint64_t>::max())
    return operation::Result<TrajectoryLoadResult>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "coordinate source revision space is exhausted", {}});
  object->system = std::move(candidate.system);
  ++object->coordinate_source_revision;
  ++object->coordinate_revision;
  object->representations = std::move(candidate.representations);
  object->molecular_surface.reset();
  object->trajectory = std::move(candidate.trajectory);
  scene_ = std::move(candidate.scene);
  candidate.result.prefetch = schedule_prefetch(*object->trajectory);
  return operation::Result<TrajectoryLoadResult>::success(
      std::move(candidate.result));
}

operation::Result<TrajectoryFrameResult>
Workspace::set_trajectory_frame(std::size_t frame_index) {
  const auto plan = plan_trajectory_frame(frame_index);
  if (!plan.has_value())
    return operation::Result<TrajectoryFrameResult>::failure(plan.error());
  operation::TaskContext context;
  auto candidate =
      build_trajectory_frame_candidate(plan.value(), context);
  if (!candidate.has_value())
    return operation::Result<TrajectoryFrameResult>::failure(
        candidate.error());
  return commit_trajectory_frame(std::move(candidate.value()));
}

operation::Result<TrajectoryFramePlan>
Workspace::plan_trajectory_frame(std::size_t frame_index) const {
  const auto *object = active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryFramePlan>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<TrajectoryFramePlan>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "active object has no attached trajectory", "run traj load first"});
  }
  auto next_timeline = object->trajectory->timeline;
  if (const auto error = next_timeline.seek(frame_index); error.has_value()) {
    return operation::Result<TrajectoryFramePlan>::failure(*error);
  }
  return operation::Result<TrajectoryFramePlan>::success(
      {*object, render_settings_, render_settings_.snapshot(), frame_index,
       color_table_});
}

operation::Result<TrajectoryFrameCandidate>
Workspace::build_trajectory_frame_candidate(TrajectoryFramePlan plan,
                                             operation::TaskContext &context) {
  if (!plan.object.trajectory.has_value())
    return operation::Result<TrajectoryFrameCandidate>::failure(
        operation::Error{operation::ErrorCode::invalid_argument,
                         "trajectory frame plan has no attached source", {}});
  // Interactive frame work supersedes speculative read-ahead. A reader call
  // already in progress remains cooperative and is discarded after return.
  plan.object.trajectory->prefetch->cancel();
  if (context.cancellation.is_cancelled())
    return operation::Result<TrajectoryFrameCandidate>::failure(
        operation::Error{operation::ErrorCode::cancelled,
                         "trajectory frame candidate was cancelled before "
                         "decode",
                         {}});
  const auto frame =
      plan.object.trajectory->cache->read_frame(plan.frame_index);
  if (!frame.has_value()) {
    return operation::Result<TrajectoryFrameCandidate>::failure(frame.error());
  }
  if (context.report_progress)
    context.report_progress({0.45, "trajectory-frame-decode"});
  if (context.cancellation.is_cancelled())
    return operation::Result<TrajectoryFrameCandidate>::failure(
        operation::Error{operation::ErrorCode::cancelled,
                         "trajectory frame candidate was cancelled after "
                         "decode",
                         {}});
  const auto rebuilt = rebuild_representations(
      plan.object, plan.object.system, *frame.value(), plan.frame_index,
      plan.render_settings, plan.color_table);
  if (!rebuilt.has_value()) {
    return operation::Result<TrajectoryFrameCandidate>::failure(
        rebuilt.error());
  }
  if (context.report_progress)
    context.report_progress({0.85, "trajectory-frame-rebuild"});
  return operation::Result<TrajectoryFrameCandidate>::success(
      {std::move(plan), frame.value(), std::move(rebuilt.value())});
}

operation::Result<TrajectoryFrameResult>
Workspace::commit_trajectory_frame(TrajectoryFrameCandidate candidate) {
  auto *object = mutable_active_object();
  const auto stale = [&](std::string message) {
    return operation::Result<TrajectoryFrameResult>::failure(operation::Error{
        operation::ErrorCode::stale_result, std::move(message),
        "retry the seek against the current Workspace state",
        {{"object_id", std::to_string(candidate.plan.object.id)},
         {"frame", std::to_string(candidate.plan.frame_index)}}});
  };
  if (object == nullptr || object->id != candidate.plan.object.id)
    return stale("trajectory frame candidate targets a stale active object");
  if (!object->trajectory.has_value() ||
      !candidate.plan.object.trajectory.has_value() ||
      object->system != candidate.plan.object.system ||
      object->coordinate_source_revision !=
          candidate.plan.object.coordinate_source_revision ||
      object->coordinate_revision !=
          candidate.plan.object.coordinate_revision ||
      object->trajectory->cache != candidate.plan.object.trajectory->cache ||
      object->system->topology()->version() !=
          candidate.plan.object.system->topology()->version())
    return stale("trajectory frame candidate targets stale molecular data");
  if (render_settings_.snapshot() != candidate.plan.render_setting_snapshot ||
      color_table_ != candidate.plan.color_table ||
      object->object_color != candidate.plan.object.object_color ||
      object->atom_colors != candidate.plan.object.atom_colors ||
      object->atom_labels != candidate.plan.object.atom_labels ||
      object->cartoon_types != candidate.plan.object.cartoon_types ||
      object->representation_visibility.snapshot() !=
          candidate.plan.object.representation_visibility.snapshot())
    return stale("trajectory frame candidate uses stale visual settings");
  const auto current_selections = object->selections.list();
  const auto planned_selections = candidate.plan.object.selections.list();
  const auto same_selections =
      current_selections.size() == planned_selections.size() &&
      std::equal(current_selections.begin(), current_selections.end(),
                 planned_selections.begin(),
                 [](const auto &left, const auto &right) {
                   return left.name == right.name &&
                          left.expression == right.expression &&
                          left.dynamic == right.dynamic;
                 });
  const auto same_representations =
      object->representations.size() ==
          candidate.plan.object.representations.size() &&
      std::equal(object->representations.begin(), object->representations.end(),
                 candidate.plan.object.representations.begin(),
                 [](const auto &left, const auto &right) {
                   return left.kind == right.kind &&
                          left.selection_expression ==
                              right.selection_expression;
                 });
  if (!same_selections || !same_representations)
    return stale("trajectory frame candidate uses stale representation inputs");
  const auto previous_frame = object->trajectory->timeline.snapshot().frame;
  if (candidate.plan.frame_index != previous_frame &&
      object->coordinate_revision ==
          std::numeric_limits<std::uint64_t>::max())
    return operation::Result<TrajectoryFrameResult>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "coordinate revision space is exhausted", {}});
  auto next_timeline = object->trajectory->timeline;
  if (const auto error = next_timeline.seek(candidate.plan.frame_index);
      error.has_value())
    return operation::Result<TrajectoryFrameResult>::failure(*error);
  object->trajectory->timeline = std::move(next_timeline);
  if (candidate.plan.frame_index != previous_frame)
    ++object->coordinate_revision;
  if (candidate.plan.frame_index != previous_frame)
    object->molecular_surface.reset();
  object->trajectory->clock.reset();
  object->representations = std::move(candidate.representations);
  const auto prefetch = schedule_prefetch(*object->trajectory);
  return operation::Result<TrajectoryFrameResult>::success(frame_result(
      object->id, object->trajectory->timeline, object->trajectory->clock,
      *candidate.frame, object->representations.size(), 0U, 0U, false,
      prefetch));
}

operation::Result<TrajectoryFrameResult>
Workspace::play_trajectory(trajectory::PlaybackMode mode,
                           trajectory::PlaybackDirection direction,
                           std::size_t transitions) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryFrameResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "active object has no attached trajectory", "run traj load first"});
  }
  auto next_timeline = object->trajectory->timeline;
  next_timeline.set_mode(mode);
  next_timeline.set_direction(direction);
  next_timeline.play();
  const auto advanced = next_timeline.advance(transitions);
  const auto frame_changed =
      advanced.snapshot.frame != object->trajectory->timeline.snapshot().frame;
  const auto frame =
      object->trajectory->cache->read_frame(advanced.snapshot.frame);
  if (!frame.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(frame.error());
  }
  std::vector<RepresentationRecord> rebuilt;
  if (frame_changed) {
    const auto candidate = rebuild_representations(
        *object, object->system, *frame.value(), advanced.snapshot.frame,
        render_settings_, color_table_);
    if (!candidate.has_value()) {
      return operation::Result<TrajectoryFrameResult>::failure(
          candidate.error());
    }
    rebuilt = std::move(candidate.value());
  }
  if (frame_changed && object->coordinate_revision ==
                           std::numeric_limits<std::uint64_t>::max())
    return operation::Result<TrajectoryFrameResult>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "coordinate revision space is exhausted", {}});
  object->trajectory->timeline = std::move(next_timeline);
  if (frame_changed) {
    ++object->coordinate_revision;
    object->molecular_surface.reset();
  }
  const auto rebuilt_count = rebuilt.size();
  if (!rebuilt.empty())
    object->representations = std::move(rebuilt);
  const auto prefetch = schedule_prefetch(*object->trajectory);
  return operation::Result<TrajectoryFrameResult>::success(frame_result(
      object->id, object->trajectory->timeline, object->trajectory->clock,
      *frame.value(), rebuilt_count, advanced.transitions,
      advanced.boundary_crossings, false, prefetch));
}

operation::Result<TrajectoryFrameResult>
Workspace::configure_trajectory_range(trajectory::PlaybackRange range,
                                      trajectory::PlaybackMode mode,
                                      trajectory::PlaybackDirection direction) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryFrameResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "active object has no attached trajectory", "run traj load first"});
  }
  const auto count = object->trajectory->cache->frame_count();
  if (!count.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(
        invalid("trajectory range requires a known frame count"));
  }
  auto timeline =
      trajectory::PlaybackTimeline::create(*count, range, mode, direction);
  if (!timeline.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(timeline.error());
  }
  const auto frame_index = timeline.value().snapshot().frame;
  const auto frame_changed =
      frame_index != object->trajectory->timeline.snapshot().frame;
  const auto frame = object->trajectory->cache->read_frame(frame_index);
  if (!frame.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(frame.error());
  }
  const auto rebuilt = rebuild_representations(*object, object->system,
                                               *frame.value(), frame_index,
                                               render_settings_, color_table_);
  if (!rebuilt.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(rebuilt.error());
  }
  if (frame_changed && object->coordinate_revision ==
                           std::numeric_limits<std::uint64_t>::max())
    return operation::Result<TrajectoryFrameResult>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "coordinate revision space is exhausted", {}});
  object->trajectory->timeline = std::move(timeline.value());
  if (frame_changed) {
    ++object->coordinate_revision;
    object->molecular_surface.reset();
  }
  object->trajectory->clock.reset();
  object->representations = std::move(rebuilt.value());
  const auto prefetch = schedule_prefetch(*object->trajectory);
  return operation::Result<TrajectoryFrameResult>::success(frame_result(
      object->id, object->trajectory->timeline, object->trajectory->clock,
      *frame.value(), object->representations.size(), 0U, 0U, false, prefetch));
}

operation::Result<TrajectoryFrameResult> Workspace::pause_trajectory() {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryFrameResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "active object has no attached trajectory", "run traj load first"});
  }
  auto timeline = object->trajectory->timeline;
  auto clock = object->trajectory->clock;
  timeline.pause();
  clock.reset();
  const auto frame = active_frame(*object);
  if (!frame.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(frame.error());
  }
  object->trajectory->timeline = std::move(timeline);
  object->trajectory->clock = std::move(clock);
  object->trajectory->prefetch->cancel();
  return operation::Result<TrajectoryFrameResult>::success(
      frame_result(object->id, object->trajectory->timeline,
                   object->trajectory->clock, *frame.value(), 0U, 0U, 0U, false,
                   object->trajectory->prefetch->snapshot()));
}

operation::Result<TrajectoryFrameResult>
Workspace::set_trajectory_speed(double frames_per_second) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryFrameResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "active object has no attached trajectory", "run traj load first"});
  }
  auto clock = object->trajectory->clock;
  if (const auto error = clock.set_frames_per_second(frames_per_second);
      error.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(*error);
  }
  const auto frame = active_frame(*object);
  if (!frame.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(frame.error());
  }
  object->trajectory->clock = std::move(clock);
  return operation::Result<TrajectoryFrameResult>::success(
      frame_result(object->id, object->trajectory->timeline,
                   object->trajectory->clock, *frame.value(), 0U, 0U, 0U, false,
                   object->trajectory->prefetch->snapshot()));
}

operation::Result<TrajectoryFrameResult>
Workspace::tick_trajectory(double elapsed_seconds) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryFrameResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "active object has no attached trajectory", "run traj load first"});
  }
  auto timeline = object->trajectory->timeline;
  auto clock = object->trajectory->clock;
  trajectory::ClockAdvance clock_advance;
  trajectory::AdvanceResult advanced{timeline.snapshot(), 0U, 0U};
  if (timeline.snapshot().playing) {
    const auto consumed = clock.consume_elapsed_seconds(elapsed_seconds);
    if (!consumed.has_value()) {
      return operation::Result<TrajectoryFrameResult>::failure(
          consumed.error());
    }
    clock_advance = consumed.value();
    advanced = timeline.advance(clock_advance.transitions);
    if (!advanced.snapshot.playing)
      clock.reset();
  } else if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0) {
    return operation::Result<TrajectoryFrameResult>::failure(
        invalid("playback elapsed time must be finite and non-negative"));
  }
  const auto frame =
      object->trajectory->cache->read_frame(advanced.snapshot.frame);
  if (!frame.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(frame.error());
  }
  std::vector<RepresentationRecord> rebuilt;
  const auto frame_changed = advanced.snapshot.frame !=
                             object->trajectory->timeline.snapshot().frame;
  if (frame_changed) {
    const auto candidate = rebuild_representations(
        *object, object->system, *frame.value(), advanced.snapshot.frame,
        render_settings_, color_table_);
    if (!candidate.has_value()) {
      return operation::Result<TrajectoryFrameResult>::failure(
          candidate.error());
    }
    rebuilt = std::move(candidate.value());
  }
  if (frame_changed && object->coordinate_revision ==
                           std::numeric_limits<std::uint64_t>::max())
    return operation::Result<TrajectoryFrameResult>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "coordinate revision space is exhausted", {}});
  object->trajectory->timeline = std::move(timeline);
  if (frame_changed) {
    ++object->coordinate_revision;
    object->molecular_surface.reset();
  }
  object->trajectory->clock = std::move(clock);
  const auto rebuilt_count = rebuilt.size();
  if (!rebuilt.empty())
    object->representations = std::move(rebuilt);
  const auto prefetch = object->trajectory->timeline.snapshot().playing
                            ? schedule_prefetch(*object->trajectory)
                            : (object->trajectory->prefetch->cancel(),
                               object->trajectory->prefetch->snapshot());
  return operation::Result<TrajectoryFrameResult>::success(frame_result(
      object->id, object->trajectory->timeline, object->trajectory->clock,
      *frame.value(), rebuilt_count, advanced.transitions,
      advanced.boundary_crossings, clock_advance.catch_up_limited, prefetch));
}

} // namespace molshredder::application
