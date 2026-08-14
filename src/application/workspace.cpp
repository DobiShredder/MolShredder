#include "molshredder/application/workspace.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "molshredder/operation/error.hpp"
#include "molshredder/selection/evaluator.hpp"
#include "molshredder/selection/expression.hpp"

namespace molshredder::application {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

operation::Error missing_active() {
  return operation::Error{operation::ErrorCode::not_found,
                          "workspace has no active molecular object",
                          "load a structure first"};
}

render::AtomVisual default_visual(const model::AtomRecord& atom) {
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

operation::Result<std::vector<RepresentationRecord>> rebuild_representations(
    const WorkspaceObject& object,
    const std::shared_ptr<const model::MolecularSystem>& system,
    const model::CoordinateFrame& frame, std::size_t frame_index) {
  std::vector<render::AtomVisual> visuals;
  visuals.reserve(system->topology()->atom_count());
  for (const auto& atom : system->topology()->atoms()) {
    visuals.push_back(default_visual(atom));
  }
  std::vector<RepresentationRecord> rebuilt;
  rebuilt.reserve(object.representations.size());
  for (const auto& representation : object.representations) {
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
    render::RepresentationRequest request{
        system->topology().get(), &frame, frame_index, object.scene_node.value,
        visuals, mask.value(), {}, {}};
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
    std::uint64_t object_id, const trajectory::PlaybackTimeline& timeline,
    const trajectory::PlaybackClock& clock,
    const model::CoordinateFrame& frame, std::size_t rebuilt_count,
    std::size_t transitions = 0U, std::size_t boundary_crossings = 0U,
    bool catch_up_limited = false,
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

trajectory::PrefetchSnapshot schedule_prefetch(TrajectoryState& state) {
  static_cast<void>(state.prefetch->schedule(
      state.timeline.prefetch_hint(state.prefetch_frame_count)));
  return state.prefetch->snapshot();
}

operation::Result<std::vector<std::uint8_t>> selection_mask(
    const WorkspaceObject& object, std::string_view expression) {
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

operation::Result<model::AtomIndex> one_atom(
    const WorkspaceObject& object, std::string_view expression) {
  const auto mask = selection_mask(object, expression);
  if (!mask.has_value()) {
    return operation::Result<model::AtomIndex>::failure(mask.error());
  }
  std::optional<std::size_t> selected;
  for (std::size_t index = 0; index < mask.value().size(); ++index) {
    if (mask.value()[index] == 0U) continue;
    if (selected.has_value()) {
      return operation::Result<model::AtomIndex>::failure(operation::Error{
          operation::ErrorCode::invalid_selection,
          "atom distance endpoint selects multiple atoms",
          "use an expression selecting exactly one atom"});
    }
    selected = index;
  }
  if (!selected.has_value()) {
    return operation::Result<model::AtomIndex>::failure(operation::Error{
        operation::ErrorCode::invalid_selection,
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

operation::Result<ResolvedWeights> resolve_weights(
    const model::Topology& topology, analysis::WeightMode mode) {
  if (mode == analysis::WeightMode::uniform) {
    return operation::Result<ResolvedWeights>::success(
        ResolvedWeights{{}, {analysis::WeightMode::uniform, "uniform", {},
                             false}});
  }
  auto masses = analysis::masses_from_property(topology);
  if (!masses.has_value() &&
      masses.error().code == operation::ErrorCode::not_found) {
    masses = analysis::estimated_element_masses(topology);
  }
  if (!masses.has_value()) {
    return operation::Result<ResolvedWeights>::failure(masses.error());
  }
  return operation::Result<ResolvedWeights>::success(ResolvedWeights{
      std::move(masses.value().values),
      {analysis::WeightMode::mass, masses.value().source,
       masses.value().unit, masses.value().estimated}});
}

}  // namespace

Workspace::Workspace() {
  const auto built = scene::SceneBuilder{}.build();
  if (!built.has_value()) std::terminate();
  scene_ = built.value();
}

WorkspaceObject* Workspace::mutable_active_object() noexcept {
  return active_index_.has_value() ? &objects_[active_index_.value()] : nullptr;
}

const WorkspaceObject* Workspace::active_object() const noexcept {
  return active_index_.has_value() ? &objects_[active_index_.value()] : nullptr;
}

const WorkspaceObject* Workspace::object_by_scene_node(
    std::uint64_t scene_node_id) const noexcept {
  const auto found = std::find_if(objects_.begin(), objects_.end(),
                                  [scene_node_id](const auto& object) {
                                    return object.scene_node.value ==
                                           scene_node_id;
                                  });
  return found == objects_.end() ? nullptr : &*found;
}

std::vector<WorkspaceObjectInfo> Workspace::list_objects() const {
  std::vector<WorkspaceObjectInfo> result;
  result.reserve(objects_.size());
  for (std::size_t index = 0; index < objects_.size(); ++index) {
    const auto& object = objects_[index];
    const auto* node = scene_->find(object.scene_node);
    const auto frame_count = object.trajectory.has_value()
                                 ? object.trajectory->cache->frame_count()
                                 : object.system->coordinates()->frame_count();
    result.push_back(WorkspaceObjectInfo{
        object.id,
        object.scene_node.value,
        object.system->name(),
        object.system->topology()->atom_count(),
        object.representations.size(),
        frame_count,
        active_index_.has_value() && *active_index_ == index,
        node != nullptr && node->visible(),
        scene_->effectively_visible(object.scene_node),
        object.trajectory.has_value()});
  }
  return result;
}

operation::Result<WorkspaceObjectInfo> Workspace::activate_object(
    std::uint64_t object_id) {
  const auto found = std::find_if(objects_.begin(), objects_.end(),
                                  [object_id](const auto& object) {
                                    return object.id == object_id;
                                  });
  if (found == objects_.end()) {
    return operation::Result<WorkspaceObjectInfo>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "workspace object does not exist: " + std::to_string(object_id),
        "use object list to obtain a current object ID"});
  }
  const auto index = static_cast<std::size_t>(found - objects_.begin());
  auto scene_builder = scene::SceneBuilder::from(*scene_);
  if (const auto error =
          scene_builder.set_selection({found->scene_node});
      error.has_value()) {
    return operation::Result<WorkspaceObjectInfo>::failure(*error);
  }
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value()) {
    return operation::Result<WorkspaceObjectInfo>::failure(next_scene.error());
  }
  active_index_ = index;
  scene_ = next_scene.value();
  return operation::Result<WorkspaceObjectInfo>::success(
      list_objects()[index]);
}

operation::Result<WorkspaceObjectInfo> Workspace::set_object_visibility(
    std::uint64_t object_id, bool visible) {
  const auto found = std::find_if(objects_.begin(), objects_.end(),
                                  [object_id](const auto& object) {
                                    return object.id == object_id;
                                  });
  if (found == objects_.end()) {
    return operation::Result<WorkspaceObjectInfo>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "workspace object does not exist: " + std::to_string(object_id),
        "use object list to obtain a current object ID"});
  }
  const auto index = static_cast<std::size_t>(found - objects_.begin());
  auto scene_builder = scene::SceneBuilder::from(*scene_);
  if (const auto error =
          scene_builder.set_visible(found->scene_node, visible);
      error.has_value()) {
    return operation::Result<WorkspaceObjectInfo>::failure(*error);
  }
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value()) {
    return operation::Result<WorkspaceObjectInfo>::failure(next_scene.error());
  }
  scene_ = next_scene.value();
  return operation::Result<WorkspaceObjectInfo>::success(
      list_objects()[index]);
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
Workspace::active_frame(const WorkspaceObject& object) const {
  const auto index = object.trajectory.has_value()
                         ? object.trajectory->timeline.snapshot().frame
                         : 0U;
  return object.system->coordinates()->read_frame(index);
}

operation::Result<LoadResult> Workspace::load_structure(
    const std::filesystem::path& path, std::optional<std::string> name,
    io::StructureFormat format) {
  const auto document = io::read_structure_file(path, format);
  if (!document.has_value()) {
    return operation::Result<LoadResult>::failure(document.error());
  }
  if (document.value().structures.size() != 1U) {
    return operation::Result<LoadResult>::failure(invalid(
        "foundation load requires exactly one structure data block",
        "split a multi-block file before loading"));
  }
  const auto& data = document.value().structures.front();
  auto object_name = name.has_value() ? std::move(name.value()) : data.name;
  if (object_name.empty()) object_name = path.stem().string();
  if (object_name.empty()) {
    return operation::Result<LoadResult>::failure(
        invalid("loaded object requires a non-empty name"));
  }
  if (std::any_of(objects_.begin(), objects_.end(), [&](const auto& object) {
        return object.system->name() == object_name;
      })) {
    return operation::Result<LoadResult>::failure(
        invalid("workspace object name already exists: " + object_name,
                "choose a unique --name"));
  }
  const auto object_id = next_object_id_;
  const auto system = model::MolecularSystem::create(
      object_id, object_name, data.topology, data.coordinates);
  if (!system.has_value()) {
    return operation::Result<LoadResult>::failure(system.error());
  }
  auto scene_builder = scene::SceneBuilder::from(*scene_);
  const auto node = scene_builder.add_system(scene_->root(), object_name,
                                             system.value());
  if (!node.has_value()) {
    return operation::Result<LoadResult>::failure(node.error());
  }
  if (const auto error = scene_builder.set_selection({node.value()});
      error.has_value()) {
    return operation::Result<LoadResult>::failure(*error);
  }
  const auto next_scene = scene_builder.build();
  if (!next_scene.has_value()) {
    return operation::Result<LoadResult>::failure(next_scene.error());
  }
  ++next_object_id_;
  objects_.push_back(
      WorkspaceObject{object_id, node.value(), system.value(), {}, {}, {}});
  active_index_ = objects_.size() - 1U;
  scene_ = next_scene.value();
  return operation::Result<LoadResult>::success(
      LoadResult{object_id, object_name, data.topology->atom_count(),
                 data.coordinates->frame_count().value_or(0U),
                 document.value().format});
}

std::optional<operation::Error> Workspace::set_named_selection(
    std::string name, std::string expression, bool dynamic) {
  auto* object = mutable_active_object();
  if (object == nullptr) return missing_active();
  const auto parsed = selection::Expression::parse(expression);
  if (!parsed.has_value()) return parsed.error();
  return object->selections.set(std::move(name), parsed.value(), dynamic,
                                *object->system->topology());
}

operation::Result<ShowResult> Workspace::show(
    render::RepresentationKind kind, std::string selection_expression,
    bool replace_existing) {
  auto* object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<ShowResult>::failure(missing_active());
  }
  const auto parsed = selection::Expression::parse(selection_expression);
  if (!parsed.has_value()) {
    return operation::Result<ShowResult>::failure(parsed.error());
  }
  const auto mask = selection::evaluate(
      parsed.value(), *object->system->topology(),
      [&](std::string_view name) {
        return object->selections.evaluate(name, *object->system->topology());
      });
  if (!mask.has_value()) {
    return operation::Result<ShowResult>::failure(mask.error());
  }
  const auto frame = active_frame(*object);
  if (!frame.has_value()) {
    return operation::Result<ShowResult>::failure(frame.error());
  }
  std::vector<render::AtomVisual> visuals;
  visuals.reserve(object->system->topology()->atom_count());
  for (const auto& atom : object->system->topology()->atoms()) {
    visuals.push_back(default_visual(atom));
  }
  const auto frame_index = object->trajectory.has_value()
                               ? object->trajectory->timeline.snapshot().frame
                               : 0U;
  render::RepresentationRequest request{
      object->system->topology().get(), frame.value().get(), frame_index,
      object->scene_node.value, visuals, mask.value(), {}, {}};
  request.style.kind = kind;
  const auto packet = render::build_representation(request);
  if (!packet.has_value()) {
    return operation::Result<ShowResult>::failure(packet.error());
  }
  const auto selected_count = static_cast<std::size_t>(
      std::count(mask.value().begin(), mask.value().end(), std::uint8_t{1U}));
  const auto primitive_count = packet.value().lines.size() +
                               packet.value().cylinders.size() +
                               packet.value().spheres.size() +
                               packet.value().mesh_triangles.size();
  if (replace_existing) object->representations.clear();
  const auto index = object->representations.size();
  object->representations.push_back(
      RepresentationRecord{kind, std::move(selection_expression),
                           packet.value()});
  return operation::Result<ShowResult>::success(
      ShowResult{object->id, index, primitive_count, selected_count});
}

operation::Result<CenterAnalysisResult> Workspace::analyze_center(
    std::string selection_expression, analysis::CenterMode mode) {
  auto* object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<CenterAnalysisResult>::failure(missing_active());
  }
  const auto parsed = selection::Expression::parse(selection_expression);
  if (!parsed.has_value()) {
    return operation::Result<CenterAnalysisResult>::failure(parsed.error());
  }
  const auto mask = selection::evaluate(
      parsed.value(), *object->system->topology(),
      [&](std::string_view name) {
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
      resolved = analysis::estimated_element_masses(*object->system->topology());
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
  return operation::Result<CenterAnalysisResult>::success(
      CenterAnalysisResult{object->id, std::move(selection_expression), mode,
                           center.value()});
}

operation::Result<DistanceMeasurementRecord> Workspace::measure_distance(
    std::string from_expression, std::string to_expression,
    analysis::DistanceBoundary boundary) {
  auto* object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<DistanceMeasurementRecord>::failure(
        missing_active());
  }
  const auto frame = active_frame(*object);
  if (!frame.has_value()) {
    return operation::Result<DistanceMeasurementRecord>::failure(frame.error());
  }
  const auto resolve_one = [&](std::string_view expression)
      -> operation::Result<model::AtomIndex> {
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
      if (mask.value()[index] == 0U) continue;
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
    return operation::Result<DistanceMeasurementRecord>::failure(second.error());
  }
  const auto distance = analysis::atom_distance(
      *frame.value(), first.value(), second.value(), boundary);
  if (!distance.has_value()) {
    return operation::Result<DistanceMeasurementRecord>::failure(
        distance.error());
  }
  DistanceMeasurementRecord record{next_measurement_id_, object->id,
                                   std::move(from_expression),
                                   std::move(to_expression), boundary,
                                   distance.value()};
  ++next_measurement_id_;
  measurements_.push_back(record);
  return operation::Result<DistanceMeasurementRecord>::success(
      std::move(record));
}

operation::Result<ContactAnalysisResult> Workspace::analyze_contacts(
    std::string first_expression, std::string second_expression, double cutoff,
    analysis::DistanceBoundary boundary, bool same_selection,
    bool exclude_bonded, operation::LengthUnit cutoff_unit) {
  auto* object = mutable_active_object();
  if (object == nullptr)
    return operation::Result<ContactAnalysisResult>::failure(missing_active());
  const auto frame = active_frame(*object);
  if (!frame.has_value())
    return operation::Result<ContactAnalysisResult>::failure(frame.error());
  const auto first = selection_mask(*object, first_expression);
  if (!first.has_value())
    return operation::Result<ContactAnalysisResult>::failure(first.error());
  const auto second = same_selection ? first : selection_mask(*object, second_expression);
  if (!second.has_value())
    return operation::Result<ContactAnalysisResult>::failure(second.error());
  if (cutoff_unit != frame.value()->metadata().coordinate_unit)
    cutoff *= cutoff_unit == operation::LengthUnit::angstrom ? 0.1 : 10.0;
  const auto contacts = analysis::find_contacts(
      {*frame.value(), first.value(), second.value(),
       object->system->topology()->bonds(), cutoff, boundary, same_selection,
       exclude_bonded});
  if (!contacts.has_value())
    return operation::Result<ContactAnalysisResult>::failure(contacts.error());
  return operation::Result<ContactAnalysisResult>::success(
      {object->id, std::move(first_expression), std::move(second_expression),
       cutoff, boundary, contacts.value()});
}

operation::Result<HydrogenBondAnalysisResult>
Workspace::analyze_hydrogen_bonds(
    std::string donor_expression, std::string acceptor_expression,
    double distance_cutoff, double maximum_angle_deviation_degrees,
    analysis::DistanceBoundary boundary, bool same_selection,
    operation::LengthUnit cutoff_unit) {
  auto* object=mutable_active_object();
  if(object==nullptr)
    return operation::Result<HydrogenBondAnalysisResult>::failure(missing_active());
  const auto frame=active_frame(*object);
  if(!frame.has_value())
    return operation::Result<HydrogenBondAnalysisResult>::failure(frame.error());
  const auto donors=selection_mask(*object,donor_expression);
  if(!donors.has_value())
    return operation::Result<HydrogenBondAnalysisResult>::failure(donors.error());
  const auto acceptors=same_selection ? donors : selection_mask(*object,acceptor_expression);
  if(!acceptors.has_value())
    return operation::Result<HydrogenBondAnalysisResult>::failure(acceptors.error());
  const auto typing=analysis::resolve_hydrogen_bond_typing(*object->system->topology());
  if(!typing.has_value())
    return operation::Result<HydrogenBondAnalysisResult>::failure(typing.error());
  if(cutoff_unit!=frame.value()->metadata().coordinate_unit)
    distance_cutoff *= cutoff_unit==operation::LengthUnit::angstrom ? 0.1 : 10.0;
  const auto bonds=analysis::find_hydrogen_bonds(
      {*frame.value(),*object->system->topology(),donors.value(),acceptors.value(),
       typing.value().donors,typing.value().acceptors,distance_cutoff,
       maximum_angle_deviation_degrees,boundary,same_selection});
  if(!bonds.has_value())
    return operation::Result<HydrogenBondAnalysisResult>::failure(bonds.error());
  return operation::Result<HydrogenBondAnalysisResult>::success(
      {object->id,std::move(donor_expression),std::move(acceptor_expression),
       distance_cutoff,maximum_angle_deviation_degrees,boundary,
       {typing.value().donor_source,typing.value().acceptor_source,
        typing.value().estimated},bonds.value()});
}

operation::Result<SecondaryStructureAnalysisResult>
Workspace::analyze_secondary_structure(
    std::string selection_expression,
    analysis::SecondaryStructureParameters parameters) {
  auto* object=mutable_active_object();
  if(object==nullptr)
    return operation::Result<SecondaryStructureAnalysisResult>::failure(missing_active());
  const auto frame=active_frame(*object);
  if(!frame.has_value())
    return operation::Result<SecondaryStructureAnalysisResult>::failure(frame.error());
  const auto selected=selection_mask(*object,selection_expression);
  if(!selected.has_value())
    return operation::Result<SecondaryStructureAnalysisResult>::failure(selected.error());
  const auto assignment=analysis::assign_secondary_structure(
      *object->system->topology(),*frame.value(),parameters);
  if(!assignment.has_value())
    return operation::Result<SecondaryStructureAnalysisResult>::failure(assignment.error());
  std::vector<std::uint8_t> selected_residues(
      object->system->topology()->residue_count(),0U);
  for(std::size_t atom=0;atom<selected.value().size();++atom)
    if(selected.value()[atom])
      selected_residues[object->system->topology()->atoms()[atom].residue.value]=1U;
  return operation::Result<SecondaryStructureAnalysisResult>::success(
      {object->id,std::move(selection_expression),parameters,assignment.value(),
       std::move(selected_residues)});
}

operation::Result<CenterTimeSeriesResult>
Workspace::analyze_center_time_series(
    std::string selection_expression, analysis::CenterMode mode,
    analysis::SeriesRange range,
    analysis::MissingAtomPolicy missing_atom_policy,
    operation::TaskContext& context) {
  auto* object = mutable_active_object();
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
      resolved = analysis::estimated_element_masses(*object->system->topology());
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
  request.mass_unit = masses.unit.has_value()
                          ? std::string_view{*masses.unit}
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
Workspace::analyze_distance_time_series(
    std::string from_expression, std::string to_expression,
    analysis::DistanceBoundary boundary, analysis::SeriesRange range,
    operation::TaskContext& context) {
  auto* object = mutable_active_object();
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

operation::Result<RmsdTimeSeriesResult>
Workspace::analyze_rmsd_time_series(
    std::string selection_expression, std::string fit_selection_expression,
    std::size_t reference_frame, analysis::SeriesRange range,
    analysis::FitMode fit, analysis::WeightMode weight_mode,
    analysis::MissingAtomPolicy missing_atom_policy,
    operation::TaskContext& context) {
  auto* object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<RmsdTimeSeriesResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<RmsdTimeSeriesResult>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "active object has no attached trajectory",
        "attach one with traj load first"});
  }
  const auto selected = selection_mask(*object, selection_expression);
  if (!selected.has_value()) {
    return operation::Result<RmsdTimeSeriesResult>::failure(selected.error());
  }
  const auto fit_selected =
      selection_mask(*object, fit_selection_expression);
  if (!fit_selected.has_value()) {
    return operation::Result<RmsdTimeSeriesResult>::failure(
        fit_selected.error());
  }
  auto weights = resolve_weights(*object->system->topology(), weight_mode);
  if (!weights.has_value()) {
    return operation::Result<RmsdTimeSeriesResult>::failure(weights.error());
  }
  const auto rows = analysis::rmsd_series(
      analysis::RmsdSeriesRequest{
          object->trajectory->cache, reference_frame, range, selected.value(),
          fit_selected.value(), weights.value().values, fit,
          missing_atom_policy},
      context);
  if (!rows.has_value()) {
    return operation::Result<RmsdTimeSeriesResult>::failure(rows.error());
  }
  return operation::Result<RmsdTimeSeriesResult>::success(
      RmsdTimeSeriesResult{object->id, std::move(selection_expression),
                           std::move(fit_selection_expression), reference_frame,
                           range, fit, weights.value().provenance,
                           rows.value()});
}

operation::Result<RmsfTimeSeriesResult>
Workspace::analyze_rmsf_time_series(
    std::string selection_expression, std::string fit_selection_expression,
    std::size_t reference_frame, analysis::SeriesRange range,
    analysis::FitMode fit, analysis::WeightMode weight_mode,
    analysis::MissingAtomPolicy missing_atom_policy,
    operation::TaskContext& context) {
  auto* object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<RmsfTimeSeriesResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<RmsfTimeSeriesResult>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "active object has no attached trajectory",
        "attach one with traj load first"});
  }
  const auto selected = selection_mask(*object, selection_expression);
  if (!selected.has_value()) {
    return operation::Result<RmsfTimeSeriesResult>::failure(selected.error());
  }
  const auto fit_selected =
      selection_mask(*object, fit_selection_expression);
  if (!fit_selected.has_value()) {
    return operation::Result<RmsfTimeSeriesResult>::failure(
        fit_selected.error());
  }
  auto weights = resolve_weights(*object->system->topology(), weight_mode);
  if (!weights.has_value()) {
    return operation::Result<RmsfTimeSeriesResult>::failure(weights.error());
  }
  const auto series = analysis::rmsf_series(
      analysis::RmsfSeriesRequest{
          object->trajectory->cache, reference_frame, range, selected.value(),
          fit_selected.value(), weights.value().values, fit,
          missing_atom_policy},
      context);
  if (!series.has_value()) {
    return operation::Result<RmsfTimeSeriesResult>::failure(series.error());
  }
  return operation::Result<RmsfTimeSeriesResult>::success(
      RmsfTimeSeriesResult{object->id, std::move(selection_expression),
                           std::move(fit_selection_expression), reference_frame,
                           range, fit, weights.value().provenance,
                           series.value()});
}

