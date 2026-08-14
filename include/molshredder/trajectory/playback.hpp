#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "molshredder/operation/result.hpp"

namespace molshredder::trajectory {

enum class PlaybackMode { once, loop, rock };
enum class PlaybackDirection { forward, reverse };

struct PlaybackRange {
  std::size_t first{};
  std::optional<std::size_t> last{};
  std::size_t stride{1U};
};

struct PlaybackSnapshot {
  std::size_t frame{};
  std::size_t sequence_position{};
  std::size_t sequence_size{};
  PlaybackMode mode{PlaybackMode::once};
  PlaybackDirection direction{PlaybackDirection::forward};
  bool playing{};
};

struct AdvanceResult {
  PlaybackSnapshot snapshot;
  std::size_t transitions{};
  std::size_t boundary_crossings{};
};

struct ClockAdvance {
  std::size_t transitions{};
  double pending_fractional_transitions{};
  bool catch_up_limited{};
};

class PlaybackClock {
 public:
  [[nodiscard]] static operation::Result<PlaybackClock> create(
      double frames_per_second = 30.0,
      std::size_t maximum_transitions_per_tick = 120U);

  [[nodiscard]] std::optional<operation::Error> set_frames_per_second(
      double frames_per_second);
  void reset() noexcept { pending_transitions_ = 0.0; }
  [[nodiscard]] operation::Result<ClockAdvance> consume_elapsed_seconds(
      double elapsed_seconds);

  [[nodiscard]] double frames_per_second() const noexcept {
    return frames_per_second_;
  }
  [[nodiscard]] std::size_t maximum_transitions_per_tick() const noexcept {
    return maximum_transitions_per_tick_;
  }
  [[nodiscard]] double pending_transitions() const noexcept {
    return pending_transitions_;
  }

 private:
  PlaybackClock(double frames_per_second,
                std::size_t maximum_transitions_per_tick)
      : frames_per_second_{frames_per_second},
        maximum_transitions_per_tick_{maximum_transitions_per_tick} {}

  double frames_per_second_{30.0};
  std::size_t maximum_transitions_per_tick_{120U};
  double pending_transitions_{};
};

class PlaybackTimeline {
 public:
  [[nodiscard]] static operation::Result<PlaybackTimeline> create(
      std::size_t frame_count, PlaybackRange range = {},
      PlaybackMode mode = PlaybackMode::once,
      PlaybackDirection direction = PlaybackDirection::forward);

  void play() noexcept { playing_ = true; }
  void pause() noexcept { playing_ = false; }
  void set_mode(PlaybackMode mode) noexcept { mode_ = mode; }
  void set_direction(PlaybackDirection direction) noexcept {
    direction_ = direction;
  }

  [[nodiscard]] std::optional<operation::Error> seek(std::size_t frame);
  [[nodiscard]] AdvanceResult advance(std::size_t transitions = 1U) noexcept;
  [[nodiscard]] std::vector<std::size_t> prefetch_hint(
      std::size_t count) const;
  [[nodiscard]] PlaybackSnapshot snapshot() const noexcept;
  [[nodiscard]] const std::vector<std::size_t>& sequence() const noexcept {
    return sequence_;
  }

 private:
  PlaybackTimeline(std::vector<std::size_t> sequence, PlaybackMode mode,
                   PlaybackDirection direction, std::size_t position)
      : sequence_{std::move(sequence)},
        position_{position},
        mode_{mode},
        direction_{direction} {}

  enum class StepOutcome { moved, boundary_moved, boundary_stopped };
  [[nodiscard]] StepOutcome step_once() noexcept;

  std::vector<std::size_t> sequence_;
  std::size_t position_{};
  PlaybackMode mode_{PlaybackMode::once};
  PlaybackDirection direction_{PlaybackDirection::forward};
  bool playing_{};
};

}  // namespace molshredder::trajectory
