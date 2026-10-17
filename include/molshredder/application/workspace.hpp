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
#include "molshredder/analysis/backbone.hpp"
#include "molshredder/analysis/principal_axes.hpp"
#include "molshredder/analysis/shape.hpp"
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
#include "molshredder/render/color_table.hpp"
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
#include "molshredder/trajectory/pbc.hpp"
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

// Non-destructive PBC processing of an object's frames (Q-035 a): the original
// coordinate source and trajectory are kept so `pbc mode none` restores them.
struct PbcProcessing {
  trajectory::PbcMode mode{trajectory::PbcMode::wrap};
  std::shared_ptr<const model::CoordinateSource> original_coordinates;
  std::optional<TrajectoryState> original_trajectory;
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
  // PyMOL-compatible object color index (assigned from the auto cycle at
  // load) and sparse per-atom color indices keyed by persistent AtomId,
  // sorted by id. Atoms without an entry keep the MolShredder default palette.
  std::int32_t object_color{render::kColorDefault};
  std::shared_ptr<const std::vector<std::pair<std::uint64_t, std::int32_t>>>
      atom_colors;
  // Non-empty atom label text keyed by persistent AtomId, sorted by id.
  std::shared_ptr<const std::vector<std::pair<std::uint64_t, std::string>>>
      atom_labels;
  // Explicit PyMOL cartoon type codes keyed by AtomId (automatic omitted).
  std::shared_ptr<const std::vector<std::pair<std::uint64_t, std::int32_t>>>
      cartoon_types;
  std::optional<PbcProcessing> pbc;
  // VMD `mol fix`: the object's scene placement is not changed by align,
  // placement copy or placement reset until it is freed.
  bool fixed{false};
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

enum class ContourMode { surface, mesh, dot };

// Named PyMOL-style map contour (isosurface/isomesh/isodot object).
struct ContourRecord {
  std::string name;
  ContourMode mode{ContourMode::mesh};
  float level{};
  std::int32_t color{};
  // Region restriction from a selection of the active molecular object:
  // carve keeps geometry within the distance of any selected atom; buffer
  // keeps geometry inside the selection bounding box padded by the distance.
  std::vector<model::Vec3d> region_atoms;
  double carve{};
  double buffer{};
  render::Bounds3d extent;
  render::RenderPacket packet;
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
  std::vector<ContourRecord> contours;
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
  render::ColorTable color_table;
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
  // Set for io::TrajectoryFormat::molfile; owns the provider-backed frames.
  std::shared_ptr<const model::CoordinateSource> external_source;
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
  render::ColorTable color_table;
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
  bool fixed{};
};

// Atom and state counts of one selection in one object (PyMOL count_atoms /
// count_states).
struct SelectionCountResult {
  std::uint64_t object_id{};
  std::string object_name;
  std::string selection_expression;
  std::size_t atom_count{};
  std::size_t object_atom_count{};
  std::optional<std::size_t> state_count;
};

// A new molecular object made from the selected atoms of another object.
struct ObjectCreateResult {
  std::uint64_t source_object_id{};
  std::string source_object_name;
  std::uint64_t object_id{};
  std::string object_name;
  std::string selection_expression;
  std::size_t atom_count{};
  std::size_t bond_count{};
  std::optional<std::size_t> state_count;
  std::size_t removed_from_source_atom_count{};
};

// Read-only table of atom properties (PyMOL iterate / iterate_state / identify /
// index) with no code evaluation: columns are chosen from a fixed property list.
struct AtomListing {
  std::vector<std::string> columns;
  std::vector<std::vector<command::Value>> rows;
  std::size_t selected_atom_count{};
  bool truncated{};
};

struct ChargeSum {
  std::string kind;
  double sum{};
  std::size_t atom_count{};
  std::string source;
};

struct MassSum {
  double mass{};
  std::size_t atom_count{};
  std::string unit;
  std::string source;
  bool estimated{};
};

// Rigid alignment of one molecular object onto another (Q-035 a). The fit is
// computed on world-space positions of the paired atoms in each object's
// current frame; applying it only changes the mobile object's scene transform.
struct AlignmentRequest {
  std::string mobile_object{"current"};
  std::string target_object;
  std::string mobile_selection{"all"};
  std::string target_selection{"all"};
  // "order" pairs the selected atoms of both objects one by one in atom order
  // (equal counts required); "identifier" pairs atoms with the same chain,
  // residue number, insertion code and atom name.
  std::string match{"order"};
  // Explicit 1-based atom index pairs (mobile, target); overrides the selections.
  std::vector<std::pair<std::size_t, std::size_t>> pairs;
  bool uniform_weights{true};
  bool apply{};
};

struct AlignmentResult {
  std::uint64_t mobile_object_id{};
  std::uint64_t target_object_id{};
  std::size_t paired_atom_count{};
  double rmsd_before{};
  double rmsd_after{};
  // Row-major 4x4 matrix taking mobile world coordinates onto the target.
  std::array<double, 16> matrix{};
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  bool applied{};
  std::optional<scene::Transform> transform;
  std::uint64_t scene_version{};
};

struct PbcModeResult {
  std::uint64_t object_id{};
  std::string object_name;
  std::string previous_mode;
  std::string mode;
  std::optional<std::size_t> frame_count;
  bool changed{};
};

struct ObjectSplitResult {
  std::uint64_t source_object_id{};
  std::string source_object_name;
  std::vector<ObjectCreateResult> created;
};

struct StateAppendResult {
  std::uint64_t object_id{};
  std::string object_name;
  std::size_t previous_state_count{};
  std::size_t appended_state_count{};
  std::size_t state_count{};
};

struct StateDeleteResult {
  std::uint64_t object_id{};
  std::string object_name;
  std::size_t previous_state_count{};
  std::size_t removed_state_count{};
  std::size_t state_count{};
};

// How join_states matches atoms between the joined objects (PyMOL join_states
// modes 1 and 2). Discrete (0) and sequence-alignment (3) joins are not
// available: they need state-varying topology and a sequence alignment.
enum class StateJoinMode { identical_topology, identifier_intersection };

// A molecular object that contains at least one atom of a selection.
struct CoveredObject {
  std::uint64_t object_id{};
  std::string object_name;
  std::size_t atom_count{};
};

// A named group of molecular objects and other groups (PyMOL group object).
// Membership is workspace metadata: objects stay direct children of the scene
// root, so rendering, ordering and transforms are unaffected.
struct GroupInfo {
  std::string name;
  // Direct members in insertion order: object names and nested group names.
  std::vector<std::string> members;
  std::optional<std::string> parent;
  // Panel expansion state; it does not change visibility.
  bool open{true};
  // True when at least one object below the group is visible.
  bool enabled{};
  // Molecular objects below the group, nested groups included.
  std::size_t object_count{};
};

struct GroupChangeResult {
  std::string name;
  bool created{};
  bool deleted{};
  // Direct members after the change.
  std::size_t member_count{};
  // Names that were added, removed, moved or deleted by this call.
  std::vector<std::string> affected;
};

// Kind of a named workspace entity (PyMOL get_type).
struct EntityTypeResult {
  std::string name;
  // "object:molecule", "object:volume" or "selection".
  std::string type;
  std::uint64_t object_id{};
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

// Object range of selection-based display operations (Q-021 b): the active
// object (default) or every molecular object, like PyMOL.
enum class ObjectScope { active, workspace };

struct ShowResult {
  std::uint64_t object_id{};
  std::size_t representation_index{};
  std::size_t primitive_count{};
  std::size_t selected_atom_count{};
};

struct ColorAssignResult {
  std::uint64_t object_id{};
  std::int32_t color_index{};
  std::string color_name;
  std::size_t affected_atom_count{};
  std::size_t colored_atom_count{};
  std::size_t representation_count{};
};

struct ColorDefineResult {
  std::int32_t index{};
  std::string name;
  render::ColorRgb rgb;
  bool created{};
};

struct ColorQueryResult {
  std::int32_t index{};
  std::string name;
  std::optional<render::ColorRgb> rgb;
};

struct SpectrumRequest {
  std::string expression{"count"};
  std::string palette{"rainbow"};
  std::string selection{"all"};
  std::optional<double> minimum;
  std::optional<double> maximum;
  bool by_residue{};
  std::string interpolation{"rgb"};
};

struct SpectrumResult {
  std::uint64_t object_id{};
  double minimum{};
  double maximum{};
  // "palette" (named ramp colors) or "color-list" (interpolated literals).
  std::string mode;
  std::string palette;
  std::size_t affected_atom_count{};
  std::size_t colored_atom_count{};
  std::size_t representation_count{};
};

struct LabelResult {
  std::uint64_t object_id{};
  std::size_t affected_atom_count{};
  std::size_t labeled_atom_count{};
  std::size_t visible_label_count{};
  // Text of the first affected atom, for frontends and parity checks.
  std::string first_text;
};

struct ImageSaveRequest {
  std::filesystem::path path;
  // 0 uses 640 for width and width / camera aspect for height.
  std::size_t width{};
  std::size_t height{};
  std::optional<double> dpi;
  // true mirrors PyMOL ray (ray_opaque_background), false mirrors draw.
  bool ray{true};
  // "guess" picks png/ppm from the extension; "png" and "ppm" are explicit.
  // A path without an extension gets the format's extension appended.
  std::string format{"guess"};
  // width_value/height_value are measured in `units` (pixels, inches,
  // centimeters or millimeters) when set; physical units need a dpi
  // (300 when omitted). They replace width/height.
  std::string units{"pixels"};
  std::optional<double> width_value;
  std::optional<double> height_value;
};

struct MovieExportRequest {
  // Frame files are named <prefix><4-digit frame>.<extension>.
  std::filesystem::path prefix;
  // 1-based inclusive movie frames; unset means the whole timeline.
  std::optional<std::size_t> first;
  std::optional<std::size_t> last;
  bool overwrite{};
  ImageSaveRequest image;
};

struct MovieExportResult {
  std::size_t first_frame{};
  std::size_t last_frame{};
  std::size_t frame_count{};
  std::size_t width{};
  std::size_t height{};
  std::size_t total_bytes{};
  std::string format;
  std::vector<std::filesystem::path> paths;
  std::vector<std::string> warnings;
};

struct ImageSaveResult {
  std::filesystem::path path;
  std::size_t width{};
  std::size_t height{};
  std::size_t bytes{};
  std::string renderer;
  bool opaque_background{};
  std::size_t primitive_count{};
  std::string format;
  std::optional<double> dpi;
  std::vector<std::string> warnings;
};

struct CartoonTypeResult {
  std::uint64_t object_id{};
  std::string type;
  std::int32_t code{};
  std::size_t affected_atom_count{};
  std::size_t representation_count{};
};

struct ContourRequest {
  std::string name;
  ContourMode mode{ContourMode::mesh};
  double level{1.0};
  std::string map{"current"};
  std::string color{"default"};
  std::string selection;
  double carve{};
  double buffer{};
};

struct ContourResult {
  std::uint64_t volume_id{};
  std::string name;
  ContourMode mode{ContourMode::mesh};
  double level{};
  bool replaced{};
  std::size_t primitive_count{};
  render::Bounds3d extent;
  // Explicit fallbacks, e.g. an isosurface_algorithm MolShredder does not
  // implement (geometry always comes from marching tetrahedra).
  std::vector<std::string> warnings;
};

struct RampResult {
  std::string name;
  std::int32_t color_index{};
  std::string source_kind;
  std::vector<double> levels;
  std::size_t color_count{};
  int spectrum{};
  bool replaced{};
};

struct BackgroundResult {
  std::optional<std::int32_t> color_index;
  std::string color_name;
  render::ColorRgb rgb;
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

// Read-only copy of the selected atoms of the active object in its current
// frame, for plugins and other consumers that must not touch live state.
struct SelectionSnapshot {
  std::uint64_t object_id{};
  std::string object_name;
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::size_t frame_index{};
  std::vector<std::uint64_t> atom_ids;
  std::vector<std::string> atom_names;
  std::vector<std::uint8_t> atomic_numbers;
  std::vector<model::Vec3d> positions;
};

struct ShapeAnalysisResult {
  std::uint64_t object_id{};
  std::string selection_expression;
  analysis::CenterMode mode{analysis::CenterMode::centroid};
  analysis::ShapeResult shape;
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

enum class SceneInsert { end, before, after };
enum class SceneStep { first, next, previous, last };

struct ColorUsage {
  std::int32_t index{};
  std::string name;
  std::size_t atom_count{};
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
  // Commits a provider-produced single-volume document with the same name
  // checks and scene registration as load_volume.
  [[nodiscard]] operation::Result<VolumeLoadResult>
  load_volume_document(io::VolumeDocument document,
                       const std::filesystem::path &path);
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
                        bool overwrite, operation::TaskContext &context,
                        std::string_view selection_expression = "all") const;
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
  // Molecular objects in which `selection_expression` matches an atom. A named
  // selection that does not exist in an object simply leaves it uncovered.
  [[nodiscard]] operation::Result<std::vector<CoveredObject>>
  covering_objects(std::string selection_expression) const;
  // Shows or hides every molecular object whose name matches a glob (`*`, `?`)
  // or that is covered by a selection. Either all targets change or none.
  [[nodiscard]] operation::Result<std::vector<WorkspaceObjectInfo>>
  set_visibility_by_pattern(std::string_view pattern, bool visible);
  [[nodiscard]] operation::Result<std::vector<WorkspaceObjectInfo>>
  set_visibility_by_selection(std::string selection_expression, bool visible);
  [[nodiscard]] operation::Result<EntityTypeResult>
  entity_type(std::string_view name) const;