operation::Result<ContactTimeSeriesResult> Workspace::analyze_contact_time_series(
    std::string first_expression, std::string second_expression, double cutoff,
    operation::LengthUnit cutoff_unit, analysis::DistanceBoundary boundary,
    bool same_selection, bool exclude_bonded, bool collect_occupancy,
    analysis::SeriesRange range,
    operation::TaskContext& context) {
  auto* object=mutable_active_object();
  if(object==nullptr) return operation::Result<ContactTimeSeriesResult>::failure(missing_active());
  if(!object->trajectory.has_value()) return operation::Result<ContactTimeSeriesResult>::failure(
      operation::Error{operation::ErrorCode::not_found,"active object has no attached trajectory",
                       "attach one with traj load first"});
  const auto first=selection_mask(*object,first_expression);
  if(!first.has_value()) return operation::Result<ContactTimeSeriesResult>::failure(first.error());
  const auto second=same_selection ? first : selection_mask(*object,second_expression);
  if(!second.has_value()) return operation::Result<ContactTimeSeriesResult>::failure(second.error());
  const auto series=analysis::contact_series({object->trajectory->cache,
      object->system->topology().get(),range,first.value(),second.value(),cutoff,
      cutoff_unit,boundary,same_selection,exclude_bonded,collect_occupancy},context);
  if(!series.has_value()) return operation::Result<ContactTimeSeriesResult>::failure(series.error());
  return operation::Result<ContactTimeSeriesResult>::success(
      {object->id,std::move(first_expression),std::move(second_expression),cutoff,
       boundary,range,series.value()});
}

