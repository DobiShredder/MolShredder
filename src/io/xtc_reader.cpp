#include "molshredder/io/trajectory_reader.hpp"

// The XTC integer decompression algorithm is adapted from xdrfile 1.1.4
// (Copyright 2009-2014 Erik Lindahl & David van der Spoel, BSD-2-Clause)
// and Chemfiles (Copyright 2020 Guillaume Fraux and contributors,
// BSD-3-Clause). See THIRD_PARTY_NOTICES.md for the complete notices.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::io {
namespace {

constexpr std::int32_t kLegacyMagic = 1995;
constexpr std::int32_t kLongMagic = 2023;
constexpr std::size_t kMaxUncompressedAtoms = 9U;
constexpr std::array<std::int32_t, 73> kMagicInts{
    0,       0,       0,       0,       0,       0,       0,       0,
    0,       8,       10,      12,      16,      20,      25,      32,
    40,      50,      64,      80,      101,     128,     161,     203,
    256,     322,     406,     512,     645,     812,     1024,    1290,
    1625,    2048,    2580,    3250,    4096,    5060,    6501,    8192,
    10321,   13003,   16384,   20642,   26007,   32768,   41285,   52015,
    65536,   82570,   104031,  131072,  165140,  208063,  262144,  330280,
    416127,  524287,  660561,  832255,  1048576, 1321122, 1664510, 2097152,
    2642245, 3329021, 4194304, 5284491, 6658042, 8388607, 10568983,
    13316085, 16777216};
constexpr std::uint32_t kFirstMagicIndex = 9U;

struct XtcHeader {
  std::int32_t magic{};
  std::size_t atom_count{};
  std::int32_t step{};
  float time{};
};

operation::Error xtc_error(const std::filesystem::path& path,
                           std::string message) {
  return {operation::ErrorCode::invalid_argument,
          "XTC '" + path.string() + "': " + std::move(message), {}};
}

bool read_exact(std::istream& input, void* destination, std::size_t bytes) {
  if (bytes > static_cast<std::size_t>(
                  std::numeric_limits<std::streamsize>::max())) {
    return false;
  }
  input.read(static_cast<char*>(destination),
             static_cast<std::streamsize>(bytes));
  return input.good();
}

operation::Result<std::uint32_t> read_u32(
    std::istream& input, const std::filesystem::path& path,
    std::string_view context) {
  std::array<unsigned char, 4> bytes{};
  if (!read_exact(input, bytes.data(), bytes.size())) {
    return operation::Result<std::uint32_t>::failure(
        xtc_error(path, "unexpected end while reading " +
                            std::string{context}));
  }
  return operation::Result<std::uint32_t>::success(
      (static_cast<std::uint32_t>(bytes[0]) << 24U) |
      (static_cast<std::uint32_t>(bytes[1]) << 16U) |
      (static_cast<std::uint32_t>(bytes[2]) << 8U) |
      static_cast<std::uint32_t>(bytes[3]));
}

operation::Result<std::uint64_t> read_u64(
    std::istream& input, const std::filesystem::path& path,
    std::string_view context) {
  const auto high = read_u32(input, path, context);
  if (!high.has_value()) {
    return operation::Result<std::uint64_t>::failure(high.error());
  }
  const auto low = read_u32(input, path, context);
  if (!low.has_value()) {
    return operation::Result<std::uint64_t>::failure(low.error());
  }
  return operation::Result<std::uint64_t>::success(
      (static_cast<std::uint64_t>(high.value()) << 32U) | low.value());
}

operation::Result<std::int32_t> read_i32(
    std::istream& input, const std::filesystem::path& path,
    std::string_view context) {
  const auto value = read_u32(input, path, context);
  if (!value.has_value()) {
    return operation::Result<std::int32_t>::failure(value.error());
  }
  return operation::Result<std::int32_t>::success(
      std::bit_cast<std::int32_t>(value.value()));
}

operation::Result<float> read_f32(std::istream& input,
                                  const std::filesystem::path& path,
                                  std::string_view context) {
  const auto value = read_u32(input, path, context);
  if (!value.has_value()) return operation::Result<float>::failure(value.error());
  return operation::Result<float>::success(std::bit_cast<float>(value.value()));
}

operation::Result<XtcHeader> read_header(
    std::istream& input, const std::filesystem::path& path) {
  const auto magic = read_i32(input, path, "magic");
  const auto atom_count = read_i32(input, path, "atom count");
  const auto step = read_i32(input, path, "step");
  const auto time = read_f32(input, path, "time");
  if (!magic.has_value() || !atom_count.has_value() || !step.has_value() ||
      !time.has_value()) {
    return operation::Result<XtcHeader>::failure(
        !magic.has_value()       ? magic.error()
        : !atom_count.has_value() ? atom_count.error()
        : !step.has_value()       ? step.error()
                                  : time.error());
  }
  if (magic.value() != kLegacyMagic && magic.value() != kLongMagic) {
    return operation::Result<XtcHeader>::failure(
        xtc_error(path, "frame magic is neither 1995 nor 2023"));
  }
  if (atom_count.value() <= 0 || !std::isfinite(time.value())) {
    return operation::Result<XtcHeader>::failure(
        xtc_error(path, "atom count or physical time is invalid"));
  }
  return operation::Result<XtcHeader>::success(
      {magic.value(), static_cast<std::size_t>(atom_count.value()),
       step.value(), time.value()});
}

operation::Result<std::optional<model::UnitCell>> read_cell(
    std::istream& input, const std::filesystem::path& path) {
  std::array<double, 9> matrix{};
  for (auto& component : matrix) {
    const auto value = read_f32(input, path, "unit cell");
    if (!value.has_value()) {
      return operation::Result<std::optional<model::UnitCell>>::failure(
          value.error());
    }
    component = static_cast<double>(value.value()) * 10.0;
  }
  if (std::all_of(matrix.begin(), matrix.end(),
                  [](double value) { return value == 0.0; })) {
    return operation::Result<std::optional<model::UnitCell>>::success(
        std::nullopt);
  }
  model::UnitCell cell{{matrix[0], matrix[1], matrix[2]},
                       {matrix[3], matrix[4], matrix[5]},
                       {matrix[6], matrix[7], matrix[8]}};
  if (!cell.is_valid()) {
    return operation::Result<std::optional<model::UnitCell>>::failure(
        xtc_error(path, "unit cell is non-finite or degenerate"));
  }
  return operation::Result<std::optional<model::UnitCell>>::success(cell);
}

std::optional<operation::Error> skip_bytes(
    std::istream& input, std::uint64_t bytes,
    const std::filesystem::path& path, std::string_view context) {
  if (bytes > static_cast<std::uint64_t>(
                  std::numeric_limits<std::streamoff>::max())) {
    return xtc_error(path, std::string{context} + " is too large");
  }
  input.seekg(static_cast<std::streamoff>(bytes), std::ios::cur);
  if (!input.good()) {
    return xtc_error(path, "truncated " + std::string{context});
  }
  return std::nullopt;
}

std::uint32_t bits_for_integer(std::uint32_t size) {
  std::uint32_t bits{};
  std::uint64_t value = 1U;
  while (size >= value && bits < 32U) {
    ++bits;
    value <<= 1U;
  }
  return bits;
}

operation::Result<std::uint32_t> bits_for_sizes(
    const std::array<std::uint32_t, 3>& sizes,
    const std::filesystem::path& path) {
  std::array<std::uint8_t, 32> bytes{};
  std::size_t byte_count = 1U;
  bytes[0] = 1U;
  for (const auto size : sizes) {
    std::uint32_t carry{};
    std::size_t index{};
    for (; index < byte_count; ++index) {
      const auto value = static_cast<std::uint32_t>(bytes[index]) * size + carry;
      bytes[index] = static_cast<std::uint8_t>(value & 0xffU);
      carry = value >> 8U;
    }
    while (carry != 0U) {
      if (index >= bytes.size()) {
        return operation::Result<std::uint32_t>::failure(
            xtc_error(path, "compressed integer range is too large"));
      }
      bytes[index++] = static_cast<std::uint8_t>(carry & 0xffU);
      carry >>= 8U;
    }
    byte_count = index;
  }
  --byte_count;
  std::uint32_t bits{};
  std::uint32_t value = 1U;
  while (bytes[byte_count] >= value) {
    ++bits;
    value <<= 1U;
  }
  return operation::Result<std::uint32_t>::success(
      bits + static_cast<std::uint32_t>(byte_count * 8U));
}

class BitReader {
 public:
  BitReader(const std::vector<unsigned char>& data,
            const std::filesystem::path& path)
      : data_{data}, path_{path} {}

