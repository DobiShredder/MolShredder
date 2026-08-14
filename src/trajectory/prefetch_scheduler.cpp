#include "molshredder/trajectory/prefetch_scheduler.hpp"

#include <limits>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::trajectory {

operation::Result<std::shared_ptr<PrefetchScheduler>>
PrefetchScheduler::create(std::shared_ptr<FrameCache> cache) {
  if (cache == nullptr) {
    return operation::Result<std::shared_ptr<PrefetchScheduler>>::failure(
        operation::Error{operation::ErrorCode::invalid_argument,
                         "prefetch scheduler requires a frame cache", {}});
  }
  auto scheduler = std::shared_ptr<PrefetchScheduler>(
      new PrefetchScheduler(std::move(cache)));
  scheduler->worker_ = std::jthread(
      [scheduler_raw = scheduler.get()](std::stop_token stop) {
        scheduler_raw->run(stop);
      });
  return operation::Result<std::shared_ptr<PrefetchScheduler>>::success(
      std::move(scheduler));
}

PrefetchScheduler::~PrefetchScheduler() {
  worker_.request_stop();
  condition_.notify_all();
  if (worker_.joinable()) worker_.join();
}

std::uint64_t PrefetchScheduler::schedule(
    std::vector<std::size_t> frame_indices) {
  std::lock_guard lock{mutex_};
  generation_ = generation_ == std::numeric_limits<std::uint64_t>::max()
                    ? 1U
                    : generation_ + 1U;
  snapshot_ = PrefetchSnapshot{generation_,
                               frame_indices.empty() ? PrefetchState::idle
                                                     : PrefetchState::queued,
                               frame_indices, 0U, std::nullopt};
  if (frame_indices.empty()) {
    pending_.reset();
  } else {
    pending_ = Request{generation_, std::move(frame_indices)};
  }
  condition_.notify_all();
  return generation_;
}

void PrefetchScheduler::cancel() {
  static_cast<void>(schedule({}));
}

PrefetchSnapshot PrefetchScheduler::snapshot() const {
  std::lock_guard lock{mutex_};
  return snapshot_;
}

void PrefetchScheduler::run(std::stop_token stop) {
  while (!stop.stop_requested()) {
    Request request;
    {
      std::unique_lock lock{mutex_};
      condition_.wait(lock, stop,
                      [&] { return pending_.has_value(); });
      if (stop.stop_requested()) return;
      request = std::move(*pending_);
      pending_.reset();
      if (request.generation != generation_) continue;
      snapshot_.state = PrefetchState::running;
    }
    for (const auto frame_index : request.frame_indices) {
      {
        std::lock_guard lock{mutex_};
        if (request.generation != generation_) break;
      }
      const auto loaded = cache_->read_frame(frame_index);
      std::lock_guard lock{mutex_};
      if (request.generation != generation_) break;
      if (!loaded.has_value()) {
        auto error = loaded.error();
        error.message = "trajectory prefetch frame " +
                        std::to_string(frame_index) + " failed: " +
                        error.message;
        snapshot_.state = PrefetchState::failed;
        snapshot_.error = std::move(error);
        break;
      }
      ++snapshot_.completed_count;
      if (snapshot_.completed_count == snapshot_.frame_indices.size()) {
        snapshot_.state = PrefetchState::succeeded;
      }
    }
  }
}

std::string_view to_string(PrefetchState state) noexcept {
  switch (state) {
    case PrefetchState::idle: return "idle";
    case PrefetchState::queued: return "queued";
    case PrefetchState::running: return "running";
    case PrefetchState::succeeded: return "succeeded";
    case PrefetchState::failed: return "failed";
  }
  return "idle";
}

}  // namespace molshredder::trajectory
