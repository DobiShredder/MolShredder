#include "molshredder/io/trajectory_reader.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace molshredder::io {
namespace {

using operation::Result;

operation::Error invalid(const std::filesystem::path &path, std::string message,
                         std::string suggestion = {}) {
  return {operation::ErrorCode::invalid_argument,
          "BINPOS trajectory '" + path.string() + "': " +
              std::move(message),
          std::move(suggestion)};
}

std::uint32_t unsigned32(const unsigned char *bytes, ByteOrder order) {
  if (order == ByteOrder::little_endian) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
  }
  return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
         (static_cast<std::uint32_t>(bytes[1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[2]) << 8U) |
         static_cast<std::uint32_t>(bytes[3]);
}

std::int32_t signed32(const unsigned char *bytes, ByteOrder order) {
  return std::bit_cast<std::int32_t>(unsigned32(bytes, order));
}

Result<std::array<unsigned char, 4U>> read_word(
    std::istream &input, const std::filesystem::path &path,
    std::string_view purpose) {
  std::array<unsigned char, 4U> bytes{};
  input.read(reinterpret_cast<char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
    return Result<std::array<unsigned char, 4U>>::failure(
        invalid(path, std::string{purpose} + " is truncated"));
  }
  return Result<std::array<unsigned char, 4U>>::success(bytes);
}

std::string_view byte_order_name(ByteOrder order) {
  return order == ByteOrder::little_endian ? "little-endian" : "big-endian";
}

} // namespace

