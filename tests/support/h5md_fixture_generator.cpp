#include <hdf5.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void checked(herr_t status, std::string_view operation) {
  if (status < 0)
    throw std::runtime_error{"HDF5 fixture " + std::string{operation} +
                             " failed"};
}

hid_t checked_id(hid_t id, std::string_view operation) {
  if (id < 0)
    throw std::runtime_error{"HDF5 fixture " + std::string{operation} +
                             " failed"};
  return id;
}

struct Handle {
  hid_t id{-1};
  herr_t (*close)(hid_t){};

  Handle() = default;
  Handle(hid_t value, herr_t (*closer)(hid_t)) : id{value}, close{closer} {}
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
  Handle(Handle &&other) noexcept : id{other.id}, close{other.close} {
    other.id = -1;
  }
  Handle &operator=(Handle &&other) noexcept {
    if (this != &other) {
      if (id >= 0)
        static_cast<void>(close(id));
      id = other.id;
      close = other.close;
      other.id = -1;
    }
    return *this;
  }
  ~Handle() {
    if (id >= 0)
      static_cast<void>(close(id));
  }
};

Handle group(hid_t parent, std::string_view name) {
  return {checked_id(H5Gcreate2(parent, std::string{name}.c_str(), H5P_DEFAULT,
                                H5P_DEFAULT, H5P_DEFAULT),
                     "create group"),
          H5Gclose};
}

Handle dataspace(const std::vector<hsize_t> &shape) {
  if (shape.empty())
    return {checked_id(H5Screate(H5S_SCALAR), "create scalar dataspace"),
            H5Sclose};
  return {checked_id(H5Screate_simple(static_cast<int>(shape.size()),
                                      shape.data(), nullptr),
                     "create dataspace"),
          H5Sclose};
}

void string_attribute(hid_t object, std::string_view name,
                      std::string_view value) {
  Handle type{checked_id(H5Tcopy(H5T_C_S1), "copy string type"), H5Tclose};
  checked(H5Tset_size(type.id, H5T_VARIABLE), "set variable string size");
  Handle space = dataspace({});
  Handle attribute{
      checked_id(H5Acreate2(object, std::string{name}.c_str(), type.id,
                            space.id, H5P_DEFAULT, H5P_DEFAULT),
                 "create string attribute"),
      H5Aclose};
  const char *raw = value.data();
  checked(H5Awrite(attribute.id, type.id, &raw), "write string attribute");
}

void string_array_attribute(hid_t object, std::string_view name,
                            const std::array<std::string_view, 3U> &values) {
  std::size_t width = 1U;
  for (const auto value : values)
    width = std::max(width, value.size() + 1U);
  Handle type{checked_id(H5Tcopy(H5T_C_S1), "copy fixed string type"),
              H5Tclose};
  checked(H5Tset_size(type.id, width), "set fixed string size");
  checked(H5Tset_strpad(type.id, H5T_STR_NULLTERM), "set string padding");
  Handle space = dataspace({3U});
  Handle attribute{
      checked_id(H5Acreate2(object, std::string{name}.c_str(), type.id,
                            space.id, H5P_DEFAULT, H5P_DEFAULT),
                 "create string-array attribute"),
      H5Aclose};
  std::vector<char> bytes(width * values.size(), '\0');
  for (std::size_t index = 0U; index < values.size(); ++index)
    std::copy(values[index].begin(), values[index].end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(index * width));
  checked(H5Awrite(attribute.id, type.id, bytes.data()),
          "write string-array attribute");
}

template <typename Value>
void numeric_attribute(hid_t object, std::string_view name, hid_t type,
                       const std::vector<hsize_t> &shape, const Value *values) {
  Handle space = dataspace(shape);
  Handle attribute{
      checked_id(H5Acreate2(object, std::string{name}.c_str(), type, space.id,
                            H5P_DEFAULT, H5P_DEFAULT),
                 "create numeric attribute"),
      H5Aclose};
  checked(H5Awrite(attribute.id, type, values), "write numeric attribute");
}

