#include "molshredder/io/trajectory_reader.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::io {
namespace {

constexpr std::int32_t kTrrMagic = 1993;
constexpr std::string_view kTrrVersion = "GMX_trn_file";

struct TrrHeader {
  bool use_double{};
  std::array<std::uint32_t, 10> sizes{};
  std::size_t atom_count{};
  std::int32_t step{};
  std::int32_t nre{};
  double time{};
  double lambda{};
  std::uint64_t payload_bytes{};
};

operation::Error trr_error(const std::filesystem::path& path,
                           std::string message) {
  return {operation::ErrorCode::invalid_argument,
          "TRR '" + path.string() + "': " + std::move(message), {}};
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
        trr_error(path, "unexpected end while reading " +
                            std::string{context}));
  }
  const auto value = (static_cast<std::uint32_t>(bytes[0]) << 24U) |
                     (static_cast<std::uint32_t>(bytes[1]) << 16U) |
                     (static_cast<std::uint32_t>(bytes[2]) << 8U) |
                     static_cast<std::uint32_t>(bytes[3]);
  return operation::Result<std::uint32_t>::success(value);
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
  if (!value.has_value()) {
    return operation::Result<float>::failure(value.error());
  }
  return operation::Result<float>::success(std::bit_cast<float>(value.value()));
}

operation::Result<double> read_f64(std::istream& input,
                                   const std::filesystem::path& path,
                                   std::string_view context) {
  const auto high = read_u32(input, path, context);
  if (!high.has_value()) return operation::Result<double>::failure(high.error());
  const auto low = read_u32(input, path, context);
  if (!low.has_value()) return operation::Result<double>::failure(low.error());
  const auto bits = (static_cast<std::uint64_t>(high.value()) << 32U) |
                    static_cast<std::uint64_t>(low.value());
  return operation::Result<double>::success(std::bit_cast<double>(bits));
}

operation::Result<TrrHeader> read_header(
    std::istream& input, const std::filesystem::path& path) {
  const auto magic = read_i32(input, path, "magic");
  if (!magic.has_value()) return operation::Result<TrrHeader>::failure(magic.error());
  if (magic.value() != kTrrMagic) {
    return operation::Result<TrrHeader>::failure(
        trr_error(path, "frame magic is not 1993"));
  }
  const auto string_storage = read_i32(input, path, "version storage length");
  const auto string_length = read_i32(input, path, "version length");
  if (!string_storage.has_value() || !string_length.has_value()) {
    return operation::Result<TrrHeader>::failure(
        !string_storage.has_value() ? string_storage.error()
                                    : string_length.error());
  }
  if (string_storage.value() != static_cast<std::int32_t>(kTrrVersion.size() + 1U) ||
      string_length.value() != static_cast<std::int32_t>(kTrrVersion.size())) {
    return operation::Result<TrrHeader>::failure(
        trr_error(path, "unsupported version string length"));
  }
  std::array<char, 12> version{};
  if (!read_exact(input, version.data(), version.size()) ||
      std::string_view{version.data(), version.size()} != kTrrVersion) {
    return operation::Result<TrrHeader>::failure(
        trr_error(path, "version is not GMX_trn_file"));
  }

  TrrHeader header;
  for (std::size_t index = 0; index < header.sizes.size(); ++index) {
    const auto size = read_i32(input, path, "data-block size");
    if (!size.has_value()) return operation::Result<TrrHeader>::failure(size.error());
    if (size.value() < 0) {
      return operation::Result<TrrHeader>::failure(
          trr_error(path, "data-block size is negative"));
    }
    header.sizes[index] = static_cast<std::uint32_t>(size.value());
    header.payload_bytes += header.sizes[index];
  }
  const auto atom_count = read_i32(input, path, "atom count");
  const auto step = read_i32(input, path, "step");
  const auto nre = read_i32(input, path, "nre");
  if (!atom_count.has_value() || !step.has_value() || !nre.has_value()) {
    return operation::Result<TrrHeader>::failure(
        !atom_count.has_value() ? atom_count.error()
                                : (!step.has_value() ? step.error() : nre.error()));
  }
  if (atom_count.value() <= 0) {
    return operation::Result<TrrHeader>::failure(
        trr_error(path, "atom count must be positive"));
  }
  header.atom_count = static_cast<std::size_t>(atom_count.value());
  header.step = step.value();
  header.nre = nre.value();

  const auto ir = header.sizes[0];
  const auto energy = header.sizes[1];
  const auto box = header.sizes[2];
  const auto virial = header.sizes[3];
  const auto pressure = header.sizes[4];
  const auto topology = header.sizes[5];
  const auto symmetry = header.sizes[6];
  const auto positions = header.sizes[7];
  const auto velocities = header.sizes[8];
  const auto forces = header.sizes[9];
  if (ir != 0U || energy != 0U || topology != 0U || symmetry != 0U) {
    return operation::Result<TrrHeader>::failure(trr_error(
        path, "legacy input/energy/topology/symmetry blocks are unsupported"));
  }
  if (positions == 0U) {
    return operation::Result<TrrHeader>::failure(
        trr_error(path, "positionless frames are unsupported"));
  }
  const auto components = static_cast<std::uint64_t>(header.atom_count) * 3U;
  std::uint64_t real_size = positions / components;
  if (positions % components != 0U ||
      (real_size != sizeof(float) && real_size != sizeof(double))) {
    return operation::Result<TrrHeader>::failure(
        trr_error(path, "coordinate block has an invalid precision/size"));
  }
  header.use_double = real_size == sizeof(double);
  const auto expected_vector = components * real_size;
  const auto expected_matrix = 9U * real_size;
  if ((box != 0U && box != expected_matrix) ||
      (virial != 0U && virial != expected_matrix) ||
      (pressure != 0U && pressure != expected_matrix) ||
      (velocities != 0U && velocities != expected_vector) ||
      (forces != 0U && forces != expected_vector)) {
    return operation::Result<TrrHeader>::failure(
        trr_error(path, "frame data-block sizes are inconsistent"));
  }
  if (header.use_double) {
    const auto time = read_f64(input, path, "time");
    const auto lambda = read_f64(input, path, "lambda");
    if (!time.has_value() || !lambda.has_value()) {
      return operation::Result<TrrHeader>::failure(
          !time.has_value() ? time.error() : lambda.error());
    }
    header.time = time.value();
    header.lambda = lambda.value();
  } else {
    const auto time = read_f32(input, path, "time");
    const auto lambda = read_f32(input, path, "lambda");
    if (!time.has_value() || !lambda.has_value()) {
      return operation::Result<TrrHeader>::failure(
          !time.has_value() ? time.error() : lambda.error());
    }
    header.time = time.value();
    header.lambda = lambda.value();
  }
  if (!std::isfinite(header.time) || !std::isfinite(header.lambda)) {
    return operation::Result<TrrHeader>::failure(
        trr_error(path, "time or lambda is non-finite"));
  }
  return operation::Result<TrrHeader>::success(header);
}

