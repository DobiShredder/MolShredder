#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "molshredder/io/volume_reader.hpp"
#include "molshredder/model/volume.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::io {

struct VolumeFormatLoss {
  std::string channel;
  std::uint64_t count{};
  std::string message;

  friend bool operator==(const VolumeFormatLoss &, const VolumeFormatLoss &) =
      default;
};

struct VolumeWriteOptions {
  VolumeFormat format{VolumeFormat::auto_detect};
  std::string name;
};

struct VolumeWriteReport {
  VolumeFormat format{VolumeFormat::auto_detect};
  model::VolumeShape shape;
  model::VolumePrecision precision{model::VolumePrecision::float64};
  std::uint64_t value_count{};
  std::uint64_t byte_count{};
  std::vector<VolumeFormatLoss> losses;
};

struct SerializedVolume {
  std::string content;
  VolumeWriteReport report;
};

[[nodiscard]] operation::Result<SerializedVolume>
serialize_volume(const model::VolumeGrid &grid, VolumeWriteOptions options,
                 operation::TaskContext &context);

[[nodiscard]] operation::Result<VolumeWriteReport>
write_volume_file(const std::filesystem::path &path,
                  const model::VolumeGrid &grid, VolumeWriteOptions options,
                  bool overwrite, operation::TaskContext &context);

} // namespace molshredder::io
