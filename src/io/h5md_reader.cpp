#include "molshredder/io/trajectory_reader.hpp"

#include <hdf5.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace molshredder::io {
namespace {

using operation::Result;

std::mutex &hdf5_mutex() {
  // HDF5 thread-safety varies by build, and coordinate sources can outlive
  // other translation-unit statics during embedded-Python shutdown.
  static auto *mutex = new std::mutex;
  return *mutex;
}

operation::Error invalid(const std::filesystem::path &path, std::string message,
                         std::string suggestion = {}) {
  return {operation::ErrorCode::invalid_argument,
          "H5MD '" + path.string() + "': " + std::move(message),
          std::move(suggestion)};
}

operation::Error hdf5_error(const std::filesystem::path &path,
                            std::string_view operation) {
  return invalid(path, std::string{operation} + " failed",
                 "verify that the file is a readable, uncorrupted H5MD file");
}

struct Handle {
  hid_t id{-1};
  herr_t (*closer)(hid_t){};

  Handle() = default;
  Handle(hid_t value, herr_t (*close_function)(hid_t))
      : id{value}, closer{close_function} {}
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
  Handle(Handle &&other) noexcept : id{other.id}, closer{other.closer} {
    other.id = -1;
  }
  Handle &operator=(Handle &&other) noexcept {
    if (this != &other) {
      reset();
      id = other.id;
      closer = other.closer;
      other.id = -1;
    }
    return *this;
  }
  ~Handle() { reset(); }

