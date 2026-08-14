#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <variant>

#include "molshredder/io/trajectory_reader.hpp"
#include "molshredder/model/coordinates.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  if (argc != 3) {
    std::cerr << "expected RST7 fixture and build directory\n";
    return 2;
  }
  bool passed = true;
  io::AmberRestartMetadata metadata;
  const auto source = io::open_amber_restart(argv[1], 4U, &metadata);
  passed &= expect(
      source.has_value() && source.value()->atom_count() == 4U &&
          source.value()->frame_count() == 1U && metadata.has_time &&
          metadata.has_temperature && metadata.has_velocities &&
          metadata.has_unit_cell,
      "RST7 must expose one coordinate/velocity/time/temperature/box frame");
  if (source.has_value()) {
    const auto frame = source.value()->read_frame(0U);
    passed &= expect(frame.has_value(), "RST7 frame zero must decode");
    if (frame.has_value()) {
      const auto &positions = std::get<std::vector<model::Vec3d>>(
          frame.value()->positions().values());
      const auto &velocities = std::get<std::vector<model::Vec3d>>(
          frame.value()->velocities()->values());
      passed &= expect(
          positions[3].z == 11.0 &&
              std::abs(velocities[0].x - 2.0455) < 1.0e-12 &&
              frame.value()->metadata().physical_time.has_value() &&
              frame.value()->metadata().physical_time->value == 1.25 &&
              frame.value()->metadata().unit_cell.has_value() &&
              frame.value()->metadata().unit_cell->signed_volume() > 7000.0 &&
              frame.value()->metadata().fields.at("temperature_unit") ==
                  "kelvin",
          "RST7 values, AKMA velocity scale and triclinic cell must be typed");
    }
  }
  const auto mismatch =
      io::open_trajectory(argv[1], io::TrajectoryFormat::rst7, 5U);
  passed &= expect(!mismatch.has_value() &&
                       mismatch.error().message.find("does not match") !=
                           std::string::npos,
                   "RST7 attachment must reject topology atom-count mismatch");

  const auto malformed_path =
      std::filesystem::path{argv[2]} / "malformed_optional.rst7";
  {
    std::ofstream output{malformed_path};
    output << "bad optional block\n    4\n"
              "   0.0000000   0.0000000   0.0000000   1.0000000   1.0000000   "
              "1.0000000\n"
              "   2.0000000   2.0000000   2.0000000   3.0000000   3.0000000   "
              "3.0000000\n"
              "   1.0000000\n";
  }
  const auto malformed = io::open_amber_restart(malformed_path, 4U);
  passed &= expect(
      !malformed.has_value() &&
          malformed.error().message.find("trailing block") != std::string::npos,
      "RST7 incomplete optional blocks must fail deterministically");
  std::error_code ignored;
  std::filesystem::remove(malformed_path, ignored);
  return passed ? 0 : 1;
}