  operation::Result<std::uint32_t> read(std::uint32_t bits) {
    if (bits > 32U || bit_position_ > data_.size() * 8U ||
        bits > data_.size() * 8U - bit_position_) {
      return operation::Result<std::uint32_t>::failure(
          xtc_error(path_, "compressed coordinate bitstream is truncated"));
    }
    std::uint32_t value{};
    for (std::uint32_t index = 0; index < bits; ++index) {
      const auto byte = data_[bit_position_ / 8U];
      const auto shift = 7U - static_cast<unsigned int>(bit_position_ % 8U);
      value = (value << 1U) | ((byte >> shift) & 1U);
      ++bit_position_;
    }
    return operation::Result<std::uint32_t>::success(value);
  }

 private:
  const std::vector<unsigned char>& data_;
  const std::filesystem::path& path_;
  std::size_t bit_position_{};
};

operation::Result<std::array<std::int32_t, 3>> decode_packed_integers(
    BitReader& bits, std::uint32_t bit_count,
    const std::array<std::uint32_t, 3>& sizes,
    const std::filesystem::path& path) {
  if (std::any_of(sizes.begin(), sizes.end(),
                  [](std::uint32_t size) { return size == 0U; })) {
    return operation::Result<std::array<std::int32_t, 3>>::failure(
        xtc_error(path, "compressed integer range contains zero"));
  }
  std::array<std::uint8_t, 32> bytes{};
  std::size_t byte_count{};
  while (bit_count >= 8U) {
    if (byte_count >= bytes.size()) {
      return operation::Result<std::array<std::int32_t, 3>>::failure(
          xtc_error(path, "compressed integer exceeds decoder capacity"));
    }
    const auto value = bits.read(8U);
    if (!value.has_value()) {
      return operation::Result<std::array<std::int32_t, 3>>::failure(
          value.error());
    }
    bytes[byte_count++] = static_cast<std::uint8_t>(value.value());
    bit_count -= 8U;
  }
  if (bit_count != 0U) {
    if (byte_count >= bytes.size()) {
      return operation::Result<std::array<std::int32_t, 3>>::failure(
          xtc_error(path, "compressed integer exceeds decoder capacity"));
    }
    const auto value = bits.read(bit_count);
    if (!value.has_value()) {
      return operation::Result<std::array<std::int32_t, 3>>::failure(
          value.error());
    }
    bytes[byte_count++] = static_cast<std::uint8_t>(value.value());
  }
  std::array<std::int32_t, 3> result{};
  for (std::size_t axis = 2U; axis > 0U; --axis) {
    std::uint32_t remainder{};
    for (std::size_t index = byte_count; index > 0U; --index) {
      const auto current = (remainder << 8U) | bytes[index - 1U];
      bytes[index - 1U] =
          static_cast<std::uint8_t>(current / sizes[axis]);
      remainder = current % sizes[axis];
    }
    result[axis] = static_cast<std::int32_t>(remainder);
  }
  std::uint32_t first{};
  const auto copy_count = std::min<std::size_t>(4U, byte_count);
  for (std::size_t index = 0; index < copy_count; ++index) {
    first |= static_cast<std::uint32_t>(bytes[index]) << (index * 8U);
  }
  result[0] = std::bit_cast<std::int32_t>(first);
  return operation::Result<std::array<std::int32_t, 3>>::success(result);
}

operation::Result<std::vector<model::Vec3f>> decode_compressed(
    std::istream& input, const XtcHeader& header,
    const std::filesystem::path& path, float& precision) {
  const auto precision_value = read_f32(input, path, "compression precision");
  if (!precision_value.has_value() || !(precision_value.value() > 0.0F) ||
      !std::isfinite(precision_value.value())) {
    return operation::Result<std::vector<model::Vec3f>>::failure(
        precision_value.has_value()
            ? xtc_error(path, "compression precision must be finite and positive")
            : precision_value.error());
  }
  precision = precision_value.value();
  std::array<std::int32_t, 3> minimum{};
  std::array<std::int32_t, 3> maximum{};
  for (auto& value : minimum) {
    const auto decoded = read_i32(input, path, "minimum coordinate");
    if (!decoded.has_value()) {
      return operation::Result<std::vector<model::Vec3f>>::failure(decoded.error());
    }
    value = decoded.value();
  }
  for (auto& value : maximum) {
    const auto decoded = read_i32(input, path, "maximum coordinate");
    if (!decoded.has_value()) {
      return operation::Result<std::vector<model::Vec3f>>::failure(decoded.error());
    }
    value = decoded.value();
  }
  std::array<std::uint32_t, 3> ranges{};
  std::array<std::uint32_t, 3> range_bits{};
  bool large_range = false;
  for (std::size_t axis = 0; axis < 3U; ++axis) {
    const auto difference = static_cast<std::int64_t>(maximum[axis]) - minimum[axis];
    if (difference < 0 || difference >= std::numeric_limits<std::uint32_t>::max()) {
      return operation::Result<std::vector<model::Vec3f>>::failure(
          xtc_error(path, "compressed coordinate range is invalid"));
    }
    ranges[axis] = static_cast<std::uint32_t>(difference) + 1U;
    large_range |= ranges[axis] > 0xffffffU;
    range_bits[axis] = bits_for_integer(ranges[axis]);
  }
  std::uint32_t packed_bits{};
  if (!large_range) {
    const auto packed_bits_result = bits_for_sizes(ranges, path);
    if (!packed_bits_result.has_value()) {
      return operation::Result<std::vector<model::Vec3f>>::failure(
          packed_bits_result.error());
    }
    packed_bits = packed_bits_result.value();
  }
  const auto small_index_value = read_u32(input, path, "small-coordinate index");
  if (!small_index_value.has_value()) {
    return operation::Result<std::vector<model::Vec3f>>::failure(
        small_index_value.error());
  }
  auto small_index = small_index_value.value();
  if (small_index < kFirstMagicIndex || small_index >= kMagicInts.size()) {
    return operation::Result<std::vector<model::Vec3f>>::failure(
        xtc_error(path, "small-coordinate index is outside its table"));
  }
  operation::Result<std::uint64_t> data_size =
      header.magic == kLongMagic
          ? read_u64(input, path, "compressed payload size")
          : [&]() -> operation::Result<std::uint64_t> {
              const auto value = read_i32(input, path, "compressed payload size");
              if (!value.has_value()) {
                return operation::Result<std::uint64_t>::failure(value.error());
              }
              if (value.value() < 0) {
                return operation::Result<std::uint64_t>::failure(
                    xtc_error(path, "compressed payload size is negative"));
              }
              return operation::Result<std::uint64_t>::success(
                  static_cast<std::uint64_t>(value.value()));
            }();
  if (!data_size.has_value() ||
      data_size.value() > static_cast<std::uint64_t>(
                              std::numeric_limits<std::size_t>::max())) {
    return operation::Result<std::vector<model::Vec3f>>::failure(
        data_size.has_value() ? xtc_error(path, "compressed payload is too large")
                              : data_size.error());
  }
  std::vector<unsigned char> compressed(
      static_cast<std::size_t>(data_size.value()));
  if (!read_exact(input, compressed.data(), compressed.size())) {
    return operation::Result<std::vector<model::Vec3f>>::failure(
        xtc_error(path, "compressed payload is truncated"));
  }
  const auto padding = (4U - (compressed.size() % 4U)) % 4U;
  if (const auto error = skip_bytes(input, padding, path, "XDR padding");
      error.has_value()) {
    return operation::Result<std::vector<model::Vec3f>>::failure(error.value());
  }

  BitReader bits{compressed, path};
  std::vector<model::Vec3f> positions;
  positions.reserve(header.atom_count);
  std::array<std::int32_t, 3> previous{};
  auto smaller = kMagicInts[std::max(kFirstMagicIndex, small_index - 1U)] / 2;
  auto small_number = kMagicInts[small_index] / 2;
  int run{};
  const auto scale = 10.0F / precision;
  std::size_t decoded_atoms{};
  while (decoded_atoms < header.atom_count) {
    std::array<std::int32_t, 3> current{};
    if (packed_bits == 0U) {
      for (std::size_t axis = 0; axis < 3U; ++axis) {
        const auto value = bits.read(range_bits[axis]);
        if (!value.has_value()) {
          return operation::Result<std::vector<model::Vec3f>>::failure(
              value.error());
        }
        current[axis] = std::bit_cast<std::int32_t>(value.value());
      }
    } else {
      const auto value = decode_packed_integers(bits, packed_bits, ranges, path);
      if (!value.has_value()) {
        return operation::Result<std::vector<model::Vec3f>>::failure(value.error());
      }
      current = value.value();
    }
    for (std::size_t axis = 0; axis < 3U; ++axis) {
      const auto sum = static_cast<std::int64_t>(current[axis]) + minimum[axis];
      if (sum < std::numeric_limits<std::int32_t>::min() ||
          sum > std::numeric_limits<std::int32_t>::max()) {
        return operation::Result<std::vector<model::Vec3f>>::failure(
            xtc_error(path, "decoded coordinate integer overflows"));
      }
      current[axis] = static_cast<std::int32_t>(sum);
    }
    previous = current;
    const auto flag = bits.read(1U);
    if (!flag.has_value()) {
      return operation::Result<std::vector<model::Vec3f>>::failure(flag.error());
    }
    int change{};
    if (flag.value() != 0U) {
      const auto encoded_run = bits.read(5U);
      if (!encoded_run.has_value()) {
        return operation::Result<std::vector<model::Vec3f>>::failure(
            encoded_run.error());
      }
      run = static_cast<int>(encoded_run.value());
      change = run % 3;
      run -= change;
      --change;
    }
    if (run < 0 || run % 3 != 0 ||
        static_cast<std::size_t>(run / 3) > header.atom_count - decoded_atoms - 1U) {
      return operation::Result<std::vector<model::Vec3f>>::failure(
          xtc_error(path, "compressed run exceeds atom count"));
    }
    if (run == 0) {
      positions.push_back({static_cast<float>(current[0]) * scale,
                           static_cast<float>(current[1]) * scale,
                           static_cast<float>(current[2]) * scale});
      ++decoded_atoms;
    } else {
      for (int offset = 0; offset < run; offset += 3) {
        const std::array<std::uint32_t, 3> small_sizes{
            static_cast<std::uint32_t>(kMagicInts[small_index]),
            static_cast<std::uint32_t>(kMagicInts[small_index]),
            static_cast<std::uint32_t>(kMagicInts[small_index])};
        const auto delta = decode_packed_integers(bits, small_index,
                                                   small_sizes, path);
        if (!delta.has_value()) {
          return operation::Result<std::vector<model::Vec3f>>::failure(
              delta.error());
        }
        std::array<std::int32_t, 3> next{};
        for (std::size_t axis = 0; axis < 3U; ++axis) {
          const auto sum = static_cast<std::int64_t>(delta.value()[axis]) +
                           previous[axis] - small_number;
          if (sum < std::numeric_limits<std::int32_t>::min() ||
              sum > std::numeric_limits<std::int32_t>::max()) {
            return operation::Result<std::vector<model::Vec3f>>::failure(
                xtc_error(path, "decoded coordinate delta overflows"));
          }
          next[axis] = static_cast<std::int32_t>(sum);
        }
        if (offset == 0) {
          std::swap(next, previous);
          positions.push_back({static_cast<float>(previous[0]) * scale,
                               static_cast<float>(previous[1]) * scale,
                               static_cast<float>(previous[2]) * scale});
          ++decoded_atoms;
        } else {
          previous = next;
        }
        positions.push_back({static_cast<float>(next[0]) * scale,
                             static_cast<float>(next[1]) * scale,
                             static_cast<float>(next[2]) * scale});
        ++decoded_atoms;
      }
    }
    if (change < 0) {
      if (small_index == kFirstMagicIndex) {
        return operation::Result<std::vector<model::Vec3f>>::failure(
            xtc_error(path, "small-coordinate index underflows"));
      }
      --small_index;
      small_number = smaller;
      smaller = small_index > kFirstMagicIndex
                    ? kMagicInts[small_index - 1U] / 2
                    : 0;
    } else if (change > 0) {
      if (small_index + 1U >= kMagicInts.size()) {
        return operation::Result<std::vector<model::Vec3f>>::failure(
            xtc_error(path, "small-coordinate index overflows"));
      }
      ++small_index;
      smaller = small_number;
      small_number = kMagicInts[small_index] / 2;
    }
  }
  if (!std::all_of(positions.begin(), positions.end(), [](const auto& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z);
      })) {
    return operation::Result<std::vector<model::Vec3f>>::failure(
        xtc_error(path, "decoded coordinates are non-finite"));
  }
  return operation::Result<std::vector<model::Vec3f>>::success(
      std::move(positions));
}

