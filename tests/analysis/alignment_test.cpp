#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "molshredder/analysis/alignment.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/error.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

bool near(double left, double right, double tolerance = 1.0e-10) {
  return std::abs(left - right) <= tolerance;
}

std::shared_ptr<const molshredder::model::CoordinateFrame> make_frame(
    std::vector<molshredder::model::Vec3d> positions,
    molshredder::operation::LengthUnit unit =
        molshredder::operation::LengthUnit::angstrom,
    std::vector<std::uint8_t> presence = {}) {
  molshredder::model::FrameMetadata metadata;
  metadata.coordinate_unit = unit;
  return molshredder::model::CoordinateFrame::create(
             molshredder::model::CoordinateBuffer{std::move(positions)},
             std::nullopt, std::move(presence), metadata)
      .value();
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  const auto mobile = make_frame({{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
                                  {0.0, 2.0, 0.0}, {0.0, 0.0, 3.0}});
  const auto reference = make_frame({{5.0, -2.0, 1.0}, {5.0, -1.0, 1.0},
                                     {3.0, -2.0, 1.0}, {5.0, -2.0, 4.0}});
  const std::vector<std::uint8_t> all{1U, 1U, 1U, 1U};
  const auto fitted = analysis::fit_rigid(
      analysis::FitRequest{reference.get(), mobile.get(), all, {},
                           analysis::MissingAtomPolicy::error});
  const auto transformed = fitted.has_value()
                               ? fitted.value().transform.apply({1.0, 0.0, 0.0})
                               : model::Vec3d{};
  passed &= expect(
      fitted.has_value() && fitted.value().before.rmsd > 4.0 &&
          fitted.value().after.rmsd < 1.0e-10 && near(transformed.x, 5.0) &&
          near(transformed.y, -1.0) && near(transformed.z, 1.0) &&
          fitted.value().after.paired_atom_count == 4U,
      "rigid fit must recover a right-handed rotation and translation");

  const auto mobile_nm = make_frame(
      {{0.0, 0.0, 0.0}, {0.1, 0.0, 0.0}, {0.0, 0.2, 0.0},
       {0.0, 0.0, 0.3}},
      operation::LengthUnit::nanometer);
  const auto reference_a = make_frame(
      {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 2.0, 0.0},
       {0.0, 0.0, 3.0}},
      operation::LengthUnit::angstrom);
  const auto scaled = analysis::fit_rigid(
      analysis::FitRequest{reference_a.get(), mobile_nm.get(), all, {},
                           analysis::MissingAtomPolicy::error});
  passed &= expect(scaled.has_value() && scaled.value().after.rmsd < 1.0e-10 &&
                       scaled.value().transform.input_scale == 10.0,
                   "fit must convert mobile coordinates into reference units");
  const auto unscaled_mixed_units = analysis::calculate_rmsd(
      analysis::RmsdRequest{reference_a.get(), mobile_nm.get(), all, {},
                            analysis::MissingAtomPolicy::error, {}});
  passed &= expect(!unscaled_mixed_units.has_value(),
                   "mixed-unit RMSD must not silently use an identity scale");

  const std::vector<double> weights{1.0, 2.0, 3.0, 0.0};
  const auto weighted = analysis::fit_rigid(
      analysis::FitRequest{reference.get(), mobile.get(), all, weights,
                           analysis::MissingAtomPolicy::error});
  passed &= expect(weighted.has_value() &&
                       weighted.value().after.rmsd < 1.0e-10 &&
                       weighted.value().after.effective_atom_count == 3U &&
                       weighted.value().after.weight_sum == 6.0,
                   "weighted fit must ignore zero-weight atoms in the solve");

  const auto missing_mobile = make_frame(
      {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 2.0, 0.0},
       {0.0, 0.0, 3.0}},
      operation::LengthUnit::angstrom, {1U, 1U, 1U, 0U});
  const auto skipped = analysis::fit_rigid(
      analysis::FitRequest{reference.get(), missing_mobile.get(), all, {},
                           analysis::MissingAtomPolicy::skip});
  passed &= expect(skipped.has_value() &&
                       skipped.value().after.paired_atom_count == 3U &&
                       skipped.value().after.skipped_missing_atom_count == 1U,
                   "skip policy must report omitted fit pairs");
  passed &= expect(
      !analysis::fit_rigid(
           analysis::FitRequest{reference.get(), missing_mobile.get(), all, {},
                                analysis::MissingAtomPolicy::error})
           .has_value(),
      "default fit policy must reject missing pairs");

  const auto collinear = make_frame(
      {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {2.0, 0.0, 0.0}});
  const std::vector<std::uint8_t> three{1U, 1U, 1U};
  const auto degenerate = analysis::fit_rigid(
      analysis::FitRequest{collinear.get(), collinear.get(), three, {},
                           analysis::MissingAtomPolicy::error});
  passed &= expect(!degenerate.has_value() &&
                       degenerate.error().code ==
                           operation::ErrorCode::invalid_selection,
                   "rigid fit must reject a non-unique collinear rotation");

  const std::vector<std::uint8_t> one{1U, 0U, 0U};
  const auto one_atom_rmsd = analysis::calculate_rmsd(analysis::RmsdRequest{
      collinear.get(), collinear.get(), one, {},
      analysis::MissingAtomPolicy::error, {}});
  passed &= expect(one_atom_rmsd.has_value() && one_atom_rmsd.value().rmsd == 0.0,
                   "RMSD without fitting must support one selected atom");
  auto invalid_transform = analysis::RigidTransform{};
  invalid_transform.input_scale = 0.0;
  const auto invalid_rmsd = analysis::calculate_rmsd(analysis::RmsdRequest{
      collinear.get(), collinear.get(), one, {},
      analysis::MissingAtomPolicy::error, invalid_transform});
  passed &= expect(!invalid_rmsd.has_value() &&
                       invalid_rmsd.error().code ==
                           operation::ErrorCode::invalid_argument,
                   "RMSD must reject a non-rigid or invalid transform");

  return passed ? 0 : 1;
}
