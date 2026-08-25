#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::operation {

enum class TaskPriority { interactive, normal, background };
enum class TaskState {
  queued,
  running,
  ready_to_commit,
  committing,
  succeeded,
  failed,
  cancelled,
  stale
};

using TaskCommit = std::function<std::optional<Error>()>;
using TaskWork = std::function<Result<TaskCommit>(TaskContext&)>;

struct TaskSchedulerConfig {
  std::size_t worker_count{1U};
  std::size_t queue_capacity{64U};
  std::size_t memory_budget_bytes{256U * 1024U * 1024U};
  // A queued task becomes the next candidate after this many bypasses by a
  // higher-priority task. This gives deterministic priority without starvation.
  std::size_t priority_bypass_limit{4U};
  std::size_t completed_history_capacity{256U};
};

struct TaskRequest {
  TaskPriority priority{TaskPriority::normal};
  std::size_t memory_bytes{};
  std::uint64_t generation{};
  std::function<bool(std::uint64_t)> generation_is_current;
  TaskWork work;
  ProgressCallback report_progress;
};

struct TaskSnapshot {
  std::uint64_t task_id{};
  TaskPriority priority{TaskPriority::normal};
  TaskState state{TaskState::queued};
  std::size_t memory_bytes{};
  std::uint64_t generation{};
  double progress_fraction{};
  std::string progress_stage;
  std::optional<Error> error;
};

struct TaskSchedulerSnapshot {
  std::size_t worker_count{};
  std::size_t queued_count{};
  std::size_t running_count{};
  std::size_t reserved_memory_bytes{};
  std::size_t memory_budget_bytes{};
};

class TaskScheduler {
 public:
  [[nodiscard]] static Result<std::shared_ptr<TaskScheduler>> create(
      TaskSchedulerConfig config);

  TaskScheduler(const TaskScheduler&) = delete;
  TaskScheduler& operator=(const TaskScheduler&) = delete;
  ~TaskScheduler();

  [[nodiscard]] Result<std::uint64_t> submit(TaskRequest request);
  [[nodiscard]] std::optional<Error> cancel(std::uint64_t task_id);
  // The Workspace/UI owner calls this on its mutation thread after observing
  // ready_to_commit. Worker threads never publish a candidate themselves.
  [[nodiscard]] std::optional<Error> commit_ready(std::uint64_t task_id);
  [[nodiscard]] Result<TaskSnapshot> snapshot(std::uint64_t task_id) const;
  [[nodiscard]] Result<TaskSnapshot> wait(
      std::uint64_t task_id,
      std::chrono::milliseconds timeout = std::chrono::milliseconds::max())
      const;
  [[nodiscard]] TaskSchedulerSnapshot scheduler_snapshot() const;

 private:
  explicit TaskScheduler(TaskSchedulerConfig config)
      : config_{config} {}

  struct TaskRecord;
  void run(std::stop_token stop);
  [[nodiscard]] std::shared_ptr<TaskRecord> take_next_locked();
  void finish(const std::shared_ptr<TaskRecord>& record, TaskState state,
              std::optional<Error> error, bool release_running = true);
  void prune_completed_locked(
      const std::shared_ptr<TaskRecord>& preserve = {});

  TaskSchedulerConfig config_;
  mutable std::mutex mutex_;
  mutable std::condition_variable_any condition_;
  std::vector<std::shared_ptr<TaskRecord>> records_;
  std::vector<std::shared_ptr<TaskRecord>> queue_;
  std::vector<std::jthread> workers_;
  std::uint64_t next_task_id_{1U};
  std::size_t running_count_{};
  std::size_t reserved_memory_bytes_{};
  bool stopping_{};
};

[[nodiscard]] std::string_view to_string(TaskPriority priority) noexcept;
[[nodiscard]] std::string_view to_string(TaskState state) noexcept;

}  // namespace molshredder::operation
