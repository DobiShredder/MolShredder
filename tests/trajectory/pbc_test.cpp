#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "molshredder/analysis/basic.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/trajectory/pbc.hpp"

namespace {

bool near(double left, double right, double tolerance = 1.0e-10) {
  return std::abs(left - right) <= tolerance;
}

bool near_vec(molshredder::model::Vec3d value,
              molshredder::model::Vec3d expected,
              double tolerance = 1.0e-10) {
  return near(value.x, expected.x, tolerance) &&
         near(value.y, expected.y, tolerance) &&
         near(value.z, expected.z, tolerance);
}

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

std::shared_ptr<const molshredder::model::CoordinateFrame> frame(
    std::vector<molshredder::model::Vec3f> positions,
    std::vector<std::uint8_t> presence,
    std::optional<molshredder::model::UnitCell> cell) {
  molshredder::model::FrameMetadata metadata;
  metadata.unit_cell = cell;
  metadata.fields.emplace("source", "pbc-test");
  return molshredder::model::CoordinateFrame::create(
             molshredder::model::CoordinateBuffer{std::move(positions)},
             std::nullopt, std::move(presence), std::move(metadata))
      .value();
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  const model::UnitCell orthorhombic{{10.0, 0.0, 0.0},
                                     {0.0, 10.0, 0.0},
                                     {0.0, 0.0, 10.0}};
  const model::UnitCell skewed{{10.0, 0.0, 0.0},
                               {9.0, 2.0, 0.0},
                               {0.0, 0.0, 10.0}};

  const auto fractional = trajectory::cartesian_to_fractional(
      skewed, {9.31, 0.98, 5.0});
  passed &= expect(fractional.has_value() &&
                       near_vec(fractional.value(), {0.49, 0.49, 0.5}),
                   "triclinic Cartesian-to-fractional conversion must invert the basis");
  const auto round_trip = trajectory::fractional_to_cartesian(
      skewed, fractional.value());
  passed &= expect(round_trip.has_value() &&
                       near_vec(round_trip.value(), {9.31, 0.98, 5.0}),
                   "triclinic fractional conversion must round-trip");

  const auto orthogonal_image =
      trajectory::minimum_image(orthorhombic, {6.0, -6.0, 1.0});
  passed &= expect(
      orthogonal_image.has_value() &&
          near_vec(orthogonal_image.value().displacement, {-4.0, 4.0, 1.0}) &&
          orthogonal_image.value().lattice_shift ==
              std::array<std::int64_t, 3>{1, -1, 0},
      "orthorhombic minimum image must return displacement and lattice shift");
  const auto positive_tie =
      trajectory::minimum_image(orthorhombic, {5.0, 0.0, 0.0});
  const auto negative_tie =
      trajectory::minimum_image(orthorhombic, {-5.0, 0.0, 0.0});
  passed &= expect(
      positive_tie.has_value() && negative_tie.has_value() &&
          near(positive_tie.value().displacement.x, -5.0) &&
          near(negative_tie.value().displacement.x, -5.0),
      "half-cell ties must deterministically select the negative boundary");

  const auto skewed_image = trajectory::minimum_image(skewed, {9.31, 0.98, 0.0});
  passed &= expect(
      skewed_image.has_value() &&
          near_vec(skewed_image.value().displacement, {0.31, -1.02, 0.0}) &&
          skewed_image.value().lattice_shift ==
              std::array<std::int64_t, 3>{0, 1, 0},
      "exact triclinic search must beat independent fractional rounding");

  const auto outside = trajectory::fractional_to_cartesian(
                           skewed, {-0.2, 1.2, 0.5})
                           .value();
  const auto wrapped = trajectory::wrap_position(skewed, outside);
  const auto wrapped_fractional = trajectory::cartesian_to_fractional(
      skewed, wrapped.value());
  passed &= expect(wrapped.has_value() && wrapped_fractional.has_value() &&
                       near_vec(wrapped_fractional.value(), {0.8, 0.2, 0.5}),
                   "atom wrapping must map each fractional component to [0,1)");

  const auto source = frame({{11.0F, -1.0F, 5.0F}, {99.0F, 99.0F, 99.0F}},
                            {1U, 0U}, orthorhombic);
  const auto wrapped_frame = trajectory::wrap_frame(*source);
  passed &= expect(wrapped_frame.has_value(), "periodic frame must wrap");
  if (wrapped_frame.has_value()) {
    const auto& positions = std::get<std::vector<model::Vec3f>>(
        wrapped_frame.value()->positions().values());
    passed &= expect(near(positions[0].x, 1.0) && near(positions[0].y, 9.0) &&
                         positions[1] == model::Vec3f{99.0F, 99.0F, 99.0F} &&
                         wrapped_frame.value()->metadata().fields.at("source") ==
                             "pbc-test" &&
                         wrapped_frame.value()->metadata().fields.at("pbc.operation") ==
                             "wrap-atoms",
                     "frame wrap must preserve precision/missing placeholder/metadata");
  }
  model::FrameMetadata double_metadata;
  double_metadata.unit_cell = orthorhombic;
  const auto double_source = model::CoordinateFrame::create(
      model::CoordinateBuffer{
          std::vector<model::Vec3d>{{10.25, -0.25, 0.125}}},
      std::nullopt, std::vector<std::uint8_t>{1U}, std::move(double_metadata));
  const auto double_wrapped = trajectory::wrap_frame(*double_source.value());
  passed &= expect(
      double_wrapped.has_value() &&
          std::holds_alternative<std::vector<model::Vec3d>>(
              double_wrapped.value()->positions().values()) &&
          near_vec(std::get<std::vector<model::Vec3d>>(
                       double_wrapped.value()->positions().values())[0],
                   {0.25, 9.75, 0.125}),
      "frame wrap must preserve float64 coordinate precision");
  passed &= expect(!trajectory::wrap_frame(*frame({{1.0F, 2.0F, 3.0F}},
                                                   {1U}, std::nullopt))
                        .has_value(),
                   "frame wrap without a cell must fail");

  const auto distance_frame = frame({{9.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}},
                                    {1U, 1U}, orthorhombic);
  const auto raw_distance = analysis::atom_distance(
      *distance_frame, {0U}, {1U}, analysis::DistanceBoundary::raw);
  const auto pbc_distance = analysis::atom_distance(
      *distance_frame, {0U}, {1U}, analysis::DistanceBoundary::minimum_image);
  passed &= expect(raw_distance.has_value() && pbc_distance.has_value() &&
                       near(raw_distance.value().distance, 8.0) &&
                       near(pbc_distance.value().distance, 2.0) &&
                       near(pbc_distance.value().displacement.x, 2.0),
                   "distance kernel must expose raw and minimum-image modes");
  passed &= expect(!analysis::atom_distance(
                        *frame({{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}},
                               {1U, 1U}, std::nullopt),
                        {0U}, {1U}, analysis::DistanceBoundary::minimum_image)
                        .has_value(),
                   "minimum-image distance without a cell must fail");

  trajectory::TrajectoryUnwrapper unwrapper{2U};
  const auto first = unwrapper.push(
      *frame({{9.0F, 1.0F, 1.0F}, {8.0F, 0.0F, 0.0F}}, {1U, 1U},
             orthorhombic));
  const auto second = unwrapper.push(
      *frame({{1.0F, 1.0F, 1.0F}, {99.0F, 99.0F, 99.0F}}, {1U, 0U},
             orthorhombic));
  const auto third = unwrapper.push(
      *frame({{3.0F, 1.0F, 1.0F}, {1.0F, 0.0F, 0.0F}}, {1U, 1U},
             orthorhombic));
  passed &= expect(first.has_value() && second.has_value() && third.has_value() &&
                       unwrapper.processed_frame_count() == 3U,
                   "valid trajectory frames must advance unwrap state");
  if (third.has_value()) {
    const auto& positions = std::get<std::vector<model::Vec3f>>(
        third.value()->positions().values());
    passed &= expect(near(positions[0].x, 13.0) && near(positions[1].x, 1.0) &&
                         third.value()->metadata().fields.at("pbc.operation") ==
                             "unwrap-atom-continuity",
                     "unwrap must maintain continuity and re-anchor reappearing atoms");
  }
  const auto before_failure = unwrapper.processed_frame_count();
  passed &= expect(!unwrapper.push(*frame({{1.0F, 0.0F, 0.0F}}, {1U},
                                         orthorhombic))
                        .has_value() &&
                       unwrapper.processed_frame_count() == before_failure,
                   "failed unwrap must not mutate trajectory state");
  unwrapper.reset();
  passed &= expect(unwrapper.processed_frame_count() == 0U,
                   "unwrap reset must clear processed state");

  trajectory::TrajectoryUnwrapper variable_cell_unwrapper{1U};
  const model::UnitCell doubled_cell{{20.0, 0.0, 0.0},
                                     {0.0, 20.0, 0.0},
                                     {0.0, 0.0, 20.0}};
  const auto variable_first = variable_cell_unwrapper.push(
      *frame({{9.0F, 0.0F, 0.0F}}, {1U}, orthorhombic));
  const auto variable_second = variable_cell_unwrapper.push(
      *frame({{1.0F, 0.0F, 0.0F}}, {1U}, doubled_cell));
  passed &= expect(
      variable_first.has_value() && variable_second.has_value() &&
          near(std::get<std::vector<model::Vec3f>>(
                   variable_second.value()->positions().values())[0]
                   .x,
               1.0),
      "variable-cell unwrap must use the arriving frame cell");

  const model::UnitCell invalid_cell{{1.0, 0.0, 0.0},
                                     {2.0, 0.0, 0.0},
                                     {0.0, 0.0, 1.0}};
  passed &= expect(!trajectory::minimum_image(invalid_cell, {1.0, 2.0, 3.0})
                        .has_value(),
                   "degenerate unit cells must fail PBC operations");
  return passed ? 0 : 1;
}
