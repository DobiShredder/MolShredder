#include "molshredder/analysis/principal_axes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "molshredder/operation/error.hpp"
#include "molshredder/scene/math.hpp"

namespace molshredder::analysis {
namespace {

using scene::operator+;
using scene::operator-;
using scene::operator*;
using scene::operator/;

using Matrix3 = std::array<std::array<double, 3U>, 3U>;

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

bool valid_basis(const std::array<model::Vec3d, 3U>& axes) {
  constexpr double tolerance = 1.0e-8;
  return std::all_of(axes.begin(), axes.end(), scene::is_finite) &&
         std::abs(scene::length(axes[0]) - 1.0) <= tolerance &&
         std::abs(scene::length(axes[1]) - 1.0) <= tolerance &&
         std::abs(scene::length(axes[2]) - 1.0) <= tolerance &&
         std::abs(scene::dot(axes[0], axes[1])) <= tolerance &&
         std::abs(scene::dot(axes[0], axes[2])) <= tolerance &&
         std::abs(scene::dot(axes[1], axes[2])) <= tolerance &&
         scene::dot(scene::cross(axes[0], axes[1]), axes[2]) >=
             1.0 - tolerance;
}

std::array<model::Vec3d, 3U> symmetric_eigenvectors(
    Matrix3 matrix, std::array<double, 3U>& eigenvalues) {
  Matrix3 vectors{{{{1.0, 0.0, 0.0}},
                   {{0.0, 1.0, 0.0}},
                   {{0.0, 0.0, 1.0}}}};
  double scale{};
  for (const auto& row : matrix) {
    for (const auto value : row) scale = std::max(scale, std::abs(value));
  }
  for (std::size_t iteration = 0; iteration < 48U; ++iteration) {
    std::size_t p{};
    std::size_t q{1U};
    double largest = std::abs(matrix[p][q]);
    for (std::size_t row = 0; row < 3U; ++row) {
      for (std::size_t column = row + 1U; column < 3U; ++column) {
        if (std::abs(matrix[row][column]) > largest) {
          largest = std::abs(matrix[row][column]);
          p = row;
          q = column;
        }
      }
    }
    if (largest <= std::max(1.0e-30, scale * 1.0e-14)) break;
    const auto angle = 0.5 * std::atan2(
        2.0 * matrix[p][q], matrix[q][q] - matrix[p][p]);
    const auto cosine = std::cos(angle);
    const auto sine = std::sin(angle);
    const auto app = matrix[p][p];
    const auto aqq = matrix[q][q];
    const auto apq = matrix[p][q];
    matrix[p][p] = cosine * cosine * app -
                   2.0 * sine * cosine * apq + sine * sine * aqq;
    matrix[q][q] = sine * sine * app +
                   2.0 * sine * cosine * apq + cosine * cosine * aqq;
    matrix[p][q] = 0.0;
    matrix[q][p] = 0.0;
    for (std::size_t index = 0; index < 3U; ++index) {
      if (index == p || index == q) continue;
      const auto aip = matrix[index][p];
      const auto aiq = matrix[index][q];
      matrix[index][p] = cosine * aip - sine * aiq;
      matrix[p][index] = matrix[index][p];
      matrix[index][q] = sine * aip + cosine * aiq;
      matrix[q][index] = matrix[index][q];
    }
    for (std::size_t row = 0; row < 3U; ++row) {
      const auto vip = vectors[row][p];
      const auto viq = vectors[row][q];
      vectors[row][p] = cosine * vip - sine * viq;
      vectors[row][q] = sine * vip + cosine * viq;
    }
  }

  std::array<std::size_t, 3U> order{0U, 1U, 2U};
  std::stable_sort(order.begin(), order.end(), [&](std::size_t left,
                                                    std::size_t right) {
    return matrix[left][left] > matrix[right][right];
  });
  std::array<model::Vec3d, 3U> axes;
  for (std::size_t rank = 0; rank < 3U; ++rank) {
    const auto column = order[rank];
    eigenvalues[rank] = std::max(0.0, matrix[column][column]);
    axes[rank] = scene::normalized(model::Vec3d{
        vectors[0][column], vectors[1][column], vectors[2][column]});
  }
  return axes;
}

model::Vec3d projected_axis(model::Vec3d preferred,
                            model::Vec3d normal,
                            model::Vec3d fallback) {
  auto projected = preferred - normal * scene::dot(preferred, normal);
  if (scene::length(projected) <= 1.0e-12) {
    projected = fallback - normal * scene::dot(fallback, normal);
  }
  return scene::normalized(projected);
}

std::array<model::Vec3d, 3U> closest_signs(
    std::array<model::Vec3d, 3U> axes,
    const std::array<model::Vec3d, 3U>& preferred) {
  axes[0] = scene::normalized(axes[0]);
  axes[1] = scene::normalized(
      axes[1] - axes[0] * scene::dot(axes[0], axes[1]));
  axes[2] = scene::normalized(scene::cross(axes[0], axes[1]));
  auto best = axes;
  auto best_score = -std::numeric_limits<double>::infinity();
  for (const double right_sign : {-1.0, 1.0}) {
    for (const double up_sign : {-1.0, 1.0}) {
      const std::array<model::Vec3d, 3U> candidate{
          axes[0] * right_sign, axes[1] * up_sign,
          axes[2] * (right_sign * up_sign)};
      const auto score = scene::dot(candidate[0], preferred[0]) +
                         scene::dot(candidate[1], preferred[1]) +
                         scene::dot(candidate[2], preferred[2]);
      if (score > best_score) {
        best_score = score;
        best = candidate;
      }
    }
  }
  return best;
}

}  // namespace

bool accumulate(PrincipalMoments& moments,
                model::Vec3d coordinate) noexcept {
  if (!scene::is_finite(coordinate) ||
      moments.sample_count == std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  ++moments.sample_count;
  const auto delta = coordinate - moments.centroid;
  moments.centroid =
      moments.centroid + delta / static_cast<double>(moments.sample_count);
  const auto adjusted = coordinate - moments.centroid;
  moments.centered_products[0] += delta.x * adjusted.x;
  moments.centered_products[1] += delta.x * adjusted.y;
  moments.centered_products[2] += delta.x * adjusted.z;
  moments.centered_products[3] += delta.y * adjusted.y;
  moments.centered_products[4] += delta.y * adjusted.z;
  moments.centered_products[5] += delta.z * adjusted.z;
  return std::all_of(moments.centered_products.begin(),
                     moments.centered_products.end(),
                     [](double value) { return std::isfinite(value); });
}

operation::Result<PrincipalAxesResult> calculate_principal_axes(
    const PrincipalMoments& moments,
    std::array<model::Vec3d, 3U> preferred_axes) {
  if (moments.sample_count == 0U) {
    return operation::Result<PrincipalAxesResult>::failure(invalid(
        "principal-axis analysis requires at least one coordinate"));
  }
  if (!scene::is_finite(moments.centroid) ||
      !std::all_of(moments.centered_products.begin(),
                   moments.centered_products.end(),
                   [](double value) { return std::isfinite(value); })) {
    return operation::Result<PrincipalAxesResult>::failure(
        invalid("principal-axis moments must be finite"));
  }
  if (!valid_basis(preferred_axes)) {
    return operation::Result<PrincipalAxesResult>::failure(invalid(
        "preferred principal-axis basis must be right-handed and orthonormal"));
  }

  const auto inverse_count = 1.0 / static_cast<double>(moments.sample_count);
  const auto& products = moments.centered_products;
  Matrix3 covariance{{
      {{products[0] * inverse_count, products[1] * inverse_count,
        products[2] * inverse_count}},
      {{products[1] * inverse_count, products[3] * inverse_count,
        products[4] * inverse_count}},
      {{products[2] * inverse_count, products[4] * inverse_count,
        products[5] * inverse_count}},
  }};
  PrincipalAxesResult result;
  result.centroid = moments.centroid;
  result.sample_count = moments.sample_count;
  auto axes = symmetric_eigenvectors(covariance, result.variances);
  const auto tolerance = std::max(1.0e-24, result.variances[0] * 1.0e-10);
  result.primary_secondary_degenerate =
      result.variances[0] - result.variances[1] <= tolerance;
  result.secondary_tertiary_degenerate =
      result.variances[1] - result.variances[2] <= tolerance;

  if (result.primary_secondary_degenerate &&
      result.secondary_tertiary_degenerate) {
    axes = preferred_axes;
  } else if (result.primary_secondary_degenerate) {
    const auto backward = axes[2];
    const auto right = projected_axis(preferred_axes[0], backward,
                                      preferred_axes[1]);
    axes = {right, scene::cross(backward, right), backward};
  } else if (result.secondary_tertiary_degenerate) {
    const auto right = axes[0];
    const auto up = projected_axis(preferred_axes[1], right,
                                   preferred_axes[2]);
    axes = {right, up, scene::cross(right, up)};
  }
  result.axes = closest_signs(axes, preferred_axes);
  return operation::Result<PrincipalAxesResult>::success(result);
}

}  // namespace molshredder::analysis