operation::Result<HydrogenBondTimeSeriesResult>
Workspace::analyze_hydrogen_bond_time_series(
    std::string donor_expression, std::string acceptor_expression, double cutoff,
    operation::LengthUnit cutoff_unit, double maximum_angle_deviation_degrees,
    analysis::DistanceBoundary boundary, bool same_selection,
    bool collect_occupancy, analysis::SeriesRange range,
    operation::TaskContext& context) {
  auto* object=mutable_active_object();
  if(object==nullptr) return operation::Result<HydrogenBondTimeSeriesResult>::failure(missing_active());
  if(!object->trajectory.has_value()) return operation::Result<HydrogenBondTimeSeriesResult>::failure(
      operation::Error{operation::ErrorCode::not_found,"active object has no attached trajectory",
                       "attach one with traj load first"});
  const auto donors=selection_mask(*object,donor_expression);
  if(!donors.has_value()) return operation::Result<HydrogenBondTimeSeriesResult>::failure(donors.error());
  const auto acceptors=same_selection ? donors : selection_mask(*object,acceptor_expression);
  if(!acceptors.has_value()) return operation::Result<HydrogenBondTimeSeriesResult>::failure(acceptors.error());
  const auto typing=analysis::resolve_hydrogen_bond_typing(*object->system->topology());
  if(!typing.has_value()) return operation::Result<HydrogenBondTimeSeriesResult>::failure(typing.error());
  const auto series=analysis::hydrogen_bond_series({object->trajectory->cache,
      object->system->topology().get(),range,donors.value(),acceptors.value(),
      typing.value().donors,typing.value().acceptors,cutoff,cutoff_unit,
      maximum_angle_deviation_degrees,boundary,same_selection,collect_occupancy},context);
  if(!series.has_value()) return operation::Result<HydrogenBondTimeSeriesResult>::failure(series.error());
  return operation::Result<HydrogenBondTimeSeriesResult>::success(
      {object->id,std::move(donor_expression),std::move(acceptor_expression),cutoff,
       maximum_angle_deviation_degrees,boundary,range,
       {typing.value().donor_source,typing.value().acceptor_source,typing.value().estimated},
       series.value()});
}

