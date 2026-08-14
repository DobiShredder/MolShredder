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
    std::cerr << "expected MDCRD fixture and build directory\n";
    return 2;
  }
  bool passed = true;
  const auto source = io::open_amber_ascii_trajectory(argv[1], 4U, 90.0);
  passed &= expect(source.has_value() && source.value()->atom_count() == 4U &&
                       source.value()->frame_count() == 2U &&
                       source.value()->access() ==
                           model::FrameAccess::random_access &&
                       source.value()->metadata().box_value_count == 3U,
                   "MDCRD must index both fixed-width boxed frames");
  if (source.has_value()) {
    const auto second = source.value()->read_frame(1U);
    const auto first = source.value()->read_frame(0U);
    passed &= expect(second.has_value() && first.has_value(),
                     "MDCRD frames must decode out of order");
    if (second.has_value()) {
      const auto &positions = std::get<std::vector<model::Vec3d>>(
          second.value()->positions().values());
      passed &= expect(
          positions[0].x == 1.5 && positions[3].z == 6.5 &&
              second.value()->metadata().unit_cell.has_value() &&
              std::abs(second.value()->metadata().unit_cell->signed_volume() -
                       1509.375) < 1.0e-9 &&
              second.value()->metadata().fields.at("box_angle_source") ==
                  "prmtop-box-dimensions",
          "MDCRD coordinates and topology-derived box angles must be typed");
    }
    const auto outside = source.value()->read_frame(2U);
    passed &=
        expect(!outside.has_value(), "MDCRD out-of-range frame must fail");
  }

  const auto missing_angle =
      io::open_amber_ascii_trajectory(argv[1], 4U, std::nullopt);
  passed &=
      expect(!missing_angle.has_value() &&
                 missing_angle.error().message.find("BOX_DIMENSIONS") !=
                     std::string::npos,
             "three-value MDCRD box must not invent missing topology angles");
  const auto mismatch = io::open_amber_ascii_trajectory(argv[1], 5U, 90.0);
  passed &= expect(!mismatch.has_value(),
                   "MDCRD topology atom-count mismatch must fail");

  const auto small_path =
      std::filesystem::path{argv[2]} / "ambiguous-small.mdcrd";
  {
    std::ofstream output{small_path};
    output << "small\n"
              "   1.000   2.000   3.000\n"
              "   4.000   5.000   6.000\n";
  }
  const auto small =
      io::open_amber_ascii_trajectory(small_path, 1U, std::nullopt);
  passed &= expect(!small.has_value() &&
                       small.error().code == operation::ErrorCode::unsupported,
                   "small-system frame/box ambiguity must not be guessed");

  const auto malformed_path =
      std::filesystem::path{argv[2]} / "malformed.mdcrd";
  {
    std::ofstream output{malformed_path};
    output << "truncated\n"
              "   1.000   2.000   3.000   4.000\n";
  }
  const auto malformed =
      io::open_amber_ascii_trajectory(malformed_path, 4U, 90.0);
  passed &=
      expect(!malformed.has_value() && malformed.error().message.find(
                                           "requires 10") != std::string::npos,
             "truncated MDCRD coordinate rows must fail deterministically");
  std::error_code ignored;
  std::filesystem::remove(small_path, ignored);
  std::filesystem::remove(malformed_path, ignored);
  return passed ? 0 : 1;
}
