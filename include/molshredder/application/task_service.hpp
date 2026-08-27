#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "molshredder/application/workspace.hpp"
#include "molshredder/command/registry.hpp"
#include "molshredder/operation/task_scheduler.hpp"

namespace molshredder::application {

struct ScheduledStructureBatchRequest {
  std::vector<StructureLoadRequest> inputs;
  operation::TaskPriority priority{operation::TaskPriority::interactive};
  std::size_t memory_reservation_bytes{};
  std::uint64_t generation{};
  std::function<bool(std::uint64_t)> generation_is_current;
  operation::ProgressCallback report_progress;
};

// Parse is performed on the bounded worker pool. The returned task becomes
// ready_to_commit after parsing; TaskScheduler::commit_ready must be called by
// the Workspace owner thread to build and publish the atomic candidate.
[[nodiscard]] operation::Result<std::uint64_t> schedule_structure_batch(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<operation::TaskScheduler> scheduler,
    ScheduledStructureBatchRequest request);

class ScheduledTrajectoryFrameCompletion {
 public:
  [[nodiscard]] operation::Result<TrajectoryFrameResult> result() const;

 private:
  friend operation::Result<struct ScheduledTrajectoryFrame>
  schedule_trajectory_frame(
      std::shared_ptr<Workspace>,
      std::shared_ptr<operation::TaskScheduler>,
      struct ScheduledTrajectoryFrameRequest);
  void publish(operation::Result<TrajectoryFrameResult> result);

  mutable std::mutex mutex_;
  std::optional<operation::Result<TrajectoryFrameResult>> result_;
};

struct ScheduledTrajectoryFrameRequest {
  std::size_t frame_index{};
  operation::TaskPriority priority{operation::TaskPriority::interactive};
  // Zero selects the service's conservative topology/representation estimate.
  std::size_t memory_reservation_bytes{};
  std::uint64_t generation{};
  std::function<bool(std::uint64_t)> generation_is_current;
  operation::ProgressCallback report_progress;
};

struct ScheduledTrajectoryFrame {
  std::uint64_t task_id{};
  std::size_t reserved_memory_bytes{};
  std::shared_ptr<ScheduledTrajectoryFrameCompletion> completion;
};

// Planning happens synchronously on the Workspace owner thread. Decode and
// representation candidate construction run on the bounded worker pool;
// TaskScheduler::commit_ready publishes on the owner thread.
[[nodiscard]] operation::Result<ScheduledTrajectoryFrame>
schedule_trajectory_frame(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<operation::TaskScheduler> scheduler,
    ScheduledTrajectoryFrameRequest request);

class ScheduledTrajectoryLoadCompletion {
 public:
  [[nodiscard]] operation::Result<TrajectoryLoadResult> result() const;
  void publish(operation::Result<TrajectoryLoadResult> result);

 private:
  mutable std::mutex mutex_;
  std::optional<operation::Result<TrajectoryLoadResult>> result_;
};

struct ScheduledTrajectoryLoadRequest {
  std::filesystem::path path;
  io::TrajectoryFormat format{io::TrajectoryFormat::auto_detect};
  std::size_t cache_budget_bytes{};
  std::size_t prefetch_frame_count{4U};
  std::optional<operation::LengthUnit> coordinate_unit;
  std::optional<std::string> h5md_particle_group;
  trajectory::AtomMappingPolicy mapping_policy{
      trajectory::AtomMappingPolicy::exact};
  std::vector<model::AtomId> source_to_target_atom_ids;
  std::optional<std::uint64_t> expected_topology_version;
  operation::TaskPriority priority{operation::TaskPriority::interactive};
  std::size_t memory_reservation_bytes{};
  std::uint64_t generation{};
  std::function<bool(std::uint64_t)> generation_is_current;
  operation::ProgressCallback report_progress;
};

struct ScheduledTrajectoryLoad {
  std::uint64_t task_id{};
  std::size_t reserved_memory_bytes{};
  std::shared_ptr<ScheduledTrajectoryLoadCompletion> completion;
};

[[nodiscard]] operation::Result<ScheduledTrajectoryLoad>
schedule_trajectory_load(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<operation::TaskScheduler> scheduler,
    ScheduledTrajectoryLoadRequest request);

class ScheduledDirectVolumeCompletion {
 public:
  [[nodiscard]] operation::Result<DirectVolumeResult> result() const;
  void publish(operation::Result<DirectVolumeResult> result);