operation::Result<TrajectoryLoadResult> Workspace::load_trajectory(
    const std::filesystem::path& path, io::TrajectoryFormat format,
    std::size_t cache_budget_bytes, std::size_t prefetch_frame_count) {
  auto* object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryLoadResult>::failure(missing_active());
  }
  const auto opened = io::open_trajectory(
      path, format, object->system->topology()->atom_count());
  if (!opened.has_value()) {
    return operation::Result<TrajectoryLoadResult>::failure(opened.error());
  }
  const auto cache = trajectory::FrameCache::create(opened.value().source,
                                                     cache_budget_bytes);
  if (!cache.has_value()) {
    return operation::Result<TrajectoryLoadResult>::failure(cache.error());
  }
  const auto count = cache.value()->frame_count();
  if (!count.has_value() || count.value() == 0U) {
    return operation::Result<TrajectoryLoadResult>::failure(invalid(
        "trajectory attachment requires a known non-zero frame count"));
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
  const auto system = model::MolecularSystem::create(
      object->id, object->system->name(), object->system->topology(),
      cache.value());
  if (!system.has_value()) {
    return operation::Result<TrajectoryLoadResult>::failure(system.error());
  }
  const auto rebuilt = rebuild_representations(
      *object, system.value(), *first_frame.value(), 0U);
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
    return operation::Result<TrajectoryLoadResult>::failure(
        next_scene.error());
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
  object->trajectory = TrajectoryState{
      path, opened.value().format, cache.value(),
      std::move(timeline.value()), std::move(clock.value()), prefetch.value(),
      prefetch_frame_count};
  scene_ = next_scene.value();
  const auto prefetch_snapshot = schedule_prefetch(*object->trajectory);
  return operation::Result<TrajectoryLoadResult>::success(
      {object->id, opened.value().format, opened.value().source->atom_count(),
       count.value(), cache_budget_bytes, 0U, prefetch_frame_count,
       prefetch_snapshot});
}

