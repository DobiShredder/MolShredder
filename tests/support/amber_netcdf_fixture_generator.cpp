#include <netcdf.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void checked(int status, const char *operation) {
  if (status != NC_NOERR) {
    std::cerr << operation << ": " << nc_strerror(status) << '\n';
    std::exit(1);
  }
}

void text_attribute(int ncid, int variable, const char *name,
                    const char *value) {
  checked(nc_put_att_text(ncid, variable, name,
                          std::char_traits<char>::length(value), value),
          name);
}

void string_attribute(int ncid, int variable, const char *name,
                      const char *value) {
  const char *values[]{value};
  checked(nc_put_att_string(ncid, variable, name, 1U, values), name);
}

struct Dimensions {
  int frame{};
  int atom{};
  int spatial{};
  int cell_spatial{};
  int cell_angular{};
  int label{};
};

Dimensions dimensions(int ncid) {
  Dimensions result;
  checked(nc_def_dim(ncid, "frame", NC_UNLIMITED, &result.frame), "frame dim");
  checked(nc_def_dim(ncid, "atom", 4U, &result.atom), "atom dim");
  checked(nc_def_dim(ncid, "spatial", 3U, &result.spatial), "spatial dim");
  checked(nc_def_dim(ncid, "cell_spatial", 3U, &result.cell_spatial),
          "cell spatial dim");
  checked(nc_def_dim(ncid, "cell_angular", 3U, &result.cell_angular),
          "cell angular dim");
  checked(nc_def_dim(ncid, "label", 5U, &result.label), "label dim");
  return result;
}

void globals(int ncid, bool valid_convention = true) {
  text_attribute(ncid, NC_GLOBAL, "Conventions",
                 valid_convention ? "AMBER" : "NOT_AMBER");
  text_attribute(ncid, NC_GLOBAL, "ConventionVersion", "1.0");
  text_attribute(ncid, NC_GLOBAL, "title", "MolShredder Amber NetCDF fixture");
  text_attribute(ncid, NC_GLOBAL, "program", "fixture-generator");
  text_attribute(ncid, NC_GLOBAL, "programVersion", "1");
  text_attribute(ncid, NC_GLOBAL, "application", "MolShredder tests");
}

void write_standard(const std::filesystem::path &path,
                    bool valid_convention = true, bool partial_cell = false,
                    bool missing_coordinate = false) {
  int ncid{};
  checked(nc_create(path.string().c_str(), NC_CLOBBER | NC_64BIT_OFFSET, &ncid),
          "create standard");
  globals(ncid, valid_convention);
  const auto dims = dimensions(ncid);
  int spatial{};
  checked(nc_def_var(ncid, "spatial", NC_CHAR, 1, &dims.spatial, &spatial),
          "spatial variable");
  int cell_spatial_labels{};
  int cell_angular_labels{};
  checked(nc_def_var(ncid, "cell_spatial", NC_CHAR, 1, &dims.cell_spatial,
                     &cell_spatial_labels),
          "cell spatial labels");
  const std::array<int, 2U> angular_label_dims{dims.cell_angular, dims.label};
  checked(nc_def_var(ncid, "cell_angular", NC_CHAR, 2,
                     angular_label_dims.data(), &cell_angular_labels),
          "cell angular labels");
  const std::array<int, 3U> frame_atom_spatial{dims.frame, dims.atom,
                                               dims.spatial};
  int coordinates{};
  int velocities{};
  int forces{};
  int time{};
  int temperature{};
  checked(nc_def_var(ncid, "coordinates", NC_FLOAT, 3,
                     frame_atom_spatial.data(), &coordinates),
          "coordinates variable");
  checked(nc_def_var(ncid, "velocities", NC_FLOAT, 3, frame_atom_spatial.data(),
                     &velocities),
          "velocities variable");
  checked(nc_def_var(ncid, "forces", NC_FLOAT, 3, frame_atom_spatial.data(),
                     &forces),
          "forces variable");
  checked(nc_def_var(ncid, "time", NC_FLOAT, 1, &dims.frame, &time),
          "time variable");
  checked(nc_def_var(ncid, "temp0", NC_FLOAT, 1, &dims.frame, &temperature),
          "temperature variable");
  int cell_lengths{};
  const std::array<int, 2U> length_dims{dims.frame, dims.cell_spatial};
  checked(nc_def_var(ncid, "cell_lengths", NC_DOUBLE, 2, length_dims.data(),
                     &cell_lengths),
          "cell lengths variable");
  int cell_angles{-1};
  if (!partial_cell) {
    const std::array<int, 2U> angle_dims{dims.frame, dims.cell_angular};
    checked(nc_def_var(ncid, "cell_angles", NC_DOUBLE, 2, angle_dims.data(),
                       &cell_angles),
            "cell angles variable");
  }
  text_attribute(ncid, coordinates, "units", "angstrom");
  text_attribute(ncid, velocities, "units", "angstrom/picosecond");
  text_attribute(ncid, forces, "units", "kilocalorie/mole/angstrom");
  text_attribute(ncid, time, "units", "picosecond");
  text_attribute(ncid, temperature, "units", "kelvin");
  text_attribute(ncid, cell_lengths, "units", "angstrom");
  if (cell_angles >= 0)
    text_attribute(ncid, cell_angles, "units", "degree");
  constexpr float coordinate_scale = 0.5F;
  constexpr float time_scale = 2.0F;
  checked(nc_put_att_float(ncid, coordinates, "scale_factor", NC_FLOAT, 1,
                           &coordinate_scale),
          "coordinate scale");
  checked(
      nc_put_att_float(ncid, time, "scale_factor", NC_FLOAT, 1, &time_scale),
      "time scale");
  constexpr float coordinate_fill = -9999.0F;
  if (missing_coordinate)
    checked(nc_put_att_float(ncid, coordinates, "_FillValue", NC_FLOAT, 1,
                             &coordinate_fill),
            "coordinate fill");
  checked(nc_enddef(ncid), "end definition");
  constexpr std::array<char, 3U> labels{'x', 'y', 'z'};
  checked(nc_put_var_text(ncid, spatial, labels.data()), "spatial labels");
  constexpr std::array<char, 3U> cell_axes{'a', 'b', 'c'};
  constexpr std::array<char, 15U> angle_axes{'a', 'l', 'p', 'h', 'a',
                                             'b', 'e', 't', 'a', ' ',
                                             'g', 'a', 'm', 'm', 'a'};
  checked(nc_put_var_text(ncid, cell_spatial_labels, cell_axes.data()),
          "cell spatial labels data");
  checked(nc_put_var_text(ncid, cell_angular_labels, angle_axes.data()),
          "cell angular labels data");

  std::array<float, 24U> coordinate_values{};
  std::array<float, 24U> velocity_values{};
  std::array<float, 24U> force_values{};
  for (std::size_t index = 0U; index < coordinate_values.size(); ++index) {
    const auto frame = index / 12U;
    const auto actual = static_cast<float>(frame * 10U + index % 12U);
    coordinate_values[index] = actual / coordinate_scale;
    velocity_values[index] = actual / 10.0F;
    force_values[index] = 100.0F + actual;
  }
  if (missing_coordinate)
    coordinate_values[0] = coordinate_fill;
  const std::array<std::size_t, 3U> start3{0U, 0U, 0U};
  const std::array<std::size_t, 3U> count3{2U, 4U, 3U};
  checked(nc_put_vara_float(ncid, coordinates, start3.data(), count3.data(),
                            coordinate_values.data()),
          "coordinate data");
  checked(nc_put_vara_float(ncid, velocities, start3.data(), count3.data(),
                            velocity_values.data()),
          "velocity data");
  checked(nc_put_vara_float(ncid, forces, start3.data(), count3.data(),
                            force_values.data()),
          "force data");
  constexpr std::array<float, 2U> time_values{0.0F, 0.5F};
  constexpr std::array<float, 2U> temperature_values{300.0F, 310.0F};
  const std::array<std::size_t, 1U> start1{0U};
  const std::array<std::size_t, 1U> count1{2U};
  checked(nc_put_vara_float(ncid, time, start1.data(), count1.data(),
                            time_values.data()),
          "time data");
  checked(nc_put_vara_float(ncid, temperature, start1.data(), count1.data(),
                            temperature_values.data()),
          "temperature data");
  constexpr std::array<double, 6U> lengths{10.0, 11.0, 12.0, 20.0, 21.0, 22.0};
  constexpr std::array<double, 6U> angles{90.0, 90.0, 90.0, 80.0, 90.0, 100.0};
  const std::array<std::size_t, 2U> start2{0U, 0U};
  const std::array<std::size_t, 2U> count2{2U, 3U};
  checked(nc_put_vara_double(ncid, cell_lengths, start2.data(), count2.data(),
                             lengths.data()),
          "cell lengths data");
  if (cell_angles >= 0)
    checked(nc_put_vara_double(ncid, cell_angles, start2.data(), count2.data(),
                               angles.data()),
            "cell angles data");
  checked(nc_close(ncid), "close standard");
}

