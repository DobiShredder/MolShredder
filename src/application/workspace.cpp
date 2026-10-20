#include "molshredder/application/workspace.hpp"
#include "molshredder/application/movie_mapping.hpp"
#include "molshredder/application/video_encoder.hpp"
#include "molshredder/analysis/sequence_alignment.hpp"

#include <algorithm>
#include <numeric>
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

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
frame_with_coordinates(const model::CoordinateFrame &frame,
                       const std::vector<std::size_t> &atoms,
                       const std::vector<model::Vec3d> &positions) {
  using R = operation::Result<std::shared_ptr<const model::CoordinateFrame>>;
  if (atoms.size() != positions.size())
    return R::failure(invalid("coordinate edit needs one position per atom"));
  model::CoordinateBuffer buffer = std::visit(
      [&](const auto &source) -> model::CoordinateBuffer {
        using Vector = std::decay_t<decltype(source)>;
        using Scalar = typename Vector::value_type;
        using Component = decltype(Scalar{}.x);
        auto values = source;
        for (std::size_t i = 0; i < atoms.size(); ++i)
          if (atoms[i] < values.size())
            values[atoms[i]] = Scalar{static_cast<Component>(positions[i].x),
                                      static_cast<Component>(positions[i].y),
                                      static_cast<Component>(positions[i].z)};
        return model::CoordinateBuffer{std::move(values)};
      },
      frame.positions().values());
  if (!buffer.all_finite())
    return R::failure(invalid("coordinate edit overflows the source precision",
                              "use finite values representable by the coordinate source"));
  return model::CoordinateFrame::create(std::move(buffer), frame.velocities(),
                                        frame.presence(), frame.metadata());
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
  // VMD representation instances: each draws its own selection with its own
  // style and coloring on top of the show/hide representations.
  for (const auto &instance : object.rep_instances) {
    if (!instance.visible) continue;
    const auto info = find_vmd_style(instance.style);
    if (!info.has_value() || !info->renderable) continue;
    std::vector<std::uint8_t> mask(system->topology()->atom_count(), 0U);
    if (instance.frozen && !instance.selection_update) {
      for (const auto id : instance.frozen_atoms)
        if (const auto atom = system->topology()->atom_index(model::AtomId{id}); atom.has_value())
          mask[atom->value] = 1U;
    } else {
      const auto parsed = selection::Expression::parse(instance.selection);
      if (!parsed.has_value()) continue;
      const selection::EvaluationContext selection_context{
          &frame, frame_index, system->coordinates().get(), system->name()};
      const auto evaluated = selection::evaluate(
          parsed.value(), *system->topology(),
          [&](std::string_view name) {
            return object.selections.evaluate(name, *system->topology(), selection_context);
          },
          selection_context);
      if (!evaluated.has_value()) continue;
      mask = evaluated.value();
    }
    if (std::none_of(mask.begin(), mask.end(), [](std::uint8_t value) { return value != 0U; }))
      continue;
    auto instance_visuals = visuals.value();
    const auto instance_colors = vmd_atom_colors(
        instance, *system->topology(), frame,
        static_cast<std::int32_t>((object.id + 32U) % 33U));
    for (std::size_t atom = 0; atom < instance_visuals.size() && atom < instance_colors.size(); ++atom)
      instance_visuals[atom].color = instance_colors[atom];
    for (const auto kind : info->kinds) {
      render::RepresentationRequest request{system->topology().get(),
                                            &frame,
                                            frame_index,
                                            object.scene_node.value,
                                            instance_visuals,
                                            mask,
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
      double sphere_scale = 1.0;
      apply_vmd_style_parameters(instance, kind, request.style, sphere_scale);
      request.style.sphere_scale = sphere_scale;
      const auto packet = render::build_representation(request);
      if (!packet.has_value()) continue;
      rebuilt.push_back(RepresentationRecord{kind, "<rep:" + instance.name + ">", packet.value()});
    }
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
Workspace::store_named_scene(std::string name, SceneInsert insert) {
  if (const auto error = validate_view_name(name); error.has_value())
    return operation::Result<NamedSceneStoreResult>::failure(*error);
  auto snapshot = std::make_shared<Workspace>(*this);
  snapshot->named_scenes_.clear();
  snapshot->scene_order_.clear();
  snapshot->current_scene_name_.reset();
  const auto replaced = named_scenes_.contains(name);
  const auto anchor = current_scene_name_;
  named_scenes_.insert_or_assign(name, std::move(snapshot));
  const auto existing =
      std::find(scene_order_.begin(), scene_order_.end(), name);
  if (existing == scene_order_.end() || insert != SceneInsert::end) {
    if (existing != scene_order_.end()) scene_order_.erase(existing);
    auto position = scene_order_.end();
    if (insert != SceneInsert::end && anchor.has_value()) {
      const auto found =
          std::find(scene_order_.begin(), scene_order_.end(), *anchor);
      if (found != scene_order_.end())
        position = insert == SceneInsert::before ? found : found + 1;
    }
    scene_order_.insert(position, name);
  }
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
  candidate.scene_order_ = scene_order_;
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
  std::erase(scene_order_, std::string{name});
  if (current_scene_name_ == name) current_scene_name_.reset();
  return operation::Result<NamedSceneDeleteResult>::success(
      NamedSceneDeleteResult{std::string{name}, named_scenes_.size(), false});
}

NamedSceneDeleteResult Workspace::clear_named_scenes() {
  named_scenes_.clear();
  scene_order_.clear();
  current_scene_name_.reset();
  return NamedSceneDeleteResult{"", 0U, true};
}

std::vector<NamedSceneRecord> Workspace::list_named_scenes() const {
  std::vector<NamedSceneRecord> result;
  result.reserve(scene_order_.size());
  for (const auto &name : scene_order_) {
    const auto found = named_scenes_.find(name);
    if (found == named_scenes_.end()) continue;
    result.push_back(NamedSceneRecord{
        name, found->second->object_count(), found->second->volume_count(),
        current_scene_name_.has_value() && *current_scene_name_ == name});
  }
  return result;
}

operation::Result<NamedSceneRecord>
Workspace::rename_named_scene(std::string_view name, std::string new_name) {
  using R = operation::Result<NamedSceneRecord>;
  if (const auto error = validate_view_name(name); error.has_value())
    return R::failure(*error);
  if (const auto error = validate_view_name(new_name); error.has_value())
    return R::failure(*error);
  auto node = named_scenes_.extract(std::string{name});
  if (node.empty())
    return R::failure(operation::Error{
        operation::ErrorCode::not_found,
        "named scene does not exist: " + std::string{name},
        "use scene list to inspect stored scene names"});
  if (new_name != name && named_scenes_.contains(new_name)) {
    named_scenes_.insert(std::move(node));
    return R::failure(invalid("a scene named '" + new_name + "' already exists",
                              "choose an unused scene name"));
  }
  node.key() = new_name;
  const auto snapshot = node.mapped();
  named_scenes_.insert(std::move(node));
  std::replace(scene_order_.begin(), scene_order_.end(), std::string{name}, new_name);
  if (current_scene_name_ == name) current_scene_name_ = new_name;
  if (movie_.has_value())
    for (auto &[frame, keyframe] : movie_->keyframes)
      if (keyframe.scene_name == name) keyframe.scene_name = new_name;
  const bool is_current =
      current_scene_name_.has_value() && *current_scene_name_ == new_name;
  return R::success(NamedSceneRecord{std::move(new_name), snapshot->object_count(),
                                     snapshot->volume_count(), is_current});
}

operation::Result<NamedSceneStoreResult> Workspace::update_named_scene() {
  if (!current_scene_name_.has_value())
    return operation::Result<NamedSceneStoreResult>::failure(operation::Error{
        operation::ErrorCode::not_found, "no scene is current",
        "recall or store a scene first, or use scene store --name NAME"});
  return store_named_scene(*current_scene_name_);
}

operation::Result<NamedSceneRecord>
Workspace::step_named_scene(SceneStep step, bool wrap) {
  using R = operation::Result<NamedSceneRecord>;
  if (scene_order_.empty())
    return R::failure(operation::Error{
        operation::ErrorCode::not_found, "no scenes are stored",
        "use scene store --name NAME first"});
  const auto count = scene_order_.size();
  const auto current = current_scene_name_.has_value()
                           ? std::find(scene_order_.begin(), scene_order_.end(),
                                       *current_scene_name_)
                           : scene_order_.end();
  std::size_t target{};
  switch (step) {
  case SceneStep::first: target = 0U; break;
  case SceneStep::last: target = count - 1U; break;
  case SceneStep::next:
    if (current == scene_order_.end()) { target = 0U; break; }
    target = static_cast<std::size_t>(current - scene_order_.begin()) + 1U;
    if (target >= count) {
      if (!wrap)
        return R::failure(operation::Error{
            operation::ErrorCode::not_found, "already at the last scene",
            "pass --wrap true to continue from the first scene"});
      target = 0U;
    }
    break;
  case SceneStep::previous:
    if (current == scene_order_.end()) { target = count - 1U; break; }
    target = static_cast<std::size_t>(current - scene_order_.begin());
    if (target == 0U) {
      if (!wrap)
        return R::failure(operation::Error{
            operation::ErrorCode::not_found, "already at the first scene",
            "pass --wrap true to continue from the last scene"});
      target = count - 1U;
    } else {
      --target;
    }
    break;
  }
  const auto name = scene_order_[target];
  return recall_named_scene(name);
}

namespace {

// Natural order: digit runs compare by value, other bytes by value.
bool natural_less(const std::string &left, const std::string &right) {
  std::size_t a{}, b{};
  while (a < left.size() && b < right.size()) {
    if (std::isdigit(static_cast<unsigned char>(left[a])) != 0 &&
        std::isdigit(static_cast<unsigned char>(right[b])) != 0) {
      auto a_end = a, b_end = b;
      while (a_end < left.size() &&
             std::isdigit(static_cast<unsigned char>(left[a_end])) != 0) ++a_end;
      while (b_end < right.size() &&
             std::isdigit(static_cast<unsigned char>(right[b_end])) != 0) ++b_end;
      auto a_start = a, b_start = b;
      while (a_start + 1U < a_end && left[a_start] == '0') ++a_start;
      while (b_start + 1U < b_end && right[b_start] == '0') ++b_start;
      const auto a_digits = a_end - a_start;
      const auto b_digits = b_end - b_start;
      if (a_digits != b_digits) return a_digits < b_digits;
      const auto compared = left.compare(a_start, a_digits, right, b_start, b_digits);
      if (compared != 0) return compared < 0;
      a = a_end;
      b = b_end;
    } else {
      if (left[a] != right[b])
        return static_cast<unsigned char>(left[a]) < static_cast<unsigned char>(right[b]);
      ++a;
      ++b;
    }
  }
  return left.size() - a < right.size() - b;
}

} // namespace

operation::Result<std::vector<std::string>>
Workspace::order_named_scenes(const std::vector<std::string> &names, bool sort,
                              std::string_view location) {
  using R = operation::Result<std::vector<std::string>>;
  if (location != "top" && location != "bottom" && location != "current")
    return R::failure(invalid("scene location must be top, bottom or current"));
  std::vector<std::string> moved;
  for (const auto &name : names) {
    if (!named_scenes_.contains(name))
      return R::failure(operation::Error{
          operation::ErrorCode::not_found,
          "named scene does not exist: " + name,
          "use scene list to inspect stored scene names", {}});
    if (std::find(moved.begin(), moved.end(), name) == moved.end())
      moved.push_back(name);
  }
  if (moved.empty()) {
    if (!sort)
      return R::failure(invalid("scene order needs scene names or --sort true"));
    moved = scene_order_;
  }
  if (sort) std::stable_sort(moved.begin(), moved.end(), natural_less);
  std::vector<std::string> remaining;
  for (const auto &name : scene_order_)
    if (std::find(moved.begin(), moved.end(), name) == moved.end())
      remaining.push_back(name);
  auto position = remaining.begin();
  if (location == "bottom") {
    position = remaining.end();
  } else if (location == "current") {
    position = remaining.end();
    if (current_scene_name_.has_value()) {
      const auto found =
          std::find(remaining.begin(), remaining.end(), *current_scene_name_);
      if (found != remaining.end()) position = found;
    }
  }
  remaining.insert(position, moved.begin(), moved.end());
  scene_order_ = std::move(remaining);
  return R::success(scene_order_);
}

operation::Result<std::vector<ColorUsage>>
Workspace::color_usage(std::string selection_expression) const {
  using R = operation::Result<std::vector<ColorUsage>>;
  const auto *object = active_object();
  if (object == nullptr) return R::failure(missing_active());
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value()) return R::failure(mask.error());
  const auto &topology = *object->system->topology();
  std::map<std::int32_t, std::size_t> counts;
  for (std::size_t atom = 0; atom < mask.value().size(); ++atom) {
    if (mask.value()[atom] == 0U) continue;
    std::int32_t index = render::kColorDefault;
    if (object->atom_colors != nullptr) {
      const auto id = topology.atom_id(model::AtomIndex{atom});
      if (id.has_value()) {
        const auto found = std::lower_bound(
            object->atom_colors->begin(), object->atom_colors->end(), id->value,
            [](const auto &entry, std::uint64_t value) { return entry.first < value; });
        if (found != object->atom_colors->end() && found->first == id->value)
          index = found->second;
      }
    }
    if (index == render::kColorDefault) index = object->object_color;
    ++counts[index];
  }
  std::vector<ColorUsage> usage;
  for (const auto &[index, count] : counts)
    usage.push_back({index, color_table_.name(index).value_or(
                                index == render::kColorDefault ? "default" : ""),
                     count});
  return R::success(std::move(usage));
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

operation::Result<MovieTimelineResult>
Workspace::map_movie_states(std::string_view specification,
                            std::optional<std::size_t> start_frame) {
  using R = operation::Result<MovieTimelineResult>;
  if (start_frame.has_value() && *start_frame == 0U)
    return R::failure(invalid("movie frames are numbered from 1"));
  std::size_t current_state = 0;
  std::optional<std::size_t> available;
  if (const auto *object = active_object();
      object != nullptr && object->trajectory.has_value()) {
    available = object->trajectory->cache->frame_count();
    current_state = object->trajectory->timeline.snapshot().frame;
  } else if (object != nullptr) {
    available = object->system->coordinates()->frame_count();
  }
  const auto parsed = parse_movie_state_map(specification, current_state);
  if (!parsed.has_value()) return R::failure(parsed.error());
  if (available.has_value()) {
    for (const auto state : parsed.value())
      if (state >= *available)
        return R::failure(invalid(
            "movie mapping uses state " + std::to_string(state + 1U) +
                " but the active object has " + std::to_string(*available) + " state(s)",
            "map only states that exist, or load the trajectory first"));
  }
  MovieTimelineState timeline =
      movie_.value_or(MovieTimelineState{0U, 1U, 30.0, false, false, {}, {}});
  // Frames defined by `movie configure` without a map count as state 1... keep them unmapped by padding with 0.
  auto map = timeline.state_map;
  if (map.size() < timeline.frame_count) map.resize(timeline.frame_count, 0U);
  const std::size_t keep = start_frame.has_value() ? *start_frame - 1U : map.size();
  if (keep > kMaxMovieFrames)
    return R::failure(invalid("movie start frame exceeds the supported length"));
  map.resize(keep, 0U);
  if (map.size() + parsed.value().size() > kMaxMovieFrames)
    return R::failure(invalid("movie mapping exceeds 1000000 frames"));
  map.insert(map.end(), parsed.value().begin(), parsed.value().end());
  if (map.empty()) {
    movie_.reset();
    return R::success(MovieTimelineResult{MovieTimelineState{}, 0U});
  }
  timeline.frame_count = map.size();
  timeline.state_map = std::move(map);
  timeline.current_frame = std::min(timeline.current_frame, timeline.frame_count);
  for (auto it = timeline.keyframes.begin(); it != timeline.keyframes.end();)
    it = it->first > timeline.frame_count ? timeline.keyframes.erase(it) : std::next(it);
  movie_ = std::move(timeline);
  return R::success(MovieTimelineResult{*movie_, 0U});
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
  // The state map supplies the trajectory frame unless a keyframe names one explicitly.
  const bool explicit_trajectory = keyframe != candidate.movie_->keyframes.end() &&
                                   keyframe->second.trajectory_frame.has_value();
  const auto *mapped_object = candidate.active_object();
  if (!explicit_trajectory && frame <= candidate.movie_->state_map.size() &&
      mapped_object != nullptr && mapped_object->trajectory.has_value()) {
    const auto selected =
        candidate.set_trajectory_frame(candidate.movie_->state_map[frame - 1U]);
    if (!selected.has_value())
      return operation::Result<MovieTimelineResult>::failure(selected.error());
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
        object.trajectory.has_value(), object.fixed});
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
  if (duplicate_molecule || duplicate_volume || group_name_exists(name)) {
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
  for (auto &group : groups_)
    std::erase_if(group.members, [deleted_id](const GroupMember &member) {
      return member.group_name.empty() && member.object_id == deleted_id;
    });
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
  if (object->pbc.has_value())
    return operation::Result<TopologyMutationResult>::failure(invalid(
        "the object is shown with PBC processing",
        "run pbc mode --mode none before editing atoms or states"));
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

namespace {

bool glob_matches(std::string_view pattern, std::string_view text) {
  std::size_t p{}, t{};
  std::size_t star = std::string_view::npos, retry{};
  while (t < text.size()) {
    if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
      ++p;
      ++t;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      retry = t;
    } else if (star != std::string_view::npos) {
      p = star + 1U;
      t = ++retry;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') ++p;
  return p == pattern.size();
}

} // namespace

operation::Result<std::vector<CoveredObject>>
Workspace::covering_objects(std::string selection_expression) const {
  using R = operation::Result<std::vector<CoveredObject>>;
  const auto parsed = selection::Expression::parse(selection_expression);
  if (!parsed.has_value())
    return R::failure(parsed.error());
  std::vector<CoveredObject> covered;
  for (const auto &object : objects_) {
    const auto mask = selection_mask(object, selection_expression);
    if (!mask.has_value()) {
      // A named selection defined in another object only: not covered here.
      if (mask.error().code == operation::ErrorCode::not_found) continue;
      return R::failure(mask.error());
    }
    const auto count = static_cast<std::size_t>(
        std::count_if(mask.value().begin(), mask.value().end(),
                      [](const auto value) { return value != 0U; }));
    if (count != 0U)
      covered.push_back({object.id, object.system->name(), count});
  }
  return R::success(std::move(covered));
}

namespace {

// Applies a visibility change to several objects, restoring the previous
// state of the already changed ones if any step fails.
operation::Result<std::vector<WorkspaceObjectInfo>>
set_visibility_batch(Workspace &workspace, const std::vector<std::uint64_t> &ids,
                     bool visible) {
  using R = operation::Result<std::vector<WorkspaceObjectInfo>>;
  std::vector<std::pair<std::uint64_t, bool>> previous;
  for (const auto &info : workspace.list_objects())
    if (std::find(ids.begin(), ids.end(), info.id) != ids.end())
      previous.emplace_back(info.id, info.visible);
  std::vector<WorkspaceObjectInfo> changed;
  for (const auto id : ids) {
    const auto result = workspace.set_object_visibility(id, visible);
    if (!result.has_value()) {
      for (const auto &[restore_id, was_visible] : previous)
        static_cast<void>(workspace.set_object_visibility(restore_id, was_visible));
      return R::failure(result.error());
    }
    changed.push_back(result.value());
  }
  return R::success(std::move(changed));
}

} // namespace

operation::Result<std::vector<WorkspaceObjectInfo>>
Workspace::set_visibility_by_pattern(std::string_view pattern, bool visible) {
  using R = operation::Result<std::vector<WorkspaceObjectInfo>>;
  if (pattern.empty())
    return R::failure(invalid("object name pattern must not be empty"));
  std::vector<std::uint64_t> ids;
  const auto add_id = [&ids](std::uint64_t id) {
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
  };
  for (const auto &object : objects_)
    if (glob_matches(pattern, object.system->name())) add_id(object.id);
  // A group name selects every object below the group.
  for (const auto &group : groups_)
    if (glob_matches(pattern, group.name)) {
      std::vector<std::uint64_t> below;
      std::vector<std::string> ignored;
      collect_group_objects(group, below, ignored);
      for (const auto id : below) add_id(id);
    }
  if (ids.empty())
    return R::failure(operation::Error{
        operation::ErrorCode::not_found,
        "no object name matches '" + std::string{pattern} + "'",
        "use object list to see the current names"});
  return set_visibility_batch(*this, ids, visible);
}

operation::Result<std::vector<WorkspaceObjectInfo>>
Workspace::set_visibility_by_selection(std::string selection_expression,
                                       bool visible) {
  using R = operation::Result<std::vector<WorkspaceObjectInfo>>;
  const auto covered = covering_objects(std::move(selection_expression));
  if (!covered.has_value()) return R::failure(covered.error());
  if (covered.value().empty())
    return R::failure(operation::Error{
        operation::ErrorCode::not_found,
        "the selection does not cover any object", "check the selection", {}});
  std::vector<std::uint64_t> ids;
  for (const auto &item : covered.value()) ids.push_back(item.object_id);
  return set_visibility_batch(*this, ids, visible);
}

bool Workspace::group_name_exists(std::string_view name) const {
  return find_group(name) != nullptr;
}

const Workspace::GroupRecord *Workspace::find_group(std::string_view name) const {
  const auto found = std::find_if(groups_.begin(), groups_.end(),
                                  [&](const auto &group) { return group.name == name; });
  return found == groups_.end() ? nullptr : &*found;
}

const Workspace::GroupRecord *
Workspace::parent_group_of_object(std::uint64_t id) const {
  for (const auto &group : groups_)
    for (const auto &member : group.members)
      if (member.group_name.empty() && member.object_id == id) return &group;
  return nullptr;
}

const Workspace::GroupRecord *
Workspace::parent_group_of_group(std::string_view name) const {
  for (const auto &group : groups_)
    for (const auto &member : group.members)
      if (member.group_name == name) return &group;
  return nullptr;
}

void Workspace::collect_group_objects(const GroupRecord &group,
                                      std::vector<std::uint64_t> &objects,
                                      std::vector<std::string> &groups) const {
  for (const auto &member : group.members) {
    if (member.group_name.empty()) {
      objects.push_back(member.object_id);
    } else if (const auto *nested = find_group(member.group_name); nested != nullptr) {
      groups.push_back(nested->name);
      collect_group_objects(*nested, objects, groups);
    }
  }
}

std::string Workspace::member_name(const GroupMember &member) const {
  if (!member.group_name.empty()) return member.group_name;
  const auto found = std::ranges::find(objects_, member.object_id, &WorkspaceObject::id);
  return found == objects_.end() ? std::string{} : found->system->name();
}

GroupInfo Workspace::describe_group(const GroupRecord &group) const {
  GroupInfo info;
  info.name = group.name;
  info.open = group.open;
  for (const auto &member : group.members) {
    if (!member.group_name.empty()) {
      info.members.push_back(member.group_name);
    } else if (const auto found = std::ranges::find(objects_, member.object_id,
                                                    &WorkspaceObject::id);
               found != objects_.end()) {
      info.members.push_back(found->system->name());
    }
  }
  if (const auto *parent = parent_group_of_group(group.name); parent != nullptr)
    info.parent = parent->name;
  std::vector<std::uint64_t> objects;
  std::vector<std::string> nested;
  collect_group_objects(group, objects, nested);
  info.object_count = objects.size();
  for (const auto id : objects)
    for (const auto &object_info : list_objects())
      if (object_info.id == id && object_info.visible) info.enabled = true;
  return info;
}

std::vector<GroupInfo> Workspace::list_groups() const {
  std::vector<GroupInfo> result;
  result.reserve(groups_.size());
  for (const auto &group : groups_) result.push_back(describe_group(group));
  return result;
}

namespace {

bool valid_group_name(std::string_view name) {
  return !name.empty() &&
         !std::all_of(name.begin(), name.end(), [](unsigned char character) {
           return std::isspace(character) != 0;
         }) &&
         std::none_of(name.begin(), name.end(), [](unsigned char character) {
           return character < 0x20U || character == 0x7fU || character == ',' ||
                  character == '*' || character == '?';
         });
}

} // namespace

operation::Result<GroupChangeResult>
Workspace::group_add(std::string_view name, const std::vector<std::string> &members) {
  using R = operation::Result<GroupChangeResult>;
  if (!valid_group_name(name))
    return R::failure(invalid(
        "group name must contain visible characters and no commas or wildcards"));
  if (members.empty())
    return R::failure(invalid("group add needs at least one member",
                              "pass object or group names, for example --members \"a b*\""));
  const bool exists = group_name_exists(name);
  if (!exists) {
    const bool taken =
        std::any_of(objects_.begin(), objects_.end(),
                    [&](const auto &object) { return object.system->name() == name; }) ||
        std::any_of(volumes_.begin(), volumes_.end(),
                    [&](const auto &volume) { return volume.name == name; });
    if (taken)
      return R::failure(invalid("group name is already used by an object: " +
                                    std::string{name},
                                "choose a unique group name"));
  }
  // Resolve every pattern before changing anything.
  std::vector<GroupMember> resolved;
  std::vector<std::string> names;
  const auto add_unique = [&](GroupMember member, std::string display) {
    const auto duplicate = std::any_of(
        resolved.begin(), resolved.end(), [&](const GroupMember &existing) {
          return existing.object_id == member.object_id &&
                 existing.group_name == member.group_name;
        });
    if (!duplicate) {
      resolved.push_back(std::move(member));
      names.push_back(std::move(display));
    }
  };
  for (const auto &pattern : members) {
    bool matched = false;
    for (const auto &object : objects_)
      if (glob_matches(pattern, object.system->name())) {
        add_unique({object.id, {}}, object.system->name());
        matched = true;
      }
    for (const auto &group : groups_)
      if (group.name != name && glob_matches(pattern, group.name)) {
        add_unique({0U, group.name}, group.name);
        matched = true;
      }
    if (!matched)
      return R::failure(operation::Error{
          operation::ErrorCode::not_found,
          "no object or group matches '" + pattern + "'",
          "use object list and group list to see the current names", {}});
  }
  // A group cannot contain itself or one of its ancestors.
  for (const auto &member : resolved) {
    if (member.group_name.empty()) continue;
    const auto *nested = find_group(member.group_name);
    std::vector<std::uint64_t> ignored_objects;
    std::vector<std::string> below;
    collect_group_objects(*nested, ignored_objects, below);
    if (std::find(below.begin(), below.end(), std::string{name}) != below.end())
      return R::failure(invalid("group '" + member.group_name +
                                    "' already contains '" + std::string{name} + "'",
                                "a group cannot contain one of its ancestors"));
  }
  auto next = groups_;
  if (!exists) next.push_back({std::string{name}, {}, true});
  const auto target = std::find_if(next.begin(), next.end(),
                                   [&](const auto &group) { return group.name == name; });
  // An object or group belongs to one group: adding it moves it.
  for (const auto &member : resolved)
    for (auto &group : next)
      std::erase_if(group.members, [&](const GroupMember &existing) {
        return existing.object_id == member.object_id &&
               existing.group_name == member.group_name;
      });
  for (const auto &member : resolved) target->members.push_back(member);
  GroupChangeResult result;
  result.name = std::string{name};
  result.created = !exists;
  result.member_count = target->members.size();
  result.affected = std::move(names);
  groups_ = std::move(next);
  return R::success(std::move(result));
}

operation::Result<GroupChangeResult>
Workspace::group_remove(std::string_view name, const std::vector<std::string> &members) {
  using R = operation::Result<GroupChangeResult>;
  const auto *group = find_group(name);
  if (group == nullptr)
    return R::failure(operation::Error{operation::ErrorCode::not_found,
                                       "no group named '" + std::string{name} + "'",
                                       "use group list to see the current groups", {}});
  if (members.empty())
    return R::failure(invalid("group remove needs at least one member"));
  std::vector<GroupMember> removed;
  std::vector<std::string> names;
  for (const auto &pattern : members) {
    bool matched = false;
    for (const auto &member : group->members) {
      const auto display = member_name(member);
      if (!glob_matches(pattern, display)) continue;
      matched = true;
      const auto already = std::any_of(removed.begin(), removed.end(), [&](const auto &m) {
        return m.object_id == member.object_id && m.group_name == member.group_name;
      });
      if (!already) {
        removed.push_back(member);
        names.push_back(display);
      }
    }
    if (!matched)
      return R::failure(operation::Error{
          operation::ErrorCode::not_found,
          "'" + pattern + "' is not a member of group '" + std::string{name} + "'",
          "use group list to see its members", {}});
  }
  auto next = groups_;
  const auto target = std::find_if(next.begin(), next.end(),
                                   [&](const auto &g) { return g.name == name; });
  std::erase_if(target->members, [&](const GroupMember &existing) {
    return std::any_of(removed.begin(), removed.end(), [&](const auto &m) {
      return m.object_id == existing.object_id && m.group_name == existing.group_name;
    });
  });
  GroupChangeResult result;
  result.name = std::string{name};
  result.member_count = target->members.size();
  result.affected = std::move(names);
  groups_ = std::move(next);
  return R::success(std::move(result));
}

operation::Result<GroupChangeResult> Workspace::group_empty(std::string_view name) {
  using R = operation::Result<GroupChangeResult>;
  const auto *group = find_group(name);
  if (group == nullptr)
    return R::failure(operation::Error{operation::ErrorCode::not_found,
                                       "no group named '" + std::string{name} + "'",
                                       "use group list to see the current groups", {}});
  GroupChangeResult result;
  result.name = std::string{name};
  result.affected = describe_group(*group).members;
  const auto target = std::find_if(groups_.begin(), groups_.end(),
                                   [&](const auto &g) { return g.name == name; });
  target->members.clear();
  return R::success(std::move(result));
}

operation::Result<GroupChangeResult>
Workspace::group_ungroup(const std::vector<std::string> &members) {
  using R = operation::Result<GroupChangeResult>;
  if (members.empty())
    return R::failure(invalid("ungroup needs at least one member"));
  GroupChangeResult result;
  auto next = groups_;
  for (const auto &pattern : members) {
    bool matched = false;
    for (auto &group : next) {
      std::vector<GroupMember> kept;
      for (const auto &member : group.members) {
        const auto display = member_name(member);
        if (glob_matches(pattern, display)) {
          matched = true;
          result.affected.push_back(display);
        } else {
          kept.push_back(member);
        }
      }
      group.members = std::move(kept);
    }
    if (!matched)
      return R::failure(operation::Error{
          operation::ErrorCode::not_found,
          "'" + pattern + "' is not in any group",
          "use group list to see the current members", {}});
  }
  groups_ = std::move(next);
  result.member_count = result.affected.size();
  return R::success(std::move(result));
}

operation::Result<GroupChangeResult> Workspace::group_excise(std::string_view name) {
  using R = operation::Result<GroupChangeResult>;
  if (find_group(name) == nullptr)
    return R::failure(operation::Error{operation::ErrorCode::not_found,
                                       "no group named '" + std::string{name} + "'",
                                       "use group list to see the current groups", {}});
  auto next = groups_;
  const auto target = std::find_if(next.begin(), next.end(),
                                   [&](const auto &g) { return g.name == name; });
  const auto members = target->members;
  GroupChangeResult result;
  result.name = std::string{name};
  result.deleted = true;
  result.affected = describe_group(*find_group(name)).members;
  // The members take the group's place in its parent, if it has one.
  for (auto &group : next) {
    const auto position = std::find_if(group.members.begin(), group.members.end(),
                                       [&](const GroupMember &m) { return m.group_name == name; });
    if (position == group.members.end()) continue;
    const auto offset = position - group.members.begin();
    group.members.erase(position);
    group.members.insert(group.members.begin() + offset, members.begin(), members.end());
    break;
  }
  std::erase_if(next, [&](const auto &g) { return g.name == name; });
  result.member_count = members.size();
  groups_ = std::move(next);
  return R::success(std::move(result));
}

operation::Result<GroupChangeResult> Workspace::group_purge(std::string_view name) {
  using R = operation::Result<GroupChangeResult>;
  const auto *group = find_group(name);
  if (group == nullptr)
    return R::failure(operation::Error{operation::ErrorCode::not_found,
                                       "no group named '" + std::string{name} + "'",
                                       "use group list to see the current groups", {}});
  std::vector<std::uint64_t> object_ids;
  std::vector<std::string> nested;
  collect_group_objects(*group, object_ids, nested);
  GroupChangeResult result;
  result.name = std::string{name};
  for (const auto id : object_ids)
    if (const auto found = std::ranges::find(objects_, id, &WorkspaceObject::id);
        found != objects_.end())
      result.affected.push_back(found->system->name());
  for (const auto id : object_ids) {
    const auto deleted = delete_object(std::to_string(id));
    if (!deleted.has_value()) return R::failure(deleted.error());
  }
  // delete_object already removed the objects from their groups; drop the
  // nested groups and leave the purged group empty.
  std::erase_if(groups_, [&](const auto &g) {
    return std::find(nested.begin(), nested.end(), g.name) != nested.end();
  });
  const auto target = std::find_if(groups_.begin(), groups_.end(),
                                   [&](const auto &g) { return g.name == name; });
  target->members.clear();
  result.member_count = 0U;
  return R::success(std::move(result));
}

operation::Result<GroupInfo>
Workspace::group_set_expansion(std::string_view name, GroupExpansion expansion) {
  using R = operation::Result<GroupInfo>;
  const auto target = std::find_if(groups_.begin(), groups_.end(),
                                   [&](const auto &g) { return g.name == name; });
  if (target == groups_.end())
    return R::failure(operation::Error{operation::ErrorCode::not_found,
                                       "no group named '" + std::string{name} + "'",
                                       "use group list to see the current groups", {}});
  target->open = expansion == GroupExpansion::open    ? true
                 : expansion == GroupExpansion::close ? false
                                                        : !target->open;
  return R::success(describe_group(*target));
}

operation::Result<EntityTypeResult>
Workspace::entity_type(std::string_view name) const {
  using R = operation::Result<EntityTypeResult>;
  for (const auto &object : objects_)
    if (object.system->name() == name)
      return R::success({std::string{name}, "object:molecule", object.id});
  for (const auto &volume : volumes_)
    if (volume.name == name)
      return R::success({std::string{name}, "object:volume", volume.id});
  if (group_name_exists(name))
    return R::success({std::string{name}, "object:group", 0U});
  if (const auto *active = active_object(); active != nullptr)
    for (const auto &item : active->selections.list())
      if (item.name == name)
        return R::success({std::string{name}, "selection", active->id});
  return R::failure(operation::Error{
      operation::ErrorCode::not_found,
      "no object or selection is named '" + std::string{name} + "'",
      "use object list to see the current names"});
}

operation::Result<AtomListing>
Workspace::list_atoms(std::string selection_expression,
                      const std::vector<std::string> &properties,
                      std::size_t limit) const {
  using R = operation::Result<AtomListing>;
  const auto *object = active_object();
  if (object == nullptr) return R::failure(missing_active());
  if (limit == 0U || limit > 1000000U)
    return R::failure(invalid("atom listing limit must be between 1 and 1000000"));
  if (properties.empty())
    return R::failure(invalid("atom listing needs at least one property"));
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value()) return R::failure(mask.error());
  const auto frame_index = object->trajectory.has_value()
                               ? object->trajectory->timeline.snapshot().frame
                               : 0U;
  const auto frame = object->system->coordinates()->read_frame(frame_index);
  if (!frame.has_value()) return R::failure(frame.error());
  const selection::EvaluationContext context{
      frame.value().get(), frame_index, object->system->coordinates().get(),
      object->system->name()};
  const auto &topology = *object->system->topology();

  // Numeric properties reuse the selection grammar (b, q, pc, fc, x, y, z,
  // mass, radius, atomicnumber, numbonds) so there is exactly one definition.
  static const std::map<std::string, std::string, std::less<>> numeric_sources{
      {"b", "b"}, {"q", "q"}, {"partial_charge", "partial_charge"},
      {"formal_charge", "formal_charge"}, {"x", "x"}, {"y", "y"}, {"z", "z"},
      {"mass", "mass"}, {"radius", "radius"}, {"atomic_number", "atomicnumber"},
      {"bond_count", "numbonds"}};
  static const std::set<std::string, std::less<>> text_properties{
      "index", "id", "source_id", "name", "element", "resn", "resi", "chain",
      "segment", "altloc"};
  AtomListing listing;
  listing.columns = properties;
  std::vector<std::vector<std::optional<double>>> numeric(properties.size());
  for (std::size_t column = 0; column < properties.size(); ++column) {
    const auto &property = properties[column];
    if (const auto found = numeric_sources.find(property);
        found != numeric_sources.end()) {
      const auto expression = selection::parse_numeric_expression(found->second);
      if (!expression.has_value()) return R::failure(expression.error());
      auto values = selection::evaluate_numeric(*expression.value(), topology, context);
      if (!values.has_value()) {
        // A missing optional property is reported per atom as null, other
        // failures are real errors.
        if (property == "mass" || property == "partial_charge" || property == "b" ||
            property == "q")
          numeric[column].assign(topology.atom_count(), std::nullopt);
        else
          return R::failure(values.error());
      } else {
        numeric[column] = std::move(values.value());
      }
    } else if (!text_properties.contains(property)) {
      return R::failure(invalid("unknown atom property '" + property + "'",
                                "choose from index id source_id name element resn "
                                "resi chain segment altloc b q partial_charge "
                                "formal_charge x y z mass radius atomic_number bond_count"));
    }
  }
  for (std::size_t atom = 0; atom < mask.value().size(); ++atom) {
    if (mask.value()[atom] == 0U) continue;
    ++listing.selected_atom_count;
    if (listing.rows.size() >= limit) {
      listing.truncated = true;
      continue;
    }
    const auto &record = topology.atoms()[atom];
    const auto &residue = topology.residues()[record.residue.value];
    std::vector<command::Value> row;
    row.reserve(properties.size());
    for (std::size_t column = 0; column < properties.size(); ++column) {
      const auto &property = properties[column];
      if (!numeric[column].empty()) {
        const auto &value = numeric[column][atom];
        row.emplace_back(value.has_value() ? command::Value{*value} : command::Value{nullptr});
      } else if (property == "index") {
        row.emplace_back(static_cast<std::uint64_t>(atom + 1U));
      } else if (property == "id") {
        const auto id = topology.atom_id(model::AtomIndex{atom});
        row.emplace_back(id.has_value() ? command::Value{id->value} : command::Value{nullptr});
      } else if (property == "source_id") {
        row.emplace_back(record.source_serial.has_value()
                             ? command::Value{static_cast<std::int64_t>(*record.source_serial)}
                             : command::Value{nullptr});
      } else if (property == "name") {
        row.emplace_back(record.name);
      } else if (property == "element") {
        row.emplace_back(selection::atom_element_symbol(record.atomic_number));
      } else if (property == "resn") {
        row.emplace_back(residue.name);
      } else if (property == "resi") {
        row.emplace_back(std::to_string(residue.sequence_number) + residue.insertion_code);
      } else if (property == "chain") {
        row.emplace_back(residue.chain_id);
      } else if (property == "segment") {
        row.emplace_back(residue.segment_id);
      } else {
        row.emplace_back(record.alternate_location);
      }
    }
    listing.rows.push_back(std::move(row));
  }
  return R::success(std::move(listing));
}

operation::Result<AtomListing>
Workspace::list_bonds(std::string selection_expression, std::size_t limit) const {
  using R = operation::Result<AtomListing>;
  const auto *object = active_object();
  if (object == nullptr) return R::failure(missing_active());
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value()) return R::failure(mask.error());
  const auto &topology = *object->system->topology();
  const auto order_name = [](model::BondOrder order) -> std::string {
    switch (order) {
      case model::BondOrder::single: return "single";
      case model::BondOrder::double_bond: return "double";
      case model::BondOrder::triple: return "triple";
      case model::BondOrder::aromatic: return "aromatic";
      case model::BondOrder::amide: return "amide";
      case model::BondOrder::zero: return "zero";
      case model::BondOrder::query: return "query";
      case model::BondOrder::unknown: break;
    }
    return "unknown";
  };
  AtomListing listing;
  listing.columns = {"index1", "index2", "id1", "id2", "name1", "name2", "order", "bond_id"};
  for (const auto selected : mask.value()) listing.selected_atom_count += selected != 0U ? 1U : 0U;
  std::size_t bond_index = 0U;
  for (const auto &bond : topology.bonds()) {
    const auto current_bond_index = bond_index++;
    if (mask.value()[bond.first.value] == 0U && mask.value()[bond.second.value] == 0U) continue;
    if (listing.rows.size() >= limit) {
      listing.truncated = true;
      break;
    }
    const auto first_id = topology.atom_id(bond.first);
    const auto second_id = topology.atom_id(bond.second);
    listing.rows.push_back({static_cast<std::uint64_t>(bond.first.value + 1U),
                            static_cast<std::uint64_t>(bond.second.value + 1U),
                            first_id.has_value() ? first_id->value : std::uint64_t{0},
                            second_id.has_value() ? second_id->value : std::uint64_t{0},
                            topology.atoms()[bond.first.value].name,
                            topology.atoms()[bond.second.value].name, order_name(bond.order),
                            [&] {
                              const auto id = topology.bond_id(current_bond_index);
                              return id.has_value() ? id->value : std::uint64_t{0};
                            }()});
  }
  return R::success(std::move(listing));
}

operation::Result<std::vector<std::string>>
Workspace::list_chains(std::string selection_expression) const {
  using R = operation::Result<std::vector<std::string>>;
  const auto *object = active_object();
  if (object == nullptr) return R::failure(missing_active());
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value()) return R::failure(mask.error());
  const auto &topology = *object->system->topology();
  std::vector<std::string> chains;
  for (std::size_t atom = 0; atom < mask.value().size(); ++atom) {
    if (mask.value()[atom] == 0U) continue;
    const auto &chain = topology.residues()[topology.atoms()[atom].residue.value].chain_id;
    if (std::find(chains.begin(), chains.end(), chain) == chains.end())
      chains.push_back(chain);
  }
  return R::success(std::move(chains));
}

operation::Result<ChargeSum>
Workspace::sum_charge(std::string selection_expression, std::string_view kind) const {
  using R = operation::Result<ChargeSum>;
  const auto *object = active_object();
  if (object == nullptr) return R::failure(missing_active());
  if (kind != "formal" && kind != "partial")
    return R::failure(invalid("charge kind must be formal or partial"));
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value()) return R::failure(mask.error());
  const auto &topology = *object->system->topology();
  ChargeSum result;
  result.kind = std::string{kind};
  if (kind == "formal") {
    result.source = "topology formal charge";
    for (std::size_t atom = 0; atom < mask.value().size(); ++atom) {
      if (mask.value()[atom] == 0U) continue;
      result.sum += topology.atoms()[atom].formal_charge;
      ++result.atom_count;
    }
    return R::success(std::move(result));
  }
  const auto *property = topology.properties().find("partial_charge");
  if (property == nullptr)
    return R::failure(operation::Error{
        operation::ErrorCode::not_found,
        "the active object has no partial_charge property",
        "load a PQR, MOL2 or topology file that carries partial charges", {}});
  const auto expression = selection::parse_numeric_expression("partial_charge");
  if (!expression.has_value()) return R::failure(expression.error());
  const auto frame = active_frame(*object);
  if (!frame.has_value()) return R::failure(frame.error());
  const selection::EvaluationContext context{
      frame.value().get(),
      object->trajectory.has_value() ? object->trajectory->timeline.snapshot().frame : 0U,
      object->system->coordinates().get(), object->system->name()};
  const auto values = selection::evaluate_numeric(*expression.value(), topology, context);
  if (!values.has_value()) return R::failure(values.error());
  result.source = "topology partial_charge property";
  for (std::size_t atom = 0; atom < mask.value().size(); ++atom) {
    if (mask.value()[atom] == 0U) continue;
    if (!values.value()[atom].has_value())
      return R::failure(invalid("an atom of the selection has no partial charge",
                                "select only atoms that carry a partial charge"));
    result.sum += *values.value()[atom];
    ++result.atom_count;
  }
  return R::success(std::move(result));
}

operation::Result<SelectedAtomData>
Workspace::selected_atom_data(std::string selection_expression, bool need_partial_charges, bool need_masses) const {
  using R = operation::Result<SelectedAtomData>;
  const auto *object = active_object();
  if (object == nullptr) return R::failure(missing_active());
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value()) return R::failure(mask.error());
  const auto frame = active_frame(*object);
  if (!frame.has_value()) return R::failure(frame.error());
  const auto &topology = *object->system->topology();
  SelectedAtomData data;
  data.object_id = object->id;
  data.object_name = object->system->name();
  data.frame_index = object->trajectory.has_value() ? object->trajectory->timeline.snapshot().frame : 0U;
  const double scale = frame.value()->metadata().coordinate_unit == operation::LengthUnit::nanometer ? 10.0 : 1.0;
  data.unit_cell = frame.value()->metadata().unit_cell;
  if (data.unit_cell.has_value()) {
    for (auto *vector : {&data.unit_cell->a, &data.unit_cell->b, &data.unit_cell->c}) {
      vector->x *= scale;
      vector->y *= scale;
      vector->z *= scale;
    }
  }
  std::vector<double> all_charges;
  if (need_partial_charges) {
    if (topology.properties().find("partial_charge") == nullptr)
      return R::failure(operation::Error{operation::ErrorCode::not_found, "the active object has no partial_charge property",
                                         "load a PQR, MOL2, MMOD, MAE or topology file that carries partial charges", {}});
    const auto expression = selection::parse_numeric_expression("partial_charge");
    if (!expression.has_value()) return R::failure(expression.error());
    const selection::EvaluationContext context{frame.value().get(), data.frame_index, object->system->coordinates().get(),
                                               object->system->name()};
    const auto values = selection::evaluate_numeric(*expression.value(), topology, context);
    if (!values.has_value()) return R::failure(values.error());
    for (const auto &value : values.value()) all_charges.push_back(value.value_or(std::nan("")));
  }
  analysis::MassValues masses;
  if (need_masses) {
    auto resolved = analysis::masses_from_property(topology);
    if (!resolved.has_value() && resolved.error().code == operation::ErrorCode::not_found)
      resolved = analysis::estimated_element_masses(topology);
    if (!resolved.has_value()) return R::failure(resolved.error());
    masses = std::move(resolved.value());
    data.masses_estimated = masses.estimated;
  }
  for (std::size_t atom = 0; atom < mask.value().size(); ++atom) {
    if (mask.value()[atom] == 0U) continue;
    if (!frame.value()->atom_present(atom))
      return R::failure(invalid("atom " + std::to_string(atom + 1U) + " is absent in the current frame",
                                "select only atoms that are present"));
    const auto position = std::visit(
        [atom](const auto &values) {
          return model::Vec3d{static_cast<double>(values[atom].x), static_cast<double>(values[atom].y),
                              static_cast<double>(values[atom].z)};
        },
        frame.value()->positions().values());
    data.indices.push_back(atom);
    data.positions.push_back({position.x * scale, position.y * scale, position.z * scale});
    if (need_partial_charges) {
      if (std::isnan(all_charges[atom]))
        return R::failure(invalid("atom " + std::to_string(atom + 1U) + " has no partial charge",
                                  "select only atoms that carry a partial charge"));
      data.partial_charges.push_back(all_charges[atom]);
    }
    if (need_masses) data.masses.push_back(masses.values[atom]);
  }
  if (data.indices.empty()) return R::failure(invalid("the selection matches no atoms"));
  return R::success(std::move(data));
}

