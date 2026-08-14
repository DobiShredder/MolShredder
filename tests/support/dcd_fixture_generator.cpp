#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void append_u32(std::vector<unsigned char>& output, std::uint32_t value) {
  for (unsigned int index = 0; index < 4U; ++index) {
    output.push_back(
        static_cast<unsigned char>((value >> (index * 8U)) & 0xffU));
  }
}

void put_i32(std::vector<unsigned char>& output, std::size_t offset,
             std::int32_t value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  for (unsigned int index = 0; index < 4U; ++index) {
    output[offset + index] =
        static_cast<unsigned char>((bits >> (index * 8U)) & 0xffU);
  }
}

void put_f32(std::vector<unsigned char>& output, std::size_t offset,
             float value) {
  put_i32(output, offset,
          std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value)));
}

void append_f32(std::vector<unsigned char>& output, float value) {
  append_u32(output, std::bit_cast<std::uint32_t>(value));
}

void append_record(std::vector<unsigned char>& output,
                   const std::vector<unsigned char>& payload) {
  append_u32(output, static_cast<std::uint32_t>(payload.size()));
  output.insert(output.end(), payload.begin(), payload.end());
  append_u32(output, static_cast<std::uint32_t>(payload.size()));
}

bool write_fixture(const std::filesystem::path& path) {
  constexpr std::int32_t atom_count = 3;
  constexpr std::int32_t frame_count = 4;
  std::vector<unsigned char> file;
  std::vector<unsigned char> header(84U, 0U);
  std::memcpy(header.data(), "CORD", 4U);
  put_i32(header, 4U, frame_count);
  put_i32(header, 8U, 100);
  put_i32(header, 12U, 10);
  put_f32(header, 40U, 0.5F);
  put_i32(header, 80U, 24);
  append_record(file, header);

  std::vector<unsigned char> title(84U, static_cast<unsigned char>(' '));
  put_i32(title, 0U, 1);
  constexpr std::string_view text{"MolShredder desktop trajectory fixture"};
  std::copy(text.begin(), text.end(), title.begin() + 4);
  append_record(file, title);

  std::vector<unsigned char> atoms;
  append_u32(atoms, static_cast<std::uint32_t>(atom_count));
  append_record(file, atoms);
  for (std::int32_t frame = 0; frame < frame_count; ++frame) {
    for (std::int32_t axis = 0; axis < 3; ++axis) {
      std::vector<unsigned char> coordinates;
      for (std::int32_t atom = 0; atom < atom_count; ++atom) {
        const auto base = static_cast<float>(atom) * 1.5F;
        const auto motion = static_cast<float>(frame) * 0.25F;
        const auto value = axis == 0 ? base + motion
                           : axis == 1 ? static_cast<float>(atom % 2) + motion
                                       : static_cast<float>(axis) * 0.1F;
        append_f32(coordinates, value);
      }
      append_record(file, coordinates);
    }
  }
  std::ofstream stream{path, std::ios::binary};
  stream.write(reinterpret_cast<const char*>(file.data()),
               static_cast<std::streamsize>(file.size()));
  return stream.good();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "expected output DCD path\n";
    return 2;
  }
  return write_fixture(std::filesystem::path{argv[1]}) ? 0 : 1;
}
