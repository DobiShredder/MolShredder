#include <cmath>
#include <iostream>
#include <string_view>

#include "molshredder/analysis/principal_axes.hpp"
#include "molshredder/scene/math.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

bool parallel(molshredder::model::Vec3d left,
              molshredder::model::Vec3d right,
              double tolerance = 1.0e-10) {
  return std::abs(std::abs(molshredder::scene::dot(left, right)) - 1.0) <=
         tolerance;
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  analysis::PrincipalMoments moments;
  for (const auto point : {model::Vec3d{-4.0, -4.0, 0.0},
                           model::Vec3d{-2.0, -2.0, 1.0},
                           model::Vec3d{-1.0, -1.0, -1.0},
                           model::Vec3d{1.0, 1.0, -1.0},
                           model::Vec3d{2.0, 2.0, 1.0},
                           model::Vec3d{4.0, 4.0, 0.0}}) {
    passed &= expect(analysis::accumulate(moments, point),
                     "finite coordinate accumulation must succeed");
  }
  const std::array<model::Vec3d, 3U> identity{
      model::Vec3d{1.0, 0.0, 0.0}, model::Vec3d{0.0, 1.0, 0.0},
      model::Vec3d{0.0, 0.0, 1.0}};
  const auto solved = analysis::calculate_principal_axes(moments, identity);
  passed &= expect(
      solved.has_value() && solved.value().sample_count == 6U &&
          solved.value().variances[0] > solved.value().variances[1] &&
          solved.value().variances[1] > solved.value().variances[2] &&
          parallel(solved.value().axes[0],
                   scene::normalized(model::Vec3d{1.0, 1.0, 0.0})) &&
          scene::dot(scene::cross(solved.value().axes[0],
                                  solved.value().axes[1]),
                     solved.value().axes[2]) > 1.0 - 1.0e-10,
      "principal axes must be variance-sorted and right-handed");

  analysis::PrincipalMoments microscopic;
  for (const auto point : {model::Vec3d{-4.0e-9, -4.0e-9, 0.0},
                           model::Vec3d{-2.0e-9, -2.0e-9, 1.0e-9},
                           model::Vec3d{-1.0e-9, -1.0e-9, -1.0e-9},
                           model::Vec3d{1.0e-9, 1.0e-9, -1.0e-9},
                           model::Vec3d{2.0e-9, 2.0e-9, 1.0e-9},
                           model::Vec3d{4.0e-9, 4.0e-9, 0.0}}) {
    static_cast<void>(analysis::accumulate(microscopic, point));
  }
  const auto microscopic_solved =
      analysis::calculate_principal_axes(microscopic, identity);
  passed &= expect(
      microscopic_solved.has_value() &&
          !microscopic_solved.value().primary_secondary_degenerate &&
          parallel(microscopic_solved.value().axes[0],
                   scene::normalized(model::Vec3d{1.0, 1.0, 0.0})),
      "principal-axis solve must remain scale-aware for small coordinates");

  analysis::PrincipalMoments isotropic;
  for (const auto point : {model::Vec3d{1.0, 0.0, 0.0},
                           model::Vec3d{-1.0, 0.0, 0.0},
                           model::Vec3d{0.0, 1.0, 0.0},
                           model::Vec3d{0.0, -1.0, 0.0},
                           model::Vec3d{0.0, 0.0, 1.0},
                           model::Vec3d{0.0, 0.0, -1.0}}) {
    static_cast<void>(analysis::accumulate(isotropic, point));
  }
  const std::array<model::Vec3d, 3U> preferred{
      model::Vec3d{0.0, 1.0, 0.0}, model::Vec3d{-1.0, 0.0, 0.0},
      model::Vec3d{0.0, 0.0, 1.0}};
  const auto stable = analysis::calculate_principal_axes(isotropic, preferred);
  passed &= expect(
      stable.has_value() && stable.value().primary_secondary_degenerate &&
          stable.value().secondary_tertiary_degenerate &&
          stable.value().axes == preferred,
      "fully degenerate covariance must preserve the preferred camera basis");

  analysis::PrincipalMoments invalid;
  passed &= expect(!analysis::calculate_principal_axes(invalid, identity)
                        .has_value(),
                   "empty moments must be rejected");
  return passed ? 0 : 1;
}
