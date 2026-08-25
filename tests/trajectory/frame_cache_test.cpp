#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <string_view>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/trajectory/frame_cache.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

class CountingSource final : public molshredder::model::CoordinateSource {
 public:
  CountingSource() {
    for (std::size_t frame = 0; frame < frames_.size(); ++frame) {
      auto created = molshredder::model::CoordinateFrame::create(
          molshredder::model::CoordinateBuffer{
              std::vector<molshredder::model::Vec3f>{
                  {static_cast<float>(frame), 0.0F, 0.0F},
                  {static_cast<float>(frame), 1.0F, 0.0F}}});
      frames_[frame] = created.value();
    }
  }

  [[nodiscard]] std::size_t atom_count() const noexcept override { return 2U; }
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
              "counting frame is out of range", {}});
    }
    ++reads_[frame_index];
    return molshredder::operation::Result<
        std::shared_ptr<const molshredder::model::CoordinateFrame>>::success(
        frames_[frame_index]);
  }

  [[nodiscard]] std::size_t reads(std::size_t index) const {
    return reads_[index].load();
  }

 private:
  std::array<std::shared_ptr<const molshredder::model::CoordinateFrame>, 3>
      frames_;
  mutable std::array<std::atomic_size_t, 3> reads_{};
};

class BlockingCountingSource final
    : public molshredder::model::CoordinateSource {
 public:
  BlockingCountingSource()
      : frame_{molshredder::model::CoordinateFrame::create(
                   molshredder::model::CoordinateBuffer{
                       std::vector<molshredder::model::Vec3f>{{1.0F, 0.0F,
                                                               0.0F}}})
                   .value()} {}

  [[nodiscard]] std::size_t atom_count() const noexcept override { return 1U; }
  [[nodiscard]] std::optional<std::size_t> frame_count() const
      noexcept override {
    return 1U;
  }
  [[nodiscard]] molshredder::model::FrameAccess access() const
      noexcept override {
    return molshredder::model::FrameAccess::random_access;
  }
  [[nodiscard]] molshredder::operation::Result<
      std::shared_ptr<const molshredder::model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override {
    if (frame_index != 0U)
      return molshredder::operation::Result<
          std::shared_ptr<const molshredder::model::CoordinateFrame>>::failure(
          {molshredder::operation::ErrorCode::not_found,
           "blocking frame is out of range", {}});
    std::unique_lock lock{mutex_};
    ++reads_;
    started_ = true;
    condition_.notify_all();
    condition_.wait(lock, [&] { return released_; });
    return molshredder::operation::Result<
        std::shared_ptr<const molshredder::model::CoordinateFrame>>::success(
        frame_);
  }

  void wait_started() const {
    std::unique_lock lock{mutex_};
    condition_.wait(lock, [&] { return started_; });
  }
  void release() {
    std::lock_guard lock{mutex_};
    released_ = true;
    condition_.notify_all();
  }
  [[nodiscard]] std::size_t reads() const {
    std::lock_guard lock{mutex_};
    return reads_;
  }

 private:
  std::shared_ptr<const molshredder::model::CoordinateFrame> frame_;
  mutable std::mutex mutex_;
  mutable std::condition_variable condition_;
  mutable std::size_t reads_{};
  mutable bool started_{};
  bool released_{};
};

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  auto source = std::make_shared<CountingSource>();
  const auto sample = source->read_frame(0U);
  const auto payload = trajectory::frame_payload_bytes(*sample.value());
  passed &= expect(payload == 26U,
                   "two float32 positions and presence bytes must be counted");

  passed &= expect(
      !trajectory::FrameCache::create(nullptr, payload).has_value() &&
          !trajectory::FrameCache::create(source, 0U).has_value() &&
          !trajectory::FrameCache::create(source, payload, 0U).has_value(),
      "cache must reject null source and zero resource bounds");
  const auto cache_result = trajectory::FrameCache::create(source, payload * 2U);
  auto cache = cache_result.value();
  const auto frame0 = cache->read_frame(0U);
  const auto frame1 = cache->read_frame(1U);
  const auto frame0_hit = cache->read_frame(0U);
  const auto frame2 = cache->read_frame(2U);
  auto stats = cache->stats();
  passed &= expect(
      frame0.has_value() && frame1.has_value() && frame0_hit.has_value() &&
          frame2.has_value() && source->reads(0U) == 2U &&
          source->reads(1U) == 1U && source->reads(2U) == 1U &&
          frame0.value() == frame0_hit.value() && stats.hits == 1U &&
          stats.misses == 3U && stats.evictions == 1U &&
          stats.resident_frame_count == 2U &&
          stats.resident_payload_bytes == payload * 2U,
      "LRU cache must hit, promote and evict within its payload budget");

  const auto frame1_again = cache->read_frame(1U);
  stats = cache->stats();
  const auto* retained = std::get_if<std::vector<model::Vec3f>>(
      &frame0.value()->positions().values());
  passed &= expect(
      frame1_again.has_value() && source->reads(1U) == 2U &&
          stats.evictions == 2U && retained != nullptr &&
          (*retained)[0].x == 0.0F,
      "eviction must not invalidate an external immutable frame lease");

  cache->clear();
  stats = cache->stats();
  passed &= expect(stats.resident_frame_count == 0U &&
                       stats.resident_payload_bytes == 0U &&
                       stats.hits == 1U && stats.misses == 4U,
                   "clear must release residents while preserving telemetry");

  const auto small_result = trajectory::FrameCache::create(source, payload - 1U);
  auto small = small_result.value();
  passed &= expect(small->read_frame(0U).has_value() &&
                       small->read_frame(0U).has_value() &&
                       small->stats().resident_frame_count == 0U &&
                       small->stats().oversized_bypasses == 2U,
                   "a frame larger than the budget must bypass caching");

  auto prefetch_source = std::make_shared<CountingSource>();
  auto prefetch_cache =
      trajectory::FrameCache::create(prefetch_source, payload * 2U).value();
  operation::CancellationToken active;
  auto future = prefetch_cache->prefetch_async({0U, 1U}, active);
  const auto prefetched = future.get();
  passed &= expect(prefetched.has_value() &&
                       prefetched.value().requested_count == 2U &&
                       prefetched.value().completed_count == 2U &&
                       prefetch_cache->read_frame(0U).has_value() &&
                       prefetch_source->reads(0U) == 1U &&
                       prefetch_cache->stats().hits == 1U,
                   "async prefetch must populate frames for later cache hits");

  operation::CancellationToken cancelled;
  cancelled.request_cancel();
  auto cancelled_future = prefetch_cache->prefetch_async({2U}, cancelled);
  const auto cancelled_result = cancelled_future.get();
  passed &= expect(!cancelled_result.has_value() &&
                       cancelled_result.error().code ==
                           operation::ErrorCode::cancelled &&
                       prefetch_source->reads(2U) == 0U,
                   "pre-cancelled prefetch must perform no decode");

  operation::CancellationToken failure_token;
  auto failure_future = prefetch_cache->prefetch_async({99U}, failure_token);
  const auto failure = failure_future.get();
  passed &= expect(!failure.has_value() &&
                       failure.error().message.starts_with(
                           "trajectory prefetch frame 99 failed:"),
                   "prefetch decode error must identify the frame index");

  auto blocking_source = std::make_shared<BlockingCountingSource>();
  auto coalescing_cache =
      trajectory::FrameCache::create(blocking_source, 1024U, 1U).value();
  auto first_read = std::async(std::launch::async,
                               [&] { return coalescing_cache->read_frame(0U); });
  blocking_source->wait_started();
  auto second_read = std::async(
      std::launch::async, [&] { return coalescing_cache->read_frame(0U); });
  const auto over_limit = coalescing_cache->read_frame(1U);
  bool joined{};
  for (std::size_t attempt = 0; attempt < 500U; ++attempt) {
    if (coalescing_cache->stats().coalesced_waits == 1U) {
      joined = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  blocking_source->release();
  const auto first_coalesced = first_read.get();
  const auto second_coalesced = second_read.get();
  const auto coalesced_stats = coalescing_cache->stats();
  passed &= expect(
      joined && first_coalesced.has_value() && second_coalesced.has_value() &&
          !over_limit.has_value() &&
          over_limit.error().code == operation::ErrorCode::resource_exhausted &&
          first_coalesced.value() == second_coalesced.value() &&
          blocking_source->reads() == 1U &&
          coalesced_stats.coalesced_waits == 1U &&
          coalesced_stats.in_flight_count == 0U &&
          coalesced_stats.duplicate_decodes == 0U,
      "same-frame misses must coalesce while distinct decodes obey the "
      "in-flight limit");

  return passed ? 0 : 1;
}
