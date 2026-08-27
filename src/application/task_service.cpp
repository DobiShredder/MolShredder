#include "molshredder/application/task_service.hpp"

#include "molshredder/application/default_registry.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <limits>
#include <string>
#include <utility>

namespace molshredder::application {
namespace {

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

std::size_t trajectory_candidate_reservation(const WorkspaceObject &object) {
  constexpr std::size_t per_atom_bytes = 1024U;
  constexpr std::size_t per_bond_bytes = 512U;
  constexpr std::size_t minimum_bytes = 1024U * 1024U;
  auto bytes = saturated_multiply(
      object.system->topology()->atom_count(), per_atom_bytes);
  bytes = saturated_add(
      bytes, saturated_multiply(object.system->topology()->bonds().size(),
                                per_bond_bytes));
  return std::max(bytes, minimum_bytes);
}

}  // namespace

operation::Result<std::uint64_t> schedule_structure_batch(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<operation::TaskScheduler> scheduler,
    ScheduledStructureBatchRequest request) {
  if (workspace == nullptr || scheduler == nullptr) {
    return operation::Result<std::uint64_t>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "scheduled structure batch requires a Workspace and scheduler", {}});
  }
  if (request.inputs.empty()) {
    return operation::Result<std::uint64_t>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "scheduled structure batch requires at least one input", {}});
  }
  auto inputs = std::make_shared<std::vector<StructureLoadRequest>>(
      std::move(request.inputs));
  operation::TaskRequest task;
  task.priority = request.priority;
  task.memory_bytes = request.memory_reservation_bytes;
  task.generation = request.generation;
  task.generation_is_current = std::move(request.generation_is_current);
  task.report_progress = std::move(request.report_progress);
  task.work =
      [workspace = std::move(workspace), inputs](operation::TaskContext& context)
      -> operation::Result<operation::TaskCommit> {
    auto documents = std::make_shared<std::vector<StructureDocumentLoadRequest>>();
    documents->reserve(inputs->size());
    for (std::size_t index = 0; index < inputs->size(); ++index) {
      if (context.cancellation.is_cancelled()) {
        return operation::Result<operation::TaskCommit>::failure(
            operation::Error{operation::ErrorCode::cancelled,
                             "scheduled structure parse was cancelled", {}});
      }
      const auto& input = (*inputs)[index];
      auto document = io::read_structure_file(input.path, input.format);
      if (!document.has_value()) {
        auto error = document.error();
        error.message = "scheduled structure input " +
                        std::to_string(index + 1U) + " failed: " +
                        error.message;
        error.details["input_index"] = std::to_string(index);
        error.details["path"] = input.path.string();
        return operation::Result<operation::TaskCommit>::failure(
            std::move(error));
      }
      documents->push_back(
          {std::move(document.value()), input.path, input.name});
      if (context.report_progress) {
        context.report_progress(
            {0.8 * static_cast<double>(index + 1U) /
                 static_cast<double>(inputs->size()),
             "structure-batch-parse"});
      }
    }
    const auto cancellation = context.cancellation;
    const auto progress = context.report_progress;
    return operation::Result<operation::TaskCommit>::success(
        [workspace, documents, cancellation,
         progress]() mutable -> std::optional<operation::Error> {
          operation::TaskContext commit_context{cancellation, progress};
          auto result = workspace->load_structure_documents(
              std::move(*documents), commit_context);
          if (!result.has_value()) return result.error();
          return std::nullopt;
        });
  };
  return scheduler->submit(std::move(task));
}

operation::Result<TrajectoryFrameResult>
ScheduledTrajectoryFrameCompletion::result() const {
  std::lock_guard lock{mutex_};
  if (!result_.has_value())
    return operation::Result<TrajectoryFrameResult>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "scheduled trajectory frame has not committed", {}});
  return *result_;
}

void ScheduledTrajectoryFrameCompletion::publish(
    operation::Result<TrajectoryFrameResult> result) {
  std::lock_guard lock{mutex_};
  result_ = std::move(result);
}

