#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/analysis/basic.hpp"
#include "molshredder/analysis/contacts.hpp"
#include "molshredder/analysis/principal_axes.hpp"
#include "molshredder/analysis/secondary_structure.hpp"
#include "molshredder/analysis/time_series.hpp"
#include "molshredder/application/analysis_result.hpp"
#include "molshredder/application/representation_visibility.hpp"
#include "molshredder/io/structure_reader.hpp"
#include "molshredder/io/structure_writer.hpp"
#include "molshredder/io/trajectory_reader.hpp"
#include "molshredder/io/trajectory_writer.hpp"
#include "molshredder/io/volume_reader.hpp"
#include "molshredder/io/volume_writer.hpp"
#include "molshredder/model/molecular_system.hpp"
#include "molshredder/model/volume.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/render/packet.hpp"
#include "molshredder/render/representation.hpp"
#include "molshredder/render/setting_store.hpp"
#include "molshredder/render/volume_isosurface.hpp"
#include "molshredder/scene/scene.hpp"
#include "molshredder/scene/camera.hpp"
#include "molshredder/scene/stereo.hpp"
#include "molshredder/selection/named_selection.hpp"
#include "molshredder/trajectory/frame_cache.hpp"
#include "molshredder/trajectory/attachment.hpp"
#include "molshredder/trajectory/playback.hpp"
#include "molshredder/trajectory/prefetch_scheduler.hpp"

