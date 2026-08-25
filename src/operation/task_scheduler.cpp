#include "molshredder/operation/task_scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace molshredder::operation {

struct TaskScheduler::TaskRecord {
  TaskSnapshot snapshot;
  std::size_t bypass_count{};
  CancellationToken cancellation;
  TaskWork work;
  std::function<bool(std::uint64_t)> generation_is_current;
  ProgressCallback external_progress;
  TaskCommit commit;
  bool reservation_released{};
};

namespace {

bool terminal(TaskState state) {
  return state == TaskState::succeeded || state == TaskState::failed ||
         state == TaskState::cancelled || state == TaskState::stale;
}

Error cancelled_error(std::uint64_t task_id) {
  return {ErrorCode::cancelled,
          "task " + std::to_string(task_id) + " was cancelled", {}};
}

}  // namespace

Result<std::shared_ptr<TaskScheduler>> TaskScheduler::create(
    TaskSchedulerConfig config) {
  if (config.worker_count == 0U || config.queue_capacity == 0U ||
      config.memory_budget_bytes == 0U ||
      config.priority_bypass_limit == 0U ||
      config.completed_history_capacity == 0U) {
    return Result<std::shared_ptr<TaskScheduler>>::failure(
        {ErrorCode::invalid_argument,
         "task scheduler limits must all be greater than zero", {}});
  }
  auto scheduler = std::shared_ptr<TaskScheduler>(
      new TaskScheduler(config));
  scheduler->workers_.reserve(config.worker_count);
  for (std::size_t index = 0; index < config.worker_count; ++index) {
    scheduler->workers_.emplace_back(
        [raw = scheduler.get()](std::stop_token stop) { raw->run(stop); });
  }
  return Result<std::shared_ptr<TaskScheduler>>::success(std::move(scheduler));
}

TaskScheduler::~TaskScheduler() {
  {
    std::lock_guard lock{mutex_};
    stopping_ = true;
    for (const auto& record : records_) record->cancellation.request_cancel();
  }
  for (auto& worker : workers_) worker.request_stop();
  condition_.notify_all();
  for (auto& worker : workers_) {
    if (worker.joinable()) worker.join();
  }
}

Result<std::uint64_t> TaskScheduler::submit(TaskRequest request) {
  if (!request.work) {
    return Result<std::uint64_t>::failure(
        {ErrorCode::invalid_argument, "scheduled task requires work", {}});
  }
  std::lock_guard lock{mutex_};
  if (stopping_) {
    return Result<std::uint64_t>::failure(
        {ErrorCode::cancelled, "task scheduler is stopping", {}});
  }
  prune_completed_locked();
  if (queue_.size() >= config_.queue_capacity) {
    return Result<std::uint64_t>::failure(
        {ErrorCode::resource_exhausted,
         "task queue capacity is exhausted",
         "wait for queued work to complete before submitting again",
         {{"queue_capacity", std::to_string(config_.queue_capacity)}}});
  }
  if (request.memory_bytes > config_.memory_budget_bytes ||
      request.memory_bytes >
          config_.memory_budget_bytes - reserved_memory_bytes_) {
    return Result<std::uint64_t>::failure(
        {ErrorCode::resource_exhausted,
         "task memory reservation exceeds the scheduler budget",
         "reduce the task memory request or wait for existing work",
         {{"memory_budget_bytes",
           std::to_string(config_.memory_budget_bytes)},
          {"memory_reserved_bytes", std::to_string(reserved_memory_bytes_)},
          {"memory_requested_bytes", std::to_string(request.memory_bytes)}}});
  }
  if (next_task_id_ == 0U) {
    return Result<std::uint64_t>::failure(
        {ErrorCode::resource_exhausted, "task identifier space is exhausted",
         {}});
  }
  auto record = std::make_shared<TaskRecord>();
  record->snapshot = TaskSnapshot{next_task_id_, request.priority,
                                  TaskState::queued, request.memory_bytes,
                                  request.generation, 0.0, "queued",
                                  std::nullopt};
  record->work = std::move(request.work);
  record->generation_is_current =
      std::move(request.generation_is_current);
  record->external_progress = std::move(request.report_progress);
  records_.push_back(record);
  queue_.push_back(record);
  reserved_memory_bytes_ += request.memory_bytes;
  const auto task_id = next_task_id_;
  next_task_id_ = next_task_id_ == std::numeric_limits<std::uint64_t>::max()
                      ? 0U
                      : next_task_id_ + 1U;
  condition_.notify_all();
  return Result<std::uint64_t>::success(task_id);
}