operation::Result<ScheduledTrajectoryFrame> schedule_trajectory_frame(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<operation::TaskScheduler> scheduler,
    ScheduledTrajectoryFrameRequest request) {
  if (workspace == nullptr || scheduler == nullptr)
    return operation::Result<ScheduledTrajectoryFrame>::failure(
        operation::Error{operation::ErrorCode::invalid_argument,
                         "scheduled trajectory frame requires a Workspace and "
                         "scheduler",
                         {}});
  auto plan = workspace->plan_trajectory_frame(request.frame_index);
  if (!plan.has_value())
    return operation::Result<ScheduledTrajectoryFrame>::failure(plan.error());
  const auto reservation =
      request.memory_reservation_bytes == 0U
          ? trajectory_candidate_reservation(plan.value().object)
          : request.memory_reservation_bytes;
  auto plan_holder =
      std::make_shared<TrajectoryFramePlan>(std::move(plan.value()));
  auto completion = std::make_shared<ScheduledTrajectoryFrameCompletion>();
  operation::TaskRequest task;
  task.priority = request.priority;
  task.memory_bytes = reservation;
  task.generation = request.generation;
  task.generation_is_current = std::move(request.generation_is_current);
  task.report_progress = std::move(request.report_progress);
  task.work =
      [workspace, plan_holder,
       completion](operation::TaskContext &context)
      -> operation::Result<operation::TaskCommit> {
    auto candidate = Workspace::build_trajectory_frame_candidate(
        std::move(*plan_holder), context);
    if (!candidate.has_value())
      return operation::Result<operation::TaskCommit>::failure(
          candidate.error());
    auto candidate_holder = std::make_shared<TrajectoryFrameCandidate>(
        std::move(candidate.value()));
    return operation::Result<operation::TaskCommit>::success(
        [workspace, candidate_holder,
         completion]() mutable -> std::optional<operation::Error> {
          auto committed = workspace->commit_trajectory_frame(
              std::move(*candidate_holder));
          if (!committed.has_value()) {
            const auto error = committed.error();
            completion->publish(
                operation::Result<TrajectoryFrameResult>::failure(error));
            return error;
          }
          completion->publish(
              operation::Result<TrajectoryFrameResult>::success(
                  committed.value()));
          return std::nullopt;
        });
  };
  const auto submitted = scheduler->submit(std::move(task));
  if (!submitted.has_value())
    return operation::Result<ScheduledTrajectoryFrame>::failure(
        submitted.error());
  return operation::Result<ScheduledTrajectoryFrame>::success(
      {submitted.value(), reservation, std::move(completion)});
}

operation::Result<TrajectoryLoadResult>
ScheduledTrajectoryLoadCompletion::result() const {
  std::lock_guard lock{mutex_};
  if (!result_.has_value())
    return operation::Result<TrajectoryLoadResult>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "scheduled trajectory load has not committed", {}});
  return *result_;
}

void ScheduledTrajectoryLoadCompletion::publish(
    operation::Result<TrajectoryLoadResult> result) {
  std::lock_guard lock{mutex_};
  result_ = std::move(result);
}