void write_compressed(const std::filesystem::path &path) {
  int ncid{};
  checked(nc_create(path.string().c_str(), NC_CLOBBER | NC_NETCDF4, &ncid),
          "create compressed");
  string_attribute(ncid, NC_GLOBAL, "Conventions", "AMBER");
  string_attribute(ncid, NC_GLOBAL, "ConventionVersion", "1.0");
  string_attribute(ncid, NC_GLOBAL, "application", "MolShredder tests");
  const auto dims = dimensions(ncid);
  int spatial{};
  checked(nc_def_var(ncid, "spatial", NC_CHAR, 1, &dims.spatial, &spatial),
          "spatial variable");
  const std::array<int, 3U> shape{dims.frame, dims.atom, dims.spatial};
  int coordinates{};
  checked(
      nc_def_var(ncid, "compressedpos", NC_INT, 3, shape.data(), &coordinates),
      "compressed positions");
  text_attribute(ncid, coordinates, "units", "angstrom");
  constexpr double factor = 1000.0;
  checked(nc_put_att_double(ncid, coordinates, "icompressfac", NC_DOUBLE, 1,
                            &factor),
          "compression factor");
  checked(nc_enddef(ncid), "end compressed definition");
  constexpr std::array<char, 3U> labels{'x', 'y', 'z'};
  checked(nc_put_var_text(ncid, spatial, labels.data()), "spatial labels");
  std::array<int, 24U> values{};
  for (std::size_t index = 0U; index < values.size(); ++index)
    values[index] = static_cast<int>((index / 12U * 10U + index % 12U) * 1000U);
  const std::array<std::size_t, 3U> start{0U, 0U, 0U};
  const std::array<std::size_t, 3U> count{2U, 4U, 3U};
  checked(nc_put_vara_int(ncid, coordinates, start.data(), count.data(),
                          values.data()),
          "compressed data");
  checked(nc_close(ncid), "close compressed");
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 6) {
    std::cerr << "expected standard, compressed, bad-convention, partial-cell "
                 "and missing-data paths\n";
    return 2;
  }
  write_standard(argv[1]);
  write_compressed(argv[2]);
  write_standard(argv[3], false);
  write_standard(argv[4], true, true);
  write_standard(argv[5], true, false, true);
  return 0;
}
