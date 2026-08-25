#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/error.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  using namespace molshredder::model;
  using molshredder::operation::ErrorCode;
  using molshredder::operation::LengthUnit;

  bool passed = true;
  const UnitCell triclinic{{10.0, 0.0, 0.0},
                           {2.0, 11.0, 0.0},
                           {1.0, 3.0, 12.0}};
  passed &= expect(triclinic.is_valid() &&
                       std::abs(triclinic.signed_volume() - 1320.0) < 1.0e-12,
                   "triclinic cell vectors must preserve signed volume");

  FrameMetadata metadata;
  metadata.source_step = 500;
  metadata.physical_time = PhysicalTime{1.25, TimeUnit::picosecond};
  metadata.unit_cell = triclinic;
  metadata.coordinate_unit = LengthUnit::angstrom;
  metadata.velocity_time_unit = TimeUnit::femtosecond;
  metadata.atom_properties.emplace(
      "force_magnitude",
      AtomProperty{std::vector<float>{0.5F, 0.75F},
                   PropertyMetadata{"kcal/mol/angstrom", "synthetic", {}}});
  metadata.fields.emplace("temperature", "300 K");
  const auto float_frame = CoordinateFrame::create(
      CoordinateBuffer{std::vector<Vec3f>{{0, 0, 0}, {1, 2, 3}}},
      CoordinateBuffer{std::vector<Vec3f>{{0.1F, 0, 0}, {0, 0.2F, 0}}},
      {1, 0}, metadata);
  passed &= expect(float_frame.has_value(), "valid float frame must build");
  if (float_frame.has_value()) {
    passed &= expect(float_frame.value()->atom_count() == 2 &&
                         float_frame.value()->positions().precision() ==
                             CoordinatePrecision::float32,
                     "float coordinate precision must be explicit");
    passed &= expect(float_frame.value()->atom_present(0) &&
                         !float_frame.value()->atom_present(1) &&
                         !float_frame.value()->atom_present(2),
                     "presence mask must encode missing atoms safely");
    passed &= expect(float_frame.value()->metadata().source_step == 500 &&
                         float_frame.value()->metadata().velocity_time_unit ==
                             TimeUnit::femtosecond &&
                         float_frame.value()->metadata().fields.at(
                             "temperature") == "300 K",
                     "frame source metadata must be preserved");
    passed &= expect(
        property_kind(float_frame.value()
                          ->metadata()
                          .atom_properties.at("force_magnitude")
                          .values) ==
            PropertyKind::float32,
        "frame-dependent atom properties must preserve their type");
    passed &= expect(float_frame.value()
                             ->metadata()
                             .atom_properties.at("force_magnitude")
                             .metadata.unit == "kcal/mol/angstrom",
                     "frame atom property unit must be preserved");
  }

  const auto double_frame = CoordinateFrame::create(
      CoordinateBuffer{std::vector<Vec3d>{{4.0, 5.0, 6.0},
                                          {7.0, 8.0, 9.0}}});
  passed &= expect(double_frame.has_value() &&
                       double_frame.value()->positions().precision() ==
                           CoordinatePrecision::float64 &&
                       double_frame.value()->atom_present(1),
                   "double frame must create a default all-present mask");

  const auto bad_position = CoordinateFrame::create(CoordinateBuffer{
      std::vector<Vec3d>{{std::numeric_limits<double>::quiet_NaN(), 0, 0}}});
  passed &= expect(!bad_position.has_value(),
                   "non-finite positions must fail");
  const auto bad_velocity_count = CoordinateFrame::create(
      CoordinateBuffer{std::vector<Vec3d>{{0, 0, 0}, {1, 1, 1}}},
      CoordinateBuffer{std::vector<Vec3d>{{0, 0, 0}}});
  passed &= expect(!bad_velocity_count.has_value(),
                   "velocity count must match positions");
  const auto mixed_velocity_precision = CoordinateFrame::create(
      CoordinateBuffer{std::vector<Vec3d>{{0, 0, 0}}},
      CoordinateBuffer{std::vector<Vec3f>{{0, 0, 0}}});
  passed &= expect(mixed_velocity_precision.has_value() &&
                       mixed_velocity_precision.value()
                               ->velocities()
                               ->precision() == CoordinatePrecision::float32,
                   "velocity precision must be preserved independently");
  const auto bad_presence_count = CoordinateFrame::create(
      CoordinateBuffer{std::vector<Vec3d>{{0, 0, 0}}}, std::nullopt, {1, 0});
  passed &= expect(!bad_presence_count.has_value(),
                   "presence count must match positions");
  const auto bad_presence_value = CoordinateFrame::create(
      CoordinateBuffer{std::vector<Vec3d>{{0, 0, 0}}}, std::nullopt, {2});
  passed &= expect(!bad_presence_value.has_value(),
                   "presence values must be 0 or 1");

  FrameMetadata invalid_cell_metadata;
  invalid_cell_metadata.unit_cell =
      UnitCell{{1, 0, 0}, {2, 0, 0}, {0, 0, 1}};
  const auto bad_cell = CoordinateFrame::create(
      CoordinateBuffer{std::vector<Vec3d>{{0, 0, 0}}}, std::nullopt, {},
      invalid_cell_metadata);
  passed &= expect(!bad_cell.has_value(),
                   "coplanar unit cell vectors must fail");
  FrameMetadata left_handed_cell_metadata;
  left_handed_cell_metadata.unit_cell =
      UnitCell{{1, 0, 0}, {0, 1, 0}, {0, 0, -1}};
  const auto left_handed_cell = CoordinateFrame::create(
      CoordinateBuffer{std::vector<Vec3d>{{0, 0, 0}}}, std::nullopt, {},
      left_handed_cell_metadata);
  passed &= expect(!left_handed_cell.has_value(),
                   "unit cell must use a right-handed basis");
  FrameMetadata invalid_time_metadata;
  invalid_time_metadata.physical_time = PhysicalTime{
      std::numeric_limits<double>::infinity(), TimeUnit::femtosecond};
  const auto bad_time = CoordinateFrame::create(
      CoordinateBuffer{std::vector<Vec3d>{{0, 0, 0}}}, std::nullopt, {},
      invalid_time_metadata);
  passed &= expect(!bad_time.has_value(), "non-finite physical time must fail");
  FrameMetadata bad_frame_property_metadata;
  bad_frame_property_metadata.atom_properties.emplace(
      "partial_charge",
      AtomProperty{std::vector<double>{1.0, 2.0}, {}});
  const auto bad_frame_property = CoordinateFrame::create(
      CoordinateBuffer{std::vector<Vec3d>{{0, 0, 0}}}, std::nullopt, {},
      bad_frame_property_metadata);
  passed &= expect(!bad_frame_property.has_value(),
                   "frame atom property rows must match positions");
  FrameMetadata bad_frame_boolean_metadata;
  bad_frame_boolean_metadata.atom_properties.emplace(
      "visible", AtomProperty{BooleanColumn{{2}}, {}});
  const auto bad_frame_boolean = CoordinateFrame::create(
      CoordinateBuffer{std::vector<Vec3d>{{0, 0, 0}}}, std::nullopt, {},
      bad_frame_boolean_metadata);
  passed &= expect(!bad_frame_boolean.has_value(),
                   "frame boolean property values must be 0 or 1");

  if (float_frame.has_value() && double_frame.has_value()) {
    const auto source = InMemoryCoordinateSource::create(
        2, {float_frame.value(), double_frame.value()});
    passed &= expect(source.has_value(), "matching frames must create a source");
    if (source.has_value()) {
      passed &= expect(source.value()->atom_count() == 2 &&
                           source.value()->frame_count() == 2 &&
                           source.value()->access() == FrameAccess::random_access,
                       "in-memory source capabilities must be explicit");
      const auto frame = source.value()->read_frame(1);
      passed &= expect(frame.has_value() &&
                           frame.value()->positions().precision() ==
                               CoordinatePrecision::float64,
                       "random frame access must preserve frame precision");
      const auto missing = source.value()->read_frame(2);
      passed &= expect(!missing.has_value() &&
                           missing.error().code == ErrorCode::not_found,
                       "out-of-range frame read must return stable not-found");
      const auto remapped = RemappedCoordinateSource::create(
          source.value(), {1U, std::nullopt, 0U});
      const auto remapped_frame =
          remapped.has_value()
              ? remapped.value()->read_frame(0U)
              : molshredder::operation::Result<
                    std::shared_ptr<const CoordinateFrame>>::failure(
                    remapped.error());
      const auto *positions =
          remapped_frame.has_value()
              ? std::get_if<std::vector<Vec3f>>(
                    &remapped_frame.value()->positions().values())
              : nullptr;
      const auto *forces =
          remapped_frame.has_value()
              ? std::get_if<std::vector<float>>(
                    &remapped_frame.value()
                         ->metadata()
                         .atom_properties.at("force_magnitude")
                         .values)
              : nullptr;
      passed &= expect(
          remapped.has_value() && remapped.value()->atom_count() == 3U &&
              remapped.value()->frame_count() == 2U &&
              remapped_frame.has_value() && positions != nullptr &&
              *positions == std::vector<Vec3f>{{1, 2, 3}, {0, 0, 0},
                                                {0, 0, 0}} &&
              remapped_frame.value()->presence() ==
                  std::vector<std::uint8_t>{0U, 0U, 1U} &&
              forces != nullptr &&
              *forces == std::vector<float>{0.75F, 0.0F, 0.5F} &&
              remapped_frame.value()->metadata().fields.at(
                  "molshredder.coordinate_remap") == "stable-identity-v1",
          "coordinate remap must reorder every atom channel and represent inserted/missing atoms with finite absent placeholders");
      passed &= expect(
          !RemappedCoordinateSource::create(source.value(), {0U, 0U})
               .has_value() &&
              !RemappedCoordinateSource::create(source.value(), {2U})
                   .has_value() &&
              !RemappedCoordinateSource::create(nullptr, {}).has_value(),
          "duplicate, out-of-range and null coordinate remaps must fail");
    }
    const auto mismatched =
        InMemoryCoordinateSource::create(1, {float_frame.value()});
    passed &= expect(!mismatched.has_value(),
                     "source atom count must match every frame");
    const auto null_frame = InMemoryCoordinateSource::create(
        2, {std::shared_ptr<const CoordinateFrame>{}});
    passed &= expect(!null_frame.has_value(), "null source frame must fail");
  }

  return passed ? 0 : 1;
}
