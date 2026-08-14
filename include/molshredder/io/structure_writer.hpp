#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "molshredder/io/structure_reader.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::io {

struct FormatLoss {
  std::string channel;
  std::uint64_t count{};
  std::string message;

  friend bool operator==(const FormatLoss&, const FormatLoss&) = default;
};

struct StructureWriteOptions {
  StructureFormat format{StructureFormat::auto_detect};
  std::vector<std::size_t> frame_indices;
  unsigned int decimal_places{6U};
  std::string comment;
};

struct StructureWriteReport {
  StructureFormat format{StructureFormat::auto_detect};
  std::size_t atom_count{};
  std::size_t frame_count{};
  std::uint64_t byte_count{};
  std::vector<FormatLoss> losses;
};

struct SerializedStructure {
  std::string content;
  StructureWriteReport report;
};

[[nodiscard]] operation::Result<SerializedStructure> serialize_structure(
    const model::Topology& topology, const model::CoordinateSource& coordinates,
    StructureWriteOptions options, operation::TaskContext& context);

[[nodiscard]] operation::Result<StructureWriteReport> write_structure_file(
    const std::filesystem::path& path, const model::Topology& topology,
    const model::CoordinateSource& coordinates, StructureWriteOptions options,
    bool overwrite, operation::TaskContext& context);

}  // namespace molshredder::io
