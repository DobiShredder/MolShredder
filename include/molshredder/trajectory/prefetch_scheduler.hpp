#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "molshredder/operation/result.hpp"
#include "molshredder/trajectory/frame_cache.hpp"

namespace molshredder::trajectory {

enum class PrefetchState { idle, queued, running, succeeded, failed };

struct PrefetchSnapshot {
  std::uint64_t generation{};
  PrefetchState state{PrefetchState::idle};
  std::vector<std::size_t> frame_indices;
  std::size_t completed_count{};
  std::optional<operation::Error> error;
};

class PrefetchScheduler {
 public:
  [[nodiscard]] static operation::Result<std::shared_ptr<PrefetchScheduler>>
  create(std::shared_ptr<FrameCache> cache);

  PrefetchScheduler(const PrefetchScheduler&) = delete;
  PrefetchScheduler& operator=(const PrefetchScheduler&) = delete;
  ~PrefetchScheduler();

  [[nodiscard]] std::uint64_t schedule(
      std::vector<std::size_t> frame_indices);
  void cancel();
  [[nodiscard]] PrefetchSnapshot snapshot() const;

 private:
  explicit PrefetchScheduler(std::shared_ptr<FrameCache> cache)
      : cache_{std::move(cache)} {}

  struct Request {
    std::uint64_t generation{};
    std::vector<std::size_t> frame_indices;
  };

  void run();

  std::shared_ptr<FrameCache> cache_;
  mutable std::mutex mutex_;
  std::condition_variable_any condition_;
  std::optional<Request> pending_;
  PrefetchSnapshot snapshot_;
  std::uint64_t generation_{};
  std::thread worker_;
  bool stopping_{};
};

[[nodiscard]] std::string_view to_string(PrefetchState state) noexcept;

}  // namespace molshredder::trajectory
