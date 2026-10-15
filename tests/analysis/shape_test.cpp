#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#include "molshredder/analysis/shape.hpp"
#include "molshredder/model/coordinates.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

bool near(double left, double right, double tolerance = 1.0e-10) {
  return std::abs(left - right) <= tolerance;
}

std::shared_ptr<const molshredder::model::CoordinateFrame>
frame_of(std::vector<molshredder::model::Vec3d> points,
         std::vector<std::uint8_t> present = {}) {
  using namespace molshredder;
  if (present.empty()) present.assign(points.size(), 1U);
  return model::CoordinateFrame::create(
             model::CoordinateBuffer{std::move(points)}, std::nullopt,
             std::move(present), model::FrameMetadata{})
      .value();
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;

  // Rectangle +-2 by +-1 in the z = 0 plane, far from the origin on purpose.
  const double ox = 1.0e6;
  const auto rectangle = frame_of({{ox + 2.0, 1.0, 0.0},
                                   {ox - 2.0, 1.0, 0.0},
                                   {ox + 2.0, -1.0, 0.0},
                                   {ox - 2.0, -1.0, 0.0}});
  analysis::ShapeRequest request;
  request.frame = rectangle.get();
  const auto shape = analysis::calculate_shape(request);
  passed &= expect(shape.has_value(), "rectangle shape must succeed");
  if (shape.has_value()) {
    const auto& value = shape.value();
    passed &= expect(near(value.center.x, ox, 1.0e-6) && near(value.center.y, 0.0),
                     "centroid of the rectangle");
    passed &= expect(near(value.radius_of_gyration, std::sqrt(5.0), 1.0e-6),
                     "radius of gyration is sqrt(a^2 + b^2)");
    passed &= expect(near(value.principal_moments[0], 4.0, 1.0e-6) &&
                         near(value.principal_moments[1], 16.0, 1.0e-6) &&
                         near(value.principal_moments[2], 20.0, 1.0e-6),
                     "rectangle principal moments 4, 16, 20");
    passed &= expect(near(std::abs(value.principal_axes[0].x), 1.0, 1.0e-6) &&
                         near(std::abs(value.principal_axes[1].y), 1.0, 1.0e-6) &&
                         near(std::abs(value.principal_axes[2].z), 1.0, 1.0e-6),
                     "rectangle principal axes are the coordinate axes");
    const auto& a = value.principal_axes;
    passed &= expect(near(a[0].y * a[1].z - a[0].z * a[1].y, a[2].x) &&
                         near(a[0].z * a[1].x - a[0].x * a[1].z, a[2].y) &&
                         near(a[0].x * a[1].y - a[0].y * a[1].x, a[2].z),
                     "principal axes are right-handed");
    passed &= expect(!value.degenerate && !value.total_mass.has_value() &&
                         value.used_atom_count == 4U,
                     "rectangle is non-degenerate and unweighted");
  }

  // Mass weighting: masses 1 and 3 at x = 0 and 4 -> center 3, Rg = sqrt(3).
  const auto pair = frame_of({{0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}});
  const std::vector<double> masses{1.0, 3.0};
  analysis::ShapeRequest weighted;
  weighted.frame = pair.get();
  weighted.mode = analysis::CenterMode::center_of_mass;
  weighted.masses = masses;
  weighted.mass_source = "fixture";
  weighted.mass_unit = "dalton";
  const auto linear = analysis::calculate_shape(weighted);
  passed &= expect(linear.has_value() && near(linear.value().center.x, 3.0) &&
                       near(linear.value().radius_of_gyration, std::sqrt(3.0)) &&
                       linear.value().total_mass.has_value() &&
                       near(*linear.value().total_mass, 4.0) &&
                       linear.value().degenerate &&
                       near(linear.value().principal_moments[0], 0.0, 1.0e-9) &&
                       near(linear.value().principal_moments[1], 12.0, 1.0e-9),
                   "weighted linear molecule has a zero axial moment and is degenerate");

  // Contract: unweighted requests reject masses, weighted requests need them.
  analysis::ShapeRequest bad = request;
  bad.masses = masses;
  passed &= expect(!analysis::calculate_shape(bad).has_value(),
                   "centroid shape must reject mass values");
  analysis::ShapeRequest missing_masses = weighted;
  missing_masses.masses = {};
  passed &= expect(!analysis::calculate_shape(missing_masses).has_value(),
                   "mass-weighted shape requires one mass per atom");

  // Missing atoms: error by default, skipped with the explicit policy.
  const auto holes = frame_of({{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {9.0, 9.0, 9.0}},
                              {1U, 1U, 0U});
  analysis::ShapeRequest missing;
  missing.frame = holes.get();
  passed &= expect(!analysis::calculate_shape(missing).has_value(),
                   "a missing atom must be an error by default");
  missing.missing_atom_policy = analysis::MissingAtomPolicy::skip;
  const auto skipped = analysis::calculate_shape(missing);
  passed &= expect(skipped.has_value() && skipped.value().used_atom_count == 2U &&
                       skipped.value().skipped_missing_atom_count == 1U &&
                       near(skipped.value().radius_of_gyration, 1.0),
                   "skip policy ignores missing atoms and reports the count");

  // Invalid selection mask size.
  const std::vector<std::uint8_t> short_mask{1U};
  analysis::ShapeRequest masked = request;
  masked.selected = short_mask;
  passed &= expect(!analysis::calculate_shape(masked).has_value(),
                   "an invalid selection mask must be rejected");
  return passed ? 0 : 1;
}
