#pragma once

#include <array>
#include <cstdint>
#include <deque>
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
#include "molshredder/analysis/rdf.hpp"
#include "molshredder/analysis/sasa.hpp"
#include "molshredder/analysis/secondary_structure.hpp"
#include "molshredder/analysis/time_series.hpp"
#include "molshredder/application/analysis_result.hpp"
#include "molshredder/application/representation_visibility.hpp"
#include "molshredder/command/registry.hpp"
#include "molshredder/io/structure_reader.hpp"
#include "molshredder/io/structure_writer.hpp"
#include "molshredder/io/trajectory_reader.hpp"
#include "molshredder/io/trajectory_writer.hpp"
#include "molshredder/io/volume_reader.hpp"
#include "molshredder/io/volume_writer.hpp"
#include "molshredder/model/molecular_system.hpp"
#include "molshredder/model/molecular_builder.hpp"
#include "molshredder/model/chemical_perception.hpp"
#include "molshredder/model/volume.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/render/packet.hpp"
#include "molshredder/render/representation.hpp"
#include "molshredder/render/setting_store.hpp"
#include "molshredder/render/volume_isosurface.hpp"
#include "molshredder/render/volume_slice.hpp"
#include "molshredder/render/transfer_function.hpp"
#include "molshredder/render/direct_volume.hpp"
#include "molshredder/render/molecular_surface.hpp"
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
  std::uint64_t coordinate_source_revision{1U};
  std::uint64_t coordinate_revision{1U};
  selection::NamedSelections selections;
  RepresentationVisibilityState representation_visibility;
  std::vector<RepresentationRecord> representations;
  std::optional<TrajectoryState> trajectory;
  std::optional<render::RenderPacket> molecular_surface;
};

struct MolecularSurfaceResult {
  std::uint64_t object_id{};
  render::MolecularSurfaceKind kind{
      render::MolecularSurfaceKind::solvent_accessible};
  std::string selection_expression;
  double probe_radius_angstrom{};
  double grid_spacing_angstrom{};
  std::size_t voxel_count{};
  std::size_t voxel_budget{};
  std::size_t memory_budget_bytes{};
  std::size_t vertex_count{};
  std::size_t triangle_count{};
  std::size_t pick_target_count{};
  render::Bounds3d bounds;
};

struct WorkspaceVolume {
  std::uint64_t id{};
  scene::NodeId scene_node;
  std::string name;
  std::filesystem::path path;
  io::VolumeFormat format{io::VolumeFormat::auto_detect};
  std::shared_ptr<const model::VolumeGrid> grid;
  std::uint64_t presentation_revision{1U};
  std::vector<render::RenderPacket> representations;
  std::optional<render::TransferFunction> transfer_function;
  std::string transfer_function_name;
  std::shared_ptr<const render::DirectVolumeData> direct_volume;
};

struct VolumeRampResult {
  std::uint64_t object_id{};
  std::string name;
  std::string algorithm;
  std::size_t algorithm_version{};
  std::vector<render::TransferPoint> points;
};

struct DirectVolumeResult {
  std::uint64_t object_id{};
  std::string transfer_function_name;
  render::VolumeClassificationMode mode{
      render::VolumeClassificationMode::post_classified};
  double sampling_step{};
  std::size_t maximum_steps{};
  std::size_t lookup_table_samples{};
  std::size_t texture_budget_bytes{};
  std::size_t required_texture_bytes{};
};

struct DirectVolumePlan {
  std::uint64_t object_id{};
  scene::NodeId scene_node;
  std::shared_ptr<const model::VolumeGrid> grid;
  std::uint64_t expected_presentation_revision{};
  render::TransferFunction transfer_function;
  std::string transfer_function_name;
  bool commit_transfer_function{};
  bool replace_existing{};
  render::DirectVolumeStyle style;
  std::size_t required_texture_bytes{};
};

