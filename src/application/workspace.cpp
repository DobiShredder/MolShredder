#include "molshredder/application/workspace.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "molshredder/operation/error.hpp"
#include "molshredder/selection/evaluator.hpp"
#include "molshredder/selection/expression.hpp"

namespace molshredder::application {
namespace {

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

operation::Result<std::vector<RepresentationRecord>> rebuild_representations(
    const WorkspaceObject &object,
    const std::shared_ptr<const model::MolecularSystem> &system,
    const model::CoordinateFrame &frame, std::size_t frame_index) {
  const auto visuals = atom_visuals(*system->topology());
  if (!visuals.has_value()) {
    return operation::Result<std::vector<RepresentationRecord>>::failure(
        visuals.error());
  }
  std::vector<RepresentationRecord> rebuilt;
  rebuilt.reserve(object.representations.size());
  for (const auto &representation : object.representations) {
    const auto parsed =
        selection::Expression::parse(representation.selection_expression);
    if (!parsed.has_value()) {
      return operation::Result<std::vector<RepresentationRecord>>::failure(
          parsed.error());
    }
    const auto mask = selection::evaluate(
        parsed.value(), *system->topology(), [&](std::string_view name) {
          return object.selections.evaluate(name, *system->topology());
        });
    if (!mask.has_value()) {
      return operation::Result<std::vector<RepresentationRecord>>::failure(
          mask.error());
    }
    render::RepresentationRequest request{system->topology().get(),
                                          &frame,
                                          frame_index,
                                          object.scene_node.value,
                                          visuals.value(),
                                          mask.value(),
                                          {},
                                          {}};
    request.style.kind = representation.kind;
    const auto packet = render::build_representation(request);
    if (!packet.has_value()) {
      return operation::Result<std::vector<RepresentationRecord>>::failure(
          packet.error());
    }
    rebuilt.push_back(RepresentationRecord{representation.kind,
                                           representation.selection_expression,
                                           packet.value()});
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
  return selection::evaluate(
      parsed.value(), *object.system->topology(), [&](std::string_view name) {
        return object.selections.evaluate(name, *object.system->topology());
      });
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
                           "atom distance endpoint selects multiple atoms",
                           "use an expression selecting exactly one atom"});
    }
    selected = index;
  }
  if (!selected.has_value()) {
    return operation::Result<model::AtomIndex>::failure(
        operation::Error{operation::ErrorCode::invalid_selection,
                         "atom distance endpoint selects no atoms",
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

Workspace::Workspace() {
  const auto built = scene::SceneBuilder{}.build();
  if (!built.has_value())
    std::terminate();
  scene_ = built.value();
  auto camera = scene::Camera::create();
  if (!camera.has_value())
    std::terminate();
  camera_ = std::move(camera.value());
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
  const auto parsed = std::from_chars(reference.data(),
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

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
Workspace::active_frame(const WorkspaceObject &object) const {
  const auto index = object.trajectory.has_value()
                         ? object.trajectory->timeline.snapshot().frame
                         : 0U;
  return object.system->coordinates()->read_frame(index);
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
  if (document.structures.empty()) {
    return operation::Result<LoadResult>::failure(
        invalid("structure document contains no data blocks"));
  }
  if (name.has_value() && name->empty()) {
    return operation::Result<LoadResult>::failure(
        invalid("explicit workspace object name must not be empty",
                "omit --name to use names from the file"));
  }
  const auto structure_count = document.structures.size();
  if (structure_count >
      std::numeric_limits<std::uint64_t>::max() - next_object_id_) {
    return operation::Result<LoadResult>::failure(
        invalid("structure import would overflow the object identifier space"));
  }

  std::vector<std::string> object_names;
  object_names.reserve(structure_count);
  for (std::size_t index = 0; index < structure_count; ++index) {
    auto object_name = name.has_value()
                           ? name.value()
                           : document.structures[index].name;
    if (name.has_value() && structure_count > 1U) {
      object_name += "_" + std::to_string(index + 1U);
    }
    if (object_name.empty()) {
      object_name = source_path.stem().string();
      if (structure_count > 1U) {
        object_name += "_" + std::to_string(index + 1U);
      }
    }
    if (object_name.empty()) {
      return operation::Result<LoadResult>::failure(
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
      return operation::Result<LoadResult>::failure(
          invalid("workspace object name already exists: " + object_name,
                  "choose a unique --name"));
    }
    object_names.push_back(std::move(object_name));
  }

  auto scene_builder = scene::SceneBuilder::from(*scene_);
  std::vector<WorkspaceObject> pending_objects;
  std::vector<LoadedObjectResult> loaded_objects;
  pending_objects.reserve(structure_count);
  loaded_objects.reserve(structure_count);
  for (std::size_t index = 0; index < structure_count; ++index) {
    const auto object_id = next_object_id_ + index;
    const auto &data = document.structures[index];
    auto coordinate_source = data.coordinates;
    std::optional<TrajectoryState> embedded_trajectory;
    const auto frame_count = data.coordinates->frame_count().value_or(0U);
    if (frame_count > 1U) {
      constexpr std::size_t kEmbeddedCacheBudgetBytes = 256U * 1024U * 1024U;
      const auto cache = trajectory::FrameCache::create(
          data.coordinates, kEmbeddedCacheBudgetBytes);
      if (!cache.has_value()) {
        return operation::Result<LoadResult>::failure(cache.error());
      }
      auto timeline = trajectory::PlaybackTimeline::create(frame_count);
      if (!timeline.has_value()) {
        return operation::Result<LoadResult>::failure(timeline.error());
      }
      auto clock = trajectory::PlaybackClock::create();
      if (!clock.has_value()) {
        return operation::Result<LoadResult>::failure(clock.error());
      }
      auto prefetch = trajectory::PrefetchScheduler::create(cache.value());
      if (!prefetch.has_value()) {
        return operation::Result<LoadResult>::failure(prefetch.error());
      }
      coordinate_source = cache.value();
      embedded_trajectory = TrajectoryState{source_path,
                                            io::TrajectoryFormat::auto_detect,
                                            cache.value(),
                                            std::move(timeline.value()),
                                            std::move(clock.value()),
                                            prefetch.value(),
                                            4U};
    }
    const auto system = model::MolecularSystem::create(
        object_id, object_names[index], data.topology, coordinate_source);
    if (!system.has_value()) {
      return operation::Result<LoadResult>::failure(system.error());
    }
    const auto node = scene_builder.add_system(
        scene_->root(), object_names[index], system.value());
    if (!node.has_value()) {
      return operation::Result<LoadResult>::failure(node.error());
    }
    pending_objects.push_back(WorkspaceObject{object_id,
                                              node.value(),
                                              system.value(),
                                              {},
                                              {},
                                              std::move(embedded_trajectory)});
    loaded_objects.push_back(LoadedObjectResult{
        object_id, object_names[index], data.topology->atom_count(),
        data.coordinates->frame_count().value_or(0U), index});
  }
  if (const auto error =
          scene_builder.set_selection({pending_objects.back().scene_node});
      error.has_value()) {
    return operation::Result<LoadResult>::failure(*error);
  }
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value()) {
    return operation::Result<LoadResult>::failure(next_scene.error());
  }
  objects_.insert(objects_.end(),
                  std::make_move_iterator(pending_objects.begin()),
                  std::make_move_iterator(pending_objects.end()));
  next_object_id_ += structure_count;
  active_index_ = objects_.size() - 1U;
  scene_ = next_scene.value();
  const auto &active = loaded_objects.back();
  return operation::Result<LoadResult>::success(LoadResult{
      active.object_id, active.object_name, active.atom_count,
      active.frame_count, document.format, std::move(loaded_objects)});
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
  const auto document = io::read_volume_file(path, std::move(options));
  if (!document.has_value()) {
    return operation::Result<VolumeLoadResult>::failure(document.error());
  }
  if (document.value().volumes.size() != 1U) {
    return operation::Result<VolumeLoadResult>::failure(
        invalid("volume import requires exactly one scalar field"));
  }
  const auto &data = document.value().volumes.front();
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
  volumes_.push_back(WorkspaceVolume{object_id, node.value(), data.name, path,
                                     document.value().format, data.grid, {}});
  active_volume_index_ = volumes_.size() - 1U;
  scene_ = next_scene.value();
  const auto [minimum, maximum] = data.grid->scalars().range();
  return operation::Result<VolumeLoadResult>::success(VolumeLoadResult{
      object_id, data.name, document.value().format, data.grid->shape(),
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
  return operation::Result<VolumeIsosurfaceResult>::success(
      {volume->id, index, level, color, vertex_count, triangle_count, bounds});
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
  return object->selections.set(std::move(name), parsed.value(), dynamic,
                                *object->system->topology());
}

operation::Result<ShowResult> Workspace::show(render::RepresentationKind kind,
                                              std::string selection_expression,
                                              bool replace_existing) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<ShowResult>::failure(missing_active());
  }
  const auto parsed = selection::Expression::parse(selection_expression);
  if (!parsed.has_value()) {
    return operation::Result<ShowResult>::failure(parsed.error());
  }
  const auto mask = selection::evaluate(
      parsed.value(), *object->system->topology(), [&](std::string_view name) {
        return object->selections.evaluate(name, *object->system->topology());
      });
  if (!mask.has_value()) {
    return operation::Result<ShowResult>::failure(mask.error());
  }
  const auto frame = active_frame(*object);
  if (!frame.has_value()) {
    return operation::Result<ShowResult>::failure(frame.error());
  }
  const auto visuals = atom_visuals(*object->system->topology());
  if (!visuals.has_value()) {
    return operation::Result<ShowResult>::failure(visuals.error());
  }
  const auto frame_index = object->trajectory.has_value()
                               ? object->trajectory->timeline.snapshot().frame
                               : 0U;
  render::RepresentationRequest request{object->system->topology().get(),
                                        frame.value().get(),
                                        frame_index,
                                        object->scene_node.value,
                                        visuals.value(),
                                        mask.value(),
                                        {},
                                        {}};
  request.style.kind = kind;
  const auto packet = render::build_representation(request);
  if (!packet.has_value()) {
    return operation::Result<ShowResult>::failure(packet.error());
  }
  const auto selected_count = static_cast<std::size_t>(
      std::count(mask.value().begin(), mask.value().end(), std::uint8_t{1U}));
  const auto primitive_count =
      packet.value().lines.size() + packet.value().cylinders.size() +
      packet.value().spheres.size() + packet.value().mesh_triangles.size();
  if (replace_existing)
    object->representations.clear();
  const auto index = object->representations.size();
  object->representations.push_back(RepresentationRecord{
      kind, std::move(selection_expression), packet.value()});
  return operation::Result<ShowResult>::success(
      ShowResult{object->id, index, primitive_count, selected_count});
}

operation::Result<CenterAnalysisResult>
Workspace::analyze_center(std::string selection_expression,
                          analysis::CenterMode mode) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<CenterAnalysisResult>::failure(missing_active());
  }
  const auto parsed = selection::Expression::parse(selection_expression);
  if (!parsed.has_value()) {
    return operation::Result<CenterAnalysisResult>::failure(parsed.error());
  }
  const auto mask = selection::evaluate(
      parsed.value(), *object->system->topology(), [&](std::string_view name) {
        return object->selections.evaluate(name, *object->system->topology());
      });
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
  const auto parsed = selection::Expression::parse(selection_expression);
  if (!parsed.has_value())
    return operation::Result<CameraDepthExtent>::failure(parsed.error());
  const auto mask = selection::evaluate(
      parsed.value(), *object->system->topology(),
      [&](std::string_view name) {
        return object->selections.evaluate(name, *object->system->topology());
      });
  if (!mask.has_value())
    return operation::Result<CameraDepthExtent>::failure(mask.error());

  const auto frame_range = resolve_camera_frame_range(*object, state_scope);
  if (!frame_range.has_value())
    return operation::Result<CameraDepthExtent>::failure(frame_range.error());
  const auto first_frame_index = frame_range.value().first;
  const auto scoped_frame_count = frame_range.value().count;

  CameraDepthExtent result;
  auto &extent = result.spatial;
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
  extent.center = (extent.minimum + extent.maximum) * 0.5;
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
  const auto dimensions = extent.value().maximum - extent.value().minimum;
  auto radius = complete
                    ? extent.value().maximum_radius
                    : 0.5 * std::max({dimensions.x, dimensions.y,
                                      dimensions.z});
  radius += buffer;
  // PyMOL applies a MAX_VDW floor for degenerate/small selections. Keep an
  // explicit molecular-scale floor until atom-radius-aware framing lands.
  radius = std::max(radius, 2.0);
  const auto framed = camera().frame_sphere(extent.value().center, radius, 1.0);
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
  extent.center = (extent.minimum + extent.maximum) * 0.5;
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
  const auto framed = oriented.value().frame_box(oriented_center, half_extents);
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

  scene::CameraParameters parameters;
  // Preserve the live viewport shape, but reset every other camera field to
  // the application defaults before framing visible data.
  parameters.aspect_ratio = camera().parameters().aspect_ratio;
  operation::Result<scene::Camera> reset = scene::Camera::create(parameters);
  if (!reset.has_value())
    return operation::Result<CameraResetResult>::failure(reset.error());
  std::optional<SpatialExtent> result_extent;
  if (!empty) {
    extent.center = (extent.minimum + extent.maximum) * 0.5;
    extent.maximum_radius =
        scene::length((extent.maximum - extent.minimum) * 0.5);
    const auto radius = std::max(extent.maximum_radius, 2.0);
    parameters.model_origin = extent.center;
    reset = scene::Camera::create(parameters);
    if (!reset.has_value())
      return operation::Result<CameraResetResult>::failure(reset.error());
    reset = reset.value().frame_sphere(extent.center, radius, 1.0);
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
  const auto turned = camera().turn_axis_degrees(axis, angle_degrees);
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
    const auto parsed = selection::Expression::parse(expression);
    if (!parsed.has_value()) {
      return operation::Result<model::AtomIndex>::failure(parsed.error());
    }
    const auto mask = selection::evaluate(
        parsed.value(), *object->system->topology(),
        [&](std::string_view name) {
          return object->selections.evaluate(name, *object->system->topology());
        });
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
                             "atom distance endpoint selects multiple atoms",
                             "use an expression selecting exactly one atom"});
      }
      selected = index;
    }
    if (!selected.has_value()) {
      return operation::Result<model::AtomIndex>::failure(
          operation::Error{operation::ErrorCode::invalid_selection,
                           "atom distance endpoint selects no atoms",
                           "use an expression selecting exactly one atom"});
    }
    return operation::Result<model::AtomIndex>::success(
        model::AtomIndex{selected.value()});
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
      {object->id, std::move(selection_expression), parameters,
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
    std::optional<std::string> h5md_particle_group) {
  auto *object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryLoadResult>::failure(missing_active());
  }
  std::optional<double> amber_box_angle;
  const auto &source_metadata = object->system->topology()->source_metadata();
  const auto angle = source_metadata.find("amber.box_angle_degrees");
  if (angle != source_metadata.end()) {
    double value{};
    const auto parsed =
        std::from_chars(angle->second.data(),
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
      amber_box_angle, coordinate_unit, std::move(h5md_particle_group)};
  const auto opened = io::open_trajectory(path, format, open_context);
  if (!opened.has_value()) {
    return operation::Result<TrajectoryLoadResult>::failure(opened.error());
  }
  const auto cache =
      trajectory::FrameCache::create(opened.value().source, cache_budget_bytes);
  if (!cache.has_value()) {
    return operation::Result<TrajectoryLoadResult>::failure(cache.error());
  }
  const auto count = cache.value()->frame_count();
  if (!count.has_value() || count.value() == 0U) {
    return operation::Result<TrajectoryLoadResult>::failure(
        invalid("trajectory attachment requires a known non-zero frame count"));
  }
  auto timeline = trajectory::PlaybackTimeline::create(count.value());
  if (!timeline.has_value()) {
    return operation::Result<TrajectoryLoadResult>::failure(timeline.error());
  }
  const auto first_frame = cache.value()->read_frame(0U);
  if (!first_frame.has_value()) {
    return operation::Result<TrajectoryLoadResult>::failure(
        first_frame.error());
  }
  const auto system =
      model::MolecularSystem::create(object->id, object->system->name(),
                                     object->system->topology(), cache.value());
  if (!system.has_value()) {
    return operation::Result<TrajectoryLoadResult>::failure(system.error());
  }
  const auto rebuilt = rebuild_representations(*object, system.value(),
                                               *first_frame.value(), 0U);
  if (!rebuilt.has_value()) {
    return operation::Result<TrajectoryLoadResult>::failure(rebuilt.error());
  }
  auto scene_builder = scene::SceneBuilder::from(*scene_);
  if (const auto error =
          scene_builder.replace_system(object->scene_node, system.value());
      error.has_value()) {
    return operation::Result<TrajectoryLoadResult>::failure(*error);
  }
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value()) {
    return operation::Result<TrajectoryLoadResult>::failure(next_scene.error());
  }
  auto clock = trajectory::PlaybackClock::create();
  if (!clock.has_value()) {
    return operation::Result<TrajectoryLoadResult>::failure(clock.error());
  }
  auto prefetch = trajectory::PrefetchScheduler::create(cache.value());
  if (!prefetch.has_value()) {
    return operation::Result<TrajectoryLoadResult>::failure(prefetch.error());
  }
  object->system = system.value();
  object->representations = std::move(rebuilt.value());
  object->trajectory = TrajectoryState{path,
                                       opened.value().format,
                                       cache.value(),
                                       std::move(timeline.value()),
                                       std::move(clock.value()),
                                       prefetch.value(),
                                       prefetch_frame_count};
  scene_ = next_scene.value();
  const auto prefetch_snapshot = schedule_prefetch(*object->trajectory);
  return operation::Result<TrajectoryLoadResult>::success(
      {object->id, opened.value().format, opened.value().source->atom_count(),
       count.value(), cache_budget_bytes, 0U, prefetch_frame_count,
       prefetch_snapshot});
}