operation::Result<TrajectoryFrameResult> Workspace::set_trajectory_frame(
    std::size_t frame_index) {
  auto* object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryFrameResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "active object has no attached trajectory",
                         "run traj load first"});
  }
  auto next_timeline = object->trajectory->timeline;
  if (const auto error = next_timeline.seek(frame_index); error.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(*error);
  }
  const auto frame = object->trajectory->cache->read_frame(frame_index);
  if (!frame.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(frame.error());
  }
  const auto rebuilt = rebuild_representations(
      *object, object->system, *frame.value(), frame_index);
  if (!rebuilt.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(rebuilt.error());
  }
  object->trajectory->timeline = std::move(next_timeline);
  object->trajectory->clock.reset();
  object->representations = std::move(rebuilt.value());
  const auto prefetch = schedule_prefetch(*object->trajectory);
  return operation::Result<TrajectoryFrameResult>::success(frame_result(
      object->id, object->trajectory->timeline, object->trajectory->clock,
      *frame.value(),
      object->representations.size(), 0U, 0U, false, prefetch));
}

operation::Result<TrajectoryFrameResult> Workspace::play_trajectory(
    trajectory::PlaybackMode mode, trajectory::PlaybackDirection direction,
    std::size_t transitions) {
  auto* object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryFrameResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "active object has no attached trajectory",
                         "run traj load first"});
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
  if (!rebuilt.empty()) object->representations = std::move(rebuilt);
  const auto prefetch = schedule_prefetch(*object->trajectory);
  return operation::Result<TrajectoryFrameResult>::success(frame_result(
      object->id, object->trajectory->timeline, object->trajectory->clock,
      *frame.value(), rebuilt_count, advanced.transitions,
      advanced.boundary_crossings, false, prefetch));
}

