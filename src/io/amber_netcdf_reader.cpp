#include "molshredder/io/trajectory_reader.hpp"

#include <netcdf.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "structure_reader_internal.hpp"

namespace molshredder::io {
namespace {

using operation::Result;

std::mutex &netcdf_mutex() {
  // Coordinate sources can outlive other translation-unit statics (notably
  // the embedded Python workspace). A process-lifetime mutex avoids static
  // destruction order making nc_close unsafe during interpreter shutdown.
  static auto *mutex = new std::mutex;
  return *mutex;
}

operation::Error invalid(const std::filesystem::path &path, std::string message,
                         std::string suggestion = {}) {
  return {operation::ErrorCode::invalid_argument,
          "Amber NetCDF '" + path.string() + "': " + std::move(message),
          std::move(suggestion)};
}

operation::Error library_error(const std::filesystem::path &path,
                               std::string_view operation, int status) {
  return {operation::ErrorCode::invalid_argument,
          "Amber NetCDF '" + path.string() + "': " + std::string{operation} +
              " failed: " + nc_strerror(status),
          "verify that the file is a readable, uncorrupted Amber NetCDF "
          "trajectory"};
}

struct Variable {
  int id{-1};
  nc_type type{NC_NAT};
  double scale{1.0};
  std::optional<double> fill;
  bool compressed{};
  double compression_factor{1.0};

  [[nodiscard]] bool present() const noexcept { return id >= 0; }
};

struct ReaderState {
  bool restart{};
  Variable coordinates;
  Variable velocities;
  Variable forces;
  Variable time;
  Variable cell_lengths;
  Variable cell_angles;
  Variable temperature;
};

Result<std::optional<std::string>>
text_attribute(int ncid, int variable, std::string_view name,
               const std::filesystem::path &path) {
  std::size_t length{};
  const std::string owned{name};
  auto status = nc_inq_attlen(ncid, variable, owned.c_str(), &length);
  if (status == NC_ENOTATT)
    return Result<std::optional<std::string>>::success(std::nullopt);
  if (status != NC_NOERR)
    return Result<std::optional<std::string>>::failure(
        library_error(path, "reading attribute " + owned, status));
  nc_type type{NC_NAT};
  status = nc_inq_atttype(ncid, variable, owned.c_str(), &type);
  if (status != NC_NOERR)
    return Result<std::optional<std::string>>::failure(
        library_error(path, "reading attribute type " + owned, status));
  if (type == NC_STRING) {
    if (length != 1U) {
      return Result<std::optional<std::string>>::failure(
          invalid(path, "attribute " + owned + " must contain one string"));
    }
    char *raw{nullptr};
    status = nc_get_att_string(ncid, variable, owned.c_str(), &raw);
    if (status != NC_NOERR)
      return Result<std::optional<std::string>>::failure(
          library_error(path, "reading attribute " + owned, status));
    std::string value = raw == nullptr ? std::string{} : std::string{raw};
    static_cast<void>(nc_free_string(1U, &raw));
    return Result<std::optional<std::string>>::success(std::move(value));
  }
  if (type != NC_CHAR) {
    return Result<std::optional<std::string>>::failure(
        invalid(path, "attribute " + owned + " must be text"));
  }
  std::string value(length, '\0');
  status = nc_get_att_text(ncid, variable, owned.c_str(), value.data());
  if (status != NC_NOERR)
    return Result<std::optional<std::string>>::failure(
        library_error(path, "reading attribute " + owned, status));
  while (!value.empty() && value.back() == '\0')
    value.pop_back();
  return Result<std::optional<std::string>>::success(std::move(value));
}

Result<std::optional<double>>
numeric_attribute(int ncid, int variable, std::string_view name,
                  const std::filesystem::path &path) {
  const std::string owned{name};
  nc_type type{NC_NAT};
  auto status = nc_inq_atttype(ncid, variable, owned.c_str(), &type);
  if (status == NC_ENOTATT)
    return Result<std::optional<double>>::success(std::nullopt);
  if (status != NC_NOERR)
    return Result<std::optional<double>>::failure(
        library_error(path, "reading attribute type " + owned, status));
  if (type == NC_CHAR || type == NC_STRING) {
    return Result<std::optional<double>>::failure(
        invalid(path, "attribute " + owned + " must be numeric"));
  }
  std::size_t length{};
  status = nc_inq_attlen(ncid, variable, owned.c_str(), &length);
  if (status != NC_NOERR)
    return Result<std::optional<double>>::failure(
        library_error(path, "reading attribute " + owned, status));
  if (length != 1U) {
    return Result<std::optional<double>>::failure(
        invalid(path, "attribute " + owned + " must be scalar"));
  }
  double value{};
  status = nc_get_att_double(ncid, variable, owned.c_str(), &value);
  if (status != NC_NOERR)
    return Result<std::optional<double>>::failure(
        library_error(path, "reading attribute " + owned, status));
  if (!std::isfinite(value)) {
    return Result<std::optional<double>>::failure(
        invalid(path, "attribute " + owned + " must be finite"));
  }
  return Result<std::optional<double>>::success(value);
}

std::string normalized(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char character) {
                               return character == ' ' || character == '\t';
                             }),
              value.end());
  return value;
}

