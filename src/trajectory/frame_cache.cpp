#include "molshredder/trajectory/frame_cache.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

#include "molshredder/model/topology.hpp"
#include "molshredder/operation/error.hpp"

namespace molshredder::trajectory {
namespace {

std::size_t saturated_add(std::size_t left, std::size_t right) noexcept {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    return std::numeric_limits<std::size_t>::max();
  }
  return left + right;
}

std::size_t saturated_multiply(std::size_t left,
                               std::size_t right) noexcept {
  if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
    return std::numeric_limits<std::size_t>::max();
  }
  return left * right;
}

std::size_t buffer_payload_bytes(const model::CoordinateBuffer& buffer) {
  return std::visit(
      [](const auto& values) {
        using Value = typename std::decay_t<decltype(values)>::value_type;
        return saturated_multiply(values.size(), sizeof(Value));
      },
      buffer.values());
}

std::size_t property_payload_bytes(const model::AtomProperty& property) {
  auto bytes = std::visit(
      [](const auto& values) {
        using Values = std::decay_t<decltype(values)>;
        if constexpr (std::is_same_v<Values, model::BooleanColumn>) {
          return values.values.size();
        } else if constexpr (std::is_same_v<Values,
                                            std::vector<std::string>>) {
          auto total = saturated_multiply(values.size(), sizeof(std::string));
          for (const auto& value : values) {
            total = saturated_add(total, value.capacity());
          }
          return total;
        } else {
          using Value = typename Values::value_type;
          return saturated_multiply(values.size(), sizeof(Value));
        }
      },
      property.values);
  if (property.metadata.unit.has_value()) {
    bytes = saturated_add(bytes, property.metadata.unit->capacity());
  }
  bytes = saturated_add(bytes, property.metadata.source.capacity());
  for (const auto& [name, value] : property.metadata.annotations) {
    bytes = saturated_add(bytes, name.capacity());
    bytes = saturated_add(bytes, value.capacity());
  }
  return bytes;
}

}  // namespace

std::size_t frame_payload_bytes(
    const model::CoordinateFrame& frame) noexcept {
  auto bytes = buffer_payload_bytes(frame.positions());
  if (frame.velocities().has_value()) {
    bytes = saturated_add(bytes,
                          buffer_payload_bytes(frame.velocities().value()));
  }
  bytes = saturated_add(bytes, frame.presence().size());
  for (const auto& [name, property] : frame.metadata().atom_properties) {
    bytes = saturated_add(bytes, name.capacity());
    bytes = saturated_add(bytes, property_payload_bytes(property));
  }
  for (const auto& [name, value] : frame.metadata().fields) {
    bytes = saturated_add(bytes, name.capacity());
    bytes = saturated_add(bytes, value.capacity());
  }
  return bytes;
}

operation::Result<std::shared_ptr<FrameCache>> FrameCache::create(
    std::shared_ptr<const model::CoordinateSource> source,
    std::size_t payload_budget_bytes) {
  if (source == nullptr) {
    return operation::Result<std::shared_ptr<FrameCache>>::failure(
        operation::Error{operation::ErrorCode::invalid_argument,
                         "frame cache requires a coordinate source", {}});
  }
  if (payload_budget_bytes == 0U) {
    return operation::Result<std::shared_ptr<FrameCache>>::failure(
        operation::Error{operation::ErrorCode::invalid_argument,
                         "frame cache payload budget must be positive", {}});
  }
  return operation::Result<std::shared_ptr<FrameCache>>::success(
      std::shared_ptr<FrameCache>(
          new FrameCache(std::move(source), payload_budget_bytes)));
}

void FrameCache::promote_locked(
    std::unordered_map<std::size_t, Entry>::iterator entry) const {
  recency_.erase(entry->second.recency);
  recency_.push_front(entry->first);
  entry->second.recency = recency_.begin();
}

void FrameCache::insert_locked(
    std::size_t frame_index,
    std::shared_ptr<const model::CoordinateFrame> frame,
    std::size_t payload_bytes) const {
  while (!recency_.empty() &&
         payload_bytes > payload_budget_bytes_ - resident_payload_bytes_) {
    const auto victim_index = recency_.back();
    const auto victim = entries_.find(victim_index);
    if (victim != entries_.end()) {
      resident_payload_bytes_ -= victim->second.payload_bytes;
      entries_.erase(victim);
      ++evictions_;
    }
    recency_.pop_back();
  }
  recency_.push_front(frame_index);
  entries_.emplace(frame_index,
                   Entry{std::move(frame), payload_bytes, recency_.begin()});
  resident_payload_bytes_ += payload_bytes;
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
FrameCache::read_frame(std::size_t frame_index) const {
  {
    std::lock_guard lock{mutex_};
    auto found = entries_.find(frame_index);
    if (found != entries_.end()) {
      ++hits_;
      promote_locked(found);
      return operation::Result<
          std::shared_ptr<const model::CoordinateFrame>>::success(
          found->second.frame);
    }
    ++misses_;
  }
  const auto decoded = source_->read_frame(frame_index);
  if (!decoded.has_value()) {
    return operation::Result<
        std::shared_ptr<const model::CoordinateFrame>>::failure(
        decoded.error());
  }
  const auto payload_bytes = frame_payload_bytes(*decoded.value());
  std::lock_guard lock{mutex_};
  auto raced = entries_.find(frame_index);
  if (raced != entries_.end()) {
    ++duplicate_decodes_;
    promote_locked(raced);
    return operation::Result<
        std::shared_ptr<const model::CoordinateFrame>>::success(
        raced->second.frame);
  }
  if (payload_bytes > payload_budget_bytes_) {
    ++oversized_bypasses_;
    return decoded;
  }
  insert_locked(frame_index, decoded.value(), payload_bytes);
  return decoded;
}

std::future<operation::Result<PrefetchResult>> FrameCache::prefetch_async(
    std::vector<std::size_t> frame_indices,
    operation::CancellationToken cancellation) {
  auto self = shared_from_this();
  return std::async(
      std::launch::async,
      [self = std::move(self), indices = std::move(frame_indices),
       cancellation]() mutable {
        PrefetchResult result{indices.size(), 0U};
        for (const auto index : indices) {
          if (cancellation.is_cancelled()) {
            return operation::Result<PrefetchResult>::failure(
                operation::Error{operation::ErrorCode::cancelled,
                                 "trajectory prefetch cancelled after " +
                                     std::to_string(result.completed_count) +
                                     " frames",
                                 {}});
          }
          const auto loaded = self->read_frame(index);
          if (!loaded.has_value()) {
            auto error = loaded.error();
            error.message = "trajectory prefetch frame " +
                            std::to_string(index) + " failed: " +
                            error.message;
            return operation::Result<PrefetchResult>::failure(
                std::move(error));
          }
          ++result.completed_count;
        }
        return operation::Result<PrefetchResult>::success(result);
      });
}

FrameCacheStats FrameCache::stats() const noexcept {
  std::lock_guard lock{mutex_};
  return FrameCacheStats{payload_budget_bytes_, resident_payload_bytes_,
                         entries_.size(), hits_, misses_, evictions_,
                         oversized_bypasses_, duplicate_decodes_};
}

void FrameCache::clear() noexcept {
  std::lock_guard lock{mutex_};
  entries_.clear();
  recency_.clear();
  resident_payload_bytes_ = 0U;
}

}  // namespace molshredder::trajectory