template <typename Value>
Handle numeric_dataset(hid_t parent, std::string_view name, hid_t type,
                       const std::vector<hsize_t> &shape, const Value *values,
                       bool compressed = false,
                       const Value *fill_value = nullptr) {
  Handle space = dataspace(shape);
  Handle creation{
      checked_id(H5Pcreate(H5P_DATASET_CREATE), "create dataset property list"),
      H5Pclose};
  if (compressed && !shape.empty()) {
    std::vector<hsize_t> chunk = shape;
    chunk.front() = 1U;
    checked(
        H5Pset_chunk(creation.id, static_cast<int>(chunk.size()), chunk.data()),
        "set chunk shape");
    checked(H5Pset_deflate(creation.id, 1U), "set deflate filter");
  }
  if (fill_value != nullptr)
    checked(H5Pset_fill_value(creation.id, type, fill_value), "set fill value");
  Handle dataset{
      checked_id(H5Dcreate2(parent, std::string{name}.c_str(), type, space.id,
                            H5P_DEFAULT, creation.id, H5P_DEFAULT),
                 "create dataset"),
      H5Dclose};
  checked(H5Dwrite(dataset.id, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, values),
          "write dataset");
  return dataset;
}

void root_metadata(hid_t file) {
  auto h5md = group(file, "h5md");
  constexpr std::array<std::int64_t, 2U> version{1, 1};
  numeric_attribute(h5md.id, "version", H5T_NATIVE_INT64, {2U}, version.data());
  auto author = group(h5md.id, "author");
  string_attribute(author.id, "name", "MolShredder tests");
  string_attribute(author.id, "email", "tests@example.invalid");
  auto creator = group(h5md.id, "creator");
  string_attribute(creator.id, "name", "MolShredder fixture generator");
  string_attribute(creator.id, "version", "1");
}

void write_timeline(hid_t element, const std::array<std::int64_t, 2U> &steps,
                    const std::array<double, 2U> &times,
                    bool hard_link_from_position, hid_t particle) {
  if (hard_link_from_position) {
    checked(H5Lcreate_hard(particle, "position/step", element, "step",
                           H5P_DEFAULT, H5P_DEFAULT),
            "link step");
    checked(H5Lcreate_hard(particle, "position/time", element, "time",
                           H5P_DEFAULT, H5P_DEFAULT),
            "link time");
    return;
  }
  static_cast<void>(
      numeric_dataset(element, "step", H5T_NATIVE_INT64, {2U}, steps.data()));
  auto time =
      numeric_dataset(element, "time", H5T_NATIVE_DOUBLE, {2U}, times.data());
  string_attribute(time.id, "unit", "ps");
}

void box(hid_t particle, bool partial_boundary, bool dynamic,
         hid_t position_group) {
  auto box_group = group(particle, "box");
  constexpr std::int64_t dimension = 3;
  numeric_attribute(box_group.id, "dimension", H5T_NATIVE_INT64, {},
                    &dimension);
  const std::array<std::string_view, 3U> boundaries =
      partial_boundary
          ? std::array<std::string_view, 3U>{"periodic", "none", "periodic"}
          : std::array<std::string_view, 3U>{"periodic", "periodic",
                                             "periodic"};
  string_array_attribute(box_group.id, "boundary", boundaries);
  if (dynamic) {
    auto edges = group(box_group.id, "edges");
    checked(H5Lcreate_hard(position_group, "step", edges.id, "step",
                           H5P_DEFAULT, H5P_DEFAULT),
            "link box step");
    checked(H5Lcreate_hard(position_group, "time", edges.id, "time",
                           H5P_DEFAULT, H5P_DEFAULT),
            "link box time");
    constexpr std::array<double, 18U> values{1.0, 0.0, 0.0, 0.2, 1.1, 0.0,
                                             0.1, 0.3, 1.2, 2.0, 0.0, 0.0,
                                             0.4, 2.1, 0.0, 0.2, 0.6, 2.2};
    auto value = numeric_dataset(edges.id, "value", H5T_NATIVE_DOUBLE,
                                 {2U, 3U, 3U}, values.data(), true);
    string_attribute(value.id, "unit", "nm");
  } else {
    constexpr std::array<double, 3U> values{2.0, 3.0, 4.0};
    auto edges = numeric_dataset(box_group.id, "edges", H5T_NATIVE_DOUBLE, {3U},
                                 values.data());
    string_attribute(edges.id, "unit", "angstrom");
  }
}