operation::Result<std::shared_ptr<const BinposCoordinateSource>>
open_binpos(const std::filesystem::path &path,
            std::size_t expected_atom_count) {
  if (expected_atom_count == 0U ||
      expected_atom_count >
          static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) ||
      expected_atom_count >
          (std::numeric_limits<std::size_t>::max() - 4U) / 12U) {
    return Result<std::shared_ptr<const BinposCoordinateSource>>::failure(
        invalid(path,
                "active topology atom count must be positive and addressable"));
  }
  std::ifstream input{path, std::ios::binary};
  if (!input) {
    return Result<std::shared_ptr<const BinposCoordinateSource>>::failure(
        {operation::ErrorCode::not_found,
         "cannot open BINPOS trajectory file: " + path.string(), {}});
  }
  std::array<char, 4U> magic{};
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  if (input.gcount() != static_cast<std::streamsize>(magic.size()) ||
      magic != std::array<char, 4U>{'f', 'x', 'y', 'z'}) {
    return Result<std::shared_ptr<const BinposCoordinateSource>>::failure(
        invalid(path, "file does not begin with the fxyz magic header"));
  }
  auto first_count = read_word(input, path, "first frame atom count");
  if (!first_count.has_value()) {
    return Result<std::shared_ptr<const BinposCoordinateSource>>::failure(
        first_count.error());
  }
  const auto expected = static_cast<std::int32_t>(expected_atom_count);
  const bool little_matches =
      signed32(first_count.value().data(), ByteOrder::little_endian) == expected;
  const bool big_matches =
      signed32(first_count.value().data(), ByteOrder::big_endian) == expected;
  if (!little_matches && !big_matches) {
    return Result<std::shared_ptr<const BinposCoordinateSource>>::failure(
        invalid(path, "first frame atom count does not match the active "
                      "topology in either byte order"));
  }
  if (little_matches && big_matches) {
    return Result<std::shared_ptr<const BinposCoordinateSource>>::failure(
        {operation::ErrorCode::unsupported,
         "BINPOS atom count has byte-order-ambiguous binary representation",
         "convert the trajectory to NetCDF, DCD, XTC or TRR"});
  }
  const auto order = little_matches ? ByteOrder::little_endian
                                    : ByteOrder::big_endian;

  input.clear();
  input.seekg(0, std::ios::end);
  const auto end = input.tellg();
  if (end < std::streampos{}) {
    return Result<std::shared_ptr<const BinposCoordinateSource>>::failure(
        invalid(path, "could not determine file size"));
  }
  const auto file_size = static_cast<std::uint64_t>(end);
  const auto coordinate_bytes = static_cast<std::uint64_t>(
      expected_atom_count * 3U * sizeof(float));
  const auto record_size = 4U + coordinate_bytes;
  if (file_size <= 4U || (file_size - 4U) % record_size != 0U) {
    return Result<std::shared_ptr<const BinposCoordinateSource>>::failure(
        invalid(path, "payload size is not an exact sequence of atom-count "
                      "and 3*N float32 frame records"));
  }
  const auto frame_count = (file_size - 4U) / record_size;
  if (frame_count == 0U ||
      frame_count >
          static_cast<std::uint64_t>(
              std::numeric_limits<std::size_t>::max())) {
    return Result<std::shared_ptr<const BinposCoordinateSource>>::failure(
        invalid(path, "frame count is empty or not addressable"));
  }

  std::vector<std::uint64_t> offsets;
  offsets.reserve(static_cast<std::size_t>(frame_count));
  input.clear();
  input.seekg(4, std::ios::beg);
  for (std::uint64_t frame = 0U; frame < frame_count; ++frame) {
    const auto offset = 4U + frame * record_size;
    offsets.push_back(offset);
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    auto count = read_word(input, path, "frame atom count");
    if (!count.has_value()) {
      return Result<std::shared_ptr<const BinposCoordinateSource>>::failure(
          count.error());
    }
    if (signed32(count.value().data(), order) != expected) {
      return Result<std::shared_ptr<const BinposCoordinateSource>>::failure(
          invalid(path, "frame " + std::to_string(frame) +
                            " atom count differs from the active topology"));
    }
  }
  BinposMetadata metadata{expected_atom_count, offsets.size(), order};
  return Result<std::shared_ptr<const BinposCoordinateSource>>::success(
      std::shared_ptr<const BinposCoordinateSource>{new BinposCoordinateSource{
          path, metadata, std::move(offsets)}});
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
BinposCoordinateSource::read_frame(std::size_t frame_index) const {
  if (frame_index >= frame_offsets_.size()) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        invalid(path_, "frame index " + std::to_string(frame_index) +
                           " is outside the available range"));
  }
  std::ifstream input{path_, std::ios::binary};
  if (!input) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        {operation::ErrorCode::not_found,
         "cannot reopen BINPOS trajectory file: " + path_.string(), {}});
  }
  input.seekg(static_cast<std::streamoff>(frame_offsets_[frame_index]),
              std::ios::beg);
  if (!input) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        invalid(path_, "could not seek to indexed frame"));
  }
  auto count = read_word(input, path_, "frame atom count");
  if (!count.has_value()) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        count.error());
  }
  if (signed32(count.value().data(), metadata_.byte_order) !=
      static_cast<std::int32_t>(metadata_.atom_count)) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        invalid(path_, "indexed frame atom count changed after open"));
  }
  const auto value_count = metadata_.atom_count * 3U;
  std::vector<unsigned char> payload(value_count * sizeof(float));
  input.read(reinterpret_cast<char *>(payload.data()),
             static_cast<std::streamsize>(payload.size()));
  if (input.gcount() != static_cast<std::streamsize>(payload.size())) {
    return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        invalid(path_, "coordinate payload is truncated"));
  }
  std::vector<model::Vec3f> positions(metadata_.atom_count);
  for (std::size_t index = 0U; index < value_count; ++index) {
    const auto bits =
        unsigned32(payload.data() + index * sizeof(float), metadata_.byte_order);
    const auto value = std::bit_cast<float>(bits);
    if (!std::isfinite(value)) {
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          invalid(path_, "frame " + std::to_string(frame_index) +
                              " contains a non-finite coordinate"));
    }
    auto &position = positions[index / 3U];
    if (index % 3U == 0U)
      position.x = value;
    else if (index % 3U == 1U)
      position.y = value;
    else
      position.z = value;
  }
  model::FrameMetadata frame_metadata;
  frame_metadata.coordinate_unit = operation::LengthUnit::angstrom;
  frame_metadata.fields["format"] = "binpos";
  frame_metadata.fields["byte_order"] =
      std::string{byte_order_name(metadata_.byte_order)};
  frame_metadata.fields["coordinate_unit_source"] =
      "scripps-binpos-cpptraj-contract";
  return model::CoordinateFrame::create(
      model::CoordinateBuffer{std::move(positions)}, std::nullopt, {},
      std::move(frame_metadata));
}

} // namespace molshredder::io