operation::Result<MassSum>
Workspace::sum_mass(std::string selection_expression) const {
  using R = operation::Result<MassSum>;
  const auto *object = active_object();
  if (object == nullptr) return R::failure(missing_active());
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value()) return R::failure(mask.error());
  auto masses = analysis::masses_from_property(*object->system->topology());
  if (!masses.has_value() && masses.error().code == operation::ErrorCode::not_found)
    masses = analysis::estimated_element_masses(*object->system->topology());
  if (!masses.has_value()) return R::failure(masses.error());
  MassSum result;
  result.unit = masses.value().unit.value_or("unspecified");
  result.source = masses.value().source;
  result.estimated = masses.value().estimated;
  for (std::size_t atom = 0; atom < mask.value().size(); ++atom) {
    if (mask.value()[atom] == 0U) continue;
    result.mass += masses.value().values[atom];
    ++result.atom_count;
  }
  if (result.atom_count == 0U)
    return R::failure(invalid("selection matches no atoms", "check the selection expression"));
  return R::success(std::move(result));
}

operation::Result<std::vector<analysis::BackboneAngles>>
Workspace::backbone_angles(std::string selection_expression) const {
  using R = operation::Result<std::vector<analysis::BackboneAngles>>;
  const auto *object = active_object();
  if (object == nullptr) return R::failure(missing_active());
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value()) return R::failure(mask.error());
  const auto frame = active_frame(*object);
  if (!frame.has_value()) return R::failure(frame.error());
  analysis::BackboneRequest request;
  request.topology = object->system->topology().get();
  request.frame = frame.value().get();
  request.selected = mask.value();
  return analysis::calculate_backbone_angles(request);
}

