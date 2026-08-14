#include "molshredder/io/trajectory_reader.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include "molshredder/operation/error.hpp"

namespace molshredder::io {
namespace {

TrajectoryFormat detect(const std::filesystem::path &path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  if (extension == ".dcd")
    return TrajectoryFormat::dcd;
  if (extension == ".trr")
    return TrajectoryFormat::trr;
  if (extension == ".xtc")
    return TrajectoryFormat::xtc;
  if (extension == ".rst7" || extension == ".restrt" ||
      extension == ".inpcrd" || extension == ".inprst") {
    return TrajectoryFormat::rst7;
  }
  if (extension == ".mdcrd" || extension == ".crd") {
    return TrajectoryFormat::mdcrd;
  }
  if (extension == ".nc" || extension == ".ncdf" || extension == ".netcdf") {
    return TrajectoryFormat::amber_netcdf;
  }
  if (extension == ".h5md")
    return TrajectoryFormat::h5md;
  if (extension == ".lammpstrj" || extension == ".lammpstraj" ||
      extension == ".dump") {
    return TrajectoryFormat::lammps_dump;
  }
  if (extension == ".binpos")
    return TrajectoryFormat::binpos;
  return TrajectoryFormat::auto_detect;
}

operation::Error unsupported_format(const std::filesystem::path &path) {
  return {operation::ErrorCode::unsupported,
          "cannot detect trajectory format from path: " + path.string(),
          "use --file-format dcd, trr, xtc, rst7, mdcrd, netcdf, h5md, "
          "lammps, or binpos"};
}

} // namespace

std::string_view to_string(TrajectoryFormat format) noexcept {
  switch (format) {
  case TrajectoryFormat::auto_detect:
    return "auto";
  case TrajectoryFormat::dcd:
    return "dcd";
  case TrajectoryFormat::trr:
    return "trr";
  case TrajectoryFormat::xtc:
    return "xtc";
  case TrajectoryFormat::rst7:
    return "rst7";
  case TrajectoryFormat::mdcrd:
    return "mdcrd";
  case TrajectoryFormat::amber_netcdf:
    return "netcdf";
  case TrajectoryFormat::h5md:
    return "h5md";
  case TrajectoryFormat::lammps_dump:
    return "lammps";
  case TrajectoryFormat::binpos:
    return "binpos";
  }
  return "auto";
}

operation::Result<OpenedTrajectory>
open_trajectory(const std::filesystem::path &path, TrajectoryFormat format,
                std::optional<std::size_t> expected_atom_count,
                std::optional<double> amber_box_angle_degrees) {
  return open_trajectory(path, format,
                         TrajectoryOpenContext{expected_atom_count,
                                               {},
                                               amber_box_angle_degrees,
                                               std::nullopt,
                                               std::nullopt});
}

operation::Result<OpenedTrajectory>
open_trajectory(const std::filesystem::path &path, TrajectoryFormat format,
                const TrajectoryOpenContext &context) {
  const auto resolved =
      format == TrajectoryFormat::auto_detect ? detect(path) : format;
  if (resolved == TrajectoryFormat::auto_detect) {
    return operation::Result<OpenedTrajectory>::failure(
        unsupported_format(path));
  }
  if (resolved == TrajectoryFormat::dcd) {
    const auto source = open_dcd(path, context.expected_atom_count);
    if (!source.has_value()) {
      return operation::Result<OpenedTrajectory>::failure(source.error());
    }
    return operation::Result<OpenedTrajectory>::success(
        {resolved, source.value()});
  }
  if (resolved == TrajectoryFormat::trr) {
    const auto source = open_trr(path, context.expected_atom_count);
    if (!source.has_value()) {
      return operation::Result<OpenedTrajectory>::failure(source.error());
    }
    return operation::Result<OpenedTrajectory>::success(
        {resolved, source.value()});
  }
  if (resolved == TrajectoryFormat::rst7) {
    const auto source = open_amber_restart(path, context.expected_atom_count);
    if (!source.has_value()) {
      return operation::Result<OpenedTrajectory>::failure(source.error());
    }
    return operation::Result<OpenedTrajectory>::success(
        {resolved, source.value()});
  }
  if (resolved == TrajectoryFormat::mdcrd) {
    if (!context.expected_atom_count.has_value()) {
      return operation::Result<OpenedTrajectory>::failure(operation::Error{
          operation::ErrorCode::invalid_argument,
          "Amber ASCII trajectory requires the active topology atom count",
          "load a matching topology before attaching the trajectory"});
    }
    const auto source = open_amber_ascii_trajectory(
        path, *context.expected_atom_count, context.amber_box_angle_degrees);
    if (!source.has_value()) {
      return operation::Result<OpenedTrajectory>::failure(source.error());
    }
    return operation::Result<OpenedTrajectory>::success(
        {resolved, source.value()});
  }
  if (resolved == TrajectoryFormat::amber_netcdf) {
    const auto source = open_amber_netcdf(path, context.expected_atom_count);
    if (!source.has_value()) {
      return operation::Result<OpenedTrajectory>::failure(source.error());
    }
    return operation::Result<OpenedTrajectory>::success(
        {resolved, source.value()});
  }
  if (resolved == TrajectoryFormat::h5md) {
    const auto source =
        open_h5md(path, context.expected_atom_count, context.source_atom_ids,
                  context.coordinate_unit, context.h5md_particle_group);
    if (!source.has_value()) {
      return operation::Result<OpenedTrajectory>::failure(source.error());
    }
    return operation::Result<OpenedTrajectory>::success(
        {resolved, source.value()});
  }
  if (resolved == TrajectoryFormat::lammps_dump) {
    if (!context.coordinate_unit.has_value()) {
      return operation::Result<OpenedTrajectory>::failure(operation::Error{
          operation::ErrorCode::invalid_argument,
          "LAMMPS text dump does not encode its coordinate unit",
          "set --coordinate-unit angstrom or nanometer to match the LAMMPS "
          "units command"});
    }
    const auto source = open_lammps_dump(path, context.source_atom_ids,
                                         *context.coordinate_unit);
    if (!source.has_value()) {
      return operation::Result<OpenedTrajectory>::failure(source.error());
    }
    return operation::Result<OpenedTrajectory>::success(
        {resolved, source.value()});
  }
  if (resolved == TrajectoryFormat::binpos) {
    if (!context.expected_atom_count.has_value()) {
      return operation::Result<OpenedTrajectory>::failure(operation::Error{
          operation::ErrorCode::invalid_argument,
          "BINPOS trajectory requires the active topology atom count",
          "load a matching topology before attaching the trajectory"});
    }
    const auto source = open_binpos(path, *context.expected_atom_count);
    if (!source.has_value()) {
      return operation::Result<OpenedTrajectory>::failure(source.error());
    }
    return operation::Result<OpenedTrajectory>::success(
        {resolved, source.value()});
  }
  const auto source = open_xtc(path, context.expected_atom_count);
  if (!source.has_value()) {
    return operation::Result<OpenedTrajectory>::failure(source.error());
  }
  return operation::Result<OpenedTrajectory>::success(
      {resolved, source.value()});
}

} // namespace molshredder::io
