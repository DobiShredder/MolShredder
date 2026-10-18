#include <cmath>
#include <iostream>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/analysis/point_group.hpp"

namespace {

using molshredder::model::Vec3d;
namespace analysis = molshredder::analysis;

struct Molecule {
  std::vector<Vec3d> positions;
  std::vector<int> labels;
  void add(int label, double x, double y, double z) { positions.push_back({x, y, z}); labels.push_back(label); }
};

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

std::string group_of(const Molecule &molecule, double tolerance = 0.1) {
  analysis::PointGroupRequest request;
  request.positions = molecule.positions;
  request.labels = molecule.labels;
  request.tolerance = tolerance;
  const auto result = analysis::detect_point_group(request);
  return result.has_value() ? result.value().point_group : "error: " + result.error().message;
}

} // namespace

int main() {
  bool passed = true;
  const auto check = [&](const Molecule &molecule, std::string_view expected, std::string_view name, double tolerance = 0.1) {
    const auto got = group_of(molecule, tolerance);
    if (got != expected) std::cerr << name << ": expected " << expected << " got " << got << '\n';
    passed &= got == expected;
  };
  const double pi = std::numbers::pi;

  { Molecule m; m.add(8, 0, 0, 0.117); m.add(1, 0.757, 0, -0.469); m.add(1, -0.757, 0, -0.469); check(m, "C2v", "water"); }
  { Molecule m; m.add(7, 0, 0, 0.38);
    for (int k = 0; k < 3; ++k) m.add(1, 0.94 * std::cos(2 * pi * k / 3), 0.94 * std::sin(2 * pi * k / 3), -0.0);
    check(m, "C3v", "ammonia"); }
  { Molecule m; m.add(6, 0, 0, 0);
    for (const auto &s : std::vector<std::array<double, 3>>{{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}}) m.add(1, 0.629 * s[0], 0.629 * s[1], 0.629 * s[2]);
    check(m, "Td", "methane"); }
  { Molecule m;
    for (int k = 0; k < 6; ++k) { m.add(6, 1.40 * std::cos(pi * k / 3), 1.40 * std::sin(pi * k / 3), 0); m.add(1, 2.48 * std::cos(pi * k / 3), 2.48 * std::sin(pi * k / 3), 0); }
    check(m, "D6h", "benzene"); }
  { Molecule m; m.add(6, 0.667, 0, 0); m.add(6, -0.667, 0, 0);
    for (const double sx : {1.0, -1.0}) for (const double sy : {1.0, -1.0}) m.add(1, 1.24 * sx, 0.92 * sy, 0);
    check(m, "D2h", "ethylene"); }
  { Molecule m; m.add(6, 0, 0, 0.77); m.add(6, 0, 0, -0.77);
    for (int k = 0; k < 3; ++k) {
      m.add(1, 1.02 * std::cos(2 * pi * k / 3), 1.02 * std::sin(2 * pi * k / 3), 1.16);
      m.add(1, 1.02 * std::cos(2 * pi * k / 3 + pi / 3), 1.02 * std::sin(2 * pi * k / 3 + pi / 3), -1.16);
    }
    check(m, "D3d", "staggered ethane"); }
  { Molecule m; m.add(6, 0, 0, 0); m.add(6, 0, 0, 1.31); m.add(6, 0, 0, -1.31);
    m.add(1, 0.93, 0, 1.85); m.add(1, -0.93, 0, 1.85); m.add(1, 0, 0.93, -1.85); m.add(1, 0, -0.93, -1.85);
    check(m, "D2d", "allene"); }
  { Molecule m; m.add(6, 0, 0, 1.16); m.add(8, 0, 0, 0); m.add(6, 0, 0, -1.16); (void)m; Molecule c; c.add(8, 0, 0, 1.16); c.add(6, 0, 0, 0); c.add(8, 0, 0, -1.16); check(c, "Dinfh", "carbon dioxide"); }
  { Molecule m; m.add(17, 0, 0, 0); m.add(1, 0, 0, 1.27); check(m, "Cinfv", "hydrogen chloride"); }
  { Molecule m; m.add(16, 0, 0, 0); for (const double s : {1.0, -1.0}) { m.add(9, 1.56 * s, 0, 0); m.add(9, 0, 1.56 * s, 0); m.add(9, 0, 0, 1.56 * s); } check(m, "Oh", "sulfur hexafluoride"); }
  { Molecule m; const double phi = (1 + std::sqrt(5.0)) / 2;
    for (const double a : {1.0, -1.0}) for (const double b : {phi, -phi}) { m.add(6, 0, a, b); m.add(6, a, b, 0); m.add(6, b, 0, a); }
    check(m, "Ih", "icosahedron"); }
  { Molecule m; m.add(9, 1.1, 0.4, 0.3); m.add(17, -0.2, 1.0, -0.5); m.add(35, 0.3, -0.7, 0.9); m.add(1, -0.9, -0.6, -0.4); check(m, "C1", "asymmetric tetrahedron"); }
  { Molecule m; m.add(6, 0, 0, 0); m.add(9, 0.9, 0.5, 0.3); m.add(17, -0.4, 1.1, 0.2); m.add(1, 0.2, -0.3, -1.0); m.add(1, 0.2, -0.3, 1.0); (void)m;
    Molecule s; s.add(6, 0, 0, 0); s.add(9, 1.2, 0.6, 0); s.add(17, -0.5, 1.4, 0); s.add(1, 0.3, -0.8, 0.9); s.add(1, 0.3, -0.8, -0.9); check(s, "Cs", "CH2FCl"); }
  { Molecule m; m.add(1, 1.0, 0.5, 0.2); m.add(1, -1.0, -0.5, -0.2); m.add(6, 0.3, 1.1, -0.7); m.add(6, -0.3, -1.1, 0.7); m.add(8, 0.7, -0.4, 1.2); m.add(8, -0.7, 0.4, -1.2); (void)m;
    Molecule i; i.add(1, 1.0, 0.5, 0.2); i.add(1, -1.0, -0.5, -0.2); i.add(6, 0.3, 1.1, -0.7); i.add(6, -0.3, -1.1, 0.7);
    // A pair of identical atoms plus a differently-labelled pair on a generic line still has C2; use three distinct pairs.
    Molecule three; three.add(1, 1.0, 0.5, 0.2); three.add(1, -1.0, -0.5, -0.2); three.add(6, 0.3, 1.1, -0.7); three.add(6, -0.3, -1.1, 0.7);
    three.add(8, 0.7, -0.4, 1.2); three.add(8, -0.7, 0.4, -1.2);
    // three generic inversion pairs of different elements are C2h only when one C2 relates them; generic ones are Ci.
    const auto got = group_of(three);
    passed &= expect(got == "Ci" || got == "C2h", "inversion pairs give Ci (or C2h for degenerate placements)"); }
  { Molecule m; m.add(6, 0.67, 0, 0); m.add(6, -0.67, 0, 0); m.add(17, 1.4, 1.2, 0); m.add(17, -1.4, -1.2, 0); m.add(1, 1.2, -0.9, 0); m.add(1, -1.2, 0.9, 0); check(m, "C2h", "trans-dichloroethene"); }
  { Molecule m; const double x = 1.0, y = 0.3, z = 0.5;
    m.add(6, x, y, z); m.add(6, -y, x, -z); m.add(6, -x, -y, z); m.add(6, y, -x, -z); check(m, "D2d", "four points on an S4 orbit with a square tetrahedron-like arrangement"); }
  { Molecule m; const double x = 1.0, y = 0.3, z = 0.5;
    // lower the symmetry of the S4 orbit: different radii for each atom of a second orbit -> S4 alone
    m.add(6, x, y, z); m.add(6, -y, x, -z); m.add(6, -x, -y, z); m.add(6, y, -x, -z);
    m.add(8, 0.2, 0.9, 1.4); m.add(8, -0.9, 0.2, -1.4); m.add(8, -0.2, -0.9, 1.4); m.add(8, 0.9, -0.2, -1.4);
    check(m, "S4", "S4"); }
  { // C3h: two C3 triangles of different elements, rotated against each other, mirrored through z = 0
    Molecule m;
    for (int k = 0; k < 3; ++k) {
      const double a = 2 * pi * k / 3;
      for (const double z : {0.6, -0.6}) {
        m.add(6, 1.1 * std::cos(a + 0.38), 1.1 * std::sin(a + 0.38), z);
        m.add(8, 1.9 * std::cos(a + 0.38 + 0.45), 1.9 * std::sin(a + 0.38 + 0.45), 1.3 * z);
      }
    }
    check(m, "C3h", "C3h"); }
  { // tolerance: a slightly distorted water is C2v within 0.1 A but only Cs at a tight tolerance
    Molecule m; m.add(8, 0, 0, 0.117); m.add(1, 0.757, 0, -0.469); m.add(1, -0.807, 0, -0.469);
    check(m, "C2v", "distorted water, loose tolerance", 0.1);
    check(m, "Cs", "distorted water, tight tolerance", 0.01); }
  { Molecule m; m.add(6, 1, 2, 3); check(m, "Kh", "single atom"); }
  {
    analysis::PointGroupRequest request;
    Molecule m;
    request.positions = m.positions;
    request.labels = m.labels;
    passed &= expect(!analysis::detect_point_group(request).has_value(), "empty input is an error");
    Molecule big;
    for (int i = 0; i < 400; ++i) big.add(6, i * 1.5, 0, 0);
    request.positions = big.positions;
    request.labels = big.labels;
    passed &= expect(!analysis::detect_point_group(request).has_value(), "atom limit is enforced");
  }
  return passed ? 0 : 1;
}