Result<int> dimension(int ncid, std::string_view name, std::size_t &length,
                      const std::filesystem::path &path) {
  int id{};
  const std::string owned{name};
  auto status = nc_inq_dimid(ncid, owned.c_str(), &id);
  if (status != NC_NOERR)
    return Result<int>::failure(
        library_error(path, "finding dimension " + owned, status));
  status = nc_inq_dimlen(ncid, id, &length);
  if (status != NC_NOERR)
    return Result<int>::failure(
        library_error(path, "reading dimension " + owned, status));
  return Result<int>::success(id);
}

Result<std::optional<int>> variable_id(int ncid, std::string_view name,
                                       const std::filesystem::path &path) {
  int id{};
  const std::string owned{name};
  const auto status = nc_inq_varid(ncid, owned.c_str(), &id);
  if (status == NC_ENOTVAR)
    return Result<std::optional<int>>::success(std::nullopt);
  if (status != NC_NOERR)
    return Result<std::optional<int>>::failure(
        library_error(path, "finding variable " + owned, status));
  return Result<std::optional<int>>::success(id);
}

Result<Variable> inspect_variable(int ncid, int id, std::string_view name,
                                  const std::vector<int> &expected_dimensions,
                                  std::string_view expected_units,
                                  bool compressed,
                                  const std::filesystem::path &path) {
  nc_type type{NC_NAT};
  int rank{};
  std::array<int, NC_MAX_VAR_DIMS> dimensions{};
  const std::string owned{name};
  auto status =
      nc_inq_var(ncid, id, nullptr, &type, &rank, dimensions.data(), nullptr);
  if (status != NC_NOERR)
    return Result<Variable>::failure(
        library_error(path, "inspecting variable " + owned, status));
  if (rank != static_cast<int>(expected_dimensions.size()) ||
      !std::equal(expected_dimensions.begin(), expected_dimensions.end(),
                  dimensions.begin())) {
    return Result<Variable>::failure(
        invalid(path, "variable " + owned + " has an invalid dimension order"));
  }
  if (compressed) {
    if (type != NC_INT) {
      return Result<Variable>::failure(
          invalid(path, "compressed variable " + owned + " must be NC_INT"));
    }
  } else if (type != NC_FLOAT && type != NC_DOUBLE) {
    return Result<Variable>::failure(
        invalid(path, "variable " + owned + " must be NC_FLOAT or NC_DOUBLE"));
  }

  if (!expected_units.empty()) {
    const auto units = text_attribute(ncid, id, "units", path);
    if (!units.has_value())
      return Result<Variable>::failure(units.error());
    if (!units.value().has_value() ||
        normalized(*units.value()) != normalized(std::string{expected_units})) {
      return Result<Variable>::failure(
          invalid(path, "variable " + owned + " must use units '" +
                            std::string{expected_units} + "'"));
    }
  }

  Variable result;
  result.id = id;
  result.type = type;
  result.compressed = compressed;
  const auto fill = numeric_attribute(ncid, id, "_FillValue", path);
  if (!fill.has_value())
    return Result<Variable>::failure(fill.error());
  result.fill = fill.value();
  if (compressed) {
    const auto factor = numeric_attribute(ncid, id, "icompressfac", path);
    if (!factor.has_value())
      return Result<Variable>::failure(factor.error());
    if (!factor.value().has_value() || *factor.value() <= 0.0) {
      return Result<Variable>::failure(
          invalid(path, "compressed variable " + owned +
                            " requires positive icompressfac"));
    }
    result.compression_factor = *factor.value();
  } else {
    const auto scale = numeric_attribute(ncid, id, "scale_factor", path);
    if (!scale.has_value())
      return Result<Variable>::failure(scale.error());
    if (scale.value().has_value()) {
      if (*scale.value() == 0.0) {
        return Result<Variable>::failure(
            invalid(path, "variable " + owned + " has a zero scale_factor"));
      }
      result.scale = *scale.value();
    }
  }
  return Result<Variable>::success(result);
}