struct DirectVolumeCandidate {
  DirectVolumePlan plan;
  std::shared_ptr<const render::DirectVolumeData> data;
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

struct VolumeSliceResult {
  std::uint64_t object_id{};
  std::size_t representation_index{};
  render::VolumeSliceAxis axis{render::VolumeSliceAxis::z};
  std::size_t index{};
  render::ColorRgba minimum_color;
  render::ColorRgba maximum_color;
  std::size_t memory_budget_bytes{};
  std::size_t required_bytes{};
  std::size_t vertex_count{};
  std::size_t triangle_count{};
  std::size_t pick_target_count{};
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

inline constexpr unsigned int kEditTransactionDiffSchemaVersion = 1U;

enum class EditTransactionKind {
  atom_position,
  atom_properties,
  residue_properties,
  bond_order,
  molecule_build
};

[[nodiscard]] std::string_view to_string(EditTransactionKind kind) noexcept;

struct CoordinateEditResult {
  std::uint64_t transaction_id{};
  std::uint64_t object_id{};
  model::AtomId atom_id;
  std::size_t frame_index{};
  model::Vec3d previous_position;
  model::Vec3d position;
  std::uint64_t previous_coordinate_source_revision{};
  std::uint64_t previous_coordinate_revision{};
  std::uint64_t coordinate_source_revision{};
  std::uint64_t coordinate_revision{};
  std::size_t affected_state_count{};
  std::size_t invalidated_measurement_count{};
  std::size_t undo_bytes{};
  std::size_t evicted_transaction_count{};
  EditTransactionKind transaction_kind{EditTransactionKind::atom_position};
};

struct EditHistoryStatus {
  std::size_t memory_budget_bytes{};
  std::size_t memory_used_bytes{};
  std::size_t undo_count{};
  std::size_t redo_count{};
  std::optional<std::uint64_t> next_undo_transaction;
  std::optional<std::uint64_t> next_redo_transaction;
};

struct EditHistoryResult {
  std::uint64_t transaction_id{};
  std::string direction;
  std::uint64_t object_id{};
  std::uint64_t coordinate_source_revision{};
  std::uint64_t coordinate_revision{};
  std::size_t invalidated_measurement_count{};
  EditHistoryStatus history;
  EditTransactionKind transaction_kind{EditTransactionKind::atom_position};
};

struct MoleculeBuildCommitResult {
  LoadResult loaded;
  std::uint64_t transaction_id{};
  std::size_t undo_bytes{};
  std::size_t evicted_transaction_count{};
  EditTransactionKind transaction_kind{EditTransactionKind::molecule_build};
};

struct TopologyPropertyEditResult {
  std::uint64_t transaction_id{};
  std::uint64_t object_id{};
  std::uint64_t previous_topology_version{};
  std::uint64_t topology_version{};
  std::uint64_t previous_coordinate_source_revision{};
  std::uint64_t coordinate_source_revision{};
  std::uint64_t coordinate_revision{};
  std::size_t invalidated_measurement_count{};
  std::size_t undo_bytes{};
  std::size_t evicted_transaction_count{};
  EditTransactionKind transaction_kind{EditTransactionKind::atom_properties};
};

struct ChemicalPerceptionApplyResult {
  std::uint64_t object_id{};
  std::uint64_t previous_topology_version{};
  std::uint64_t topology_version{};
  std::size_t previous_bond_count{};
  std::size_t bond_count{};
  std::size_t added_bond_count{};
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

struct AngleAnalysisResult {
  std::uint64_t object_id{};
  std::string first_expression;
  std::string vertex_expression;
  std::string third_expression;
  analysis::AtomAngleResult angle;
};

struct DihedralAnalysisResult {
  std::uint64_t object_id{};
  std::string first_expression;
  std::string second_expression;
  std::string third_expression;
  std::string fourth_expression;
  analysis::AtomDihedralResult dihedral;
};

struct SasaAnalysisResult {
  std::uint64_t object_id{};
  std::string selection_expression;
  std::string radius_source;
  analysis::SasaResult sasa;
};

struct RdfAnalysisResult {
  std::uint64_t object_id{};
  std::string first_expression;
  std::string second_expression;
  analysis::RdfResult rdf;
};

struct AnalysisInputStamp {
  std::uint64_t object_id{};
  std::shared_ptr<const model::MolecularSystem> system;
  std::uint64_t topology_version{};
  std::uint64_t coordinate_source_revision{};
  std::uint64_t coordinate_revision{};
};

struct SasaAnalysisPlan {
  AnalysisInputStamp source;
  std::shared_ptr<const model::CoordinateFrame> frame;
  std::string selection_expression;
  std::string radius_source;
  std::vector<std::uint8_t> selected;
  std::vector<double> vdw_radii_angstrom;
  double probe_radius_angstrom{};
  std::size_t samples_per_atom{};
  std::size_t evaluation_budget{};
};

struct SasaAnalysisCandidate {
  SasaAnalysisPlan plan;
  analysis::SasaResult sasa;
};

struct RdfAnalysisPlan {
  AnalysisInputStamp source;
  std::shared_ptr<const model::CoordinateFrame> frame;
  std::string first_expression;
  std::string second_expression;
  std::vector<std::uint8_t> first_selected;
  std::vector<std::uint8_t> second_selected;
  double maximum_radius{};
  double bin_width{};
  analysis::DistanceBoundary boundary{analysis::DistanceBoundary::raw};
  analysis::RdfNormalization normalization{analysis::RdfNormalization::pair_count};
  bool same_selection{};
  std::uint64_t evaluation_budget{};
};

struct RdfAnalysisCandidate {
  RdfAnalysisPlan plan;
  analysis::RdfResult rdf;
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
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
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

struct RmsdMatrixAnalysisResult {
  std::uint64_t object_id{};
  std::string selection_expression;
  std::string fit_selection_expression;
  analysis::SeriesRange range;
  analysis::FitMode fit{analysis::FitMode::rigid};
  AnalysisWeightProvenance weights;
  analysis::RmsdMatrixResult matrix;
};

struct RmsdMatrixAnalysisPlan {
  AnalysisInputStamp source;
  std::shared_ptr<trajectory::FrameCache> cache;
  std::string selection_expression;
  std::string fit_selection_expression;
  analysis::SeriesRange range;
  analysis::FitMode fit{analysis::FitMode::rigid};
  AnalysisWeightProvenance weights;
  std::vector<std::uint8_t> selected;
  std::vector<std::uint8_t> fit_selected;
  std::vector<double> weight_values;
  analysis::MissingAtomPolicy missing_atom_policy{analysis::MissingAtomPolicy::error};
  std::uint64_t frame_pair_budget{};
};

struct RmsdMatrixAnalysisCandidate {
  RmsdMatrixAnalysisPlan plan;
  analysis::RmsdMatrixResult matrix;
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

struct NamedSceneRecord {
  std::string name;
  std::size_t molecular_object_count{};
  std::size_t volume_object_count{};
  bool current{};