operation::Result<TrajectoryFrameResult>
Workspace::configure_trajectory_range(
    trajectory::PlaybackRange range, trajectory::PlaybackMode mode,
    trajectory::PlaybackDirection direction) {
  auto* object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryFrameResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "active object has no attached trajectory",
                         "run traj load first"});
  }
  const auto count = object->trajectory->cache->frame_count();
  if (!count.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(invalid(
        "trajectory range requires a known frame count"));
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
  const auto rebuilt = rebuild_representations(
      *object, object->system, *frame.value(), frame_index);
  if (!rebuilt.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(rebuilt.error());
  }
  object->trajectory->timeline = std::move(timeline.value());
  object->trajectory->clock.reset();
  object->representations = std::move(rebuilt.value());
  const auto prefetch = schedule_prefetch(*object->trajectory);
  return operation::Result<TrajectoryFrameResult>::success(frame_result(
      object->id, object->trajectory->timeline, object->trajectory->clock,
      *frame.value(), object->representations.size(), 0U, 0U, false,
      prefetch));
}

operation::Result<TrajectoryFrameResult> Workspace::pause_trajectory() {
  auto* object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryFrameResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "active object has no attached trajectory",
                         "run traj load first"});
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
  return operation::Result<TrajectoryFrameResult>::success(frame_result(
      object->id, object->trajectory->timeline, object->trajectory->clock,
      *frame.value(), 0U, 0U, 0U, false,
      object->trajectory->prefetch->snapshot()));
}