  void reset() noexcept {
    if (id >= 0 && closer != nullptr) {
      htri_t valid{-1};
      H5E_BEGIN_TRY { valid = H5Iis_valid(id); }
      H5E_END_TRY;
      if (valid > 0)
        static_cast<void>(closer(id));
    }
    id = -1;
  }
  [[nodiscard]] bool valid() const noexcept { return id >= 0; }
};

bool link_exists(hid_t parent, std::string_view name) {
  const std::string owned{name};
  const auto result = H5Lexists(parent, owned.c_str(), H5P_DEFAULT);
  return result > 0;
}

struct LinkAudit {
  bool external{};
};

herr_t audit_link(hid_t, const char *, const H5L_info2_t *info,
                  void *user_data) {
  if (info != nullptr && info->type == H5L_TYPE_EXTERNAL)
    static_cast<LinkAudit *>(user_data)->external = true;
  return 0;
}

Result<Handle> open_group(hid_t parent, std::string_view name,
                          const std::filesystem::path &path) {
  const std::string owned{name};
  const auto id = H5Gopen2(parent, owned.c_str(), H5P_DEFAULT);
  if (id < 0)
    return Result<Handle>::failure(hdf5_error(path, "opening group " + owned));
  return Result<Handle>::success(Handle{id, H5Gclose});
}

Result<Handle> open_dataset(hid_t parent, std::string_view name,
                            const std::filesystem::path &path) {
  const std::string owned{name};
  const auto id = H5Dopen2(parent, owned.c_str(), H5P_DEFAULT);
  if (id < 0)
    return Result<Handle>::failure(
        hdf5_error(path, "opening dataset " + owned));
  return Result<Handle>::success(Handle{id, H5Dclose});
}

Result<std::vector<std::string>>
string_attribute(hid_t object, std::string_view name,
                 const std::filesystem::path &path) {
  const std::string owned{name};
  const auto exists = H5Aexists(object, owned.c_str());
  if (exists < 0)
    return Result<std::vector<std::string>>::failure(
        hdf5_error(path, "checking attribute " + owned));
  if (exists == 0)
    return Result<std::vector<std::string>>::success({});
  Handle attribute{H5Aopen(object, owned.c_str(), H5P_DEFAULT), H5Aclose};
  if (!attribute.valid())
    return Result<std::vector<std::string>>::failure(
        hdf5_error(path, "opening attribute " + owned));
  Handle type{H5Aget_type(attribute.id), H5Tclose};
  Handle space{H5Aget_space(attribute.id), H5Sclose};
  if (!type.valid() || !space.valid())
    return Result<std::vector<std::string>>::failure(
        hdf5_error(path, "inspecting attribute " + owned));
  if (H5Tget_class(type.id) != H5T_STRING)
    return Result<std::vector<std::string>>::failure(
        invalid(path, "attribute " + owned + " must contain text"));
  const auto points = H5Sget_simple_extent_npoints(space.id);
  if (points < 0)
    return Result<std::vector<std::string>>::failure(
        hdf5_error(path, "reading attribute shape " + owned));
  const auto count = static_cast<std::size_t>(points);
  std::vector<std::string> result;
  result.reserve(count);
  if (H5Tis_variable_str(type.id) > 0) {
    std::vector<char *> raw(count, nullptr);
    if (H5Aread(attribute.id, type.id, raw.data()) < 0)
      return Result<std::vector<std::string>>::failure(
          hdf5_error(path, "reading attribute " + owned));
    for (const auto *value : raw)
      result.emplace_back(value == nullptr ? "" : value);
    if (H5Treclaim(type.id, space.id, H5P_DEFAULT, raw.data()) < 0)
      return Result<std::vector<std::string>>::failure(
          hdf5_error(path, "reclaiming attribute " + owned));
    return Result<std::vector<std::string>>::success(std::move(result));
  }
  const auto width = H5Tget_size(type.id);
  if (width == 0U ||
      (count != 0U &&
       width > std::numeric_limits<std::size_t>::max() / count)) {
    return Result<std::vector<std::string>>::failure(
        invalid(path, "attribute " + owned + " has an invalid string size"));
  }
  std::vector<char> raw(width * count, '\0');
  if (H5Aread(attribute.id, type.id, raw.data()) < 0)
    return Result<std::vector<std::string>>::failure(
        hdf5_error(path, "reading attribute " + owned));
  for (std::size_t index = 0U; index < count; ++index) {
    const auto begin = raw.begin() + static_cast<std::ptrdiff_t>(index * width);
    auto end = begin + static_cast<std::ptrdiff_t>(width);
    const auto null = std::find(begin, end, '\0');
    end = null;
    while (end != begin && (end[-1] == ' ' || end[-1] == '\0'))
      --end;
    result.emplace_back(begin, end);
  }
  return Result<std::vector<std::string>>::success(std::move(result));
}

Result<std::optional<std::string>>
scalar_string_attribute(hid_t object, std::string_view name,
                        const std::filesystem::path &path) {
  auto values = string_attribute(object, name, path);
  if (!values.has_value())
    return Result<std::optional<std::string>>::failure(values.error());
  if (values.value().empty())
    return Result<std::optional<std::string>>::success(std::nullopt);
  if (values.value().size() != 1U)
    return Result<std::optional<std::string>>::failure(
        invalid(path, "attribute " + std::string{name} + " must be scalar"));
  return Result<std::optional<std::string>>::success(
      std::move(values.value().front()));
}

template <typename Value>
Result<std::optional<Value>>
numeric_attribute(hid_t object, std::string_view name, hid_t native_type,
                  const std::filesystem::path &path) {
  const std::string owned{name};
  const auto exists = H5Aexists(object, owned.c_str());
  if (exists < 0)
    return Result<std::optional<Value>>::failure(
        hdf5_error(path, "checking attribute " + owned));
  if (exists == 0)
    return Result<std::optional<Value>>::success(std::nullopt);
  Handle attribute{H5Aopen(object, owned.c_str(), H5P_DEFAULT), H5Aclose};
  Handle space{attribute.valid() ? H5Aget_space(attribute.id) : -1, H5Sclose};
  Handle type{attribute.valid() ? H5Aget_type(attribute.id) : -1, H5Tclose};
  if (!attribute.valid() || !space.valid() || !type.valid())
    return Result<std::optional<Value>>::failure(
        hdf5_error(path, "opening attribute " + owned));
  if ((H5Tget_class(type.id) != H5T_INTEGER &&
       H5Tget_class(type.id) != H5T_FLOAT) ||
      H5Sget_simple_extent_npoints(space.id) != 1) {
    return Result<std::optional<Value>>::failure(
        invalid(path, "attribute " + owned + " must be one numeric scalar"));
  }
  Value value{};
  if (H5Aread(attribute.id, native_type, &value) < 0)
    return Result<std::optional<Value>>::failure(
        hdf5_error(path, "reading attribute " + owned));
  return Result<std::optional<Value>>::success(value);
}

Result<std::array<std::int64_t, 2U>>
version_attribute(hid_t h5md, const std::filesystem::path &path) {
  if (H5Aexists(h5md, "version") <= 0)
    return Result<std::array<std::int64_t, 2U>>::failure(
        invalid(path, "missing /h5md version attribute"));
  Handle attribute{H5Aopen(h5md, "version", H5P_DEFAULT), H5Aclose};
  Handle type{attribute.valid() ? H5Aget_type(attribute.id) : -1, H5Tclose};
  Handle space{attribute.valid() ? H5Aget_space(attribute.id) : -1, H5Sclose};
  if (!attribute.valid() || !type.valid() || !space.valid())
    return Result<std::array<std::int64_t, 2U>>::failure(
        hdf5_error(path, "opening /h5md version attribute"));
  if (H5Tget_class(type.id) != H5T_INTEGER ||
      H5Sget_simple_extent_ndims(space.id) != 1 ||
      H5Sget_simple_extent_npoints(space.id) != 2) {
    return Result<std::array<std::int64_t, 2U>>::failure(
        invalid(path, "/h5md version must be a two-element integer vector"));
  }
  std::array<std::int64_t, 2U> version{};
  if (H5Aread(attribute.id, H5T_NATIVE_INT64, version.data()) < 0)
    return Result<std::array<std::int64_t, 2U>>::failure(
        hdf5_error(path, "reading /h5md version"));
  if (version[0] < 0 || version[1] < 0)
    return Result<std::array<std::int64_t, 2U>>::failure(
        invalid(path, "/h5md version values must be non-negative"));
  return Result<std::array<std::int64_t, 2U>>::success(version);
}

Result<std::vector<std::string>>
direct_child_groups(hid_t parent, const std::filesystem::path &path) {
  H5G_info_t info{};
  if (H5Gget_info(parent, &info) < 0)
    return Result<std::vector<std::string>>::failure(
        hdf5_error(path, "listing particle groups"));
  std::vector<std::string> result;
  for (hsize_t index = 0U; index < info.nlinks; ++index) {
    const auto length =
        H5Lget_name_by_idx(parent, ".", H5_INDEX_NAME, H5_ITER_INC, index,
                           nullptr, 0U, H5P_DEFAULT);
    if (length < 0)
      return Result<std::vector<std::string>>::failure(
          hdf5_error(path, "reading particle group name"));
    std::string name(static_cast<std::size_t>(length) + 1U, '\0');
    if (H5Lget_name_by_idx(parent, ".", H5_INDEX_NAME, H5_ITER_INC, index,
                           name.data(), name.size(), H5P_DEFAULT) < 0) {
      return Result<std::vector<std::string>>::failure(
          hdf5_error(path, "reading particle group name"));
    }
    name.resize(static_cast<std::size_t>(length));
    Handle object{H5Oopen(parent, name.c_str(), H5P_DEFAULT), H5Oclose};
    if (object.valid() && H5Iget_type(object.id) == H5I_GROUP)
      result.push_back(std::move(name));
  }
  return Result<std::vector<std::string>>::success(std::move(result));
}

struct Dataset {
  Handle handle;
  std::vector<std::size_t> shape;
  H5T_class_t type_class{H5T_NO_CLASS};
  std::size_t type_size{};
  std::optional<double> double_fill;
  std::optional<std::int64_t> integer_fill;
  std::string unit;
};

Result<Dataset> inspect_dataset(hid_t parent, std::string_view name,
                                const std::filesystem::path &path) {
  auto opened = open_dataset(parent, name, path);
  if (!opened.has_value())
    return Result<Dataset>::failure(opened.error());
  Dataset result;
  result.handle = std::move(opened.value());
  Handle space{H5Dget_space(result.handle.id), H5Sclose};
  Handle type{H5Dget_type(result.handle.id), H5Tclose};
  if (!space.valid() || !type.valid())
    return Result<Dataset>::failure(
        hdf5_error(path, "inspecting dataset " + std::string{name}));
  const auto rank = H5Sget_simple_extent_ndims(space.id);
  if (rank < 0)
    return Result<Dataset>::failure(
        hdf5_error(path, "reading dataset rank " + std::string{name}));
  std::vector<hsize_t> shape(static_cast<std::size_t>(rank));
  if (rank > 0 &&
      H5Sget_simple_extent_dims(space.id, shape.data(), nullptr) < 0)
    return Result<Dataset>::failure(
        hdf5_error(path, "reading dataset shape " + std::string{name}));
  result.shape.reserve(shape.size());
  for (const auto value : shape) {
    if (value > static_cast<hsize_t>(std::numeric_limits<std::size_t>::max()))
      return Result<Dataset>::failure(
          invalid(path, "dataset " + std::string{name} + " is too large"));
    result.shape.push_back(static_cast<std::size_t>(value));
  }
  result.type_class = H5Tget_class(type.id);
  result.type_size = H5Tget_size(type.id);
  if (result.type_class != H5T_FLOAT && result.type_class != H5T_INTEGER)
    return Result<Dataset>::failure(
        invalid(path, "dataset " + std::string{name} + " must be numeric"));
  auto unit = scalar_string_attribute(result.handle.id, "unit", path);
  if (!unit.has_value())
    return Result<Dataset>::failure(unit.error());
  if (unit.value().has_value())
    result.unit = std::move(*unit.value());
  Handle creation{H5Dget_create_plist(result.handle.id), H5Pclose};
  if (!creation.valid())
    return Result<Dataset>::failure(
        hdf5_error(path, "reading dataset properties " + std::string{name}));
  const auto external_storage_count = H5Pget_external_count(creation.id);
  if (external_storage_count < 0)
    return Result<Dataset>::failure(
        hdf5_error(path, "checking external storage " + std::string{name}));
  if (external_storage_count != 0 ||
      H5Pget_layout(creation.id) == H5D_VIRTUAL) {
    return Result<Dataset>::failure(invalid(
        path,
        "dataset " + std::string{name} +
            " references storage outside the selected H5MD file",
        "materialize external or virtual data into one self-contained H5MD "
        "file"));
  }
  H5D_fill_value_t fill_state{H5D_FILL_VALUE_UNDEFINED};
  if (H5Pfill_value_defined(creation.id, &fill_state) < 0)
    return Result<Dataset>::failure(
        hdf5_error(path, "reading fill value state " + std::string{name}));
  if (fill_state == H5D_FILL_VALUE_USER_DEFINED) {
    if (result.type_class == H5T_FLOAT) {
      double fill{};
      if (H5Pget_fill_value(creation.id, H5T_NATIVE_DOUBLE, &fill) < 0)
        return Result<Dataset>::failure(
            hdf5_error(path, "reading fill value " + std::string{name}));
      result.double_fill = fill;
    } else {
      std::int64_t fill{};
      if (H5Pget_fill_value(creation.id, H5T_NATIVE_INT64, &fill) < 0)
        return Result<Dataset>::failure(
            hdf5_error(path, "reading fill value " + std::string{name}));
      result.integer_fill = fill;
    }
  }
  return Result<Dataset>::success(std::move(result));
}

Result<std::vector<double>>
read_double_values(const Dataset &dataset, std::optional<std::size_t> frame,
                   const std::filesystem::path &path, std::string_view name) {
  if (dataset.type_class != H5T_FLOAT)
    return Result<std::vector<double>>::failure(
        invalid(path, std::string{name} + " must use a floating-point type"));
  std::size_t count = 1U;
  const auto first_dimension = frame.has_value() ? 1U : 0U;
  for (std::size_t dimension = first_dimension;
       dimension < dataset.shape.size(); ++dimension) {
    if (dataset.shape[dimension] != 0U &&
        count > std::numeric_limits<std::size_t>::max() /
                    dataset.shape[dimension]) {
      return Result<std::vector<double>>::failure(
          invalid(path, std::string{name} + " slice is too large"));
    }
    count *= dataset.shape[dimension];
  }
  std::vector<double> values(count);
  if (!frame.has_value()) {
    if (H5Dread(dataset.handle.id, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL,
                H5P_DEFAULT, values.data()) < 0) {
      return Result<std::vector<double>>::failure(
          hdf5_error(path, "reading " + std::string{name}));
    }
    return Result<std::vector<double>>::success(std::move(values));
  }
  if (dataset.shape.empty() || *frame >= dataset.shape.front())
    return Result<std::vector<double>>::failure(
        invalid(path, std::string{name} + " frame is out of range"));
  Handle file_space{H5Dget_space(dataset.handle.id), H5Sclose};
  if (!file_space.valid())
    return Result<std::vector<double>>::failure(
        hdf5_error(path, "opening " + std::string{name} + " dataspace"));
  std::vector<hsize_t> start(dataset.shape.size(), 0U);
  std::vector<hsize_t> selection(dataset.shape.size());
  start.front() = static_cast<hsize_t>(*frame);
  for (std::size_t index = 0U; index < dataset.shape.size(); ++index)
    selection[index] = index == 0U ? 1U : dataset.shape[index];
  if (H5Sselect_hyperslab(file_space.id, H5S_SELECT_SET, start.data(), nullptr,
                          selection.data(), nullptr) < 0) {
    return Result<std::vector<double>>::failure(
        hdf5_error(path, "selecting " + std::string{name} + " frame"));
  }
  std::vector<hsize_t> memory_shape(selection.begin() + 1, selection.end());
  if (memory_shape.empty())
    memory_shape.push_back(1U);
  Handle memory_space{H5Screate_simple(static_cast<int>(memory_shape.size()),
                                       memory_shape.data(), nullptr),
                      H5Sclose};
  if (!memory_space.valid() ||
      H5Dread(dataset.handle.id, H5T_NATIVE_DOUBLE, memory_space.id,
              file_space.id, H5P_DEFAULT, values.data()) < 0) {
    return Result<std::vector<double>>::failure(
        hdf5_error(path, "reading " + std::string{name} + " frame"));
  }
  return Result<std::vector<double>>::success(std::move(values));
}

Result<std::vector<std::int64_t>>
read_integer_values(const Dataset &dataset, std::optional<std::size_t> frame,
                    const std::filesystem::path &path, std::string_view name) {
  if (dataset.type_class != H5T_INTEGER)
    return Result<std::vector<std::int64_t>>::failure(
        invalid(path, std::string{name} + " must use an integer type"));
  std::size_t count = 1U;
  const auto first_dimension = frame.has_value() ? 1U : 0U;
  for (std::size_t dimension = first_dimension;
       dimension < dataset.shape.size(); ++dimension) {
    if (dataset.shape[dimension] != 0U &&
        count > std::numeric_limits<std::size_t>::max() /
                    dataset.shape[dimension]) {
      return Result<std::vector<std::int64_t>>::failure(
          invalid(path, std::string{name} + " slice is too large"));
    }
    count *= dataset.shape[dimension];
  }
  std::vector<std::int64_t> values(count);
  if (!frame.has_value()) {
    if (H5Dread(dataset.handle.id, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL,
                H5P_DEFAULT, values.data()) < 0) {
      return Result<std::vector<std::int64_t>>::failure(
          hdf5_error(path, "reading " + std::string{name}));
    }
    return Result<std::vector<std::int64_t>>::success(std::move(values));
  }
  if (dataset.shape.empty() || *frame >= dataset.shape.front())
    return Result<std::vector<std::int64_t>>::failure(
        invalid(path, std::string{name} + " frame is out of range"));
  Handle file_space{H5Dget_space(dataset.handle.id), H5Sclose};
  std::vector<hsize_t> start(dataset.shape.size(), 0U);
  std::vector<hsize_t> selection(dataset.shape.size());
  start.front() = static_cast<hsize_t>(*frame);
  for (std::size_t index = 0U; index < dataset.shape.size(); ++index)
    selection[index] = index == 0U ? 1U : dataset.shape[index];
  if (!file_space.valid() ||
      H5Sselect_hyperslab(file_space.id, H5S_SELECT_SET, start.data(), nullptr,
                          selection.data(), nullptr) < 0) {
    return Result<std::vector<std::int64_t>>::failure(
        hdf5_error(path, "selecting " + std::string{name} + " frame"));
  }
  std::vector<hsize_t> memory_shape(selection.begin() + 1, selection.end());
  if (memory_shape.empty())
    memory_shape.push_back(1U);
  Handle memory_space{H5Screate_simple(static_cast<int>(memory_shape.size()),
                                       memory_shape.data(), nullptr),
                      H5Sclose};
  if (!memory_space.valid() ||
      H5Dread(dataset.handle.id, H5T_NATIVE_INT64, memory_space.id,
              file_space.id, H5P_DEFAULT, values.data()) < 0) {
    return Result<std::vector<std::int64_t>>::failure(
        hdf5_error(path, "reading " + std::string{name} + " frame"));
  }
  return Result<std::vector<std::int64_t>>::success(std::move(values));
}

struct UnitDimensions {
  double factor{1.0};
  int length_power{};
  int time_power{};
  bool supported{true};
};

std::optional<std::pair<std::string, int>> split_unit_power(std::string token) {
  if (token.empty())
    return std::nullopt;
  std::size_t power_begin = token.size();
  for (std::size_t index = 1U; index < token.size(); ++index) {
    if ((token[index] == '+' || token[index] == '-') &&
        index + 1U < token.size() &&
        std::all_of(token.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                    token.end(), [](unsigned char value) {
                      return value >= '0' && value <= '9';
                    })) {
      power_begin = index;
      break;
    }
  }
  int power = 1;
  if (power_begin != token.size()) {
    const auto power_text = std::string_view{token}.substr(power_begin);
    const auto parsed = std::from_chars(
        power_text.data(), power_text.data() + power_text.size(), power);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != power_text.data() + power_text.size() || power == 0)
      return std::nullopt;
    token.resize(power_begin);
  }
  std::transform(token.begin(), token.end(), token.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  return std::pair<std::string, int>{std::move(token), power};
}

std::optional<double> numeric_unit_factor(std::string_view token) {
  double value{};
  const auto plain =
      std::from_chars(token.data(), token.data() + token.size(), value);
  if (plain.ec == std::errc{} && plain.ptr == token.data() + token.size() &&
      std::isfinite(value) && value > 0.0)
    return value;
  const auto exponent_sign = token.find_first_of("+-", 1U);
  if (exponent_sign == std::string_view::npos)
    return std::nullopt;
  double base{};
  int exponent{};
  const auto base_result =
      std::from_chars(token.data(), token.data() + exponent_sign, base);
  const auto exponent_result = std::from_chars(
      token.data() + exponent_sign, token.data() + token.size(), exponent);
  if (base_result.ec != std::errc{} ||
      base_result.ptr != token.data() + exponent_sign ||
      exponent_result.ec != std::errc{} ||
      exponent_result.ptr != token.data() + token.size())
    return std::nullopt;
  const auto result = std::pow(base, exponent);
  if (!std::isfinite(result) || result <= 0.0)
    return std::nullopt;
  return result;
}

std::optional<UnitDimensions> parse_unit(std::string unit) {
  if (unit.find('/') != std::string::npos) {
    std::replace(unit.begin(), unit.end(), '/', ' ');
    const auto separator = unit.find(' ');
    if (separator != std::string::npos) {
      auto remainder = separator + 1U;
      while (remainder < unit.size() && unit[remainder] == ' ')
        ++remainder;
      if (remainder < unit.size() &&
          unit.find_first_of("+-", remainder) == std::string::npos)
        unit += "-1";
    }
  }
  std::istringstream stream{unit};
  std::string token;
  UnitDimensions result;
  bool saw_unit = false;
  bool saw_numeric = false;
  while (stream >> token) {
    if (const auto numeric = numeric_unit_factor(token); numeric.has_value()) {
      if (saw_unit || saw_numeric)
        return std::nullopt;
      result.factor *= *numeric;
      saw_numeric = true;
      continue;
    }
    saw_unit = true;
    const auto split = split_unit_power(std::move(token));
    if (!split.has_value())
      return std::nullopt;
    const auto &[symbol, power] = *split;
    double factor{};
    if (symbol == "m") {
      factor = 1.0e10;
      result.length_power += power;
    } else if (symbol == "dm") {
      factor = 1.0e9;
      result.length_power += power;
    } else if (symbol == "cm") {
      factor = 1.0e8;
      result.length_power += power;
    } else if (symbol == "mm") {
      factor = 1.0e7;
      result.length_power += power;
    } else if (symbol == "um") {
      factor = 1.0e4;
      result.length_power += power;
    } else if (symbol == "nm" || symbol == "nanometer" ||
               symbol == "nanometers") {
      factor = 10.0;
      result.length_power += power;
    } else if (symbol == "pm") {
      factor = 0.01;
      result.length_power += power;
    } else if (symbol == "angstrom" || symbol == "angstroms" ||
               symbol == "ang" || symbol == "a") {
      factor = 1.0;
      result.length_power += power;
    } else if (symbol == "s") {
      factor = 1.0e12;
      result.time_power += power;
    } else if (symbol == "ms") {
      factor = 1.0e9;
      result.time_power += power;
    } else if (symbol == "us") {
      factor = 1.0e6;
      result.time_power += power;
    } else if (symbol == "ns") {
      factor = 1.0e3;
      result.time_power += power;
    } else if (symbol == "ps" || symbol == "picosecond" ||
               symbol == "picoseconds") {
      factor = 1.0;
      result.time_power += power;
    } else if (symbol == "fs" || symbol == "femtosecond" ||
               symbol == "femtoseconds") {
      factor = 0.001;
      result.time_power += power;
    } else {
      result.supported = false;
      continue;
    }
    result.factor *= std::pow(factor, power);
  }
  if (!saw_unit || !std::isfinite(result.factor) || result.factor <= 0.0)
    return std::nullopt;
  return result;
}

Result<double> length_factor_to_angstrom(
    const std::string &unit, std::optional<operation::LengthUnit> unit_override,
    const std::filesystem::path &path, std::string_view channel) {
  if (unit.empty()) {
    if (!unit_override.has_value())
      return Result<double>::failure(invalid(
          path, std::string{channel} + " has no unit attribute",
          "set --coordinate-unit angstrom or nanometer to describe the stored "
          "coordinates"));
    return Result<double>::success(
        *unit_override == operation::LengthUnit::angstrom ? 1.0 : 10.0);
  }
  const auto parsed = parse_unit(unit);
  if (!parsed.has_value() || !parsed->supported || parsed->length_power != 1 ||
      parsed->time_power != 0) {
    return Result<double>::failure(
        invalid(path,
                std::string{channel} + " unit '" + unit +
                    "' is not a supported length unit",
                "use an H5MD SI length unit such as m, nm, pm or Angstrom"));
  }
  return Result<double>::success(parsed->factor);
}

Result<double>
velocity_factor_to_angstrom_per_ps(const std::string &unit,
                                   const std::filesystem::path &path) {
  if (unit.empty())
    return Result<double>::failure(
        invalid(path, "velocity has no unit attribute",
                "store an H5MD velocity unit such as 'nm ps-1'"));
  const auto parsed = parse_unit(unit);
  if (!parsed.has_value() || !parsed->supported || parsed->length_power != 1 ||
      parsed->time_power != -1) {
    return Result<double>::failure(
        invalid(path, "velocity unit '" + unit +
                          "' is not a supported length/time unit"));
  }
  return Result<double>::success(parsed->factor);
}

std::optional<double> time_factor_to_picoseconds(const std::string &unit) {
  if (unit.empty())
    return std::nullopt;
  const auto parsed = parse_unit(unit);
  if (!parsed.has_value() || !parsed->supported || parsed->length_power != 0 ||
      parsed->time_power != 1)
    return std::nullopt;
  return parsed->factor;
}

struct Timeline {
  std::vector<std::uint64_t> steps;
  std::vector<double> times;
  std::string time_unit;
};

Result<Timeline> inspect_timeline(hid_t group, std::size_t frame_count,
                                  const std::filesystem::path &path,
                                  std::string_view element_name) {
  if (!link_exists(group, "step"))
    return Result<Timeline>::failure(
        invalid(path, std::string{element_name} + " group is missing step"));
  auto step = inspect_dataset(group, "step", path);
  if (!step.has_value())
    return Result<Timeline>::failure(step.error());
  if (step.value().type_class != H5T_INTEGER)
    return Result<Timeline>::failure(
        invalid(path, std::string{element_name} + " step must be integer"));
  Timeline timeline;
  timeline.steps.reserve(frame_count);
  if (step.value().shape.empty()) {
    auto increment_values =
        read_integer_values(step.value(), std::nullopt, path, "step");
    if (!increment_values.has_value())
      return Result<Timeline>::failure(increment_values.error());
    const auto increment = increment_values.value().front();
    auto offset = numeric_attribute<std::int64_t>(
        step.value().handle.id, "offset", H5T_NATIVE_INT64, path);
    if (!offset.has_value())
      return Result<Timeline>::failure(offset.error());
    const auto start = offset.value().value_or(0);
    if (start < 0 || (frame_count > 1U && increment <= 0))
      return Result<Timeline>::failure(invalid(
          path, std::string{element_name} +
                    " fixed step increment/offset must produce non-negative "
                    "increasing steps"));
    for (std::size_t frame = 0U; frame < frame_count; ++frame) {
      const auto frame64 = static_cast<std::uint64_t>(frame);
      const auto increment64 = static_cast<std::uint64_t>(increment);
      const auto start64 = static_cast<std::uint64_t>(start);
      if (increment > 0 &&
          frame64 > (std::numeric_limits<std::uint64_t>::max() - start64) /
                        increment64) {
        return Result<Timeline>::failure(
            invalid(path, std::string{element_name} + " step overflows"));
      }
      timeline.steps.push_back(start64 + frame64 * increment64);
    }
  } else {
    if (step.value().shape != std::vector<std::size_t>{frame_count})
      return Result<Timeline>::failure(
          invalid(path, std::string{element_name} +
                            " step vector must match its value frame count"));
    auto values = read_integer_values(step.value(), std::nullopt, path, "step");
    if (!values.has_value())
      return Result<Timeline>::failure(values.error());
    for (const auto value : values.value()) {
      if (value < 0)
        return Result<Timeline>::failure(
            invalid(path, std::string{element_name} +
                              " step values must be non-negative"));
      timeline.steps.push_back(static_cast<std::uint64_t>(value));
    }
  }
  if (!std::is_sorted(timeline.steps.begin(), timeline.steps.end()) ||
      std::adjacent_find(timeline.steps.begin(), timeline.steps.end()) !=
          timeline.steps.end()) {
    return Result<Timeline>::failure(
        invalid(path, std::string{element_name} +
                          " steps must be strictly increasing"));
  }

  if (!link_exists(group, "time"))
    return Result<Timeline>::success(std::move(timeline));
  auto time = inspect_dataset(group, "time", path);
  if (!time.has_value())
    return Result<Timeline>::failure(time.error());
  if (time.value().type_class != H5T_FLOAT)
    return Result<Timeline>::failure(invalid(
        path, std::string{element_name} + " time must be floating-point"));
  timeline.time_unit = time.value().unit;
  timeline.times.reserve(frame_count);
  if (time.value().shape.empty()) {
    auto increment_values =
        read_double_values(time.value(), std::nullopt, path, "time");
    if (!increment_values.has_value())
      return Result<Timeline>::failure(increment_values.error());
    auto offset = numeric_attribute<double>(time.value().handle.id, "offset",
                                            H5T_NATIVE_DOUBLE, path);
    if (!offset.has_value())
      return Result<Timeline>::failure(offset.error());
    const auto increment = increment_values.value().front();
    const auto start = offset.value().value_or(0.0);
    if (!std::isfinite(start) || !std::isfinite(increment) ||
        (frame_count > 1U && increment <= 0.0)) {
      return Result<Timeline>::failure(invalid(
          path,
          std::string{element_name} +
              " fixed time increment/offset must be finite and increasing"));
    }
    for (std::size_t frame = 0U; frame < frame_count; ++frame) {
      const auto value = start + static_cast<double>(frame) * increment;
      if (!std::isfinite(value))
        return Result<Timeline>::failure(
            invalid(path, std::string{element_name} + " time overflows"));
      timeline.times.push_back(value);
    }
  } else {
    if (time.value().shape != std::vector<std::size_t>{frame_count})
      return Result<Timeline>::failure(
          invalid(path, std::string{element_name} +
                            " time vector must match its value frame count"));
    auto values = read_double_values(time.value(), std::nullopt, path, "time");
    if (!values.has_value())
      return Result<Timeline>::failure(values.error());
    for (const auto value : values.value()) {
      if (!std::isfinite(value))
        return Result<Timeline>::failure(
            invalid(path, std::string{element_name} +
                              " time contains a non-finite value"));
      timeline.times.push_back(value);
    }
  }
  return Result<Timeline>::success(std::move(timeline));
}

struct Element {
  Dataset value;
  bool time_dependent{};
  Timeline timeline;
  std::vector<std::optional<std::size_t>> frame_map;
};

Result<std::optional<Element>>
inspect_element(hid_t particle, std::string_view name,
                std::size_t position_frame_count,
                const std::vector<std::uint64_t> &position_steps,
                const std::filesystem::path &path) {
  if (!link_exists(particle, name))
    return Result<std::optional<Element>>::success(std::nullopt);
  const std::string owned{name};
  Handle object{H5Oopen(particle, owned.c_str(), H5P_DEFAULT), H5Oclose};
  if (!object.valid())
    return Result<std::optional<Element>>::failure(
        hdf5_error(path, "opening element " + owned));
  Element element;
  if (H5Iget_type(object.id) == H5I_DATASET) {
    auto dataset = inspect_dataset(particle, name, path);
    if (!dataset.has_value())
      return Result<std::optional<Element>>::failure(dataset.error());
    element.value = std::move(dataset.value());
    element.frame_map.assign(position_frame_count, 0U);
    return Result<std::optional<Element>>::success(std::move(element));
  }
  if (H5Iget_type(object.id) != H5I_GROUP)
    return Result<std::optional<Element>>::failure(
        invalid(path, "element " + owned + " must be a dataset or group"));
  auto group_handle = open_group(particle, name, path);
  if (!group_handle.has_value())
    return Result<std::optional<Element>>::failure(group_handle.error());
  if (!link_exists(group_handle.value().id, "value"))
    return Result<std::optional<Element>>::failure(
        invalid(path, "element " + owned + " group is missing value"));
  auto dataset = inspect_dataset(group_handle.value().id, "value", path);
  if (!dataset.has_value())
    return Result<std::optional<Element>>::failure(dataset.error());
  if (dataset.value().shape.empty() || dataset.value().shape.front() == 0U)
    return Result<std::optional<Element>>::failure(
        invalid(path, "element " + owned + " has no frames"));
  const auto element_frame_count = dataset.value().shape.front();
  auto timeline = inspect_timeline(group_handle.value().id, element_frame_count,
                                   path, name);
  if (!timeline.has_value())
    return Result<std::optional<Element>>::failure(timeline.error());
  element.value = std::move(dataset.value());
  element.time_dependent = true;
  element.timeline = std::move(timeline.value());
  element.frame_map.resize(position_frame_count);
  if (position_steps.empty()) {
    if (position_frame_count != element_frame_count)
      return Result<std::optional<Element>>::failure(
          invalid(path, "time-dependent element " + owned +
                            " cannot align to time-independent positions"));
    for (std::size_t frame = 0U; frame < position_frame_count; ++frame)
      element.frame_map[frame] = frame;
  } else {
    for (std::size_t frame = 0U; frame < position_steps.size(); ++frame) {
      const auto found =
          std::lower_bound(element.timeline.steps.begin(),
                           element.timeline.steps.end(), position_steps[frame]);
      if (found != element.timeline.steps.end() &&
          *found == position_steps[frame]) {
        element.frame_map[frame] = static_cast<std::size_t>(
            std::distance(element.timeline.steps.begin(), found));
      }
    }
  }
  return Result<std::optional<Element>>::success(std::move(element));
}

Result<Element> inspect_position(hid_t particle,
                                 const std::filesystem::path &path) {
  if (!link_exists(particle, "position"))
    return Result<Element>::failure(
        invalid(path, "selected particle group has no position element"));
  Handle object{H5Oopen(particle, "position", H5P_DEFAULT), H5Oclose};
  if (!object.valid())
    return Result<Element>::failure(hdf5_error(path, "opening position"));
  Element position;
  if (H5Iget_type(object.id) == H5I_DATASET) {
    auto dataset = inspect_dataset(particle, "position", path);
    if (!dataset.has_value())
      return Result<Element>::failure(dataset.error());
    if (dataset.value().shape.size() != 2U || dataset.value().shape[0] == 0U ||
        dataset.value().shape[1] != 3U) {
      return Result<Element>::failure(
          invalid(path, "time-independent position must have shape [N,3]"));
    }
    position.value = std::move(dataset.value());
    position.frame_map = {0U};
    return Result<Element>::success(std::move(position));
  }
  if (H5Iget_type(object.id) != H5I_GROUP)
    return Result<Element>::failure(
        invalid(path, "position must be a dataset or time-dependent group"));
  auto position_group = open_group(particle, "position", path);
  if (!position_group.has_value())
    return Result<Element>::failure(position_group.error());
  auto dataset = inspect_dataset(position_group.value().id, "value", path);
  if (!dataset.has_value())
    return Result<Element>::failure(dataset.error());
  if (dataset.value().shape.size() != 3U || dataset.value().shape[0] == 0U ||
      dataset.value().shape[1] == 0U || dataset.value().shape[2] != 3U) {
    return Result<Element>::failure(
        invalid(path, "time-dependent position value must have shape [F,N,3]"));
  }
  auto timeline = inspect_timeline(position_group.value().id,
                                   dataset.value().shape[0], path, "position");
  if (!timeline.has_value())
    return Result<Element>::failure(timeline.error());
  position.value = std::move(dataset.value());
  position.time_dependent = true;
  position.timeline = std::move(timeline.value());
  position.frame_map.resize(position.value.shape[0]);
  for (std::size_t frame = 0U; frame < position.frame_map.size(); ++frame)
    position.frame_map[frame] = frame;
  return Result<Element>::success(std::move(position));
}

std::size_t position_frame_count(const Element &position) {
  return position.time_dependent ? position.value.shape[0] : 1U;
}

std::size_t position_particle_count(const Element &position) {
  return position.time_dependent ? position.value.shape[1]
                                 : position.value.shape[0];
}

Result<std::optional<std::size_t>>
element_frame(const Element &element, std::size_t position_frame,
              const std::filesystem::path &path, std::string_view name,
              bool required) {
  if (!element.time_dependent)
    return Result<std::optional<std::size_t>>::success(std::nullopt);
  if (position_frame >= element.frame_map.size())
    return Result<std::optional<std::size_t>>::failure(
        invalid(path, std::string{name} + " frame map is out of range"));
  if (!element.frame_map[position_frame].has_value() && required)
    return Result<std::optional<std::size_t>>::failure(
        invalid(path, std::string{name} +
                          " has no sample at the position step for frame " +
                          std::to_string(position_frame)));
  return Result<std::optional<std::size_t>>::success(
      element.frame_map[position_frame]);
}

bool missing(double value, const Dataset &dataset) {
  if (!dataset.double_fill.has_value())
    return false;
  if (std::isnan(*dataset.double_fill))
    return std::isnan(value);
  return value == *dataset.double_fill;
}

struct BoxState {
  std::array<std::string, 3U> boundary;
  std::optional<Element> edges;
  double factor_to_angstrom{1.0};
  bool periodic{};
};

struct ReaderState {
  Element position;
  std::optional<Element> velocity;
  std::optional<Element> force;
  std::optional<Element> id;
  std::optional<Element> mass;
  std::optional<Element> charge;
  std::optional<Element> species;
  std::optional<Element> image;
  BoxState box;
  double position_factor_to_angstrom{1.0};
  double velocity_factor_to_angstrom_per_ps{1.0};