 private:
  mutable std::mutex mutex_;
  std::optional<operation::Result<DirectVolumeResult>> result_;
};

struct ScheduledDirectVolumeRequest {
  render::DirectVolumeStyle style;
  std::optional<render::TransferPreset> preset;
  bool replace_existing{};
  operation::TaskPriority priority{operation::TaskPriority::interactive};
  // Zero reserves the exact prepared texture payload estimated by Workspace.
  std::size_t memory_reservation_bytes{};
  std::uint64_t generation{};
  std::function<bool(std::uint64_t)> generation_is_current;
  operation::ProgressCallback report_progress;
};

struct ScheduledDirectVolume {
  std::uint64_t task_id{};
  std::size_t reserved_memory_bytes{};
  std::shared_ptr<ScheduledDirectVolumeCompletion> completion;
};

// Planning snapshots immutable volume/ramp input on the Workspace owner
// thread. Preparation runs on a bounded worker; commit_ready performs the
// revision-checked publication on the owner thread.
[[nodiscard]] operation::Result<ScheduledDirectVolume>
schedule_direct_volume(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<operation::TaskScheduler> scheduler,
    ScheduledDirectVolumeRequest request);

class ScheduledAnalysisCompletion {
 public:
  [[nodiscard]] operation::Result<command::Response> result() const;
  void publish(operation::Result<command::Response> result);

 private:
  mutable std::mutex mutex_;
  std::optional<operation::Result<command::Response>> result_;
};

struct ScheduledAnalysis {
  std::uint64_t task_id{};
  std::size_t reserved_memory_bytes{};
  std::shared_ptr<ScheduledAnalysisCompletion> completion;
};

struct ScheduledSasaRequest {
  command::Arguments arguments;
  std::string selection_expression;
  double probe_radius_angstrom{};
  std::size_t samples_per_atom{};
  std::size_t evaluation_budget{};
  std::size_t memory_reservation_bytes{};
  std::uint64_t generation{};
  std::function<bool(std::uint64_t)> generation_is_current;
  operation::ProgressCallback report_progress;
};

struct ScheduledRdfRequest {
  command::Arguments arguments;
  std::string first_expression;
  std::string second_expression;
  double maximum_radius{};
  double bin_width{};
  analysis::DistanceBoundary boundary{analysis::DistanceBoundary::raw};
  analysis::RdfNormalization normalization{analysis::RdfNormalization::pair_count};
  bool same_selection{};
  std::uint64_t evaluation_budget{};
  operation::LengthUnit distance_unit{operation::LengthUnit::angstrom};
  std::size_t memory_reservation_bytes{};
  std::uint64_t generation{};
  std::function<bool(std::uint64_t)> generation_is_current;
  operation::ProgressCallback report_progress;
};

struct ScheduledRmsdMatrixRequest {
  command::Arguments arguments;
  std::string selection_expression;
  std::string fit_selection_expression;
  analysis::SeriesRange range;
  analysis::FitMode fit{analysis::FitMode::rigid};
  analysis::WeightMode weight_mode{analysis::WeightMode::uniform};
  analysis::MissingAtomPolicy missing_atom_policy{analysis::MissingAtomPolicy::error};
  std::uint64_t frame_pair_budget{};
  std::size_t memory_reservation_bytes{};
  std::uint64_t generation{};
  std::function<bool(std::uint64_t)> generation_is_current;
  operation::ProgressCallback report_progress;
};

[[nodiscard]] operation::Result<ScheduledAnalysis> schedule_sasa_analysis(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<operation::TaskScheduler> scheduler,
    ScheduledSasaRequest request);
[[nodiscard]] operation::Result<ScheduledAnalysis> schedule_rdf_analysis(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<operation::TaskScheduler> scheduler,
    ScheduledRdfRequest request);
[[nodiscard]] operation::Result<ScheduledAnalysis>
schedule_rmsd_matrix_analysis(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<operation::TaskScheduler> scheduler,
    ScheduledRmsdMatrixRequest request);

}  // namespace molshredder::application
