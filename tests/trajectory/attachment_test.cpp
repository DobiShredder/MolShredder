#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

#include "molshredder/trajectory/attachment.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

double x(const molshredder::model::CoordinateBuffer& values,
         std::size_t index) {
  return std::visit(
      [index](const auto& data) { return static_cast<double>(data[index].x); },
      values.values());
}

double numeric(const molshredder::model::AtomProperty& property,
               std::size_t index) {
  return std::visit(
      [index](const auto& values) -> double {
        using Values = std::decay_t<decltype(values)>;
        if constexpr (std::is_same_v<Values, std::vector<float>> ||
                      std::is_same_v<Values, std::vector<double>>) {
          return values[index];
        }
        return 0.0;
      },
      property.values);
}

std::shared_ptr<const molshredder::model::CoordinateFrame> frame(
    std::uint64_t step, double time_fs, bool velocities = true) {
  using namespace molshredder;
  model::FrameMetadata metadata;
  metadata.source_step = step;
  metadata.physical_time =
      model::PhysicalTime{time_fs, model::TimeUnit::femtosecond};
  metadata.coordinate_unit = operation::LengthUnit::nanometer;
  metadata.unit_cell = model::UnitCell{{1.0, 0.0, 0.0},
                                       {0.0, 2.0, 0.0},
                                       {0.0, 0.0, 3.0}};
  if (velocities)
    metadata.velocity_time_unit = model::TimeUnit::femtosecond;
  for (const auto* name : {"force_x", "force_y", "force_z"}) {
    metadata.atom_properties.emplace(
        name, model::AtomProperty{std::vector<double>{10.0, 20.0},
                                  {"kJ mol^-1 nm^-1", "test force", {}}});
  }
  std::optional<model::CoordinateBuffer> velocity;
  if (velocities)
    velocity.emplace(std::vector<model::Vec3d>{{0.001, 0.0, 0.0},
                                                {0.002, 0.0, 0.0}});
  return model::CoordinateFrame::create(
             model::CoordinateBuffer{
                 std::vector<model::Vec3d>{{1.0, 0.0, 0.0},
                                            {2.0, 0.0, 0.0}}},
             std::move(velocity), {}, std::move(metadata))
      .value();
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;

  model::TopologyBuilder builder;
  const auto residue = builder.add_residue({"GLY", 7, "", "A", "PROT"});
  static_cast<void>(builder.add_atom(
      {"N", 7, residue.value(), "", 0, std::int64_t{11}}));
  static_cast<void>(builder.add_atom(
      {"CA", 6, residue.value(), "", 0, std::int64_t{12}}));
  const auto topology = builder.build().value();

  const auto index = trajectory::resolve_atom_mapping(
      {trajectory::AtomMappingPolicy::index_order, topology.get(), 2U, {}, {}});
  passed &= expect(index.has_value() &&
                       index.value().identity_strength ==
                           "ordinal-only-explicit-opt-in" &&
                       index.value().target_to_source[1] == 1U,
                   "index mapping must be explicit and deterministic");

  const std::array explicit_ids{topology->atom_ids()[1],
                                topology->atom_ids()[0]};
  const auto explicit_mapping = trajectory::resolve_atom_mapping(
      {trajectory::AtomMappingPolicy::explicit_map, topology.get(), 2U, {},
       explicit_ids});
  passed &= expect(explicit_mapping.has_value() &&
                       explicit_mapping.value().target_to_source[0] == 1U &&
                       explicit_mapping.value().target_to_source[1] == 0U,
                   "explicit mapping must resolve stable IDs to target order");
  const std::array duplicate_ids{topology->atom_ids()[0],
                                 topology->atom_ids()[0]};
  passed &= expect(
      !trajectory::resolve_atom_mapping(
           {trajectory::AtomMappingPolicy::explicit_map, topology.get(), 2U,
            {}, duplicate_ids})
           .has_value(),
      "duplicate explicit stable IDs must fail");
  const std::array missing_id{topology->atom_ids()[0]};
  const std::array unknown_ids{topology->atom_ids()[0], model::AtomId{999999U}};
  const std::array zero_ids{topology->atom_ids()[0], model::AtomId{0U}};
  passed &= expect(
      !trajectory::resolve_atom_mapping(
           {trajectory::AtomMappingPolicy::explicit_map, topology.get(), 2U,
            {}, missing_id})
           .has_value() &&
          !trajectory::resolve_atom_mapping(
               {trajectory::AtomMappingPolicy::explicit_map, topology.get(),
                2U, {}, unknown_ids})
               .has_value() &&
          !trajectory::resolve_atom_mapping(
               {trajectory::AtomMappingPolicy::explicit_map, topology.get(),
                2U, {}, zero_ids})
               .has_value(),
      "missing, unknown and zero explicit stable IDs must fail");

  const std::array identities{
      trajectory::TrajectoryAtomIdentity{11, "N", "GLY", 7, "", "A",
                                         "PROT", 7},
      trajectory::TrajectoryAtomIdentity{12, "CA", "GLY", 7, "", "A",
                                         "PROT", 6}};
  const auto exact = trajectory::resolve_atom_mapping(
      {trajectory::AtomMappingPolicy::exact, topology.get(), 2U, identities,
       {}});
  passed &= expect(exact.has_value() &&
                       exact.value().identity_strength ==
                           "full-identity-exact" &&
                       exact.value().compared_axes.size() == 8U,
                   "exact mapping must compare the complete identity tuple");
  const std::array serial_identities{
      trajectory::TrajectoryAtomIdentity{.source_serial = 11},
      trajectory::TrajectoryAtomIdentity{.source_serial = 12}};
  const auto serial_exact = trajectory::resolve_atom_mapping(
      {trajectory::AtomMappingPolicy::exact, topology.get(), 2U,
       serial_identities, {}});
  passed &= expect(serial_exact.has_value() &&
                       serial_exact.value().identity_strength ==
                           "available-identity-exact" &&
                       serial_exact.value().compared_axes ==
                           std::vector<std::string>{"source_serial"},
                   "source-ID formats must report their narrower exact "
                   "identity strength");
  auto mismatch = identities;
  mismatch[1].atom_name = "CB";
  const auto rejected = trajectory::resolve_atom_mapping(
      {trajectory::AtomMappingPolicy::exact, topology.get(), 2U, mismatch, {}});
  passed &= expect(!rejected.has_value() &&
                       rejected.error().details.at("source_ordinal") == "1" &&
                       rejected.error().details.at("identity_axis") ==
                           "atom_name" &&
                       rejected.error().details.at("expected") == "CA" &&
                       rejected.error().details.at("actual") == "CB",
                   "exact identity mismatch must identify its source ordinal");
  passed &= expect(
      !trajectory::resolve_atom_mapping(
           {trajectory::AtomMappingPolicy::exact, topology.get(), 2U, {}, {}})
           .has_value(),
      "exact mapping must not fall back to atom count or index order");

  const auto source = model::InMemoryCoordinateSource::create(
      2U, {frame(1U, 1000.0), frame(2U, 2000.0)});
  const auto semantic =
      trajectory::SemanticCoordinateSource::create(source.value());
  const auto normalized = semantic.value()->read_frame(1U);
  const auto& metadata = normalized.value()->metadata();
  passed &= expect(
      semantic.has_value() && normalized.has_value() &&
          semantic.value()->report().coordinate_conversion_applied &&
          semantic.value()->report().time_conversion_applied &&
          semantic.value()->report().force_conversion_applied &&
          metadata.coordinate_unit == operation::LengthUnit::angstrom &&
          metadata.physical_time->unit == model::TimeUnit::picosecond &&
          std::abs(metadata.physical_time->value - 2.0) < 1.0e-12 &&
          metadata.velocity_time_unit == model::TimeUnit::picosecond &&
          std::abs(x(normalized.value()->positions(), 0U) - 10.0) < 1.0e-12 &&
          std::abs(x(*normalized.value()->velocities(), 0U) - 10.0) < 1.0e-12 &&
          std::abs(metadata.unit_cell->a.x - 10.0) < 1.0e-12 &&
          std::abs(numeric(metadata.atom_properties.at("force_x"), 0U) - 1.0) <
              1.0e-12,
      "semantic source must normalize position/cell/velocity/time/force units");

  const auto drift_source = model::InMemoryCoordinateSource::create(
      2U, {frame(1U, 1000.0), frame(2U, 2000.0, false)});
  const auto drift =
      trajectory::SemanticCoordinateSource::create(drift_source.value());
  passed &= expect(drift.has_value() &&
                       !drift.value()->read_frame(1U).has_value(),
                   "optional channel drift must be a typed read failure");
  const auto step_source = model::InMemoryCoordinateSource::create(
      2U, {frame(2U, 1000.0), frame(1U, 2000.0)});
  const auto non_monotonic_step =
      trajectory::SemanticCoordinateSource::create(step_source.value());
  const auto time_source = model::InMemoryCoordinateSource::create(
      2U, {frame(1U, 2000.0), frame(2U, 1000.0)});
  const auto non_monotonic_time =
      trajectory::SemanticCoordinateSource::create(time_source.value());
  passed &= expect(
      non_monotonic_step.has_value() &&
          !non_monotonic_step.value()->read_frame(1U).has_value() &&
          non_monotonic_time.has_value() &&
          !non_monotonic_time.value()->read_frame(1U).has_value(),
      "non-monotonic adjacent step and time must fail independently");

  const auto valid = frame(1U, 1000.0);
  const auto make_changed_frame = [&](model::FrameMetadata metadata) {
    return model::CoordinateFrame::create(
               valid->positions(), valid->velocities(), valid->presence(),
               std::move(metadata))
        .value();
  };
  auto no_cell_metadata = valid->metadata();
  no_cell_metadata.source_step = 2U;
  no_cell_metadata.physical_time->value = 2000.0;
  no_cell_metadata.unit_cell.reset();
  const auto cell_drift_source = model::InMemoryCoordinateSource::create(
      2U, {valid, make_changed_frame(std::move(no_cell_metadata))});
  auto unit_drift_metadata = valid->metadata();
  unit_drift_metadata.source_step = 2U;
  unit_drift_metadata.physical_time->value = 2000.0;
  unit_drift_metadata.coordinate_unit = operation::LengthUnit::angstrom;
  const auto unit_drift_source = model::InMemoryCoordinateSource::create(
      2U, {valid, make_changed_frame(std::move(unit_drift_metadata))});
  auto force_drift_metadata = valid->metadata();
  force_drift_metadata.source_step = 2U;
  force_drift_metadata.physical_time->value = 2000.0;
  for (const auto* name : {"force_x", "force_y", "force_z"})
    force_drift_metadata.atom_properties.at(name).metadata.unit =
        "kilocalorie/mole/angstrom";
  const auto force_drift_source = model::InMemoryCoordinateSource::create(
      2U, {valid, make_changed_frame(std::move(force_drift_metadata))});
  const auto cell_drift = trajectory::SemanticCoordinateSource::create(
      cell_drift_source.value());
  const auto unit_drift = trajectory::SemanticCoordinateSource::create(
      unit_drift_source.value());
  const auto force_drift = trajectory::SemanticCoordinateSource::create(
      force_drift_source.value());
  passed &= expect(
      cell_drift.has_value() &&
          !cell_drift.value()->read_frame(1U).has_value() &&
          unit_drift.has_value() &&
          !unit_drift.value()->read_frame(1U).has_value() &&
          force_drift.has_value() &&
          !force_drift.value()->read_frame(1U).has_value(),
      "cell, coordinate-unit and force-unit drift must fail explicitly");

  auto incomplete_metadata = valid->metadata();
  incomplete_metadata.atom_properties.erase("force_z");
  const auto incomplete_frame = model::CoordinateFrame::create(
      valid->positions(), valid->velocities(), valid->presence(),
      std::move(incomplete_metadata));
  const auto incomplete_source = model::InMemoryCoordinateSource::create(
      2U, {incomplete_frame.value()});
  passed &= expect(
      !trajectory::SemanticCoordinateSource::create(incomplete_source.value())
           .has_value(),
      "incomplete force vectors must fail semantic validation");

  auto unsupported_metadata = valid->metadata();
  for (const auto* name : {"force_x", "force_y", "force_z"})
    unsupported_metadata.atom_properties.at(name).metadata.unit = "newton";
  const auto unsupported_frame = model::CoordinateFrame::create(
      valid->positions(), valid->velocities(), valid->presence(),
      std::move(unsupported_metadata));
  const auto unsupported_source = model::InMemoryCoordinateSource::create(
      2U, {unsupported_frame.value()});
  passed &= expect(
      !trajectory::SemanticCoordinateSource::create(unsupported_source.value())
           .has_value(),
      "unknown force units must fail instead of being guessed");

  auto non_finite_force_metadata = valid->metadata();
  std::get<std::vector<double>>(
      non_finite_force_metadata.atom_properties.at("force_x").values)[0] =
      std::numeric_limits<double>::quiet_NaN();
  const auto non_finite_force = make_changed_frame(
      std::move(non_finite_force_metadata));
  const auto non_finite_force_source = model::InMemoryCoordinateSource::create(
      2U, {non_finite_force});
  passed &= expect(
      !trajectory::SemanticCoordinateSource::create(
           non_finite_force_source.value())
           .has_value(),
      "non-finite force values must fail semantic validation");

  auto untyped_velocity_metadata = valid->metadata();
  untyped_velocity_metadata.velocity_time_unit.reset();
  const auto untyped_velocity_frame = model::CoordinateFrame::create(
      valid->positions(), valid->velocities(), valid->presence(),
      std::move(untyped_velocity_metadata));
  const auto untyped_velocity_source = model::InMemoryCoordinateSource::create(
      2U, {untyped_velocity_frame.value()});
  passed &= expect(
      !trajectory::SemanticCoordinateSource::create(
           untyped_velocity_source.value())
           .has_value(),
      "velocity without a time unit must fail semantic validation");

  return passed ? 0 : 1;
}