  void close() noexcept {
    position.value.handle.reset();
    const auto close_element = [](std::optional<Element> &element) {
      if (element.has_value())
        element->value.handle.reset();
    };
    close_element(velocity);
    close_element(force);
    close_element(id);
    close_element(mass);
    close_element(charge);
    close_element(species);
    close_element(image);
    close_element(box.edges);
  }
};

Result<BoxState>
inspect_box(hid_t particle, const Element &position,
            const std::filesystem::path &path,
            std::optional<operation::LengthUnit> coordinate_unit_override) {
  if (!link_exists(particle, "box"))
    return Result<BoxState>::failure(
        invalid(path, "selected particle group has no mandatory box group"));
  auto box_group = open_group(particle, "box", path);
  if (!box_group.has_value())
    return Result<BoxState>::failure(box_group.error());
  auto dimension = numeric_attribute<std::int64_t>(
      box_group.value().id, "dimension", H5T_NATIVE_INT64, path);
  if (!dimension.has_value())
    return Result<BoxState>::failure(dimension.error());
  if (!dimension.value().has_value() || *dimension.value() != 3)
    return Result<BoxState>::failure(
        invalid(path, "box dimension must equal 3"));
  auto boundary = string_attribute(box_group.value().id, "boundary", path);
  if (!boundary.has_value())
    return Result<BoxState>::failure(boundary.error());
  if (boundary.value().size() != 3U)
    return Result<BoxState>::failure(
        invalid(path, "box boundary must contain three strings"));
  BoxState result;
  std::size_t periodic_count{};
  for (std::size_t index = 0U; index < 3U; ++index) {
    auto value = std::move(boundary.value()[index]);
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });
    if (value != "periodic" && value != "none")
      return Result<BoxState>::failure(
          invalid(path, "box boundary values must be 'periodic' or 'none'"));
    if (value == "periodic")
      ++periodic_count;
    result.boundary[index] = std::move(value);
  }
  if (periodic_count != 0U && periodic_count != 3U) {
    return Result<BoxState>::failure(
        invalid(path, "partially periodic H5MD boxes are not yet representable",
                "use a fully periodic or fully non-periodic particle group"));
  }
  result.periodic = periodic_count == 3U;
  const auto frames = position_frame_count(position);
  const auto &steps = position.timeline.steps;
  auto edges =
      inspect_element(box_group.value().id, "edges", frames, steps, path);
  if (!edges.has_value())
    return Result<BoxState>::failure(edges.error());
  if (result.periodic && !edges.value().has_value())
    return Result<BoxState>::failure(
        invalid(path, "fully periodic box is missing edges"));
  result.edges = std::move(edges.value());
  if (!result.edges.has_value())
    return Result<BoxState>::success(std::move(result));
  const auto &shape = result.edges->value.shape;
  const bool valid_static = !result.edges->time_dependent &&
                            (shape == std::vector<std::size_t>{3U} ||
                             shape == std::vector<std::size_t>{3U, 3U});
  const bool valid_dynamic =
      result.edges->time_dependent &&
      (shape.size() == 2U || shape.size() == 3U) && shape.front() > 0U &&
      ((shape.size() == 2U && shape[1] == 3U) ||
       (shape.size() == 3U && shape[1] == 3U && shape[2] == 3U));
  if (!valid_static && !valid_dynamic)
    return Result<BoxState>::failure(invalid(
        path, "box edges must have shape [3], [3,3], [F,3] or [F,3,3]"));
  if (result.edges->time_dependent &&
      std::any_of(result.edges->frame_map.begin(),
                  result.edges->frame_map.end(),
                  [](const auto &frame) { return !frame.has_value(); })) {
    return Result<BoxState>::failure(invalid(
        path, "time-dependent box edges must align with every position step"));
  }
  const auto &unit = result.edges->value.unit.empty()
                         ? position.value.unit
                         : result.edges->value.unit;
  auto factor = length_factor_to_angstrom(unit, coordinate_unit_override, path,
                                          "box edges");
  if (!factor.has_value())
    return Result<BoxState>::failure(factor.error());
  result.factor_to_angstrom = factor.value();
  return Result<BoxState>::success(std::move(result));
}

