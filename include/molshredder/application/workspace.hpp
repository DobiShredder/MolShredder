#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/analysis/basic.hpp"
#include "molshredder/analysis/contacts.hpp"
#include "molshredder/analysis/secondary_structure.hpp"
#include "molshredder/analysis/time_series.hpp"
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
#include "molshredder/render/volume_isosurface.hpp"
#include "molshredder/scene/scene.hpp"
#include "molshredder/selection/named_selection.hpp"
#include "molshredder/trajectory/frame_cache.hpp"
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
};

struct WorkspaceObject {
  std::uint64_t id{};
  scene::NodeId scene_node;
  std::shared_ptr<const model::MolecularSystem> system;
  selection::NamedSelections selections;
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

struct TrajectoryLoadResult {
  std::uint64_t object_id{};
  io::TrajectoryFormat format{io::TrajectoryFormat::auto_detect};
  std::size_t atom_count{};
  std::size_t frame_count{};
  std::size_t cache_budget_bytes{};
  std::size_t current_frame{};
  std::size_t prefetch_frame_count{};
  trajectory::PrefetchSnapshot prefetch;
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

struct ShowResult {
  std::uint64_t object_id{};
  std::size_t representation_index{};
  std::size_t primitive_count{};
  std::size_t selected_atom_count{};
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
  [[nodiscard]] std::optional<operation::Error>
  set_named_selection(std::string name, std::string expression, bool dynamic);
  [[nodiscard]] operation::Result<ShowResult>
  show(render::RepresentationKind kind, std::string selection_expression,
       bool replace_existing = false);
  [[nodiscard]] operation::Result<CenterAnalysisResult>
  analyze_center(std::string selection_expression, analysis::CenterMode mode);
  [[nodiscard]] operation::Result<DistanceMeasurementRecord> measure_distance(
      std::string from_expression, std::string to_expression,
      analysis::DistanceBoundary boundary = analysis::DistanceBoundary::raw);
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
      std::optional<std::string> h5md_particle_group = std::nullopt);
  [[nodiscard]] operation::Result<TrajectorySaveResult>
  save_active_trajectory_frame(const std::filesystem::path &path,
                               io::TrajectoryFormat format,
                               std::string title, bool overwrite,
                               operation::TaskContext &context) const;
  [[nodiscard]] operation::Result<TrajectoryFrameResult>
  set_trajectory_frame(std::size_t frame_index);
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

private:
  [[nodiscard]] WorkspaceObject *mutable_active_object() noexcept;
  [[nodiscard]] WorkspaceVolume *mutable_active_volume() noexcept;
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
};

} // namespace molshredder::application