Result<Variable> optional_channel(int ncid, std::string_view standard_name,
                                  std::string_view compressed_name,
                                  const std::vector<int> &dimensions,
                                  std::string_view units,
                                  const std::filesystem::path &path) {
  const auto standard = variable_id(ncid, standard_name, path);
  if (!standard.has_value())
    return Result<Variable>::failure(standard.error());
  const auto compressed = variable_id(ncid, compressed_name, path);
  if (!compressed.has_value())
    return Result<Variable>::failure(compressed.error());
  if (standard.value().has_value() && compressed.value().has_value()) {
    return Result<Variable>::failure(invalid(
        path, "variables " + std::string{standard_name} + " and " +
                  std::string{compressed_name} + " cannot both be present"));
  }
  if (standard.value().has_value())
    return inspect_variable(ncid, *standard.value(), standard_name, dimensions,
                            units, false, path);
  if (compressed.value().has_value())
    return inspect_variable(ncid, *compressed.value(), compressed_name,
                            dimensions, units, true, path);
  return Result<Variable>::success({});
}

bool missing_value(double value, const Variable &variable) {
  if (variable.fill.has_value() && value == *variable.fill)
    return true;
  if (variable.type == NC_FLOAT && value == static_cast<double>(NC_FILL_FLOAT))
    return true;
  return variable.type == NC_DOUBLE && value == NC_FILL_DOUBLE;
}

Result<std::vector<double>> read_values(int ncid, const Variable &variable,
                                        std::size_t frame,
                                        std::size_t value_count,
                                        const std::filesystem::path &path,
                                        std::string_view name, bool restart) {
  const std::array<std::size_t, 3U> start{frame, 0U, 0U};
  const std::array<std::size_t, 3U> count{1U, value_count / 3U, 3U};
  std::vector<double> result(value_count);
  int status{};
  if (variable.compressed) {
    std::vector<int> encoded(value_count);
    status = restart ? nc_get_var_int(ncid, variable.id, encoded.data())
                     : nc_get_vara_int(ncid, variable.id, start.data(),
                                       count.data(), encoded.data());
    if (status == NC_NOERR) {
      for (std::size_t index = 0U; index < value_count; ++index) {
        if (encoded[index] == NC_FILL_INT ||
            (variable.fill.has_value() &&
             static_cast<double>(encoded[index]) == *variable.fill)) {
          return Result<std::vector<double>>::failure(
              invalid(path, "variable " + std::string{name} +
                                " contains missing data in frame " +
                                std::to_string(frame)));
        }
        result[index] =
            static_cast<double>(encoded[index]) / variable.compression_factor;
      }
    }
  } else {
    status = restart ? nc_get_var_double(ncid, variable.id, result.data())
                     : nc_get_vara_double(ncid, variable.id, start.data(),
                                          count.data(), result.data());
    if (status == NC_NOERR) {
      for (auto &value : result) {
        if (!std::isfinite(value) || missing_value(value, variable)) {
          return Result<std::vector<double>>::failure(invalid(
              path, "variable " + std::string{name} +
                        " contains missing or non-finite data in frame " +
                        std::to_string(frame)));
        }
        value *= variable.scale;
      }
    }
  }
  if (status != NC_NOERR)
    return Result<std::vector<double>>::failure(
        library_error(path, "reading variable " + std::string{name}, status));
  for (const auto value : result) {
    if (!std::isfinite(value)) {
      return Result<std::vector<double>>::failure(
          invalid(path, "variable " + std::string{name} +
                            " contains missing or non-finite data in frame " +
                            std::to_string(frame)));
    }
  }
  return Result<std::vector<double>>::success(std::move(result));
}

