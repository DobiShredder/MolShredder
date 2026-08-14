#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string_view>
#include <variant>

#include "molshredder/io/trajectory_reader.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

void word(std::ostream &output, std::uint32_t value,
          molshredder::io::ByteOrder order) {
  std::array<unsigned char, 4U> bytes{};
  if (order == molshredder::io::ByteOrder::little_endian) {
    for (std::size_t index = 0U; index < 4U; ++index)
      bytes[index] = static_cast<unsigned char>((value >> (index * 8U)) & 0xffU);
  } else {
    for (std::size_t index = 0U; index < 4U; ++index)
      bytes[index] =
          static_cast<unsigned char>((value >> ((3U - index) * 8U)) & 0xffU);
  }
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

void frame(std::ostream &output, std::int32_t count,
           const std::array<float, 9U> &values,
           molshredder::io::ByteOrder order) {
  word(output, std::bit_cast<std::uint32_t>(count), order);
  for (const auto value : values)
    word(output, std::bit_cast<std::uint32_t>(value), order);
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  if (argc != 3) {
    std::cerr << "expected BINPOS fixture and build directory\n";
    return 2;
  }
  bool passed = true;
  const auto source = io::open_binpos(argv[1], 3U);
  passed &= expect(source.has_value() && source.value()->atom_count() == 3U &&
                       source.value()->frame_count() == 2U &&
                       source.value()->access() ==
                           model::FrameAccess::random_access &&
                       source.value()->metadata().byte_order ==
                           io::ByteOrder::little_endian,
                   "little-endian BINPOS must expose two indexed frames");
  if (source.has_value()) {
    const auto second = source.value()->read_frame(1U);
    const auto first = source.value()->read_frame(0U);
    passed &= expect(second.has_value() && first.has_value(),
                     "BINPOS frames must decode out of order");
    if (second.has_value()) {
      const auto &positions = std::get<std::vector<model::Vec3f>>(
          second.value()->positions().values());
      passed &= expect(
          positions[0] == model::Vec3f{10.0F, 11.0F, 12.0F} &&
              positions[2] == model::Vec3f{16.0F, 17.0F, 18.0F} &&
              second.value()->metadata().coordinate_unit ==
                  operation::LengthUnit::angstrom &&
              !second.value()->metadata().unit_cell.has_value() &&
              !second.value()->metadata().source_step.has_value(),
          "BINPOS must preserve float32 Å coordinates without invented cell/time");
    }
    passed &= expect(!source.value()->read_frame(2U).has_value(),
                     "BINPOS out-of-range seek must fail");
  }
  const auto auto_open = io::open_trajectory(
      argv[1], io::TrajectoryFormat::auto_detect, 3U);
  passed &= expect(auto_open.has_value() &&
                       auto_open.value().format == io::TrajectoryFormat::binpos,
                   "BINPOS suffix must dispatch through generic trajectory API");
  passed &= expect(!io::open_binpos(argv[1], 4U).has_value(),
                   "BINPOS topology atom-count mismatch must fail");

  const auto build = std::filesystem::path{argv[2]};
  constexpr std::array<float, 9U> values{-1.0F, -2.0F, -3.0F, 1.5F, 2.5F,
                                          3.5F, 100.0F, 200.0F, 300.0F};
  const auto big_path = build / "big-endian.binpos";
  {
    std::ofstream output{big_path, std::ios::binary};
    output.write("fxyz", 4);
    frame(output, 3, values, io::ByteOrder::big_endian);
  }
  const auto big = io::open_binpos(big_path, 3U);
  passed &= expect(big.has_value() &&
                       big.value()->metadata().byte_order ==
                           io::ByteOrder::big_endian,
                   "big-endian BINPOS must be detected from topology count");
  if (big.has_value()) {
    const auto decoded = big.value()->read_frame(0U);
    const auto &positions = std::get<std::vector<model::Vec3f>>(
        decoded.value()->positions().values());
    passed &= expect(positions[0] == model::Vec3f{-1.0F, -2.0F, -3.0F} &&
                         positions[2] == model::Vec3f{100.0F, 200.0F, 300.0F},
                     "big-endian float payload must decode exactly");
  }

  const auto varying_path = build / "varying-count.binpos";
  {
    std::ofstream output{varying_path, std::ios::binary};
    output.write("fxyz", 4);
    frame(output, 3, values, io::ByteOrder::little_endian);
    frame(output, 4, values, io::ByteOrder::little_endian);
  }
  passed &= expect(!io::open_binpos(varying_path, 3U).has_value(),
                   "varying BINPOS frame atom count must fail at open");

  const auto nonfinite_path = build / "nonfinite.binpos";
  {
    auto invalid_values = values;
    invalid_values[4] = std::numeric_limits<float>::infinity();
    std::ofstream output{nonfinite_path, std::ios::binary};
    output.write("fxyz", 4);
    frame(output, 3, invalid_values, io::ByteOrder::little_endian);
  }
  const auto nonfinite = io::open_binpos(nonfinite_path, 3U);
  passed &= expect(nonfinite.has_value() &&
                       !nonfinite.value()->read_frame(0U).has_value(),
                   "non-finite BINPOS payload must fail at decode");

  const auto truncated_path = build / "truncated.binpos";
  {
    std::ofstream output{truncated_path, std::ios::binary};
    output.write("fxyz", 4);
    word(output, 3U, io::ByteOrder::little_endian);
    word(output, std::bit_cast<std::uint32_t>(1.0F),
         io::ByteOrder::little_endian);
  }
  passed &= expect(!io::open_binpos(truncated_path, 3U).has_value(),
                   "truncated BINPOS record must fail deterministically");
  std::error_code ignored;
  std::filesystem::remove(big_path, ignored);
  std::filesystem::remove(varying_path, ignored);
  std::filesystem::remove(nonfinite_path, ignored);
  std::filesystem::remove(truncated_path, ignored);
  return passed ? 0 : 1;
}