operation::Result<AlignmentResult>
Workspace::align_objects(const AlignmentRequest &request) {
  using R = operation::Result<AlignmentResult>;
  if (request.match != "order" && request.match != "identifier" && request.match != "sequence")
    return R::failure(invalid("alignment match must be order, identifier or sequence"));
  if (!(request.cutoff > 0.0) || !std::isfinite(request.cutoff))
    return R::failure(invalid("alignment cutoff must be positive"));
  if (request.cycles > 100U) return R::failure(invalid("alignment cycles must not exceed 100"));
  const auto mobile_index = object_index_by_reference(request.mobile_object);
  if (!mobile_index.has_value()) return R::failure(mobile_index.error());
  const auto target_index = object_index_by_reference(request.target_object);
  if (!target_index.has_value()) return R::failure(target_index.error());
  if (mobile_index.value() == target_index.value())
    return R::failure(invalid("the mobile and target objects must differ",
                              "to align states of one object use analyze trajectory rmsd"));
  const auto &mobile = objects_[mobile_index.value()];
  const auto &target = objects_[target_index.value()];
  const auto mobile_frame = active_frame(mobile);
  if (!mobile_frame.has_value()) return R::failure(mobile_frame.error());
  const auto target_frame = active_frame(target);
  if (!target_frame.has_value()) return R::failure(target_frame.error());
  if (mobile_frame.value()->metadata().coordinate_unit !=
      target_frame.value()->metadata().coordinate_unit)
    return R::failure(invalid("the objects use different coordinate units"));
  if (request.apply && mobile.fixed)
    return R::failure(invalid("the mobile object is fixed: " + mobile.system->name(),
                              "run object free first"));

  std::vector<std::size_t> mobile_atoms, target_atoms;
  double sequence_score = 0.0;
  std::size_t aligned_residues = 0, identical_residues = 0, initial_pair_count = 0;
  if (!request.pairs.empty()) {
    for (const auto &[from, to] : request.pairs) {
      if (from == 0U || to == 0U ||
          from > mobile.system->topology()->atom_count() ||
          to > target.system->topology()->atom_count())
        return R::failure(invalid("an explicit atom pair is out of range",
                                  "atom indices are 1-based and must exist in their object"));
      mobile_atoms.push_back(from - 1U);
      target_atoms.push_back(to - 1U);
    }
  } else {
    const auto mobile_mask = selection_mask(mobile, request.mobile_selection);
    if (!mobile_mask.has_value()) return R::failure(mobile_mask.error());
    const auto target_mask = selection_mask(target, request.target_selection);
    if (!target_mask.has_value()) return R::failure(target_mask.error());
    std::vector<std::size_t> from, to;
    for (std::size_t atom = 0; atom < mobile_mask.value().size(); ++atom)
      if (mobile_mask.value()[atom] != 0U) from.push_back(atom);
    for (std::size_t atom = 0; atom < target_mask.value().size(); ++atom)
      if (target_mask.value()[atom] != 0U) to.push_back(atom);
    if (request.match == "sequence") {
      // Residues of each selection in atom order, one-letter sequence, BLOSUM62 alignment, then atoms by name.
      const auto residues_of = [](const WorkspaceObject &object, const std::vector<std::size_t> &atoms) {
        std::vector<std::size_t> residues;
        std::string letters;
        const auto &topology = *object.system->topology();
        for (const auto atom : atoms) {
          const auto residue = topology.atoms()[atom].residue.value;
          if (residues.empty() || residues.back() != residue) {
            residues.push_back(residue);
            letters.push_back(analysis::residue_letter(topology.residues()[residue].name));
          }
        }
        return std::pair{std::move(residues), std::move(letters)};
      };
      const auto [mobile_residues, mobile_letters] = residues_of(mobile, from);
      const auto [target_residues, target_letters] = residues_of(target, to);
      if (mobile_residues.empty() || target_residues.empty())
        return R::failure(invalid("sequence alignment needs residues in both selections"));
      const auto sequence = analysis::align_sequences({mobile_letters, target_letters, request.gap_open, request.gap_extend, true});
      if (!sequence.has_value()) return R::failure(sequence.error());
      sequence_score = sequence.value().score;
      aligned_residues = sequence.value().pairs.size();
      identical_residues = sequence.value().identical;
      const auto &mobile_topology = *mobile.system->topology();
      const auto &target_topology = *target.system->topology();
      std::map<std::size_t, std::vector<std::size_t>> mobile_atoms_of, target_atoms_of;
      for (const auto atom : from) mobile_atoms_of[mobile_topology.atoms()[atom].residue.value].push_back(atom);
      for (const auto atom : to) target_atoms_of[target_topology.atoms()[atom].residue.value].push_back(atom);
      for (const auto &[left, right] : sequence.value().pairs) {
        const auto &left_atoms = mobile_atoms_of[mobile_residues[left]];
        std::map<std::string, std::size_t> right_by_name;
        for (const auto atom : target_atoms_of[target_residues[right]]) right_by_name.emplace(target_topology.atoms()[atom].name, atom);
        for (const auto atom : left_atoms) {
          const auto found = right_by_name.find(mobile_topology.atoms()[atom].name);
          if (found == right_by_name.end()) continue;
          mobile_atoms.push_back(atom);
          target_atoms.push_back(found->second);
        }
      }
    } else if (request.match == "order") {
      if (from.size() != to.size())
        return R::failure(invalid(
            "the selections have " + std::to_string(from.size()) + " and " +
                std::to_string(to.size()) + " atoms",
            "select the same number of atoms or use --match identifier"));
      mobile_atoms = std::move(from);
      target_atoms = std::move(to);
    } else {
      const auto key = [](const WorkspaceObject &object, std::size_t atom) {
        const auto &topology = *object.system->topology();
        const auto &record = topology.atoms()[atom];
        const auto &residue = topology.residues()[record.residue.value];
        return residue.chain_id + "|" + std::to_string(residue.sequence_number) + "|" +
               residue.insertion_code + "|" + record.name + "|" + record.alternate_location;
      };
      std::map<std::string, std::size_t> target_by_key;
      for (const auto atom : to) target_by_key.emplace(key(target, atom), atom);
      for (const auto atom : from) {
        const auto found = target_by_key.find(key(mobile, atom));
        if (found == target_by_key.end()) continue;
        mobile_atoms.push_back(atom);
        target_atoms.push_back(found->second);
      }
    }
  }
  if (mobile_atoms.size() < 3U)
    return R::failure(invalid("alignment needs at least three paired atoms",
                              "check the selections and the matching mode"));

  const auto world = [&](const WorkspaceObject &object) {
    return scene_->world_transform(object.scene_node);
  };
  const auto mobile_world = world(mobile);
  if (!mobile_world.has_value()) return R::failure(mobile_world.error());
  const auto target_world = world(target);
  if (!target_world.has_value()) return R::failure(target_world.error());
  const auto positions = [&](const WorkspaceObject &object,
                             const model::CoordinateFrame &frame,
                             const std::vector<std::size_t> &atoms,
                             const scene::Matrix4d &matrix_world)
      -> operation::Result<std::vector<model::Vec3d>> {
    std::vector<model::Vec3d> points;
    points.reserve(atoms.size());
    for (const auto atom : atoms) {
      if (!frame.atom_present(atom))
        return operation::Result<std::vector<model::Vec3d>>::failure(
            invalid("atom " + std::to_string(atom + 1U) + " of " +
                    object.system->name() + " has no position in the current frame"));
      const auto local = std::visit(
          [atom](const auto &values) {
            return model::Vec3d{static_cast<double>(values[atom].x),
                                static_cast<double>(values[atom].y),
                                static_cast<double>(values[atom].z)};
          },
          frame.positions().values());
      points.push_back(scene::transform_point(matrix_world, local));
    }
    return operation::Result<std::vector<model::Vec3d>>::success(std::move(points));
  };
  const auto mobile_points =
      positions(mobile, *mobile_frame.value(), mobile_atoms, mobile_world.value());
  if (!mobile_points.has_value()) return R::failure(mobile_points.error());
  const auto target_points =
      positions(target, *target_frame.value(), target_atoms, target_world.value());
  if (!target_points.has_value()) return R::failure(target_points.error());

  // Paired world-space frames: atom i of both frames is one fitted pair.
  model::FrameMetadata metadata;
  metadata.coordinate_unit = mobile_frame.value()->metadata().coordinate_unit;
  std::vector<double> all_weights;
  if (!request.uniform_weights) {
    // Mass weights per pair: the mobile atom's mass.
    auto masses = analysis::masses_from_property(*mobile.system->topology());
    if (!masses.has_value() && masses.error().code == operation::ErrorCode::not_found)
      masses = analysis::estimated_element_masses(*mobile.system->topology());
    if (!masses.has_value()) return R::failure(masses.error());
    for (const auto atom : mobile_atoms) all_weights.push_back(masses.value().values[atom]);
  }
  const auto fit_active = [&](const std::vector<std::size_t> &active) -> operation::Result<analysis::FitResult> {
    std::vector<model::Vec3d> reference_points, moving_points;
    std::vector<double> weights;
    for (const auto pair : active) {
      reference_points.push_back(target_points.value()[pair]);
      moving_points.push_back(mobile_points.value()[pair]);
      if (!request.uniform_weights) weights.push_back(all_weights[pair]);
    }
    const std::vector<std::uint8_t> present(active.size(), 1U);
    const auto reference_frame = model::CoordinateFrame::create(
        model::CoordinateBuffer{std::move(reference_points)}, std::nullopt, present, metadata);
    if (!reference_frame.has_value())
      return operation::Result<analysis::FitResult>::failure(reference_frame.error());
    const auto moving_frame = model::CoordinateFrame::create(
        model::CoordinateBuffer{std::move(moving_points)}, std::nullopt, present, metadata);
    if (!moving_frame.has_value())
      return operation::Result<analysis::FitResult>::failure(moving_frame.error());
    analysis::FitRequest fit_request;
    fit_request.reference = reference_frame.value().get();
    fit_request.mobile = moving_frame.value().get();
    fit_request.weights = weights;
    return analysis::fit_rigid(fit_request);
  };
  std::vector<std::size_t> active(mobile_atoms.size());
  std::iota(active.begin(), active.end(), std::size_t{0});
  auto fit = fit_active(active);
  if (!fit.has_value()) return R::failure(fit.error());
  const double rmsd_before = fit.value().before.rmsd;
  std::size_t cycles_run = 0;
  for (std::size_t cycle = 1; cycle <= request.cycles; ++cycle) {
    const double rms = fit.value().after.rmsd;
    if (!(rms > 1.0e-4)) break;
    std::vector<std::size_t> kept;
    for (const auto pair : active) {
      const auto moved = fit.value().transform.apply(mobile_points.value()[pair]);
      const auto& reference = target_points.value()[pair];
      const double distance = std::sqrt((moved.x - reference.x) * (moved.x - reference.x) +
                                        (moved.y - reference.y) * (moved.y - reference.y) +
                                        (moved.z - reference.z) * (moved.z - reference.z));
      if (distance / rms <= request.cutoff) kept.push_back(pair);
    }
    cycles_run = cycle;  // like PyMOL, the final pass that rejects nothing still counts
    if (kept.size() == active.size()) break;
    if (kept.size() < 3U)
      return R::failure(invalid("outlier rejection would leave fewer than three pairs",
                                "raise --cutoff or lower --cycles"));
    active = std::move(kept);
    fit = fit_active(active);
    if (!fit.has_value()) return R::failure(fit.error());
  }
  // Report the pairs that were actually fitted.
  {
    std::vector<std::size_t> final_mobile, final_target;
    for (const auto pair : active) {
      final_mobile.push_back(mobile_atoms[pair]);
      final_target.push_back(target_atoms[pair]);
    }
    const auto initial_pairs = mobile_atoms.size();
    mobile_atoms = std::move(final_mobile);
    target_atoms = std::move(final_target);
    initial_pair_count = initial_pairs;
  }

  const auto &rotation = fit.value().transform.rotation;  // row-major 3x3
  const auto &translation = fit.value().transform.translation;
  AlignmentResult result;
  result.mobile_object_id = mobile.id;
  result.target_object_id = target.id;
  result.paired_atom_count = mobile_atoms.size();
  result.initial_pair_count = initial_pair_count;
  result.cycles_run = cycles_run;
  result.aligned_residue_count = aligned_residues;
  result.identical_residue_count = identical_residues;
  result.alignment_score = sequence_score;
  result.coordinate_unit = metadata.coordinate_unit;
  result.rmsd_before = rmsd_before;
  result.rmsd_after = fit.value().after.rmsd;
  result.matrix = {rotation[0], rotation[1], rotation[2], translation.x,
                   rotation[3], rotation[4], rotation[5], translation.y,
                   rotation[6], rotation[7], rotation[8], translation.z,
                   0.0,         0.0,         0.0,         1.0};
  if (!request.apply) return R::success(std::move(result));

  // New world matrix F * M; the object's node keeps pivot 0 and a decomposed
  // translation / rotation / scale so that `view reset --object` restores it.
  scene::Matrix4d fit_matrix;  // column-major
  for (std::size_t row = 0; row < 3U; ++row)
    for (std::size_t column = 0; column < 3U; ++column)
      fit_matrix.values[column * 4U + row] = rotation[row * 3U + column];
  fit_matrix.values[12] = translation.x;
  fit_matrix.values[13] = translation.y;
  fit_matrix.values[14] = translation.z;
  const auto *node = scene_->find(mobile.scene_node);
  if (node == nullptr || !node->parent().has_value() || *node->parent() != scene_->root())
    return R::failure(operation::Error{operation::ErrorCode::internal,
                                       "the mobile object is not a root scene child", {}});
  const auto combined = fit_matrix * scene::matrix(node->local_transform());
  const auto column = [&](std::size_t index) {
    return model::Vec3d{combined(0U, index), combined(1U, index), combined(2U, index)};
  };
  const auto length = [](model::Vec3d v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
  const auto scale_x = length(column(0)), scale_y = length(column(1)), scale_z = length(column(2));
  if (scale_x <= 0.0 || scale_y <= 0.0 || scale_z <= 0.0)
    return R::failure(operation::Error{operation::ErrorCode::internal,
                                       "the combined transform is degenerate", {}});
  scene::Transform next;
  next.translation = {combined(0U, 3U), combined(1U, 3U), combined(2U, 3U)};
  next.scale = {scale_x, scale_y, scale_z};
  const auto c0 = column(0), c1 = column(1), c2 = column(2);
  next.rotation = scene::quaternion_from_basis(
      {c0.x / scale_x, c0.y / scale_x, c0.z / scale_x},
      {c1.x / scale_y, c1.y / scale_y, c1.z / scale_y},
      {c2.x / scale_z, c2.y / scale_z, c2.z / scale_z});
  auto builder = scene::SceneBuilder::from(*scene_);
  if (const auto error = builder.set_transform(mobile.scene_node, next); error.has_value())
    return R::failure(*error);
  const auto next_scene = builder.build();
  if (!next_scene.has_value()) return R::failure(next_scene.error());
  scene_ = next_scene.value();
  result.applied = true;
  result.transform = next;
  result.scene_version = scene_->version();
  return R::success(std::move(result));
}

operation::Result<StateAppendResult>
Workspace::append_states(std::string_view object_reference,
                         const io::StructureDocument &document) {
  using R = operation::Result<StateAppendResult>;
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value()) return R::failure(index.error());
  const auto &object = objects_[index.value()];
  if (object.pbc.has_value())
    return R::failure(invalid("the object is shown with PBC processing",
                              "run pbc mode --mode none before appending states"));
  // Multi-model files load as an "embedded" trajectory over their own states;
  // those and single-state objects accept more states, attached trajectories
  // (`traj load`) do not.
  const bool embedded = object.trajectory.has_value() &&
                        object.trajectory->mapping.identity_strength ==
                            "embedded-structure-order";
  if (object.trajectory.has_value() && !embedded)
    return R::failure(operation::Error{
        operation::ErrorCode::unsupported,
        "states cannot be appended to an object with an attached trajectory",
        "use traj load for trajectories, or append before attaching", {}});
  const auto &topology = *object.system->topology();
  const auto previous_count = object.system->coordinates()->frame_count();
  constexpr std::size_t kMaximumStates = 100000U;
  if (!previous_count.has_value() || *previous_count == 0U)
    return R::failure(invalid("the object's state count is unknown",
                              "append states only to objects loaded from a structure file"));
  if (document.structures.empty())
    return R::failure(invalid("the file contains no structures"));

  // Identity check of every incoming structure against the object topology.
  const auto same_atoms = [&](const model::Topology &other) -> std::optional<std::string> {
    if (other.atom_count() != topology.atom_count())
      return "atom count " + std::to_string(other.atom_count()) + " differs from " +
             std::to_string(topology.atom_count());
    for (std::size_t atom = 0; atom < topology.atom_count(); ++atom) {
      const auto &a = topology.atoms()[atom];
      const auto &b = other.atoms()[atom];
      const auto &ra = topology.residues()[a.residue.value];
      const auto &rb = other.residues()[b.residue.value];
      if (a.name != b.name || a.atomic_number != b.atomic_number || ra.name != rb.name ||
          ra.sequence_number != rb.sequence_number || ra.chain_id != rb.chain_id ||
          ra.insertion_code != rb.insertion_code)
        return "atom " + std::to_string(atom + 1U) + " (" + ra.name + " " +
               std::to_string(ra.sequence_number) + " " + a.name + ") differs from (" + rb.name +
               " " + std::to_string(rb.sequence_number) + " " + b.name + ")";
    }
    return std::nullopt;
  };
  std::vector<std::shared_ptr<const model::CoordinateFrame>> frames;
  for (std::size_t state = 0; state < *previous_count; ++state) {
    const auto frame = object.system->coordinates()->read_frame(state);
    if (!frame.has_value()) return R::failure(frame.error());
    frames.push_back(frame.value());
  }
  std::size_t appended = 0;
  for (const auto &structure : document.structures) {
    if (structure.topology == nullptr || structure.coordinates == nullptr)
      return R::failure(invalid("a structure in the file has no coordinates"));
    if (const auto difference = same_atoms(*structure.topology); difference.has_value())
      return R::failure(invalid("the file does not match the object: " + *difference,
                                "append only files with the same atoms in the same order"));
    const auto count = structure.coordinates->frame_count();
    if (!count.has_value())
      return R::failure(invalid("a structure in the file has an unknown state count"));
    for (std::size_t state = 0; state < *count; ++state) {
      if (frames.size() >= kMaximumStates)
        return R::failure(operation::Error{
            operation::ErrorCode::resource_exhausted,
            "an object is limited to 100000 states", {}});
      const auto frame = structure.coordinates->read_frame(state);
      if (!frame.has_value()) return R::failure(frame.error());
      if (frame.value()->metadata().coordinate_unit !=
          frames.front()->metadata().coordinate_unit)
        return R::failure(invalid("the file uses a different coordinate unit than the object"));
      frames.push_back(frame.value());
      ++appended;
    }
  }
  if (appended == 0U) return R::failure(invalid("the file contains no states"));
  const auto replaced = replace_object_frames(
      index.value(), std::move(frames),
      embedded ? object.trajectory->timeline.snapshot().frame : 0U);
  if (!replaced.has_value()) return R::failure(replaced.error());
  return R::success({objects_[index.value()].id, objects_[index.value()].system->name(),
                     *previous_count, appended, replaced.value()});
}