  friend bool operator==(const NamedSceneRecord&, const NamedSceneRecord&) =
      default;
};

struct NamedSceneStoreResult {
  NamedSceneRecord scene;
  std::size_t scene_count{};
  bool replaced{};
};

struct NamedSceneDeleteResult {
  std::string name;
  std::size_t scene_count{};
  bool cleared_all{};
};

struct MovieFrameRecord {
  // Movie frames are one-based at the product API boundary.
  std::size_t frame{1U};
  std::optional<std::string> scene_name;
  // Trajectory frames retain the existing zero-based trajectory contract.
  std::optional<std::size_t> trajectory_frame;

  friend bool operator==(const MovieFrameRecord&, const MovieFrameRecord&) =
      default;
};

struct MovieTimelineState {
  std::size_t frame_count{};
  std::size_t current_frame{1U};
  double frames_per_second{30.0};
  bool loop{};
  bool playing{};
  std::map<std::size_t, MovieFrameRecord> keyframes;

  friend bool operator==(const MovieTimelineState&, const MovieTimelineState&) =
      default;
};

struct MovieTimelineResult {
  MovieTimelineState timeline;
  std::size_t applied_keyframe_count{};
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
  [[nodiscard]] operation::Result<VolumeSliceResult>
  show_volume_slice(render::VolumeSliceStyle style, bool replace_existing,
                    operation::TaskContext &context);
  [[nodiscard]] operation::Result<VolumeRampResult>
  set_volume_ramp(render::TransferPreset preset);
  [[nodiscard]] operation::Result<VolumeRampResult>
  define_volume_ramp(std::string name, render::TransferFunction transfer);
  [[nodiscard]] operation::Result<VolumeRampResult> volume_ramp() const;
  [[nodiscard]] operation::Result<DirectVolumeResult>
  show_direct_volume(render::DirectVolumeStyle style,
                     std::optional<render::TransferPreset> preset,
                     bool replace_existing,
                     operation::TaskContext &context);
  [[nodiscard]] operation::Result<DirectVolumePlan>
  plan_direct_volume(render::DirectVolumeStyle style,
                     std::optional<render::TransferPreset> preset,
                     bool replace_existing) const;
  [[nodiscard]] static operation::Result<DirectVolumeCandidate>
  build_direct_volume_candidate(DirectVolumePlan plan,
                                operation::TaskContext &context);
  [[nodiscard]] operation::Result<DirectVolumeResult>
  commit_direct_volume(DirectVolumeCandidate candidate);
  [[nodiscard]] operation::Result<bool> hide_direct_volume();
  [[nodiscard]] operation::Result<MolecularSurfaceResult>
  show_molecular_surface(std::string selection_expression,
                         render::MolecularSurfaceStyle style,
                         operation::TaskContext &context);
  [[nodiscard]] operation::Result<bool> hide_molecular_surface();
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
  [[nodiscard]] operation::Result<CoordinateEditResult>
  set_active_atom_position(model::AtomId atom_id, model::Vec3d position,
                           std::uint64_t expected_topology_version,
                           std::uint64_t expected_coordinate_source_revision);
  [[nodiscard]] operation::Result<EditHistoryResult> undo_edit();
  [[nodiscard]] operation::Result<EditHistoryResult> redo_edit();
  [[nodiscard]] EditHistoryStatus edit_history_status() const noexcept;
  [[nodiscard]] operation::Result<EditHistoryStatus>
  set_edit_history_budget(std::size_t memory_budget_bytes);
  [[nodiscard]] operation::Result<MoleculeBuildCommitResult>
  commit_built_molecule(std::string name,
                        const model::MoleculeBuildResult &built);
  [[nodiscard]] operation::Result<TopologyPropertyEditResult>
  commit_active_topology_edit(
      std::shared_ptr<const model::Topology> topology,
      std::uint64_t expected_topology_version,
      std::uint64_t expected_coordinate_source_revision,
      EditTransactionKind transaction_kind);
  [[nodiscard]] operation::Result<ChemicalPerceptionApplyResult>
  apply_active_chemical_perception(
      const model::ChemicalPerceptionReport &report);
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
  [[nodiscard]] operation::Result<ScientificResultContract>
  bind_scientific_result_contract(ScientificResultContract contract) const;
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
  [[nodiscard]] operation::Result<AngleAnalysisResult> analyze_angle(
      std::string first_expression, std::string vertex_expression,
      std::string third_expression,
      analysis::DistanceBoundary boundary = analysis::DistanceBoundary::raw);
  [[nodiscard]] operation::Result<DihedralAnalysisResult> analyze_dihedral(
      std::string first_expression, std::string second_expression,
      std::string third_expression, std::string fourth_expression,
      analysis::DistanceBoundary boundary = analysis::DistanceBoundary::raw);
  [[nodiscard]] operation::Result<SasaAnalysisResult> analyze_sasa(
      std::string selection_expression, double probe_radius_angstrom,
      std::size_t samples_per_atom, std::size_t evaluation_budget,
      operation::TaskContext *context = nullptr);
  [[nodiscard]] operation::Result<SasaAnalysisPlan> plan_sasa_analysis(
      std::string selection_expression, double probe_radius_angstrom,
      std::size_t samples_per_atom, std::size_t evaluation_budget) const;
  [[nodiscard]] static operation::Result<SasaAnalysisCandidate>
  build_sasa_analysis_candidate(SasaAnalysisPlan plan,
                                operation::TaskContext &context);
  [[nodiscard]] operation::Result<SasaAnalysisResult>
  commit_sasa_analysis(SasaAnalysisCandidate candidate) const;
  [[nodiscard]] operation::Result<RdfAnalysisResult> analyze_rdf(
      std::string first_expression, std::string second_expression,
      double maximum_radius, double bin_width,
      analysis::DistanceBoundary boundary,
      analysis::RdfNormalization normalization, bool same_selection,
      std::uint64_t evaluation_budget,
      operation::LengthUnit distance_unit = operation::LengthUnit::angstrom,
      operation::TaskContext *context = nullptr);
  [[nodiscard]] operation::Result<RdfAnalysisPlan> plan_rdf_analysis(
      std::string first_expression, std::string second_expression,
      double maximum_radius, double bin_width,
      analysis::DistanceBoundary boundary,
      analysis::RdfNormalization normalization, bool same_selection,
      std::uint64_t evaluation_budget,
      operation::LengthUnit distance_unit = operation::LengthUnit::angstrom) const;
  [[nodiscard]] static operation::Result<RdfAnalysisCandidate>
  build_rdf_analysis_candidate(RdfAnalysisPlan plan,
                               operation::TaskContext &context);
  [[nodiscard]] operation::Result<RdfAnalysisResult>
  commit_rdf_analysis(RdfAnalysisCandidate candidate) const;
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
  [[nodiscard]] operation::Result<RmsdMatrixAnalysisResult>
  analyze_rmsd_matrix(std::string selection_expression,
                      std::string fit_selection_expression,
                      analysis::SeriesRange range, analysis::FitMode fit,
                      analysis::WeightMode weight_mode,
                      analysis::MissingAtomPolicy missing_atom_policy,
                      std::uint64_t frame_pair_budget,
                      operation::TaskContext &context);
  [[nodiscard]] operation::Result<RmsdMatrixAnalysisPlan>
  plan_rmsd_matrix_analysis(
      std::string selection_expression, std::string fit_selection_expression,
      analysis::SeriesRange range, analysis::FitMode fit,
      analysis::WeightMode weight_mode,
      analysis::MissingAtomPolicy missing_atom_policy,
      std::uint64_t frame_pair_budget) const;
  [[nodiscard]] static operation::Result<RmsdMatrixAnalysisCandidate>
  build_rmsd_matrix_analysis_candidate(RmsdMatrixAnalysisPlan plan,
                                       operation::TaskContext &context);
  [[nodiscard]] operation::Result<RmsdMatrixAnalysisResult>
  commit_rmsd_matrix_analysis(RmsdMatrixAnalysisCandidate candidate) const;
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
  [[nodiscard]] operation::Result<NamedSceneStoreResult>
  store_named_scene(std::string name);
  [[nodiscard]] operation::Result<NamedSceneRecord>
  recall_named_scene(std::string_view name);
  [[nodiscard]] operation::Result<NamedSceneDeleteResult>
  delete_named_scene(std::string_view name);
  [[nodiscard]] NamedSceneDeleteResult clear_named_scenes();
  [[nodiscard]] std::vector<NamedSceneRecord> list_named_scenes() const;
  [[nodiscard]] operation::Result<MovieTimelineResult>
  configure_movie(std::size_t frame_count, double frames_per_second,
                  bool loop);
  [[nodiscard]] operation::Result<MovieTimelineResult>
  set_movie_keyframe(MovieFrameRecord keyframe);
  [[nodiscard]] operation::Result<MovieTimelineResult>
  seek_movie(std::size_t frame);
  [[nodiscard]] operation::Result<MovieTimelineResult>
  play_movie(bool playing);
  [[nodiscard]] operation::Result<MovieTimelineResult>
  step_movie(std::size_t steps);
  [[nodiscard]] operation::Result<MovieTimelineResult> clear_movie();
  [[nodiscard]] const std::optional<MovieTimelineState>& movie() const
      noexcept {
    return movie_;
  }

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

