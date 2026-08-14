#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>

namespace {

void word(std::ostream &output, std::uint32_t value) {
  const std::array<unsigned char, 4U> bytes{
      static_cast<unsigned char>(value & 0xffU),
      static_cast<unsigned char>((value >> 8U) & 0xffU),
      static_cast<unsigned char>((value >> 16U) & 0xffU),
      static_cast<unsigned char>((value >> 24U) & 0xffU)};
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

void frame(std::ostream &output, std::span<const float> values) {
  word(output, 3U);
  for (const auto value : values)
    word(output, std::bit_cast<std::uint32_t>(value));
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "expected output path\n";
    return 2;
  }
  std::ofstream output{argv[1], std::ios::binary};
  if (!output)
    return 1;
  output.write("fxyz", 4);
  constexpr std::array<float, 9U> first{0.0F, 1.0F, 2.0F, 3.0F, 4.0F,
                                        5.0F, 6.0F, 7.0F, 8.0F};
  constexpr std::array<float, 9U> second{10.0F, 11.0F, 12.0F, 13.0F, 14.0F,
                                         15.0F, 16.0F, 17.0F, 18.0F};
  frame(output, first);
  frame(output, second);
  return output ? 0 : 1;
}