operation::Result<std::size_t> Workspace::replace_object_frames(
    std::size_t index, std::vector<std::shared_ptr<const model::CoordinateFrame>> frames,
    std::size_t current_frame) {
  using R = operation::Result<std::size_t>;
  if (frames.empty()) return R::failure(invalid("an object needs at least one state"));
  const auto &object = objects_[index];
  const auto &topology = *object.system->topology();
  const bool embedded = object.trajectory.has_value() &&
                        object.trajectory->mapping.identity_strength ==
                            "embedded-structure-order";
  const auto source = model::InMemoryCoordinateSource::create(topology.atom_count(), frames);
  if (!source.has_value()) return R::failure(source.error());
  const auto system = model::MolecularSystem::create(
      object.system->id(), object.system->name(), object.system->topology(), source.value());
  if (!system.has_value()) return R::failure(system.error());

  WorkspaceObject candidate = object;
  const std::size_t previous_frame = std::min(current_frame, frames.size() - 1U);
  std::shared_ptr<const model::CoordinateSource> coordinates = source.value();
  candidate.trajectory.reset();
  if (frames.size() > 1U) {
    // Same embedded-trajectory construction as a multi-model load.
    constexpr std::size_t kEmbeddedCacheBudgetBytes = 256U * 1024U * 1024U;
    const auto cache = trajectory::FrameCache::create(source.value(), kEmbeddedCacheBudgetBytes);
    if (!cache.has_value()) return R::failure(cache.error());
    auto timeline = trajectory::PlaybackTimeline::create(frames.size());
    if (!timeline.has_value()) return R::failure(timeline.error());
    if (const auto error = timeline.value().seek(previous_frame); error.has_value())
      return R::failure(*error);
    auto clock = trajectory::PlaybackClock::create();
    if (!clock.has_value()) return R::failure(clock.error());
    auto prefetch = trajectory::PrefetchScheduler::create(cache.value());
    if (!prefetch.has_value()) return R::failure(prefetch.error());
    coordinates = cache.value();
    candidate.trajectory = TrajectoryState{
        embedded ? object.trajectory->path : std::filesystem::path{},
        io::TrajectoryFormat::auto_detect,
        cache.value(),
        std::move(timeline.value()),
        std::move(clock.value()),
        prefetch.value(),
        4U,
        trajectory::AtomMappingReport{trajectory::AtomMappingPolicy::index_order,
                                      "embedded-structure-order",
                                      {},
                                      {"structure-record-order"},
                                      topology.version()},
        {}};
  }
  const auto final_system = model::MolecularSystem::create(
      object.system->id(), object.system->name(), object.system->topology(), coordinates);
  if (!final_system.has_value()) return R::failure(final_system.error());
  candidate.system = final_system.value();
  if (candidate.coordinate_source_revision == std::numeric_limits<std::uint64_t>::max())
    return R::failure(operation::Error{operation::ErrorCode::resource_exhausted,
                                       "coordinate source revision space is exhausted", {}});
  ++candidate.coordinate_source_revision;
  candidate.molecular_surface.reset();
  const auto current = candidate.system->coordinates()->read_frame(previous_frame);
  if (!current.has_value()) return R::failure(current.error());
  const auto rebuilt = rebuild_representations(candidate, candidate.system, *current.value(),
                                               previous_frame, render_settings_, color_table_);
  if (!rebuilt.has_value()) return R::failure(rebuilt.error());
  candidate.representations = rebuilt.value();
  auto builder = scene::SceneBuilder::from(*scene_);
  if (const auto error = builder.replace_system(candidate.scene_node, candidate.system);
      error.has_value())
    return R::failure(*error);
  const auto next_scene = builder.build();
  if (!next_scene.has_value()) return R::failure(next_scene.error());
  objects_[index] = std::move(candidate);
  scene_ = next_scene.value();
  return R::success(frames.size());
}