std::optional<operation::Error> seek_forward(
    std::istream& input, std::uint64_t bytes, const std::filesystem::path& path,
    std::string_view context) {
  if (bytes > static_cast<std::uint64_t>(
                  std::numeric_limits<std::streamoff>::max())) {
    return trr_error(path, std::string{context} + " is too large");
  }
  input.seekg(static_cast<std::streamoff>(bytes), std::ios::cur);
  if (!input.good()) {
    return trr_error(path, "truncated " + std::string{context});
  }
  return std::nullopt;
}

template <typename Scalar>
operation::Result<std::vector<model::Vec3<Scalar>>> read_vectors(
    std::istream& input, std::size_t count, double scale,
    const std::filesystem::path& path, std::string_view context) {
  std::vector<model::Vec3<Scalar>> values(count);
  for (auto& value : values) {
    std::array<double, 3> components{};
    for (auto& component : components) {
      if constexpr (std::is_same_v<Scalar, double>) {
        const auto decoded = read_f64(input, path, context);
        if (!decoded.has_value()) {
          return operation::Result<std::vector<model::Vec3<Scalar>>>::failure(
              decoded.error());
        }
        component = decoded.value();
      } else {
        const auto decoded = read_f32(input, path, context);
        if (!decoded.has_value()) {
          return operation::Result<std::vector<model::Vec3<Scalar>>>::failure(
              decoded.error());
        }
        component = decoded.value();
      }
      component *= scale;
      if (!std::isfinite(component)) {
        return operation::Result<std::vector<model::Vec3<Scalar>>>::failure(
            trr_error(path, std::string{context} + " is non-finite"));
      }
    }
    value = {static_cast<Scalar>(components[0]),
             static_cast<Scalar>(components[1]),
             static_cast<Scalar>(components[2])};
  }
  return operation::Result<std::vector<model::Vec3<Scalar>>>::success(
      std::move(values));
}

