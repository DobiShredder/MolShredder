#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

#include "molshredder/io/trajectory_reader.hpp"
#include "molshredder/model/coordinates.hpp"

namespace {

using molshredder::io::ByteOrder;

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

void append_u32(std::vector<unsigned char>& output, std::uint32_t value,
                ByteOrder order) {
  for (unsigned int index = 0; index < 4U; ++index) {
    const auto shift = order == ByteOrder::little_endian ? index * 8U
                                                         : (3U - index) * 8U;
    output.push_back(static_cast<unsigned char>((value >> shift) & 0xffU));
  }
}

void put_u32(std::vector<unsigned char>& output, std::size_t offset,
             std::uint32_t value, ByteOrder order) {
  std::vector<unsigned char> bytes;
  append_u32(bytes, value, order);
  std::copy(bytes.begin(), bytes.end(), output.begin() +
                                              static_cast<std::ptrdiff_t>(offset));
}

void put_i32(std::vector<unsigned char>& output, std::size_t offset,
             std::int32_t value, ByteOrder order) {
  put_u32(output, offset, std::bit_cast<std::uint32_t>(value), order);
}

void put_f32(std::vector<unsigned char>& output, std::size_t offset,
             float value, ByteOrder order) {
  put_u32(output, offset, std::bit_cast<std::uint32_t>(value), order);
}

void append_f32(std::vector<unsigned char>& output, float value,
                ByteOrder order) {
  append_u32(output, std::bit_cast<std::uint32_t>(value), order);
}

void append_f64(std::vector<unsigned char>& output, double value,
                ByteOrder order) {
  const auto bits = std::bit_cast<std::uint64_t>(value);
  for (unsigned int index = 0; index < 8U; ++index) {
    const auto shift = order == ByteOrder::little_endian ? index * 8U
                                                         : (7U - index) * 8U;
    output.push_back(static_cast<unsigned char>((bits >> shift) & 0xffU));
  }
}

void put_f64(std::vector<unsigned char>& output, std::size_t offset,
             double value, ByteOrder order) {
  std::vector<unsigned char> bytes;
  append_f64(bytes, value, order);
  std::copy(bytes.begin(), bytes.end(), output.begin() +
                                              static_cast<std::ptrdiff_t>(offset));
}

void append_record(std::vector<unsigned char>& output,
                   const std::vector<unsigned char>& payload,
                   ByteOrder order) {
  append_u32(output, static_cast<std::uint32_t>(payload.size()), order);
  output.insert(output.end(), payload.begin(), payload.end());
  append_u32(output, static_cast<std::uint32_t>(payload.size()), order);
}

void write_dcd(const std::filesystem::path& path, ByteOrder order,
               std::int32_t header_frames, std::int32_t written_frames,
               std::int32_t atom_count, bool with_cell,
               std::int32_t fixed_atoms = 0, bool xplor = false) {
  std::vector<unsigned char> file;
  std::vector<unsigned char> header(84U, 0U);
  std::memcpy(header.data(), "CORD", 4U);
  put_i32(header, 4U, header_frames, order);
  put_i32(header, 8U, 100, order);
  put_i32(header, 12U, 10, order);
  put_i32(header, 20U, fixed_atoms, order);
  if (xplor) {
    put_f64(header, 40U, 0.5, order);
  } else {
    put_f32(header, 40U, 0.5F, order);
    put_i32(header, 44U, with_cell ? 1 : 0, order);
    put_i32(header, 80U, 24, order);
  }
  append_record(file, header, order);

  std::vector<unsigned char> title(84U, static_cast<unsigned char>(' '));
  put_i32(title, 0U, 1, order);
  constexpr std::string_view title_text{"MolShredder synthetic DCD"};
  std::copy(title_text.begin(), title_text.end(), title.begin() + 4);
  append_record(file, title, order);

  std::vector<unsigned char> atoms;
  append_u32(atoms, static_cast<std::uint32_t>(atom_count), order);
  append_record(file, atoms, order);
  for (std::int32_t frame = 0; frame < written_frames; ++frame) {
    if (with_cell) {
      std::vector<unsigned char> cell;
      for (const double value : {10.0, 90.0, 20.0, 90.0, 90.0, 30.0}) {
        append_f64(cell, value, order);
      }
      append_record(file, cell, order);
    }
    for (std::int32_t axis = 0; axis < 3; ++axis) {
      std::vector<unsigned char> coordinates;
      for (std::int32_t atom = 0; atom < atom_count; ++atom) {
        append_f32(coordinates,
                   static_cast<float>(frame * 100 + axis * 10 + atom),
                   order);
      }
      append_record(file, coordinates, order);
    }
  }
  std::ofstream output{path, std::ios::binary};
  output.write(reinterpret_cast<const char*>(file.data()),
               static_cast<std::streamsize>(file.size()));
}

double length(const molshredder::model::Vec3d& vector) {
  return std::sqrt(vector.x * vector.x + vector.y * vector.y +
                   vector.z * vector.z);
}

}  // namespace

