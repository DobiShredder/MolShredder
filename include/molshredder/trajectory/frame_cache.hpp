#pragma once

#include <cstddef>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::trajectory {

struct FrameCacheStats {
  std::size_t payload_budget_bytes{};
  std::size_t resident_payload_bytes{};
  std::size_t resident_frame_count{};
  std::size_t hits{};
  std::size_t misses{};
  std::size_t evictions{};
  std::size_t oversized_bypasses{};
  std::size_t duplicate_decodes{};
};

struct PrefetchResult {
  std::size_t requested_count{};
  std::size_t completed_count{};
};

[[nodiscard]] std::size_t frame_payload_bytes(
    const model::CoordinateFrame& frame) noexcept;

class FrameCache final : public model::CoordinateSource,
                         public std::enable_shared_from_this<FrameCache> {
 public:
  [[nodiscard]] static operation::Result<std::shared_ptr<FrameCache>> create(
      std::shared_ptr<const model::CoordinateSource> source,
      std::size_t payload_budget_bytes);

  [[nodiscard]] std::size_t atom_count() const noexcept override {
    return source_->atom_count();
  }
  [[nodiscard]] std::optional<std::size_t> frame_count() const
      noexcept override {
    return source_->frame_count();
  }
  [[nodiscard]] model::FrameAccess access() const noexcept override {
    return source_->access();
  }
  [[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override;

  [[nodiscard]] std::future<operation::Result<PrefetchResult>> prefetch_async(
      std::vector<std::size_t> frame_indices,
      operation::CancellationToken cancellation);

  [[nodiscard]] FrameCacheStats stats() const noexcept;
  void clear() noexcept;

 private:
  struct Entry {
    std::shared_ptr<const model::CoordinateFrame> frame;
    std::size_t payload_bytes{};
    std::list<std::size_t>::iterator recency;
  };

  FrameCache(std::shared_ptr<const model::CoordinateSource> source,
             std::size_t payload_budget_bytes)
      : source_{std::move(source)},
        payload_budget_bytes_{payload_budget_bytes} {}

  void promote_locked(std::unordered_map<std::size_t, Entry>::iterator entry)
      const;
  void insert_locked(
      std::size_t frame_index,
      std::shared_ptr<const model::CoordinateFrame> frame,
      std::size_t payload_bytes) const;

  std::shared_ptr<const model::CoordinateSource> source_;
  std::size_t payload_budget_bytes_{};
  mutable std::mutex mutex_;
  mutable std::list<std::size_t> recency_;
  mutable std::unordered_map<std::size_t, Entry> entries_;
  mutable std::size_t resident_payload_bytes_{};
  mutable std::size_t hits_{};
  mutable std::size_t misses_{};
  mutable std::size_t evictions_{};
  mutable std::size_t oversized_bypasses_{};
  mutable std::size_t duplicate_decodes_{};
};

}  // namespace molshredder::trajectory
