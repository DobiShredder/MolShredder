#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

#include "molshredder/operation/error.hpp"
#include "molshredder/trajectory/playback.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  passed &= expect(
      !trajectory::PlaybackClock::create(0.0).has_value() &&
          !trajectory::PlaybackClock::create(
               std::numeric_limits<double>::infinity())
               .has_value() &&
          !trajectory::PlaybackClock::create(30.0, 0U).has_value(),
      "clock must reject invalid speed and catch-up bounds");
  auto clock = trajectory::PlaybackClock::create(4.0, 3U).value();
  const auto partial = clock.consume_elapsed_seconds(0.125);
  const auto one = clock.consume_elapsed_seconds(0.125);
  const auto limited = clock.consume_elapsed_seconds(1.0);
  const auto backlog = clock.consume_elapsed_seconds(0.0);
  passed &= expect(
      partial.has_value() && partial.value().transitions == 0U &&
          partial.value().pending_fractional_transitions == 0.5 &&
          one.has_value() && one.value().transitions == 1U &&
          limited.has_value() && limited.value().transitions == 3U &&
          limited.value().catch_up_limited && backlog.has_value() &&
          backlog.value().transitions == 1U &&
          !backlog.value().catch_up_limited,
      "clock must preserve fractional time and bounded catch-up backlog");
  passed &= expect(
      !clock.consume_elapsed_seconds(-0.1).has_value() &&
          !clock.set_frames_per_second(8.0).has_value() &&
          clock.frames_per_second() == 8.0,
      "clock speed update must validate without discarding valid state");
  clock.reset();
  passed &= expect(clock.pending_transitions() == 0.0,
                   "clock reset must discard fractional/backlog time");
  passed &= expect(
      !trajectory::PlaybackTimeline::create(0U).has_value() &&
          !trajectory::PlaybackTimeline::create(
               5U, trajectory::PlaybackRange{0U, 4U, 0U})
               .has_value() &&
          !trajectory::PlaybackTimeline::create(
               5U, trajectory::PlaybackRange{4U, 3U, 1U})
               .has_value(),
      "empty source, zero stride and reversed range must fail");

  const auto default_range = trajectory::PlaybackTimeline::create(3U).value();
  const auto explicit_frame_zero =
      trajectory::PlaybackTimeline::create(3U, {0U, 0U, 1U}).value();
  passed &= expect(
      default_range.sequence() == std::vector<std::size_t>{0U, 1U, 2U} &&
          explicit_frame_zero.sequence() == std::vector<std::size_t>{0U},
      "omitted last frame and explicit frame zero must remain distinct");

  auto once = trajectory::PlaybackTimeline::create(
                  6U, {1U, 5U, 2U}, trajectory::PlaybackMode::once,
                  trajectory::PlaybackDirection::forward)
                  .value();
  passed &= expect(once.sequence() == std::vector<std::size_t>{1U, 3U, 5U} &&
                       once.snapshot().frame == 1U &&
                       !once.snapshot().playing,
                   "range must normalize to inclusive stride sequence");
  once.play();
  auto advanced = once.advance(10U);
  passed &= expect(advanced.transitions == 2U &&
                       advanced.boundary_crossings == 1U &&
                       advanced.snapshot.frame == 5U &&
                       !advanced.snapshot.playing,
                   "once playback must stop at the forward endpoint");
  passed &= expect(once.seek(3U) == std::nullopt &&
                       once.snapshot().frame == 3U &&
                       once.seek(4U).has_value(),
                   "seek must accept only frames in the normalized sequence");

  auto loop = trajectory::PlaybackTimeline::create(
                  3U, {}, trajectory::PlaybackMode::loop,
                  trajectory::PlaybackDirection::reverse)
                  .value();
  loop.play();
  passed &= expect(loop.snapshot().frame == 2U &&
                       loop.advance().snapshot.frame == 1U &&
                       loop.advance().snapshot.frame == 0U &&
                       loop.advance().snapshot.frame == 2U &&
                       loop.snapshot().direction ==
                           trajectory::PlaybackDirection::reverse &&
                       loop.snapshot().playing,
                   "reverse loop playback must wrap without changing direction");

  auto rock = trajectory::PlaybackTimeline::create(
                  3U, {}, trajectory::PlaybackMode::rock,
                  trajectory::PlaybackDirection::forward)
                  .value();
  rock.play();
  std::vector<std::size_t> rock_frames;
  std::size_t crossings{};
  for (std::size_t index = 0; index < 6U; ++index) {
    const auto step = rock.advance();
    rock_frames.push_back(step.snapshot.frame);
    crossings += step.boundary_crossings;
  }
  passed &= expect(rock_frames ==
                           std::vector<std::size_t>{1U, 2U, 1U, 0U, 1U, 2U} &&
                       crossings == 2U && rock.snapshot().playing,
                   "rock playback must reverse without duplicating endpoints");
  passed &= expect(rock.prefetch_hint(5U) ==
                       std::vector<std::size_t>{1U, 0U, 2U},
                   "prefetch hint must follow mode/direction and deduplicate");

  auto single = trajectory::PlaybackTimeline::create(
                    4U, {2U, 2U, 1U}, trajectory::PlaybackMode::once)
                    .value();
  single.play();
  const auto single_step = single.advance(3U);
  passed &= expect(single_step.transitions == 0U &&
                       single_step.boundary_crossings == 1U &&
                       single_step.snapshot.frame == 2U &&
                       !single_step.snapshot.playing,
                   "single-frame once range must stop on its only frame");

  return passed ? 0 : 1;
}