  // Group lifecycle (Q-033 a). Member arguments are object or group names and
  // may use the glob characters `*` and `?`; a pattern that matches nothing is
  // an error and every call changes the workspace completely or not at all.
  [[nodiscard]] operation::Result<GroupChangeResult>
  group_add(std::string_view name, const std::vector<std::string> &members);
  [[nodiscard]] operation::Result<GroupChangeResult>
  group_remove(std::string_view name, const std::vector<std::string> &members);
  [[nodiscard]] operation::Result<GroupChangeResult>
  group_empty(std::string_view name);
  [[nodiscard]] operation::Result<GroupChangeResult>
  group_ungroup(const std::vector<std::string> &members);
  // Deletes the group and moves its members to the group's parent (or the top
  // level) in place.
  [[nodiscard]] operation::Result<GroupChangeResult>
  group_excise(std::string_view name);
  // Deletes every object below the group and the nested groups; the group
  // itself stays and becomes empty.
  [[nodiscard]] operation::Result<GroupChangeResult>
  group_purge(std::string_view name);
  enum class GroupExpansion { open, close, toggle };
  [[nodiscard]] operation::Result<GroupInfo>
  group_set_expansion(std::string_view name, GroupExpansion expansion);
  [[nodiscard]] std::vector<GroupInfo> list_groups() const;
  [[nodiscard]] operation::Result<AlignmentResult>
  align_objects(const AlignmentRequest &request);
  // One new object per chain of the source (named <prefix><chain>, or
  // <source>_<chain> without a prefix); chains without an identifier are
  // refused. All objects are created or none.
  [[nodiscard]] operation::Result<ObjectSplitResult>
  split_object_by_chain(std::string_view object_reference, std::string prefix);
  // Natural-sorts the object panel order by object name.
  [[nodiscard]] operation::Result<ObjectLifecycleResult> sort_objects_by_name();
  // Copies the scene placement (translation, rotation, scale, pivot) of one
  // object onto another; coordinates are untouched.
  // Fixes or frees an object's scene placement (VMD mol fix / mol free).
  [[nodiscard]] operation::Result<ObjectLifecycleResult>
  set_object_fixed(std::string_view object_reference, bool fixed);
  [[nodiscard]] operation::Result<ObjectTransformResetResult>
  copy_object_transform(std::string_view source_reference,
                        std::string_view target_reference);
  // Shows an object's frames wrapped into the unit cell, unwrapped by atom
  // continuity, or as loaded ("none"; "status" only reports the mode). Source
  // files and stored coordinates are never changed. Editing, undo and state
  // append are refused while a mode is active.
  [[nodiscard]] operation::Result<PbcModeResult>
  set_pbc_mode(std::string_view object_reference, std::string_view mode);
  // Appends every state of a parsed structure document to a static object.
  // The topology must match atom by atom (name, element, residue name, number,
  // chain); a trajectory-attached object and an unknown state count are
  // refused. Either every state is appended or the object is unchanged.
  [[nodiscard]] operation::Result<StateAppendResult>
  append_states(std::string_view object_reference, const io::StructureDocument &document);
  // Creates one single-state object per state of a multi-state object (PyMOL
  // split_states). States are numbered from 1; `last` 0 means the final
  // state. Objects are named <prefix><state:04d> (prefix defaults to
  // "<object>_"). The source is kept. All objects are created or none.
  using StateJoinMode = application::StateJoinMode;
  [[nodiscard]] operation::Result<ObjectSplitResult>
  split_states(std::string_view object_reference, std::size_t first,
               std::size_t last, std::string prefix);
  // Removes states given as "1-3 10" style lists/ranges (1-based, whitespace
  // or comma separated, out-of-range numbers ignored). Removing every state
  // is refused.
  [[nodiscard]] operation::Result<StateDeleteResult>
  delete_states(std::string_view object_reference, std::string_view states);
  // VMD-style frame editing on a static or multi-model object (frames are
  // numbered from 0): deletes the frames in [beg, end] (end -1 = last) except
  // every `skip`-th one counted from beg (skip 0 or 1: delete the whole range;
  // 1 keeps all). Deleting every frame is refused.
  [[nodiscard]] operation::Result<StateDeleteResult>
  delete_frames(std::string_view object_reference, long long beg, long long end,
                long long skip);
  // Appends a copy of a frame (default: the current one) as a new last frame.
  [[nodiscard]] operation::Result<StateAppendResult>
  duplicate_frame(std::string_view object_reference, std::optional<std::size_t> frame);
  // Creates a multi-state object `name` from the states of several objects
  // (PyMOL join_states). The selection restricts the atoms taken from each
  // object. The sources are kept.
  [[nodiscard]] operation::Result<ObjectCreateResult>
  join_states(std::string name, const std::vector<std::string> &object_references,
              std::string selection_expression, StateJoinMode mode);
  // Read-only atom queries on the active object's current frame.
  [[nodiscard]] operation::Result<AtomListing>
  list_atoms(std::string selection_expression,
             const std::vector<std::string> &properties, std::size_t limit) const;
  [[nodiscard]] operation::Result<std::vector<std::string>>
  list_chains(std::string selection_expression) const;
  [[nodiscard]] operation::Result<ChargeSum>
  sum_charge(std::string selection_expression, std::string_view kind) const;
  [[nodiscard]] operation::Result<MassSum>
  sum_mass(std::string selection_expression) const;
  [[nodiscard]] operation::Result<std::vector<analysis::BackboneAngles>>
  backbone_angles(std::string selection_expression) const;
  // Counts selected atoms and states without changing any state.
  [[nodiscard]] operation::Result<SelectionCountResult>
  count_selection(std::string_view object_reference,
                  std::string selection_expression) const;
  // Creates a new object from the selected atoms of `source_reference`
  // (all states, stable atom IDs kept). With `remove_from_source` the atoms
  // are also removed from the source object (PyMOL extract).
  [[nodiscard]] operation::Result<ObjectCreateResult>
  create_object_from_selection(std::string_view source_reference,
                               std::string name,
                               std::string selection_expression,
                               bool remove_from_source);
  // Removes the selected atoms of the active object and their bonds
  // (PyMOL remove). Removing every atom is refused; delete the object instead.
  [[nodiscard]] operation::Result<TopologyMutationResult>
  remove_active_selection(std::string selection_expression);
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
  set_named_selection(std::string name, std::string expression, bool dynamic,
                      bool merge = false, std::string state = "current");
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
      RepresentationVisibilityMutation mutation,
      ObjectScope scope = ObjectScope::active);
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
  // Radius of gyration and principal inertia axes of the active object's
  // selection in its current frame (pure result; no state change).
  [[nodiscard]] operation::Result<ShapeAnalysisResult>
  analyze_shape(std::string selection_expression, analysis::CenterMode mode);
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
  // Measurement overlay style from render settings and the resulting packet
  // of visible analysis results (PyMOL dash/arc layout).
  [[nodiscard]] AnalysisOverlayStyle analysis_overlay_style() const;
  [[nodiscard]] render::RenderPacket analysis_overlay_packet() const;
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
      std::optional<std::uint64_t> expected_topology_version = std::nullopt,
      std::shared_ptr<const model::CoordinateSource> external_source =
          nullptr);
  [[nodiscard]] operation::Result<TrajectoryLoadPlan> plan_trajectory_load(
      const std::filesystem::path &path, io::TrajectoryFormat format,
      std::size_t cache_budget_bytes, std::size_t prefetch_frame_count,
      std::optional<operation::LengthUnit> coordinate_unit,
      std::optional<std::string> h5md_particle_group,
      trajectory::AtomMappingPolicy mapping_policy,
      std::span<const model::AtomId> source_to_target_atom_ids,
      std::optional<std::uint64_t> expected_topology_version,
      std::shared_ptr<const model::CoordinateSource> external_source =
          nullptr) const;
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
  store_named_scene(std::string name, SceneInsert insert = SceneInsert::end);
  // Renames a scene in place: its position, current marker and movie
  // keyframes follow the new name.
  [[nodiscard]] operation::Result<NamedSceneRecord>
  rename_named_scene(std::string_view name, std::string new_name);
  // Re-stores the current scene from the present workspace state and keeps its
  // position in the scene order.
  [[nodiscard]] operation::Result<NamedSceneStoreResult> update_named_scene();
  // Recalls the first, next, previous or last scene of the scene order. Without
  // a current scene `next` starts at the first and `previous` at the last.
  [[nodiscard]] operation::Result<NamedSceneRecord>
  step_named_scene(SceneStep step, bool wrap);
  // Moves scenes inside the order: location is top, bottom or current (placed
  // before the current scene). `sort` natural-sorts the given scenes (all when
  // none are given). Returns the new order.
  [[nodiscard]] operation::Result<std::vector<std::string>>
  order_named_scenes(const std::vector<std::string> &names, bool sort,
                     std::string_view location);
  // Number of atoms of `selection_expression` per effective color in the
  // active object (kColorDefault atoms use the object color when it is set).
  [[nodiscard]] operation::Result<std::vector<ColorUsage>>
  color_usage(std::string selection_expression) const;
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
  // Renders movie frames to numbered image files and restores the timeline
  // position. Refuses to run while playing, and either writes every frame or
  // removes the files this call created.
  [[nodiscard]] operation::Result<MovieExportResult>
  export_movie_frames(const MovieExportRequest &request);
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
  // Atom-scope set/unset for every atom of a selection in one commit, like
  // PyMOL `set NAME, VALUE, SELECTION`. The scope supplies object and state;
  // its atom id is replaced per selected atom. Returns the selected count.
  [[nodiscard]] operation::Result<std::size_t> set_render_setting_selection(
      std::string_view name, const render::RenderSettingScope &scope,
      std::string_view selection_expression,
      std::optional<render::RenderSettingValue> value);
  // Persistent IDs of bonds whose two atoms are in an atom-ID list.
  [[nodiscard]] operation::Result<std::vector<std::uint64_t>>
  selected_bond_ids(std::uint64_t object_id,
                    const std::vector<std::uint64_t> &atom_ids) const;
  // Present atoms of the active object's selection in the current frame.
  // Fails instead of truncating when more than `max_atoms` match.
  [[nodiscard]] operation::Result<SelectionSnapshot>
  selection_snapshot(std::string_view selection_expression,
                     std::size_t max_atoms) const;
  // Persistent atom IDs of a selection in one object, in atom order.
  [[nodiscard]] operation::Result<std::vector<std::uint64_t>>
  selected_atom_ids(std::uint64_t object_id,
                    std::string_view selection_expression) const;
  [[nodiscard]] operation::Result<std::size_t>
  reset_render_setting_scope(const render::RenderSettingScope &scope);
  [[nodiscard]] operation::Result<render::ResolvedRenderSetting>
  resolve_render_setting(std::string_view name,
                         const render::RenderSettingContext &context) const;
  [[nodiscard]] render::RenderSettingSnapshot
  render_setting_snapshot() const;
  [[nodiscard]] std::optional<operation::Error>
  restore_render_settings(const render::RenderSettingSnapshot &snapshot);

