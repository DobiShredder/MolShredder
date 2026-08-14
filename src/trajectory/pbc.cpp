#include "molshredder/trajectory/pbc.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::trajectory {
namespace {

constexpr std::size_t kMaximumSearchNodes = 1'000'000U;

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return {operation::ErrorCode::invalid_argument, std::move(message),
          std::move(suggestion)};
}

bool finite(model::Vec3d value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

model::Vec3d add(model::Vec3d left, model::Vec3d right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

model::Vec3d subtract(model::Vec3d left, model::Vec3d right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

model::Vec3d multiply(model::Vec3d value, double scalar) {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}

double dot(model::Vec3d left, model::Vec3d right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

model::Vec3d cross(model::Vec3d left, model::Vec3d right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

double squared_norm(model::Vec3d value) { return dot(value, value); }

operation::Result<std::int64_t> checked_nearest_integer(
    double value, std::string_view context) {
  constexpr double kMaximumExactInteger = 0x1p52;
  if (!std::isfinite(value) || value <= -kMaximumExactInteger ||
      value >= kMaximumExactInteger) {
    return operation::Result<std::int64_t>::failure(
        invalid(std::string{context} + " is outside the supported lattice range"));
  }
  return operation::Result<std::int64_t>::success(
      static_cast<std::int64_t>(std::floor(value + 0.5)));
}

model::Vec3d lattice_vector(const model::UnitCell& cell,
                            const std::array<std::int64_t, 3>& shift) {
  return add(add(multiply(cell.a, static_cast<double>(shift[0])),
                 multiply(cell.b, static_cast<double>(shift[1]))),
             multiply(cell.c, static_cast<double>(shift[2])));
}

struct QrBasis {
  std::array<model::Vec3d, 3> q{};
  std::array<std::array<double, 3>, 3> r{};
};

operation::Result<QrBasis> qr_basis(const model::UnitCell& cell) {
  if (!cell.is_valid()) {
    return operation::Result<QrBasis>::failure(
        invalid("PBC operation requires a finite right-handed unit cell"));
  }
  QrBasis result;
  const auto norm_a = std::sqrt(squared_norm(cell.a));
  if (!(norm_a > 0.0) || !std::isfinite(norm_a)) {
    return operation::Result<QrBasis>::failure(
        invalid("unit-cell first basis vector is degenerate"));
  }
  result.r[0][0] = norm_a;
  result.q[0] = multiply(cell.a, 1.0 / norm_a);
  result.r[0][1] = dot(result.q[0], cell.b);
  auto orthogonal_b = subtract(cell.b, multiply(result.q[0], result.r[0][1]));
  result.r[1][1] = std::sqrt(squared_norm(orthogonal_b));
  if (!(result.r[1][1] > 0.0) || !std::isfinite(result.r[1][1])) {
    return operation::Result<QrBasis>::failure(
        invalid("unit-cell first two basis vectors are collinear"));
  }
  result.q[1] = multiply(orthogonal_b, 1.0 / result.r[1][1]);
  result.r[0][2] = dot(result.q[0], cell.c);
  result.r[1][2] = dot(result.q[1], cell.c);
  const auto orthogonal_c =
      subtract(subtract(cell.c, multiply(result.q[0], result.r[0][2])),
               multiply(result.q[1], result.r[1][2]));
  result.r[2][2] = std::sqrt(squared_norm(orthogonal_c));
  if (!(result.r[2][2] > 0.0) || !std::isfinite(result.r[2][2])) {
    return operation::Result<QrBasis>::failure(
        invalid("unit-cell basis vectors are coplanar"));
  }
  result.q[2] = multiply(orthogonal_c, 1.0 / result.r[2][2]);
  return operation::Result<QrBasis>::success(result);
}

template <typename Scalar>
std::vector<model::Vec3d> to_double_positions(
    const std::vector<model::Vec3<Scalar>>& positions) {
  std::vector<model::Vec3d> result;
  result.reserve(positions.size());
  for (const auto& position : positions) {
    result.push_back({static_cast<double>(position.x),
                      static_cast<double>(position.y),
                      static_cast<double>(position.z)});
  }
  return result;
}

std::vector<model::Vec3d> double_positions(
    const model::CoordinateFrame& frame) {
  return std::visit(
      [](const auto& positions) { return to_double_positions(positions); },
      frame.positions().values());
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>> make_frame(
    const model::CoordinateFrame& source, std::vector<model::Vec3d> positions,
    std::string operation_name) {
  auto metadata = source.metadata();
  metadata.fields["pbc.operation"] = std::move(operation_name);
  if (source.positions().precision() == model::CoordinatePrecision::float32) {
    std::vector<model::Vec3f> converted;
    converted.reserve(positions.size());
    for (const auto& position : positions) {
      const model::Vec3f value{static_cast<float>(position.x),
                               static_cast<float>(position.y),
                               static_cast<float>(position.z)};
      if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
          !std::isfinite(value.z)) {
        return operation::Result<
            std::shared_ptr<const model::CoordinateFrame>>::failure(
            invalid("PBC-transformed float32 coordinate overflows"));
      }
      converted.push_back(value);
    }
    return model::CoordinateFrame::create(
        model::CoordinateBuffer{std::move(converted)}, source.velocities(),
        source.presence(), std::move(metadata));
  }
  return model::CoordinateFrame::create(
      model::CoordinateBuffer{std::move(positions)}, source.velocities(),
      source.presence(), std::move(metadata));
}

}  // namespace

operation::Result<model::Vec3d> cartesian_to_fractional(
    const model::UnitCell& cell, model::Vec3d cartesian) {
  if (!cell.is_valid() || !finite(cartesian)) {
    return operation::Result<model::Vec3d>::failure(
        invalid("fractional conversion requires a valid cell and finite vector"));
  }
  const auto volume = cell.signed_volume();
  const model::Vec3d result{dot(cartesian, cross(cell.b, cell.c)) / volume,
                            dot(cartesian, cross(cell.c, cell.a)) / volume,
                            dot(cartesian, cross(cell.a, cell.b)) / volume};
  if (!finite(result)) {
    return operation::Result<model::Vec3d>::failure(
        invalid("fractional conversion produced a non-finite vector"));
  }
  return operation::Result<model::Vec3d>::success(result);
}

operation::Result<model::Vec3d> fractional_to_cartesian(
    const model::UnitCell& cell, model::Vec3d fractional) {
  if (!cell.is_valid() || !finite(fractional)) {
    return operation::Result<model::Vec3d>::failure(
        invalid("Cartesian conversion requires a valid cell and finite vector"));
  }
  const auto result =
      add(add(multiply(cell.a, fractional.x), multiply(cell.b, fractional.y)),
          multiply(cell.c, fractional.z));
  if (!finite(result)) {
    return operation::Result<model::Vec3d>::failure(
        invalid("Cartesian conversion produced a non-finite vector"));
  }
  return operation::Result<model::Vec3d>::success(result);
}

operation::Result<MinimumImageResult> minimum_image(
    const model::UnitCell& cell, model::Vec3d displacement) {
  if (!finite(displacement)) {
    return operation::Result<MinimumImageResult>::failure(
        invalid("minimum-image displacement must be finite"));
  }
  const auto qr = qr_basis(cell);
  if (!qr.has_value()) {
    return operation::Result<MinimumImageResult>::failure(qr.error());
  }
  std::array<double, 3> projected{dot(qr.value().q[0], displacement),
                                  dot(qr.value().q[1], displacement),
                                  dot(qr.value().q[2], displacement)};
  std::array<std::int64_t, 3> candidate{};
  for (int level = 2; level >= 0; --level) {
    auto residual = projected[static_cast<std::size_t>(level)];
    for (std::size_t axis = static_cast<std::size_t>(level) + 1U; axis < 3U;
         ++axis) {
      residual -= qr.value().r[static_cast<std::size_t>(level)][axis] *
                  static_cast<double>(candidate[axis]);
    }
    const auto nearest = checked_nearest_integer(
        residual / qr.value().r[static_cast<std::size_t>(level)]
                                [static_cast<std::size_t>(level)],
        "minimum-image lattice coordinate");
    if (!nearest.has_value()) {
      return operation::Result<MinimumImageResult>::failure(nearest.error());
    }
    candidate[static_cast<std::size_t>(level)] = nearest.value();
  }
  auto best_shift = candidate;
  auto best_displacement = subtract(displacement, lattice_vector(cell, best_shift));
  auto best_squared = squared_norm(best_displacement);
  if (!std::isfinite(best_squared)) {
    return operation::Result<MinimumImageResult>::failure(
        invalid("minimum-image initial distance is non-finite"));
  }

  std::size_t visited_nodes{};
  bool search_too_large = false;
  std::function<void(int, double)> search = [&](int level,
                                                double partial_squared) {
    if (search_too_large || level < 0) {
      if (level < 0) {
        const auto tolerance =
            32.0 * std::numeric_limits<double>::epsilon() *
            std::max({1.0, best_squared, partial_squared});
        if (partial_squared < best_squared - tolerance ||
            (std::abs(partial_squared - best_squared) <= tolerance &&
             candidate > best_shift)) {
          best_squared = partial_squared;
          best_shift = candidate;
        }
      }
      return;
    }
    const auto index = static_cast<std::size_t>(level);
    auto residual = projected[index];
    for (std::size_t axis = index + 1U; axis < 3U; ++axis) {
      residual -= qr.value().r[index][axis] *
                  static_cast<double>(candidate[axis]);
    }
    const auto remaining = std::max(0.0, best_squared - partial_squared);
    const auto radius = std::sqrt(remaining) / qr.value().r[index][index];
    const auto center = residual / qr.value().r[index][index];
    if (!std::isfinite(radius) || !std::isfinite(center) ||
        radius > static_cast<double>(kMaximumSearchNodes)) {
      search_too_large = true;
      return;
    }
    const auto lower_double = std::ceil(center - radius);
    const auto upper_double = std::floor(center + radius);
    if (lower_double < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        upper_double > static_cast<double>(std::numeric_limits<std::int64_t>::max()) ||
        upper_double - lower_double >
            static_cast<double>(kMaximumSearchNodes)) {
      search_too_large = true;
      return;
    }
    const auto lower = static_cast<std::int64_t>(lower_double);
    const auto upper = static_cast<std::int64_t>(upper_double);
    if (lower > upper) return;
    for (auto integer = lower;; ++integer) {
      if (++visited_nodes > kMaximumSearchNodes) {
        search_too_large = true;
        return;
      }
      candidate[index] = integer;
      const auto delta = residual - qr.value().r[index][index] *
                                        static_cast<double>(integer);
      const auto next_squared = partial_squared + delta * delta;
      const auto tolerance = 32.0 * std::numeric_limits<double>::epsilon() *
                             std::max({1.0, best_squared, next_squared});
      if (next_squared <= best_squared + tolerance) {
        search(level - 1, next_squared);
      }
      if (integer == upper) break;
    }
  };
  search(2, 0.0);
  if (search_too_large) {
    return operation::Result<MinimumImageResult>::failure(invalid(
        "unit cell is too ill-conditioned for bounded exact minimum-image search",
        "reduce the lattice basis or use a physically well-conditioned cell"));
  }
  best_displacement = subtract(displacement, lattice_vector(cell, best_shift));
  if (!finite(best_displacement)) {
    return operation::Result<MinimumImageResult>::failure(
        invalid("minimum-image result is non-finite"));
  }
  return operation::Result<MinimumImageResult>::success(
      {best_displacement, best_shift});
}

operation::Result<model::Vec3d> wrap_position(const model::UnitCell& cell,
                                               model::Vec3d position) {
  const auto fractional = cartesian_to_fractional(cell, position);
  if (!fractional.has_value()) {
    return operation::Result<model::Vec3d>::failure(fractional.error());
  }
  model::Vec3d wrapped = fractional.value();
  for (auto* component : {&wrapped.x, &wrapped.y, &wrapped.z}) {
    if (std::abs(*component) >= 0x1p52) {
      return operation::Result<model::Vec3d>::failure(invalid(
          "position is too many cell lengths away to preserve a fractional part"));
    }
    *component -= std::floor(*component);
    if (*component >= 1.0) *component = 0.0;
  }
  return fractional_to_cartesian(cell, wrapped);
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>> wrap_frame(
    const model::CoordinateFrame& frame) {
  if (!frame.metadata().unit_cell.has_value()) {
    return operation::Result<
        std::shared_ptr<const model::CoordinateFrame>>::failure(
        invalid("frame wrapping requires a periodic unit cell"));
  }
  auto positions = double_positions(frame);
  for (std::size_t index = 0; index < positions.size(); ++index) {
    if (!frame.atom_present(index)) continue;
    const auto wrapped = wrap_position(*frame.metadata().unit_cell,
                                       positions[index]);
    if (!wrapped.has_value()) {
      return operation::Result<
          std::shared_ptr<const model::CoordinateFrame>>::failure(
          wrapped.error());
    }
    positions[index] = wrapped.value();
  }
  return make_frame(frame, std::move(positions), "wrap-atoms");
}

TrajectoryUnwrapper::TrajectoryUnwrapper(std::size_t atom_count)
    : atom_count_{atom_count},
      previous_wrapped_(atom_count),
      previous_unwrapped_(atom_count),
      continuity_(atom_count) {}

void TrajectoryUnwrapper::reset() noexcept {
  processed_frame_count_ = 0U;
  std::fill(continuity_.begin(), continuity_.end(), 0U);
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
TrajectoryUnwrapper::push(const model::CoordinateFrame& wrapped_frame) {
  if (wrapped_frame.atom_count() != atom_count_) {
    return operation::Result<
        std::shared_ptr<const model::CoordinateFrame>>::failure(
        invalid("unwrap frame atom count does not match unwrapper"));
  }
  if (!wrapped_frame.metadata().unit_cell.has_value()) {
    return operation::Result<
        std::shared_ptr<const model::CoordinateFrame>>::failure(
        invalid("trajectory unwrapping requires a unit cell on every frame"));
  }
  const auto wrapped = double_positions(wrapped_frame);
  auto unwrapped = wrapped;
  auto next_wrapped = previous_wrapped_;
  auto next_unwrapped = previous_unwrapped_;
  auto next_continuity = continuity_;
  for (std::size_t index = 0; index < atom_count_; ++index) {
    if (!wrapped_frame.atom_present(index)) {
      next_continuity[index] = 0U;
      continue;
    }
    if (processed_frame_count_ != 0U && continuity_[index] != 0U) {
      const auto delta = minimum_image(
          *wrapped_frame.metadata().unit_cell,
          subtract(wrapped[index], previous_wrapped_[index]));
      if (!delta.has_value()) {
        return operation::Result<
            std::shared_ptr<const model::CoordinateFrame>>::failure(
            delta.error());
      }
      unwrapped[index] = add(previous_unwrapped_[index],
                             delta.value().displacement);
    }
    next_wrapped[index] = wrapped[index];
    next_unwrapped[index] = unwrapped[index];
    next_continuity[index] = 1U;
  }
  auto result = make_frame(wrapped_frame, std::move(unwrapped),
                           "unwrap-atom-continuity");
  if (!result.has_value()) return result;
  previous_wrapped_ = std::move(next_wrapped);
  previous_unwrapped_ = std::move(next_unwrapped);
  continuity_ = std::move(next_continuity);
  ++processed_frame_count_;
  return result;
}

}  // namespace molshredder::trajectory
