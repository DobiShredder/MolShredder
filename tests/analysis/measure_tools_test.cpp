#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

#include "molshredder/analysis/measure_tools.hpp"

namespace {

using molshredder::model::Vec3d;
namespace analysis = molshredder::analysis;

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }

} // namespace

int main() {
  bool passed = true;

  // Dipole: +1 at x=0 and -1 at x=2 -> mu = (-2, 0, 0) e*A, independent of the reference for a neutral system.
  {
    const std::vector<Vec3d> positions{{0, 0, 0}, {2, 0, 0}};
    const std::vector<double> charges{1.0, -1.0};
    const std::vector<double> masses{1.0, 3.0};
    for (const auto reference : {analysis::DipoleReference::geometric, analysis::DipoleReference::mass, analysis::DipoleReference::origin}) {
      const auto result = analysis::dipole_moment(positions, charges, masses, reference);
      passed &= expect(result.has_value() && near(result.value().moment.x, -2.0) && near(result.value().magnitude, 2.0) &&
                           near(result.value().total_charge, 0.0),
                       "neutral dipole is -2 e*A along x for every reference");
    }
    // Charged: +1 at 0 and 0 at 2 -> about the geometric center (1,0,0): mu = (-1,0,0); about the origin: 0.
    const std::vector<double> ion{1.0, 0.0};
    const auto geometric = analysis::dipole_moment(positions, ion, masses, analysis::DipoleReference::geometric);
    const auto origin = analysis::dipole_moment(positions, ion, masses, analysis::DipoleReference::origin);
    const auto mass = analysis::dipole_moment(positions, ion, masses, analysis::DipoleReference::mass);
    passed &= expect(near(geometric.value().moment.x, -1.0) && near(origin.value().moment.x, 0.0) && near(mass.value().moment.x, -1.5),
                     "net-charged dipole depends on the reference point");
    passed &= expect(!analysis::dipole_moment(positions, std::vector<double>{1.0}, masses, analysis::DipoleReference::geometric).has_value(),
                     "charge count must match");
    passed &= expect(!analysis::dipole_moment({}, {}, {}, analysis::DipoleReference::geometric).has_value(), "empty selection is an error");
  }

  // Surface atoms: a filled 5x5x5 lattice (spacing 1.5) has only its outer shell exposed to a 1.0 A probe-sized radius.
  {
    std::vector<Vec3d> lattice;
    for (int x = 0; x < 5; ++x)
      for (int y = 0; y < 5; ++y)
        for (int z = 0; z < 5; ++z) lattice.push_back({1.5 * x, 1.5 * y, 1.5 * z});
    analysis::SurfaceAtomsRequest request;
    request.grid_spacing = 0.5;
    request.radius = 1.0;
    request.depth = 0.0;
    const auto surface = analysis::surface_atoms(lattice, request);
    passed &= expect(surface.has_value(), "surface computation succeeds");
    if (surface.has_value()) {
      std::size_t interior_hits = 0;
      for (const auto index : surface.value()) {
        const int x = static_cast<int>(index / 25U), y = static_cast<int>((index / 5U) % 5U), z = static_cast<int>(index % 5U);
        if (x > 0 && x < 4 && y > 0 && y < 4 && z > 0 && z < 4) ++interior_hits;
      }
      // corner/edge/face atoms of the shell are exposed; the 27 inner atoms are buried
      passed &= expect(interior_hits == 0, "buried lattice atoms are not reported as surface atoms");
      passed &= expect(surface.value().size() == 98U, "all 98 shell atoms of the lattice are surface atoms");
    }
    // A deeper window pulls inner layer atoms in.
    request.depth = 1.6;
    const auto deeper = analysis::surface_atoms(lattice, request);
    passed &= expect(deeper.has_value() && deeper.value().size() > 98U, "depth widens the surface layer");
    // A lone atom is a surface atom; resource limits and bad parameters are errors.
    passed &= expect(analysis::surface_atoms(std::vector<Vec3d>{{0, 0, 0}}, request).value().size() == 1U, "single atom is exposed");
    analysis::SurfaceAtomsRequest bad = request;
    bad.grid_spacing = 0.0;
    passed &= expect(!analysis::surface_atoms(lattice, bad).has_value(), "zero grid spacing is rejected");
    bad = request;
    bad.max_grid_points = 100;
    passed &= expect(!analysis::surface_atoms(lattice, bad).has_value(), "grid budget is enforced");
  }

  // QT clustering on 1-D points: {0, 0.5, 1.0} fit in cutoff 1.0; {5, 5.2} form the second cluster; 9 is alone.
  {
    const std::vector<double> points{0.0, 0.5, 1.0, 5.0, 5.2, 9.0};
    const auto distance = [&](std::size_t a, std::size_t b) { return std::abs(points[a] - points[b]); };
    const auto clusters = analysis::quality_threshold_clusters(points.size(), distance, 1.0);
    passed &= expect(clusters.size() == 3U, "three clusters");
    passed &= expect(clusters[0].members == std::vector<std::size_t>({0, 1, 2}) && near(clusters[0].diameter, 1.0), "largest cluster first");
    passed &= expect(clusters[1].members == std::vector<std::size_t>({3, 4}), "second cluster");
    passed &= expect(clusters[2].members == std::vector<std::size_t>({5}), "singleton last");
    // The diameter bound is strict: with cutoff 0.9 point 1.0 no longer joins {0, 0.5, 1.0} but {0.5, 1.0} + ... still bounded.
    const auto tighter = analysis::quality_threshold_clusters(points.size(), distance, 0.6);
    for (const auto &cluster : tighter) {
      for (const auto a : cluster.members)
        for (const auto b : cluster.members) passed &= expect(distance(a, b) <= 0.6 + 1e-12, "QT clusters never exceed the cutoff diameter");
    }
    std::size_t total = 0;
    for (const auto &cluster : tighter) total += cluster.members.size();
    passed &= expect(total == points.size(), "every point lands in exactly one cluster");
    // Chains that would pass a seed-radius test but violate the diameter are split: 0, 0.9, 1.8 with cutoff 1.0.
    const std::vector<double> chain{0.0, 0.9, 1.8};
    const auto chain_clusters = analysis::quality_threshold_clusters(
        chain.size(), [&](std::size_t a, std::size_t b) { return std::abs(chain[a] - chain[b]); }, 1.0);
    passed &= expect(chain_clusters.size() == 2U && chain_clusters[0].members.size() == 2U, "diameter, not seed radius, bounds the cluster");
    passed &= expect(analysis::quality_threshold_clusters(0, distance, 1.0).empty(), "empty input has no clusters");
  }

  return passed ? 0 : 1;
}