void standard_particle_group(hid_t particles, std::string_view name,
                             bool partial_boundary = false,
                             bool missing_position = false) {
  auto particle = group(particles, name);
  auto position = group(particle.id, "position");
  constexpr std::array<std::int64_t, 2U> steps{10, 20};
  constexpr std::array<double, 2U> times{0.0, 0.5};
  write_timeline(position.id, steps, times, false, particle.id);

  std::array<float, 18U> coordinates{0.3F, 0.6F, 0.9F, 0.1F, 0.2F, 0.3F,
                                     0.2F, 0.4F, 0.6F, 1.3F, 1.6F, 1.9F,
                                     1.1F, 1.2F, 1.3F, 1.2F, 1.4F, 1.6F};
  constexpr float fill = -999.0F;
  if (missing_position)
    coordinates[9] = fill;
  auto position_value = numeric_dataset(position.id, "value", H5T_NATIVE_FLOAT,
                                        {2U, 3U, 3U}, coordinates.data(), true,
                                        missing_position ? &fill : nullptr);
  string_attribute(position_value.id, "unit", "nm");

  constexpr std::array<std::int64_t, 3U> ids{30, 10, 20};
  static_cast<void>(
      numeric_dataset(particle.id, "id", H5T_NATIVE_INT64, {3U}, ids.data()));

  auto velocity = group(particle.id, "velocity");
  write_timeline(velocity.id, steps, times, true, particle.id);
  std::array<double, 18U> velocities{};
  for (std::size_t index = 0U; index < velocities.size(); ++index)
    velocities[index] = static_cast<double>(index + 1U) * 0.01;
  auto velocity_value = numeric_dataset(velocity.id, "value", H5T_NATIVE_DOUBLE,
                                        {2U, 3U, 3U}, velocities.data());
  string_attribute(velocity_value.id, "unit", "nm ps-1");

  auto force = group(particle.id, "force");
  write_timeline(force.id, steps, times, true, particle.id);
  std::array<float, 18U> forces{};
  for (std::size_t index = 0U; index < forces.size(); ++index)
    forces[index] = static_cast<float>(index + 1U) * 0.5F;
  auto force_value = numeric_dataset(force.id, "value", H5T_NATIVE_FLOAT,
                                     {2U, 3U, 3U}, forces.data());
  string_attribute(force_value.id, "unit", "kJ mol-1 nm-1");

  constexpr std::array<double, 3U> masses{30.0, 10.0, 20.0};
  auto mass = numeric_dataset(particle.id, "mass", H5T_NATIVE_DOUBLE, {3U},
                              masses.data());
  string_attribute(mass.id, "unit", "u");
  constexpr std::array<double, 3U> charges{-0.3, 0.1, 0.2};
  auto charge = numeric_dataset(particle.id, "charge", H5T_NATIVE_DOUBLE, {3U},
                                charges.data());
  string_attribute(charge.id, "unit", "e");
  constexpr std::array<std::int64_t, 3U> species{8, 6, 7};
  static_cast<void>(numeric_dataset(particle.id, "species", H5T_NATIVE_INT64,
                                    {3U}, species.data()));
  box(particle.id, partial_boundary, true, position.id);
}

void write_standard(const std::filesystem::path &path, bool partial = false,
                    bool missing = false) {
  Handle file{checked_id(H5Fcreate(path.string().c_str(), H5F_ACC_TRUNC,
                                   H5P_DEFAULT, H5P_DEFAULT),
                         "create file"),
              H5Fclose};
  root_metadata(file.id);
  auto particles = group(file.id, "particles");
  standard_particle_group(particles.id, "trajectory", partial, missing);
}

void write_fixed(const std::filesystem::path &path) {
  Handle file{checked_id(H5Fcreate(path.string().c_str(), H5F_ACC_TRUNC,
                                   H5P_DEFAULT, H5P_DEFAULT),
                         "create fixed file"),
              H5Fclose};
  root_metadata(file.id);
  auto particles = group(file.id, "particles");
  auto particle = group(particles.id, "trajectory");
  auto position = group(particle.id, "position");
  constexpr std::int64_t step_increment = 5;
  auto step = numeric_dataset(position.id, "step", H5T_NATIVE_INT64, {},
                              &step_increment);
  constexpr std::int64_t step_offset = 7;
  numeric_attribute(step.id, "offset", H5T_NATIVE_INT64, {}, &step_offset);
  constexpr double time_increment = 0.25;
  auto time = numeric_dataset(position.id, "time", H5T_NATIVE_DOUBLE, {},
                              &time_increment);
  constexpr double time_offset = 1.0;
  numeric_attribute(time.id, "offset", H5T_NATIVE_DOUBLE, {}, &time_offset);
  string_attribute(time.id, "unit", "fs");
  constexpr std::array<double, 18U> values{0.0, 0.0, 0.0, 1.0, 2.0, 3.0,
                                           2.0, 4.0, 6.0, 1.0, 1.0, 1.0,
                                           2.0, 3.0, 4.0, 3.0, 5.0, 7.0};
  static_cast<void>(numeric_dataset(position.id, "value", H5T_NATIVE_DOUBLE,
                                    {2U, 3U, 3U}, values.data()));
  box(particle.id, false, false, position.id);
}