Result<std::array<double, 3U>> read_triplet(int ncid, const Variable &variable,
                                            std::size_t frame,
                                            const std::filesystem::path &path,
                                            std::string_view name,
                                            bool restart) {
  const std::array<std::size_t, 2U> start{frame, 0U};
  const std::array<std::size_t, 2U> count{1U, 3U};
  std::array<double, 3U> result{};
  const auto status =
      restart ? nc_get_var_double(ncid, variable.id, result.data())
              : nc_get_vara_double(ncid, variable.id, start.data(),
                                    count.data(), result.data());
  if (status != NC_NOERR)
    return Result<std::array<double, 3U>>::failure(
        library_error(path, "reading variable " + std::string{name}, status));
  for (auto &value : result) {
    if (!std::isfinite(value) || missing_value(value, variable))
      return Result<std::array<double, 3U>>::failure(
          invalid(path, "variable " + std::string{name} +
                            " contains missing or non-finite data"));
    value *= variable.scale;
  }
  for (const auto value : result) {
    if (!std::isfinite(value))
      return Result<std::array<double, 3U>>::failure(
          invalid(path, "variable " + std::string{name} +
                            " contains missing or non-finite data"));
  }
  return Result<std::array<double, 3U>>::success(result);
}

Result<double> read_scalar(int ncid, const Variable &variable,
                           std::size_t frame, const std::filesystem::path &path,
                           std::string_view name, bool restart) {
  const std::array<std::size_t, 1U> start{frame};
  double result{};
  const auto status = restart
                          ? nc_get_var_double(ncid, variable.id, &result)
                          : nc_get_var1_double(ncid, variable.id, start.data(),
                                               &result);
  if (status != NC_NOERR)
    return Result<double>::failure(
        library_error(path, "reading variable " + std::string{name}, status));
  if (!std::isfinite(result) || missing_value(result, variable))
    return Result<double>::failure(
        invalid(path, "variable " + std::string{name} +
                          " contains missing or non-finite data"));
  result *= variable.scale;
  if (!std::isfinite(result))
    return Result<double>::failure(invalid(
        path, "scaled variable " + std::string{name} + " is non-finite"));
  return Result<double>::success(result);
}

std::string scalar_string(double value) { return std::to_string(value); }

model::CoordinateBuffer coordinate_buffer(const std::vector<double> &values,
                                          const Variable &variable) {
  if (variable.type == NC_FLOAT && !variable.compressed) {
    std::vector<model::Vec3f> converted;
    converted.reserve(values.size() / 3U);
    for (std::size_t index = 0U; index < values.size(); index += 3U)
      converted.push_back({static_cast<float>(values[index]),
                           static_cast<float>(values[index + 1U]),
                           static_cast<float>(values[index + 2U])});
    return model::CoordinateBuffer{std::move(converted)};
  }
  std::vector<model::Vec3d> converted;
  converted.reserve(values.size() / 3U);
  for (std::size_t index = 0U; index < values.size(); index += 3U)
    converted.push_back(
        {values[index], values[index + 1U], values[index + 2U]});
  return model::CoordinateBuffer{std::move(converted)};
}

class AmberNetcdfCoordinateSource final : public model::CoordinateSource {
public:
  AmberNetcdfCoordinateSource(std::filesystem::path path, int ncid,
                              AmberNetcdfMetadata metadata, ReaderState state)
      : path_{std::move(path)}, ncid_{ncid}, metadata_{std::move(metadata)},
        state_{std::move(state)} {}

  ~AmberNetcdfCoordinateSource() override {
    std::lock_guard lock{netcdf_mutex()};
    if (ncid_ >= 0)
      static_cast<void>(nc_close(ncid_));
  }

  [[nodiscard]] std::size_t atom_count() const noexcept override {
    return metadata_.atom_count;
  }
  [[nodiscard]] std::optional<std::size_t>
  frame_count() const noexcept override {
    return metadata_.frame_count;
  }
  [[nodiscard]] model::FrameAccess access() const noexcept override {
    return model::FrameAccess::random_access;
  }

