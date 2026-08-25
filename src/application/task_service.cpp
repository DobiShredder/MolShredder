#include "molshredder/application/task_service.hpp"

#include <algorithm>
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

}  // namespace molshredder::application