operation::Result<std::uint64_t> frame_payload_bytes(
    std::istream& input, const XtcHeader& header,
    const std::filesystem::path& path) {
  constexpr std::uint64_t box_and_repeat = 40U;
  if (header.atom_count <= kMaxUncompressedAtoms) {
    const auto coordinate_bytes =
        static_cast<std::uint64_t>(header.atom_count) * 12U;
    return operation::Result<std::uint64_t>::success(box_and_repeat +
                                                     coordinate_bytes);
  }
  if (const auto error = skip_bytes(input, 40U + 32U, path,
                                    "box/repeated count/compression header");
      error.has_value()) {
    return operation::Result<std::uint64_t>::failure(error.value());
  }
  operation::Result<std::uint64_t> size =
      header.magic == kLongMagic
          ? read_u64(input, path, "compressed payload size")
          : [&]() -> operation::Result<std::uint64_t> {
              const auto value = read_i32(input, path, "compressed payload size");
              if (!value.has_value()) {
                return operation::Result<std::uint64_t>::failure(value.error());
              }
              if (value.value() < 0) {
                return operation::Result<std::uint64_t>::failure(
                    xtc_error(path, "compressed payload size is negative"));
              }
              return operation::Result<std::uint64_t>::success(
                  static_cast<std::uint64_t>(value.value()));
            }();
  if (!size.has_value()) return size;
  const auto prefix = header.magic == kLongMagic ? 80U : 76U;
  const auto padding = (4U - (size.value() % 4U)) % 4U;
  if (size.value() > std::numeric_limits<std::uint64_t>::max() - prefix - padding) {
    return operation::Result<std::uint64_t>::failure(
        xtc_error(path, "frame payload size overflows"));
  }
  return operation::Result<std::uint64_t>::success(prefix + size.value() +
                                                   padding);
}

}  // namespace