  [[nodiscard]] Result<std::shared_ptr<const model::CoordinateFrame>>
  read_frame(std::size_t frame) const override {
    if (frame >= metadata_.frame_count) {
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          invalid(path_, "frame index is out of range"));
    }
    std::lock_guard lock{netcdf_mutex()};
    const auto value_count = metadata_.atom_count * 3U;
    auto positions = read_values(
        ncid_, state_.coordinates, frame, value_count, path_,
        state_.coordinates.compressed ? "compressedpos" : "coordinates",
        state_.restart);
    if (!positions.has_value())
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          positions.error());

    std::optional<model::CoordinateBuffer> velocity_buffer;
    if (state_.velocities.present()) {
      auto velocities = read_values(
          ncid_, state_.velocities, frame, value_count, path_,
          state_.velocities.compressed ? "compressedvel" : "velocities",
          state_.restart);
      if (!velocities.has_value())
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            velocities.error());
      velocity_buffer.emplace(
          coordinate_buffer(velocities.value(), state_.velocities));
    }

    model::FrameMetadata frame_metadata;
    frame_metadata.coordinate_unit = operation::LengthUnit::angstrom;
    frame_metadata.fields.emplace(
        "format", state_.restart ? "amber-netcdf-restart" : "amber-netcdf");
    frame_metadata.fields.emplace("storage_format", metadata_.storage_format);
    if (state_.velocities.present())
      frame_metadata.velocity_time_unit = model::TimeUnit::picosecond;
    if (state_.time.present()) {
      auto time = read_scalar(ncid_, state_.time, frame, path_, "time",
                              state_.restart);
      if (!time.has_value())
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            time.error());
      frame_metadata.physical_time =
          model::PhysicalTime{time.value(), model::TimeUnit::picosecond};
    }
    if (state_.temperature.present()) {
      auto temperature =
          read_scalar(ncid_, state_.temperature, frame, path_, "temp0",
                      state_.restart);
      if (!temperature.has_value())
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            temperature.error());
      frame_metadata.fields.emplace("temperature",
                                    scalar_string(temperature.value()));
      frame_metadata.fields.emplace("temperature_unit", "kelvin");
    }
    if (state_.cell_lengths.present()) {
      auto lengths = read_triplet(ncid_, state_.cell_lengths, frame, path_,
                                  "cell_lengths", state_.restart);
      auto angles = read_triplet(ncid_, state_.cell_angles, frame, path_,
                                 "cell_angles", state_.restart);
      if (!lengths.has_value())
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            lengths.error());
      if (!angles.has_value())
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            angles.error());
      auto cell = detail::make_unit_cell(lengths.value()[0], lengths.value()[1],
                                         lengths.value()[2], angles.value()[0],
                                         angles.value()[1], angles.value()[2],
                                         path_.string(), frame);
      if (!cell.has_value())
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            cell.error());
      frame_metadata.unit_cell = cell.value();
    }
    if (state_.forces.present()) {
      auto forces =
          read_values(ncid_, state_.forces, frame, value_count, path_,
                      state_.forces.compressed ? "compressedfrc" : "forces",
                      state_.restart);
      if (!forces.has_value())
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            forces.error());
      const std::array<std::string, 3U> names{"force.x", "force.y", "force.z"};
      for (std::size_t component = 0U; component < 3U; ++component) {
        model::AtomPropertyColumn column;
        if (state_.forces.type == NC_FLOAT && !state_.forces.compressed) {
          std::vector<float> values;
          values.reserve(metadata_.atom_count);
          for (std::size_t atom = 0U; atom < metadata_.atom_count; ++atom)
            values.push_back(
                static_cast<float>(forces.value()[atom * 3U + component]));
          column = std::move(values);
        } else {
          std::vector<double> values;
          values.reserve(metadata_.atom_count);
          for (std::size_t atom = 0U; atom < metadata_.atom_count; ++atom)
            values.push_back(forces.value()[atom * 3U + component]);
          column = std::move(values);
        }
        frame_metadata.atom_properties.emplace(
            names[component],
            model::AtomProperty{
                std::move(column),
                {"kilocalorie/mole/angstrom", "Amber NetCDF forces", {}}});
      }
    }
    auto result = model::CoordinateFrame::create(
        coordinate_buffer(positions.value(), state_.coordinates),
        std::move(velocity_buffer), {}, std::move(frame_metadata));
    if (!result.has_value())
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          result.error());
    return result;
  }

private:
  std::filesystem::path path_;
  int ncid_{-1};
  AmberNetcdfMetadata metadata_;
  ReaderState state_;
};

Result<std::string> required_text_attribute(int ncid, std::string_view name,
                                            const std::filesystem::path &path) {
  auto value = text_attribute(ncid, NC_GLOBAL, name, path);
  if (!value.has_value())
    return Result<std::string>::failure(value.error());
  if (!value.value().has_value())
    return Result<std::string>::failure(
        invalid(path, "missing global attribute " + std::string{name}));
  return Result<std::string>::success(std::move(*value.value()));
}

