#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/trajectory/frame_cache.hpp"
#include "molshredder/trajectory/prefetch_scheduler.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

class BlockingSource final : public molshredder::model::CoordinateSource {
 public:
  BlockingSource() {
    for (std::size_t index = 0; index < frames_.size(); ++index) {
      frames_[index] = molshredder::model::CoordinateFrame::create(
                           molshredder::model::CoordinateBuffer{
                               std::vector<molshredder::model::Vec3f>{
                                   {static_cast<float>(index), 0.0F, 0.0F}}})
                           .value();
    }
  }

  [[nodiscard]] std::size_t atom_count() const noexcept override { return 1U; }
  [[nodiscard]] std::optional<std::size_t> frame_count() const
      noexcept override {
    return frames_.size();
  }
  [[nodiscard]] molshredder::model::FrameAccess access() const
      noexcept override {
    return molshredder::model::FrameAccess::random_access;
  }
  [[nodiscard]] molshredder::operation::Result<
      std::shared_ptr<const molshredder::model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override {
    if (frame_index >= frames_.size()) {
      return molshredder::operation::Result<
          std::shared_ptr<const molshredder::model::CoordinateFrame>>::failure(
          molshredder::operation::Error{
              molshredder::operation::ErrorCode::not_found,
              "blocking source frame is out of range", {}});
    }
    {
      std::unique_lock lock{mutex_};
      ++reads_[frame_index];
      if (frame_index == 0U && block_zero_) {
        zero_started_ = true;
        condition_.notify_all();
        condition_.wait(lock, [&] { return !block_zero_; });
      }
    }
    return molshredder::operation::Result<
        std::shared_ptr<const molshredder::model::CoordinateFrame>>::success(
        frames_[frame_index]);
  }

  void wait_for_zero() const {
    std::unique_lock lock{mutex_};
    condition_.wait(lock, [&] { return zero_started_; });
  }
  void release_zero() {
    std::lock_guard lock{mutex_};
    block_zero_ = false;
    condition_.notify_all();
  }
  [[nodiscard]] std::size_t reads(std::size_t index) const {
    std::lock_guard lock{mutex_};
    return reads_[index];
  }

 private:
  std::array<std::shared_ptr<const molshredder::model::CoordinateFrame>, 3>
      frames_;
  mutable std::mutex mutex_;
  mutable std::condition_variable condition_;
  mutable std::array<std::size_t, 3> reads_{};
  mutable bool zero_started_{};
  mutable bool block_zero_{true};
};

bool wait_for_state(
    const std::shared_ptr<molshredder::trajectory::PrefetchScheduler>& scheduler,
    std::uint64_t generation,
    molshredder::trajectory::PrefetchState expected) {
  for (std::size_t attempt = 0; attempt < 200U; ++attempt) {
    const auto snapshot = scheduler->snapshot();
    if (snapshot.generation == generation && snapshot.state == expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return false;
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  passed &= expect(!trajectory::PrefetchScheduler::create(nullptr).has_value(),
                   "prefetch scheduler must reject a null cache");
  auto source = std::make_shared<BlockingSource>();
  auto cache = trajectory::FrameCache::create(source, 1024U).value();
  auto scheduler = trajectory::PrefetchScheduler::create(cache).value();

  const auto first_generation = scheduler->schedule({0U, 1U});
  source->wait_for_zero();
  passed &= expect(
      scheduler->snapshot().generation == first_generation &&
          scheduler->snapshot().state == trajectory::PrefetchState::running,
      "scheduled request must enter running state");
  const auto replacement_generation = scheduler->schedule({2U});
  source->release_zero();
  passed &= expect(
      wait_for_state(scheduler, replacement_generation,
                     trajectory::PrefetchState::succeeded) &&
          source->reads(0U) == 1U && source->reads(1U) == 0U &&
          source->reads(2U) == 1U,
      "new generation must supersede remaining stale read-ahead");
  auto replacement = scheduler->snapshot();
  passed &= expect(replacement.frame_indices == std::vector<std::size_t>{2U} &&
                       replacement.completed_count == 1U &&
                       !replacement.error.has_value(),
                   "successful snapshot must preserve generation telemetry");

  const auto failure_generation = scheduler->schedule({99U});
  passed &= expect(
      wait_for_state(scheduler, failure_generation,
                     trajectory::PrefetchState::failed) &&
          scheduler->snapshot().error.has_value() &&
          scheduler->snapshot().error->message.starts_with(
              "trajectory prefetch frame 99 failed:"),
      "decode failure must be observable without terminating the worker");
  scheduler->cancel();
  passed &= expect(scheduler->snapshot().state ==
                           trajectory::PrefetchState::idle &&
                       scheduler->snapshot().frame_indices.empty(),
                   "cancel must advance generation and clear queued work");
  return passed ? 0 : 1;
}