namespace molshredder::application {

struct RepresentationRecord {
  render::RepresentationKind kind{render::RepresentationKind::lines};
  std::string selection_expression;
  render::RenderPacket packet;
};

struct TrajectoryState {
  std::filesystem::path path;
  io::TrajectoryFormat format{io::TrajectoryFormat::auto_detect};
  std::shared_ptr<trajectory::FrameCache> cache;
  trajectory::PlaybackTimeline timeline;
  trajectory::PlaybackClock clock;
  std::shared_ptr<trajectory::PrefetchScheduler> prefetch;
  std::size_t prefetch_frame_count{4U};
  trajectory::AtomMappingReport mapping;
  trajectory::TrajectorySemanticReport semantics;
};

struct WorkspaceObject {
  std::uint64_t id{};
  scene::NodeId scene_node;
  std::shared_ptr<const model::MolecularSystem> system;
  selection::NamedSelections selections;
  RepresentationVisibilityState representation_visibility;
  std::vector<RepresentationRecord> representations;
  std::optional<TrajectoryState> trajectory;
};

struct WorkspaceVolume {
  std::uint64_t id{};
  scene::NodeId scene_node;
  std::string name;
  std::filesystem::path path;
  io::VolumeFormat format{io::VolumeFormat::auto_detect};
  std::shared_ptr<const model::VolumeGrid> grid;
  std::vector<render::RenderPacket> representations;
};

struct VolumeIsosurfaceResult {
  std::uint64_t object_id{};
  std::size_t representation_index{};
  double level{};
  render::ColorRgba color;
  std::size_t vertex_count{};
  std::size_t triangle_count{};
  render::Bounds3d bounds;
};

struct VolumeLoadResult {
  std::uint64_t object_id{};
  std::string object_name;
  io::VolumeFormat format{io::VolumeFormat::auto_detect};
  model::VolumeShape shape;
  std::size_t value_count{};
  model::VolumePrecision precision{model::VolumePrecision::float64};
  model::Vec3d origin;
  std::array<model::Vec3d, 3U> deltas;
  double minimum{};
  double maximum{};
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
};

struct WorkspaceVolumeInfo {
  std::uint64_t id{};
  std::uint64_t scene_node_id{};
  std::string name;
  model::VolumeShape shape;
  std::size_t value_count{};
  model::VolumePrecision precision{model::VolumePrecision::float64};
  double minimum{};
  double maximum{};
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::size_t representation_count{};
  bool active{};
  bool visible{true};
  bool effectively_visible{true};
};

struct SpatialExtent {
  model::Vec3d minimum;
  model::Vec3d maximum;
  model::Vec3d center;
  double maximum_radius{};
  std::size_t selected_atom_count{};
  std::size_t used_atom_count{};
  std::size_t skipped_missing_atom_count{};
  std::size_t evaluated_frame_count{};
};

enum class CameraStateScopeKind { current, all, explicit_state };

struct CameraStateScope {
  CameraStateScopeKind kind{CameraStateScopeKind::current};
  // Zero-based internally. User-facing explicit states are one-based.
  std::size_t frame_index{};
};

struct CameraSelectionResult {
  std::uint64_t object_id{};
  std::string selection_expression;
  CameraStateScope state_scope;
  SpatialExtent extent;
  scene::Camera camera;
};

struct CameraOrientResult {
  std::uint64_t object_id{};
  std::string selection_expression;
  CameraStateScope state_scope;
  SpatialExtent extent;
  analysis::PrincipalAxesResult principal_axes;
  model::Vec3d oriented_center;
  model::Vec3d oriented_half_extents;
  scene::Camera camera;
};

struct ObjectOriginResult {
  std::uint64_t object_id{};
  std::string object_name;
  model::Vec3d position;
  std::optional<std::string> selection_expression;
  std::optional<CameraStateScope> state_scope;
  std::optional<SpatialExtent> extent;
  scene::Transform transform;
  std::uint64_t scene_version{};
};

struct ObjectTransformResetResult {
  std::string object_reference;
  std::vector<std::uint64_t> object_ids;
  std::uint64_t scene_version{};
};

struct CameraResetResult {
  scene::Camera camera;
  std::optional<SpatialExtent> extent;
  std::size_t molecular_object_count{};
  std::size_t volume_object_count{};
};

enum class CameraClipMode {
  near_relative,
  far_relative,
  move,
  slab,
  atoms,
  near_absolute,
  far_absolute,
};

struct CameraDepthExtent {
  SpatialExtent spatial;
  double minimum_depth{};
  double maximum_depth{};
};

struct CameraClipResult {
  CameraClipMode mode{CameraClipMode::near_relative};
  double distance{};
  std::optional<std::string> selection_expression;
  std::optional<CameraDepthExtent> extent;
  CameraStateScope state_scope;
  scene::Camera camera;
};

struct CameraNavigationResult {
  scene::CameraAxis axis{scene::CameraAxis::x};
  double amount{};
  scene::Camera camera;
};

struct CameraProjectionResult {
  scene::ProjectionMode previous_mode{scene::ProjectionMode::perspective};
  scene::ProjectionMode mode{scene::ProjectionMode::perspective};
  double previous_vertical_span{};
  double vertical_span{};
  double field_of_view_degrees{};
  bool preserve_scale{true};
  scene::Camera camera;
};

struct StereoConfigurationResult {
  scene::StereoParameters previous;
  scene::StereoParameters current;
};

struct TrajectoryLoadResult {
  std::uint64_t object_id{};
  io::TrajectoryFormat format{io::TrajectoryFormat::auto_detect};
  std::size_t atom_count{};
  std::size_t frame_count{};
  std::size_t cache_budget_bytes{};
  std::size_t current_frame{};
  std::size_t prefetch_frame_count{};
  trajectory::PrefetchSnapshot prefetch;
  trajectory::AtomMappingReport mapping;
  trajectory::TrajectorySemanticReport semantics;
};

struct TrajectoryLoadPlan {
  WorkspaceObject object;
  std::shared_ptr<const scene::Scene> scene;
  render::RenderSettingStore render_settings;
  render::RenderSettingSnapshot render_setting_snapshot;
  std::filesystem::path path;
  io::TrajectoryFormat format{io::TrajectoryFormat::auto_detect};
  std::size_t cache_budget_bytes{};
  std::size_t prefetch_frame_count{};
  std::optional<operation::LengthUnit> coordinate_unit;
  std::optional<std::string> h5md_particle_group;
  trajectory::AtomMappingPolicy mapping_policy{
      trajectory::AtomMappingPolicy::exact};
  std::vector<model::AtomId> source_to_target_atom_ids;
  std::optional<std::uint64_t> expected_topology_version;
};

struct TrajectoryLoadCandidate {
  TrajectoryLoadPlan plan;
  std::shared_ptr<const model::MolecularSystem> system;
  std::vector<RepresentationRecord> representations;
  TrajectoryState trajectory;
  std::shared_ptr<const scene::Scene> scene;
  TrajectoryLoadResult result;
};

struct TrajectoryFrameResult {
  std::uint64_t object_id{};
  trajectory::PlaybackSnapshot playback;
  std::optional<std::uint64_t> source_step;
  std::optional<double> physical_time;
  std::optional<std::string> physical_time_unit;
  std::size_t rebuilt_representation_count{};
  std::size_t transitions{};
  std::size_t boundary_crossings{};
  double frames_per_second{30.0};
  double pending_transitions{};
  bool catch_up_limited{};
  trajectory::PrefetchSnapshot prefetch;
};

// Immutable owner-thread snapshot used to decode and rebuild a seek candidate
// without touching live Workspace state from a worker.
struct TrajectoryFramePlan {
  WorkspaceObject object;
  render::RenderSettingStore render_settings;
  render::RenderSettingSnapshot render_setting_snapshot;
  std::size_t frame_index{};
};

struct TrajectoryFrameCandidate {
  TrajectoryFramePlan plan;
  std::shared_ptr<const model::CoordinateFrame> frame;
  std::vector<RepresentationRecord> representations;
};

struct LoadedObjectResult {
  std::uint64_t object_id{};
  std::string object_name;
  std::size_t atom_count{};
  std::size_t frame_count{};
  std::size_t source_record_index{};
};

struct LoadResult {
  std::uint64_t object_id{};
  std::string object_name;
  std::size_t atom_count{};
  std::size_t frame_count{};
  io::StructureFormat format{io::StructureFormat::auto_detect};
  std::vector<LoadedObjectResult> objects;
};

struct StructureLoadRequest {
  std::filesystem::path path;
  std::optional<std::string> name;
  io::StructureFormat format{io::StructureFormat::auto_detect};
};

struct StructureDocumentLoadRequest {
  io::StructureDocument document;
  std::filesystem::path source_path;
  std::optional<std::string> name;
};

struct BatchLoadResult {
  std::uint64_t object_id{};
  std::string object_name;
  std::size_t atom_count{};
  std::size_t frame_count{};
  std::size_t input_count{};
  std::vector<io::StructureFormat> formats;
  std::vector<LoadedObjectResult> objects;
};

struct SaveResult {
  std::uint64_t object_id{};
  std::filesystem::path path;
  io::StructureWriteReport report;
};

struct VolumeSaveResult {
  std::uint64_t object_id{};
  std::filesystem::path path;
  io::VolumeWriteReport report;
};

struct TrajectorySaveResult {
  std::uint64_t object_id{};
  std::size_t frame_index{};
  std::filesystem::path path;
  io::TrajectoryWriteReport report;
};

struct WorkspaceObjectInfo {
  std::uint64_t id{};
  std::uint64_t scene_node_id{};
  std::string name;
  std::size_t atom_count{};
  std::size_t representation_count{};
  std::optional<std::size_t> frame_count;
  bool active{};
  bool visible{true};
  bool effectively_visible{true};
  bool has_trajectory{};
};

struct ObjectLifecycleResult {
  std::uint64_t object_id{};
  std::uint64_t scene_node_id{};
  std::string name;
  std::size_t old_position{};
  std::size_t new_position{};
  std::size_t object_count{};
  bool deleted{};
  std::optional<std::uint64_t> active_object_id;
  std::vector<std::uint64_t> ordered_object_ids;
  std::size_t removed_measurement_count{};
  std::size_t removed_setting_override_count{};
};

struct TopologyMutationResult {
  std::uint64_t object_id{};
  std::uint64_t previous_version{};
  std::uint64_t topology_version{};
  std::size_t previous_atom_count{};
  std::size_t atom_count{};
  std::size_t removed_atom_count{};
  std::size_t removed_bond_count{};
  std::size_t invalidated_measurement_count{};
  std::size_t removed_setting_override_count{};
  std::vector<std::uint64_t> ordered_atom_ids;
};

struct ShowResult {
  std::uint64_t object_id{};
  std::size_t representation_index{};
  std::size_t primitive_count{};
  std::size_t selected_atom_count{};
};

struct RepresentationVisibilityResult {
  std::uint64_t object_id{};
  render::RepresentationKind kind{render::RepresentationKind::lines};
  RepresentationVisibilityMutation mutation{
      RepresentationVisibilityMutation::show};
  std::size_t affected_atom_count{};
  std::size_t visible_atom_count{};
  std::size_t representation_count{};
  std::vector<render::RepresentationKind> representation_kinds;
};

struct CenterAnalysisResult {
  std::uint64_t object_id{};
  std::string selection_expression;
  analysis::CenterMode mode{analysis::CenterMode::centroid};
  analysis::CenterResult center;
};

struct DistanceMeasurementRecord {
  std::uint64_t measurement_id{};
  std::uint64_t object_id{};
  std::string from_expression;
  std::string to_expression;
  analysis::DistanceBoundary boundary{analysis::DistanceBoundary::raw};
  analysis::AtomDistanceResult distance;
};

struct ContactAnalysisResult {
  std::uint64_t object_id{};
  std::string first_expression;
  std::string second_expression;
  double cutoff{};
  analysis::DistanceBoundary boundary{analysis::DistanceBoundary::raw};
  analysis::ContactResult contacts;
};

struct HydrogenBondTypingProvenance {
  std::string donor_source;
  std::string acceptor_source;
  bool estimated{};
};

struct HydrogenBondAnalysisResult {
  std::uint64_t object_id{};
  std::string donor_expression;
  std::string acceptor_expression;
  double distance_cutoff{};
  double maximum_angle_deviation_degrees{};
  analysis::DistanceBoundary boundary{analysis::DistanceBoundary::raw};
  HydrogenBondTypingProvenance typing;
  analysis::HydrogenBondResult hydrogen_bonds;
};

struct SecondaryStructureAnalysisResult {
  std::uint64_t object_id{};
  std::string selection_expression;
  analysis::SecondaryStructureParameters parameters;
  analysis::SecondaryStructureResult assignment;
  std::vector<std::uint8_t> selected_residues;
};

struct CenterTimeSeriesResult {
  std::uint64_t object_id{};
  std::string selection_expression;
  analysis::CenterMode mode{analysis::CenterMode::centroid};
  analysis::SeriesRange range;
  std::vector<analysis::CenterSeriesRow> rows;
};

struct DistanceTimeSeriesResult {
  std::uint64_t object_id{};
  std::string from_expression;
  std::string to_expression;
  analysis::DistanceBoundary boundary{analysis::DistanceBoundary::raw};
  analysis::SeriesRange range;
  std::vector<analysis::DistanceSeriesRow> rows;
};

struct AnalysisWeightProvenance {
  analysis::WeightMode mode{analysis::WeightMode::uniform};
  std::string source{"uniform"};
  std::optional<std::string> unit;
  bool estimated{};
};

struct RmsdTimeSeriesResult {
  std::uint64_t object_id{};
  std::string selection_expression;
  std::string fit_selection_expression;
  std::size_t reference_frame{};
  analysis::SeriesRange range;
  analysis::FitMode fit{analysis::FitMode::rigid};
  AnalysisWeightProvenance weights;
  std::vector<analysis::RmsdSeriesRow> rows;
};

struct RmsfTimeSeriesResult {
  std::uint64_t object_id{};
  std::string selection_expression;
  std::string fit_selection_expression;
  std::size_t reference_frame{};
  analysis::SeriesRange range;
  analysis::FitMode fit{analysis::FitMode::rigid};
  AnalysisWeightProvenance weights;
  analysis::RmsfSeriesResult series;
};

struct ContactTimeSeriesResult {
  std::uint64_t object_id{};
  std::string first_expression;
  std::string second_expression;
  double cutoff{};
  analysis::DistanceBoundary boundary{analysis::DistanceBoundary::raw};
  analysis::SeriesRange range;
  analysis::ContactSeriesResult series;
};

struct HydrogenBondTimeSeriesResult {
  std::uint64_t object_id{};
  std::string donor_expression;
  std::string acceptor_expression;
  double cutoff{};
  double maximum_angle_deviation_degrees{};
  analysis::DistanceBoundary boundary{analysis::DistanceBoundary::raw};
  analysis::SeriesRange range;
  HydrogenBondTypingProvenance typing;
  analysis::HydrogenBondSeriesResult series;
};

struct NamedViewRecord {
  std::string name;
  scene::CameraParameters camera;

