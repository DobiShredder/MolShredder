#include "molshredder/analysis/point_group.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <set>
#include <tuple>
#include <vector>

namespace molshredder::analysis {
namespace {

using Vec = model::Vec3d;
using Mat = std::array<double, 9>;

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument, std::move(message), std::move(suggestion)};
}

double dot(const Vec &a, const Vec &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec cross(const Vec &a, const Vec &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double norm(const Vec &a) { return std::sqrt(dot(a, a)); }
Vec add(const Vec &a, const Vec &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec sub(const Vec &a, const Vec &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

Vec apply(const Mat &m, const Vec &v) {
  return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z, m[6] * v.x + m[7] * v.y + m[8] * v.z};
}

Mat multiply(const Mat &a, const Mat &b) {
  Mat r{};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      for (int k = 0; k < 3; ++k) r[static_cast<std::size_t>(i * 3 + j)] += a[static_cast<std::size_t>(i * 3 + k)] * b[static_cast<std::size_t>(k * 3 + j)];
  return r;
}

Mat rotation(const Vec &u, double angle) {
  const double c = std::cos(angle), s = std::sin(angle), t = 1.0 - c;
  return {t * u.x * u.x + c,       t * u.x * u.y - s * u.z, t * u.x * u.z + s * u.y,
          t * u.x * u.y + s * u.z, t * u.y * u.y + c,       t * u.y * u.z - s * u.x,
          t * u.x * u.z - s * u.y, t * u.y * u.z + s * u.x, t * u.z * u.z + c};
}

Mat reflection(const Vec &n) {
  return {1 - 2 * n.x * n.x, -2 * n.x * n.y,    -2 * n.x * n.z,
          -2 * n.x * n.y,    1 - 2 * n.y * n.y, -2 * n.y * n.z,
          -2 * n.x * n.z,    -2 * n.y * n.z,    1 - 2 * n.z * n.z};
}

constexpr Mat kInversion{-1, 0, 0, 0, -1, 0, 0, 0, -1};

class Detector {
public:
  Detector(std::vector<Vec> positions, std::vector<int> labels, double tolerance)
      : positions_{std::move(positions)}, labels_{std::move(labels)}, tolerance_{tolerance} {}

  // True when every atom maps onto a distinct atom with the same label within the tolerance.
  bool maps(const Mat &matrix) const {
    const std::size_t n = positions_.size();
    std::vector<std::uint8_t> used(n, 0U);
    const double limit = tolerance_ * tolerance_;
    for (std::size_t i = 0; i < n; ++i) {
      const Vec image = apply(matrix, positions_[i]);
      bool found = false;
      for (std::size_t j = 0; j < n; ++j) {
        if (used[j] != 0U || labels_[j] != labels_[i]) continue;
        const Vec d = sub(image, positions_[j]);
        if (dot(d, d) <= limit) { used[j] = 1U; found = true; break; }
      }
      if (!found) return false;
    }
    return true;
  }

  // Highest n among `orders` for which the rotation by 2 pi / n is a symmetry operation; 0 when none.
  int rotation_order(const Vec &u) const {
    for (const int n : {8, 7, 6, 5, 4, 3, 2})
      if (maps(rotation(u, 2.0 * std::numbers::pi / n))) return n;
    return 0;
  }

  int improper_order(const Vec &u) const {
    for (const int n : {12, 10, 8, 6, 5, 4, 3}) {
      const Mat s = multiply(reflection(u), rotation(u, 2.0 * std::numbers::pi / n));
      if (maps(s)) return n;
    }
    return 0;
  }

  bool mirror(const Vec &normal) const { return maps(reflection(normal)); }

  const std::vector<Vec> &positions() const { return positions_; }
  const std::vector<int> &labels() const { return labels_; }
  double tolerance() const { return tolerance_; }

private:
  std::vector<Vec> positions_;
  std::vector<int> labels_;
  double tolerance_;
};

// Unit direction canonicalised up to sign; false when the vector is (nearly) zero.
bool canonical(Vec &v, double minimum) {
  const double length = norm(v);
  if (!(length > minimum)) return false;
  v = {v.x / length, v.y / length, v.z / length};
  for (const double component : {v.x, v.y, v.z}) {
    if (std::abs(component) < 1.0e-6) continue;
    if (component < 0.0) v = {-v.x, -v.y, -v.z};
    break;
  }
  return true;
}

class DirectionSet {
public:
  bool insert(Vec v, double minimum) {
    if (!canonical(v, minimum)) return false;
    const auto key = std::make_tuple(std::llround(v.x * 2000.0), std::llround(v.y * 2000.0), std::llround(v.z * 2000.0));
    if (!seen_.insert(key).second) return false;
    items_.push_back(v);
    return true;
  }
  const std::vector<Vec> &items() const { return items_; }
  std::size_t size() const { return items_.size(); }

private:
  std::set<std::tuple<long long, long long, long long>> seen_;
  std::vector<Vec> items_;
};

bool parallel(const Vec &a, const Vec &b, double tolerance = 1.0e-3) { return std::abs(std::abs(dot(a, b)) - 1.0) < tolerance; }
bool perpendicular(const Vec &a, const Vec &b, double tolerance = 1.0e-3) { return std::abs(dot(a, b)) < tolerance; }

} // namespace

operation::Result<PointGroupResult> detect_point_group(const PointGroupRequest &request) {
  using R = operation::Result<PointGroupResult>;
  const std::size_t n = request.positions.size();
  if (n == 0U) return R::failure(invalid("point group detection needs at least one atom"));
  if (request.labels.size() != n) return R::failure(invalid("point group detection needs one label per atom"));
  if (!request.masses.empty() && request.masses.size() != n) return R::failure(invalid("masses must match the atoms"));
  if (!(request.tolerance > 0.0) || !std::isfinite(request.tolerance)) return R::failure(invalid("tolerance must be positive"));
  if (n > request.max_atoms)
    return R::failure(operation::Error{operation::ErrorCode::resource_exhausted,
                                       "point group detection is limited to " + std::to_string(request.max_atoms) + " atoms",
                                       "select the atoms of the core you want to analyse"});
  PointGroupResult result;
  result.tolerance = request.tolerance;
  // Centre of mass (centroid when masses are absent) is where every symmetry element passes.
  Vec center{0, 0, 0};
  double total = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double weight = request.masses.empty() ? 1.0 : request.masses[i];
    center = add(center, {request.positions[i].x * weight, request.positions[i].y * weight, request.positions[i].z * weight});
    total += weight;
  }
  if (!(total > 0.0)) return R::failure(invalid("total mass must be positive"));
  center = {center.x / total, center.y / total, center.z / total};
  result.center = center;
  std::vector<Vec> shifted;
  shifted.reserve(n);
  for (const auto &p : request.positions) shifted.push_back(sub(p, center));
  if (n == 1U) {
    result.point_group = "Kh";
    result.inversion = true;
    return R::success(std::move(result));
  }
  Detector detector{shifted, std::vector<int>(request.labels.begin(), request.labels.end()), request.tolerance};

  // Linear molecules: all atoms within the tolerance of one line through the centre.
  {
    Vec axis{0, 0, 0};
    double farthest = 0.0;
    for (const auto &p : shifted)
      if (norm(p) > farthest) { farthest = norm(p); axis = p; }
    if (farthest <= request.tolerance) {  // all atoms within the tolerance of the centre
      result.point_group = "Kh";
      result.inversion = true;
      return R::success(std::move(result));
    }
    axis = {axis.x / farthest, axis.y / farthest, axis.z / farthest};
    bool linear = true;
    for (const auto &p : shifted) {
      const double along = dot(p, axis);
      const Vec off{p.x - along * axis.x, p.y - along * axis.y, p.z - along * axis.z};
      if (norm(off) > request.tolerance) { linear = false; break; }
    }
    if (linear) {
      result.linear = true;
      result.inversion = detector.maps(kInversion);
      result.point_group = result.inversion ? "Dinfh" : "Cinfv";
      result.principal_axis = axis;
      return R::success(std::move(result));
    }
  }

  result.inversion = detector.maps(kInversion);

  // Candidate directions for axes and mirror normals.
  DirectionSet candidates;
  const double minimum = std::max(1.0e-6, request.tolerance * 0.01);
  for (const auto &p : shifted) candidates.insert(p, request.tolerance * 0.5);
  std::vector<std::vector<std::size_t>> orbits;  // same label and radius
  {
    std::vector<std::uint8_t> assigned(n, 0U);
    for (std::size_t i = 0; i < n; ++i) {
      if (assigned[i] != 0U) continue;
      std::vector<std::size_t> orbit{i};
      assigned[i] = 1U;
      for (std::size_t j = i + 1U; j < n; ++j)
        if (assigned[j] == 0U && detector.labels()[j] == detector.labels()[i] && std::abs(norm(shifted[j]) - norm(shifted[i])) <= request.tolerance) {
          orbit.push_back(j);
          assigned[j] = 1U;
        }
      orbits.push_back(std::move(orbit));
    }
  }
  constexpr std::size_t kCandidateLimit = 12000U;
  for (const auto &orbit : orbits) {
    for (std::size_t a = 0; a < orbit.size(); ++a)
      for (std::size_t b = a + 1U; b < orbit.size(); ++b) {
        const Vec &pa = shifted[orbit[a]], &pb = shifted[orbit[b]];
        candidates.insert(add(pa, pb), request.tolerance * 0.5);
        candidates.insert(sub(pa, pb), request.tolerance * 0.5);
        candidates.insert(cross(pa, pb), minimum);
      }
    if (orbit.size() >= 3U && orbit.size() <= 12U)
      for (std::size_t a = 0; a < orbit.size(); ++a)
        for (std::size_t b = a + 1U; b < orbit.size(); ++b)
          for (std::size_t c = b + 1U; c < orbit.size(); ++c)
            candidates.insert(add(add(shifted[orbit[a]], shifted[orbit[b]]), shifted[orbit[c]]), request.tolerance * 0.5);
    if (candidates.size() > kCandidateLimit)
      return R::failure(operation::Error{operation::ErrorCode::resource_exhausted, "too many candidate symmetry elements",
                                         "select fewer atoms or use a larger --tolerance"});
  }
  // Pairs of different orbits of the same label would add little; cross products of atoms in distinct orbits cover planes.
  for (std::size_t a = 0; a < n && candidates.size() <= kCandidateLimit; ++a)
    for (std::size_t b = a + 1U; b < n; ++b) candidates.insert(cross(shifted[a], shifted[b]), minimum);
  if (candidates.size() > kCandidateLimit)
    return R::failure(operation::Error{operation::ErrorCode::resource_exhausted, "too many candidate symmetry elements",
                                       "select fewer atoms or use a larger --tolerance"});

  struct Axis { Vec direction; int order; };
  std::vector<Axis> axes;
  for (const auto &direction : candidates.items())
    if (const int order = detector.rotation_order(direction); order >= 2) axes.push_back({direction, order});
  std::vector<Vec> planes;
  DirectionSet normals;
  for (const auto &direction : candidates.items()) normals.insert(direction, minimum);
  for (const auto &axis : axes) normals.insert(axis.direction, minimum);
  for (const auto &normal : normals.items())
    if (detector.mirror(normal)) planes.push_back(normal);
  std::vector<std::pair<Vec, int>> improper;
  for (const auto &direction : candidates.items())
    if (const int order = detector.improper_order(direction); order >= 3) improper.emplace_back(direction, order);

  for (const auto &axis : axes) ++result.rotation_axes[axis.order];
  result.mirror_planes = planes.size();
  for (const auto &[direction, order] : improper) ++result.improper_axes[order];

  const auto count_order = [&](int order) {
    std::size_t c = 0;
    for (const auto &axis : axes) c += axis.order == order ? 1U : 0U;
    return c;
  };
  const auto has_axis_with_order_multiple = [&](int order) {
    std::size_t c = 0;
    for (const auto &axis : axes) c += (axis.order % order == 0) ? 1U : 0U;
    return c;
  };

  // Cubic groups: several axes of order >= 3.
  if (has_axis_with_order_multiple(3) >= 4U && (count_order(3) + count_order(6)) >= 4U) {
    if (count_order(5) >= 6U) { result.point_group = result.inversion ? "Ih" : "I"; return R::success(std::move(result)); }
    if (count_order(4) >= 3U) { result.point_group = result.inversion ? "Oh" : "O"; return R::success(std::move(result)); }
    if (result.inversion) result.point_group = "Th";
    else result.point_group = planes.size() >= 6U ? "Td" : "T";
    return R::success(std::move(result));
  }

  if (axes.empty()) {
    if (!planes.empty()) result.point_group = "Cs";
    else if (result.inversion) result.point_group = "Ci";
    else if (!improper.empty()) result.point_group = "S" + std::to_string(improper.front().second);
    else result.point_group = "C1";
    return R::success(std::move(result));
  }

  int highest = 0;
  for (const auto &axis : axes) highest = std::max(highest, axis.order);

  // Rank candidates so the richest description wins when several axes share the highest order (D2 family).
  struct Description { int rank; std::string name; Vec axis; };
  Description best{-1, "", {0, 0, 0}};
  for (const auto &principal : axes) {
    if (principal.order != highest) continue;
    const int order = principal.order;
    std::size_t perpendicular_c2 = 0;
    for (const auto &other : axes)
      if (other.order % 2 == 0 && perpendicular(other.direction, principal.direction)) ++perpendicular_c2;
    std::size_t horizontal = 0, vertical = 0;
    for (const auto &normal : planes) {
      if (parallel(normal, principal.direction)) ++horizontal;
      else if (perpendicular(normal, principal.direction)) ++vertical;
    }
    bool s2n = false;
    for (const auto &[direction, improper_order] : improper)
      if (parallel(direction, principal.direction) && improper_order % (2 * order) == 0) s2n = true;
    Description description{0, "", principal.direction};
    const std::string n_text = std::to_string(order);
    if (perpendicular_c2 >= static_cast<std::size_t>(order)) {
      if (horizontal > 0U) description = {5, "D" + n_text + "h", principal.direction};
      else if (vertical > 0U) description = {4, "D" + n_text + "d", principal.direction};
      else description = {3, "D" + n_text, principal.direction};
    } else if (horizontal > 0U) {
      description = {2, "C" + n_text + "h", principal.direction};
    } else if (vertical > 0U) {
      description = {2, "C" + n_text + "v", principal.direction};
    } else if (s2n) {
      description = {1, "S" + std::to_string(2 * order), principal.direction};
    } else {
      description = {0, "C" + n_text, principal.direction};
    }
    if (description.rank > best.rank) best = description;
  }
  result.point_group = best.name;
  result.principal_axis = best.axis;
  return R::success(std::move(result));
}

} // namespace molshredder::analysis
