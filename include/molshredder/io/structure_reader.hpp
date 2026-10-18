#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::io {

enum class StructureFormat {
  auto_detect,
  pdb,
  mmcif,
  bcif,
  mmtf,
  pdbqt,
  cor,
  cc1,
  cml,
  pse,
  mmod,
  mae,
  pdbml,
  pqr,
  mol,
  sdf,
  mol2,
  psf,
  prmtop,
  gro,
  g96,
  vtf,
  xyz
};

// Representation of a subset of one structure that a session file (PSE) showed.
// `representation` is a command-level name (sticks, spheres, lines, ribbon, cartoon) and `selection` a
// selection expression relative to the structure's own atoms (one-based `index` list).
struct StructureRepresentationHint {
  std::string representation;
  std::string selection;
};

struct StructureData {
  std::string name;
  std::shared_ptr<const model::Topology> topology;
  std::shared_ptr<const model::CoordinateSource> coordinates;
  std::map<std::string, std::string, std::less<>> metadata;
  std::vector<StructureRepresentationHint> representation_hints{};
  bool visible{true};
};

struct StructureDocument {
  StructureFormat format{StructureFormat::auto_detect};
  std::string source_name;
  std::vector<StructureData> structures;
  // Session-style files only: named selections (name, expression over the structure names above) and
  // notes about content that was present in the file but not imported.
  std::vector<std::pair<std::string, std::string>> named_selections{};
  std::vector<std::string> import_notes{};
};

struct StructureReadOptions {
  StructureFormat format{StructureFormat::auto_detect};
  std::string source_name{"<memory>"};
};

[[nodiscard]] operation::Result<StructureDocument>
read_structure(std::string_view content, StructureReadOptions options = {});

[[nodiscard]] operation::Result<StructureDocument>
read_structure_file(const std::filesystem::path &path,
                    StructureFormat format = StructureFormat::auto_detect);

[[nodiscard]] std::string_view to_string(StructureFormat format) noexcept;

} // namespace molshredder::io