  friend bool operator==(const NamedViewRecord&, const NamedViewRecord&) =
      default;
};

struct NamedViewStoreResult {
  NamedViewRecord view;
  std::size_t view_count{};
  bool replaced{};
};

struct NamedViewDeleteResult {
  std::string name;
  std::size_t view_count{};
  bool cleared_all{};
};

class Workspace {
public:
  Workspace();

  [[nodiscard]] operation::Result<LoadResult>
  load_structure(const std::filesystem::path &path,
                 std::optional<std::string> name, io::StructureFormat format);
  [[nodiscard]] operation::Result<LoadResult>
  load_structure_document(io::StructureDocument document,
                          const std::filesystem::path &source_path,
                          std::optional<std::string> name = std::nullopt);
  [[nodiscard]] operation::Result<BatchLoadResult>
  load_structure_batch(std::span<const StructureLoadRequest> requests,
                       operation::TaskContext &context);
  [[nodiscard]] operation::Result<BatchLoadResult>
  load_structure_documents(std::vector<StructureDocumentLoadRequest> requests,
                           operation::TaskContext &context);
  [[nodiscard]] operation::Result<VolumeLoadResult>
  load_volume(const std::filesystem::path &path,
              std::optional<std::string> name, io::VolumeFormat format,
              operation::LengthUnit coordinate_unit);
  [[nodiscard]] std::vector<WorkspaceVolumeInfo> list_volumes() const;
  [[nodiscard]] operation::Result<VolumeIsosurfaceResult>
  show_volume_isosurface(double level, render::ColorRgba color,
                         bool replace_existing,
                         operation::TaskContext &context);
  [[nodiscard]] operation::Result<VolumeSaveResult>
  save_active_volume(const std::filesystem::path &path,
                     io::VolumeFormat format, bool overwrite,
                     operation::TaskContext &context) const;
  [[nodiscard]] operation::Result<SaveResult>
  save_active_structure(const std::filesystem::path &path,
                        io::StructureFormat format, bool all_frames,
                        unsigned int decimal_places, std::string comment,
                        bool overwrite, operation::TaskContext &context) const;
  [[nodiscard]] std::vector<WorkspaceObjectInfo> list_objects() const;
  [[nodiscard]] operation::Result<WorkspaceObjectInfo>
  activate_object(std::uint64_t object_id);
  [[nodiscard]] operation::Result<WorkspaceObjectInfo>
  set_object_visibility(std::uint64_t object_id, bool visible);
  [[nodiscard]] operation::Result<ObjectLifecycleResult>
  rename_object(std::string_view object_reference, std::string name);
  [[nodiscard]] operation::Result<ObjectLifecycleResult>
  delete_object(std::string_view object_reference);
  [[nodiscard]] operation::Result<ObjectLifecycleResult>
  reorder_object(std::string_view object_reference,
                 std::size_t new_position);
  [[nodiscard]] operation::Result<TopologyMutationResult>
  retain_active_atoms(std::span<const model::AtomId> ordered_atom_ids,
                      std::uint64_t expected_topology_version);
  [[nodiscard]] std::optional<operation::Error>
  set_named_selection(std::string name, std::string expression, bool dynamic);
  [[nodiscard]] operation::Result<ShowResult>
  show(render::RepresentationKind kind, std::string selection_expression,
       bool replace_existing = false);
  [[nodiscard]] operation::Result<RepresentationVisibilityResult>
  mutate_representation_visibility(
      render::RepresentationKind kind, std::string selection_expression,
      RepresentationVisibilityMutation mutation);
  [[nodiscard]] operation::Result<RepresentationVisibilityResult>
  mutate_representation_visibility(
      std::span<const render::RepresentationKind> kinds,
      std::string selection_expression,
      RepresentationVisibilityMutation mutation);
  [[nodiscard]] operation::Result<RepresentationVisibilitySnapshot>
  representation_visibility_snapshot() const;
  [[nodiscard]] operation::Result<RepresentationVisibilitySessionSnapshot>
  representation_visibility_session_snapshot() const;
  [[nodiscard]] operation::Result<RepresentationVisibilityResult>
  restore_representation_visibility(
      const RepresentationVisibilitySnapshot &snapshot);
  [[nodiscard]] operation::Result<RepresentationVisibilityResult>
  restore_representation_visibility_session(
      const RepresentationVisibilitySessionSnapshot &snapshot);
  [[nodiscard]] operation::Result<CenterAnalysisResult>
  analyze_center(std::string selection_expression, analysis::CenterMode mode);
  [[nodiscard]] operation::Result<PersistentAnalysisResult>
  store_analysis_result(AnalysisResultDraft draft);
  [[nodiscard]] std::optional<operation::Error> validate_analysis_result_name(
      const std::optional<std::string> &name) const;
  [[nodiscard]] operation::Result<PersistentAnalysisResult>
  analysis_result(std::uint64_t result_id) const;
  [[nodiscard]] std::span<const PersistentAnalysisResult>
  analysis_results() const noexcept { return analysis_results_.records(); }
  [[nodiscard]] AnalysisSourceStatus
  analysis_source_status(const PersistentAnalysisResult &record) const noexcept;
  [[nodiscard]] operation::Result<PersistentAnalysisResult>
  delete_analysis_result(std::uint64_t result_id);
  [[nodiscard]] operation::Result<PersistentAnalysisResult>
  set_analysis_overlay_visible(std::uint64_t result_id, bool visible);
  [[nodiscard]] AnalysisResultStoreSnapshot analysis_result_snapshot() const;
  [[nodiscard]] std::optional<operation::Error>
  restore_analysis_results(const AnalysisResultStoreSnapshot &snapshot);
  [[nodiscard]] operation::Result<SpatialExtent>
  selection_extent(std::string_view selection_expression,
                   CameraStateScope state_scope = {},
                   operation::TaskContext *context = nullptr) const;
  [[nodiscard]] operation::Result<CameraDepthExtent>
  selection_camera_depth_extent(
      std::string_view selection_expression,
      CameraStateScope state_scope = {},
      operation::TaskContext *context = nullptr) const;
  [[nodiscard]] operation::Result<CameraSelectionResult>
  center_camera(std::string selection_expression, bool move_origin,
                CameraStateScope state_scope = {},
                operation::TaskContext *context = nullptr);
  [[nodiscard]] operation::Result<CameraSelectionResult>
  zoom_camera(std::string selection_expression, double buffer,
              bool complete, CameraStateScope state_scope = {},
              operation::TaskContext *context = nullptr);
  [[nodiscard]] operation::Result<CameraSelectionResult>
  set_camera_origin(std::string selection_expression,
                    CameraStateScope state_scope = {},
                    operation::TaskContext *context = nullptr);
  [[nodiscard]] operation::Result<scene::Camera>
  set_camera_origin(model::Vec3d position);
  [[nodiscard]] operation::Result<ObjectOriginResult>
  set_object_origin_from_selection(
      std::string object_reference, std::string selection_expression,
      CameraStateScope state_scope = {},
      operation::TaskContext *context = nullptr);
  [[nodiscard]] operation::Result<ObjectOriginResult>
  set_object_origin(std::string object_reference, model::Vec3d position);
  [[nodiscard]] operation::Result<ObjectTransformResetResult>
  reset_object_transforms(std::string object_reference);
  [[nodiscard]] operation::Result<CameraOrientResult>
  orient_camera(std::string selection_expression,
                CameraStateScope state_scope = {},
                operation::TaskContext *context = nullptr);
  [[nodiscard]] operation::Result<CameraResetResult> reset_camera();
  [[nodiscard]] operation::Result<CameraClipResult>
  clip_camera(CameraClipMode mode, double distance,
              std::optional<std::string> selection_expression = std::nullopt,
              CameraStateScope state_scope = {},
              operation::TaskContext *context = nullptr);
  [[nodiscard]] operation::Result<CameraNavigationResult>
  move_camera(scene::CameraAxis axis, double distance);
  [[nodiscard]] operation::Result<CameraNavigationResult>
  turn_camera(scene::CameraAxis axis, double angle_degrees);
  [[nodiscard]] operation::Result<CameraProjectionResult>
  set_camera_projection(
      scene::ProjectionMode mode,
      std::optional<double> field_of_view_degrees = std::nullopt,
      bool preserve_scale = true);
  [[nodiscard]] operation::Result<StereoConfigurationResult>
  set_stereo(scene::StereoParameters parameters);
  [[nodiscard]] operation::Result<DistanceMeasurementRecord> measure_distance(
      std::string from_expression, std::string to_expression,
      analysis::DistanceBoundary boundary = analysis::DistanceBoundary::raw);
  [[nodiscard]] operation::Result<DistanceAnalysisOverlay>
  distance_analysis_overlay(const DistanceMeasurementRecord &record,
                            std::string label) const;
  [[nodiscard]] operation::Result<ContactAnalysisResult> analyze_contacts(
      std::string first_expression, std::string second_expression,
      double cutoff, analysis::DistanceBoundary boundary, bool same_selection,
      bool exclude_bonded = true,
      operation::LengthUnit cutoff_unit = operation::LengthUnit::angstrom);
  [[nodiscard]] operation::Result<HydrogenBondAnalysisResult>
  analyze_hydrogen_bonds(
      std::string donor_expression, std::string acceptor_expression,
      double distance_cutoff, double maximum_angle_deviation_degrees,
      analysis::DistanceBoundary boundary, bool same_selection,
      operation::LengthUnit cutoff_unit = operation::LengthUnit::angstrom);
  [[nodiscard]] operation::Result<SecondaryStructureAnalysisResult>
  analyze_secondary_structure(
      std::string selection_expression,
      analysis::SecondaryStructureParameters parameters = {});
  [[nodiscard]] operation::Result<CenterTimeSeriesResult>
  analyze_center_time_series(std::string selection_expression,
                             analysis::CenterMode mode,
                             analysis::SeriesRange range,
                             analysis::MissingAtomPolicy missing_atom_policy,
                             operation::TaskContext &context);
  [[nodiscard]] operation::Result<DistanceTimeSeriesResult>
  analyze_distance_time_series(std::string from_expression,
                               std::string to_expression,
                               analysis::DistanceBoundary boundary,
                               analysis::SeriesRange range,
                               operation::TaskContext &context);
  [[nodiscard]] operation::Result<RmsdTimeSeriesResult>
  analyze_rmsd_time_series(std::string selection_expression,
                           std::string fit_selection_expression,
                           std::size_t reference_frame,
                           analysis::SeriesRange range, analysis::FitMode fit,
                           analysis::WeightMode weight_mode,
                           analysis::MissingAtomPolicy missing_atom_policy,
                           operation::TaskContext &context);
  [[nodiscard]] operation::Result<RmsfTimeSeriesResult>
  analyze_rmsf_time_series(std::string selection_expression,
                           std::string fit_selection_expression,
                           std::size_t reference_frame,
                           analysis::SeriesRange range, analysis::FitMode fit,
                           analysis::WeightMode weight_mode,
                           analysis::MissingAtomPolicy missing_atom_policy,
                           operation::TaskContext &context);
  [[nodiscard]] operation::Result<ContactTimeSeriesResult>
  analyze_contact_time_series(std::string first_expression,
                              std::string second_expression, double cutoff,
                              operation::LengthUnit cutoff_unit,
                              analysis::DistanceBoundary boundary,
                              bool same_selection, bool exclude_bonded,
                              bool collect_occupancy,
                              analysis::SeriesRange range,
                              operation::TaskContext &context);
  [[nodiscard]] operation::Result<HydrogenBondTimeSeriesResult>
  analyze_hydrogen_bond_time_series(std::string donor_expression,
                                    std::string acceptor_expression,
                                    double cutoff,
                                    operation::LengthUnit cutoff_unit,
                                    double maximum_angle_deviation_degrees,
                                    analysis::DistanceBoundary boundary,
                                    bool same_selection, bool collect_occupancy,
                                    analysis::SeriesRange range,
                                    operation::TaskContext &context);
  [[nodiscard]] operation::Result<TrajectoryLoadResult> load_trajectory(
      const std::filesystem::path &path, io::TrajectoryFormat format,
      std::size_t cache_budget_bytes, std::size_t prefetch_frame_count = 4U,
      std::optional<operation::LengthUnit> coordinate_unit = std::nullopt,
      std::optional<std::string> h5md_particle_group = std::nullopt,
      trajectory::AtomMappingPolicy mapping_policy =
          trajectory::AtomMappingPolicy::index_order,
      std::span<const model::AtomId> source_to_target_atom_ids = {},
      std::optional<std::uint64_t> expected_topology_version = std::nullopt);
  [[nodiscard]] operation::Result<TrajectoryLoadPlan> plan_trajectory_load(
      const std::filesystem::path &path, io::TrajectoryFormat format,
      std::size_t cache_budget_bytes, std::size_t prefetch_frame_count,
      std::optional<operation::LengthUnit> coordinate_unit,
      std::optional<std::string> h5md_particle_group,
      trajectory::AtomMappingPolicy mapping_policy,
      std::span<const model::AtomId> source_to_target_atom_ids,
      std::optional<std::uint64_t> expected_topology_version) const;
  [[nodiscard]] static operation::Result<TrajectoryLoadCandidate>
  build_trajectory_load_candidate(TrajectoryLoadPlan plan,
                                  operation::TaskContext &context);
  [[nodiscard]] operation::Result<TrajectoryLoadResult>
  commit_trajectory_load(TrajectoryLoadCandidate candidate);
  [[nodiscard]] operation::Result<TrajectorySaveResult>
  save_active_trajectory_frame(const std::filesystem::path &path,
                               io::TrajectoryFormat format,
                               std::string title, bool overwrite,
                               operation::TaskContext &context) const;
  [[nodiscard]] operation::Result<TrajectoryFrameResult>
  set_trajectory_frame(std::size_t frame_index);
  [[nodiscard]] operation::Result<TrajectoryFramePlan>
  plan_trajectory_frame(std::size_t frame_index) const;
  [[nodiscard]] static operation::Result<TrajectoryFrameCandidate>
  build_trajectory_frame_candidate(TrajectoryFramePlan plan,
                                   operation::TaskContext &context);
  [[nodiscard]] operation::Result<TrajectoryFrameResult>
  commit_trajectory_frame(TrajectoryFrameCandidate candidate);
  [[nodiscard]] operation::Result<TrajectoryFrameResult>
  play_trajectory(trajectory::PlaybackMode mode,
                  trajectory::PlaybackDirection direction,
                  std::size_t transitions);
  [[nodiscard]] operation::Result<TrajectoryFrameResult>
  configure_trajectory_range(trajectory::PlaybackRange range,
                             trajectory::PlaybackMode mode,
                             trajectory::PlaybackDirection direction);
  [[nodiscard]] operation::Result<TrajectoryFrameResult> pause_trajectory();
  [[nodiscard]] operation::Result<TrajectoryFrameResult>
  set_trajectory_speed(double frames_per_second);
  [[nodiscard]] operation::Result<TrajectoryFrameResult>
  tick_trajectory(double elapsed_seconds);

