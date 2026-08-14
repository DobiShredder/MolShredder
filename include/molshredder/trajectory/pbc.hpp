#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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

}  // namespace molshredder::trajectory