operation::Result<ScheduledTrajectoryLoad> schedule_trajectory_load(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<operation::TaskScheduler> scheduler,
    ScheduledTrajectoryLoadRequest request) {
  if (workspace == nullptr || scheduler == nullptr)
    return operation::Result<ScheduledTrajectoryLoad>::failure(
        operation::Error{operation::ErrorCode::invalid_argument,
                         "scheduled trajectory load requires a Workspace and "
                         "scheduler",
                         {}});
  auto plan = workspace->plan_trajectory_load(
      request.path, request.format, request.cache_budget_bytes,
      request.prefetch_frame_count, request.coordinate_unit,
      std::move(request.h5md_particle_group), request.mapping_policy,
      request.source_to_target_atom_ids, request.expected_topology_version);
  if (!plan.has_value())
    return operation::Result<ScheduledTrajectoryLoad>::failure(plan.error());
  const auto automatic_reservation = saturated_add(
      request.cache_budget_bytes,
      trajectory_candidate_reservation(plan.value().object));
  const auto reservation = request.memory_reservation_bytes == 0U
                               ? automatic_reservation
                               : request.memory_reservation_bytes;
  auto plan_holder =
      std::make_shared<TrajectoryLoadPlan>(std::move(plan.value()));
  auto completion = std::make_shared<ScheduledTrajectoryLoadCompletion>();
  operation::TaskRequest task;
  task.priority = request.priority;
  task.memory_bytes = reservation;
  task.generation = request.generation;
  task.generation_is_current = std::move(request.generation_is_current);
  task.report_progress = std::move(request.report_progress);
  task.work =
      [workspace, plan_holder,
       completion](operation::TaskContext &context)
      -> operation::Result<operation::TaskCommit> {
    auto candidate = Workspace::build_trajectory_load_candidate(
        std::move(*plan_holder), context);
    if (!candidate.has_value())
      return operation::Result<operation::TaskCommit>::failure(
          candidate.error());
    auto candidate_holder = std::make_shared<TrajectoryLoadCandidate>(
        std::move(candidate.value()));
    return operation::Result<operation::TaskCommit>::success(
        [workspace, candidate_holder,
         completion]() mutable -> std::optional<operation::Error> {
          auto committed = workspace->commit_trajectory_load(
              std::move(*candidate_holder));
          if (!committed.has_value()) {
            const auto error = committed.error();
            completion->publish(
                operation::Result<TrajectoryLoadResult>::failure(error));
            return error;
          }
          completion->publish(
              operation::Result<TrajectoryLoadResult>::success(
                  committed.value()));
          return std::nullopt;
        });
  };
  const auto submitted = scheduler->submit(std::move(task));
  if (!submitted.has_value())
    return operation::Result<ScheduledTrajectoryLoad>::failure(
        submitted.error());
  return operation::Result<ScheduledTrajectoryLoad>::success(
      {submitted.value(), reservation, std::move(completion)});
}

operation::Result<DirectVolumeResult>
ScheduledDirectVolumeCompletion::result() const {
  std::lock_guard lock{mutex_};
  if (!result_.has_value()) {
    return operation::Result<DirectVolumeResult>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "scheduled direct volume has not committed", {}});
  }
  return *result_;
}

void ScheduledDirectVolumeCompletion::publish(
    operation::Result<DirectVolumeResult> result) {
  std::lock_guard lock{mutex_};
  result_ = std::move(result);
}

operation::Result<ScheduledDirectVolume> schedule_direct_volume(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<operation::TaskScheduler> scheduler,
    ScheduledDirectVolumeRequest request) {
  if (workspace == nullptr || scheduler == nullptr) {
    return operation::Result<ScheduledDirectVolume>::failure(
        operation::Error{operation::ErrorCode::invalid_argument,
                         "scheduled direct volume requires a Workspace and "
                         "scheduler",
                         {}});
  }
  auto plan = workspace->plan_direct_volume(
      request.style, request.preset, request.replace_existing);
  if (!plan.has_value()) {
    return operation::Result<ScheduledDirectVolume>::failure(plan.error());
  }
  const auto reservation = request.memory_reservation_bytes == 0U
                               ? plan.value().required_texture_bytes
                               : request.memory_reservation_bytes;
  if (reservation < plan.value().required_texture_bytes) {
    return operation::Result<ScheduledDirectVolume>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "direct-volume task reservation is smaller than its texture payload",
        "increase the task reservation or reduce the volume size",
        {{"memory_requested_bytes", std::to_string(reservation)},
         {"memory_required_bytes",
          std::to_string(plan.value().required_texture_bytes)}}});
  }
  auto plan_holder =
      std::make_shared<DirectVolumePlan>(std::move(plan.value()));
  auto completion = std::make_shared<ScheduledDirectVolumeCompletion>();
  operation::TaskRequest task;
  task.priority = request.priority;
  task.memory_bytes = reservation;
  task.generation = request.generation;
  task.generation_is_current = std::move(request.generation_is_current);
  task.report_progress = std::move(request.report_progress);
  task.work =
      [workspace, plan_holder,
       completion](operation::TaskContext &context)
      -> operation::Result<operation::TaskCommit> {
    auto candidate = Workspace::build_direct_volume_candidate(
        std::move(*plan_holder), context);
    if (!candidate.has_value()) {
      return operation::Result<operation::TaskCommit>::failure(
          candidate.error());
    }
    auto candidate_holder = std::make_shared<DirectVolumeCandidate>(
        std::move(candidate.value()));
    return operation::Result<operation::TaskCommit>::success(
        [workspace, candidate_holder,
         completion]() mutable -> std::optional<operation::Error> {
          auto committed = workspace->commit_direct_volume(
              std::move(*candidate_holder));
          if (!committed.has_value()) {
            const auto error = committed.error();
            completion->publish(
                operation::Result<DirectVolumeResult>::failure(error));
            return error;
          }
          completion->publish(
              operation::Result<DirectVolumeResult>::success(
                  committed.value()));
          return std::nullopt;
        });
  };
  const auto submitted = scheduler->submit(std::move(task));
  if (!submitted.has_value()) {
    return operation::Result<ScheduledDirectVolume>::failure(
        submitted.error());
  }
  return operation::Result<ScheduledDirectVolume>::success(
      {submitted.value(), reservation, std::move(completion)});
}

