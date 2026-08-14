#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

enum class ByteOrder { little, big };

template <typename Integer>
void put_integer(std::vector<std::uint8_t> &bytes, std::size_t offset,
                 Integer value, ByteOrder order) {
  using Unsigned = std::make_unsigned_t<Integer>;
  const auto bits = std::bit_cast<Unsigned>(value);
  for (std::size_t index = 0; index < sizeof(Integer); ++index) {
    const auto shift = order == ByteOrder::little
                           ? index * 8U
                           : (sizeof(Integer) - index - 1U) * 8U;
    bytes[offset + index] = static_cast<std::uint8_t>(bits >> shift);
  }
}

void put_float(std::vector<std::uint8_t> &bytes, std::size_t offset,
               float value, ByteOrder order) {
  put_integer(bytes, offset, std::bit_cast<std::uint32_t>(value), order);
}

void put_text(std::vector<std::uint8_t> &bytes, std::size_t offset,
              std::string_view text) {
  for (std::size_t index = 0; index < text.size(); ++index)
    bytes[offset + index] = static_cast<std::uint8_t>(text[index]);
}

std::vector<std::uint8_t> little_fixture() {
  constexpr std::size_t extended_bytes = 80U;
  std::vector<std::uint8_t> bytes(1024U + extended_bytes, 0U);
  const auto order = ByteOrder::little;
  put_integer<std::int32_t>(bytes, 0U, 3, order);
  put_integer<std::int32_t>(bytes, 4U, 2, order);
  put_integer<std::int32_t>(bytes, 8U, 2, order);
  put_integer<std::int32_t>(bytes, 12U, 2, order);
  put_integer<std::int32_t>(bytes, 16U, 3, order);
  put_integer<std::int32_t>(bytes, 20U, -1, order);
  put_integer<std::int32_t>(bytes, 24U, 2, order);
  put_integer<std::int32_t>(bytes, 28U, 4, order);
  put_integer<std::int32_t>(bytes, 32U, 3, order);
  put_integer<std::int32_t>(bytes, 36U, 6, order);
  put_float(bytes, 40U, 4.0F, order);
  put_float(bytes, 44U, 6.0F, order);
  put_float(bytes, 48U, 9.0F, order);
  put_float(bytes, 52U, 90.0F, order);
  put_float(bytes, 56U, 90.0F, order);
  put_float(bytes, 60U, 60.0F, order);
  put_integer<std::int32_t>(bytes, 64U, 3, order);
  put_integer<std::int32_t>(bytes, 68U, 1, order);
  put_integer<std::int32_t>(bytes, 72U, 2, order);
  put_float(bytes, 76U, 0.0F, order);
  put_float(bytes, 80U, 112.0F, order);
  put_float(bytes, 84U, 56.0F, order);
  put_integer<std::int32_t>(bytes, 88U, 1, order);
  put_integer<std::int32_t>(bytes, 92U,
                            static_cast<std::int32_t>(extended_bytes), order);
  put_text(bytes, 104U, "CCP4");
  put_integer<std::int32_t>(bytes, 108U, 20141, order);
  put_float(bytes, 196U, 10.0F, order);
  put_float(bytes, 200U, 20.0F, order);
  put_float(bytes, 204U, 30.0F, order);
  put_text(bytes, 208U, "MAP ");
  bytes[212U] = 0x44U;
  bytes[213U] = 0x44U;
  put_float(bytes, 216U, 33.0F, order);
  put_integer<std::int32_t>(bytes, 220U, 1, order);
  put_text(bytes, 224U, "MolShredder permuted MRC2014 fixture");
  put_text(bytes, 1024U, "X,Y,Z");

  const std::vector<float> values{0.0F,  1.0F,  2.0F,  100.0F, 101.0F, 102.0F,
                                  10.0F, 11.0F, 12.0F, 110.0F, 111.0F, 112.0F};
  for (const auto value : values) {
    const auto offset = bytes.size();
    bytes.resize(offset + 4U);
    put_float(bytes, offset, value, order);
  }
  return bytes;
}

std::vector<std::uint8_t> big_fixture() {
  std::vector<std::uint8_t> bytes(1024U, 0U);
  const auto order = ByteOrder::big;
  put_integer<std::int32_t>(bytes, 0U, 1, order);
  put_integer<std::int32_t>(bytes, 4U, 1, order);
  put_integer<std::int32_t>(bytes, 8U, 2, order);
  put_integer<std::int32_t>(bytes, 12U, 1, order);
  put_integer<std::int32_t>(bytes, 16U, 1, order);
  put_integer<std::int32_t>(bytes, 20U, 0, order);
  put_integer<std::int32_t>(bytes, 24U, -1, order);
  put_integer<std::int32_t>(bytes, 28U, 1, order);
  put_integer<std::int32_t>(bytes, 32U, 1, order);
  put_integer<std::int32_t>(bytes, 36U, 2, order);
  put_float(bytes, 40U, 1.0F, order);
  put_float(bytes, 44U, 1.0F, order);
  put_float(bytes, 48U, 4.0F, order);
  put_float(bytes, 52U, 90.0F, order);
  put_float(bytes, 56U, 90.0F, order);
  put_float(bytes, 60U, 90.0F, order);
  put_integer<std::int32_t>(bytes, 64U, 1, order);
  put_integer<std::int32_t>(bytes, 68U, 2, order);
  put_integer<std::int32_t>(bytes, 72U, 3, order);
  put_float(bytes, 76U, -2.0F, order);
  put_float(bytes, 80U, 300.0F, order);
  put_float(bytes, 84U, 149.0F, order);
  put_integer<std::int32_t>(bytes, 88U, 1, order);
  put_integer<std::int32_t>(bytes, 108U, 20141, order);
  put_text(bytes, 208U, "MAP ");
  bytes[212U] = 0x11U;
  bytes[213U] = 0x11U;

  bytes.resize(1028U);
  put_integer<std::int16_t>(bytes, 1024U, -2, order);
  put_integer<std::int16_t>(bytes, 1026U, 300, order);
  return bytes;
}

bool write(const std::filesystem::path &path,
           const std::vector<std::uint8_t> &bytes) {
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return output.good();
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: mrc_fixture_generator LITTLE BIG\n";
    return 2;
  }
  if (!write(argv[1], little_fixture()) || !write(argv[2], big_fixture())) {
    std::cerr << "failed to write MRC fixtures\n";
    return 1;
  }
  return 0;
}
