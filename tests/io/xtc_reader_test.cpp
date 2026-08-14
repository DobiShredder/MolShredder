#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

#include "molshredder/io/trajectory_reader.hpp"

namespace {

constexpr std::array<unsigned char, 136> kCompressedFrame{
    0x00, 0x00, 0x07, 0xcb, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x2a,
    0x3f, 0xa0, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x3e, 0x4c, 0xcc, 0xcd, 0x40, 0x40, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x3d, 0xcc, 0xcc, 0xcd, 0x3e, 0x99, 0x99, 0x9a,
    0x40, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x44, 0x7a, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xf8, 0xf8, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x03, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x8c,
    0x00, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x2b, 0x20, 0x96, 0x40, 0x1d,
    0x49, 0xec, 0xcd, 0x0c, 0xb2, 0x0c, 0x65, 0xaa, 0xe9, 0x7d, 0x99, 0xa1,
    0x94, 0x00, 0xc4, 0x6a, 0x45, 0x2d, 0x90, 0xe0, 0x73, 0x1c, 0x3f, 0x81,
    0xb2, 0xd2, 0xea, 0x10, 0x65, 0xb3, 0xc1, 0x97, 0xe6, 0x09, 0x94, 0xfd,
    0x70, 0x7d, 0xb0, 0x00};

void write_u32(std::ostream& output, std::uint32_t value) {
  const std::array<char, 4> bytes{
      static_cast<char>((value >> 24U) & 0xffU),
      static_cast<char>((value >> 16U) & 0xffU),
      static_cast<char>((value >> 8U) & 0xffU),
      static_cast<char>(value & 0xffU)};
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_i32(std::ostream& output, std::int32_t value) {
  write_u32(output, std::bit_cast<std::uint32_t>(value));
}

void write_f32(std::ostream& output, float value) {
  write_u32(output, std::bit_cast<std::uint32_t>(value));
}

void write_uncompressed_frame(std::ostream& output, std::int32_t step,
                              float time, bool zero_box) {
  write_i32(output, 1995);
  write_i32(output, 2);
  write_i32(output, step);
  write_f32(output, time);
  const std::array<float, 9> box = zero_box
                                       ? std::array<float, 9>{}
                                       : std::array<float, 9>{2.0F, 0.0F, 0.0F,
                                                              0.2F, 3.0F, 0.0F,
                                                              0.1F, 0.3F, 4.0F};
  for (const auto value : box) write_f32(output, value);
  write_i32(output, 2);
  for (const auto value : {0.1F, 0.2F, 0.3F, 1.0F, 1.1F, 1.2F}) {
    write_f32(output, value + static_cast<float>(step));
  }
}

bool near(double left, double right, double tolerance = 1.0e-4) {
  return std::abs(left - right) <= tolerance;
}

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace molshredder;
  if (argc != 2) return 2;
  const auto directory = std::filesystem::path{argv[1]};
  const auto compressed_path = directory / "synthetic_compressed.xtc";
  const auto uncompressed_path = directory / "synthetic_uncompressed.xtc";
  const auto long_path = directory / "synthetic_long.xtc";
  const auto corrupt_path = directory / "synthetic_corrupt.xtc";
  {
    std::ofstream output{compressed_path, std::ios::binary};
    output.write(reinterpret_cast<const char*>(kCompressedFrame.data()),
                 static_cast<std::streamsize>(kCompressedFrame.size()));
  }
  {
    std::ofstream output{uncompressed_path, std::ios::binary};
    write_uncompressed_frame(output, 0, 0.0F, false);
    write_uncompressed_frame(output, 1, 0.5F, true);
  }
  {
    std::ofstream output{long_path, std::ios::binary};
    auto bytes = std::vector<unsigned char>{kCompressedFrame.begin(),
                                            kCompressedFrame.end()};
    bytes[2] = 0x07;
    bytes[3] = 0xe7;  // magic 2023
    output.write(reinterpret_cast<const char*>(bytes.data()), 88);
    write_u32(output, 0U);  // high half of 64-bit payload size
    output.write(reinterpret_cast<const char*>(bytes.data() + 88),
                 static_cast<std::streamsize>(bytes.size() - 88U));
  }
  {
    std::ofstream output{corrupt_path, std::ios::binary};
    auto bytes = std::vector<unsigned char>{kCompressedFrame.begin(),
                                            kCompressedFrame.end()};
    bytes[91] = 0x7f;  // claim a payload beyond the file
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }

  bool passed = true;
  const auto compressed = io::open_xtc(compressed_path, 10U);
  passed &= expect(compressed.has_value(), "compressed XTC must open");
  if (!compressed.has_value()) return 1;
  passed &= expect(compressed.value()->metadata().frame_count == 1U &&
                       compressed.value()->metadata().compressed &&
                       compressed.value()->metadata().variant ==
                           io::XtcVariant::legacy_1995,
                   "compressed XTC metadata must be retained");
  const auto frame = compressed.value()->read_frame(0U);
  passed &= expect(frame.has_value(), "compressed XTC frame must decode");
  if (frame.has_value()) {
    const auto& positions = std::get<std::vector<model::Vec3f>>(
        frame.value()->positions().values());
    passed &= expect(positions.size() == 10U && near(positions[0].x, 0.0) &&
                         near(positions[7].y, -14.0) &&
                         near(positions[9].z, 27.0),
                     "compressed coordinates must decode and convert nm to angstrom");
    const auto& cell = frame.value()->metadata().unit_cell.value();
    passed &= expect(near(cell.a.x, 20.0) && near(cell.b.x, 2.0) &&
                         near(cell.c.z, 40.0),
                     "XTC triclinic cell must convert to angstrom");
    passed &= expect(frame.value()->metadata().source_step == 42U &&
                         near(frame.value()->metadata().physical_time->value, 1.25) &&
                         frame.value()->metadata().fields.at("xtc.precision") ==
                             "1000.000000",
                     "XTC step/time/compression precision must be retained");
  }

  const auto uncompressed = io::open_xtc(uncompressed_path, 2U).value();
  passed &= expect(uncompressed->metadata().frame_count == 2U &&
                       !uncompressed->metadata().compressed,
                   "small XTC must index fixed-size uncompressed frames");
  const auto second = uncompressed->read_frame(1U).value();
  const auto& second_positions = std::get<std::vector<model::Vec3f>>(
      second->positions().values());
  passed &= expect(near(second_positions[0].x, 11.0) &&
                       !second->metadata().unit_cell.has_value(),
                   "random small-frame seek and non-periodic cell must work");
  passed &= expect(io::open_xtc(long_path).value()->metadata().variant ==
                           io::XtcVariant::long_2023 &&
                       io::open_xtc(long_path)
                           .value()
                           ->read_frame(0U)
                           .has_value(),
                   "2023 long payload-size XTC variant must decode");
  passed &= expect(!io::open_xtc(compressed_path, 11U).has_value() &&
                       !io::open_xtc(corrupt_path).has_value() &&
                       !io::open_xtc(directory / "missing.xtc").has_value() &&
                       !compressed.value()->read_frame(1U).has_value(),
                   "mismatch, corrupt, missing and out-of-range XTC must fail");
  return passed ? 0 : 1;
}