operation::Result<TrajectoryFrameResult>
Workspace::set_trajectory_frame(std::size_t frame_index) {
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
  if (const auto error = next_timeline.seek(frame_index); error.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(*error);
  }
  const auto frame = object->trajectory->cache->read_frame(frame_index);
  if (!frame.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(frame.error());
  }
  const auto rebuilt = rebuild_representations(*object, object->system,
                                               *frame.value(), frame_index);
  if (!rebuilt.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(rebuilt.error());
  }
  object->trajectory->timeline = std::move(next_timeline);
  object->trajectory->clock.reset();
  object->representations = std::move(rebuilt.value());
  const auto prefetch = schedule_prefetch(*object->trajectory);
  return operation::Result<TrajectoryFrameResult>::success(frame_result(
      object->id, object->trajectory->timeline, object->trajectory->clock,
      *frame.value(), object->representations.size(), 0U, 0U, false, prefetch));
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
        *object, object->system, *frame.value(), advanced.snapshot.frame);
    if (!candidate.has_value()) {
      return operation::Result<TrajectoryFrameResult>::failure(
          candidate.error());
    }
    rebuilt = std::move(candidate.value());
  }
  object->trajectory->timeline = std::move(next_timeline);
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
  const auto frame = object->trajectory->cache->read_frame(frame_index);
  if (!frame.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(frame.error());
  }
  const auto rebuilt = rebuild_representations(*object, object->system,
                                               *frame.value(), frame_index);
  if (!rebuilt.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(rebuilt.error());
  }
  object->trajectory->timeline = std::move(timeline.value());
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
  if (advanced.snapshot.frame !=
      object->trajectory->timeline.snapshot().frame) {
    const auto candidate = rebuild_representations(
        *object, object->system, *frame.value(), advanced.snapshot.frame);
    if (!candidate.has_value()) {
      return operation::Result<TrajectoryFrameResult>::failure(
          candidate.error());
    }
    rebuilt = std::move(candidate.value());
  }
  object->trajectory->timeline = std::move(timeline);
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
