#include <cmath>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <variant>

#include "molshredder/io/trajectory_reader.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

bool near(double left, double right, double tolerance = 1.0e-9) {
  return std::abs(left - right) <= tolerance;
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  if (argc != 6) {
    std::cerr << "expected standard, compressed, bad-convention, partial-cell "
                 "and missing-data fixtures\n";
    return 2;
  }
  bool passed = true;
  io::AmberNetcdfMetadata metadata;
  const auto source = io::open_amber_netcdf(argv[1], 4U, &metadata);
  passed &= expect(
      source.has_value() && source.value()->atom_count() == 4U &&
          source.value()->frame_count() == 2U &&
          source.value()->access() == model::FrameAccess::random_access &&
          metadata.has_time && metadata.has_velocities && metadata.has_forces &&
          metadata.has_temperature && metadata.has_unit_cell &&
          !metadata.integer_compressed &&
          metadata.storage_format == "netcdf-64bit-offset" &&
          metadata.program == "fixture-generator",
      "standard Amber NetCDF metadata must preserve all declared channels");
  if (source.has_value()) {
    const auto second = source.value()->read_frame(1U);
    const auto first = source.value()->read_frame(0U);
    passed &= expect(second.has_value() && first.has_value(),
                     "Amber NetCDF frames must decode out of order");
    if (second.has_value()) {
      const auto &positions = std::get<std::vector<model::Vec3f>>(
          second.value()->positions().values());
      const auto &velocities = std::get<std::vector<model::Vec3f>>(
          second.value()->velocities()->values());
      const auto force_x =
          second.value()->metadata().atom_properties.find("force.x");
      passed &= expect(
          positions[0] == model::Vec3f{10.0F, 11.0F, 12.0F} &&
              positions[3] == model::Vec3f{19.0F, 20.0F, 21.0F} &&
              near(velocities[0].x, 1.0, 1.0e-6) &&
              near(velocities[0].y, 1.1, 1.0e-6) &&
              near(velocities[0].z, 1.2, 1.0e-6) &&
              second.value()->metadata().physical_time.has_value() &&
              near(second.value()->metadata().physical_time->value, 1.0) &&
              second.value()->metadata().unit_cell.has_value() &&
              second.value()->metadata().unit_cell->is_valid() &&
              force_x != second.value()->metadata().atom_properties.end() &&
              std::get<std::vector<float>>(force_x->second.values)[3] ==
                  119.0F &&
              force_x->second.metadata.unit ==
                  std::optional<std::string>{"kilocalorie/mole/angstrom"} &&
              second.value()->metadata().fields.at("temperature_unit") ==
                  "kelvin",
          "scale factors, velocity, force, time, temperature and cell must "
          "decode");
    }
    passed &= expect(!source.value()->read_frame(2U).has_value(),
                     "out-of-range Amber NetCDF seek must fail");
  }

  const auto generic =
      io::open_trajectory(argv[1], io::TrajectoryFormat::auto_detect, 4U);
  passed &=
      expect(generic.has_value() &&
                 generic.value().format == io::TrajectoryFormat::amber_netcdf,
             ".nc suffix must dispatch through the generic API");
  passed &= expect(!io::open_amber_netcdf(argv[1], 3U).has_value(),
                   "topology atom-count mismatch must fail at open");

  io::AmberNetcdfMetadata compressed_metadata;
  const auto compressed =
      io::open_amber_netcdf(argv[2], 4U, &compressed_metadata);
  passed &= expect(
      compressed.has_value() && compressed_metadata.integer_compressed &&
          compressed_metadata.storage_format == "netcdf-4" &&
          compressed_metadata.application == "MolShredder tests",
      "integer-compressed NetCDF-4 and NC_STRING metadata must be recognized");
  if (compressed.has_value()) {
    const auto frame = compressed.value()->read_frame(1U);
    const auto &positions = std::get<std::vector<model::Vec3d>>(
        frame.value()->positions().values());
    passed &= expect(positions[1] == model::Vec3d{13.0, 14.0, 15.0},
                     "icompressfac coordinates must decode exactly");
  }
  const auto compressed_auto =
      io::open_trajectory(argv[2], io::TrajectoryFormat::auto_detect, 4U);
  passed &= expect(compressed_auto.has_value() &&
                       compressed_auto.value().format ==
                           io::TrajectoryFormat::amber_netcdf,
                   ".ncdf suffix must auto-detect Amber NetCDF");

  passed &= expect(!io::open_amber_netcdf(argv[3], 4U).has_value(),
                   "non-AMBER NetCDF convention must be rejected");
  passed &= expect(!io::open_amber_netcdf(argv[4], 4U).has_value(),
                   "unpaired cell variables must be rejected");
  const auto missing_data = io::open_amber_netcdf(argv[5], 4U);
  passed &= expect(missing_data.has_value() &&
                       !missing_data.value()->read_frame(0U).has_value(),
                   "declared fill coordinates must fail at frame decode");
  const auto missing = std::filesystem::path{argv[1]}.parent_path() /
                       "missing-amber-trajectory.nc";
  passed &= expect(!io::open_amber_netcdf(missing, 4U).has_value(),
                   "missing Amber NetCDF path must fail");
  return passed ? 0 : 1;
}
