#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "molshredder/io/trajectory_reader.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::io {

struct TrajectoryFormatLoss {
  std::string channel;
  std::uint64_t count{};
  std::string message;

  friend bool operator==(const TrajectoryFormatLoss &,
                         const TrajectoryFormatLoss &) = default;
};

struct TrajectoryWriteOptions {
  TrajectoryFormat format{TrajectoryFormat::auto_detect};
  std::string title;
};

struct TrajectoryWriteReport {
  TrajectoryFormat format{TrajectoryFormat::auto_detect};
  std::size_t atom_count{};
  std::uint64_t byte_count{};
  bool has_time{};
  bool has_temperature{};
  bool has_velocities{};
  bool has_forces{};
  bool has_unit_cell{};
  std::optional<TrrPrecision> precision;
  std::vector<TrajectoryFormatLoss> losses;
};

struct SerializedTrajectoryFrame {
  std::string content;
  TrajectoryWriteReport report;
};

[[nodiscard]] operation::Result<SerializedTrajectoryFrame>
serialize_trajectory_frame(const model::CoordinateFrame &frame,
                           TrajectoryWriteOptions options,
                           operation::TaskContext &context);

[[nodiscard]] operation::Result<TrajectoryWriteReport>
write_trajectory_frame_file(const std::filesystem::path &path,
                            const model::CoordinateFrame &frame,
                            TrajectoryWriteOptions options, bool overwrite,
                            operation::TaskContext &context);

} // namespace molshredder::io
