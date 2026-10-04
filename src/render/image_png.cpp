#include "molshredder/render/image_png.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace molshredder::render {
namespace {

const std::array<std::uint32_t, 256> &crc_table() {
  static const auto table = [] {
    std::array<std::uint32_t, 256> values{};
    for (std::uint32_t index = 0; index < values.size(); ++index) {
      auto crc = index;
      for (int bit = 0; bit < 8; ++bit)
        crc = (crc & 1U) != 0U ? 0xEDB88320U ^ (crc >> 1U) : crc >> 1U;
      values[index] = crc;
    }
    return values;
  }();
  return table;
}

void append_u32(std::vector<std::uint8_t> &output, std::uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8)
    output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
}

void append_chunk(std::vector<std::uint8_t> &output, std::string_view type,
                  const std::vector<std::uint8_t> &data) {
  append_u32(output, static_cast<std::uint32_t>(data.size()));
  const auto start = output.size();
  output.insert(output.end(), type.begin(), type.end());
  output.insert(output.end(), data.begin(), data.end());
  auto crc = 0xFFFFFFFFU;
  for (auto index = start; index < output.size(); ++index)
    crc = crc_table()[(crc ^ output[index]) & 0xFFU] ^ (crc >> 8U);
  append_u32(output, crc ^ 0xFFFFFFFFU);
}

} // namespace

std::vector<std::uint8_t> encode_png_rgba8(const ImageRgba8 &image,
                                           std::optional<double> dpi) {
  std::vector<std::uint8_t> output{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<std::uint8_t> header;
  append_u32(header, static_cast<std::uint32_t>(image.width));
  append_u32(header, static_cast<std::uint32_t>(image.height));
  header.insert(header.end(), {8U, 6U, 0U, 0U, 0U});
  append_chunk(output, "IHDR", header);
  if (dpi.has_value() && *dpi > 0.0) {
    const auto per_metre = static_cast<std::uint32_t>(std::floor(*dpi / 0.0254));
    std::vector<std::uint8_t> physical;
    append_u32(physical, per_metre);
    append_u32(physical, per_metre);
    physical.push_back(1U);
    append_chunk(output, "pHYs", physical);
  }
  // Raw scanlines with filter type 0, wrapped in a zlib stream of stored
  // blocks (max 65535 bytes each) followed by the Adler-32 checksum.
  std::vector<std::uint8_t> raw;
  const auto stride = image.width * 4U;
  raw.reserve((stride + 1U) * image.height);
  for (std::size_t row = 0; row < image.height; ++row) {
    raw.push_back(0U);
    const auto begin = image.pixels.begin() +
                       static_cast<std::ptrdiff_t>(row * stride);
    raw.insert(raw.end(), begin, begin + static_cast<std::ptrdiff_t>(stride));
  }
  std::vector<std::uint8_t> zlib{0x78, 0x01};
  std::size_t offset = 0;
  do {
    const auto block = std::min<std::size_t>(raw.size() - offset, 65535U);
    const auto last = offset + block == raw.size();
    zlib.push_back(last ? 1U : 0U);
    const auto length = static_cast<std::uint16_t>(block);
    zlib.push_back(static_cast<std::uint8_t>(length & 0xFFU));
    zlib.push_back(static_cast<std::uint8_t>(length >> 8U));
    const auto complement = static_cast<std::uint16_t>(0xFFFFU - length);
    zlib.push_back(static_cast<std::uint8_t>(complement & 0xFFU));
    zlib.push_back(static_cast<std::uint8_t>(complement >> 8U));
    zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                raw.begin() + static_cast<std::ptrdiff_t>(offset + block));
    offset += block;
  } while (offset < raw.size());
  std::uint32_t a = 1U;
  std::uint32_t b = 0U;
  for (const auto byte : raw) {
    a = (a + byte) % 65521U;
    b = (b + a) % 65521U;
  }
  append_u32(zlib, (b << 16U) | a);
  append_chunk(output, "IDAT", zlib);
  append_chunk(output, "IEND", {});
  return output;
}

std::optional<operation::Error>
write_png_atomic(const std::filesystem::path &path, const ImageRgba8 &image,
                 std::optional<double> dpi) {
  if (image.width == 0U || image.height == 0U ||
      image.pixels.size() != image.width * image.height * 4U)
    return operation::Error{operation::ErrorCode::invalid_argument,
                            "PNG image dimensions and pixel storage do not match",
                            {}};
  const auto parent = path.parent_path();
  std::error_code error;
  if (!parent.empty() && !std::filesystem::is_directory(parent, error))
    return operation::Error{operation::ErrorCode::not_found,
                            "image output directory does not exist: " +
                                parent.string(),
                            "create the directory or choose another path"};
  const auto bytes = encode_png_rgba8(image, dpi);
  auto temporary = path;
  temporary += ".molshredder-tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
      std::filesystem::remove(temporary, error);
      return operation::Error{operation::ErrorCode::internal,
                              "could not write image output", {}};
    }
  }
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return operation::Error{operation::ErrorCode::internal,
                            "could not move image output into place", {}};
  }
  return std::nullopt;
}

} // namespace molshredder::render