int main(int argc, char** argv) {
  using namespace molshredder;
  bool passed = true;
  if (argc != 2) {
    std::cerr << "expected temporary directory\n";
    return 1;
  }
  const std::filesystem::path directory{argv[1]};
  const auto little_path = directory / "synthetic_little.dcd";
  const auto big_path = directory / "synthetic_big.dcd";
  const auto mismatch_path = directory / "synthetic_mismatch.dcd";
  const auto fixed_path = directory / "synthetic_fixed.dcd";
  write_dcd(little_path, io::ByteOrder::little_endian, 2, 2, 3, true);
  write_dcd(big_path, io::ByteOrder::big_endian, 1, 1, 2, false, 0, true);
  write_dcd(mismatch_path, io::ByteOrder::little_endian, 2, 1, 3, false);
  write_dcd(fixed_path, io::ByteOrder::little_endian, 1, 1, 3, false, 1);

  const auto little = io::open_dcd(little_path, 3U);
  passed &= expect(
      little.has_value() && little.value()->atom_count() == 3U &&
          little.value()->frame_count() == 2U &&
          little.value()->access() == model::FrameAccess::random_access &&
          little.value()->metadata().start_step == 100 &&
          little.value()->metadata().save_interval == 10 &&
          little.value()->metadata().dialect == io::DcdDialect::charmm &&
          little.value()->metadata().has_unit_cell &&
          little.value()->metadata().title == "MolShredder synthetic DCD",
      "little-endian DCD metadata and index must load");
  const auto second = little.value()->read_frame(1U);
  const auto* second_positions =
      second.has_value()
          ? std::get_if<std::vector<model::Vec3f>>(
                &second.value()->positions().values())
          : nullptr;
  passed &= expect(
      second_positions != nullptr &&
          (*second_positions)[0] == model::Vec3f{100.0F, 110.0F, 120.0F} &&
          (*second_positions)[2] == model::Vec3f{102.0F, 112.0F, 122.0F} &&
          second.value()->metadata().source_step == 110U &&
          second.value()->metadata().coordinate_unit ==
              operation::LengthUnit::angstrom &&
          second.value()->metadata().unit_cell.has_value() &&
          std::abs(length(second.value()->metadata().unit_cell->a) - 10.0) <
              1.0e-10 &&
          std::abs(length(second.value()->metadata().unit_cell->b) - 20.0) <
              1.0e-10 &&
          std::abs(length(second.value()->metadata().unit_cell->c) - 30.0) <
              1.0e-10,
      "random frame decode must preserve float coordinates, step and cell");
  passed &= expect(!little.value()->read_frame(2U).has_value(),
                   "out-of-range DCD frame must fail");

  const auto big = io::open_dcd(big_path, 2U);
  const auto big_frame = big.has_value() ? big.value()->read_frame(0U)
                                         : operation::Result<
                                               std::shared_ptr<const model::CoordinateFrame>>::failure(
                                               big.error());
  passed &= expect(
      big.has_value() &&
          big.value()->metadata().byte_order == io::ByteOrder::big_endian &&
          big.value()->metadata().dialect == io::DcdDialect::xplor &&
          big.value()->metadata().raw_delta == 0.5 &&
          big_frame.has_value() && !big_frame.value()->metadata().unit_cell &&
          std::get<std::vector<model::Vec3f>>(
              big_frame.value()->positions().values())[1] ==
              model::Vec3f{1.0F, 11.0F, 21.0F},
      "big-endian DCD coordinates must decode");

  passed &= expect(
      !io::open_dcd(little_path, 4U).has_value() &&
          !io::open_dcd(mismatch_path).has_value() &&
          !io::open_dcd(fixed_path).has_value() &&
          !io::open_dcd(directory / "missing.dcd").has_value(),
      "topology mismatch, truncated count, fixed atoms and missing file fail");

  return passed ? 0 : 1;
}