void write_ambiguous(const std::filesystem::path &path) {
  Handle file{checked_id(H5Fcreate(path.string().c_str(), H5F_ACC_TRUNC,
                                   H5P_DEFAULT, H5P_DEFAULT),
                         "create ambiguous file"),
              H5Fclose};
  root_metadata(file.id);
  auto particles = group(file.id, "particles");
  standard_particle_group(particles.id, "alpha");
  standard_particle_group(particles.id, "beta");
}

void write_dynamic_ids(const std::filesystem::path &path) {
  Handle file{checked_id(H5Fcreate(path.string().c_str(), H5F_ACC_TRUNC,
                                   H5P_DEFAULT, H5P_DEFAULT),
                         "create dynamic-ID file"),
              H5Fclose};
  root_metadata(file.id);
  auto particles = group(file.id, "particles");
  auto particle = group(particles.id, "trajectory");
  auto position = group(particle.id, "position");
  constexpr std::array<std::int64_t, 2U> steps{0, 1};
  constexpr std::array<double, 2U> times{0.0, 1.0};
  write_timeline(position.id, steps, times, false, particle.id);
  constexpr float coordinate_fill = -999.0F;
  constexpr std::array<float, 24U> coordinates{1.0F,
                                               0.0F,
                                               0.0F,
                                               2.0F,
                                               0.0F,
                                               0.0F,
                                               3.0F,
                                               0.0F,
                                               0.0F,
                                               coordinate_fill,
                                               coordinate_fill,
                                               coordinate_fill,
                                               30.0F,
                                               0.0F,
                                               0.0F,
                                               10.0F,
                                               0.0F,
                                               0.0F,
                                               coordinate_fill,
                                               coordinate_fill,
                                               coordinate_fill,
                                               coordinate_fill,
                                               coordinate_fill,
                                               coordinate_fill};
  auto value =
      numeric_dataset(position.id, "value", H5T_NATIVE_FLOAT, {2U, 4U, 3U},
                      coordinates.data(), false, &coordinate_fill);
  string_attribute(value.id, "unit", "angstrom");

  auto ids = group(particle.id, "id");
  checked(H5Lcreate_hard(position.id, "step", ids.id, "step", H5P_DEFAULT,
                         H5P_DEFAULT),
          "link dynamic ID step");
  constexpr std::int64_t id_fill = -1;
  constexpr std::array<std::int64_t, 8U> id_values{10, 20, 30,      id_fill,
                                                   30, 10, id_fill, id_fill};
  static_cast<void>(numeric_dataset(ids.id, "value", H5T_NATIVE_INT64, {2U, 4U},
                                    id_values.data(), false, &id_fill));

  auto mass = group(particle.id, "mass");
  checked(H5Lcreate_hard(position.id, "step", mass.id, "step", H5P_DEFAULT,
                         H5P_DEFAULT),
          "link dynamic mass step");
  constexpr double mass_fill = -1.0;
  constexpr std::array<double, 8U> masses{10.0, 20.0, 30.0,      mass_fill,
                                          30.0, 10.0, mass_fill, mass_fill};
  auto mass_value = numeric_dataset(mass.id, "value", H5T_NATIVE_DOUBLE,
                                    {2U, 4U}, masses.data(), false, &mass_fill);
  string_attribute(mass_value.id, "unit", "u");

  auto box_group = group(particle.id, "box");
  constexpr std::int64_t dimension = 3;
  numeric_attribute(box_group.id, "dimension", H5T_NATIVE_INT64, {},
                    &dimension);
  string_array_attribute(box_group.id, "boundary", {"none", "none", "none"});
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 7) {
    std::cerr << "expected standard, fixed-step, ambiguous, partial-periodic "
                 "missing-data and dynamic-ID paths\n";
    return 2;
  }
  try {
    write_standard(argv[1]);
    write_fixed(argv[2]);
    write_ambiguous(argv[3]);
    write_standard(argv[4], true, false);
    write_standard(argv[5], false, true);
    write_dynamic_ids(argv[6]);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