Result<std::vector<std::optional<std::size_t>>> build_mapping(
    const std::vector<std::int64_t> &ids, const Dataset &id_dataset,
    std::size_t stored_particle_count, std::size_t atom_count,
    const std::unordered_map<std::int64_t, std::size_t> &source_id_to_index,
    const std::filesystem::path &path, std::size_t frame) {
  if (ids.size() != stored_particle_count)
    return Result<std::vector<std::optional<std::size_t>>>::failure(
        invalid(path, "id element has an invalid particle dimension"));
  std::vector<std::optional<std::size_t>> mapping(stored_particle_count);
  std::vector<std::uint8_t> claimed(atom_count, 0U);
  for (std::size_t slot = 0U; slot < ids.size(); ++slot) {
    if (id_dataset.integer_fill.has_value() &&
        ids[slot] == *id_dataset.integer_fill)
      continue;
    const auto found = source_id_to_index.find(ids[slot]);
    if (found == source_id_to_index.end()) {
      return Result<std::vector<std::optional<std::size_t>>>::failure(invalid(
          path,
          "id " + std::to_string(ids[slot]) + " in frame " +
              std::to_string(frame) + " is absent from the active topology",
          "attach a topology with matching source atom IDs"));
    }
    if (claimed[found->second] != 0U)
      return Result<std::vector<std::optional<std::size_t>>>::failure(invalid(
          path, "id " + std::to_string(ids[slot]) + " is duplicated in frame " +
                    std::to_string(frame)));
    claimed[found->second] = 1U;
    mapping[slot] = found->second;
  }
  return Result<std::vector<std::optional<std::size_t>>>::success(
      std::move(mapping));
}