std::optional<Error> TaskScheduler::cancel(std::uint64_t task_id) {
  std::shared_ptr<TaskRecord> record;
  bool finish_immediately{};
  {
    std::lock_guard lock{mutex_};
    const auto found = std::find_if(records_.begin(), records_.end(),
                                    [task_id](const auto& candidate) {
                                      return candidate->snapshot.task_id ==
                                             task_id;
                                    });
    if (found == records_.end()) {
      return Error{ErrorCode::not_found,
                   "scheduled task does not exist: " +
                       std::to_string(task_id),
                   {}};
    }
    record = *found;
    if (terminal(record->snapshot.state)) return std::nullopt;
    record->cancellation.request_cancel();
    if (record->snapshot.state == TaskState::queued) {
      std::erase(queue_, record);
      finish_immediately = true;
    } else if (record->snapshot.state == TaskState::ready_to_commit) {
      record->commit = {};
      finish_immediately = true;
    }
  }
  if (finish_immediately) {
    finish(record, TaskState::cancelled, cancelled_error(task_id), false);
    return std::nullopt;
  }
  condition_.notify_all();
  return std::nullopt;
}

std::optional<Error> TaskScheduler::commit_ready(std::uint64_t task_id) {
  std::shared_ptr<TaskRecord> record;
  {
    std::lock_guard lock{mutex_};
    const auto found = std::find_if(records_.begin(), records_.end(),
                                    [task_id](const auto& candidate) {
                                      return candidate->snapshot.task_id ==
                                             task_id;
                                    });
    if (found == records_.end()) {
      return Error{ErrorCode::not_found,
                   "scheduled task does not exist: " +
                       std::to_string(task_id),
                   {}};
    }
    record = *found;
    if (record->snapshot.state != TaskState::ready_to_commit) {
      return Error{ErrorCode::invalid_argument,
                   "task is not ready to commit: " +
                       std::string{to_string(record->snapshot.state)},
                   "wait for ready_to_commit before publishing the result"};
    }
    record->snapshot.state = TaskState::committing;
    record->snapshot.progress_stage = "committing";
  }
  if (record->cancellation.is_cancelled()) {
    finish(record, TaskState::cancelled, cancelled_error(task_id), false);
    return std::nullopt;
  }
  bool generation_current{true};
  try {
    if (record->generation_is_current) {
      generation_current =
          record->generation_is_current(record->snapshot.generation);
    }
  } catch (const std::exception& exception) {
    finish(record, TaskState::failed,
           Error{ErrorCode::internal,
                 "generation validation threw an exception: " +
                     std::string{exception.what()},
                 {}},
           false);
    return std::nullopt;
  } catch (...) {
    finish(record, TaskState::failed,
           Error{ErrorCode::internal,
                 "generation validation threw an unknown exception", {}},
           false);
    return std::nullopt;
  }
  if (!generation_current) {
    finish(record, TaskState::stale,
           Error{ErrorCode::stale_result,
                 "task completion generation became stale before commit", {},
                 {{"generation", std::to_string(record->snapshot.generation)}}},
           false);
    return std::nullopt;
  }
  std::optional<Error> commit_error;
  try {
    if (record->commit) commit_error = record->commit();
  } catch (const std::exception& exception) {
    commit_error = Error{ErrorCode::internal,
                         "task commit threw an exception: " +
                             std::string{exception.what()},
                         {}};
  } catch (...) {
    commit_error = Error{ErrorCode::internal,
                         "task commit threw an unknown exception", {}};
  }
  if (commit_error.has_value()) {
    finish(record, TaskState::failed, std::move(commit_error), false);
  } else {
    finish(record, TaskState::succeeded, std::nullopt, false);
  }
  return std::nullopt;
}

