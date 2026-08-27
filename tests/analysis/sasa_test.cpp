#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <string_view>
#include <vector>

#include "molshredder/analysis/sasa.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/error.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

std::shared_ptr<const molshredder::model::CoordinateFrame> frame(
    std::vector<molshredder::model::Vec3d> positions,
    std::vector<std::uint8_t> presence,
    molshredder::operation::LengthUnit unit =
        molshredder::operation::LengthUnit::angstrom) {
  molshredder::model::FrameMetadata metadata;
  metadata.coordinate_unit = unit;
  return molshredder::model::CoordinateFrame::create(
             molshredder::model::CoordinateBuffer{std::move(positions)},
             std::nullopt, std::move(presence), metadata)
      .value();
}

} // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  auto isolated_frame = frame({{0.0, 0.0, 0.0}}, {1U});
  const std::vector<double> isolated_radius{1.0};
  analysis::SasaRequest request;
  request.frame = isolated_frame.get();
  request.vdw_radii_angstrom = isolated_radius;
  request.probe_radius_angstrom = 1.0;
  request.samples_per_atom = 64U;
  const auto isolated = analysis::solvent_accessible_surface_area(request);
  const auto isolated_oracle = 16.0 * std::numbers::pi_v<double>;
  passed &= expect(
      isolated.has_value() && isolated.value().atoms.size() == 1U &&
          std::abs(isolated.value().total_area_square_angstrom -
                   isolated_oracle) < 1.0e-12 &&
          isolated.value().atoms[0].accessible_sample_count == 64U,
      "an isolated expanded sphere must have exact analytic SASA");

  auto nanometer_frame = frame({{0.0, 0.0, 0.0}}, {1U},
                               operation::LengthUnit::nanometer);
  request.frame = nanometer_frame.get();
  const auto nanometer = analysis::solvent_accessible_surface_area(request);
  passed &= expect(
      nanometer.has_value() &&
          std::abs(nanometer.value().total_area_square_angstrom -
                   isolated_oracle) < 1.0e-12,
      "SASA output must remain square angstrom for nanometer coordinates");

  auto overlap_frame = frame({{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}}, {1U, 1U});
  const std::vector<double> overlap_radii{1.0, 1.0};
  request.frame = overlap_frame.get();
  request.vdw_radii_angstrom = overlap_radii;
  request.samples_per_atom = 20'000U;
  request.evaluation_budget = 100'000U;
  const auto overlap = analysis::solvent_accessible_surface_area(request);
  const auto overlap_oracle = 24.0 * std::numbers::pi_v<double>;
  passed &= expect(
      overlap.has_value() && overlap.value().atoms.size() == 2U &&
          std::abs(overlap.value().total_area_square_angstrom -
                   overlap_oracle) < 0.03,
      "two equal overlapping expanded spheres must converge to the analytic exposed area");

  const std::vector<std::uint8_t> select_first{1U, 0U};
  request.selected = select_first;
  const auto selected = analysis::solvent_accessible_surface_area(request);
  passed &= expect(
      selected.has_value() && selected.value().atoms.size() == 1U &&
          std::abs(selected.value().total_area_square_angstrom -
                   12.0 * std::numbers::pi_v<double>) < 0.03,
      "unselected present atoms must still occlude selected-atom SASA");

  request.evaluation_budget = 1U;
  const auto exhausted = analysis::solvent_accessible_surface_area(request);
  passed &= expect(!exhausted.has_value() &&
                       exhausted.error().code ==
                           operation::ErrorCode::resource_exhausted,
                   "SASA evaluation budget exhaustion must be explicit");
  request.evaluation_budget = 100'000U;
  operation::TaskContext context;
  context.cancellation.request_cancel();
  request.context = &context;
  const auto cancelled = analysis::solvent_accessible_surface_area(request);
  passed &= expect(!cancelled.has_value() &&
                       cancelled.error().code == operation::ErrorCode::cancelled,
                   "SASA cancellation must not publish a partial result");

  auto missing_frame = frame({{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}}, {1U, 0U});
  request.context = nullptr;
  request.frame = missing_frame.get();
  request.selected = select_first;
  const auto ignored_missing =
      analysis::solvent_accessible_surface_area(request);
  const std::vector<std::uint8_t> select_missing{0U, 1U};
  request.selected = select_missing;
  const auto rejected_missing =
      analysis::solvent_accessible_surface_area(request);
  passed &= expect(
      ignored_missing.has_value() &&
          ignored_missing.value().ignored_missing_occluders == 1U &&
          !rejected_missing.has_value(),
      "missing unselected occluders must be reported while missing selected atoms fail");
  return passed ? 0 : 1;
}