Result<model::CoordinateBuffer>
coordinate_buffer(const std::vector<model::Vec3d> &values,
                  std::size_t source_type_size,
                  const std::filesystem::path &path, std::string_view channel) {
  if (source_type_size <= sizeof(float)) {
    std::vector<model::Vec3f> converted;
    converted.reserve(values.size());
    for (const auto &value : values) {
      const model::Vec3f item{static_cast<float>(value.x),
                              static_cast<float>(value.y),
                              static_cast<float>(value.z)};
      if (!std::isfinite(item.x) || !std::isfinite(item.y) ||
          !std::isfinite(item.z)) {
        return Result<model::CoordinateBuffer>::failure(
            invalid(path, std::string{channel} +
                              " overflows float32 after unit conversion"));
      }
      converted.push_back(item);
    }
    return Result<model::CoordinateBuffer>::success(
        model::CoordinateBuffer{std::move(converted)});
  }
  return Result<model::CoordinateBuffer>::success(
      model::CoordinateBuffer{values});
}

Result<std::vector<model::Vec3d>>
scatter_vectors(const std::vector<double> &values, const Dataset &dataset,
                double factor,
                const std::vector<std::optional<std::size_t>> &mapping,
                std::size_t atom_count, std::vector<std::uint8_t> *presence,
                const std::filesystem::path &path, std::string_view channel) {
  if (values.size() != mapping.size() * 3U)
    return Result<std::vector<model::Vec3d>>::failure(
        invalid(path, std::string{channel} + " has an invalid vector shape"));
  std::vector<model::Vec3d> output(atom_count);
  if (presence != nullptr)
    presence->assign(atom_count, 0U);
  for (std::size_t slot = 0U; slot < mapping.size(); ++slot) {
    if (!mapping[slot].has_value())
      continue;
    const auto base = slot * 3U;
    if (!std::isfinite(values[base]) || !std::isfinite(values[base + 1U]) ||
        !std::isfinite(values[base + 2U]) || missing(values[base], dataset) ||
        missing(values[base + 1U], dataset) ||
        missing(values[base + 2U], dataset)) {
      return Result<std::vector<model::Vec3d>>::failure(invalid(
          path,
          std::string{channel} +
              " contains missing or non-finite data for a present particle"));
    }
    const model::Vec3d converted{values[base] * factor,
                                 values[base + 1U] * factor,
                                 values[base + 2U] * factor};
    if (!std::isfinite(converted.x) || !std::isfinite(converted.y) ||
        !std::isfinite(converted.z))
      return Result<std::vector<model::Vec3d>>::failure(invalid(
          path, std::string{channel} + " overflows after unit conversion"));
    output[*mapping[slot]] = converted;
    if (presence != nullptr)
      (*presence)[*mapping[slot]] = 1U;
  }
  return Result<std::vector<model::Vec3d>>::success(std::move(output));
}

