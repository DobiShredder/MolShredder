#include "molshredder/analysis/alignment.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "molshredder/operation/error.hpp"
#include "molshredder/selection/evaluator.hpp"

namespace molshredder::analysis {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

operation::Error invalid_selection(std::string message,
                                   std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_selection,
                          std::move(message), std::move(suggestion)};
}

double unit_scale(operation::LengthUnit input,
                  operation::LengthUnit output) noexcept {
  if (input == output) return 1.0;
  return input == operation::LengthUnit::angstrom ? 0.1 : 10.0;
}

model::Vec3d coordinate(const model::CoordinateFrame& frame,
                        std::size_t index) {
  return std::visit(
      [index](const auto& values) {
        return model::Vec3d{static_cast<double>(values[index].x),
                            static_cast<double>(values[index].y),
                            static_cast<double>(values[index].z)};
      },
      frame.positions().values());
}

model::Vec3d add(model::Vec3d left, model::Vec3d right) noexcept {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

model::Vec3d subtract(model::Vec3d left, model::Vec3d right) noexcept {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

model::Vec3d multiply(model::Vec3d value, double scale) noexcept {
  return {value.x * scale, value.y * scale, value.z * scale};
}

double dot(model::Vec3d left, model::Vec3d right) noexcept {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

model::Vec3d cross(model::Vec3d left, model::Vec3d right) noexcept {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

struct PairSet {
  std::vector<std::size_t> indices;
  std::size_t selected_count{};
  std::size_t skipped_count{};
  std::size_t effective_count{};
  double weight_sum{};
};

operation::Result<PairSet> pairs(const model::CoordinateFrame* reference,
                                 const model::CoordinateFrame* mobile,
                                 std::span<const std::uint8_t> selected,
                                 std::span<const double> weights,
                                 MissingAtomPolicy missing_policy) {
  if (reference == nullptr || mobile == nullptr) {
    return operation::Result<PairSet>::failure(
        invalid("alignment requires reference and mobile coordinate frames"));
  }
  if (reference->atom_count() != mobile->atom_count()) {
    return operation::Result<PairSet>::failure(
        invalid("alignment frame atom counts do not match"));
  }
  const auto atom_count = reference->atom_count();
  if (!selected.empty() &&
      !selection::mask_is_valid(selected, atom_count)) {
    return operation::Result<PairSet>::failure(
        invalid_selection("alignment selection mask is invalid"));
  }
  if (!weights.empty() && weights.size() != atom_count) {
    return operation::Result<PairSet>::failure(
        invalid("alignment weights must contain one value per atom"));
  }
  if (!std::all_of(weights.begin(), weights.end(), [](double weight) {
        return std::isfinite(weight) && weight >= 0.0;
      })) {
    return operation::Result<PairSet>::failure(
        invalid("alignment weights must be finite and non-negative"));
  }
  PairSet result;
  result.indices.reserve(atom_count);
  for (std::size_t index = 0; index < atom_count; ++index) {
    if (!selected.empty() && selected[index] == 0U) continue;
    ++result.selected_count;
    if (!reference->atom_present(index) || !mobile->atom_present(index)) {
      if (missing_policy == MissingAtomPolicy::error) {
        return operation::Result<PairSet>::failure(invalid_selection(
            "alignment selection contains a missing atom at index " +
                std::to_string(index + 1U),
            "remove missing atoms or explicitly use the skip policy"));
      }
      ++result.skipped_count;
      continue;
    }
    result.indices.push_back(index);
    const auto weight = weights.empty() ? 1.0 : weights[index];
    result.weight_sum += weight;
    if (weight > 0.0) ++result.effective_count;
  }
  if (result.indices.empty()) {
    return operation::Result<PairSet>::failure(
        invalid_selection("alignment selection contains no paired atoms"));
  }
  if (!(result.weight_sum > 0.0) || !std::isfinite(result.weight_sum)) {
    return operation::Result<PairSet>::failure(
        invalid("alignment selected total weight must be positive and finite"));
  }
  return operation::Result<PairSet>::success(std::move(result));
}

bool non_collinear(const std::vector<model::Vec3d>& points) {
  if (points.size() < 3U) return false;
  const auto base = points.front();
  std::size_t second = 1U;
  double largest_span_squared{};
  for (std::size_t index = 1U; index < points.size(); ++index) {
    const auto delta = subtract(points[index], base);
    const auto span_squared = dot(delta, delta);
    if (span_squared > largest_span_squared) {
      largest_span_squared = span_squared;
      second = index;
    }
  }
  if (!(largest_span_squared > 0.0)) return false;
  const auto axis = subtract(points[second], base);
  double largest_area_squared{};
  for (std::size_t index = 1U; index < points.size(); ++index) {
    const auto area = cross(axis, subtract(points[index], base));
    largest_area_squared = std::max(largest_area_squared, dot(area, area));
  }
  const auto threshold =
      std::max(1.0, largest_span_squared * largest_span_squared) * 1.0e-24;
  return largest_area_squared > threshold;
}

std::array<double, 4> largest_eigenvector(
    std::array<std::array<double, 4>, 4> matrix) {
  double matrix_scale{};
  for (const auto& row : matrix) {
    for (const auto value : row) {
      matrix_scale = std::max(matrix_scale, std::abs(value));
    }
  }
  std::array<std::array<double, 4>, 4> vectors{};
  for (std::size_t index = 0; index < 4U; ++index) {
    vectors[index][index] = 1.0;
  }
  for (std::size_t iteration = 0; iteration < 64U; ++iteration) {
    std::size_t p{};
    std::size_t q{1U};
    double largest = std::abs(matrix[p][q]);
    for (std::size_t row = 0; row < 4U; ++row) {
      for (std::size_t column = row + 1U; column < 4U; ++column) {
        if (std::abs(matrix[row][column]) > largest) {
          largest = std::abs(matrix[row][column]);
          p = row;
          q = column;
        }
      }
    }
    if (largest <= matrix_scale * 1.0e-14) break;
    const auto theta =
        0.5 * std::atan2(2.0 * matrix[p][q], matrix[q][q] - matrix[p][p]);
    const auto cosine = std::cos(theta);
    const auto sine = std::sin(theta);
    const auto app = matrix[p][p];
    const auto aqq = matrix[q][q];
    const auto apq = matrix[p][q];
    matrix[p][p] = cosine * cosine * app - 2.0 * sine * cosine * apq +
                   sine * sine * aqq;
    matrix[q][q] = sine * sine * app + 2.0 * sine * cosine * apq +
                   cosine * cosine * aqq;
    matrix[p][q] = 0.0;
    matrix[q][p] = 0.0;
    for (std::size_t index = 0; index < 4U; ++index) {
      if (index == p || index == q) continue;
      const auto aip = matrix[index][p];
      const auto aiq = matrix[index][q];
      matrix[index][p] = cosine * aip - sine * aiq;
      matrix[p][index] = matrix[index][p];
      matrix[index][q] = sine * aip + cosine * aiq;
      matrix[q][index] = matrix[index][q];
    }
    for (std::size_t row = 0; row < 4U; ++row) {
      const auto vip = vectors[row][p];
      const auto viq = vectors[row][q];
      vectors[row][p] = cosine * vip - sine * viq;
      vectors[row][q] = sine * vip + cosine * viq;
    }
  }
  std::size_t largest_index{};
  for (std::size_t index = 1U; index < 4U; ++index) {
    if (matrix[index][index] > matrix[largest_index][largest_index]) {
      largest_index = index;
    }
  }
  std::array<double, 4> result{};
  double norm_squared{};
  for (std::size_t row = 0; row < 4U; ++row) {
    result[row] = vectors[row][largest_index];
    norm_squared += result[row] * result[row];
  }
  const auto inverse_norm = 1.0 / std::sqrt(norm_squared);
  for (auto& value : result) value *= inverse_norm;
  if (result[0] < 0.0) {
    for (auto& value : result) value = -value;
  }
  return result;
}

std::array<double, 9> quaternion_rotation(
    const std::array<double, 4>& quaternion) noexcept {
  const auto w = quaternion[0];
  const auto x = quaternion[1];
  const auto y = quaternion[2];
  const auto z = quaternion[3];
  return {1.0 - 2.0 * (y * y + z * z),
          2.0 * (x * y - z * w),
          2.0 * (x * z + y * w),
          2.0 * (x * y + z * w),
          1.0 - 2.0 * (x * x + z * z),
          2.0 * (y * z - x * w),
          2.0 * (x * z - y * w),
          2.0 * (y * z + x * w),
          1.0 - 2.0 * (x * x + y * y)};
}

}  // namespace

model::Vec3d RigidTransform::apply(model::Vec3d point) const noexcept {
  point = multiply(point, input_scale);
  return {rotation[0] * point.x + rotation[1] * point.y +
              rotation[2] * point.z + translation.x,
          rotation[3] * point.x + rotation[4] * point.y +
              rotation[5] * point.z + translation.y,
          rotation[6] * point.x + rotation[7] * point.y +
              rotation[8] * point.z + translation.z};
}

bool is_valid(const RigidTransform& transform) noexcept {
  if (!(std::isfinite(transform.input_scale) &&
        transform.input_scale > 0.0 &&
        std::isfinite(transform.translation.x) &&
        std::isfinite(transform.translation.y) &&
        std::isfinite(transform.translation.z) &&
        std::all_of(transform.rotation.begin(), transform.rotation.end(),
                    [](double value) { return std::isfinite(value); }))) {
    return false;
  }
  const auto& r = transform.rotation;
  constexpr double tolerance = 1.0e-9;
  for (std::size_t row = 0; row < 3U; ++row) {
    double norm{};
    for (std::size_t column = 0; column < 3U; ++column) {
      norm += r[row * 3U + column] * r[row * 3U + column];
    }
    if (std::abs(norm - 1.0) > tolerance) return false;
  }
  for (std::size_t first = 0; first < 3U; ++first) {
    for (std::size_t second = first + 1U; second < 3U; ++second) {
      double product{};
      for (std::size_t column = 0; column < 3U; ++column) {
        product += r[first * 3U + column] * r[second * 3U + column];
      }
      if (std::abs(product) > tolerance) return false;
    }
  }
  const auto determinant =
      r[0] * (r[4] * r[8] - r[5] * r[7]) -
      r[1] * (r[3] * r[8] - r[5] * r[6]) +
      r[2] * (r[3] * r[7] - r[4] * r[6]);
  return std::abs(determinant - 1.0) <= tolerance;
}

operation::Result<RmsdResult> calculate_rmsd(const RmsdRequest& request) {
  if (!is_valid(request.transform)) {
    return operation::Result<RmsdResult>::failure(
        invalid("RMSD transform must be a finite proper rigid transform"));
  }
  if (request.reference != nullptr && request.mobile != nullptr) {
    const auto expected_scale = unit_scale(
        request.mobile->metadata().coordinate_unit,
        request.reference->metadata().coordinate_unit);
    if (std::abs(request.transform.input_scale - expected_scale) >
        expected_scale * 1.0e-12) {
      return operation::Result<RmsdResult>::failure(invalid(
          "RMSD transform scale does not convert mobile units to reference units",
          "use the trajectory coordinator or a fit transform for mixed units"));
    }
  }
  const auto paired = pairs(request.reference, request.mobile,
                            request.selected, request.weights,
                            request.missing_atom_policy);
  if (!paired.has_value()) {
    return operation::Result<RmsdResult>::failure(paired.error());
  }
  long double squared_sum{};
  for (const auto index : paired.value().indices) {
    const auto weight = request.weights.empty() ? 1.0 : request.weights[index];
    const auto delta = subtract(request.transform.apply(
                                    coordinate(*request.mobile, index)),
                                coordinate(*request.reference, index));
    squared_sum += static_cast<long double>(weight) *
                   static_cast<long double>(dot(delta, delta));
  }
  const auto rmsd = std::sqrt(static_cast<double>(
      squared_sum / static_cast<long double>(paired.value().weight_sum)));
  return operation::Result<RmsdResult>::success(RmsdResult{
      rmsd, request.reference->metadata().coordinate_unit,
      paired.value().selected_count, paired.value().indices.size(),
      paired.value().skipped_count, paired.value().effective_count,
      paired.value().weight_sum});
}

operation::Result<FitResult> fit_rigid(const FitRequest& request) {
  const auto paired = pairs(request.reference, request.mobile,
                            request.selected, request.weights,
                            request.missing_atom_policy);
  if (!paired.has_value()) {
    return operation::Result<FitResult>::failure(paired.error());
  }
  if (paired.value().effective_count < 3U) {
    return operation::Result<FitResult>::failure(invalid_selection(
        "rigid fit requires at least three positive-weight paired atoms"));
  }
  const auto scale = unit_scale(request.mobile->metadata().coordinate_unit,
                                request.reference->metadata().coordinate_unit);
  std::vector<model::Vec3d> reference_points;
  std::vector<model::Vec3d> mobile_points;
  reference_points.reserve(paired.value().effective_count);
  mobile_points.reserve(paired.value().effective_count);
  model::Vec3d reference_center;
  model::Vec3d mobile_center;
  double accumulated_weight{};
  for (const auto index : paired.value().indices) {
    const auto weight = request.weights.empty() ? 1.0 : request.weights[index];
    if (weight == 0.0) continue;
    const auto reference = coordinate(*request.reference, index);
    const auto mobile = multiply(coordinate(*request.mobile, index), scale);
    reference_points.push_back(reference);
    mobile_points.push_back(mobile);
    accumulated_weight += weight;
    const auto fraction = weight / accumulated_weight;
    reference_center = add(
        reference_center,
        multiply(subtract(reference, reference_center), fraction));
    mobile_center = add(mobile_center,
                        multiply(subtract(mobile, mobile_center), fraction));
  }
  if (!non_collinear(reference_points) || !non_collinear(mobile_points)) {
    return operation::Result<FitResult>::failure(invalid_selection(
        "rigid fit requires non-collinear reference and mobile atoms",
        "select at least three atoms that do not lie on one line"));
  }

  std::array<std::array<double, 3>, 3> covariance{};
  std::size_t point = 0U;
  for (const auto index : paired.value().indices) {
    const auto weight = request.weights.empty() ? 1.0 : request.weights[index];
    if (weight == 0.0) continue;
    const auto mobile = subtract(mobile_points[point], mobile_center);
    const auto reference = subtract(reference_points[point], reference_center);
    const std::array<double, 3> left{mobile.x, mobile.y, mobile.z};
    const std::array<double, 3> right{reference.x, reference.y, reference.z};
    for (std::size_t row = 0; row < 3U; ++row) {
      for (std::size_t column = 0; column < 3U; ++column) {
        covariance[row][column] += weight * left[row] * right[column];
      }
    }
    ++point;
  }
  const auto& s = covariance;
  std::array<std::array<double, 4>, 4> horn{{
      {{s[0][0] + s[1][1] + s[2][2], s[1][2] - s[2][1],
        s[2][0] - s[0][2], s[0][1] - s[1][0]}},
      {{s[1][2] - s[2][1], s[0][0] - s[1][1] - s[2][2],
        s[0][1] + s[1][0], s[2][0] + s[0][2]}},
      {{s[2][0] - s[0][2], s[0][1] + s[1][0],
        -s[0][0] + s[1][1] - s[2][2], s[1][2] + s[2][1]}},
      {{s[0][1] - s[1][0], s[2][0] + s[0][2],
        s[1][2] + s[2][1], -s[0][0] - s[1][1] + s[2][2]}}
  }};
  RigidTransform transform;
  transform.rotation = quaternion_rotation(largest_eigenvector(horn));
  const auto rotated_mobile_center = transform.apply(mobile_center);
  transform.translation = subtract(reference_center, rotated_mobile_center);
  transform.input_scale = scale;

  RigidTransform identity;
  identity.input_scale = scale;
  const auto before = calculate_rmsd(RmsdRequest{
      request.reference, request.mobile, request.selected, request.weights,
      request.missing_atom_policy, identity});
  const auto after = calculate_rmsd(RmsdRequest{
      request.reference, request.mobile, request.selected, request.weights,
      request.missing_atom_policy, transform});
  if (!before.has_value()) return operation::Result<FitResult>::failure(before.error());
  if (!after.has_value()) return operation::Result<FitResult>::failure(after.error());
  return operation::Result<FitResult>::success(
      FitResult{transform, before.value(), after.value()});
}

}  // namespace molshredder::analysis
