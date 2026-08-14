#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "molshredder/io/volume_reader.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

bool near(double left, double right, double tolerance = 1.0e-12) {
  return std::abs(left - right) <= tolerance;
}

constexpr std::string_view kFloatGrid = R"DX(
object "positions" class gridpositions counts 1 1 2
origin 0 0 0
delta 1 0 0
delta 0 1 0
delta 0 0 0.5
object "connections" class gridconnections counts 1 1 2
object "data" class array type float rank 0 items 2 data follows 1.25 -2.5
attribute "dep" string "positions"
object "field" class field
)DX";

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  bool passed = true;
  passed &= expect(argc == 2, "OpenDX reader test requires fixture path");
  if (argc != 2)
    return 1;

  io::VolumeReadOptions options;
  options.coordinate_unit = operation::LengthUnit::nanometer;
  const auto file =
      io::read_volume_file(std::filesystem::path{argv[1]}, options);
  passed &= expect(file.has_value() &&
                       file.value().format == io::VolumeFormat::opendx &&
                       file.value().volumes.size() == 1U,
                   "OpenDX file must auto-detect one regular scalar field");
  if (file.has_value()) {
    const auto &volume = file.value().volumes.front();
    const auto &grid = *volume.grid;
    const auto range = grid.scalars().range();
    const auto final_position = grid.position(1U, 1U, 2U);
    passed &= expect(
        volume.name == "potential" &&
            grid.shape() == model::VolumeShape{2U, 2U, 3U} &&
            grid.value_count() == 12U &&
            grid.scalars().precision() == model::VolumePrecision::float64 &&
            near(grid.value(1U, 1U, 2U), 9.5) && near(final_position.x, -0.4) &&
            near(final_position.y, 3.4) && near(final_position.z, 3.5) &&
            range == std::pair<double, double>{-2.5, 9.5} &&
            grid.metadata().coordinate_unit ==
                operation::LengthUnit::nanometer &&
            grid.metadata().fields.at("ordering") == "z_fastest" &&
            grid.metadata().fields.at("dependency") == "positions",
        "OpenDX must preserve shape, skewed geometry, ordering and values");
  }

  const auto float_grid = io::read_volume(kFloatGrid);
  if (!float_grid.has_value())
    std::cerr << float_grid.error().message << '\n';
  passed &= expect(
      float_grid.has_value() &&
          float_grid.value().volumes.front().grid->scalars().precision() ==
              model::VolumePrecision::float32 &&
          float_grid.value().volumes.front().grid->value(0U, 0U, 1U) == -2.5,
      "OpenDX inline float arrays and quoted identifiers must load");

  const auto mismatch = io::read_volume(std::string{kFloatGrid}.replace(
      std::string{kFloatGrid}.find("gridconnections counts 1 1 2"),
      std::string{"gridconnections counts 1 1 2"}.size(),
      "gridconnections counts 1 2 2"));
  const auto truncated = io::read_volume(
      std::string{kFloatGrid}.replace(std::string{kFloatGrid}.find("1.25 -2.5"),
                                      std::string{"1.25 -2.5"}.size(), "1.25"));
  const auto vector_array = io::read_volume(
      std::string{kFloatGrid}.replace(std::string{kFloatGrid}.find("rank 0"),
                                      std::string{"rank 0"}.size(), "rank 1"));
  const auto degenerate = io::read_volume(std::string{kFloatGrid}.replace(
      std::string{kFloatGrid}.find("delta 0 1 0"),
      std::string{"delta 0 1 0"}.size(), "delta 2 0 0"));
  const auto non_finite = io::read_volume(std::string{kFloatGrid}.replace(
      std::string{kFloatGrid}.find("1.25 -2.5"),
      std::string{"1.25 -2.5"}.size(), "nan -2.5"));
  const auto excess = io::read_volume(std::string{kFloatGrid}.replace(
      std::string{kFloatGrid}.find("1.25 -2.5"),
      std::string{"1.25 -2.5"}.size(), "1.25 -2.5\n3.0"));
  io::VolumeReadOptions binary_options;
  binary_options.format = io::VolumeFormat::opendx;
  const auto binary = io::read_volume(std::string{"object\0gridpositions", 20U},
                                      binary_options);
  passed &= expect(!mismatch.has_value() && !truncated.has_value() &&
                       !vector_array.has_value() && !degenerate.has_value() &&
                       !non_finite.has_value() && !excess.has_value() &&
                       !binary.has_value(),
                   "OpenDX must reject mismatched, truncated, vector, "
                   "degenerate, non-finite, excess and binary input");

  passed &= expect(io::to_string(io::VolumeFormat::opendx) == "opendx" &&
                       io::to_string(io::VolumeFormat::auto_detect) == "auto",
                   "volume format names must be stable");
  return passed ? 0 : 1;
}