operation::Result<command::Response> ScheduledAnalysisCompletion::result() const {
  std::lock_guard lock{mutex_};
  if (!result_.has_value()) {
    return operation::Result<command::Response>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "scheduled analysis has not committed", {}});
  }
  return *result_;
}

void ScheduledAnalysisCompletion::publish(
    operation::Result<command::Response> result) {
  std::lock_guard lock{mutex_};
  result_ = std::move(result);
}

operation::Result<ScheduledAnalysis> schedule_sasa_analysis(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<operation::TaskScheduler> scheduler,
    ScheduledSasaRequest request) {
  if (workspace == nullptr || scheduler == nullptr)
    return operation::Result<ScheduledAnalysis>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "scheduled SASA requires a Workspace and scheduler", {}});
  auto plan = workspace->plan_sasa_analysis(
      std::move(request.selection_expression), request.probe_radius_angstrom,
      request.samples_per_atom, request.evaluation_budget);
  if (!plan.has_value())
    return operation::Result<ScheduledAnalysis>::failure(plan.error());
  const auto automatic = std::max<std::size_t>(
      1024U * 1024U,
      saturated_multiply(plan.value().selected.size(),
                         sizeof(double) * 4U + sizeof(std::uint8_t)));
  const auto reservation = request.memory_reservation_bytes == 0U
                               ? automatic
                               : request.memory_reservation_bytes;
  if (reservation < automatic)
    return operation::Result<ScheduledAnalysis>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "SASA task reservation is smaller than its prepared input/result",
        "increase the analysis memory reservation"});
  auto plan_holder =
      std::make_shared<SasaAnalysisPlan>(std::move(plan.value()));
  auto arguments =
      std::make_shared<command::Arguments>(std::move(request.arguments));
  auto completion = std::make_shared<ScheduledAnalysisCompletion>();
  operation::TaskRequest task;
  task.priority = operation::TaskPriority::interactive;
  task.memory_bytes = reservation;
  task.generation = request.generation;
  task.generation_is_current = std::move(request.generation_is_current);
  task.report_progress = std::move(request.report_progress);
  task.work = [workspace, plan_holder, arguments, completion](
                  operation::TaskContext &context)
      -> operation::Result<operation::TaskCommit> {
    auto candidate = Workspace::build_sasa_analysis_candidate(
        std::move(*plan_holder), context);
    if (!candidate.has_value())
      return operation::Result<operation::TaskCommit>::failure(
          candidate.error());
    auto holder = std::make_shared<SasaAnalysisCandidate>(
        std::move(candidate.value()));
    return operation::Result<operation::TaskCommit>::success(
        [workspace, holder, arguments,
         completion]() mutable -> std::optional<operation::Error> {
          auto committed =
              workspace->commit_sasa_analysis(std::move(*holder));
          if (!committed.has_value()) {
            completion->publish(
                operation::Result<command::Response>::failure(
                    committed.error()));
            return committed.error();
          }
          auto persisted = persist_sasa_analysis(
              *workspace, *arguments, std::move(committed.value()));
          if (!persisted.has_value()) {
            completion->publish(
                operation::Result<command::Response>::failure(
                    persisted.error()));
            return persisted.error();
          }
          completion->publish(operation::Result<command::Response>::success(
              std::move(persisted.value())));
          return std::nullopt;
        });
  };
  const auto submitted = scheduler->submit(std::move(task));
  if (!submitted.has_value())
    return operation::Result<ScheduledAnalysis>::failure(submitted.error());
  return operation::Result<ScheduledAnalysis>::success(
      {submitted.value(), reservation, std::move(completion)});
}

