#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::trajectory {

struct MinimumImageResult {
  model::Vec3d displacement;
  std::array<std::int64_t, 3> lattice_shift{};
};

[[nodiscard]] operation::Result<model::Vec3d> cartesian_to_fractional(
    const model::UnitCell& cell, model::Vec3d cartesian);

[[nodiscard]] operation::Result<model::Vec3d> fractional_to_cartesian(
    const model::UnitCell& cell, model::Vec3d fractional);

[[nodiscard]] operation::Result<MinimumImageResult> minimum_image(
    const model::UnitCell& cell, model::Vec3d displacement);

[[nodiscard]] operation::Result<model::Vec3d> wrap_position(
    const model::UnitCell& cell, model::Vec3d position);

[[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateFrame>>
wrap_frame(const model::CoordinateFrame& frame);

class TrajectoryUnwrapper {
 public:
  explicit TrajectoryUnwrapper(std::size_t atom_count);

  [[nodiscard]] std::size_t atom_count() const noexcept { return atom_count_; }
  [[nodiscard]] std::size_t processed_frame_count() const noexcept {
    return processed_frame_count_;
  }
  void reset() noexcept;

  [[nodiscard]] operation::Result<
      std::shared_ptr<const model::CoordinateFrame>>
  push(const model::CoordinateFrame& wrapped_frame);

 private:
  std::size_t atom_count_{};
  std::size_t processed_frame_count_{};
  std::vector<model::Vec3d> previous_wrapped_;
  std::vector<model::Vec3d> previous_unwrapped_;
  std::vector<std::uint8_t> continuity_;
};

enum class PbcMode { wrap, unwrap };

// A read-only view of another coordinate source whose frames are wrapped into
// the primary cell or unwrapped by atom continuity (non-destructive PBC
// processing). Wrapping works per frame; unwrapping needs the frames in order,
// so sequential reads are O(1) and a jump replays from frame 0 (bounded by
// `maximum_replay_frames`).
class PbcProcessedSource final : public model::CoordinateSource {
 public:
  [[nodiscard]] static operation::Result<std::shared_ptr<const PbcProcessedSource>>
  create(std::shared_ptr<const model::CoordinateSource> base, PbcMode mode,
         std::size_t maximum_replay_frames = 50000U);

  [[nodiscard]] std::size_t atom_count() const noexcept override {
    return base_->atom_count();
  }
  [[nodiscard]] std::optional<std::size_t> frame_count() const noexcept override {
    return base_->frame_count();
  }
  [[nodiscard]] model::FrameAccess access() const noexcept override {
    return mode_ == PbcMode::unwrap ? model::FrameAccess::sequential
                                    : base_->access();
  }
  [[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override;
  [[nodiscard]] PbcMode mode() const noexcept { return mode_; }

 private:
  PbcProcessedSource(std::shared_ptr<const model::CoordinateSource> base,
                     PbcMode mode, std::size_t maximum_replay_frames);

  std::shared_ptr<const model::CoordinateSource> base_;
  PbcMode mode_;
  std::size_t maximum_replay_frames_;
  mutable std::mutex mutex_;
  mutable TrajectoryUnwrapper unwrapper_;
  mutable std::optional<std::size_t> last_index_;
  mutable std::shared_ptr<const model::CoordinateFrame> last_frame_;
};

}  // namespace molshredder::trajectory