  // Successful product-facing state mutations are recorded as normalized
  // canonical invocations. Query/export commands and failed mutations are not
  // part of this journal. Session producers use it instead of reverse
  // engineering mutable Workspace internals.
  void record_session_invocation(command::Invocation invocation);
  [[nodiscard]] std::span<const command::Invocation>
  session_invocations() const noexcept {
    return session_invocations_;
  }
  void clear_session_invocations() noexcept { session_invocations_.clear(); }

private:
  enum class EditRecordKind { coordinate, object_create };
  struct EditRecord {
    EditRecordKind kind{EditRecordKind::coordinate};
    std::uint64_t transaction_id{};
    std::uint64_t object_id{};
    std::shared_ptr<const model::MolecularSystem> before;
    std::shared_ptr<const model::MolecularSystem> after;
    std::optional<WorkspaceObject> created_object;
    std::size_t memory_bytes{};
    EditTransactionKind transaction_kind{EditTransactionKind::atom_position};
  };
  [[nodiscard]] operation::Result<std::size_t>
  apply_edit_record(const EditRecord &record, bool use_after);
  void evict_edit_history_to_budget();
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
  // A scene is an in-memory full Workspace state checkpoint. Stored snapshots
  // have their own scene map cleared, avoiding recursive ownership. The
  // canonical `scene store` journal reconstructs them across session replay.
  std::map<std::string, std::shared_ptr<const Workspace>, std::less<>>
      named_scenes_;
  std::optional<std::string> current_scene_name_;
  std::optional<MovieTimelineState> movie_;
  render::RenderSettingStore render_settings_;
  std::uint64_t next_edit_transaction_id_{1U};
  std::size_t edit_history_memory_budget_bytes_{256U * 1024U * 1024U};
  std::size_t edit_history_memory_used_bytes_{};
  std::deque<EditRecord> undo_history_;
  std::deque<EditRecord> redo_history_;
  std::vector<command::Invocation> session_invocations_;
};

} // namespace molshredder::application
