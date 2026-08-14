#include "molshredder/trajectory/playback.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::trajectory {

operation::Result<PlaybackClock> PlaybackClock::create(
    double frames_per_second, std::size_t maximum_transitions_per_tick) {
  if (!std::isfinite(frames_per_second) || frames_per_second <= 0.0) {
    return operation::Result<PlaybackClock>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "playback speed must be finite and positive", {}});
  }
  if (maximum_transitions_per_tick == 0U) {
    return operation::Result<PlaybackClock>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "maximum playback transitions per tick must be positive", {}});
  }
  return operation::Result<PlaybackClock>::success(
      PlaybackClock{frames_per_second, maximum_transitions_per_tick});
}

std::optional<operation::Error> PlaybackClock::set_frames_per_second(
    double frames_per_second) {
  if (!std::isfinite(frames_per_second) || frames_per_second <= 0.0) {
    return operation::Error{operation::ErrorCode::invalid_argument,
                            "playback speed must be finite and positive", {}};
  }
  frames_per_second_ = frames_per_second;
  return std::nullopt;
}

operation::Result<ClockAdvance> PlaybackClock::consume_elapsed_seconds(
    double elapsed_seconds) {
  if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0) {
    return operation::Result<ClockAdvance>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "playback elapsed time must be finite and non-negative", {}});
  }
  const auto produced = elapsed_seconds * frames_per_second_;
  constexpr double maximum_backlog = 1.0e9;
  if (!std::isfinite(produced) ||
      produced > maximum_backlog - pending_transitions_) {
    return operation::Result<ClockAdvance>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "playback elapsed time creates an excessive transition backlog",
        "send smaller periodic tick intervals"});
  }
  pending_transitions_ += produced;
  const auto available = std::floor(pending_transitions_);
  const auto limited = available >
                       static_cast<double>(maximum_transitions_per_tick_);
  const auto transitions = limited
                               ? maximum_transitions_per_tick_
                               : static_cast<std::size_t>(available);
  pending_transitions_ -= static_cast<double>(transitions);
  return operation::Result<ClockAdvance>::success(
      {transitions, pending_transitions_, limited});
}

operation::Result<PlaybackTimeline> PlaybackTimeline::create(
    std::size_t frame_count, PlaybackRange range, PlaybackMode mode,
    PlaybackDirection direction) {
  if (frame_count == 0U) {
    return operation::Result<PlaybackTimeline>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "playback timeline requires at least one frame", {}});
  }
  if (range.stride == 0U) {
    return operation::Result<PlaybackTimeline>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "playback stride must be positive", {}});
  }
  const auto last = range.last.value_or(frame_count - 1U);
  if (range.first >= frame_count || last >= frame_count ||
      range.first > last) {
    return operation::Result<PlaybackTimeline>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "playback range is outside the available frame count",
        "use first <= last < frame_count"});
  }
  std::vector<std::size_t> sequence;
  for (auto frame = range.first;;) {
    sequence.push_back(frame);
    if (range.stride > last - frame) break;
    frame += range.stride;
  }
  const auto position = direction == PlaybackDirection::forward
                            ? 0U
                            : sequence.size() - 1U;
  return operation::Result<PlaybackTimeline>::success(
      PlaybackTimeline{std::move(sequence), mode, direction, position});
}

std::optional<operation::Error> PlaybackTimeline::seek(std::size_t frame) {
  const auto found = std::lower_bound(sequence_.begin(), sequence_.end(), frame);
  if (found == sequence_.end() || *found != frame) {
    return operation::Error{
        operation::ErrorCode::invalid_argument,
        "frame " + std::to_string(frame) +
            " is not part of the playback range/stride sequence",
        "seek to one of the normalized playback sequence frames"};
  }
  position_ = static_cast<std::size_t>(found - sequence_.begin());
  return std::nullopt;
}

PlaybackTimeline::StepOutcome PlaybackTimeline::step_once() noexcept {
  if (sequence_.size() == 1U) {
    if (mode_ == PlaybackMode::once) playing_ = false;
    return StepOutcome::boundary_stopped;
  }
  if (direction_ == PlaybackDirection::forward) {
    if (position_ + 1U < sequence_.size()) {
      ++position_;
      return StepOutcome::moved;
    }
    if (mode_ == PlaybackMode::once) {
      playing_ = false;
    } else if (mode_ == PlaybackMode::loop) {
      position_ = 0U;
    } else {
      direction_ = PlaybackDirection::reverse;
      --position_;
    }
    return mode_ == PlaybackMode::once ? StepOutcome::boundary_stopped
                                       : StepOutcome::boundary_moved;
  }
  if (position_ > 0U) {
    --position_;
    return StepOutcome::moved;
  }
  if (mode_ == PlaybackMode::once) {
    playing_ = false;
  } else if (mode_ == PlaybackMode::loop) {
    position_ = sequence_.size() - 1U;
  } else {
    direction_ = PlaybackDirection::forward;
    ++position_;
  }
  return mode_ == PlaybackMode::once ? StepOutcome::boundary_stopped
                                     : StepOutcome::boundary_moved;
}

AdvanceResult PlaybackTimeline::advance(std::size_t transitions) noexcept {
  AdvanceResult result;
  for (std::size_t index = 0; index < transitions && playing_; ++index) {
    const auto outcome = step_once();
    if (outcome != StepOutcome::moved) ++result.boundary_crossings;
    if (outcome != StepOutcome::boundary_stopped) ++result.transitions;
  }
  result.snapshot = snapshot();
  return result;
}

std::vector<std::size_t> PlaybackTimeline::prefetch_hint(
    std::size_t count) const {
  auto copy = *this;
  copy.playing_ = true;
  std::vector<std::size_t> frames;
  frames.reserve(std::min(count, sequence_.size()));
  for (std::size_t index = 0; index < count; ++index) {
    const auto before = copy.position_;
    const auto direction = copy.direction_;
    const auto outcome = copy.step_once();
    if (outcome == StepOutcome::boundary_stopped) break;
    if (copy.position_ == before && copy.direction_ == direction) break;
    const auto frame = copy.sequence_[copy.position_];
    if (std::find(frames.begin(), frames.end(), frame) == frames.end()) {
      frames.push_back(frame);
    }
    if (!copy.playing_) break;
  }
  return frames;
}

PlaybackSnapshot PlaybackTimeline::snapshot() const noexcept {
  return PlaybackSnapshot{sequence_[position_], position_, sequence_.size(),
                          mode_, direction_, playing_};
}

}  // namespace molshredder::trajectory