Result<TaskSnapshot> TaskScheduler::snapshot(std::uint64_t task_id) const {
  std::lock_guard lock{mutex_};
  const auto found = std::find_if(records_.begin(), records_.end(),
                                  [task_id](const auto& record) {
                                    return record->snapshot.task_id == task_id;
                                  });
  if (found == records_.end()) {
    return Result<TaskSnapshot>::failure(
        {ErrorCode::not_found,
         "scheduled task does not exist: " + std::to_string(task_id), {}});
  }
  return Result<TaskSnapshot>::success((*found)->snapshot);
}

Result<TaskSnapshot> TaskScheduler::wait(
    std::uint64_t task_id, std::chrono::milliseconds timeout) const {
  std::unique_lock lock{mutex_};
  const auto locate = [&]() {
    return std::find_if(records_.begin(), records_.end(),
                        [task_id](const auto& record) {
                          return record->snapshot.task_id == task_id;
                        });
  };
  auto found = locate();
  if (found == records_.end()) {
    return Result<TaskSnapshot>::failure(
        {ErrorCode::not_found,
         "scheduled task does not exist: " + std::to_string(task_id), {}});
  }
  const auto ready = [&] { return terminal((*locate())->snapshot.state); };
  const bool completed = timeout == std::chrono::milliseconds::max()
                             ? (condition_.wait(lock, ready), true)
                             : condition_.wait_for(lock, timeout, ready);
  if (!completed) {
    return Result<TaskSnapshot>::failure(
        {ErrorCode::resource_exhausted,
         "timed out waiting for task " + std::to_string(task_id),
         "poll the task snapshot or wait with a longer timeout"});
  }
  found = locate();
  return Result<TaskSnapshot>::success((*found)->snapshot);
}

TaskSchedulerSnapshot TaskScheduler::scheduler_snapshot() const {
  std::lock_guard lock{mutex_};
  return {workers_.size(), queue_.size(), running_count_,
          reserved_memory_bytes_, config_.memory_budget_bytes};
}

std::shared_ptr<TaskScheduler::TaskRecord>
TaskScheduler::take_next_locked() {
  if (queue_.empty()) return {};
  auto selected = queue_.begin();
  const auto starved = std::find_if(queue_.begin(), queue_.end(),
                                    [&](const auto& record) {
                                      return record->bypass_count >=
                                             config_.priority_bypass_limit;
                                    });
  if (starved != queue_.end()) {
    selected = starved;
  } else {
    selected = std::min_element(
        queue_.begin(), queue_.end(), [](const auto& first, const auto& second) {
          return first->snapshot.priority < second->snapshot.priority;
        });
  }
  for (auto iterator = queue_.begin(); iterator != queue_.end(); ++iterator) {
    if (iterator != selected &&
        (*iterator)->snapshot.priority > (*selected)->snapshot.priority) {
      ++(*iterator)->bypass_count;
    }
  }
  auto record = *selected;
  queue_.erase(selected);
  record->snapshot.state = TaskState::running;
  record->snapshot.progress_stage = "running";
  ++running_count_;
  return record;
}

void TaskScheduler::finish(const std::shared_ptr<TaskRecord>& record,
                           TaskState state, std::optional<Error> error,
                           bool release_running) {
  std::lock_guard lock{mutex_};
  record->snapshot.state = state;
  record->snapshot.error = std::move(error);
  if (state == TaskState::succeeded) {
    record->snapshot.progress_fraction = 1.0;
    record->snapshot.progress_stage = "complete";
  } else {
    record->snapshot.progress_stage = std::string{to_string(state)};
  }
  if (!record->reservation_released) {
    reserved_memory_bytes_ -= record->snapshot.memory_bytes;
    record->reservation_released = true;
  }
  if (release_running) --running_count_;
  prune_completed_locked(record);
  condition_.notify_all();
}

void TaskScheduler::prune_completed_locked(
    const std::shared_ptr<TaskRecord>& preserve) {
  std::size_t terminal_count = static_cast<std::size_t>(std::count_if(
      records_.begin(), records_.end(),
      [](const auto& record) { return terminal(record->snapshot.state); }));
  auto iterator = records_.begin();
  while (terminal_count > config_.completed_history_capacity &&
         iterator != records_.end()) {
    if (*iterator != preserve && terminal((*iterator)->snapshot.state)) {
      iterator = records_.erase(iterator);
      --terminal_count;
    } else {
      ++iterator;
    }
  }
}