operation::Result<PbcModeResult>
Workspace::set_pbc_mode(std::string_view object_reference, std::string_view mode) {
  using R = operation::Result<PbcModeResult>;
  if (mode != "none" && mode != "wrap" && mode != "unwrap" && mode != "status")
    return R::failure(invalid("pbc mode must be none, wrap, unwrap or status"));
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value()) return R::failure(index.error());
  auto &current = objects_[index.value()];
  const auto name_of = [](const WorkspaceObject &object) {
    return !object.pbc.has_value()                              ? std::string{"none"}
           : object.pbc->mode == trajectory::PbcMode::wrap ? std::string{"wrap"}
                                                              : std::string{"unwrap"};
  };
  PbcModeResult result;
  result.object_id = current.id;
  result.object_name = current.system->name();
  result.previous_mode = name_of(current);
  result.mode = result.previous_mode;
  result.frame_count = current.system->coordinates()->frame_count();
  if (mode == "status" || mode == result.previous_mode) return R::success(std::move(result));

  // The underlying (as loaded) coordinates and trajectory.
  const auto original_coordinates =
      current.pbc.has_value() ? current.pbc->original_coordinates : current.system->coordinates();
  std::optional<TrajectoryState> original_trajectory =
      current.pbc.has_value() ? current.pbc->original_trajectory : current.trajectory;
  WorkspaceObject candidate = current;
  std::shared_ptr<const model::CoordinateSource> coordinates = original_coordinates;
  std::optional<TrajectoryState> next_trajectory = original_trajectory;
  if (mode == "none") {
    candidate.pbc.reset();
    if (next_trajectory.has_value() && current.trajectory.has_value()) {
      next_trajectory->timeline = current.trajectory->timeline;
      next_trajectory->clock = current.trajectory->clock;
    }
  } else {
    const auto first = original_coordinates->read_frame(0U);
    if (!first.has_value()) return R::failure(first.error());
    if (!first.value()->metadata().unit_cell.has_value())
      return R::failure(invalid("the object has no periodic unit cell",
                                "PBC wrap and unwrap need frames with unit-cell metadata"));
    const auto processed = trajectory::PbcProcessedSource::create(
        original_coordinates,
        mode == "wrap" ? trajectory::PbcMode::wrap : trajectory::PbcMode::unwrap);
    if (!processed.has_value()) return R::failure(processed.error());
    coordinates = processed.value();
    candidate.pbc = PbcProcessing{
        mode == "wrap" ? trajectory::PbcMode::wrap : trajectory::PbcMode::unwrap,
        original_coordinates, original_trajectory};
    if (original_trajectory.has_value()) {
      const auto budget = original_trajectory->cache->stats().payload_budget_bytes;
      const auto cache = trajectory::FrameCache::create(processed.value(), budget);
      if (!cache.has_value()) return R::failure(cache.error());
      const auto prefetch = trajectory::PrefetchScheduler::create(cache.value());
      if (!prefetch.has_value()) return R::failure(prefetch.error());
      next_trajectory = *original_trajectory;
      next_trajectory->cache = cache.value();
      next_trajectory->prefetch = prefetch.value();
      if (current.trajectory.has_value()) {
        next_trajectory->timeline = current.trajectory->timeline;
        next_trajectory->clock = current.trajectory->clock;
      }
      coordinates = cache.value();
    }
  }
  candidate.trajectory = std::move(next_trajectory);
  const auto system = model::MolecularSystem::create(
      current.system->id(), current.system->name(), current.system->topology(), coordinates);
  if (!system.has_value()) return R::failure(system.error());
  candidate.system = system.value();
  if (candidate.coordinate_revision == std::numeric_limits<std::uint64_t>::max() ||
      candidate.coordinate_source_revision == std::numeric_limits<std::uint64_t>::max())
    return R::failure(operation::Error{operation::ErrorCode::resource_exhausted,
                                       "coordinate revision space is exhausted", {}});
  ++candidate.coordinate_revision;
  ++candidate.coordinate_source_revision;
  candidate.molecular_surface.reset();
  const std::size_t frame_index =
      candidate.trajectory.has_value() ? candidate.trajectory->timeline.snapshot().frame : 0U;
  const auto frame = candidate.system->coordinates()->read_frame(frame_index);
  if (!frame.has_value()) return R::failure(frame.error());
  const auto rebuilt = rebuild_representations(candidate, candidate.system, *frame.value(),
                                               frame_index, render_settings_, color_table_);
  if (!rebuilt.has_value()) return R::failure(rebuilt.error());
  candidate.representations = rebuilt.value();
  auto builder = scene::SceneBuilder::from(*scene_);
  if (const auto error = builder.replace_system(candidate.scene_node, candidate.system);
      error.has_value())
    return R::failure(*error);
  const auto next_scene = builder.build();
  if (!next_scene.has_value()) return R::failure(next_scene.error());
  if (candidate.trajectory.has_value()) static_cast<void>(schedule_prefetch(*candidate.trajectory));
  current = std::move(candidate);
  scene_ = next_scene.value();
  result.mode = std::string{mode};
  result.changed = true;
  result.frame_count = current.system->coordinates()->frame_count();
  return R::success(std::move(result));
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

operation::Result<ObjectSplitResult>
Workspace::split_object_by_chain(std::string_view object_reference, std::string prefix) {
  using R = operation::Result<ObjectSplitResult>;
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value()) return R::failure(index.error());
  const auto source_id = objects_[index.value()].id;
  const auto source_name = objects_[index.value()].system->name();
  const auto &topology = *objects_[index.value()].system->topology();
  std::vector<std::string> chains;
  for (const auto &atom : topology.atoms()) {
    const auto &chain = topology.residues()[atom.residue.value].chain_id;
    if (chain.empty())
      return R::failure(invalid("an atom has no chain identifier",
                                "assign chain identifiers before splitting"));
    if (std::find(chains.begin(), chains.end(), chain) == chains.end()) chains.push_back(chain);
  }
  if (chains.size() < 2U)
    return R::failure(invalid("the object has a single chain", "there is nothing to split"));
  if (prefix.empty()) prefix = source_name + "_";
  // Every target name must be free before anything is created.
  for (const auto &chain : chains) {
    const auto name = prefix + chain;
    const bool taken =
        std::any_of(objects_.begin(), objects_.end(),
                    [&](const auto &object) { return object.system->name() == name; }) ||
        std::any_of(volumes_.begin(), volumes_.end(),
                    [&](const auto &volume) { return volume.name == name; }) ||
        group_name_exists(name);
    if (taken)
      return R::failure(invalid("object name already exists: " + name, "choose another --prefix"));
  }
  // Atoms by chain in atom order, expressed as atom-ID subsets.
  const auto previous_active = active_index_;
  ObjectSplitResult result;
  result.source_object_id = source_id;
  result.source_object_name = source_name;
  for (const auto &chain : chains) {
    std::vector<model::AtomId> ids;
    for (std::size_t atom = 0; atom < topology.atom_count(); ++atom) {
      if (topology.residues()[topology.atoms()[atom].residue.value].chain_id != chain) continue;
      const auto id = topology.atom_id(model::AtomIndex{atom});
      if (!id.has_value()) return R::failure(invalid("topology atom identity is missing"));
      ids.push_back(*id);
    }
    const auto current_index = object_index_by_reference(std::to_string(source_id));
    if (!current_index.has_value()) return R::failure(current_index.error());
    const auto subset = make_atom_subset(*objects_[current_index.value()].system, ids);
    const auto rollback = [&]() {
      for (const auto &made : result.created)
        static_cast<void>(delete_object(std::to_string(made.object_id)));
      if (previous_active.has_value() && *previous_active < objects_.size())
        static_cast<void>(activate_object(objects_[*previous_active].id));
    };
    if (!subset.has_value()) {
      rollback();
      return R::failure(subset.error());
    }
    io::StructureDocument document;
    document.format = io::StructureFormat::auto_detect;
    document.source_name = "<object-" + std::to_string(source_id) + ">";
    const auto name = prefix + chain;
    document.structures.push_back({name, subset.value().topology, subset.value().coordinates,
                                   {{"derived_from_object", std::to_string(source_id)},
                                    {"chain", chain}}});
    const auto loaded = load_structure_document(std::move(document), {}, name);
    if (!loaded.has_value()) {
      rollback();
      return R::failure(loaded.error());
    }
    const auto created_index = object_index_by_reference(std::to_string(loaded.value().object_id));
    if (!created_index.has_value()) {
      rollback();
      return R::failure(created_index.error());
    }
    const auto &created = objects_[created_index.value()];
    result.created.push_back({source_id, source_name, loaded.value().object_id, name,
                              "chain " + chain, created.system->topology()->atom_count(),
                              created.system->topology()->bonds().size(),
                              created.system->coordinates()->frame_count(), 0U});
  }
  return R::success(std::move(result));
}

operation::Result<ObjectLifecycleResult> Workspace::sort_objects_by_name() {
  using R = operation::Result<ObjectLifecycleResult>;
  if (objects_.empty()) return R::failure(missing_active());
  std::vector<std::pair<std::string, std::uint64_t>> order;
  for (const auto &object : objects_) order.emplace_back(object.system->name(), object.id);
  std::stable_sort(order.begin(), order.end(), [](const auto &left, const auto &right) {
    return natural_less(left.first, right.first);
  });
  ObjectLifecycleResult last;
  for (std::size_t position = 0; position < order.size(); ++position) {
    if (objects_[position].id == order[position].second) continue;
    const auto moved = reorder_object(std::to_string(order[position].second), position);
    if (!moved.has_value()) return R::failure(moved.error());
    last = moved.value();
  }
  if (last.ordered_object_ids.empty())
    for (const auto &object : objects_) last.ordered_object_ids.push_back(object.id);
  last.object_count = objects_.size();
  return R::success(std::move(last));
}

operation::Result<ObjectLifecycleResult>
Workspace::set_object_fixed(std::string_view object_reference, bool fixed) {
  using R = operation::Result<ObjectLifecycleResult>;
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value()) return R::failure(index.error());
  objects_[index.value()].fixed = fixed;
  ObjectLifecycleResult result;
  result.object_count = objects_.size();
  for (const auto &object : objects_) result.ordered_object_ids.push_back(object.id);
  result.object_id = objects_[index.value()].id;
  result.name = objects_[index.value()].system->name();
  result.scene_node_id = objects_[index.value()].scene_node.value;
  return R::success(std::move(result));
}

operation::Result<ObjectTransformResetResult>
Workspace::copy_object_transform(std::string_view source_reference,
                                 std::string_view target_reference) {
  using R = operation::Result<ObjectTransformResetResult>;
  const auto source = object_index_by_reference(source_reference);
  if (!source.has_value()) return R::failure(source.error());
  const auto target = object_index_by_reference(target_reference);
  if (!target.has_value()) return R::failure(target.error());
  if (source.value() == target.value())
    return R::failure(invalid("the source and target objects must differ"));
  if (objects_[target.value()].fixed)
    return R::failure(invalid("the target object is fixed: " + objects_[target.value()].system->name(),
                              "run object free first"));
  const auto *node = scene_->find(objects_[source.value()].scene_node);
  if (node == nullptr)
    return R::failure(operation::Error{operation::ErrorCode::internal,
                                       "object scene node is missing", {}});
  auto builder = scene::SceneBuilder::from(*scene_);
  if (const auto error = builder.set_transform(objects_[target.value()].scene_node,
                                               node->local_transform());
      error.has_value())
    return R::failure(*error);
  const auto next_scene = builder.build();
  if (!next_scene.has_value()) return R::failure(next_scene.error());
  scene_ = next_scene.value();
  ObjectTransformResetResult result;
  result.object_reference = std::string{target_reference};
  result.object_ids = {objects_[target.value()].id};
  result.scene_version = scene_->version();
  return R::success(std::move(result));
}

namespace {

// Resolves an atom mask of a selection expression for the object's current frame.
operation::Result<std::vector<std::uint8_t>>
instance_selection_mask_now(const WorkspaceObject &object, std::string_view expression) {
  return selection_mask(object, expression);
}

std::size_t instance_primitive_count(const WorkspaceObject &object, const std::string &name) {
  std::size_t total = 0;
  const auto tag = "<rep:" + name + ">";
  for (const auto &record : object.representations) {
    if (record.selection_expression != tag) continue;
    const auto &packet = record.packet;
    total += packet.lines.size() + packet.cylinders.size() + packet.spheres.size() +
             packet.mesh_triangles.size() + packet.labels.size();
  }
  return total;
}

} // namespace

operation::Result<std::size_t>
Workspace::resolve_rep_reference(const WorkspaceObject &object, std::string_view reference) const {
  using R = operation::Result<std::size_t>;
  const auto &list = object.rep_instances;
  if (!reference.empty() &&
      std::all_of(reference.begin(), reference.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
    const auto number = static_cast<std::size_t>(std::stoul(std::string{reference}));
    if (number >= list.size())
      return R::failure(invalid("representation " + std::string{reference} + " does not exist",
                                "the object has " + std::to_string(list.size()) + " representation(s), numbered from 0"));
    return R::success(number);
  }
  for (std::size_t i = 0; i < list.size(); ++i)
    if (list[i].name == reference) return R::success(i);
  return R::failure(invalid("no representation named " + std::string{reference}));
}

std::optional<operation::Error>
Workspace::commit_rep_instances(std::size_t object_index, WorkspaceObject candidate) {
  const auto frame = active_frame(candidate);
  if (!frame.has_value()) return frame.error();
  const auto frame_index =
      candidate.trajectory.has_value() ? candidate.trajectory->timeline.snapshot().frame : 0U;
  const auto rebuilt = rebuild_representations(candidate, candidate.system, *frame.value(),
                                               frame_index, render_settings_, color_table_);
  if (!rebuilt.has_value()) return rebuilt.error();
  candidate.representations = rebuilt.value();
  objects_[object_index] = std::move(candidate);
  return std::nullopt;
}

operation::Result<std::vector<RepInstanceInfo>>
Workspace::list_rep_instances(std::string_view object_reference) const {
  using R = operation::Result<std::vector<RepInstanceInfo>>;
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value()) return R::failure(index.error());
  const auto &object = objects_[index.value()];
  std::vector<RepInstanceInfo> out;
  for (std::size_t i = 0; i < object.rep_instances.size(); ++i) {
    const auto &instance = object.rep_instances[i];
    const auto style = find_vmd_style(instance.style);
    out.push_back({i, instance, style.has_value() && style->renderable,
                   coloring_renderable(instance.color),
                   instance_primitive_count(object, instance.name)});
  }
  return R::success(std::move(out));
}

namespace {

std::optional<operation::Error> apply_rep_change(RepInstance &instance, const RepInstanceChange &change) {
  if (change.style.has_value()) {
    const auto style = find_vmd_style(*change.style);
    if (!style.has_value())
      return invalid("unknown representation style: " + *change.style, "see rep styles");
    instance.style = style->name;
  }
  if (change.params.has_value()) {
    for (const auto value : *change.params)
      if (!std::isfinite(value)) return invalid("style parameters must be finite numbers");
    instance.params = *change.params;
  }
  if (change.selection.has_value()) {
    if (const auto parsed = selection::Expression::parse(*change.selection); !parsed.has_value())
      return parsed.error();
    instance.selection = *change.selection;
  }
  if (change.color.has_value()) {
    const auto color = canonical_vmd_coloring(*change.color);
    if (!color.has_value())
      return invalid("unknown coloring method: " + *change.color, "see rep colorings");
    instance.color = *color;
  }
  if (change.color_id.has_value()) {
    if (*change.color_id < 0 || *change.color_id > 32)
      return invalid("color ids run from 0 to 32");
    instance.color_id = *change.color_id;
    if (!change.color.has_value()) instance.color = "ColorID";
  }
  if (change.material.has_value()) {
    const auto material = canonical_vmd_material(*change.material);
    if (!material.has_value())
      return invalid("unknown material: " + *change.material, "see rep materials");
    instance.material = *material;
  }
  if (change.visible.has_value()) instance.visible = *change.visible;
  if (change.name.has_value()) {
    if (change.name->empty() ||
        std::all_of(change.name->begin(), change.name->end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
      return invalid("a representation name must not be empty or only digits");
    instance.name = *change.name;
  }
  if (change.selection_update.has_value()) instance.selection_update = *change.selection_update;
  if (change.color_update.has_value()) instance.color_update = *change.color_update;
  if (change.smooth_window.has_value()) {
    if (*change.smooth_window < 0) return invalid("the smoothing window must not be negative");
    instance.smooth_window = *change.smooth_window;
  }
  if (change.draw_frames.has_value()) instance.draw_frames = *change.draw_frames;
  if (change.scale_auto) instance.scale_range.reset();
  if (change.scale_range.has_value()) {
    if (!(change.scale_range->first < change.scale_range->second))
      return invalid("scale minimum must be below maximum");
    instance.scale_range = change.scale_range;
  }
  return std::nullopt;
}

} // namespace

operation::Result<RepInstanceInfo>
Workspace::add_rep_instance(std::string_view object_reference, const RepInstanceChange &overrides) {
  using R = operation::Result<RepInstanceInfo>;
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value()) return R::failure(index.error());
  WorkspaceObject candidate = objects_[index.value()];
  RepInstance instance;
  instance.name = "rep" + std::to_string(candidate.next_rep_serial);
  instance.style = rep_defaults_.style;
  instance.color = rep_defaults_.color;
  instance.color_id = rep_defaults_.color_id;
  instance.selection = rep_defaults_.selection;
  instance.material = rep_defaults_.material;
  if (const auto error = apply_rep_change(instance, overrides); error.has_value()) return R::failure(*error);
  if (std::any_of(candidate.rep_instances.begin(), candidate.rep_instances.end(),
                  [&](const RepInstance &other) { return other.name == instance.name; }))
    return R::failure(invalid("a representation named " + instance.name + " already exists"));
  const auto mask = instance_selection_mask_now(candidate, instance.selection);
  if (!mask.has_value()) return R::failure(mask.error());
  if (!instance.selection_update) {
    const auto &topology = *candidate.system->topology();
    instance.frozen = true;
    for (std::size_t atom = 0; atom < mask.value().size(); ++atom)
      if (mask.value()[atom] != 0U) {
        const auto id = topology.atom_id(model::AtomIndex{atom});
        if (id.has_value()) instance.frozen_atoms.push_back(id->value);
      }
  }
  ++candidate.next_rep_serial;
  candidate.rep_instances.push_back(std::move(instance));
  const auto position = candidate.rep_instances.size() - 1U;
  if (const auto error = commit_rep_instances(index.value(), std::move(candidate)); error.has_value())
    return R::failure(*error);
  const auto listed = list_rep_instances(std::to_string(objects_[index.value()].id));
  if (!listed.has_value()) return R::failure(listed.error());
  return R::success(listed.value()[position]);
}

operation::Result<RepInstanceInfo>
Workspace::modify_rep_instance(std::string_view object_reference, std::string_view rep_reference,
                               const RepInstanceChange &change) {
  using R = operation::Result<RepInstanceInfo>;
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value()) return R::failure(index.error());
  WorkspaceObject candidate = objects_[index.value()];
  const auto position = resolve_rep_reference(candidate, rep_reference);
  if (!position.has_value()) return R::failure(position.error());
  auto &instance = candidate.rep_instances[position.value()];
  if (change.name.has_value() &&
      std::any_of(candidate.rep_instances.begin(), candidate.rep_instances.end(),
                  [&](const RepInstance &other) { return &other != &instance && other.name == *change.name; }))
    return R::failure(invalid("a representation named " + *change.name + " already exists"));
  if (const auto error = apply_rep_change(instance, change); error.has_value()) return R::failure(*error);
  const bool refreeze = change.selection.has_value() || change.selection_update.has_value();
  if (refreeze) {
    const auto mask = instance_selection_mask_now(candidate, instance.selection);
    if (!mask.has_value()) return R::failure(mask.error());
    instance.frozen_atoms.clear();
    instance.frozen = !instance.selection_update;
    if (instance.frozen) {
      const auto &topology = *candidate.system->topology();
      for (std::size_t atom = 0; atom < mask.value().size(); ++atom)
        if (mask.value()[atom] != 0U) {
          const auto id = topology.atom_id(model::AtomIndex{atom});
          if (id.has_value()) instance.frozen_atoms.push_back(id->value);
        }
    }
  }
  if (const auto error = commit_rep_instances(index.value(), std::move(candidate)); error.has_value())
    return R::failure(*error);
  const auto listed = list_rep_instances(std::to_string(objects_[index.value()].id));
  if (!listed.has_value()) return R::failure(listed.error());
  return R::success(listed.value()[position.value()]);
}

