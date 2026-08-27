#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

#include "molshredder/analysis/basic.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/error.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

bool near(double left, double right, double tolerance = 1.0e-12) {
  return std::abs(left - right) <= tolerance;
}

struct Fixture {
  std::shared_ptr<const molshredder::model::Topology> topology;
  std::shared_ptr<const molshredder::model::CoordinateFrame> frame;
};

Fixture make_fixture() {
  using namespace molshredder;
  model::TopologyBuilder builder;
  const auto residue =
      builder.add_residue(model::ResidueRecord{"TST", 1, "", "A", ""});
  for (std::size_t index = 0; index < 4U; ++index) {
    static_cast<void>(builder.add_atom(model::AtomRecord{
        "X", 0U, residue.value(), "", 0,
        static_cast<std::int64_t>(index + 1U)}));
  }
  static_cast<void>(builder.add_property(
      "mass", std::vector<double>{1.0, 2.0, 3.0, 4.0},
      model::PropertyMetadata{"dalton", "fixture-masses",
                              {{"estimated", "true"}}}));
  static_cast<void>(builder.add_property(
      "serials", std::vector<std::int64_t>{1, 2, 3, 4}));
  auto metadata = model::FrameMetadata{};
  metadata.coordinate_unit = operation::LengthUnit::nanometer;
  auto frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::vector<model::Vec3d>{
          {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 3.0, 0.0},
          {100.0, 100.0, 100.0}}},
      std::nullopt, {1U, 1U, 1U, 0U}, metadata);
  return {builder.build().value(), frame.value()};
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  const auto fixture = make_fixture();
  const std::vector<std::uint8_t> first_three{1U, 1U, 1U, 0U};

  analysis::CenterRequest centroid_request;
  centroid_request.frame = fixture.frame.get();
  centroid_request.selected = first_three;
  const auto centroid = analysis::calculate_center(centroid_request);
  passed &= expect(centroid.has_value() &&
                       near(centroid.value().position.x, 2.0 / 3.0) &&
                       near(centroid.value().position.y, 1.0) &&
                       centroid.value().selected_atom_count == 3U &&
                       centroid.value().used_atom_count == 3U &&
                       centroid.value().skipped_missing_atom_count == 0U &&
                       !centroid.value().total_mass.has_value() &&
                       centroid.value().coordinate_unit ==
                           operation::LengthUnit::nanometer,
                   "centroid must average selected present coordinates");

  const auto masses =
      analysis::masses_from_property(*fixture.topology, "mass");
  passed &= expect(masses.has_value() && masses.value().values.size() == 4U &&
                       masses.value().unit == "dalton" &&
                       masses.value().source == "fixture-masses" &&
                       masses.value().estimated,
                   "mass property must preserve values, unit and provenance");
  analysis::CenterRequest com_request;
  com_request.frame = fixture.frame.get();
  com_request.selected = first_three;
  com_request.mode = analysis::CenterMode::center_of_mass;
  com_request.masses = masses.value().values;
  com_request.mass_unit = masses.value().unit.value();
  com_request.mass_source = masses.value().source;
  com_request.masses_estimated = masses.value().estimated;
  const auto com = analysis::calculate_center(com_request);
  passed &= expect(com.has_value() && near(com.value().position.x, 4.0 / 6.0) &&
                       near(com.value().position.y, 9.0 / 6.0) &&
                       com.value().total_mass == 6.0 &&
                       com.value().mass_unit == "dalton" &&
                       com.value().mass_source == "fixture-masses" &&
                       com.value().masses_estimated,
                   "COM must use explicit masses and return provenance");

  analysis::CenterRequest missing_request;
  missing_request.frame = fixture.frame.get();
  passed &= expect(!analysis::calculate_center(missing_request).has_value(),
                   "default missing policy must reject a missing selected atom");
  missing_request.missing_atom_policy = analysis::MissingAtomPolicy::skip;
  const auto skipped = analysis::calculate_center(missing_request);
  passed &= expect(skipped.has_value() && skipped.value().selected_atom_count == 4U &&
                       skipped.value().used_atom_count == 3U &&
                       skipped.value().skipped_missing_atom_count == 1U &&
                       near(skipped.value().position.x, 2.0 / 3.0),
                   "explicit skip policy must report omitted missing atoms");

  const std::vector<std::uint8_t> empty_selection{0U, 0U, 0U, 0U};
  auto invalid_request = centroid_request;
  invalid_request.selected = empty_selection;
  passed &= expect(!analysis::calculate_center(invalid_request).has_value(),
                   "empty selection must fail");
  const std::vector<std::uint8_t> invalid_mask{1U, 2U, 0U, 0U};
  invalid_request.selected = invalid_mask;
  passed &= expect(!analysis::calculate_center(invalid_request).has_value(),
                   "non-binary selection mask must fail");
  invalid_request = centroid_request;
  invalid_request.frame = nullptr;
  passed &= expect(!analysis::calculate_center(invalid_request).has_value(),
                   "null frame must fail");
  invalid_request = centroid_request;
  invalid_request.mass_source = "unexpected";
  passed &= expect(!analysis::calculate_center(invalid_request).has_value(),
                   "centroid must reject mass metadata");
  auto invalid_com = com_request;
  const std::vector<double> zero_masses(4U, 0.0);
  invalid_com.masses = zero_masses;
  passed &= expect(!analysis::calculate_center(invalid_com).has_value(),
                   "zero selected total mass must fail");
  const std::vector<double> negative_mass{1.0, -1.0, 3.0, 4.0};
  invalid_com.masses = negative_mass;
  passed &= expect(!analysis::calculate_center(invalid_com).has_value(),
                   "negative mass must fail");
  invalid_com = com_request;
  invalid_com.mass_source = {};
  passed &= expect(!analysis::calculate_center(invalid_com).has_value(),
                   "COM without mass provenance must fail");
  passed &= expect(
      !analysis::masses_from_property(*fixture.topology, "missing").has_value() &&
          !analysis::masses_from_property(*fixture.topology, "serials")
               .has_value(),
      "missing or non-floating mass property must fail");

  const auto estimated = analysis::estimated_element_masses(*fixture.topology);
  passed &= expect(!estimated.has_value(),
                   "unknown elements must not receive a guessed mass");
  model::TopologyBuilder element_builder;
  const auto element_residue = element_builder.add_residue(
      model::ResidueRecord{"ELM", 1, "", "A", ""});
  static_cast<void>(element_builder.add_atom(
      model::AtomRecord{"H", 1U, element_residue.value(), "", 0, 1}));
  static_cast<void>(element_builder.add_atom(
      model::AtomRecord{"I", 53U, element_residue.value(), "", 0, 2}));
  const auto element_topology = element_builder.build();
  const auto element_masses =
      analysis::estimated_element_masses(*element_topology.value());
  passed &= expect(
      element_masses.has_value() &&
          element_masses.value().values == std::vector<double>{1.008, 126.90} &&
          element_masses.value().unit == "dalton" &&
          element_masses.value().source ==
              "CIAAW abridged standard atomic weights 2024" &&
          element_masses.value().estimated,
      "estimated masses must expose the pinned table values and provenance");

  const auto distance = analysis::atom_distance(
      *fixture.frame, model::AtomIndex{1U}, model::AtomIndex{2U});
  passed &= expect(distance.has_value() &&
                       distance.value().displacement ==
                           model::Vec3d{-2.0, 3.0, 0.0} &&
                       near(distance.value().distance, std::sqrt(13.0)) &&
                       distance.value().coordinate_unit ==
                           operation::LengthUnit::nanometer,
                   "atom distance must return directed displacement and norm");
  const auto same = analysis::atom_distance(
      *fixture.frame, model::AtomIndex{0U}, model::AtomIndex{0U});
  passed &= expect(same.has_value() && same.value().distance == 0.0,
                   "same-atom raw distance must be zero");
  passed &= expect(!analysis::atom_distance(
                         *fixture.frame, model::AtomIndex{0U},
                         model::AtomIndex{3U})
                         .has_value() &&
                       !analysis::atom_distance(
                            *fixture.frame, model::AtomIndex{0U},
                            model::AtomIndex{99U})
                            .has_value(),
                   "missing and out-of-range distance endpoints must fail");

  const auto right_angle = analysis::atom_angle(
      *fixture.frame, model::AtomIndex{1U}, model::AtomIndex{0U},
      model::AtomIndex{2U});
  passed &= expect(right_angle.has_value() &&
                       near(right_angle.value().angle_degrees, 90.0) &&
                       right_angle.value().boundary ==
                           analysis::DistanceBoundary::raw,
                   "atan2 angle must return the analytic right angle");
  const auto signed_dihedral_frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::vector<model::Vec3d>{
          {1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0},
          {0.0, 1.0, 1.0}}});
  const auto negative_dihedral = analysis::atom_dihedral(
      *signed_dihedral_frame.value(), model::AtomIndex{0U},
      model::AtomIndex{1U}, model::AtomIndex{2U}, model::AtomIndex{3U});
  passed &= expect(negative_dihedral.has_value() &&
                       near(negative_dihedral.value().angle_degrees, -90.0),
                   "projected-vector atan2 must preserve dihedral sign");

  auto periodic_metadata = model::FrameMetadata{};
  periodic_metadata.unit_cell = model::UnitCell{{10.0, 0.0, 0.0},
                                                 {0.0, 10.0, 0.0},
                                                 {0.0, 0.0, 10.0}};
  const auto periodic_frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::vector<model::Vec3d>{
          {1.0, 0.0, 0.0}, {9.0, 0.0, 0.0}, {9.0, 1.0, 0.0},
          {9.0, 1.0, 1.0}}},
      std::nullopt, {}, periodic_metadata);
  const auto periodic_angle = analysis::atom_angle(
      *periodic_frame.value(), model::AtomIndex{0U}, model::AtomIndex{1U},
      model::AtomIndex{2U}, analysis::DistanceBoundary::minimum_image);
  const auto periodic_dihedral = analysis::atom_dihedral(
      *periodic_frame.value(), model::AtomIndex{0U}, model::AtomIndex{1U},
      model::AtomIndex{2U}, model::AtomIndex{3U},
      analysis::DistanceBoundary::minimum_image);
  passed &= expect(periodic_angle.has_value() &&
                       near(periodic_angle.value().angle_degrees, 90.0) &&
                       periodic_dihedral.has_value() &&
                       near(periodic_dihedral.value().angle_degrees, -90.0),
                   "minimum-image geometry must unwrap each consecutive bond");

  const auto collinear_frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::vector<model::Vec3d>{
          {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {2.0, 0.0, 0.0},
          {3.0, 1.0, 0.0}}});
  passed &= expect(
      !analysis::atom_angle(*fixture.frame, model::AtomIndex{0U},
                            model::AtomIndex{0U}, model::AtomIndex{1U})
           .has_value() &&
          !analysis::atom_angle(*fixture.frame, model::AtomIndex{0U},
                                model::AtomIndex{1U}, model::AtomIndex{3U})
               .has_value() &&
          !analysis::atom_angle(*fixture.frame, model::AtomIndex{0U},
                                model::AtomIndex{1U}, model::AtomIndex{2U},
                                analysis::DistanceBoundary::minimum_image)
               .has_value() &&
          !analysis::atom_dihedral(
               *collinear_frame.value(), model::AtomIndex{0U},
               model::AtomIndex{1U}, model::AtomIndex{2U},
               model::AtomIndex{3U})
               .has_value(),
      "duplicate, missing-cell, missing-atom and collinear geometry must fail");

  const auto cancellation_frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::vector<model::Vec3d>{
          {1.0e16, 0.0, 0.0}, {1.0, 0.0, 0.0}, {-1.0e16, 0.0, 0.0}}});
  analysis::CenterRequest cancellation_request;
  cancellation_request.frame = cancellation_frame.value().get();
  const auto stable = analysis::calculate_center(cancellation_request);
  passed &= expect(stable.has_value() &&
                       near(stable.value().position.x, 1.0 / 3.0),
                   "compensated summation must retain small centroid terms");

  return passed ? 0 : 1;
}