std::string storage_format_name(int format) {
  switch (format) {
  case NC_FORMAT_CLASSIC:
    return "netcdf-classic";
  case NC_FORMAT_64BIT_OFFSET:
    return "netcdf-64bit-offset";
  case NC_FORMAT_64BIT_DATA:
    return "netcdf-64bit-data";
  case NC_FORMAT_NETCDF4:
    return "netcdf-4";
  case NC_FORMAT_NETCDF4_CLASSIC:
    return "netcdf-4-classic";
  default:
    return "netcdf-unknown";
  }
}

} // namespace

operation::Result<std::shared_ptr<const model::CoordinateSource>>
open_amber_netcdf(const std::filesystem::path &path,
                  std::optional<std::size_t> expected_atom_count,
                  AmberNetcdfMetadata *metadata_output) {
  std::error_code path_error;
  if (!std::filesystem::exists(path, path_error)) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        {operation::ErrorCode::not_found,
         "cannot open Amber NetCDF trajectory '" + path.string() + "'",
         "select an existing Amber NetCDF trajectory"});
  }
  std::lock_guard lock{netcdf_mutex()};
  int ncid{-1};
  auto status = nc_open(path.string().c_str(), NC_NOWRITE, &ncid);
  if (status != NC_NOERR) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        {operation::ErrorCode::invalid_argument,
         "cannot open Amber NetCDF trajectory '" + path.string() +
             "': " + nc_strerror(status),
         "select an existing Amber NetCDF trajectory"});
  }
  const auto close_on_failure = [&ncid]() {
    if (ncid >= 0)
      static_cast<void>(nc_close(ncid));
  };
  const auto fail = [&](operation::Error error) {
    close_on_failure();
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        std::move(error));
  };

  auto conventions = required_text_attribute(ncid, "Conventions", path);
  if (!conventions.has_value())
    return fail(conventions.error());
  auto version = required_text_attribute(ncid, "ConventionVersion", path);
  if (!version.has_value())
    return fail(version.error());
  const bool restart = conventions.value() == "AMBERRESTART";
  if ((!restart && conventions.value() != "AMBER") ||
      version.value() != "1.0") {
    return fail(invalid(path, "requires Conventions='AMBER' or "
                              "'AMBERRESTART' and ConventionVersion='1.0'"));
  }

  std::size_t frame_count{1U};
  std::size_t atom_count{};
  std::size_t spatial_count{};
  int frame_dimension_id{-1};
  if (!restart) {
    auto frame_dimension = dimension(ncid, "frame", frame_count, path);
    if (!frame_dimension.has_value())
      return fail(frame_dimension.error());
    frame_dimension_id = frame_dimension.value();
  }
  auto atom_dimension = dimension(ncid, "atom", atom_count, path);
  auto spatial_dimension = dimension(ncid, "spatial", spatial_count, path);
  if (!atom_dimension.has_value())
    return fail(atom_dimension.error());
  if (!spatial_dimension.has_value())
    return fail(spatial_dimension.error());
  if (frame_count == 0U || atom_count == 0U || spatial_count != 3U) {
    return fail(invalid(path, "frame and atom dimensions must be non-zero and "
                              "spatial must equal 3"));
  }
  if (atom_count > std::numeric_limits<std::size_t>::max() / 3U)
    return fail(invalid(path, "atom dimension is too large"));
  if (expected_atom_count.has_value() && *expected_atom_count != atom_count) {
    return fail(
        invalid(path,
                "atom count " + std::to_string(atom_count) +
                    " does not match the active topology atom count " +
                    std::to_string(*expected_atom_count),
                "attach a trajectory produced for the active topology"));
  }

  auto spatial_variable = variable_id(ncid, "spatial", path);
  if (!spatial_variable.has_value())
    return fail(spatial_variable.error());
  if (!spatial_variable.value().has_value())
    return fail(invalid(path, "missing spatial coordinate-label variable"));
  nc_type spatial_type{NC_NAT};
  int spatial_rank{};
  std::array<int, NC_MAX_VAR_DIMS> spatial_dimensions{};
  status = nc_inq_var(ncid, *spatial_variable.value(), nullptr, &spatial_type,
                      &spatial_rank, spatial_dimensions.data(), nullptr);
  if (status != NC_NOERR)
    return fail(library_error(path, "inspecting variable spatial", status));
  if (spatial_type != NC_CHAR || spatial_rank != 1 ||
      spatial_dimensions[0] != spatial_dimension.value())
    return fail(invalid(path, "spatial variable must be char spatial(3)"));
  std::array<char, 3U> labels{};
  status = nc_get_var_text(ncid, *spatial_variable.value(), labels.data());
  if (status != NC_NOERR)
    return fail(library_error(path, "reading variable spatial", status));
  if (labels != std::array<char, 3U>{'x', 'y', 'z'})
    return fail(invalid(path, "spatial labels must be x, y, z"));

  std::vector<int> coordinate_dimensions;
  if (!restart)
    coordinate_dimensions.push_back(frame_dimension_id);
  coordinate_dimensions.push_back(atom_dimension.value());
  coordinate_dimensions.push_back(spatial_dimension.value());
  ReaderState state;
  state.restart = restart;
  auto coordinates = optional_channel(ncid, "coordinates", "compressedpos",
                                      coordinate_dimensions, "angstrom", path);
  if (!coordinates.has_value())
    return fail(coordinates.error());
  if (!coordinates.value().present())
    return fail(invalid(path, "missing coordinates or compressedpos variable"));
  state.coordinates = coordinates.value();

  auto velocities =
      optional_channel(ncid, "velocities", "compressedvel",
                       coordinate_dimensions,
                       "angstrom/picosecond", path);
  if (!velocities.has_value())
    return fail(velocities.error());
  state.velocities = velocities.value();
  auto forces =
      optional_channel(ncid, "forces", "compressedfrc", coordinate_dimensions,
                       "kilocalorie/mole/angstrom", path);
  if (!forces.has_value())
    return fail(forces.error());
  state.forces = forces.value();

  const auto inspect_optional_scalar =
      [&](std::string_view name, std::string_view units) -> Result<Variable> {
    auto id = variable_id(ncid, name, path);
    if (!id.has_value())
      return Result<Variable>::failure(id.error());
    if (!id.value().has_value())
      return Result<Variable>::success({});
    const std::vector<int> dimensions = restart
                                            ? std::vector<int>{}
                                            : std::vector<int>{frame_dimension_id};
    return inspect_variable(ncid, *id.value(), name, dimensions, units, false,
                            path);
  };
  auto time = inspect_optional_scalar("time", "picosecond");
  if (!time.has_value())
    return fail(time.error());
  state.time = time.value();
  auto temperature = inspect_optional_scalar("temp0", "kelvin");
  if (!temperature.has_value())
    return fail(temperature.error());
  state.temperature = temperature.value();

  auto lengths_id = variable_id(ncid, "cell_lengths", path);
  auto angles_id = variable_id(ncid, "cell_angles", path);
  if (!lengths_id.has_value())
    return fail(lengths_id.error());
  if (!angles_id.has_value())
    return fail(angles_id.error());
  if (lengths_id.value().has_value() != angles_id.value().has_value())
    return fail(
        invalid(path, "cell_lengths and cell_angles must be present together"));
  if (lengths_id.value().has_value()) {
    std::size_t cell_spatial_count{};
    std::size_t cell_angular_count{};
    auto cell_spatial =
        dimension(ncid, "cell_spatial", cell_spatial_count, path);
    auto cell_angular =
        dimension(ncid, "cell_angular", cell_angular_count, path);
    if (!cell_spatial.has_value())
      return fail(cell_spatial.error());
    if (!cell_angular.has_value())
      return fail(cell_angular.error());
    if (cell_spatial_count != 3U || cell_angular_count != 3U)
      return fail(invalid(path, "cell dimensions must both equal 3"));
    std::size_t label_count{};
    auto label = dimension(ncid, "label", label_count, path);
    if (!label.has_value())
      return fail(label.error());
    if (label_count != 5U)
      return fail(invalid(path, "cell angular label width must equal 5"));
    auto cell_spatial_labels = variable_id(ncid, "cell_spatial", path);
    auto cell_angular_labels = variable_id(ncid, "cell_angular", path);
    if (!cell_spatial_labels.has_value())
      return fail(cell_spatial_labels.error());
    if (!cell_angular_labels.has_value())
      return fail(cell_angular_labels.error());
    if (!cell_spatial_labels.value().has_value() ||
        !cell_angular_labels.value().has_value())
      return fail(invalid(path, "cell axis label variables are required"));
    nc_type axis_type{NC_NAT};
    int axis_rank{};
    std::array<int, NC_MAX_VAR_DIMS> axis_dimensions{};
    status = nc_inq_var(ncid, *cell_spatial_labels.value(), nullptr, &axis_type,
                        &axis_rank, axis_dimensions.data(), nullptr);
    if (status != NC_NOERR)
      return fail(
          library_error(path, "inspecting variable cell_spatial", status));
    if (axis_type != NC_CHAR || axis_rank != 1 ||
        axis_dimensions[0] != cell_spatial.value())
      return fail(invalid(path, "cell_spatial labels must be char(3)"));
    std::array<char, 3U> axis_labels{};
    status =
        nc_get_var_text(ncid, *cell_spatial_labels.value(), axis_labels.data());
    if (status != NC_NOERR)
      return fail(library_error(path, "reading variable cell_spatial", status));
    if (axis_labels != std::array<char, 3U>{'a', 'b', 'c'})
      return fail(invalid(path, "cell spatial labels must be a, b, c"));

    axis_dimensions.fill(0);
    status = nc_inq_var(ncid, *cell_angular_labels.value(), nullptr, &axis_type,
                        &axis_rank, axis_dimensions.data(), nullptr);
    if (status != NC_NOERR)
      return fail(
          library_error(path, "inspecting variable cell_angular", status));
    if (axis_type != NC_CHAR || axis_rank != 2 ||
        axis_dimensions[0] != cell_angular.value() ||
        axis_dimensions[1] != label.value())
      return fail(invalid(path, "cell_angular labels must be char(3,5)"));
    std::array<char, 15U> angular_labels{};
    status = nc_get_var_text(ncid, *cell_angular_labels.value(),
                             angular_labels.data());
    if (status != NC_NOERR)
      return fail(library_error(path, "reading variable cell_angular", status));
    constexpr std::array<char, 15U> expected_angular{'a', 'l', 'p', 'h', 'a',
                                                     'b', 'e', 't', 'a', ' ',
                                                     'g', 'a', 'm', 'm', 'a'};
    if (angular_labels != expected_angular)
      return fail(
          invalid(path, "cell angular labels must be alpha, beta, gamma"));
    const std::vector<int> length_dimensions =
        restart ? std::vector<int>{cell_spatial.value()}
                : std::vector<int>{frame_dimension_id, cell_spatial.value()};
    const std::vector<int> angle_dimensions =
        restart ? std::vector<int>{cell_angular.value()}
                : std::vector<int>{frame_dimension_id, cell_angular.value()};
    auto lengths = inspect_variable(ncid, *lengths_id.value(), "cell_lengths",
                                    length_dimensions, "angstrom", false, path);
    auto angles = inspect_variable(
        ncid, *angles_id.value(), "cell_angles", angle_dimensions, "degree",
        false, path);
    if (!lengths.has_value())
      return fail(lengths.error());
    if (!angles.has_value())
      return fail(angles.error());
    state.cell_lengths = lengths.value();
    state.cell_angles = angles.value();
  }

  int storage_format{};
  status = nc_inq_format(ncid, &storage_format);
  if (status != NC_NOERR)
    return fail(library_error(path, "reading storage format", status));
  AmberNetcdfMetadata metadata{atom_count,
                               frame_count,
                               restart,
                               state.time.present(),
                               state.velocities.present(),
                               state.forces.present(),
                               state.temperature.present(),
                               state.cell_lengths.present(),
                               state.coordinates.compressed ||
                                   state.velocities.compressed ||
                                   state.forces.compressed,
                               storage_format_name(storage_format),
                               {},
                               {},
                               {},
                               {}};
  const auto copy_optional_global =
      [&](std::string_view name,
          std::string &destination) -> std::optional<operation::Error> {
    auto value = text_attribute(ncid, NC_GLOBAL, name, path);
    if (!value.has_value())
      return value.error();
    if (value.value().has_value())
      destination = std::move(*value.value());
    return std::nullopt;
  };
  if (auto error = copy_optional_global("title", metadata.title); error)
    return fail(*error);
  if (auto error = copy_optional_global("program", metadata.program); error)
    return fail(*error);
  if (auto error =
          copy_optional_global("programVersion", metadata.program_version);
      error)
    return fail(*error);
  if (auto error = copy_optional_global("application", metadata.application);
      error)
    return fail(*error);

  auto source = std::make_shared<AmberNetcdfCoordinateSource>(
      path, ncid, metadata, std::move(state));
  ncid = -1;
  if (metadata_output != nullptr)
    *metadata_output = metadata;
  return Result<std::shared_ptr<const model::CoordinateSource>>::success(
      std::move(source));
}

} // namespace molshredder::io