operation::Result<std::size_t>
Workspace::delete_rep_instance(std::string_view object_reference, std::string_view rep_reference) {
  using R = operation::Result<std::size_t>;
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value()) return R::failure(index.error());
  WorkspaceObject candidate = objects_[index.value()];
  const auto position = resolve_rep_reference(candidate, rep_reference);
  if (!position.has_value()) return R::failure(position.error());
  candidate.rep_instances.erase(candidate.rep_instances.begin() +
                                static_cast<std::ptrdiff_t>(position.value()));
  const auto remaining = candidate.rep_instances.size();
  if (const auto error = commit_rep_instances(index.value(), std::move(candidate)); error.has_value())
    return R::failure(*error);
  return R::success(remaining);
}

operation::Result<std::int64_t>
Workspace::rep_instance_index(std::string_view object_reference, std::string_view name) const {
  using R = operation::Result<std::int64_t>;
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value()) return R::failure(index.error());
  const auto &list = objects_[index.value()].rep_instances;
  for (std::size_t i = 0; i < list.size(); ++i)
    if (list[i].name == name) return R::success(static_cast<std::int64_t>(i));
  return R::success(-1);
}

std::optional<operation::Error>
Workspace::set_rep_default(std::string_view category, std::string_view value) {
  if (category == "style" || category == "representation") {
    const auto style = find_vmd_style(value);
    if (!style.has_value()) return invalid("unknown representation style: " + std::string{value});
    rep_defaults_.style = style->name;
  } else if (category == "color") {
    if (!value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
      const auto id = std::stoi(std::string{value});
      if (id > 32) return invalid("color ids run from 0 to 32");
      rep_defaults_.color = "ColorID";
      rep_defaults_.color_id = id;
    } else {
      const auto color = canonical_vmd_coloring(value);
      if (!color.has_value()) return invalid("unknown coloring method: " + std::string{value});
      rep_defaults_.color = *color;
    }
  } else if (category == "selection") {
    if (const auto parsed = selection::Expression::parse(value); !parsed.has_value()) return parsed.error();
    rep_defaults_.selection = std::string{value};
  } else if (category == "material") {
    const auto material = canonical_vmd_material(value);
    if (!material.has_value()) return invalid("unknown material: " + std::string{value});
    rep_defaults_.material = *material;
  } else {
    return invalid("default category must be style, color, selection or material");
  }
  return std::nullopt;
}

bool Workspace::object_name_in_use(std::string_view name) const {
  return std::any_of(objects_.begin(), objects_.end(),
                     [&](const auto &object) { return object.system->name() == name; }) ||
         std::any_of(volumes_.begin(), volumes_.end(),
                     [&](const auto &volume) { return volume.name == name; }) ||
         group_name_exists(name);
}

namespace {

constexpr std::size_t kMaximumObjectStates = 100000U;

std::string zero_padded_state(std::size_t state) {
  auto text = std::to_string(state);
  if (text.size() < 4U) text.insert(0U, 4U - text.size(), '0');
  return text;
}

// Only static objects and multi-model (embedded) objects have editable states.
std::optional<operation::Error> states_editable(const WorkspaceObject &object) {
  if (object.pbc.has_value())
    return invalid("the object is shown with PBC processing",
                   "run pbc mode --mode none before changing states");
  const bool embedded = object.trajectory.has_value() &&
                        object.trajectory->mapping.identity_strength ==
                            "embedded-structure-order";
  if (object.trajectory.has_value() && !embedded)
    return operation::Error{operation::ErrorCode::unsupported,
                            "the object has an attached trajectory",
                            "use traj load for trajectories; states are for multi-model objects", {}};
  return std::nullopt;
}

operation::Result<std::vector<std::shared_ptr<const model::CoordinateFrame>>>
read_all_states(const WorkspaceObject &object) {
  using R = operation::Result<std::vector<std::shared_ptr<const model::CoordinateFrame>>>;
  const auto count = object.system->coordinates()->frame_count();
  if (!count.has_value() || *count == 0U) return R::failure(invalid("the object's state count is unknown"));
  if (*count > kMaximumObjectStates)
    return R::failure(operation::Error{operation::ErrorCode::resource_exhausted,
                                       "an object is limited to 100000 states", {}});
  std::vector<std::shared_ptr<const model::CoordinateFrame>> frames;
  frames.reserve(*count);
  for (std::size_t state = 0; state < *count; ++state) {
    const auto frame = object.system->coordinates()->read_frame(state);
    if (!frame.has_value()) return R::failure(frame.error());
    frames.push_back(frame.value());
  }
  return R::success(std::move(frames));
}

} // namespace

operation::Result<ObjectSplitResult>
Workspace::split_states(std::string_view object_reference, std::size_t first,
                        std::size_t last, std::string prefix) {
  using R = operation::Result<ObjectSplitResult>;
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value()) return R::failure(index.error());
  const auto &object = objects_[index.value()];
  if (const auto error = states_editable(object); error.has_value()) return R::failure(*error);
  const auto frames = read_all_states(object);
  if (!frames.has_value()) return R::failure(frames.error());
  if (first == 0U) first = 1U;
  const auto total = frames.value().size();
  const auto end = (last == 0U || last > total) ? total : last;
  if (first > end)
    return R::failure(invalid("no states in the requested range",
                              "the object has " + std::to_string(total) + " state(s)"));
  const auto source_id = object.id;
  const auto source_name = object.system->name();
  if (prefix.empty()) prefix = source_name + "_";
  std::vector<std::string> names;
  for (auto state = first; state <= end; ++state) {
    names.push_back(prefix + zero_padded_state(state));
    if (object_name_in_use(names.back()))
      return R::failure(invalid("object name already exists: " + names.back(),
                                "choose another --prefix"));
  }
  const auto previous_active = active_index_;
  ObjectSplitResult result;
  result.source_object_id = source_id;
  result.source_object_name = source_name;
  const auto topology = object.system->topology();
  const auto rollback = [&]() {
    for (const auto &made : result.created)
      static_cast<void>(delete_object(std::to_string(made.object_id)));
    if (previous_active.has_value() && *previous_active < objects_.size())
      static_cast<void>(activate_object(objects_[*previous_active].id));
  };
  for (auto state = first; state <= end; ++state) {
    const auto source = model::InMemoryCoordinateSource::create(
        topology->atom_count(), {frames.value()[state - 1U]});
    if (!source.has_value()) { rollback(); return R::failure(source.error()); }
    io::StructureDocument document;
    document.format = io::StructureFormat::auto_detect;
    document.source_name = "<object-" + std::to_string(source_id) + ">";
    const auto &name = names[state - first];
    document.structures.push_back({name, topology, source.value(),
                                   {{"derived_from_object", std::to_string(source_id)},
                                    {"state", std::to_string(state)}}});
    const auto loaded = load_structure_document(std::move(document), {}, name);
    if (!loaded.has_value()) { rollback(); return R::failure(loaded.error()); }
    const auto created_index = object_index_by_reference(std::to_string(loaded.value().object_id));
    if (!created_index.has_value()) { rollback(); return R::failure(created_index.error()); }
    const auto &created = objects_[created_index.value()];
    result.created.push_back({source_id, source_name, loaded.value().object_id, name,
                              "state " + std::to_string(state), topology->atom_count(),
                              topology->bonds().size(), created.system->coordinates()->frame_count(),
                              0U});
  }
  return R::success(std::move(result));
}

operation::Result<CoordinateMoveResult>
Workspace::move_selected_atoms(std::string selection_expression, CoordinateMoveMode mode,
                               const std::vector<double> &values, std::string_view state_scope) {
  using R = operation::Result<CoordinateMoveResult>;
  auto *object = mutable_active_object();
  if (object == nullptr) return R::failure(missing_active());
  if (object->pbc.has_value())
    return R::failure(invalid("the object is shown with PBC processing",
                              "run pbc mode --mode none before editing atoms or states"));
  if (object->fixed)
    return R::failure(invalid("the object is fixed: " + object->system->name(),
                              "run object free first"));
  if (object->trajectory.has_value())
    return R::failure(operation::Error{
        operation::ErrorCode::unsupported,
        "coordinate editing is not available while a trajectory is attached",
        "detach or create a static copy before editing"});
  for (const auto value : values)
    if (!std::isfinite(value)) return R::failure(invalid("coordinate edit values must be finite"));
  const auto mask = selection_mask(*object, selection_expression);
  if (!mask.has_value()) return R::failure(mask.error());
  std::vector<std::size_t> atoms;
  for (std::size_t atom = 0; atom < mask.value().size(); ++atom)
    if (mask.value()[atom] != 0U) atoms.push_back(atom);
  if (atoms.empty()) return R::failure(invalid("selection matches no atoms"));
  const bool per_atom = mode != CoordinateMoveMode::transform && values.size() == 3U * atoms.size() &&
                        atoms.size() > 1U;
  if (mode == CoordinateMoveMode::transform) {
    if (values.size() != 16U) return R::failure(invalid("a transform needs 16 matrix numbers (row-major 4x4)"));
    if (values[12] != 0.0 || values[13] != 0.0 || values[14] != 0.0 || values[15] != 1.0)
      return R::failure(invalid("only affine matrices are supported (last row 0 0 0 1)"));
  } else if (!per_atom && values.size() != 3U) {
    return R::failure(invalid("provide 3 numbers or 3 numbers per selected atom",
                              "selected " + std::to_string(atoms.size()) + " atom(s)"));
  }
  const auto frame_count = object->system->coordinates()->frame_count();
  if (!frame_count.has_value() || *frame_count == 0U)
    return R::failure(operation::Error{operation::ErrorCode::unsupported,
                                       "coordinate editing requires a finite in-memory frame set", {}});
  std::optional<std::size_t> only_state;
  if (state_scope != "all" && state_scope != "current") {
    try {
      const auto number = std::stoul(std::string{state_scope});
      if (number == 0U || number > *frame_count) throw std::out_of_range("state");
      only_state = static_cast<std::size_t>(number - 1U);
    } catch (const std::exception &) {
      return R::failure(invalid("state must be current, all or a state number from 1"));
    }
  } else if (state_scope == "current") {
    only_state = 0U;
  }
  std::vector<std::shared_ptr<const model::CoordinateFrame>> frames;
  frames.reserve(*frame_count);
  for (std::size_t state = 0; state < *frame_count; ++state) {
    const auto frame = object->system->coordinates()->read_frame(state);
    if (!frame.has_value()) return R::failure(frame.error());
    if (only_state.has_value() && *only_state != state) {
      frames.push_back(frame.value());
      continue;
    }
    std::vector<model::Vec3d> next;
    next.reserve(atoms.size());
    for (std::size_t i = 0; i < atoms.size(); ++i) {
      if (!frame.value()->atom_present(atoms[i]))
        return R::failure(invalid("an atom is missing from state " + std::to_string(state + 1U)));
      const auto at = coordinate_at(*frame.value(), atoms[i]);
      if (mode == CoordinateMoveMode::transform) {
        next.push_back({values[0] * at.x + values[1] * at.y + values[2] * at.z + values[3],
                        values[4] * at.x + values[5] * at.y + values[6] * at.z + values[7],
                        values[8] * at.x + values[9] * at.y + values[10] * at.z + values[11]});
      } else {
        const auto base = per_atom ? 3U * i : 0U;
        const model::Vec3d given{values[base], values[base + 1U], values[base + 2U]};
        next.push_back(mode == CoordinateMoveMode::set
                           ? given
                           : model::Vec3d{at.x + given.x, at.y + given.y, at.z + given.z});
      }
    }
    const auto replaced = frame_with_coordinates(*frame.value(), atoms, next);
    if (!replaced.has_value()) return R::failure(replaced.error());
    frames.push_back(replaced.value());
  }
  const auto edited_states = only_state.has_value() ? std::size_t{1U} : *frame_count;
  const auto committed = commit_coordinate_frames(object, std::move(frames));
  if (!committed.has_value()) return R::failure(committed.error());
  return R::success({committed.value().transaction_id, object->id, atoms.size(), edited_states,
                     committed.value().invalidated_measurements});
}

operation::Result<CoordinateMoveResult> Workspace::set_unit_cell(std::optional<CellParameters> cell) {
  using R = operation::Result<CoordinateMoveResult>;
  auto *object = mutable_active_object();
  if (object == nullptr) return R::failure(missing_active());
  if (object->pbc.has_value())
    return R::failure(invalid("the object is shown with PBC processing", "run pbc mode --mode none before editing the cell"));
  if (object->fixed)
    return R::failure(invalid("the object is fixed: " + object->system->name(), "run object free first"));
  if (object->trajectory.has_value() && object->trajectory->mapping.identity_strength != "embedded-structure-order")
    return R::failure(operation::Error{operation::ErrorCode::unsupported,
                                       "the cell cannot be edited while a trajectory is attached",
                                       "detach or create a static copy before editing", {}});
  std::optional<model::UnitCell> unit_cell;
  if (cell.has_value()) {
    const double pi = std::acos(-1.0);
    for (const double angle : {cell->alpha, cell->beta, cell->gamma})
      if (!(angle > 0.0 && angle < 180.0)) return R::failure(invalid("cell angles must be between 0 and 180 degrees"));
    for (const double length : {cell->a, cell->b, cell->c})
      if (!(length > 0.0) || !std::isfinite(length)) return R::failure(invalid("cell lengths must be positive"));
    const double ca = std::cos(cell->alpha * pi / 180.0), cb = std::cos(cell->beta * pi / 180.0), cg = std::cos(cell->gamma * pi / 180.0);
    const double sg = std::sin(cell->gamma * pi / 180.0);
    const double cz_square = 1.0 - cb * cb - ((ca - cb * cg) / sg) * ((ca - cb * cg) / sg);
    if (!(cz_square > 1.0e-9)) return R::failure(invalid("the cell angles do not describe a valid parallelepiped"));
    model::UnitCell built{{cell->a, 0.0, 0.0},
                          {cell->b * cg, cell->b * sg, 0.0},
                          {cell->c * cb, cell->c * (ca - cb * cg) / sg, cell->c * std::sqrt(cz_square)}};
    unit_cell = built;
  }
  const auto frame_count = object->system->coordinates()->frame_count();
  if (!frame_count.has_value() || *frame_count == 0U) return R::failure(invalid("the object's state count is unknown"));
  std::vector<std::shared_ptr<const model::CoordinateFrame>> frames;
  for (std::size_t state = 0; state < *frame_count; ++state) {
    const auto frame = object->system->coordinates()->read_frame(state);
    if (!frame.has_value()) return R::failure(frame.error());
    auto metadata = frame.value()->metadata();
    if (unit_cell.has_value()) {
      const double scale = metadata.coordinate_unit == operation::LengthUnit::nanometer ? 0.1 : 1.0;
      metadata.unit_cell = model::UnitCell{{unit_cell->a.x * scale, unit_cell->a.y * scale, unit_cell->a.z * scale},
                                           {unit_cell->b.x * scale, unit_cell->b.y * scale, unit_cell->b.z * scale},
                                           {unit_cell->c.x * scale, unit_cell->c.y * scale, unit_cell->c.z * scale}};
    } else {
      metadata.unit_cell.reset();
    }
    const auto replaced = model::CoordinateFrame::create(frame.value()->positions(), frame.value()->velocities(), frame.value()->presence(),
                                                         std::move(metadata));
    if (!replaced.has_value()) return R::failure(replaced.error());
    frames.push_back(replaced.value());
  }
  const auto committed = commit_coordinate_frames(object, std::move(frames));
  if (!committed.has_value()) return R::failure(committed.error());
  return R::success({committed.value().transaction_id, object->id, 0U, *frame_count, committed.value().invalidated_measurements});
}

operation::Result<StateDeleteResult>
Workspace::delete_states(std::string_view object_reference, std::string_view states) {
  using R = operation::Result<StateDeleteResult>;
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value()) return R::failure(index.error());
  const auto &object = objects_[index.value()];
  if (const auto error = states_editable(object); error.has_value()) return R::failure(*error);
  const auto frames = read_all_states(object);
  if (!frames.has_value()) return R::failure(frames.error());
  const auto total = frames.value().size();

  // "1-3 10" / "1-3,10": numbers and inclusive ranges (either direction).
  std::set<std::size_t> doomed;
  std::string normalized{states};
  for (auto &ch : normalized) if (ch == ',' || ch == '\t') ch = ' ';
  std::size_t position = 0;
  const auto parse_number = [&](std::size_t &value) -> bool {
    while (position < normalized.size() && normalized[position] == ' ') ++position;
    const auto begin = position;
    while (position < normalized.size() && std::isdigit(static_cast<unsigned char>(normalized[position])))
      ++position;
    if (begin == position || position - begin > 9U) return false;
    value = static_cast<std::size_t>(std::stoul(normalized.substr(begin, position - begin)));
    return true;
  };
  bool any = false;
  while (true) {
    while (position < normalized.size() && normalized[position] == ' ') ++position;
    if (position >= normalized.size()) break;
    std::size_t lo = 0;
    if (!parse_number(lo))
      return R::failure(invalid("states must be numbers or ranges such as \"1-3 10\""));
    std::size_t hi = lo;
    if (position < normalized.size() && normalized[position] == '-') {
      ++position;
      if (!parse_number(hi)) return R::failure(invalid("a state range is missing its end"));
    }
    if (lo == 0U || hi == 0U) return R::failure(invalid("states are numbered from 1"));
    if (lo > hi) std::swap(lo, hi);
    for (auto state = lo; state <= hi && state <= total; ++state) doomed.insert(state);
    any = true;
  }
  if (!any) return R::failure(invalid("provide the states to delete, e.g. \"1-3 10\""));
  if (doomed.size() >= total)
    return R::failure(invalid("deleting every state would leave an object without coordinates",
                              "use object delete to remove the whole object"));
  StateDeleteResult result{object.id, object.system->name(), total, doomed.size(), total - doomed.size()};
  if (doomed.empty()) return R::success(std::move(result));

  std::vector<std::shared_ptr<const model::CoordinateFrame>> kept;
  const bool embedded = object.trajectory.has_value();
  const std::size_t current = embedded ? object.trajectory->timeline.snapshot().frame : 0U;
  std::size_t new_current = 0;
  for (std::size_t state = 1; state <= total; ++state) {
    if (doomed.contains(state)) continue;
    if (state - 1U <= current) new_current = kept.size();
    kept.push_back(frames.value()[state - 1U]);
  }
  const auto replaced = replace_object_frames(index.value(), std::move(kept), new_current);
  if (!replaced.has_value()) return R::failure(replaced.error());
  result.state_count = replaced.value();
  return R::success(std::move(result));
}

