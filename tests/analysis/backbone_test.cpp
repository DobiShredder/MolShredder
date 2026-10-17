#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/analysis/backbone.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"

namespace {

bool expect(bool condition, const std::string& message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

using molshredder::model::Vec3d;

// Independent IUPAC dihedral (positive when the rear bond lies clockwise of the
// front bond looking from B to C): atan2(|b2| b1.(b2 x b3), (b1 x b2).(b2 x b3)).
double reference_dihedral(Vec3d a, Vec3d b, Vec3d c, Vec3d d) {
  const auto sub = [](Vec3d p, Vec3d q) { return Vec3d{p.x - q.x, p.y - q.y, p.z - q.z}; };
  const auto cross = [](Vec3d p, Vec3d q) {
    return Vec3d{p.y * q.z - p.z * q.y, p.z * q.x - p.x * q.z, p.x * q.y - p.y * q.x};
  };
  const auto dot = [](Vec3d p, Vec3d q) { return p.x * q.x + p.y * q.y + p.z * q.z; };
  const auto b1 = sub(b, a), b2 = sub(c, b), b3 = sub(d, c);
  const auto length = std::sqrt(dot(b2, b2));
  return std::atan2(length * dot(b1, cross(b2, b3)), dot(cross(b1, b2), cross(b2, b3))) *
         180.0 / std::numbers::pi;
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;

  // Three residues (N, CA, C each) plus a distant fourth that is not linked.
  model::TopologyBuilder builder;
  std::vector<Vec3d> points;
  const std::vector<std::array<Vec3d, 3>> residues{
      {{{0.0, 0.0, 0.0}, {1.5, 0.0, 0.0}, {2.0, 1.4, 0.0}}},      // residue 1
      {{{3.2, 1.6, 0.4}, {4.5, 2.2, 0.9}, {5.6, 1.3, 1.9}}},      // residue 2
      {{{6.7, 1.9, 2.7}, {7.9, 1.2, 3.4}, {9.0, 2.1, 4.1}}},      // residue 3
      {{{40.0, 0.0, 0.0}, {41.5, 0.0, 0.0}, {42.0, 1.4, 0.0}}}};  // residue 4, far away
  for (std::size_t index = 0; index < residues.size(); ++index) {
    const auto residue = builder.add_residue(model::ResidueRecord{
        "GLY", static_cast<std::int64_t>(index + 1U), "", "A", ""});
    const std::array<std::string, 3> names{"N", "CA", "C"};
    for (std::size_t atom = 0; atom < 3U; ++atom) {
      static_cast<void>(builder.add_atom(model::AtomRecord{
          names[atom], static_cast<std::uint8_t>(atom == 0U ? 7U : 6U),
          residue.value(), "", 0, static_cast<std::int64_t>(index * 3U + atom + 1U)}));
      points.push_back(residues[index][atom]);
    }
  }
  const auto topology = builder.build().value();
  const auto frame = model::CoordinateFrame::create(
                         model::CoordinateBuffer{points}, std::nullopt,
                         std::vector<std::uint8_t>(points.size(), 1U),
                         model::FrameMetadata{})
                         .value();

  analysis::BackboneRequest request;
  request.topology = topology.get();
  request.frame = frame.get();
  const auto angles = analysis::calculate_backbone_angles(request);
  passed &= expect(angles.has_value() && angles.value().size() == 4U, "four complete residues");
  if (angles.has_value() && angles.value().size() == 4U) {
    const auto& value = angles.value();
    passed &= expect(!value[0].phi_degrees && value[0].psi_degrees,
                     "the first residue has no phi but has psi");
    passed &= expect(value[1].phi_degrees && value[1].psi_degrees,
                     "an interior residue has both angles");
    passed &= expect(value[2].phi_degrees && !value[2].psi_degrees,
                     "the residue before a chain break has no psi");
    passed &= expect(!value[3].phi_degrees && !value[3].psi_degrees,
                     "an isolated residue has neither angle");
    const auto close = [](double left, double right) { return std::abs(left - right) < 1.0e-6; };
    passed &= expect(
        close(*value[1].phi_degrees,
              reference_dihedral(residues[0][2], residues[1][0], residues[1][1], residues[1][2])),
        "phi equals the independent dihedral of C(i-1) N CA C");
    passed &= expect(
        close(*value[1].psi_degrees,
              reference_dihedral(residues[1][0], residues[1][1], residues[1][2], residues[2][0])),
        "psi equals the independent dihedral of N CA C N(i+1)");
    passed &= expect(
        close(*value[0].psi_degrees,
              reference_dihedral(residues[0][0], residues[0][1], residues[0][2], residues[1][0])),
        "the first psi uses the next residue's nitrogen");
  }

  // Selection and link distance.
  std::vector<std::uint8_t> only_second(points.size(), 0U);
  only_second[4] = 1U;  // CA of residue 2
  request.selected = only_second;
  const auto picked = analysis::calculate_backbone_angles(request);
  passed &= expect(picked.has_value() && picked.value().size() == 1U &&
                       picked.value()[0].sequence_number == 2,
                   "a selected CA reports exactly its residue");
  request.selected = {};
  request.link_distance_angstrom = 0.5;
  const auto unlinked = analysis::calculate_backbone_angles(request);
  passed &= expect(unlinked.has_value() && !unlinked.value()[1].phi_degrees &&
                       !unlinked.value()[1].psi_degrees,
                   "a short link distance breaks the chain");
  request.link_distance_angstrom = -1.0;
  passed &= expect(!analysis::calculate_backbone_angles(request).has_value(),
                   "a non-positive link distance is rejected");
  request.link_distance_angstrom = 2.0;
  const std::vector<std::uint8_t> bad_mask{1U};
  request.selected = bad_mask;
  passed &= expect(!analysis::calculate_backbone_angles(request).has_value(),
                   "a mask of the wrong size is rejected");
  return passed ? 0 : 1;
}
