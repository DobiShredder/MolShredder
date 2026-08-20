#include "molshredder/io/trajectory_reader.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numbers>
#include <string>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::io {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

operation::Error read_failure(const std::filesystem::path& path,
                              std::string message) {
  return invalid("DCD '" + path.string() + "': " + std::move(message));
}

std::uint32_t decode_u32(const std::array<unsigned char, 4>& bytes,
                         ByteOrder order) {
  if (order == ByteOrder::little_endian) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
  }
  return static_cast<std::uint32_t>(bytes[3]) |
         (static_cast<std::uint32_t>(bytes[2]) << 8U) |
         (static_cast<std::uint32_t>(bytes[1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[0]) << 24U);
}

std::int32_t decode_i32(const unsigned char* bytes, ByteOrder order) {
  std::array<unsigned char, 4> copy{};
  std::memcpy(copy.data(), bytes, copy.size());
  return std::bit_cast<std::int32_t>(decode_u32(copy, order));
}

float decode_f32(const unsigned char* bytes, ByteOrder order) {
  std::array<unsigned char, 4> copy{};
  std::memcpy(copy.data(), bytes, copy.size());
  return std::bit_cast<float>(decode_u32(copy, order));
}

double decode_f64(const unsigned char* bytes, ByteOrder order) {
  std::uint64_t bits{};
  if (order == ByteOrder::little_endian) {
    for (unsigned int index = 0; index < 8U; ++index) {
      bits |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
  } else {
    for (unsigned int index = 0; index < 8U; ++index) {
      bits |= static_cast<std::uint64_t>(bytes[7U - index]) << (index * 8U);
    }
  }
  return std::bit_cast<double>(bits);
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

operation::Result<std::uint32_t> read_marker(
    std::istream& input, ByteOrder order, const std::filesystem::path& path,
    std::string_view context) {
  std::array<unsigned char, 4> bytes{};
  if (!read_exact(input, bytes.data(), bytes.size())) {
    return operation::Result<std::uint32_t>::failure(
        read_failure(path, "unexpected end while reading " +
                               std::string{context}));
  }
  return operation::Result<std::uint32_t>::success(decode_u32(bytes, order));
}

std::optional<operation::Error> skip_record(
    std::istream& input, ByteOrder order, std::uint32_t expected_size,
    const std::filesystem::path& path, std::string_view context) {
  const auto leading = read_marker(input, order, path, context);
  if (!leading.has_value()) return leading.error();
  if (leading.value() != expected_size) {
    return read_failure(path, std::string{context} + " record size is " +
                                  std::to_string(leading.value()) +
                                  ", expected " +
                                  std::to_string(expected_size));
  }
  input.seekg(static_cast<std::streamoff>(expected_size), std::ios::cur);
  if (!input.good()) {
    return read_failure(path, "truncated " + std::string{context} +
                                  " payload");
  }
  const auto trailing = read_marker(input, order, path, context);
  if (!trailing.has_value()) return trailing.error();
  if (trailing.value() != expected_size) {
    return read_failure(path, std::string{context} +
                                  " trailing record marker mismatch");
  }
  return std::nullopt;
}

operation::Result<std::vector<unsigned char>> read_record(
    std::istream& input, ByteOrder order, std::uint32_t expected_size,
    const std::filesystem::path& path, std::string_view context) {
  const auto leading = read_marker(input, order, path, context);
  if (!leading.has_value()) {
    return operation::Result<std::vector<unsigned char>>::failure(
        leading.error());
  }
  if (leading.value() != expected_size) {
    return operation::Result<std::vector<unsigned char>>::failure(
        read_failure(path, std::string{context} + " record size mismatch"));
  }
  std::vector<unsigned char> payload(expected_size);
  if (!read_exact(input, payload.data(), payload.size())) {
    return operation::Result<std::vector<unsigned char>>::failure(
        read_failure(path, "truncated " + std::string{context} +
                               " payload"));
  }
  const auto trailing = read_marker(input, order, path, context);
  if (!trailing.has_value()) {
    return operation::Result<std::vector<unsigned char>>::failure(
        trailing.error());
  }
  if (trailing.value() != expected_size) {
    return operation::Result<std::vector<unsigned char>>::failure(
        read_failure(path, std::string{context} +
                               " trailing record marker mismatch"));
  }
  return operation::Result<std::vector<unsigned char>>::success(
      std::move(payload));
}

operation::Result<model::UnitCell> decode_cell(
    const std::vector<unsigned char>& payload, ByteOrder order,
    const std::filesystem::path& path) {
  std::array<double, 6> values{};
  for (std::size_t index = 0; index < values.size(); ++index) {
    values[index] = decode_f64(payload.data() + index * 8U, order);
    if (!std::isfinite(values[index])) {
      return operation::Result<model::UnitCell>::failure(
          read_failure(path, "unit-cell record contains a non-finite value"));
    }
  }
  const double length_a = values[0];
  const double length_b = values[2];
  const double length_c = values[5];
  double gamma = values[1];
  double beta = values[3];
  double alpha = values[4];
  if (std::abs(alpha) <= 1.0 && std::abs(beta) <= 1.0 &&
      std::abs(gamma) <= 1.0) {
    alpha = std::acos(alpha) * 180.0 / std::numbers::pi;
    beta = std::acos(beta) * 180.0 / std::numbers::pi;
    gamma = std::acos(gamma) * 180.0 / std::numbers::pi;
  }
  if (!(length_a > 0.0 && length_b > 0.0 && length_c > 0.0 &&
        alpha > 0.0 && alpha < 180.0 && beta > 0.0 && beta < 180.0 &&
        gamma > 0.0 && gamma < 180.0)) {
    return operation::Result<model::UnitCell>::failure(
        read_failure(path, "unit-cell lengths or angles are invalid"));
  }
  const auto radians = [](double degrees) {
    return degrees * std::numbers::pi / 180.0;
  };
  const auto cos_alpha = std::cos(radians(alpha));
  const auto cos_beta = std::cos(radians(beta));
  const auto cos_gamma = std::cos(radians(gamma));
  const auto sin_gamma = std::sin(radians(gamma));
  if (std::abs(sin_gamma) <= 1.0e-12) {
    return operation::Result<model::UnitCell>::failure(
        read_failure(path, "unit-cell gamma angle is degenerate"));
  }
  const auto c_x = length_c * cos_beta;
  const auto c_y = length_c *
                   (cos_alpha - cos_beta * cos_gamma) / sin_gamma;
  const auto c_z_squared = length_c * length_c - c_x * c_x - c_y * c_y;
  if (!(c_z_squared > 0.0)) {
    return operation::Result<model::UnitCell>::failure(
        read_failure(path, "unit-cell angles do not form a valid cell"));
  }
  model::UnitCell cell{{length_a, 0.0, 0.0},
                       {length_b * cos_gamma, length_b * sin_gamma, 0.0},
                       {c_x, c_y, std::sqrt(c_z_squared)}};
  if (!cell.is_valid()) {
    return operation::Result<model::UnitCell>::failure(
        read_failure(path, "decoded unit cell is invalid"));
  }
  return operation::Result<model::UnitCell>::success(cell);
}

}  // namespace

operation::Result<std::shared_ptr<const DcdCoordinateSource>> open_dcd(
    const std::filesystem::path& path,
    std::optional<std::size_t> expected_atom_count) {
  std::ifstream input{path, std::ios::binary};
  if (!input) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "cannot open DCD file: " + path.string(),
                         "check that the path exists and is readable"});
  }
  std::array<unsigned char, 4> first_marker{};
  if (!read_exact(input, first_marker.data(), first_marker.size())) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        read_failure(path, "file is too short for a DCD header"));
  }
  ByteOrder order;
  if (decode_u32(first_marker, ByteOrder::little_endian) == 84U) {
    order = ByteOrder::little_endian;
  } else if (decode_u32(first_marker, ByteOrder::big_endian) == 84U) {
    order = ByteOrder::big_endian;
  } else {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        read_failure(path,
                     "unsupported record marker; expected a 32-bit DCD header"));
  }
  std::array<unsigned char, 84> header{};
  if (!read_exact(input, header.data(), header.size())) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        read_failure(path, "truncated DCD header"));
  }
  if (std::memcmp(header.data(), "CORD", 4U) != 0) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        read_failure(path, "header magic is not CORD"));
  }
  const auto header_end = read_marker(input, order, path, "header");
  if (!header_end.has_value() || header_end.value() != 84U) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        header_end.has_value()
            ? read_failure(path, "header trailing record marker mismatch")
            : header_end.error());
  }
  const auto frame_count_value = decode_i32(header.data() + 4U, order);
  const auto start_step = decode_i32(header.data() + 8U, order);
  const auto save_interval = decode_i32(header.data() + 12U, order);
  const auto fixed_atoms = decode_i32(header.data() + 36U, order);
  const auto charmm_version = decode_i32(header.data() + 80U, order);
  const auto dialect = charmm_version == 0 ? DcdDialect::xplor
                                           : DcdDialect::charmm;
  const auto raw_delta = dialect == DcdDialect::charmm
                             ? static_cast<double>(
                                   decode_f32(header.data() + 40U, order))
                             : decode_f64(header.data() + 40U, order);
  const auto has_unit_cell =
      dialect == DcdDialect::charmm &&
      decode_i32(header.data() + 44U, order) != 0;
  const auto has_four_dimensions =
      dialect == DcdDialect::charmm &&
      decode_i32(header.data() + 48U, order) != 0;
  if (frame_count_value < 0 || save_interval <= 0 || fixed_atoms < 0) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        read_failure(path, "header contains invalid frame/timestep counts"));
  }
  if (has_four_dimensions) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        read_failure(path, "four-dimensional DCD trajectories are unsupported"));
  }

  const auto title_size = read_marker(input, order, path, "title");
  if (!title_size.has_value() || title_size.value() < 4U ||
      (title_size.value() - 4U) % 80U != 0U) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        title_size.has_value() ? read_failure(path, "invalid title record size")
                               : title_size.error());
  }
  std::vector<unsigned char> title_payload(title_size.value());
  if (!read_exact(input, title_payload.data(), title_payload.size())) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        read_failure(path, "truncated title record"));
  }
  const auto title_count = decode_i32(title_payload.data(), order);
  if (title_count < 0 ||
      static_cast<std::uint32_t>(title_count) !=
          (title_size.value() - 4U) / 80U) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        read_failure(path, "title count does not match its record size"));
  }
  const auto title_end = read_marker(input, order, path, "title");
  if (!title_end.has_value() || title_end.value() != title_size.value()) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        title_end.has_value()
            ? read_failure(path, "title trailing record marker mismatch")
            : title_end.error());
  }
  std::string title;
  if (title_count > 0) {
    title.assign(reinterpret_cast<const char*>(title_payload.data() + 4U), 80U);
    while (!title.empty() && (title.back() == ' ' || title.back() == '\0')) {
      title.pop_back();
    }
  }

  const auto atom_record = read_record(input, order, 4U, path, "atom count");
  if (!atom_record.has_value()) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        atom_record.error());
  }
  const auto atom_count_value = decode_i32(atom_record.value().data(), order);
  if (atom_count_value <= 0 ||
      static_cast<std::uint64_t>(atom_count_value) >
          std::numeric_limits<std::uint32_t>::max() / 4U) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        read_failure(path, "atom count is invalid or too large"));
  }
  const auto atom_count = static_cast<std::size_t>(atom_count_value);
  if (expected_atom_count.has_value() &&
      expected_atom_count.value() != atom_count) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        read_failure(path, "atom count " + std::to_string(atom_count) +
                               " does not match topology atom count " +
                               std::to_string(expected_atom_count.value())));
  }

  if (fixed_atoms >= atom_count_value) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        read_failure(path,
                     "fixed-atom count must be smaller than atom count"));
  }
  std::vector<std::size_t> free_atom_indices;
  if (fixed_atoms > 0) {
    const auto free_count = atom_count - static_cast<std::size_t>(fixed_atoms);
    const auto free_record = read_record(
        input, order, static_cast<std::uint32_t>(free_count * 4U), path,
        "free atom indices");
    if (!free_record.has_value()) {
      return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
          free_record.error());
    }
    std::vector<bool> seen(atom_count, false);
    free_atom_indices.reserve(free_count);
    for (std::size_t index = 0; index < free_count; ++index) {
      const auto source_index =
          decode_i32(free_record.value().data() + index * 4U, order);
      if (source_index <= 0 ||
          source_index > static_cast<std::int32_t>(atom_count)) {
        return operation::Result<
            std::shared_ptr<const DcdCoordinateSource>>::failure(
            read_failure(path, "free atom index is outside atom count"));
      }
      const auto zero_based = static_cast<std::size_t>(source_index - 1);
      if (seen[zero_based]) {
        return operation::Result<
            std::shared_ptr<const DcdCoordinateSource>>::failure(
            read_failure(path, "free atom indices contain a duplicate"));
      }
      seen[zero_based] = true;
      free_atom_indices.push_back(zero_based);
    }
  }

  std::vector<std::uint64_t> offsets;
  while (input.peek() != std::char_traits<char>::eof()) {
    const auto position = input.tellg();
    if (position < 0) {
      return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
          read_failure(path, "failed to index a frame offset"));
    }
    const auto frame_atom_count =
        fixed_atoms > 0 && !offsets.empty() ? free_atom_indices.size()
                                             : atom_count;
    const auto coordinate_bytes =
        static_cast<std::uint32_t>(frame_atom_count * 4U);
    offsets.push_back(static_cast<std::uint64_t>(position));
    if (has_unit_cell) {
      if (const auto error =
              skip_record(input, order, 48U, path, "unit cell");
          error.has_value()) {
        return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
            error.value());
      }
    }
    for (const auto axis : {"X coordinate", "Y coordinate", "Z coordinate"}) {
      if (const auto error =
              skip_record(input, order, coordinate_bytes, path, axis);
          error.has_value()) {
        return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
            error.value());
      }
    }
  }
  if (offsets.size() != static_cast<std::size_t>(frame_count_value)) {
    return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::failure(
        read_failure(path, "indexed frame count " +
                               std::to_string(offsets.size()) +
                               " does not match header count " +
                               std::to_string(frame_count_value)));
  }
  DcdMetadata metadata{atom_count,
                       offsets.size(),
                       start_step,
                       save_interval,
                       raw_delta,
                       static_cast<std::size_t>(fixed_atoms),
                       has_unit_cell,
                       order,
                       dialect,
                       std::move(title)};
  auto source = std::shared_ptr<const DcdCoordinateSource>(
      new DcdCoordinateSource(path, std::move(metadata), std::move(offsets),
                              std::move(free_atom_indices)));
  return operation::Result<std::shared_ptr<const DcdCoordinateSource>>::success(
      std::move(source));
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
DcdCoordinateSource::read_frame(std::size_t frame_index) const {
  if (frame_index >= frame_offsets_.size()) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "DCD frame index is out of range: " +
                             std::to_string(frame_index),
                         "request an index smaller than the frame count"});
  }
  std::ifstream input{path_, std::ios::binary};
  if (!input) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "cannot reopen DCD file: " + path_.string(), {}});
  }
  if (frame_offsets_[frame_index] >
      static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        read_failure(path_, "frame offset is outside stream range"));
  }
  input.seekg(static_cast<std::streamoff>(frame_offsets_[frame_index]));
  if (!input.good()) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        read_failure(path_, "cannot seek to requested frame"));
  }
  model::FrameMetadata frame_metadata;
  frame_metadata.coordinate_unit = operation::LengthUnit::angstrom;
  const auto step = static_cast<std::int64_t>(metadata_.start_step) +
                    static_cast<std::int64_t>(metadata_.save_interval) *
                        static_cast<std::int64_t>(frame_index);
  if (step >= 0) frame_metadata.source_step = static_cast<std::uint64_t>(step);
  frame_metadata.fields.emplace("dcd.signed_step", std::to_string(step));
  frame_metadata.fields.emplace("dcd.raw_delta",
                                std::to_string(metadata_.raw_delta));
  frame_metadata.fields.emplace("dcd.title", metadata_.title);
  frame_metadata.fields.emplace("dcd.fixed_atom_count",
                                std::to_string(metadata_.fixed_atom_count));
  if (metadata_.has_unit_cell) {
    const auto cell_record =
        read_record(input, metadata_.byte_order, 48U, path_, "unit cell");
    if (!cell_record.has_value()) {
      return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          cell_record.error());
    }
    const auto cell = decode_cell(cell_record.value(), metadata_.byte_order,
                                  path_);
    if (!cell.has_value()) {
      return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          cell.error());
    }
    frame_metadata.unit_cell = cell.value();
  }
  const auto frame_atom_count =
      frame_index > 0U && metadata_.fixed_atom_count > 0U
          ? free_atom_indices_.size()
          : metadata_.atom_count;
  const auto coordinate_bytes =
      static_cast<std::uint32_t>(frame_atom_count * 4U);
  std::array<std::vector<unsigned char>, 3> axes;
  for (std::size_t axis = 0; axis < axes.size(); ++axis) {
    const auto record = read_record(input, metadata_.byte_order,
                                    coordinate_bytes, path_, "coordinate");
    if (!record.has_value()) {
      return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          record.error());
    }
    axes[axis] = record.value();
  }
  std::vector<model::Vec3f> positions(metadata_.atom_count);
  if (frame_index > 0U && metadata_.fixed_atom_count > 0U) {
    const auto first = read_frame(0U);
    if (!first.has_value()) {
      return operation::Result<
          std::shared_ptr<const model::CoordinateFrame>>::failure(
          first.error());
    }
    const auto *first_positions =
        std::get_if<std::vector<model::Vec3f>>(
            &first.value()->positions().values());
    if (first_positions == nullptr) {
      return operation::Result<
          std::shared_ptr<const model::CoordinateFrame>>::failure(
          read_failure(path_, "fixed-atom reference frame precision drift"));
    }
    positions = *first_positions;
  }
  for (std::size_t item = 0; item < frame_atom_count; ++item) {
    const auto atom = frame_index > 0U && metadata_.fixed_atom_count > 0U
                          ? free_atom_indices_[item]
                          : item;
    positions[atom] = {decode_f32(axes[0].data() + item * 4U,
                                  metadata_.byte_order),
                       decode_f32(axes[1].data() + item * 4U,
                                  metadata_.byte_order),
                       decode_f32(axes[2].data() + item * 4U,
                                  metadata_.byte_order)};
  }
  return model::CoordinateFrame::create(
      model::CoordinateBuffer{std::move(positions)}, std::nullopt, {},
      std::move(frame_metadata));
}

}  // namespace molshredder::io