operation::Result<TrajectoryFrameResult> Workspace::set_trajectory_speed(
    double frames_per_second) {
  auto* object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryFrameResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "active object has no attached trajectory",
                         "run traj load first"});
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
  return operation::Result<TrajectoryFrameResult>::success(frame_result(
      object->id, object->trajectory->timeline, object->trajectory->clock,
      *frame.value(), 0U, 0U, 0U, false,
      object->trajectory->prefetch->snapshot()));
}

operation::Result<TrajectoryFrameResult> Workspace::tick_trajectory(
    double elapsed_seconds) {
  auto* object = mutable_active_object();
  if (object == nullptr) {
    return operation::Result<TrajectoryFrameResult>::failure(missing_active());
  }
  if (!object->trajectory.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "active object has no attached trajectory",
                         "run traj load first"});
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
    if (!advanced.snapshot.playing) clock.reset();
  } else if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0) {
    return operation::Result<TrajectoryFrameResult>::failure(invalid(
        "playback elapsed time must be finite and non-negative"));
  }
  const auto frame = object->trajectory->cache->read_frame(
      advanced.snapshot.frame);
  if (!frame.has_value()) {
    return operation::Result<TrajectoryFrameResult>::failure(frame.error());
  }
  std::vector<RepresentationRecord> rebuilt;
  if (advanced.snapshot.frame != object->trajectory->timeline.snapshot().frame) {
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
  if (!rebuilt.empty()) object->representations = std::move(rebuilt);
  const auto prefetch = object->trajectory->timeline.snapshot().playing
                            ? schedule_prefetch(*object->trajectory)
                            : (object->trajectory->prefetch->cancel(),
                               object->trajectory->prefetch->snapshot());
  return operation::Result<TrajectoryFrameResult>::success(frame_result(
      object->id, object->trajectory->timeline, object->trajectory->clock,
      *frame.value(), rebuilt_count,
      advanced.transitions, advanced.boundary_crossings,
      clock_advance.catch_up_limited, prefetch));
}

}  // namespace molshredder::application
