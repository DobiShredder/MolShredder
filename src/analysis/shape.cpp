#include "molshredder/analysis/shape.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>
#include <variant>
#include <vector>

#include "molshredder/operation/error.hpp"
#include "molshredder/selection/evaluator.hpp"

namespace molshredder::analysis {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion), {}};
}

operation::Error invalid_selection(std::string message,
                                   std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_selection,
                          std::move(message), std::move(suggestion), {}};
}

using Matrix3 = std::array<std::array<double, 3U>, 3U>;

// Cyclic Jacobi eigen-decomposition of a symmetric 3x3 matrix. Eigenvectors
// are returned as the columns of `vectors`.
void jacobi_eigen(Matrix3 matrix, std::array<double, 3U>& values,
                  Matrix3& vectors) {
  vectors = {{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
  for (int sweep = 0; sweep < 64; ++sweep) {
    const double off = std::abs(matrix[0][1]) + std::abs(matrix[0][2]) +
                       std::abs(matrix[1][2]);
    const double diagonal =
        std::abs(matrix[0][0]) + std::abs(matrix[1][1]) + std::abs(matrix[2][2]);
    if (off <= 1.0e-15 * std::max(diagonal, 1.0e-300)) break;
    for (std::size_t p = 0; p < 2U; ++p) {
      for (std::size_t q = p + 1U; q < 3U; ++q) {
        if (matrix[p][q] == 0.0) continue;
        const double theta =
            (matrix[q][q] - matrix[p][p]) / (2.0 * matrix[p][q]);
        const double t = (theta >= 0.0 ? 1.0 : -1.0) /
                         (std::abs(theta) + std::sqrt(theta * theta + 1.0));
        const double c = 1.0 / std::sqrt(t * t + 1.0);
        const double s = t * c;
        for (std::size_t k = 0; k < 3U; ++k) {
          const double kp = matrix[k][p];
          const double kq = matrix[k][q];
          matrix[k][p] = c * kp - s * kq;
          matrix[k][q] = s * kp + c * kq;
        }
        for (std::size_t k = 0; k < 3U; ++k) {
          const double pk = matrix[p][k];
          const double qk = matrix[q][k];
          matrix[p][k] = c * pk - s * qk;
          matrix[q][k] = s * pk + c * qk;
        }
        for (std::size_t k = 0; k < 3U; ++k) {
          const double kp = vectors[k][p];
          const double kq = vectors[k][q];
          vectors[k][p] = c * kp - s * kq;
          vectors[k][q] = s * kp + c * kq;
        }
      }
    }
  }
  values = {matrix[0][0], matrix[1][1], matrix[2][2]};
}

}  // namespace

operation::Result<ShapeResult> calculate_shape(const ShapeRequest& request) {
  using R = operation::Result<ShapeResult>;
  if (request.frame == nullptr)
    return R::failure(invalid("shape calculation requires a coordinate frame"));
  const auto atom_count = request.frame->atom_count();
  if (!request.selected.empty() &&
      !selection::mask_is_valid(request.selected, atom_count))
    return R::failure(invalid_selection("shape selection mask is invalid"));
  const bool by_mass = request.mode == CenterMode::center_of_mass;
  if (by_mass) {
    if (request.masses.size() != atom_count || request.mass_source.empty())
      return R::failure(invalid("mass-weighted shape requires one mass per "
                                "atom and mass provenance"));
    if (!std::all_of(request.masses.begin(), request.masses.end(),
                     [](double mass) { return std::isfinite(mass) && mass >= 0.0; }))
      return R::failure(invalid("shape weights must be finite and non-negative"));
  } else if (!request.masses.empty() || !request.mass_unit.empty() ||
             !request.mass_source.empty() || request.masses_estimated) {
    return R::failure(
        invalid("centroid shape does not accept mass values or provenance"));
  }

  // Two passes: weighted center first, then second moments about it. This
  // avoids the cancellation of raw moments for coordinates far from the origin.
  struct Sample { double x, y, z, w; };
  std::vector<Sample> samples;
  std::size_t selected_count{};
  std::size_t skipped_count{};
  std::optional<operation::Error> loop_error;
  std::visit(
      [&](const auto& positions) {
        for (std::size_t index = 0; index < atom_count; ++index) {
          if (!request.selected.empty() && request.selected[index] == 0U)
            continue;
          ++selected_count;
          if (!request.frame->atom_present(index)) {
            if (request.missing_atom_policy == MissingAtomPolicy::error) {
              loop_error = invalid_selection(
                  "shape selection contains a missing atom at index " +
                      std::to_string(index + 1U),
                  "remove missing atoms or explicitly use the skip policy");
              return;
            }
            ++skipped_count;
            continue;
          }
          samples.push_back({static_cast<double>(positions[index].x),
                             static_cast<double>(positions[index].y),
                             static_cast<double>(positions[index].z),
                             by_mass ? request.masses[index] : 1.0});
        }
      },
      request.frame->positions().values());
  if (loop_error.has_value()) return R::failure(std::move(*loop_error));
  if (samples.empty())
    return R::failure(invalid_selection("shape selection contains no usable atoms"));

  double total = 0.0, cx = 0.0, cy = 0.0, cz = 0.0;
  for (const auto& s : samples) {
    total += s.w;
    cx += s.w * s.x;
    cy += s.w * s.y;
    cz += s.w * s.z;
  }
  if (!std::isfinite(total) || total <= 0.0)
    return R::failure(invalid("selected total weight must be positive"));
  cx /= total;
  cy /= total;
  cz /= total;
  Matrix3 second{};
  for (const auto& s : samples) {
    const double d[3] = {s.x - cx, s.y - cy, s.z - cz};
    for (std::size_t a = 0; a < 3U; ++a)
      for (std::size_t b = 0; b < 3U; ++b) second[a][b] += s.w * d[a] * d[b];
  }
  const double trace = second[0][0] + second[1][1] + second[2][2];
  Matrix3 inertia{};
  for (std::size_t a = 0; a < 3U; ++a)
    for (std::size_t b = 0; b < 3U; ++b)
      inertia[a][b] = (a == b ? trace : 0.0) - second[a][b];

  std::array<double, 3U> values{};
  Matrix3 vectors{};
  jacobi_eigen(inertia, values, vectors);
  std::array<std::size_t, 3U> order{0U, 1U, 2U};
  std::sort(order.begin(), order.end(),
            [&](std::size_t a, std::size_t b) { return values[a] < values[b]; });

  ShapeResult result;
  result.center = {cx, cy, cz};
  result.radius_of_gyration = std::sqrt(std::max(trace, 0.0) / total);
  for (std::size_t i = 0; i < 3U; ++i) {
    result.principal_moments[i] = std::max(values[order[i]], 0.0);
    model::Vec3d axis{vectors[0][order[i]], vectors[1][order[i]],
                      vectors[2][order[i]]};
    const double largest = std::max({std::abs(axis.x), std::abs(axis.y),
                                     std::abs(axis.z)});
    const double lead = std::abs(axis.x) == largest   ? axis.x
                        : std::abs(axis.y) == largest ? axis.y
                                                       : axis.z;
    if (lead < 0.0) axis = {-axis.x, -axis.y, -axis.z};
    result.principal_axes[i] = axis;
  }
  // Right-handed basis: the third axis is the cross product of the first two.
  const auto& a0 = result.principal_axes[0];
  const auto& a1 = result.principal_axes[1];
  result.principal_axes[2] = {a0.y * a1.z - a0.z * a1.y,
                              a0.z * a1.x - a0.x * a1.z,
                              a0.x * a1.y - a0.y * a1.x};
  const double scale = std::max(result.principal_moments[2], 1.0e-300);
  result.degenerate = (result.principal_moments[1] - result.principal_moments[0]) <=
                          1.0e-9 * scale ||
                      (result.principal_moments[2] - result.principal_moments[1]) <=
                          1.0e-9 * scale;
  result.coordinate_unit = request.frame->metadata().coordinate_unit;
  result.selected_atom_count = selected_count;
  result.used_atom_count = samples.size();
  result.skipped_missing_atom_count = skipped_count;
  if (by_mass) {
    result.total_mass = total;
    if (!request.mass_unit.empty()) result.mass_unit = std::string{request.mass_unit};
    result.mass_source = std::string{request.mass_source};
    result.masses_estimated = request.masses_estimated;
  }
  return R::success(std::move(result));
}

}  // namespace molshredder::analysis
