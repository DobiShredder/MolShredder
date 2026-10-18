#include "molshredder/analysis/measure_tools.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <deque>
#include <limits>

namespace molshredder::analysis {
namespace {

using operation::Error;
using operation::ErrorCode;

Error invalid(std::string message, std::string suggestion = {}) {
  return Error{ErrorCode::invalid_argument, std::move(message), std::move(suggestion)};
}

} // namespace

operation::Result<DipoleResult> dipole_moment(std::span<const model::Vec3d> positions, std::span<const double> charges,
                                              std::span<const double> masses, DipoleReference reference) {
  using R = operation::Result<DipoleResult>;
  if (positions.empty()) return R::failure(invalid("dipole needs at least one atom"));
  if (charges.size() != positions.size()) return R::failure(invalid("dipole needs one charge per atom"));
  if (reference == DipoleReference::mass && masses.size() != positions.size())
    return R::failure(invalid("mass-centered dipole needs one mass per atom"));
  DipoleResult result{};
  for (const auto charge : charges) result.total_charge += charge;
  model::Vec3d origin{0.0, 0.0, 0.0};
  if (reference == DipoleReference::geometric) {
    for (const auto &p : positions) { origin.x += p.x; origin.y += p.y; origin.z += p.z; }
    const auto n = static_cast<double>(positions.size());
    origin = {origin.x / n, origin.y / n, origin.z / n};
  } else if (reference == DipoleReference::mass) {
    double total = 0.0;
    for (std::size_t i = 0; i < positions.size(); ++i) {
      origin.x += masses[i] * positions[i].x; origin.y += masses[i] * positions[i].y; origin.z += masses[i] * positions[i].z;
      total += masses[i];
    }
    if (!(total > 0.0)) return R::failure(invalid("total mass must be positive"));
    origin = {origin.x / total, origin.y / total, origin.z / total};
  }
  for (std::size_t i = 0; i < positions.size(); ++i) {
    result.moment.x += charges[i] * (positions[i].x - origin.x);
    result.moment.y += charges[i] * (positions[i].y - origin.y);
    result.moment.z += charges[i] * (positions[i].z - origin.z);
  }
  result.magnitude = std::sqrt(result.moment.x * result.moment.x + result.moment.y * result.moment.y + result.moment.z * result.moment.z);
  result.reference = origin;
  return R::success(result);
}

operation::Result<std::vector<std::size_t>> surface_atoms(std::span<const model::Vec3d> positions, const SurfaceAtomsRequest &request) {
  using R = operation::Result<std::vector<std::size_t>>;
  if (positions.empty()) return R::failure(invalid("surface detection needs at least one atom"));
  if (!(request.grid_spacing > 0.0) || !(request.radius > 0.0) || request.depth < 0.0 || !std::isfinite(request.grid_spacing) ||
      !std::isfinite(request.radius) || !std::isfinite(request.depth))
    return R::failure(invalid("gridsize and radius must be positive and depth must not be negative"));
  // Exterior points are by construction farther than `radius` from every atom, so one grid cell of discretization
  // tolerance is added to the reach.
  const double reach = request.radius + request.depth + request.grid_spacing;
  const double margin = reach + 2.0 * request.grid_spacing;
  model::Vec3d lo = positions.front(), hi = positions.front();
  for (const auto &p : positions) {
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return R::failure(invalid("atom coordinates must be finite"));
  }
  lo = {lo.x - margin, lo.y - margin, lo.z - margin};
  hi = {hi.x + margin, hi.y + margin, hi.z + margin};
  const auto cells = [&](double a, double b) {
    return static_cast<std::size_t>(std::ceil((b - a) / request.grid_spacing)) + 1U;
  };
  const std::size_t nx = cells(lo.x, hi.x), ny = cells(lo.y, hi.y), nz = cells(lo.z, hi.z);
  if (nx > request.max_grid_points || ny > request.max_grid_points || nz > request.max_grid_points ||
      nx * ny > request.max_grid_points || nx * ny * nz > request.max_grid_points)
    return R::failure(Error{ErrorCode::resource_exhausted, "the surface grid would exceed " + std::to_string(request.max_grid_points) + " points",
                            "use a larger --gridsize or select fewer atoms"});
  const auto index = [&](std::size_t x, std::size_t y, std::size_t z) { return (x * ny + y) * nz + z; };
  // 0 empty, 1 full, 2 exterior empty
  std::vector<std::uint8_t> state(nx * ny * nz, 0U);
  const auto cell_of = [&](double v, double origin) { return static_cast<std::ptrdiff_t>(std::floor((v - origin) / request.grid_spacing)); };
  const auto radius_cells = static_cast<std::ptrdiff_t>(std::ceil(request.radius / request.grid_spacing)) + 1;
  const double radius_squared = request.radius * request.radius;
  const auto clamp = [](std::ptrdiff_t v, std::size_t n) { return std::clamp<std::ptrdiff_t>(v, 0, static_cast<std::ptrdiff_t>(n) - 1); };
  for (const auto &p : positions) {
    const auto cx = cell_of(p.x, lo.x), cy = cell_of(p.y, lo.y), cz = cell_of(p.z, lo.z);
    for (auto x = clamp(cx - radius_cells, nx); x <= clamp(cx + radius_cells, nx); ++x)
      for (auto y = clamp(cy - radius_cells, ny); y <= clamp(cy + radius_cells, ny); ++y)
        for (auto z = clamp(cz - radius_cells, nz); z <= clamp(cz + radius_cells, nz); ++z) {
          const double dx = lo.x + static_cast<double>(x) * request.grid_spacing - p.x;
          const double dy = lo.y + static_cast<double>(y) * request.grid_spacing - p.y;
          const double dz = lo.z + static_cast<double>(z) * request.grid_spacing - p.z;
          if (dx * dx + dy * dy + dz * dz <= radius_squared)
            state[index(static_cast<std::size_t>(x), static_cast<std::size_t>(y), static_cast<std::size_t>(z))] = 1U;
        }
  }
  // Exterior = empty points connected (6-neighbourhood) to the grid boundary, which is always empty (margin).
  std::deque<std::array<std::size_t, 3>> queue;
  const auto seed = [&](std::size_t x, std::size_t y, std::size_t z) {
    auto &cell = state[index(x, y, z)];
    if (cell == 0U) { cell = 2U; queue.push_back({x, y, z}); }
  };
  seed(0, 0, 0);
  while (!queue.empty()) {
    const auto [x, y, z] = queue.front();
    queue.pop_front();
    if (x > 0) seed(x - 1, y, z);
    if (x + 1 < nx) seed(x + 1, y, z);
    if (y > 0) seed(x, y - 1, z);
    if (y + 1 < ny) seed(x, y + 1, z);
    if (z > 0) seed(x, y, z - 1);
    if (z + 1 < nz) seed(x, y, z + 1);
  }
  std::vector<std::size_t> surface;
  const auto reach_cells = static_cast<std::ptrdiff_t>(std::ceil(reach / request.grid_spacing)) + 1;
  const double reach_squared = reach * reach;
  for (std::size_t a = 0; a < positions.size(); ++a) {
    const auto &p = positions[a];
    const auto cx = cell_of(p.x, lo.x), cy = cell_of(p.y, lo.y), cz = cell_of(p.z, lo.z);
    bool exposed = false;
    for (auto x = clamp(cx - reach_cells, nx); x <= clamp(cx + reach_cells, nx) && !exposed; ++x)
      for (auto y = clamp(cy - reach_cells, ny); y <= clamp(cy + reach_cells, ny) && !exposed; ++y)
        for (auto z = clamp(cz - reach_cells, nz); z <= clamp(cz + reach_cells, nz); ++z) {
          if (state[index(static_cast<std::size_t>(x), static_cast<std::size_t>(y), static_cast<std::size_t>(z))] != 2U) continue;
          const double dx = lo.x + static_cast<double>(x) * request.grid_spacing - p.x;
          const double dy = lo.y + static_cast<double>(y) * request.grid_spacing - p.y;
          const double dz = lo.z + static_cast<double>(z) * request.grid_spacing - p.z;
          if (dx * dx + dy * dy + dz * dz <= reach_squared) { exposed = true; break; }
        }
    if (exposed) surface.push_back(a);
  }
  return R::success(std::move(surface));
}

namespace {

using Vec = model::Vec3d;

Vec cross(const Vec &u, const Vec &v) { return {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x}; }
double dot(const Vec &u, const Vec &v) { return u.x * v.x + u.y * v.y + u.z * v.z; }
double length(const Vec &u) { return std::sqrt(dot(u, u)); }

// Inverse of the matrix with columns a, b, c, as rows: fractional = inverse * cartesian.
std::array<Vec, 3> inverse_rows(const model::UnitCell &cell) {
  const double volume = cell.signed_volume();
  const Vec bc = cross(cell.b, cell.c), ca = cross(cell.c, cell.a), ab = cross(cell.a, cell.b);
  return {Vec{bc.x / volume, bc.y / volume, bc.z / volume}, Vec{ca.x / volume, ca.y / volume, ca.z / volume},
          Vec{ab.x / volume, ab.y / volume, ab.z / volume}};
}

} // namespace

operation::Result<std::vector<PbcImage>> pbc_neighbors(std::span<const model::Vec3d> positions, const model::UnitCell &cell,
                                                       const model::Vec3d &center, double cutoff, std::size_t max_images) {
  using R = operation::Result<std::vector<PbcImage>>;
  if (!cell.is_valid()) return R::failure(invalid("the frame has no valid unit cell", "load a file with a cell or set one first"));
  if (!(cutoff > 0.0) || !std::isfinite(cutoff)) return R::failure(invalid("cutoff must be positive"));
  const auto rows = inverse_rows(cell);
  const double volume = cell.signed_volume();
  // Heights of the cell along each axis: the spacing between the two faces that axis crosses.
  const std::array<double, 3> height{volume / length(cross(cell.b, cell.c)), volume / length(cross(cell.c, cell.a)),
                                     volume / length(cross(cell.a, cell.b))};
  std::array<double, 3> margin{};
  for (int k = 0; k < 3; ++k) margin[static_cast<std::size_t>(k)] = 0.5 + cutoff / height[static_cast<std::size_t>(k)];
  std::vector<PbcImage> images;
  for (std::size_t atom = 0; atom < positions.size(); ++atom) {
    const Vec relative{positions[atom].x - center.x, positions[atom].y - center.y, positions[atom].z - center.z};
    const std::array<double, 3> fractional{dot(rows[0], relative), dot(rows[1], relative), dot(rows[2], relative)};
    std::array<int, 3> low{}, high{};
    for (std::size_t k = 0; k < 3U; ++k) {
      low[k] = static_cast<int>(std::ceil(-margin[k] - fractional[k]));
      high[k] = static_cast<int>(std::floor(margin[k] - fractional[k]));
    }
    for (int i = low[0]; i <= high[0]; ++i)
      for (int j = low[1]; j <= high[1]; ++j)
        for (int l = low[2]; l <= high[2]; ++l) {
          if (i == 0 && j == 0 && l == 0) continue;
          if (images.size() >= max_images)
            return R::failure(Error{ErrorCode::resource_exhausted, "more than " + std::to_string(max_images) + " image atoms", "lower the cutoff"});
          images.push_back({atom,
                            {i, j, l},
                            {positions[atom].x + i * cell.a.x + j * cell.b.x + l * cell.c.x,
                             positions[atom].y + i * cell.a.y + j * cell.b.y + l * cell.c.y,
                             positions[atom].z + i * cell.a.z + j * cell.b.z + l * cell.c.z}});
        }
  }
  return R::success(std::move(images));
}

operation::Result<std::array<double, 16>> pbc_to_orthonormal(const model::UnitCell &cell, const model::Vec3d &center) {
  using R = operation::Result<std::array<double, 16>>;
  if (!cell.is_valid()) return R::failure(invalid("the frame has no valid unit cell", "load a file with a cell or set one first"));
  const auto rows = inverse_rows(cell);
  const std::array<double, 3> edge{length(cell.a), length(cell.b), length(cell.c)};
  std::array<double, 16> matrix{};
  for (std::size_t r = 0; r < 3U; ++r) {
    matrix[r * 4U + 0U] = edge[r] * rows[r].x;
    matrix[r * 4U + 1U] = edge[r] * rows[r].y;
    matrix[r * 4U + 2U] = edge[r] * rows[r].z;
    const double moved = matrix[r * 4U + 0U] * center.x + matrix[r * 4U + 1U] * center.y + matrix[r * 4U + 2U] * center.z;
    const double fixed = r == 0U ? center.x : (r == 1U ? center.y : center.z);
    matrix[r * 4U + 3U] = fixed - moved;
  }
  matrix[15] = 1.0;
  return R::success(matrix);
}

std::vector<QtCluster> quality_threshold_clusters(std::size_t count, const std::function<double(std::size_t, std::size_t)> &distance,
                                                  double cutoff) {
  std::vector<QtCluster> clusters;
  std::vector<std::uint8_t> removed(count, 0U);
  std::size_t remaining = count;
  while (remaining > 0U) {
    QtCluster best;
    bool have_best = false;
    for (std::size_t seed = 0; seed < count; ++seed) {
      if (removed[seed] != 0U) continue;
      QtCluster candidate;
      candidate.seed = seed;
      candidate.members.push_back(seed);
      std::vector<std::size_t> pool;
      for (std::size_t other = 0; other < count; ++other)
        if (other != seed && removed[other] == 0U && distance(seed, other) <= cutoff) pool.push_back(other);
      // Greedily add the point that grows the diameter least while it stays within the cutoff.
      std::vector<double> grown(pool.size());
      for (std::size_t i = 0; i < pool.size(); ++i) grown[i] = distance(seed, pool[i]);
      while (!pool.empty()) {
        std::size_t pick = pool.size();
        double pick_diameter = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < pool.size(); ++i) {
          const double diameter = std::max(candidate.diameter, grown[i]);
          if (diameter <= cutoff && diameter < pick_diameter) { pick = i; pick_diameter = diameter; }
        }
        if (pick == pool.size()) break;
        const auto chosen = pool[pick];
        candidate.members.push_back(chosen);
        candidate.diameter = pick_diameter;
        pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(pick));
        grown.erase(grown.begin() + static_cast<std::ptrdiff_t>(pick));
        for (std::size_t i = 0; i < pool.size(); ++i) grown[i] = std::max(grown[i], distance(chosen, pool[i]));
      }
      if (!have_best || candidate.members.size() > best.members.size()) {
        best = std::move(candidate);
        have_best = true;
      }
    }
    std::sort(best.members.begin(), best.members.end());
    for (const auto member : best.members) removed[member] = 1U;
    remaining -= best.members.size();
    clusters.push_back(std::move(best));
  }
  return clusters;
}

} // namespace molshredder::analysis
