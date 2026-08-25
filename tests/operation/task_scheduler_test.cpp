#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "molshredder/operation/task_scheduler.hpp"

namespace {

using namespace std::chrono_literals;
using molshredder::operation::Error;
using molshredder::operation::ErrorCode;
using molshredder::operation::Result;
using molshredder::operation::TaskCommit;
using molshredder::operation::TaskContext;
using molshredder::operation::TaskPriority;
using molshredder::operation::TaskRequest;
using molshredder::operation::TaskScheduler;
using molshredder::operation::TaskState;

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

bool wait_for_state(const std::shared_ptr<TaskScheduler>& scheduler,
                    std::uint64_t id, TaskState state) {
  for (std::size_t attempt = 0; attempt < 500U; ++attempt) {
    const auto snapshot = scheduler->snapshot(id);
    if (snapshot.has_value() && snapshot.value().state == state) return true;
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

TaskRequest recording_task(TaskPriority priority, std::size_t memory,
                           std::string label, std::vector<std::string>* order,
                           std::mutex* order_mutex) {
  return TaskRequest{
      priority, memory, 1U, {},
      [label = std::move(label), order, order_mutex](TaskContext&) {
        {
          std::lock_guard lock{*order_mutex};
          order->push_back(label);
        }
        return Result<TaskCommit>::success(
            []() -> std::optional<Error> { return std::nullopt; });
      },
      {}};
}

}  // namespace

int main() {
  using namespace molshredder::operation;
  bool passed = true;
  passed &= expect(!TaskScheduler::create({0U, 1U, 1U, 1U}).has_value(),
                   "zero worker count must be rejected");

  auto scheduler = TaskScheduler::create({1U, 3U, 10U, 2U}).value();
  std::mutex gate_mutex;
  std::condition_variable gate_condition;
  bool release{};
  std::atomic_bool started{};
  std::atomic<double> observed_progress{};
  const auto blocker = scheduler->submit(TaskRequest{
      TaskPriority::normal, 3U, 1U, {},
      [&](TaskContext& context) {
        started = true;
        context.report_progress({0.25, "decode"});
        std::unique_lock lock{gate_mutex};
        gate_condition.wait(lock, [&] {
          return release || context.cancellation.is_cancelled();
        });
        if (context.cancellation.is_cancelled()) {
          return Result<TaskCommit>::failure(
              {ErrorCode::cancelled, "blocked task cancelled", {}});
        }
        return Result<TaskCommit>::success([] { return std::nullopt; });
      },
      [&](const auto& update) { observed_progress.store(update.fraction); }});
  const auto progress_observed = [&] {
    for (std::size_t attempt = 0; attempt < 500U; ++attempt) {
      const auto snapshot = scheduler->snapshot(blocker.value());
      if (snapshot.has_value() &&
          snapshot.value().progress_fraction == 0.25 &&
          observed_progress.load() == 0.25)
        return true;
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }();
  passed &= expect(blocker.has_value() &&
                       wait_for_state(scheduler, blocker.value(),
                                      TaskState::running) &&
                       started && progress_observed,
                   "worker and frontend must observe running progress");

  std::vector<std::string> order;
  std::mutex order_mutex;
  const auto background = scheduler->submit(recording_task(
      TaskPriority::background, 2U, "background", &order, &order_mutex));
  const auto interactive_one = scheduler->submit(recording_task(
      TaskPriority::interactive, 1U, "interactive-1", &order, &order_mutex));
  const auto interactive_two = scheduler->submit(recording_task(
      TaskPriority::interactive, 1U, "interactive-2", &order, &order_mutex));
  const auto queue_rejected = scheduler->submit(recording_task(
      TaskPriority::normal, 1U, "overflow", &order, &order_mutex));
  passed &= expect(background.has_value() && interactive_one.has_value() &&
                       interactive_two.has_value() &&
                       !queue_rejected.has_value() &&
                       queue_rejected.error().code ==
                           ErrorCode::resource_exhausted,
                   "bounded queue must reject excess work with a typed error");
  passed &= expect(scheduler->scheduler_snapshot().reserved_memory_bytes == 7U,
                   "queued and running tasks must reserve memory");
  {
    std::lock_guard lock{gate_mutex};
    release = true;
  }
  gate_condition.notify_all();
  passed &= expect(wait_for_state(scheduler, blocker.value(),
                                  TaskState::ready_to_commit) &&
                       wait_for_state(scheduler, background.value(),
                                      TaskState::ready_to_commit) &&
                       wait_for_state(scheduler, interactive_one.value(),
                                      TaskState::ready_to_commit) &&
                       wait_for_state(scheduler, interactive_two.value(),
                                      TaskState::ready_to_commit),
                   "worker products must wait at the owner-thread commit boundary");
  passed &= expect(order == std::vector<std::string>{
                                "interactive-1", "interactive-2", "background"},
                   "priority must be deterministic and bounded by starvation limit");
  passed &= expect(!scheduler->commit_ready(blocker.value()).has_value() &&
                       !scheduler->commit_ready(interactive_one.value()).has_value() &&
                       !scheduler->commit_ready(interactive_two.value()).has_value() &&
                       !scheduler->commit_ready(background.value()).has_value() &&
                       scheduler->wait(background.value(), 2s).has_value(),
                   "owner thread must explicitly publish ready candidates");
  passed &= expect(scheduler->scheduler_snapshot().reserved_memory_bytes == 0U,
                   "successful tasks must release every reservation");

  const auto too_large = scheduler->submit(recording_task(
      TaskPriority::normal, 11U, "large", &order, &order_mutex));
  passed &= expect(!too_large.has_value() &&
                       too_large.error().code == ErrorCode::resource_exhausted,
                   "memory budget rejection must be typed");

  bool stale_committed{};
  std::atomic_uint64_t current_generation{7U};
  const auto stale = scheduler->submit(TaskRequest{
      TaskPriority::normal, 4U, 7U,
      [&](std::uint64_t generation) {
        return generation == current_generation.load();
      },
      [&](TaskContext&) {
        return Result<TaskCommit>::success(
            [&]() -> std::optional<Error> {
              stale_committed = true;
              return std::nullopt;
            });
      },
      {}});
  passed &= expect(wait_for_state(scheduler, stale.value(),
                                  TaskState::ready_to_commit),
                   "current generation product must reach the commit boundary");
  current_generation = 8U;
  passed &= expect(!scheduler->commit_ready(stale.value()).has_value(),
                   "stale commit discard is a handled completion");
  const auto stale_done = scheduler->wait(stale.value(), 2s);
  passed &= expect(stale_done.has_value() &&
                       stale_done.value().state == TaskState::stale &&
                       stale_done.value().error->code == ErrorCode::stale_result &&
                       !stale_committed &&
                       scheduler->scheduler_snapshot().reserved_memory_bytes == 0U,
                   "stale generation must discard commit and release memory");

  std::mutex cancel_mutex;
  std::condition_variable cancel_condition;
  bool cancellation_started{};
  const auto cancellable = scheduler->submit(TaskRequest{
      TaskPriority::normal, 5U, 9U, {},
      [&](TaskContext& context) {
        std::unique_lock lock{cancel_mutex};
        cancellation_started = true;
        cancel_condition.notify_all();
        cancel_condition.wait(lock,
                              [&] { return context.cancellation.is_cancelled(); });
        return Result<TaskCommit>::failure(
            {ErrorCode::cancelled, "cooperative cancellation", {}});
      },
      {}});
  {
    std::unique_lock lock{cancel_mutex};
    cancel_condition.wait_for(lock, 2s,
                              [&] { return cancellation_started; });
  }
  passed &= expect(scheduler->cancel(999999U).has_value(),
                   "unknown task cancellation must be reported");
  passed &= expect(!scheduler->cancel(cancellable.value()).has_value(),
                   "known task cancellation request must succeed");
  cancel_condition.notify_all();
  const auto cancelled = scheduler->wait(cancellable.value(), 2s);
  passed &= expect(cancelled.has_value() &&
                       cancelled.value().state == TaskState::cancelled &&
                       cancelled.value().error->code == ErrorCode::cancelled &&
                       scheduler->scheduler_snapshot().reserved_memory_bytes == 0U,
                   "cooperative cancellation must be terminal and release memory");

  std::mutex late_mutex;
  std::condition_variable late_condition;
  bool late_started{};
  bool late_release{};
  bool late_committed{};
  const auto late_return = scheduler->submit(TaskRequest{
      TaskPriority::interactive, 5U, 10U, {},
      [&](TaskContext&) {
        std::unique_lock lock{late_mutex};
        late_started = true;
        late_condition.notify_all();
        late_condition.wait(lock, [&] { return late_release; });
        return Result<TaskCommit>::success(
            [&]() -> std::optional<Error> {
              late_committed = true;
              return std::nullopt;
            });
      },
      {}});
  {
    std::unique_lock lock{late_mutex};
    late_condition.wait_for(lock, 2s, [&] { return late_started; });
  }
  passed &= expect(!scheduler->cancel(late_return.value()).has_value(),
                   "running non-cooperative work must accept cancellation");
  {
    std::lock_guard lock{late_mutex};
    late_release = true;
  }
  late_condition.notify_all();
  const auto late_cancelled = scheduler->wait(late_return.value(), 2s);
  passed &= expect(
      late_cancelled.has_value() &&
          late_cancelled.value().state == TaskState::cancelled &&
          !late_committed &&
          scheduler->scheduler_snapshot().reserved_memory_bytes == 0U,
      "late decoder return after cancellation must be discarded before commit");

  const auto failed = scheduler->submit(TaskRequest{
      TaskPriority::normal, 6U, 11U, {},
      [](TaskContext&) {
        return Result<TaskCommit>::failure(
            {ErrorCode::invalid_argument, "fixture failure", {}});
      },
      {}});
  const auto failure = scheduler->wait(failed.value(), 2s);
  passed &= expect(failure.has_value() &&
                       failure.value().state == TaskState::failed &&
                       scheduler->scheduler_snapshot().reserved_memory_bytes == 0U,
                   "failure must preserve error and release reservation");

  const auto commit_failure = scheduler->submit(TaskRequest{
      TaskPriority::normal, 2U, 12U, {},
      [](TaskContext&) {
        return Result<TaskCommit>::success([]() -> std::optional<Error> {
          return Error{ErrorCode::invalid_argument, "commit fixture failure",
                       {}};
        });
      },
      {}});
  passed &= expect(wait_for_state(scheduler, commit_failure.value(),
                                  TaskState::ready_to_commit) &&
                       !scheduler->commit_ready(commit_failure.value()).has_value(),
                   "commit failure fixture must reach the owner boundary");
  const auto commit_failed = scheduler->wait(commit_failure.value(), 2s);
  passed &= expect(commit_failed.has_value() &&
                       commit_failed.value().state == TaskState::failed &&
                       commit_failed.value().error->message ==
                           "commit fixture failure" &&
                       scheduler->scheduler_snapshot().reserved_memory_bytes == 0U,
                   "commit failure must release reservation without success");

  const auto ready_cancel = scheduler->submit(recording_task(
      TaskPriority::normal, 2U, "ready-cancel", &order, &order_mutex));
  passed &= expect(wait_for_state(scheduler, ready_cancel.value(),
                                  TaskState::ready_to_commit) &&
                       !scheduler->cancel(ready_cancel.value()).has_value(),
                   "ready candidate cancellation must discard it immediately");
  const auto ready_cancelled = scheduler->wait(ready_cancel.value(), 2s);
  passed &= expect(ready_cancelled.has_value() &&
                       ready_cancelled.value().state == TaskState::cancelled &&
                       scheduler->scheduler_snapshot().reserved_memory_bytes == 0U,
                   "cancelled ready candidate must never publish and must release memory");
  return passed ? 0 : 1;
}