template <typename Scalar>
operation::Result<std::optional<model::UnitCell>> read_cell(
    std::istream& input, const std::filesystem::path& path) {
  std::array<double, 9> matrix{};
  for (auto& component : matrix) {
    if constexpr (std::is_same_v<Scalar, double>) {
      const auto value = read_f64(input, path, "unit cell");
      if (!value.has_value()) {
        return operation::Result<std::optional<model::UnitCell>>::failure(
            value.error());
      }
      component = value.value() * 10.0;
    } else {
      const auto value = read_f32(input, path, "unit cell");
      if (!value.has_value()) {
        return operation::Result<std::optional<model::UnitCell>>::failure(
            value.error());
      }
      component = static_cast<double>(value.value()) * 10.0;
    }
  }
  const auto all_zero = std::all_of(matrix.begin(), matrix.end(),
                                    [](double value) { return value == 0.0; });
  if (all_zero) {
    return operation::Result<std::optional<model::UnitCell>>::success(
        std::nullopt);
  }
  model::UnitCell cell{{matrix[0], matrix[1], matrix[2]},
                       {matrix[3], matrix[4], matrix[5]},
                       {matrix[6], matrix[7], matrix[8]}};
  if (!cell.is_valid()) {
    return operation::Result<std::optional<model::UnitCell>>::failure(
        trr_error(path, "unit cell is non-finite or degenerate"));
  }
  return operation::Result<std::optional<model::UnitCell>>::success(cell);
}

template <typename Scalar>
operation::Result<std::shared_ptr<const model::CoordinateFrame>> decode_frame(
    std::istream& input, const TrrHeader& header,
    const std::filesystem::path& path) {
  model::FrameMetadata metadata;
  metadata.coordinate_unit = operation::LengthUnit::angstrom;
  if (header.step >= 0) metadata.source_step = static_cast<std::uint64_t>(header.step);
  metadata.physical_time = model::PhysicalTime{header.time, model::TimeUnit::picosecond};
  metadata.fields.emplace("trr.lambda", std::to_string(header.lambda));
  metadata.fields.emplace("trr.nre", std::to_string(header.nre));
  metadata.fields.emplace("trr.signed_step", std::to_string(header.step));

  if (header.sizes[2] != 0U) {
    const auto cell = read_cell<Scalar>(input, path);
    if (!cell.has_value()) {
      return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          cell.error());
    }
    metadata.unit_cell = cell.value();
  }
  if (const auto error = seek_forward(input, header.sizes[3] + header.sizes[4],
                                      path, "virial/pressure blocks");
      error.has_value()) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        error.value());
  }
  const auto positions = read_vectors<Scalar>(input, header.atom_count, 10.0,
                                               path, "coordinates");
  if (!positions.has_value()) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        positions.error());
  }
  std::optional<model::CoordinateBuffer> velocities;
  if (header.sizes[8] != 0U) {
    const auto decoded = read_vectors<Scalar>(input, header.atom_count, 10.0,
                                               path, "velocities");
    if (!decoded.has_value()) {
      return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          decoded.error());
    }
    velocities.emplace(model::CoordinateBuffer{decoded.value()});
    metadata.velocity_time_unit = model::TimeUnit::picosecond;
  }
  if (header.sizes[9] != 0U) {
    const auto forces = read_vectors<Scalar>(input, header.atom_count, 0.1,
                                              path, "forces");
    if (!forces.has_value()) {
      return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          forces.error());
    }
    std::array<std::vector<Scalar>, 3> columns;
    for (const auto& force : forces.value()) {
      columns[0].push_back(force.x);
      columns[1].push_back(force.y);
      columns[2].push_back(force.z);
    }
    for (std::size_t axis = 0; axis < columns.size(); ++axis) {
      model::PropertyMetadata property_metadata;
      property_metadata.unit = "kJ mol^-1 angstrom^-1";
      property_metadata.source = "TRR force block";
      metadata.atom_properties.emplace(
          std::string{"force_"} + "xyz"[axis],
          model::AtomProperty{std::move(columns[axis]),
                              std::move(property_metadata)});
    }
  }
  return model::CoordinateFrame::create(
      model::CoordinateBuffer{positions.value()}, std::move(velocities), {},
      std::move(metadata));
}

}  // namespace