  // SEQ-311 color system. Assignments target the active object's selection;
  // `auto` consumes the shared object/auto color cycle, `atomic` applies the
  // reference element colors and `default` restores the MolShredder palette.
  [[nodiscard]] const render::ColorTable &color_table() const noexcept {
    return color_table_;
  }
  [[nodiscard]] operation::Result<ColorAssignResult>
  assign_color(std::string_view color, std::string selection_expression,
               ObjectScope scope = ObjectScope::active);
  [[nodiscard]] operation::Result<ColorDefineResult>
  define_color(std::string_view name, std::array<double, 3> components);
  [[nodiscard]] operation::Result<ColorQueryResult>
  query_color(std::string_view color) const;
  [[nodiscard]] operation::Result<std::size_t> recolor();
  // PyMOL-compatible spectrum: built-in palettes with simple atom fields use
  // named ramp colors; other expressions or explicit color lists interpolate
  // literal colors. Targets the active object's selection.
  [[nodiscard]] operation::Result<SpectrumResult>
  spectrum(const SpectrumRequest &request);
  // `default` restores the MolShredder background; other tokens follow the
  // `color` assignment lookup and must resolve to an RGB color.
  [[nodiscard]] operation::Result<BackgroundResult>
  set_background(std::string_view color);
  [[nodiscard]] BackgroundResult background() const;
  // Writes the visible scene with the deterministic reference ray caster as
  // an RGBA PNG. PyMOL ray/draw backends are explicit fallbacks reported in
  // warnings; text labels are not rasterized.
  [[nodiscard]] operation::Result<ImageSaveResult>
  save_image(const ImageSaveRequest &request) const;
  // PyMOL `cartoon TYPE, SELECTION`: exact or unique-prefix type name.
  [[nodiscard]] operation::Result<CartoonTypeResult>
  set_cartoon_type(std::string_view type, std::string selection_expression);
  // PyMOL ramp_new: source is a volume (map value) or molecular object
  // (proximity); range is a level list; colors are names or one of the
  // built-in spectra (traditional sludge ocean hot grayable rainbow afmhot
  // grayscale). The ramp name then works as a `color` token.
  [[nodiscard]] operation::Result<RampResult>
  define_ramp(std::string name, std::string_view source,
              std::string_view range, std::string_view colors);
  // PyMOL ramp_update: replaces the range and/or colors of an existing ramp.
  [[nodiscard]] operation::Result<RampResult>
  update_ramp(std::string_view name, std::string_view range,
              std::string_view colors);
  // PyMOL isosurface/isomesh/isodot: creates or replaces a named contour of
  // a volume map; a name used by another mode is an error.
  [[nodiscard]] operation::Result<ContourResult>
  create_contour(const ContourRequest &request, operation::TaskContext &context);
  // PyMOL isolevel: updates (level) or queries (nullopt) a contour level.
  [[nodiscard]] operation::Result<ContourResult>
  contour_level(std::string_view name, std::optional<double> level,
                operation::TaskContext &context);
  [[nodiscard]] bool has_camera() const noexcept {
    return camera_.has_value();
  }
  // PyMOL `label`: evaluates a label expression per selected atom, stores
  // the text (empty clears) and shows the labels representation for the
  // selection. `literal` stores the text verbatim (PyMOL label2).
  [[nodiscard]] operation::Result<LabelResult>
  label(std::string selection_expression, std::string_view expression,
        bool literal = false);

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
  [[nodiscard]] std::optional<operation::Error>
  commit_color_table(render::ColorTable candidate);
  // Re-reads proximity ramp source atoms (state 1) after coordinate edits.
  void refresh_proximity_ramps(std::uint64_t object_id);
  // Merges sorted (AtomId, color) updates into the object's sparse atom
  // colors (kColorDefault removes an entry), rebuilds and commits atomically.
  [[nodiscard]] std::optional<operation::Error> commit_atom_colors(
      WorkspaceObject &object,
      std::vector<std::pair<std::uint64_t, std::int32_t>> updates);
  // Builds the merged atom colors and rebuilt representations without
  // committing them.
  struct AtomColorCandidate {
    std::shared_ptr<const std::vector<std::pair<std::uint64_t, std::int32_t>>>
        colors;
    std::vector<RepresentationRecord> representations;
  };
  [[nodiscard]] operation::Result<AtomColorCandidate> prepare_atom_colors(
      const WorkspaceObject &object,
      std::vector<std::pair<std::uint64_t, std::int32_t>> updates) const;
  // Targets of a selection-based display operation for the scope.
  [[nodiscard]] operation::Result<std::vector<WorkspaceObject *>>
  scope_targets(ObjectScope scope);
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
  struct GroupMember {
    // Exactly one of the two identifies the member.
    std::uint64_t object_id{};
    std::string group_name;
  };
  struct GroupRecord {
    std::string name;
    std::vector<GroupMember> members;
    bool open{true};
  };
  std::vector<GroupRecord> groups_;
  [[nodiscard]] bool group_name_exists(std::string_view name) const;
  // Replaces an object's coordinate states (rebuilds the embedded trajectory,
  // representations and scene entry). Used by append/delete of states.
  [[nodiscard]] operation::Result<std::size_t> replace_object_frames(
      std::size_t index,
      std::vector<std::shared_ptr<const model::CoordinateFrame>> frames,
      std::size_t current_frame);
  [[nodiscard]] bool object_name_in_use(std::string_view name) const;
  [[nodiscard]] const GroupRecord *find_group(std::string_view name) const;
  [[nodiscard]] const GroupRecord *parent_group_of_object(std::uint64_t id) const;
  [[nodiscard]] const GroupRecord *parent_group_of_group(std::string_view name) const;
  void collect_group_objects(const GroupRecord &group,
                             std::vector<std::uint64_t> &objects,
                             std::vector<std::string> &groups) const;
  [[nodiscard]] GroupInfo describe_group(const GroupRecord &group) const;
  [[nodiscard]] std::string member_name(const GroupMember &member) const;
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
  // Presentation order of the scenes (PyMOL scene_order); always a permutation
  // of the named_scenes_ keys.
  std::vector<std::string> scene_order_;
  std::optional<MovieTimelineState> movie_;
  render::RenderSettingStore render_settings_;
  render::ColorTable color_table_;
  std::size_t auto_color_cursor_{};
  std::uint64_t next_edit_transaction_id_{1U};
  std::size_t edit_history_memory_budget_bytes_{256U * 1024U * 1024U};
  std::size_t edit_history_memory_used_bytes_{};
  std::deque<EditRecord> undo_history_;
  std::deque<EditRecord> redo_history_;
  std::vector<command::Invocation> session_invocations_;
};

} // namespace molshredder::application