class H5mdCoordinateSource final : public model::CoordinateSource {
public:
  H5mdCoordinateSource(
      std::filesystem::path path, Handle file, H5mdMetadata metadata,
      ReaderState state,
      std::unordered_map<std::int64_t, std::size_t> source_id_to_index,
      std::vector<std::optional<std::size_t>> static_mapping)
      : path_{std::move(path)}, file_{std::move(file)},
        metadata_{std::move(metadata)}, state_{std::move(state)},
        source_id_to_index_{std::move(source_id_to_index)},
        static_mapping_{std::move(static_mapping)} {}

  ~H5mdCoordinateSource() override {
    std::lock_guard lock{hdf5_mutex()};
    state_.close();
    file_.reset();
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
    if (frame >= metadata_.frame_count)
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          invalid(path_, "frame index is out of range"));
    std::lock_guard lock{hdf5_mutex()};

    auto mapping = mapping_for_frame(frame);
    if (!mapping.has_value())
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          mapping.error());
    const auto position_source_frame = state_.position.time_dependent
                                           ? std::optional<std::size_t>{frame}
                                           : std::nullopt;
    auto raw_positions = read_double_values(
        state_.position.value, position_source_frame, path_, "position/value");
    if (!raw_positions.has_value())
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          raw_positions.error());
    std::vector<std::uint8_t> presence;
    auto positions =
        scatter_vectors(raw_positions.value(), state_.position.value,
                        state_.position_factor_to_angstrom, mapping.value(),
                        metadata_.atom_count, &presence, path_, "position");
    if (!positions.has_value())
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          positions.error());
    auto position_buffer = coordinate_buffer(
        positions.value(), state_.position.value.type_size, path_, "position");
    if (!position_buffer.has_value())
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          position_buffer.error());

    model::FrameMetadata frame_metadata;
    frame_metadata.coordinate_unit = operation::LengthUnit::angstrom;
    frame_metadata.fields.emplace("format", "h5md");
    frame_metadata.fields.emplace("h5md.particle_group",
                                  metadata_.particle_group);
    frame_metadata.fields.emplace(
        "h5md.version", std::to_string(metadata_.version_major) + "." +
                            std::to_string(metadata_.version_minor));
    frame_metadata.fields.emplace("h5md.position_source_unit",
                                  metadata_.position_unit.empty()
                                      ? "unspecified"
                                      : metadata_.position_unit);
    frame_metadata.fields.emplace("h5md.boundary.x", state_.box.boundary[0]);
    frame_metadata.fields.emplace("h5md.boundary.y", state_.box.boundary[1]);
    frame_metadata.fields.emplace("h5md.boundary.z", state_.box.boundary[2]);
    if (state_.position.time_dependent) {
      frame_metadata.source_step = state_.position.timeline.steps[frame];
      if (!state_.position.timeline.times.empty()) {
        const auto raw_time = state_.position.timeline.times[frame];
        frame_metadata.fields.emplace("h5md.time", std::to_string(raw_time));
        frame_metadata.fields.emplace("h5md.time_unit",
                                      state_.position.timeline.time_unit.empty()
                                          ? "unspecified"
                                          : state_.position.timeline.time_unit);
        if (const auto factor =
                time_factor_to_picoseconds(state_.position.timeline.time_unit);
            factor.has_value()) {
          const auto converted = raw_time * *factor;
          if (!std::isfinite(converted))
            return Result<std::shared_ptr<const model::CoordinateFrame>>::
                failure(invalid(path_, "time overflows after unit conversion"));
          frame_metadata.physical_time =
              model::PhysicalTime{converted, model::TimeUnit::picosecond};
        }
      }
    }

    if (state_.id.has_value()) {
      auto id_frame = element_frame(*state_.id, frame, path_, "id", true);
      if (!id_frame.has_value())
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            id_frame.error());
      auto ids = read_integer_values(state_.id->value, id_frame.value(), path_,
                                     "id/value");
      if (!ids.has_value())
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            ids.error());
      std::vector<std::int64_t> ordered(metadata_.atom_count, 0);
      for (std::size_t slot = 0U; slot < mapping.value().size(); ++slot) {
        if (mapping.value()[slot].has_value())
          ordered[*mapping.value()[slot]] = ids.value()[slot];
      }
      frame_metadata.atom_properties.emplace(
          "h5md.id", model::AtomProperty{std::move(ordered),
                                         {std::nullopt, "H5MD id", {}}});
    }

    std::optional<model::CoordinateBuffer> velocities;
    if (state_.velocity.has_value()) {
      auto source_frame =
          element_frame(*state_.velocity, frame, path_, "velocity", false);
      if (!source_frame.has_value())
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            source_frame.error());
      const bool available =
          !state_.velocity->time_dependent || source_frame.value().has_value();
      if (available) {
        auto raw =
            read_double_values(state_.velocity->value, source_frame.value(),
                               path_, "velocity/value");
        if (!raw.has_value())
          return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
              raw.error());
        auto vectors = scatter_vectors(
            raw.value(), state_.velocity->value,
            state_.velocity_factor_to_angstrom_per_ps, mapping.value(),
            metadata_.atom_count, nullptr, path_, "velocity");
        if (!vectors.has_value())
          return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
              vectors.error());
        auto buffer =
            coordinate_buffer(vectors.value(), state_.velocity->value.type_size,
                              path_, "velocity");
        if (!buffer.has_value())
          return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
              buffer.error());
        velocities.emplace(std::move(buffer.value()));
        frame_metadata.velocity_time_unit = model::TimeUnit::picosecond;
      } else {
        frame_metadata.fields.emplace("h5md.velocity_sample", "unavailable");
      }
    }

    if (state_.force.has_value()) {
      auto source_frame =
          element_frame(*state_.force, frame, path_, "force", false);
      if (!source_frame.has_value())
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            source_frame.error());
      const bool available =
          !state_.force->time_dependent || source_frame.value().has_value();
      if (available) {
        auto raw = read_double_values(state_.force->value, source_frame.value(),
                                      path_, "force/value");
        if (!raw.has_value())
          return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
              raw.error());
        auto vectors = scatter_vectors(raw.value(), state_.force->value, 1.0,
                                       mapping.value(), metadata_.atom_count,
                                       nullptr, path_, "force");
        if (!vectors.has_value())
          return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
              vectors.error());
        const std::array<std::string, 3U> names{"h5md.force.x", "h5md.force.y",
                                                "h5md.force.z"};
        for (std::size_t component = 0U; component < 3U; ++component) {
          if (state_.force->value.type_size <= sizeof(float)) {
            std::vector<float> column;
            column.reserve(metadata_.atom_count);
            for (const auto &value : vectors.value())
              column.push_back(static_cast<float>(component == 0U   ? value.x
                                                  : component == 1U ? value.y
                                                                    : value.z));
            frame_metadata.atom_properties.emplace(
                names[component],
                model::AtomProperty{
                    std::move(column),
                    {state_.force->value.unit.empty()
                         ? std::nullopt
                         : std::optional<std::string>{state_.force->value.unit},
                     "H5MD force",
                     {}}});
          } else {
            std::vector<double> column;
            column.reserve(metadata_.atom_count);
            for (const auto &value : vectors.value())
              column.push_back(component == 0U   ? value.x
                               : component == 1U ? value.y
                                                 : value.z);
            frame_metadata.atom_properties.emplace(
                names[component],
                model::AtomProperty{
                    std::move(column),
                    {state_.force->value.unit.empty()
                         ? std::nullopt
                         : std::optional<std::string>{state_.force->value.unit},
                     "H5MD force",
                     {}}});
          }
        }
      } else {
        frame_metadata.fields.emplace("h5md.force_sample", "unavailable");
      }
    }

    if (const auto error =
            append_scalar_property(state_.mass, frame, mapping.value(),
                                   "h5md.mass", "H5MD mass", frame_metadata);
        error.has_value())
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          *error);
    if (const auto error = append_scalar_property(
            state_.charge, frame, mapping.value(), "h5md.charge", "H5MD charge",
            frame_metadata);
        error.has_value())
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          *error);
    if (const auto error = append_scalar_property(
            state_.species, frame, mapping.value(), "h5md.species",
            "H5MD species", frame_metadata);
        error.has_value())
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          *error);
    if (const auto error = append_image(frame, mapping.value(), frame_metadata);
        error.has_value())
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          *error);

    if (state_.box.periodic && state_.box.edges.has_value()) {
      auto source_frame =
          element_frame(*state_.box.edges, frame, path_, "box/edges", true);
      if (!source_frame.has_value())
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            source_frame.error());
      auto raw =
          read_double_values(state_.box.edges->value, source_frame.value(),
                             path_, "box/edges/value");
      if (!raw.has_value())
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            raw.error());
      model::UnitCell cell;
      if (raw.value().size() == 3U) {
        cell = {{raw.value()[0] * state_.box.factor_to_angstrom, 0.0, 0.0},
                {0.0, raw.value()[1] * state_.box.factor_to_angstrom, 0.0},
                {0.0, 0.0, raw.value()[2] * state_.box.factor_to_angstrom}};
      } else if (raw.value().size() == 9U) {
        const auto factor = state_.box.factor_to_angstrom;
        cell = {{raw.value()[0] * factor, raw.value()[1] * factor,
                 raw.value()[2] * factor},
                {raw.value()[3] * factor, raw.value()[4] * factor,
                 raw.value()[5] * factor},
                {raw.value()[6] * factor, raw.value()[7] * factor,
                 raw.value()[8] * factor}};
      } else {
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            invalid(path_, "box edge slice must contain 3 or 9 values"));
      }
      if (!cell.is_valid())
        return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
            invalid(path_,
                    "box edges do not form a finite positive-volume cell"));
      frame_metadata.unit_cell = cell;
    }

    auto created = model::CoordinateFrame::create(
        std::move(position_buffer.value()), std::move(velocities),
        std::move(presence), std::move(frame_metadata));
    if (!created.has_value())
      return Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
          created.error());
    return created;
  }