operation::Result<std::shared_ptr<const TrrCoordinateSource>> open_trr(
    const std::filesystem::path& path,
    std::optional<std::size_t> expected_atom_count) {
  std::ifstream input{path, std::ios::binary};
  if (!input) {
    return operation::Result<std::shared_ptr<const TrrCoordinateSource>>::failure(
        {operation::ErrorCode::not_found,
         "cannot open TRR file: " + path.string(),
         "check that the path exists and is readable"});
  }
  std::error_code file_error;
  const auto file_size = std::filesystem::file_size(path, file_error);
  if (file_error) {
    return operation::Result<std::shared_ptr<const TrrCoordinateSource>>::failure(
        trr_error(path, "cannot determine file size"));
  }
  std::vector<std::uint64_t> offsets;
  std::optional<std::size_t> atom_count;
  bool has_cell = false;
  bool has_velocities = false;
  bool has_forces = false;
  std::optional<bool> first_double;
  bool mixed_precision = false;
  while (static_cast<std::uint64_t>(input.tellg()) < file_size) {
    const auto position = input.tellg();
    if (position < 0) {
      return operation::Result<std::shared_ptr<const TrrCoordinateSource>>::failure(
          trr_error(path, "failed to index frame position"));
    }
    const auto header = read_header(input, path);
    if (!header.has_value()) {
      return operation::Result<std::shared_ptr<const TrrCoordinateSource>>::failure(
          header.error());
    }
    if (!atom_count.has_value()) atom_count = header.value().atom_count;
    if (atom_count.value() != header.value().atom_count) {
      return operation::Result<std::shared_ptr<const TrrCoordinateSource>>::failure(
          trr_error(path, "atom count changes between frames"));
    }
    if (!first_double.has_value()) first_double = header.value().use_double;
    mixed_precision |= first_double.value() != header.value().use_double;
    has_cell |= header.value().sizes[2] != 0U;
    has_velocities |= header.value().sizes[8] != 0U;
    has_forces |= header.value().sizes[9] != 0U;
    offsets.push_back(static_cast<std::uint64_t>(position));
    if (const auto error = seek_forward(input, header.value().payload_bytes,
                                        path, "frame payload");
        error.has_value()) {
      return operation::Result<std::shared_ptr<const TrrCoordinateSource>>::failure(
          error.value());
    }
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > file_size) {
      return operation::Result<std::shared_ptr<const TrrCoordinateSource>>::failure(
          trr_error(path, "truncated frame payload"));
    }
  }
  if (offsets.empty() || !atom_count.has_value()) {
    return operation::Result<std::shared_ptr<const TrrCoordinateSource>>::failure(
        trr_error(path, "file contains no frames"));
  }
  if (expected_atom_count.has_value() &&
      expected_atom_count.value() != atom_count.value()) {
    return operation::Result<std::shared_ptr<const TrrCoordinateSource>>::failure(
        trr_error(path, "atom count " + std::to_string(atom_count.value()) +
                            " does not match topology atom count " +
                            std::to_string(expected_atom_count.value())));
  }
  const auto precision = mixed_precision
                             ? TrrPrecision::mixed
                             : (first_double.value() ? TrrPrecision::float64
                                                     : TrrPrecision::float32);
  TrrMetadata metadata{atom_count.value(), offsets.size(), has_cell,
                       has_velocities, has_forces, precision};
  auto source = std::shared_ptr<const TrrCoordinateSource>(
      new TrrCoordinateSource(path, metadata, std::move(offsets)));
  return operation::Result<std::shared_ptr<const TrrCoordinateSource>>::success(
      std::move(source));
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
TrrCoordinateSource::read_frame(std::size_t frame_index) const {
  if (frame_index >= frame_offsets_.size()) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        {operation::ErrorCode::not_found,
         "TRR frame index is out of range: " + std::to_string(frame_index),
         "request an index smaller than the frame count"});
  }
  std::ifstream input{path_, std::ios::binary};
  if (!input) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        {operation::ErrorCode::not_found,
         "cannot reopen TRR file: " + path_.string(), {}});
  }
  if (frame_offsets_[frame_index] > static_cast<std::uint64_t>(
                                        std::numeric_limits<std::streamoff>::max())) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        trr_error(path_, "frame offset is outside stream range"));
  }
  input.seekg(static_cast<std::streamoff>(frame_offsets_[frame_index]));
  const auto header = read_header(input, path_);
  if (!header.has_value()) {
    return operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
        header.error());
  }
  return header.value().use_double
             ? decode_frame<double>(input, header.value(), path_)
             : decode_frame<float>(input, header.value(), path_);
}

}  // namespace molshredder::io
