#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::io {

enum class StructureFormat { auto_detect, pdb, mmcif };

struct StructureData {
  std::string name;
  std::shared_ptr<const model::Topology> topology;
  std::shared_ptr<const model::CoordinateSource> coordinates;
  std::map<std::string, std::string, std::less<>> metadata;
};

struct StructureDocument {
  StructureFormat format{StructureFormat::auto_detect};
  std::string source_name;
  std::vector<StructureData> structures;
};

struct StructureReadOptions {
  StructureFormat format{StructureFormat::auto_detect};
  std::string source_name{"<memory>"};
};

[[nodiscard]] operation::Result<StructureDocument> read_structure(
    std::string_view content, StructureReadOptions options = {});

[[nodiscard]] operation::Result<StructureDocument> read_structure_file(
    const std::filesystem::path& path,
    StructureFormat format = StructureFormat::auto_detect);

[[nodiscard]] std::string_view to_string(StructureFormat format) noexcept;

}  // namespace molshredder::io