operation::Result<StateDeleteResult>
Workspace::delete_frames(std::string_view object_reference, long long beg, long long end,
                         long long skip) {
  using R = operation::Result<StateDeleteResult>;
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value()) return R::failure(index.error());
  const auto &object = objects_[index.value()];
  if (const auto error = states_editable(object); error.has_value()) return R::failure(*error);
  const auto frames = read_all_states(object);
  if (!frames.has_value()) return R::failure(frames.error());
  const auto total = static_cast<long long>(frames.value().size());
  if (beg < 0) beg = 0;
  if (end < 0 || end >= total) end = total - 1;
  if (skip < 0) skip = 0;
  if (beg > end)
    return R::failure(invalid("the frame range is empty",
                              "the object has " + std::to_string(total) + " frame(s), numbered from 0"));
  std::vector<std::shared_ptr<const model::CoordinateFrame>> kept;
  std::size_t removed = 0;
  const std::size_t current =
      object.trajectory.has_value() ? object.trajectory->timeline.snapshot().frame : 0U;
  std::size_t new_current = 0;
  for (long long frame = 0; frame < total; ++frame) {
    const bool in_range = frame >= beg && frame <= end;
    const bool keep = !in_range || (skip >= 1 && (frame - beg) % skip == 0);
    if (!keep) { ++removed; continue; }
    if (static_cast<std::size_t>(frame) <= current) new_current = kept.size();
    kept.push_back(frames.value()[static_cast<std::size_t>(frame)]);
  }
  StateDeleteResult result{object.id, object.system->name(), static_cast<std::size_t>(total),
                           removed, static_cast<std::size_t>(total) - removed};
  if (removed == 0U) return R::success(std::move(result));
  if (kept.empty())
    return R::failure(invalid("deleting every frame would leave an object without coordinates",
                              "use object delete to remove the whole object"));
  const auto replaced = replace_object_frames(index.value(), std::move(kept), new_current);
  if (!replaced.has_value()) return R::failure(replaced.error());
  result.state_count = replaced.value();
  return R::success(std::move(result));
}

operation::Result<StateAppendResult>
Workspace::duplicate_frame(std::string_view object_reference, std::optional<std::size_t> frame) {
  using R = operation::Result<StateAppendResult>;
  const auto index = object_index_by_reference(object_reference);
  if (!index.has_value()) return R::failure(index.error());
  const auto &object = objects_[index.value()];
  if (const auto error = states_editable(object); error.has_value()) return R::failure(*error);
  auto frames = read_all_states(object);
  if (!frames.has_value()) return R::failure(frames.error());
  if (frames.value().size() >= kMaximumObjectStates)
    return R::failure(operation::Error{operation::ErrorCode::resource_exhausted,
                                       "an object is limited to 100000 states", {}});
  const std::size_t current =
      object.trajectory.has_value() ? object.trajectory->timeline.snapshot().frame : 0U;
  const auto source = frame.value_or(current);
  if (source >= frames.value().size())
    return R::failure(invalid("frame " + std::to_string(source) + " does not exist",
                              "the object has " + std::to_string(frames.value().size()) +
                                  " frame(s), numbered from 0"));
  const auto previous = frames.value().size();
  frames.value().push_back(frames.value()[source]);
  const auto replaced = replace_object_frames(index.value(), std::move(frames.value()), current);
  if (!replaced.has_value()) return R::failure(replaced.error());
  return R::success({objects_[index.value()].id, objects_[index.value()].system->name(), previous,
                     1U, replaced.value()});
}

operation::Result<ObjectCreateResult>
Workspace::join_states(std::string name, const std::vector<std::string> &object_references,
                       std::string selection_expression, StateJoinMode mode) {
  using R = operation::Result<ObjectCreateResult>;
  if (name.empty()) return R::failure(invalid("new object name must not be empty"));
  if (object_name_in_use(name))
    return R::failure(invalid("object name already exists: " + name, "choose another name"));
  if (object_references.empty()) return R::failure(invalid("provide at least one object to join"));
  std::vector<std::size_t> indices;
  for (const auto &reference : object_references) {
    const auto index = object_index_by_reference(reference);
    if (!index.has_value()) return R::failure(index.error());
    if (std::find(indices.begin(), indices.end(), index.value()) == indices.end())
      indices.push_back(index.value());
  }
  struct Part {
    std::vector<std::size_t> atoms;   // selected atom indices in topology order
    std::vector<std::string> keys;    // identity key per selected atom
    std::vector<std::shared_ptr<const model::CoordinateFrame>> frames;
  };
  const auto key_of = [](const model::Topology &topology, std::size_t atom) {
    const auto &record = topology.atoms()[atom];
    const auto &residue = topology.residues()[record.residue.value];
    return residue.chain_id + "|" + residue.name + "|" + std::to_string(residue.sequence_number) +
           "|" + residue.insertion_code + "|" + record.name;
  };
  std::vector<Part> parts;
  for (const auto index : indices) {
    const auto &object = objects_[index];
    if (const auto error = states_editable(object); error.has_value()) return R::failure(*error);
    const auto mask = selection_mask(object, selection_expression);
    if (!mask.has_value()) return R::failure(mask.error());
    Part part;
    const auto &topology = *object.system->topology();
    for (std::size_t atom = 0; atom < mask.value().size(); ++atom)
      if (mask.value()[atom] != 0U) {
        part.atoms.push_back(atom);
        part.keys.push_back(key_of(topology, atom));
      }
    if (part.atoms.empty())
      return R::failure(invalid("the selection matches no atoms in " + object.system->name()));
    std::set<std::string> unique(part.keys.begin(), part.keys.end());
    if (unique.size() != part.keys.size())
      return R::failure(invalid("atoms in " + object.system->name() +
                                    " are not uniquely identified by chain, residue and atom name",
                                "restrict the selection to one alternate location"));
    const auto frames = read_all_states(object);
    if (!frames.has_value()) return R::failure(frames.error());
    part.frames = frames.value();
    parts.push_back(std::move(part));
  }

  // Common atoms in the order of the first object.
  std::vector<std::size_t> reference_atoms;  // atom indices in the first object
  std::vector<std::vector<std::optional<std::size_t>>> mappings(parts.size());
  if (mode == StateJoinMode::identical_topology) {
    for (std::size_t k = 1; k < parts.size(); ++k)
      if (parts[k].keys != parts[0].keys)
        return R::failure(invalid(
            "the objects do not have identical atoms: " + objects_[indices[k]].system->name() +
                " differs from " + objects_[indices[0]].system->name(),
            "use --mode intersection to join the atoms they share"));
    reference_atoms = parts[0].atoms;
    for (std::size_t k = 0; k < parts.size(); ++k) {
      mappings[k].resize(parts[k].atoms.size());
      for (std::size_t i = 0; i < parts[k].atoms.size(); ++i) mappings[k][i] = parts[k].atoms[i];
    }
  } else {
    std::vector<std::map<std::string, std::size_t>> lookup(parts.size());
    for (std::size_t k = 0; k < parts.size(); ++k)
      for (std::size_t i = 0; i < parts[k].atoms.size(); ++i)
        lookup[k].emplace(parts[k].keys[i], parts[k].atoms[i]);
    std::vector<std::size_t> kept_first;
    for (std::size_t i = 0; i < parts[0].atoms.size(); ++i) {
      const bool everywhere = std::all_of(lookup.begin() + 1, lookup.end(), [&](const auto &table) {
        return table.contains(parts[0].keys[i]);
      });
      if (everywhere) kept_first.push_back(i);
    }
    if (kept_first.empty())
      return R::failure(invalid("the objects have no atoms in common"));
    for (const auto i : kept_first) reference_atoms.push_back(parts[0].atoms[i]);
    for (std::size_t k = 0; k < parts.size(); ++k) {
      for (const auto i : kept_first) mappings[k].push_back(lookup[k].at(parts[0].keys[i]));
    }
  }

  std::vector<std::shared_ptr<const model::CoordinateFrame>> joined;
  std::optional<operation::LengthUnit> unit;
  for (std::size_t k = 0; k < parts.size(); ++k) {
    const auto source = model::InMemoryCoordinateSource::create(
        objects_[indices[k]].system->topology()->atom_count(), parts[k].frames);
    if (!source.has_value()) return R::failure(source.error());
    const auto remapped = model::RemappedCoordinateSource::create(source.value(), mappings[k]);
    if (!remapped.has_value()) return R::failure(remapped.error());
    for (std::size_t state = 0; state < parts[k].frames.size(); ++state) {
      if (joined.size() >= kMaximumObjectStates)
        return R::failure(operation::Error{operation::ErrorCode::resource_exhausted,
                                           "an object is limited to 100000 states", {}});
      const auto frame = remapped.value()->read_frame(state);
      if (!frame.has_value()) return R::failure(frame.error());
      if (!unit.has_value()) unit = frame.value()->metadata().coordinate_unit;
      else if (*unit != frame.value()->metadata().coordinate_unit)
        return R::failure(invalid("the objects use different coordinate units"));
      joined.push_back(frame.value());
    }
  }

  std::vector<model::AtomId> ids;
  const auto &first_topology = *objects_[indices[0]].system->topology();
  for (const auto atom : reference_atoms) {
    const auto id = first_topology.atom_id(model::AtomIndex{atom});
    if (!id.has_value()) return R::failure(invalid("topology atom identity is missing"));
    ids.push_back(*id);
  }
  const auto subset = make_atom_subset(*objects_[indices[0]].system, ids);
  if (!subset.has_value()) return R::failure(subset.error());
  const auto states = model::InMemoryCoordinateSource::create(ids.size(), joined);
  if (!states.has_value()) return R::failure(states.error());

  const auto source_id = objects_[indices[0]].id;
  const auto source_name = objects_[indices[0]].system->name();
  io::StructureDocument document;
  document.format = io::StructureFormat::auto_detect;
  document.source_name = "<join-states>";
  document.structures.push_back({name, subset.value().topology, states.value(),
                                 {{"derived_from_object", std::to_string(source_id)},
                                  {"join_mode", mode == StateJoinMode::identical_topology
                                                    ? "identical"
                                                    : "intersection"}}});
  const auto loaded = load_structure_document(std::move(document), {}, name);
  if (!loaded.has_value()) return R::failure(loaded.error());
  const auto created_index = object_index_by_reference(std::to_string(loaded.value().object_id));
  if (!created_index.has_value()) return R::failure(created_index.error());
  const auto &created = objects_[created_index.value()];
  return R::success({source_id, source_name, loaded.value().object_id, created.system->name(),
                     selection_expression, created.system->topology()->atom_count(),
                     created.system->topology()->bonds().size(),
                     created.system->coordinates()->frame_count(), 0U});
}

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
  if (object->pbc.has_value())
    return operation::Result<CoordinateEditResult>::failure(invalid(
        "the object is shown with PBC processing",
        "run pbc mode --mode none before editing atoms or states"));
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
  auto committed = commit_coordinate_frames(object, std::move(frames));
  if (!committed.has_value())
    return operation::Result<CoordinateEditResult>::failure(committed.error());
  const auto &done = committed.value();
  return operation::Result<CoordinateEditResult>::success(
      {done.transaction_id, object->id, atom_id, 0U, previous, position,
       done.previous_source_revision, done.previous_coordinate_revision,
       object->coordinate_source_revision, object->coordinate_revision,
       *frame_count, done.invalidated_measurements, done.memory_bytes, done.evicted});
}