void TaskScheduler::run(std::stop_token stop) {
  while (!stop.stop_requested()) {
    std::shared_ptr<TaskRecord> record;
    {
      std::unique_lock lock{mutex_};
      condition_.wait(lock, stop,
                      [&] { return stopping_ || !queue_.empty(); });
      if (stop.stop_requested() || stopping_) return;
      record = take_next_locked();
    }
    if (!record) continue;
    if (record->cancellation.is_cancelled()) {
      finish(record, TaskState::cancelled,
             cancelled_error(record->snapshot.task_id));
      continue;
    }
    TaskContext context{
        record->cancellation,
        [this, weak = std::weak_ptr<TaskRecord>{record}](
            const ProgressUpdate& update) {
          const auto active = weak.lock();
          if (!active) return;
          ProgressCallback external;
          ProgressUpdate forwarded;
          std::string stage;
          {
            std::lock_guard lock{mutex_};
            if (terminal(active->snapshot.state)) return;
            active->snapshot.progress_fraction =
                std::clamp(update.fraction, 0.0, 1.0);
            active->snapshot.progress_stage = update.stage;
            stage = active->snapshot.progress_stage;
            forwarded = {active->snapshot.progress_fraction, stage};
            external = active->external_progress;
          }
          if (external) external(forwarded);
        }};
    Result<TaskCommit> product = Result<TaskCommit>::failure(
        {ErrorCode::internal, "scheduled task did not run", {}});
    try {
      product = record->work(context);
    } catch (const std::exception& exception) {
      product = Result<TaskCommit>::failure(
          {ErrorCode::internal,
           "scheduled task threw an exception: " +
               std::string{exception.what()},
           {}});
    } catch (...) {
      product = Result<TaskCommit>::failure(
          {ErrorCode::internal, "scheduled task threw an unknown exception",
           {}});
    }
    if (record->cancellation.is_cancelled()) {
      finish(record, TaskState::cancelled,
             cancelled_error(record->snapshot.task_id));
      continue;
    }
    if (!product.has_value()) {
      finish(record, TaskState::failed, product.error());
      continue;
    }
    bool generation_current{true};
    try {
      if (record->generation_is_current) {
        generation_current =
            record->generation_is_current(record->snapshot.generation);
      }
    } catch (const std::exception& exception) {
      finish(record, TaskState::failed,
             Error{ErrorCode::internal,
                   "generation validation threw an exception: " +
                       std::string{exception.what()},
                   {}});
      continue;
    } catch (...) {
      finish(record, TaskState::failed,
             Error{ErrorCode::internal,
                   "generation validation threw an unknown exception", {}});
      continue;
    }
    if (!generation_current) {
      finish(record, TaskState::stale,
             Error{ErrorCode::stale_result,
                   "task completion generation is stale", {},
                   {{"generation",
                     std::to_string(record->snapshot.generation)}}});
      continue;
    }
    {
      std::lock_guard lock{mutex_};
      record->commit = std::move(product.value());
      record->snapshot.state = TaskState::ready_to_commit;
      record->snapshot.progress_stage = "ready-to-commit";
      --running_count_;
      condition_.notify_all();
    }
  }
}

std::string_view to_string(TaskPriority priority) noexcept {
  switch (priority) {
    case TaskPriority::interactive: return "interactive";
    case TaskPriority::normal: return "normal";
    case TaskPriority::background: return "background";
  }
  return "normal";
}

std::string_view to_string(TaskState state) noexcept {
  switch (state) {
    case TaskState::queued: return "queued";
    case TaskState::running: return "running";
    case TaskState::ready_to_commit: return "ready_to_commit";
    case TaskState::committing: return "committing";
    case TaskState::succeeded: return "succeeded";
    case TaskState::failed: return "failed";
    case TaskState::cancelled: return "cancelled";
    case TaskState::stale: return "stale";
  }
  return "failed";
}

}  // namespace molshredder::operation
