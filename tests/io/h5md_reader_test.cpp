#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "molshredder/io/trajectory_reader.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

bool close(double first, double second, double tolerance = 1.0e-6) {
  return std::abs(first - second) <= tolerance;
}

template <typename Vector>
const Vector *property(const molshredder::model::FrameMetadata &metadata,
                       std::string_view name) {
  const auto found = metadata.atom_properties.find(name);
  if (found == metadata.atom_properties.end())
    return nullptr;
  return std::get_if<Vector>(&found->second.values);
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  if (argc != 7) {
    std::cerr << "expected standard, fixed, ambiguous, partial and missing "
                 "and dynamic-ID fixtures\n";
    return 2;
  }
  bool passed = true;
  io::H5mdMetadata metadata;
  const std::vector<std::int64_t> source_ids{10, 20, 30};
  const auto source = io::open_h5md(argv[1], 3U, source_ids, std::nullopt,
                                    std::nullopt, &metadata);
  passed &= expect(source.has_value(), "standard H5MD must open");
  if (source.has_value()) {
    passed &= expect(
        source.value()->atom_count() == 3U &&
            source.value()->frame_count() == std::optional<std::size_t>{2U} &&
            source.value()->access() == model::FrameAccess::random_access,
        "H5MD source must expose topology-sized random access");
    passed &= expect(
        metadata.version_major == 1 && metadata.version_minor == 1 &&
            metadata.particle_group == "trajectory" &&
            metadata.stored_particle_count == 3U && metadata.has_time &&
            metadata.has_velocities && metadata.has_forces &&
            metadata.has_ids && !metadata.dynamic_ids && metadata.has_mass &&
            metadata.has_charge && metadata.has_species &&
            metadata.has_unit_cell && metadata.time_dependent_box &&
            metadata.position_unit == "nm" && metadata.time_unit == "ps",
        "H5MD metadata must preserve schema, channel, unit and box facts");
    const auto frame1 = source.value()->read_frame(1U);
    passed &= expect(frame1.has_value(), "second H5MD frame must decode");
    if (frame1.has_value()) {
      const auto *positions = std::get_if<std::vector<model::Vec3f>>(
          &frame1.value()->positions().values());
      passed &= expect(
          positions != nullptr && positions->size() == 3U &&
              close((*positions)[0].x, 11.0) &&
              close((*positions)[0].y, 12.0) &&
              close((*positions)[0].z, 13.0) &&
              close((*positions)[1].x, 12.0) && close((*positions)[2].x, 13.0),
          "positions must convert nm to angstrom and reorder by source ID");
      const auto *velocities =
          frame1.value()->velocities().has_value()
              ? std::get_if<std::vector<model::Vec3d>>(
                    &frame1.value()->velocities()->values())
              : nullptr;
      passed &= expect(
          velocities != nullptr && velocities->size() == 3U &&
              close((*velocities)[0].x, 1.3) &&
              close((*velocities)[1].x, 1.6) && close((*velocities)[2].x, 1.0),
          "velocities must convert to angstrom/ps and follow ID mapping");
      const auto &frame_metadata = frame1.value()->metadata();
      passed &= expect(
          frame_metadata.source_step == std::optional<std::uint64_t>{20U} &&
              frame_metadata.physical_time.has_value() &&
              close(frame_metadata.physical_time->value, 0.5) &&
              frame_metadata.physical_time->unit ==
                  model::TimeUnit::picosecond &&
              frame_metadata.unit_cell.has_value() &&
              close(frame_metadata.unit_cell->a.x, 20.0) &&
              close(frame_metadata.unit_cell->b.x, 4.0) &&
              close(frame_metadata.unit_cell->b.y, 21.0) &&
              close(frame_metadata.unit_cell->c.z, 22.0),
          "step/time and row-vector triclinic cell must survive conversion");
      const auto *ids =
          property<std::vector<std::int64_t>>(frame_metadata, "h5md.id");
      const auto *mass =
          property<std::vector<double>>(frame_metadata, "h5md.mass");
      const auto *charge =
          property<std::vector<double>>(frame_metadata, "h5md.charge");
      const auto *species =
          property<std::vector<std::int64_t>>(frame_metadata, "h5md.species");
      const auto *force_x =
          property<std::vector<float>>(frame_metadata, "h5md.force.x");
      passed &= expect(
          ids != nullptr && *ids == std::vector<std::int64_t>({10, 20, 30}) &&
              mass != nullptr && close((*mass)[0], 10.0) &&
              close((*mass)[1], 20.0) && close((*mass)[2], 30.0) &&
              charge != nullptr && close((*charge)[0], 0.1) &&
              species != nullptr &&
              *species == std::vector<std::int64_t>({6, 7, 8}) &&
              force_x != nullptr && close((*force_x)[0], 6.5) &&
              close((*force_x)[1], 8.0) && close((*force_x)[2], 5.0),
          "ID, mass, charge, species and force properties must reorder");
    }
    passed &= expect(!source.value()->read_frame(2U).has_value(),
                     "out-of-range H5MD frame must fail");
  }

  const auto generic = io::open_trajectory(
      argv[1], io::TrajectoryFormat::auto_detect,
      io::TrajectoryOpenContext{3U, source_ids, std::nullopt, std::nullopt,
                                std::nullopt});
  passed &= expect(generic.has_value() &&
                       generic.value().format == io::TrajectoryFormat::h5md,
                   ".h5md must auto-detect through generic trajectory I/O");

  io::H5mdMetadata fixed_metadata;
  const auto fixed =
      io::open_h5md(argv[2], 3U, {}, operation::LengthUnit::angstrom,
                    std::nullopt, &fixed_metadata);
  passed &= expect(fixed.has_value(),
                   "unit override must open unitless fixed-step H5MD");
  if (fixed.has_value()) {
    const auto frame = fixed.value()->read_frame(1U);
    passed &= expect(frame.has_value(), "fixed-step frame must decode");
    if (frame.has_value()) {
      const auto *positions = std::get_if<std::vector<model::Vec3d>>(
          &frame.value()->positions().values());
      passed &= expect(
          positions != nullptr && close((*positions)[0].x, 1.0) &&
              frame.value()->metadata().source_step ==
                  std::optional<std::uint64_t>{12U} &&
              frame.value()->metadata().physical_time.has_value() &&
              close(frame.value()->metadata().physical_time->value, 0.00125) &&
              frame.value()->metadata().unit_cell.has_value() &&
              close(frame.value()->metadata().unit_cell->b.y, 3.0),
          "fixed step/time offsets, fs conversion and static box must decode");
    }
  }
  passed &= expect(!io::open_h5md(argv[2], 3U).has_value(),
                   "unitless positions without an override must fail");
  passed &= expect(!io::open_h5md(argv[3], 3U, source_ids).has_value(),
                   "ambiguous particle groups must require a choice");
  passed &= expect(
      io::open_h5md(argv[3], 3U, source_ids, std::nullopt, std::string{"alpha"})
          .has_value(),
      "explicit particle-group selection must work");
  passed &= expect(!io::open_h5md(argv[4], 3U, source_ids).has_value(),
                   "partially periodic box must not be misinterpreted");
  const auto missing = io::open_h5md(argv[5], 3U, source_ids);
  passed &= expect(missing.has_value() &&
                       !missing.value()->read_frame(1U).has_value(),
                   "missing coordinates for present IDs must fail on decode");
  passed &= expect(!io::open_h5md(argv[1], 3U).has_value(),
                   "non-index IDs without topology source IDs must fail");
  io::H5mdMetadata dynamic_metadata;
  const auto dynamic = io::open_h5md(argv[6], 3U, source_ids, std::nullopt,
                                     std::nullopt, &dynamic_metadata);
  passed &= expect(dynamic.has_value() && dynamic_metadata.dynamic_ids &&
                       dynamic_metadata.stored_particle_count == 4U,
                   "dynamic ID file must expose variable-particle storage");
  if (dynamic.has_value()) {
    const auto frame = dynamic.value()->read_frame(1U);
    passed &= expect(frame.has_value(), "dynamic ID frame must decode");
    if (frame.has_value()) {
      const auto *positions = std::get_if<std::vector<model::Vec3f>>(
          &frame.value()->positions().values());
      const auto *mass =
          property<std::vector<double>>(frame.value()->metadata(), "h5md.mass");
      passed &= expect(
          positions != nullptr && close((*positions)[0].x, 10.0) &&
              close((*positions)[2].x, 30.0) &&
              frame.value()->presence() ==
                  std::vector<std::uint8_t>({1U, 0U, 1U}) &&
              mass != nullptr && close((*mass)[0], 10.0) &&
              close((*mass)[2], 30.0),
          "dynamic IDs, fill values, presence and properties must scatter");
    }
  }
  passed &= expect(!io::open_h5md(std::filesystem::path{argv[1]}.parent_path() /
                                      "missing.h5md",
                                  3U)
                        .has_value(),
                   "missing H5MD file must fail");
  return passed ? 0 : 1;
}
