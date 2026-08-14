#include "molshredder/io/trajectory_reader.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include "molshredder/operation/error.hpp"

namespace molshredder::io {
namespace {

TrajectoryFormat detect(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  if (extension == ".dcd") return TrajectoryFormat::dcd;
  if (extension == ".trr") return TrajectoryFormat::trr;
  if (extension == ".xtc") return TrajectoryFormat::xtc;
  return TrajectoryFormat::auto_detect;
}

operation::Error unsupported_format(const std::filesystem::path& path) {
  return {operation::ErrorCode::unsupported,
          "cannot detect trajectory format from path: " + path.string(),
          "use --file-format dcd, trr, or xtc"};
}

}  // namespace

std::string_view to_string(TrajectoryFormat format) noexcept {
  switch (format) {
    case TrajectoryFormat::auto_detect: return "auto";
    case TrajectoryFormat::dcd: return "dcd";
    case TrajectoryFormat::trr: return "trr";
    case TrajectoryFormat::xtc: return "xtc";
  }
  return "auto";
}

operation::Result<OpenedTrajectory> open_trajectory(
    const std::filesystem::path& path, TrajectoryFormat format,
    std::optional<std::size_t> expected_atom_count) {
  const auto resolved =
      format == TrajectoryFormat::auto_detect ? detect(path) : format;
  if (resolved == TrajectoryFormat::auto_detect) {
    return operation::Result<OpenedTrajectory>::failure(
        unsupported_format(path));
  }
  if (resolved == TrajectoryFormat::dcd) {
    const auto source = open_dcd(path, expected_atom_count);
    if (!source.has_value()) {
      return operation::Result<OpenedTrajectory>::failure(source.error());
    }
    return operation::Result<OpenedTrajectory>::success(
        {resolved, source.value()});
  }
  if (resolved == TrajectoryFormat::trr) {
    const auto source = open_trr(path, expected_atom_count);
    if (!source.has_value()) {
      return operation::Result<OpenedTrajectory>::failure(source.error());
    }
    return operation::Result<OpenedTrajectory>::success(
        {resolved, source.value()});
  }
  const auto source = open_xtc(path, expected_atom_count);
  if (!source.has_value()) {
    return operation::Result<OpenedTrajectory>::failure(source.error());
  }
  return operation::Result<OpenedTrajectory>::success(
      {resolved, source.value()});
}

}  // namespace molshredder::io