operation::Result<Workspace::CommittedCoordinateEdit>
Workspace::commit_coordinate_frames(
    WorkspaceObject *object,
    std::vector<std::shared_ptr<const model::CoordinateFrame>> frames) {
  using R = operation::Result<CommittedCoordinateEdit>;
  const auto &topology = *object->system->topology();
  const auto memory_bytes = saturated_add(
      coordinate_source_payload_bytes(*object->system->coordinates()),
      sizeof(EditRecord));
  if (memory_bytes > edit_history_memory_budget_bytes_)
    return R::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "coordinate edit cannot retain an undo snapshot within the budget",
        "increase the edit history budget or edit a smaller object",
        {{"memory_required_bytes", std::to_string(memory_bytes)},
         {"memory_budget_bytes",
          std::to_string(edit_history_memory_budget_bytes_)}}});
  const auto coordinates = model::InMemoryCoordinateSource::create(
      topology.atom_count(), std::move(frames));
  if (!coordinates.has_value())
    return R::failure(coordinates.error());
  const auto system = model::MolecularSystem::create(
      object->id, object->system->name(), object->system->topology(),
      coordinates.value());
  if (!system.has_value())
    return R::failure(system.error());
  if (object->coordinate_source_revision ==
          std::numeric_limits<std::uint64_t>::max() ||
      object->coordinate_revision == std::numeric_limits<std::uint64_t>::max() ||
      next_edit_transaction_id_ == std::numeric_limits<std::uint64_t>::max())
    return R::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "coordinate edit revision or transaction space is exhausted", {}});

  WorkspaceObject candidate = *object;
  candidate.system = system.value();
  ++candidate.coordinate_source_revision;
  ++candidate.coordinate_revision;
  candidate.molecular_surface.reset();
  const auto frame = candidate.system->coordinates()->read_frame(0U);
  if (!frame.has_value())
    return R::failure(frame.error());
  const auto rebuilt = rebuild_representations(
      candidate, candidate.system, *frame.value(), 0U, render_settings_,
      color_table_);
  if (!rebuilt.has_value())
    return R::failure(rebuilt.error());
  candidate.representations = rebuilt.value();
  auto scene_builder = scene::SceneBuilder::from(*scene_);
  if (const auto error =
          scene_builder.replace_system(candidate.scene_node, candidate.system);
      error.has_value())
    return R::failure(*error);
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value())
    return R::failure(next_scene.error());

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
    return R::failure(operation::Error{
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
  return R::success(CommittedCoordinateEdit{transaction_id, previous_source_revision,
                                           previous_coordinate_revision, invalidated, memory_bytes,
                                           evicted});
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
  if (std::any_of(objects_.begin(), objects_.end(),
                  [](const auto &object) { return object.pbc.has_value(); }))
    return operation::Result<EditHistoryResult>::failure(invalid(
        "an object is shown with PBC processing",
        "run pbc mode --mode none before undoing or redoing edits"));
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
  if (std::any_of(objects_.begin(), objects_.end(),
                  [](const auto &object) { return object.pbc.has_value(); }))
    return operation::Result<EditHistoryResult>::failure(invalid(
        "an object is shown with PBC processing",
        "run pbc mode --mode none before undoing or redoing edits"));
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
  if (request.format != "guess" && request.format != "png" &&
      request.format != "ppm")
    return R::failure(invalid("image format must be guess, png or ppm"));
  // Format and filename-extension policy: the extension selects the encoder in
  // guess mode, and a path without an extension gets the format's extension.
  auto path = request.path;
  std::string format = request.format;
  if (format == "guess") {
    auto extension = path.extension().string();
    for (auto &character : extension)
      character = static_cast<char>(
          std::tolower(static_cast<unsigned char>(character)));
    if (extension.empty())
      format = "png";
    else if (extension == ".png")
      format = "png";
    else if (extension == ".ppm")
      format = "ppm";
    else
      return R::failure(invalid(
          "cannot guess the image format from extension '" + extension + "'",
          "use a .png or .ppm path, or pass --image-format png|ppm"));
  }
  if (!path.has_extension())
    path += format == "ppm" ? ".ppm" : ".png";
  constexpr std::size_t kMaximumEdge = 8192U;
  auto parameters = camera_->parameters();
  std::size_t requested_width = request.width;
  std::size_t requested_height = request.height;
  std::optional<double> effective_dpi = request.dpi;
  if (request.units != "pixels") {
    double per_unit{};
    if (request.units == "inches") per_unit = 1.0;
    else if (request.units == "centimeters") per_unit = 1.0 / 2.54;
    else if (request.units == "millimeters") per_unit = 1.0 / 25.4;
    else
      return R::failure(invalid(
          "image units must be pixels, inches, centimeters or millimeters"));
    if (!effective_dpi.has_value()) effective_dpi = 300.0;
    const auto to_pixels = [&](const std::optional<double> &value)
        -> operation::Result<std::size_t> {
      if (!value.has_value()) return operation::Result<std::size_t>::success(0U);
      if (!std::isfinite(*value) || *value <= 0.0)
        return operation::Result<std::size_t>::failure(
            invalid("physical image dimensions must be positive"));
      const auto pixels = std::llround(*value * per_unit * *effective_dpi);
      if (pixels < 1)
        return operation::Result<std::size_t>::failure(
            invalid("physical image dimension is smaller than one pixel"));
      return operation::Result<std::size_t>::success(
          static_cast<std::size_t>(pixels));
    };
    const auto w = to_pixels(request.width_value);
    if (!w.has_value()) return R::failure(w.error());
    const auto h = to_pixels(request.height_value);
    if (!h.has_value()) return R::failure(h.error());
    requested_width = w.value();
    requested_height = h.value();
  }
  // Aspect-preserving size: a missing edge follows the camera aspect ratio.
  const double aspect = std::max(1.0e-6, parameters.aspect_ratio);
  std::size_t width = requested_width;
  std::size_t height = requested_height;
  if (width == 0U && height == 0U) width = 640U;
  if (width == 0U)
    width = std::max<std::size_t>(
        1U, static_cast<std::size_t>(
                std::lround(static_cast<double>(height) * aspect)));
  if (height == 0U)
    height = std::max<std::size_t>(
        1U, static_cast<std::size_t>(
                std::lround(static_cast<double>(width) / aspect)));
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
  if (format == "ppm") {
    if (const auto error = render::write_ppm(path, image.value()); error.has_value())
      return R::failure(*error);
  } else if (const auto error = render::write_png_atomic(path, image.value(),
                                                         effective_dpi);
             error.has_value()) {
    return R::failure(*error);
  }

  ImageSaveResult result;
  result.path = path;
  result.format = format;
  result.dpi = format == "png" ? effective_dpi : std::nullopt;
  result.width = width;
  result.height = height;
  std::error_code size_error;
  result.bytes = static_cast<std::size_t>(
      std::filesystem::file_size(path, size_error));
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

operation::Result<MovieVideoResult>
Workspace::export_movie_video(const MovieVideoRequest &request,
                              operation::TaskContext &context) {
  using R = operation::Result<MovieVideoResult>;
  if (!movie_.has_value() || movie_->frame_count == 0U)
    return R::failure(operation::Error{operation::ErrorCode::not_found,
                                       "no movie timeline is configured",
                                       "run movie configure or movie map first"});
  if (request.path.empty()) return R::failure(invalid("video output path must not be empty"));
  auto format = request.format;
  if (format == "auto") {
    format = video_format_from_extension(request.path);
    if (format.empty())
      return R::failure(invalid("cannot guess the video format from '" + request.path.string() + "'",
                                "use a .mp4, .mov, .webm, .gif or .mpg name, or pass --video-format"));
  }
  if (format != "mp4" && format != "mov" && format != "webm" && format != "gif" && format != "mpeg1")
    return R::failure(invalid("unsupported video format '" + format + "'", "use mp4, mov, webm, gif or mpeg1"));
  std::error_code filesystem_error;
  if (!request.overwrite && std::filesystem::exists(request.path, filesystem_error))
    return R::failure(invalid("video output exists: " + request.path.string(), "choose another path or pass --overwrite true"));
  const auto parent = std::filesystem::absolute(request.path).parent_path();
  if (!std::filesystem::is_directory(parent, filesystem_error))
    return R::failure(operation::Error{operation::ErrorCode::not_found,
                                       "output directory does not exist: " + parent.string(), "create it first"});
  // Fail before rendering when the encoder is missing.
  const auto encoder = find_ffmpeg(request.encoder_path);
  if (!encoder.has_value()) return R::failure(encoder.error());
  const double fps = request.fps.value_or(movie_->frames_per_second);

  // Unique scratch directory for the numbered frames.
  std::filesystem::path scratch;
  {
    const auto base = std::filesystem::temp_directory_path(filesystem_error);
    if (filesystem_error) return R::failure(operation::Error{operation::ErrorCode::internal, "no temporary directory available", {}});
    for (std::size_t attempt = 0; attempt < 100U; ++attempt) {
      const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      const auto candidate = base / ("molshredder-movie-" + std::to_string(stamp) + "-" + std::to_string(attempt));
      if (std::filesystem::create_directory(candidate, filesystem_error)) {
        scratch = candidate;
        break;
      }
    }
    if (scratch.empty()) return R::failure(operation::Error{operation::ErrorCode::internal, "could not create a scratch directory", {}});
  }
  struct Cleanup {
    std::filesystem::path path;
    bool keep{};
    ~Cleanup() {
      if (keep || path.empty()) return;
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
  } cleanup{scratch, request.keep_frames};

  MovieExportRequest frames;
  frames.prefix = scratch / "frame_";
  frames.first = request.first;
  frames.last = request.last;
  frames.overwrite = true;
  frames.image = request.image;
  frames.image.format = "png";
  const auto exported = export_movie_frames(frames);
  if (!exported.has_value()) return R::failure(exported.error());
  if (context.cancellation.is_cancelled())
    return R::failure(operation::Error{operation::ErrorCode::cancelled, "video export cancelled", {}});

  VideoEncodeRequest encode;
  encode.frames_directory = scratch;
  encode.frame_pattern = "frame_%04d.png";
  encode.first_frame_number = exported.value().first_frame;
  encode.output = request.path;
  encode.format = format;
  encode.frames_per_second = fps;
  encode.quality = request.quality;
  encode.executable = encoder.value();
  const auto encoded = encode_video(encode, context);
  if (!encoded.has_value()) {
    cleanup.keep = request.keep_frames;
    return R::failure(encoded.error());
  }
  MovieVideoResult result;
  result.output = request.path;
  result.format = format;
  result.frame_count = exported.value().frame_count;
  result.width = exported.value().width;
  result.height = exported.value().height;
  result.frames_per_second = fps;
  result.bytes = encoded.value().bytes;
  result.encoder = encoded.value().executable;
  result.command = encoded.value().command;
  if (request.keep_frames) result.frames_directory = scratch;
  result.warnings = exported.value().warnings;
  if (format != "gif" && (result.width % 2U != 0U || result.height % 2U != 0U))
    result.warnings.push_back("frame size is odd; the encoder scales it down to even dimensions");
  return R::success(std::move(result));
}

operation::Result<MovieExportResult>
Workspace::export_movie_frames(const MovieExportRequest &request) {
  using R = operation::Result<MovieExportResult>;
  if (!movie_.has_value() || movie_->frame_count == 0U)
    return R::failure(operation::Error{
        operation::ErrorCode::not_found, "no movie timeline is configured",
        "run movie configure first"});
  if (movie_->playing)
    return R::failure(invalid("movie export cannot run while the movie plays",
                              "run movie pause first"));
  if (request.prefix.empty())
    return R::failure(invalid("movie export prefix must not be empty"));
  const auto count = movie_->frame_count;
  const auto first = request.first.value_or(1U);
  const auto last = request.last.value_or(count);
  if (first < 1U || last > count || first > last)
    return R::failure(invalid("movie export range is outside the timeline",
                              "use 1-based frames with first <= last <= " +
                                  std::to_string(count)));
  constexpr std::size_t kMaximumFrames = 100000U;
  if (last - first + 1U > kMaximumFrames)
    return R::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "movie export is limited to 100000 frames per call", {}});
  // Pre-check the output names so an existing file is never replaced silently.
  auto frame_path = [&](std::size_t frame, const std::string &extension) {
    auto text = std::to_string(frame);
    if (text.size() < 4U) text.insert(0U, 4U - text.size(), '0');
    return std::filesystem::path{request.prefix.string() + text + extension};
  };
  std::string format = request.image.format;
  if (format == "guess") format = "png";
  const std::string extension = format == "ppm" ? ".ppm" : ".png";
  if (format != "png" && format != "ppm")
    return R::failure(invalid("movie export format must be png or ppm"));
  if (!request.overwrite) {
    for (auto frame = first; frame <= last; ++frame) {
      std::error_code exists_error;
      if (std::filesystem::exists(frame_path(frame, extension), exists_error))
        return R::failure(invalid(
            "movie export target exists: " + frame_path(frame, extension).string(),
            "choose another prefix or pass --overwrite true"));
    }
  }
  const auto original_frame = movie_->current_frame;
  MovieExportResult result;
  result.first_frame = first;
  result.last_frame = last;
  result.format = format;
  std::vector<std::filesystem::path> created;
  const auto cleanup = [&]() {
    for (const auto &path : created) {
      std::error_code remove_error;
      std::filesystem::remove(path, remove_error);
    }
  };
  for (auto frame = first; frame <= last; ++frame) {
    const auto moved = seek_movie(frame);
    if (!moved.has_value()) {
      cleanup();
      static_cast<void>(seek_movie(original_frame));
      return R::failure(moved.error());
    }
    auto image = request.image;
    image.format = format;
    image.path = frame_path(frame, extension);
    const auto saved = save_image(image);
    if (!saved.has_value()) {
      cleanup();
      static_cast<void>(seek_movie(original_frame));
      return R::failure(saved.error());
    }
    created.push_back(saved.value().path);
    result.paths.push_back(saved.value().path);
    result.width = saved.value().width;
    result.height = saved.value().height;
    result.total_bytes += saved.value().bytes;
    if (frame == first) result.warnings = saved.value().warnings;
  }
  const auto restored = seek_movie(original_frame);
  if (!restored.has_value()) {
    cleanup();
    return R::failure(restored.error());
  }
  result.frame_count = last - first + 1U;
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
  // Session-style documents carry representation and selection requests that are applied after the objects exist.
  struct SessionExtras {
    std::vector<std::vector<io::StructureRepresentationHint>> hints;
    std::vector<bool> visible;
    std::vector<std::string> original_names;
    std::vector<std::pair<std::string, std::string>> selections;
    std::vector<std::string> notes;
  } extras;
  const bool session_import = !document.structures.empty() &&
                              (!document.named_selections.empty() ||
                               std::any_of(document.structures.begin(), document.structures.end(),
                                           [](const io::StructureData &structure) {
                                             return !structure.representation_hints.empty() || !structure.visible;
                                           }));
  if (session_import) {
    for (const auto &structure : document.structures) {
      extras.hints.push_back(structure.representation_hints);
      extras.visible.push_back(structure.visible);
      extras.original_names.push_back(structure.name);
    }
    extras.selections = document.named_selections;
  }
  extras.notes = document.import_notes;
  operation::TaskContext context;
  auto loaded = load_structure_documents(
      {{std::move(document), source_path, std::move(name)}}, context);
  if (!loaded.has_value())
    return operation::Result<LoadResult>::failure(loaded.error());
  LoadResult result{loaded.value().object_id, loaded.value().object_name,
                    loaded.value().atom_count, loaded.value().frame_count, format,
                    std::move(loaded.value().objects), {}};
  if (session_import) {
    const auto kind_of = [](const std::string &representation) -> std::optional<render::RepresentationKind> {
      if (representation == "lines") return render::RepresentationKind::lines;
      if (representation == "sticks") return render::RepresentationKind::sticks;
      if (representation == "spheres") return render::RepresentationKind::spheres;
      if (representation == "ribbon") return render::RepresentationKind::ribbon;
      if (representation == "cartoon") return render::RepresentationKind::cartoon;
      return std::nullopt;
    };
    bool names_kept = true;
    for (const auto &object : result.objects) {
      const auto index = object.source_record_index;
      if (index >= extras.original_names.size()) continue;
      if (object.object_name != extras.original_names[index]) names_kept = false;
      if (const auto activated = activate_object(object.object_id); !activated.has_value()) {
        extras.notes.push_back("could not select object " + object.object_name + " to apply the session's representations");
        continue;
      }
      for (const auto &hint : extras.hints[index]) {
        const auto kind = kind_of(hint.representation);
        if (!kind.has_value()) continue;
        if (const auto shown = show(*kind, hint.selection, false); !shown.has_value())
          extras.notes.push_back("object " + object.object_name + ": could not show " + hint.representation + ": " + shown.error().message);
      }
      if (!extras.visible[index]) {
        if (const auto hidden = set_object_visibility(object.object_id, false); !hidden.has_value())
          extras.notes.push_back("object " + object.object_name + " could not be hidden as in the session");
      }
    }
    if (!extras.selections.empty()) {
      if (!names_kept) {
        extras.notes.push_back("named selections were not created because the objects were renamed on load");
      } else {
        for (const auto &[selection_name, expression] : extras.selections) {
          // Selections are evaluated in the active object; use the first object the expression names ("(model NAME and ...").
          constexpr std::string_view prefix = "(model ";
          if (expression.starts_with(prefix)) {
            const auto end = expression.find(' ', prefix.size());
            const auto object_name = expression.substr(prefix.size(), end == std::string::npos ? std::string::npos : end - prefix.size());
            for (const auto &object : result.objects)
              if (object.object_name == object_name) (void)activate_object(object.object_id);
          }
          if (const auto error = set_named_selection(selection_name, expression, false); error.has_value())
            extras.notes.push_back("selection " + selection_name + " was not created: " + error->message);
        }
      }
    }
    (void)activate_object(result.object_id);
  }
  result.import_notes = std::move(extras.notes);
  return operation::Result<LoadResult>::success(std::move(result));
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
      if (existing || duplicate_in_batch || group_name_exists(object_name)) {
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
        render::kColorDefault, nullptr, nullptr, nullptr, std::nullopt, false, {}, 0U});
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
Workspace::combine_volumes(const std::vector<std::string> &names, std::string_view mode, std::string result_name) {
  using R = operation::Result<VolumeLoadResult>;
  if (mode != "sum" && mode != "difference" && mode != "average" && mode != "product" && mode != "maximum" && mode != "minimum")
    return R::failure(invalid("volume combine mode must be sum, difference, average, product, maximum or minimum"));
  if (names.size() < 2U) return R::failure(invalid("volume combine needs at least two volumes"));
  if (names.size() > 64U) return R::failure(invalid("volume combine is limited to 64 volumes"));
  if (result_name.empty()) return R::failure(invalid("the combined volume needs a name"));
  std::vector<const WorkspaceVolume *> sources;
  for (const auto &name : names) {
    const auto found = std::find_if(volumes_.begin(), volumes_.end(), [&](const auto &volume) { return volume.name == name; });
    if (found == volumes_.end())
      return R::failure(operation::Error{operation::ErrorCode::not_found, "no volume named " + name, "use volume list to see the loaded volumes", {}});
    sources.push_back(&*found);
  }
  const auto &first = *sources.front()->grid;
  const auto close = [](double a, double b) { return std::abs(a - b) <= 1.0e-6 * std::max({1.0, std::abs(a), std::abs(b)}); };
  for (std::size_t i = 1; i < sources.size(); ++i) {
    const auto &other = *sources[i]->grid;
    bool same = other.shape() == first.shape() && other.metadata().coordinate_unit == first.metadata().coordinate_unit &&
                close(other.origin().x, first.origin().x) && close(other.origin().y, first.origin().y) && close(other.origin().z, first.origin().z);
    for (std::size_t axis = 0; same && axis < 3U; ++axis)
      same = close(other.deltas()[axis].x, first.deltas()[axis].x) && close(other.deltas()[axis].y, first.deltas()[axis].y) &&
             close(other.deltas()[axis].z, first.deltas()[axis].z);
    if (!same)
      return R::failure(invalid("volumes " + names.front() + " and " + names[i] + " do not share one grid",
                                "resample them to a common grid before combining"));
  }
  const std::size_t count = first.value_count();
  std::vector<double> values(count);
  for (std::size_t v = 0; v < count; ++v) {
    double result = sources.front()->grid->scalars().value(v);
    for (std::size_t i = 1; i < sources.size(); ++i) {
      const double x = sources[i]->grid->scalars().value(v);
      if (mode == "sum" || mode == "average") result += x;
      else if (mode == "difference") result -= x;
      else if (mode == "product") result *= x;
      else if (mode == "maximum") result = std::max(result, x);
      else result = std::min(result, x);
    }
    if (mode == "average") result /= static_cast<double>(sources.size());
    values[v] = result;
  }
  model::VolumeMetadata metadata = first.metadata();
  metadata.fields["combine_mode"] = std::string{mode};
  std::string joined;
  for (const auto &name : names) joined += (joined.empty() ? "" : ",") + name;
  metadata.fields["combine_sources"] = joined;
  const auto grid = model::VolumeGrid::create(first.shape(), first.origin(), first.deltas(), model::VolumeScalarBuffer{std::move(values)}, metadata);
  if (!grid.has_value()) return R::failure(grid.error());
  io::VolumeDocument document;
  document.volumes.push_back({std::move(result_name), grid.value()});
  return load_volume_document(std::move(document), std::filesystem::path{"<combined>"});
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
  if (duplicate_molecule || duplicate_volume || group_name_exists(data.name)) {
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
    bool overwrite, operation::TaskContext &context,
    std::string_view selection_expression) const {
  const auto *object = active_object();
  if (object == nullptr) {
    return operation::Result<SaveResult>::failure(missing_active());
  }
  // A selection is written as the subset topology (stable atom IDs, bonds
  // among the selected atoms) with the matching coordinate columns in every
  // state; "all" keeps the whole object untouched.
  std::shared_ptr<const model::Topology> topology = object->system->topology();
  std::shared_ptr<const model::CoordinateSource> coordinates =
      object->system->coordinates();
  if (selection_expression != "all") {
    const auto selected = selected_atom_ids(object->id, selection_expression);
    if (!selected.has_value())
      return operation::Result<SaveResult>::failure(selected.error());
    if (selected.value().empty())
      return operation::Result<SaveResult>::failure(
          invalid("selection matches no atoms",
                  "check the selection expression"));
    if (selected.value().size() != topology->atom_count()) {
      const auto subset =
          make_atom_subset(*object->system, atom_ids_from(selected.value()));
      if (!subset.has_value())
        return operation::Result<SaveResult>::failure(subset.error());
      topology = subset.value().topology;
      coordinates = subset.value().coordinates;
    }
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
      path, *topology, *coordinates, std::move(options), overwrite, context);
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
                               bool dynamic, bool merge, std::string state) {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return missing_active();
  const auto parsed = selection::Expression::parse(expression);
  if (!parsed.has_value())
    return parsed.error();
  const auto current_frame = object->trajectory.has_value()
                                 ? object->trajectory->timeline.snapshot().frame
                                 : 0U;
  const auto context_for = [&](std::size_t frame_index)
      -> operation::Result<selection::EvaluationContext> {
    const auto frame = object->system->coordinates()->read_frame(frame_index);
    if (!frame.has_value())
      return operation::Result<selection::EvaluationContext>::failure(frame.error());
    return operation::Result<selection::EvaluationContext>::success(
        selection::EvaluationContext{frame.value().get(), frame_index,
                                     object->system->coordinates().get(),
                                     object->system->name()});
  };
  const auto& topology = *object->system->topology();
  if (state != "current") {
    if (dynamic)
      return invalid("a dynamic selection follows the current state",
                     "use --state current, or define a static selection");
    if (merge)
      return invalid("--merge cannot be combined with an explicit --state",
                     "merge into the current state, or replace the selection");
    if (state == "all") {
      const auto count = object->system->coordinates()->frame_count();
      if (!count.has_value() || *count == 0U)
        return invalid("the state count is unknown",
                       "choose one state explicitly");
      std::vector<selection::EvaluationContext> contexts;
      std::vector<std::shared_ptr<const model::CoordinateFrame>> keep;
      for (std::size_t index = 0; index < *count; ++index) {
        const auto frame = object->system->coordinates()->read_frame(index);
        if (!frame.has_value()) return frame.error();
        keep.push_back(frame.value());
        contexts.push_back(selection::EvaluationContext{
            frame.value().get(), index, object->system->coordinates().get(),
            object->system->name()});
      }
      return object->selections.set_state_union(std::move(name), parsed.value(),
                                                topology, contexts);
    }
    std::size_t number{};
    const auto parsed_number = molshredder::core::from_chars(
        state.data(), state.data() + state.size(), number);
    if (parsed_number.ec != std::errc{} ||
        parsed_number.ptr != state.data() + state.size() || number == 0U)
      return invalid("--state must be current, all or a 1-based state number");
    const auto count = object->system->coordinates()->frame_count();
    if (count.has_value() && number > *count)
      return invalid("--state is out of range");
    const auto context = context_for(number - 1U);
    if (!context.has_value()) return context.error();
    return object->selections.set(std::move(name), parsed.value(), false, topology,
                                  context.value());
  }
  const auto frame = object->system->coordinates()->read_frame(current_frame);
  if (!frame.has_value()) return frame.error();
  const selection::EvaluationContext context{frame.value().get(), current_frame,
                                             object->system->coordinates().get(),
                                             object->system->name()};
  if (merge)
    return object->selections.merge(std::move(name), parsed.value(), dynamic,
                                    topology, context);
  return object->selections.set(std::move(name), parsed.value(), dynamic, topology,
                                context);
}

std::optional<operation::Error> Workspace::delete_named_selection(
    std::string_view name) {
  auto *object = mutable_active_object();
  if (object == nullptr)
    return missing_active();
  return object->selections.erase(name);
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

  const bool everything = object_reference == "all";
  if (!everything && objects_[indices.front()].fixed)
    return operation::Result<ObjectTransformResetResult>::failure(
        invalid("the object is fixed: " + objects_[indices.front()].system->name(),
                "run object free first"));
  auto builder = scene::SceneBuilder::from(*scene_);
  ObjectTransformResetResult result;
  result.object_reference = std::move(object_reference);
  result.object_ids.reserve(indices.size());
  for (const auto index : indices) {
    const auto &object = objects_[index];
    if (everything && object.fixed) continue;
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