operation::Result<std::shared_ptr<const XtcCoordinateSource>> open_xtc(
    const std::filesystem::path& path,
    std::optional<std::size_t> expected_atom_count) {
  std::ifstream input{path, std::ios::binary};
  if (!input) {
    return operation::Result<std::shared_ptr<const XtcCoordinateSource>>::failure(
        {operation::ErrorCode::not_found,
         "cannot open XTC file: " + path.string(),
         "check that the path exists and is readable"});
  }
  std::error_code file_error;
  const auto file_size = std::filesystem::file_size(path, file_error);
  if (file_error) {
    return operation::Result<std::shared_ptr<const XtcCoordinateSource>>::failure(
        xtc_error(path, "cannot determine file size"));
  }
  std::vector<std::uint64_t> offsets;
  std::optional<std::size_t> atom_count;
  std::optional<std::int32_t> first_magic;
  bool mixed_variant = false;
  bool compressed = false;
  while (true) {
    const auto position = input.tellg();
    if (position < 0) {
      return operation::Result<std::shared_ptr<const XtcCoordinateSource>>::failure(
          xtc_error(path, "failed to index frame position"));
    }
    if (static_cast<std::uint64_t>(position) == file_size) break;
    const auto header = read_header(input, path);
    if (!header.has_value()) {
      return operation::Result<std::shared_ptr<const XtcCoordinateSource>>::failure(
          header.error());
    }
    if (!atom_count.has_value()) atom_count = header.value().atom_count;
    if (atom_count.value() != header.value().atom_count) {
      return operation::Result<std::shared_ptr<const XtcCoordinateSource>>::failure(
          xtc_error(path, "atom count changes between frames"));
    }
    if (!first_magic.has_value()) first_magic = header.value().magic;
    mixed_variant |= first_magic.value() != header.value().magic;
    compressed |= header.value().atom_count > kMaxUncompressedAtoms;
    offsets.push_back(static_cast<std::uint64_t>(position));
    const auto payload = frame_payload_bytes(input, header.value(), path);
    if (!payload.has_value()) {
      return operation::Result<std::shared_ptr<const XtcCoordinateSource>>::failure(
          payload.error());
    }
    input.seekg(position);
    if (const auto error = skip_bytes(input, 16U + payload.value(), path,
                                      "frame payload");
        error.has_value()) {
      return operation::Result<std::shared_ptr<const XtcCoordinateSource>>::failure(
          error.value());
    }
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > file_size) {
      return operation::Result<std::shared_ptr<const XtcCoordinateSource>>::failure(
          xtc_error(path, "frame extends beyond file size"));
    }
  }
  if (offsets.empty() || !atom_count.has_value()) {
    return operation::Result<std::shared_ptr<const XtcCoordinateSource>>::failure(
        xtc_error(path, "file contains no frames"));
  }
  if (expected_atom_count.has_value() &&
      expected_atom_count.value() != atom_count.value()) {
    return operation::Result<std::shared_ptr<const XtcCoordinateSource>>::failure(
        xtc_error(path, "atom count " + std::to_string(atom_count.value()) +
                            " does not match topology atom count " +
                            std::to_string(expected_atom_count.value())));
  }
  const auto variant = mixed_variant
                           ? XtcVariant::mixed
                           : (first_magic.value() == kLongMagic
                                  ? XtcVariant::long_2023
                                  : XtcVariant::legacy_1995);
  XtcMetadata metadata{atom_count.value(), offsets.size(), compressed, variant};
  auto source = std::shared_ptr<const XtcCoordinateSource>(
      new XtcCoordinateSource(path, metadata, std::move(offsets)));
  return operation::Result<std::shared_ptr<const XtcCoordinateSource>>::success(
      std::move(source));
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
XtcCoordinateSource::read_frame(std::size_t frame_index) const {
  if (frame_index >= frame_offsets_.size()) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        {operation::ErrorCode::not_found,
         "XTC frame index is out of range: " + std::to_string(frame_index),
         "request an index smaller than the frame count"});
  }
  std::ifstream input{path_, std::ios::binary};
  if (!input) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        {operation::ErrorCode::not_found,
         "cannot reopen XTC file: " + path_.string(), {}});
  }
  input.seekg(static_cast<std::streamoff>(frame_offsets_[frame_index]));
  const auto header = read_header(input, path_);
  if (!header.has_value()) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        header.error());
  }
  const auto cell = read_cell(input, path_);
  if (!cell.has_value()) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        cell.error());
  }
  const auto repeated_count = read_i32(input, path_, "repeated atom count");
  if (!repeated_count.has_value() || repeated_count.value() < 0 ||
      static_cast<std::size_t>(repeated_count.value()) != header.value().atom_count) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        repeated_count.has_value()
            ? xtc_error(path_, "repeated atom count does not match header")
            : repeated_count.error());
  }
  std::vector<model::Vec3f> positions;
  std::optional<float> precision;
  if (header.value().atom_count <= kMaxUncompressedAtoms) {
    positions.resize(header.value().atom_count);
    for (auto& position : positions) {
      std::array<float, 3> components{};
      for (auto& component : components) {
        const auto value = read_f32(input, path_, "uncompressed coordinate");
        if (!value.has_value() || !std::isfinite(value.value())) {
          return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
              value.has_value() ? xtc_error(path_, "coordinate is non-finite")
                                : value.error());
        }
        component = value.value() * 10.0F;
      }
      position = {components[0], components[1], components[2]};
    }
  } else {
    float decoded_precision{};
    const auto decoded = decode_compressed(input, header.value(), path_,
                                            decoded_precision);
    if (!decoded.has_value()) {
      return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          decoded.error());
    }
    positions = decoded.value();
    precision = decoded_precision;
  }
  model::FrameMetadata metadata;
  metadata.coordinate_unit = operation::LengthUnit::angstrom;
  metadata.physical_time =
      model::PhysicalTime{header.value().time, model::TimeUnit::picosecond};
  if (header.value().step >= 0) {
    metadata.source_step = static_cast<std::uint64_t>(header.value().step);
  }
  metadata.fields.emplace("xtc.signed_step", std::to_string(header.value().step));
  metadata.fields.emplace("xtc.magic", std::to_string(header.value().magic));
  if (precision.has_value()) {
    metadata.fields.emplace("xtc.precision", std::to_string(precision.value()));
  }
  metadata.unit_cell = cell.value();
  return model::CoordinateFrame::create(
      model::CoordinateBuffer{std::move(positions)}, std::nullopt, {},
      std::move(metadata));
}

}  // namespace molshredder::io