private:
  Result<std::vector<std::optional<std::size_t>>>
  mapping_for_frame(std::size_t frame) const {
    if (!state_.id.has_value())
      return Result<std::vector<std::optional<std::size_t>>>::success(
          static_mapping_);
    if (!state_.id->time_dependent)
      return Result<std::vector<std::optional<std::size_t>>>::success(
          static_mapping_);
    auto source_frame = element_frame(*state_.id, frame, path_, "id", true);
    if (!source_frame.has_value())
      return Result<std::vector<std::optional<std::size_t>>>::failure(
          source_frame.error());
    auto ids = read_integer_values(state_.id->value, source_frame.value(),
                                   path_, "id/value");
    if (!ids.has_value())
      return Result<std::vector<std::optional<std::size_t>>>::failure(
          ids.error());
    return build_mapping(ids.value(), state_.id->value,
                         metadata_.stored_particle_count, metadata_.atom_count,
                         source_id_to_index_, path_, frame);
  }

  std::optional<operation::Error> append_scalar_property(
      const std::optional<Element> &element, std::size_t frame,
      const std::vector<std::optional<std::size_t>> &mapping,
      std::string_view property_name, std::string_view source,
      model::FrameMetadata &metadata) const {
    if (!element.has_value())
      return std::nullopt;
    auto source_frame =
        element_frame(*element, frame, path_, property_name, false);
    if (!source_frame.has_value())
      return source_frame.error();
    if (element->time_dependent && !source_frame.value().has_value())
      return std::nullopt;
    if (element->value.type_class == H5T_INTEGER) {
      auto raw = read_integer_values(element->value, source_frame.value(),
                                     path_, property_name);
      if (!raw.has_value())
        return raw.error();
      if (raw.value().size() != mapping.size())
        return invalid(path_, std::string{property_name} +
                                  " has an invalid particle dimension");
      std::vector<std::int64_t> ordered(metadata_.atom_count, 0);
      for (std::size_t slot = 0U; slot < mapping.size(); ++slot) {
        if (mapping[slot].has_value())
          ordered[*mapping[slot]] = raw.value()[slot];
      }
      metadata.atom_properties.emplace(
          std::string{property_name},
          model::AtomProperty{
              std::move(ordered),
              {element->value.unit.empty()
                   ? std::nullopt
                   : std::optional<std::string>{element->value.unit},
               std::string{source},
               {}}});
      return std::nullopt;
    }
    auto raw = read_double_values(element->value, source_frame.value(), path_,
                                  property_name);
    if (!raw.has_value())
      return raw.error();
    if (raw.value().size() != mapping.size())
      return invalid(path_, std::string{property_name} +
                                " has an invalid particle dimension");
    std::vector<double> ordered(metadata_.atom_count, 0.0);
    for (std::size_t slot = 0U; slot < mapping.size(); ++slot) {
      if (!mapping[slot].has_value())
        continue;
      const auto value = raw.value()[slot];
      if (!std::isfinite(value) || missing(value, element->value))
        return invalid(path_, std::string{property_name} +
                                  " contains missing or non-finite data");
      ordered[*mapping[slot]] = value;
    }
    metadata.atom_properties.emplace(
        std::string{property_name},
        model::AtomProperty{
            std::move(ordered),
            {element->value.unit.empty()
                 ? std::nullopt
                 : std::optional<std::string>{element->value.unit},
             std::string{source},
             {}}});
    return std::nullopt;
  }

  std::optional<operation::Error>
  append_image(std::size_t frame,
               const std::vector<std::optional<std::size_t>> &mapping,
               model::FrameMetadata &metadata) const {
    if (!state_.image.has_value())
      return std::nullopt;
    auto source_frame =
        element_frame(*state_.image, frame, path_, "image", false);
    if (!source_frame.has_value())
      return source_frame.error();
    if (state_.image->time_dependent && !source_frame.value().has_value())
      return std::nullopt;
    auto raw = read_integer_values(state_.image->value, source_frame.value(),
                                   path_, "image/value");
    if (!raw.has_value())
      return raw.error();
    if (raw.value().size() != mapping.size() * 3U)
      return invalid(path_, "image has an invalid vector shape");
    const std::array<std::string, 3U> names{"h5md.image.x", "h5md.image.y",
                                            "h5md.image.z"};
    for (std::size_t component = 0U; component < 3U; ++component) {
      std::vector<std::int64_t> ordered(metadata_.atom_count, 0);
      for (std::size_t slot = 0U; slot < mapping.size(); ++slot) {
        if (mapping[slot].has_value())
          ordered[*mapping[slot]] = raw.value()[slot * 3U + component];
      }
      metadata.atom_properties.emplace(
          names[component],
          model::AtomProperty{std::move(ordered),
                              {std::nullopt, "H5MD image", {}}});
    }
    return std::nullopt;
  }

  std::filesystem::path path_;
  mutable Handle file_;
  H5mdMetadata metadata_;
  mutable ReaderState state_;
  std::unordered_map<std::int64_t, std::size_t> source_id_to_index_;
  std::vector<std::optional<std::size_t>> static_mapping_;
};

std::optional<operation::Error>
validate_element_shape(const std::optional<Element> &element,
                       std::size_t stored_particle_count,
                       const std::filesystem::path &path, std::string_view name,
                       std::size_t component_count) {
  if (!element.has_value())
    return std::nullopt;
  const auto &shape = element->value.shape;
  if (element->time_dependent) {
    if ((component_count == 1U &&
         (shape.size() != 2U || shape[1] != stored_particle_count)) ||
        (component_count == 3U &&
         (shape.size() != 3U || shape[1] != stored_particle_count ||
          shape[2] != 3U))) {
      return invalid(path, std::string{name} +
                               " value has an invalid time-dependent shape");
    }
  } else if ((component_count == 1U &&
              shape != std::vector<std::size_t>{stored_particle_count}) ||
             (component_count == 3U &&
              shape != std::vector<std::size_t>{stored_particle_count, 3U})) {
    return invalid(path, std::string{name} + " has an invalid static shape");
  }
  return std::nullopt;
}

} // namespace