operation::Result<ScheduledAnalysis> schedule_rdf_analysis(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<operation::TaskScheduler> scheduler,
    ScheduledRdfRequest request) {
  if (workspace == nullptr || scheduler == nullptr)
    return operation::Result<ScheduledAnalysis>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "scheduled RDF requires a Workspace and scheduler", {}});
  auto plan = workspace->plan_rdf_analysis(
      std::move(request.first_expression),
      std::move(request.second_expression), request.maximum_radius,
      request.bin_width, request.boundary, request.normalization,
      request.same_selection, request.evaluation_budget,
      request.distance_unit);
  if (!plan.has_value())
    return operation::Result<ScheduledAnalysis>::failure(plan.error());
  const auto atom_bytes = saturated_multiply(
      plan.value().first_selected.size(), sizeof(std::uint8_t) * 2U);
  const auto bin_ratio = request.maximum_radius / request.bin_width;
  const auto bin_count =
      request.bin_width > 0.0 && std::isfinite(bin_ratio) && bin_ratio > 0.0 &&
              bin_ratio <=
                  static_cast<double>(std::numeric_limits<std::size_t>::max())
          ? static_cast<std::size_t>(std::ceil(bin_ratio))
          : std::numeric_limits<std::size_t>::max();
  const auto automatic = std::max<std::size_t>(
      1024U * 1024U,
      saturated_add(atom_bytes,
                    saturated_multiply(bin_count,
                                       sizeof(analysis::RdfBin))));
  const auto reservation = request.memory_reservation_bytes == 0U
                               ? automatic
                               : request.memory_reservation_bytes;
  if (reservation < automatic)
    return operation::Result<ScheduledAnalysis>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "RDF task reservation is smaller than its prepared input/result",
        "increase the analysis memory reservation"});
  auto plan_holder = std::make_shared<RdfAnalysisPlan>(std::move(plan.value()));
  auto arguments =
      std::make_shared<command::Arguments>(std::move(request.arguments));
  auto completion = std::make_shared<ScheduledAnalysisCompletion>();
  operation::TaskRequest task;
  task.priority = operation::TaskPriority::interactive;
  task.memory_bytes = reservation;
  task.generation = request.generation;
  task.generation_is_current = std::move(request.generation_is_current);
  task.report_progress = std::move(request.report_progress);
  task.work = [workspace, plan_holder, arguments, completion](
                  operation::TaskContext &context)
      -> operation::Result<operation::TaskCommit> {
    auto candidate = Workspace::build_rdf_analysis_candidate(
        std::move(*plan_holder), context);
    if (!candidate.has_value())
      return operation::Result<operation::TaskCommit>::failure(
          candidate.error());
    auto holder =
        std::make_shared<RdfAnalysisCandidate>(std::move(candidate.value()));
    return operation::Result<operation::TaskCommit>::success(
        [workspace, holder, arguments,
         completion]() mutable -> std::optional<operation::Error> {
          auto committed = workspace->commit_rdf_analysis(std::move(*holder));
          if (!committed.has_value()) {
            completion->publish(operation::Result<command::Response>::failure(
                committed.error()));
            return committed.error();
          }
          auto persisted = persist_rdf_analysis(
              *workspace, *arguments, std::move(committed.value()));
          if (!persisted.has_value()) {
            completion->publish(operation::Result<command::Response>::failure(
                persisted.error()));
            return persisted.error();
          }
          completion->publish(operation::Result<command::Response>::success(
              std::move(persisted.value())));
          return std::nullopt;
        });
  };
  const auto submitted = scheduler->submit(std::move(task));
  if (!submitted.has_value())
    return operation::Result<ScheduledAnalysis>::failure(submitted.error());
  return operation::Result<ScheduledAnalysis>::success(
      {submitted.value(), reservation, std::move(completion)});
}