  [[nodiscard]] const scene::Camera &camera() const noexcept {
    return camera_.value();
  }
  [[nodiscard]] const scene::StereoParameters &stereo() const noexcept {
    return stereo_;
  }
  [[nodiscard]] operation::Result<scene::Camera>
  set_camera(scene::CameraParameters parameters);
  [[nodiscard]] operation::Result<NamedViewStoreResult>
  store_named_view(std::string name);
  [[nodiscard]] operation::Result<NamedViewRecord>
  recall_named_view(std::string_view name);
  [[nodiscard]] operation::Result<NamedViewDeleteResult>
  delete_named_view(std::string_view name);
  [[nodiscard]] NamedViewDeleteResult clear_named_views();
  [[nodiscard]] std::vector<NamedViewRecord> list_named_views() const;

  [[nodiscard]] const std::shared_ptr<const scene::Scene> &scene() const {
    return scene_;
  }
  [[nodiscard]] const WorkspaceObject *active_object() const noexcept;
  [[nodiscard]] const WorkspaceVolume *active_volume() const noexcept;
  [[nodiscard]] const WorkspaceObject *
  object_by_scene_node(std::uint64_t scene_node_id) const noexcept;
  [[nodiscard]] std::span<const WorkspaceObject> objects() const noexcept {
    return objects_;
  }
  [[nodiscard]] std::span<const WorkspaceVolume> volumes() const noexcept {
    return volumes_;
  }
  [[nodiscard]] std::size_t object_count() const noexcept {
    return objects_.size();
  }
  [[nodiscard]] std::size_t volume_count() const noexcept {
    return volumes_.size();
  }
  [[nodiscard]] const std::vector<DistanceMeasurementRecord> &
  measurements() const noexcept {
    return measurements_;
  }
  [[nodiscard]] const render::RenderSettingStore &render_settings() const
      noexcept {
    return render_settings_;
  }
  [[nodiscard]] std::optional<operation::Error>
  set_render_setting(std::string_view name,
                     const render::RenderSettingScope &scope,
                     render::RenderSettingValue value);
  [[nodiscard]] operation::Result<bool>
  unset_render_setting(std::string_view name,
                       const render::RenderSettingScope &scope);
  [[nodiscard]] operation::Result<std::size_t>
  reset_render_setting_scope(const render::RenderSettingScope &scope);
  [[nodiscard]] operation::Result<render::ResolvedRenderSetting>
  resolve_render_setting(std::string_view name,
                         const render::RenderSettingContext &context) const;
  [[nodiscard]] render::RenderSettingSnapshot
  render_setting_snapshot() const;
  [[nodiscard]] std::optional<operation::Error>
  restore_render_settings(const render::RenderSettingSnapshot &snapshot);

private:
  [[nodiscard]] std::optional<operation::Error>
  commit_render_settings(render::RenderSettingStore candidate);
  [[nodiscard]] WorkspaceObject *mutable_active_object() noexcept;
  [[nodiscard]] WorkspaceVolume *mutable_active_volume() noexcept;
  [[nodiscard]] operation::Result<std::size_t>
  object_index_by_reference(std::string_view reference) const;
  [[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateFrame>>
  active_frame(const WorkspaceObject &object) const;

  std::uint64_t next_object_id_{1U};
  std::uint64_t next_measurement_id_{1U};
  std::vector<WorkspaceObject> objects_;
  std::optional<std::size_t> active_index_;
  std::vector<WorkspaceVolume> volumes_;
  std::optional<std::size_t> active_volume_index_;
  std::shared_ptr<const scene::Scene> scene_;
  std::vector<DistanceMeasurementRecord> measurements_;
  AnalysisResultStore analysis_results_;
  std::optional<scene::Camera> camera_;
  scene::StereoParameters stereo_;
  std::map<std::string, scene::CameraParameters, std::less<>> named_views_;
  render::RenderSettingStore render_settings_;
};

} // namespace molshredder::application