operation::Result<std::shared_ptr<const model::CoordinateSource>>
open_h5md(const std::filesystem::path &path,
          std::optional<std::size_t> expected_atom_count,
          const std::vector<std::int64_t> &source_atom_ids,
          std::optional<operation::LengthUnit> coordinate_unit,
          std::optional<std::string> particle_group,
          H5mdMetadata *metadata_output) {
  std::error_code path_error;
  if (!std::filesystem::exists(path, path_error)) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        {operation::ErrorCode::not_found,
         "cannot open H5MD trajectory '" + path.string() + "'",
         "select an existing H5MD trajectory"});
  }
  std::lock_guard lock{hdf5_mutex()};
  hid_t file_id{-1};
  H5E_BEGIN_TRY {
    file_id = H5Fopen(path.string().c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
  }
  H5E_END_TRY;
  if (file_id < 0)
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "cannot open file as HDF5",
                "select an H5MD file or choose another trajectory format"));
  Handle file{file_id, H5Fclose};
  LinkAudit link_audit;
  if (H5Lvisit2(file.id, H5_INDEX_NAME, H5_ITER_NATIVE, audit_link,
                &link_audit) < 0)
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        hdf5_error(path, "auditing HDF5 links"));
  if (link_audit.external)
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "external HDF5 links are not followed",
                "materialize linked data into one self-contained H5MD file"));
  if (!link_exists(file.id, "h5md"))
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "missing mandatory /h5md group"));
  auto h5md = open_group(file.id, "h5md", path);
  if (!h5md.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        h5md.error());
  auto version = version_attribute(h5md.value().id, path);
  if (!version.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        version.error());
  if (version.value()[0] != 1)
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path,
                "unsupported H5MD major version " +
                    std::to_string(version.value()[0]),
                "use an H5MD 1.x file"));

  H5mdMetadata metadata;
  metadata.version_major = version.value()[0];
  metadata.version_minor = version.value()[1];
  if (!link_exists(h5md.value().id, "author") ||
      !link_exists(h5md.value().id, "creator")) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "/h5md must contain author and creator groups"));
  }
  auto author = open_group(h5md.value().id, "author", path);
  auto creator = open_group(h5md.value().id, "creator", path);
  if (!author.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        author.error());
  if (!creator.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        creator.error());
  auto author_name = scalar_string_attribute(author.value().id, "name", path);
  auto author_email = scalar_string_attribute(author.value().id, "email", path);
  auto creator_name = scalar_string_attribute(creator.value().id, "name", path);
  auto creator_version =
      scalar_string_attribute(creator.value().id, "version", path);
  if (!author_name.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        author_name.error());
  if (!author_email.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        author_email.error());
  if (!creator_name.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        creator_name.error());
  if (!creator_version.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        creator_version.error());
  if (!author_name.value().has_value() || !creator_name.value().has_value() ||
      !creator_version.value().has_value()) {
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "author.name, creator.name and creator.version are "
                      "required H5MD attributes"));
  }
  metadata.author_name = std::move(*author_name.value());
  if (author_email.value().has_value())
    metadata.author_email = std::move(*author_email.value());
  metadata.creator_name = std::move(*creator_name.value());
  metadata.creator_version = std::move(*creator_version.value());

  if (!link_exists(file.id, "particles"))
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "file has no /particles group"));
  auto particles = open_group(file.id, "particles", path);
  if (!particles.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        particles.error());
  auto groups = direct_child_groups(particles.value().id, path);
  if (!groups.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        groups.error());
  if (groups.value().empty())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "/particles contains no particle groups"));
  std::string selected_group;
  if (particle_group.has_value() && !particle_group->empty()) {
    if (particle_group->find('/') != std::string::npos ||
        std::find(groups.value().begin(), groups.value().end(),
                  *particle_group) == groups.value().end()) {
      return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
          invalid(path, "particle group '" + *particle_group +
                            "' does not exist directly under /particles"));
    }
    selected_group = std::move(*particle_group);
  } else if (std::find(groups.value().begin(), groups.value().end(),
                       "trajectory") != groups.value().end()) {
    selected_group = "trajectory";
  } else if (groups.value().size() == 1U) {
    selected_group = groups.value().front();
  } else {
    std::string choices;
    for (const auto &group : groups.value()) {
      if (!choices.empty())
        choices += ", ";
      choices += group;
    }
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path,
                "multiple particle groups require an explicit choice: " +
                    choices,
                "set --particle-group to one listed group"));
  }
  metadata.particle_group = selected_group;
  auto particle = open_group(particles.value().id, selected_group, path);
  if (!particle.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        particle.error());

  auto position = inspect_position(particle.value().id, path);
  if (!position.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        position.error());
  if (position.value().value.type_class != H5T_FLOAT)
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "position values must be floating-point"));
  const auto frame_count = position_frame_count(position.value());
  const auto stored_particle_count = position_particle_count(position.value());
  const auto atom_count = expected_atom_count.value_or(stored_particle_count);
  if (atom_count == 0U)
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "active topology atom count must be non-zero"));
  if (!source_atom_ids.empty() && source_atom_ids.size() != atom_count)
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "topology source atom ID count does not match its atom "
                      "count"));
  auto position_factor = length_factor_to_angstrom(
      position.value().value.unit, coordinate_unit, path, "position");
  if (!position_factor.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        position_factor.error());

  const auto inspect_optional = [&](std::string_view name) {
    return inspect_element(particle.value().id, name, frame_count,
                           position.value().timeline.steps, path);
  };
  auto velocity = inspect_optional("velocity");
  auto force = inspect_optional("force");
  auto ids = inspect_optional("id");
  auto mass = inspect_optional("mass");
  auto charge = inspect_optional("charge");
  auto species = inspect_optional("species");
  auto image = inspect_optional("image");
  if (!velocity.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        velocity.error());
  if (!force.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        force.error());
  if (!ids.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        ids.error());
  if (!mass.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        mass.error());
  if (!charge.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        charge.error());
  if (!species.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        species.error());
  if (!image.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        image.error());

  const std::array<std::pair<const std::optional<Element> *, std::string_view>,
                   4U>
      scalar_elements{{{&ids.value(), "id"},
                       {&mass.value(), "mass"},
                       {&charge.value(), "charge"},
                       {&species.value(), "species"}}};
  for (const auto &[element, name] : scalar_elements) {
    if (const auto error = validate_element_shape(
            *element, stored_particle_count, path, name, 1U);
        error.has_value())
      return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
          *error);
  }
  const std::array<std::pair<const std::optional<Element> *, std::string_view>,
                   3U>
      vector_elements{{{&velocity.value(), "velocity"},
                       {&force.value(), "force"},
                       {&image.value(), "image"}}};
  for (const auto &[element, name] : vector_elements) {
    if (const auto error = validate_element_shape(
            *element, stored_particle_count, path, name, 3U);
        error.has_value())
      return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
          *error);
  }
  if (ids.value().has_value() && ids.value()->value.type_class != H5T_INTEGER)
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "id must use an integer type"));
  if (species.value().has_value() &&
      species.value()->value.type_class != H5T_INTEGER)
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "species must use an integer type"));
  if (image.value().has_value() &&
      image.value()->value.type_class != H5T_INTEGER)
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "image must use an integer type"));
  if (velocity.value().has_value() &&
      velocity.value()->value.type_class != H5T_FLOAT)
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "velocity must use a floating-point type"));
  if (force.value().has_value() && force.value()->value.type_class != H5T_FLOAT)
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        invalid(path, "force must use a floating-point type"));

  double velocity_factor = 1.0;
  if (velocity.value().has_value()) {
    auto factor =
        velocity_factor_to_angstrom_per_ps(velocity.value()->value.unit, path);
    if (!factor.has_value())
      return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
          factor.error());
    velocity_factor = factor.value();
  }
  auto box =
      inspect_box(particle.value().id, position.value(), path, coordinate_unit);
  if (!box.has_value())
    return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
        box.error());

  std::unordered_map<std::int64_t, std::size_t> source_id_to_index;
  source_id_to_index.reserve(atom_count);
  if (!source_atom_ids.empty()) {
    for (std::size_t index = 0U; index < source_atom_ids.size(); ++index) {
      if (!source_id_to_index.emplace(source_atom_ids[index], index).second)
        return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
            invalid(path, "active topology source atom IDs are not unique"));
    }
  } else {
    for (std::size_t index = 0U; index < atom_count; ++index) {
      if (index >
          static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
        return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
            invalid(path, "atom count exceeds the supported ID range"));
      source_id_to_index.emplace(static_cast<std::int64_t>(index), index);
    }
  }

  std::vector<std::optional<std::size_t>> static_mapping;
  if (!ids.value().has_value()) {
    if (stored_particle_count != atom_count)
      return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
          invalid(path,
                  "position particle count " +
                      std::to_string(stored_particle_count) +
                      " does not match topology atom count " +
                      std::to_string(atom_count) + " and no id element exists",
                  "store H5MD particle IDs or attach a matching topology"));
    static_mapping.resize(atom_count);
    for (std::size_t index = 0U; index < atom_count; ++index)
      static_mapping[index] = index;
  } else if (!ids.value()->time_dependent) {
    auto raw_ids =
        read_integer_values(ids.value()->value, std::nullopt, path, "id");
    if (!raw_ids.has_value())
      return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
          raw_ids.error());
    auto mapping = build_mapping(raw_ids.value(), ids.value()->value,
                                 stored_particle_count, atom_count,
                                 source_id_to_index, path, 0U);
    if (!mapping.has_value())
      return Result<std::shared_ptr<const model::CoordinateSource>>::failure(
          mapping.error());
    static_mapping = std::move(mapping.value());
  }

  metadata.atom_count = atom_count;
  metadata.stored_particle_count = stored_particle_count;
  metadata.frame_count = frame_count;
  metadata.has_time = !position.value().timeline.times.empty();
  metadata.has_velocities = velocity.value().has_value();
  metadata.has_forces = force.value().has_value();
  metadata.has_ids = ids.value().has_value();
  metadata.dynamic_ids = ids.value().has_value() && ids.value()->time_dependent;
  metadata.has_mass = mass.value().has_value();
  metadata.has_charge = charge.value().has_value();
  metadata.has_species = species.value().has_value();
  metadata.has_unit_cell = box.value().periodic;
  metadata.time_dependent_box =
      box.value().edges.has_value() && box.value().edges->time_dependent;
  metadata.position_unit = position.value().value.unit;
  if (metadata.position_unit.empty() && coordinate_unit.has_value())
    metadata.position_unit = *coordinate_unit == operation::LengthUnit::angstrom
                                 ? "angstrom (caller override)"
                                 : "nm (caller override)";
  metadata.time_unit = position.value().timeline.time_unit;

  ReaderState state{std::move(position.value()),
                    std::move(velocity.value()),
                    std::move(force.value()),
                    std::move(ids.value()),
                    std::move(mass.value()),
                    std::move(charge.value()),
                    std::move(species.value()),
                    std::move(image.value()),
                    std::move(box.value()),
                    position_factor.value(),
                    velocity_factor};
  if (metadata_output != nullptr)
    *metadata_output = metadata;
  auto source = std::make_shared<H5mdCoordinateSource>(
      path, std::move(file), metadata, std::move(state),
      std::move(source_id_to_index), std::move(static_mapping));
  return Result<std::shared_ptr<const model::CoordinateSource>>::success(
      std::move(source));
}

} // namespace molshredder::io