operation::Result<ScheduledAnalysis> schedule_rmsd_matrix_analysis(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<operation::TaskScheduler> scheduler,
    ScheduledRmsdMatrixRequest request) {
  if (workspace == nullptr || scheduler == nullptr)
    return operation::Result<ScheduledAnalysis>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "scheduled RMSD matrix requires a Workspace and scheduler", {}});
  auto plan = workspace->plan_rmsd_matrix_analysis(
      std::move(request.selection_expression),
      std::move(request.fit_selection_expression), request.range, request.fit,
      request.weight_mode, request.missing_atom_policy,
      request.frame_pair_budget);
  if (!plan.has_value())
    return operation::Result<ScheduledAnalysis>::failure(plan.error());
  const auto frame_count = request.range.last >= request.range.first &&
                                   request.range.stride > 0U
                               ? (request.range.last - request.range.first) /
                                         request.range.stride +
                                     1U
                               : std::numeric_limits<std::size_t>::max();
  const auto pair_count =
      frame_count == std::numeric_limits<std::size_t>::max()
          ? frame_count
          : saturated_multiply(frame_count, frame_count + 1U) / 2U;
  const auto automatic = std::max<std::size_t>(
      1024U * 1024U,
      saturated_add(
          saturated_multiply(plan.value().selected.size(),
                             sizeof(double) + sizeof(std::uint8_t) * 2U),
          saturated_multiply(pair_count, sizeof(analysis::RmsdMatrixCell))));
  const auto reservation = request.memory_reservation_bytes == 0U
                               ? automatic
                               : request.memory_reservation_bytes;
  if (reservation < automatic)
    return operation::Result<ScheduledAnalysis>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "RMSD matrix task reservation is smaller than its result matrix",
        "increase the analysis memory reservation or reduce the frame range"});
  auto plan_holder =
      std::make_shared<RmsdMatrixAnalysisPlan>(std::move(plan.value()));
  auto arguments =
      std::make_shared<command::Arguments>(std::move(request.arguments));
  auto completion = std::make_shared<ScheduledAnalysisCompletion>();
  operation::TaskRequest task;
  task.priority = operation::TaskPriority::interactive;
  task.memory_bytes = reservation;
  task.generation = request.generation;
  task.generation_is_current = std::move(request.generation_is_current);
  task.report_progress = std::move(request.report_progress);
  task.work = [workspace, plan_holder, arguments, completion](
                  operation::TaskContext &context)
      -> operation::Result<operation::TaskCommit> {
    auto candidate = Workspace::build_rmsd_matrix_analysis_candidate(
        std::move(*plan_holder), context);
    if (!candidate.has_value())
      return operation::Result<operation::TaskCommit>::failure(
          candidate.error());
    auto holder = std::make_shared<RmsdMatrixAnalysisCandidate>(
        std::move(candidate.value()));
    return operation::Result<operation::TaskCommit>::success(
        [workspace, holder, arguments,
         completion]() mutable -> std::optional<operation::Error> {
          auto committed =
              workspace->commit_rmsd_matrix_analysis(std::move(*holder));
          if (!committed.has_value()) {
            completion->publish(operation::Result<command::Response>::failure(
                committed.error()));
            return committed.error();
          }
          auto persisted = persist_rmsd_matrix_analysis(
              *workspace, *arguments, std::move(committed.value()));
          if (!persisted.has_value()) {
            completion->publish(operation::Result<command::Response>::failure(
                persisted.error()));
            return persisted.error();
          }
          completion->publish(operation::Result<command::Response>::success(
              std::move(persisted.value())));
          return std::nullopt;
        });
  };
  const auto submitted = scheduler->submit(std::move(task));
  if (!submitted.has_value())
    return operation::Result<ScheduledAnalysis>::failure(submitted.error());
  return operation::Result<ScheduledAnalysis>::success(
      {submitted.value(), reservation, std::move(completion)});
}

}  // namespace molshredder::application
