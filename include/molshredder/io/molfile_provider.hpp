#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "molshredder/io/structure_reader.hpp"
#include "molshredder/io/volume_reader.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::io {

enum class MolfileProviderTrust {
  application_bundled,
  user_approved,
  explicit_path,
};

struct MolfileSearchDirectory {
  std::filesystem::path path;
  MolfileProviderTrust trust{MolfileProviderTrust::user_approved};
};

struct MolfileDiscoveryRequest {
  std::vector<MolfileSearchDirectory> approved_directories;
  std::vector<std::filesystem::path> explicit_files;
};

struct MolfileProviderDescriptor {
  std::string provider_id;
  std::string plugin_name;
  std::string pretty_name;
  std::string author;
  int major_version{};
  int minor_version{};
  bool thread_safe{};
  std::vector<std::string> extensions;
  std::filesystem::path library_path;
  MolfileProviderTrust trust{MolfileProviderTrust::explicit_path};
};

struct MolfileReadLimits {
  std::size_t max_atoms{5'000'000U};
  std::size_t max_staging_bytes{1024U * 1024U * 1024U};
  std::size_t max_bonds{50'000'000U};
};

struct MolfileVolumeLimits {
  std::size_t max_values{268'435'456U};
  // Peak staging: the plugin's x-fastest block plus the z-fastest copy.
  std::size_t max_staging_bytes{1024U * 1024U * 1024U};
};

struct MolfileTrajectoryLimits {
  std::size_t max_atoms{5'000'000U};
  std::size_t max_frames{10'000'000U};
  // Peak staging for one frame (plugin coordinate buffer plus the converted
  // frame); the trajectory is never fully materialized.
  std::size_t max_frame_bytes{1024U * 1024U * 1024U};
};

// Frames read sequentially through a molfile plugin. A backward or random
// access reopens the input and skips forward, so it is correct but not an
// indexed reader; native DCD/XTC/TRR stay the automatic path.
class MolfileTrajectorySource : public model::CoordinateSource {
 public:
  // Typed semantic loss for fields the plugin channel did not deliver or this
  // adapter does not import.
  [[nodiscard]] virtual const std::vector<std::string>& loss() const
      noexcept = 0;
};

class MolfileProviderRegistry {
 public:
  MolfileProviderRegistry();
  ~MolfileProviderRegistry();

  MolfileProviderRegistry(const MolfileProviderRegistry&) = delete;
  MolfileProviderRegistry& operator=(const MolfileProviderRegistry&) = delete;
  MolfileProviderRegistry(MolfileProviderRegistry&&) noexcept;
  MolfileProviderRegistry& operator=(MolfileProviderRegistry&&) noexcept;

  [[nodiscard]] operation::Result<std::vector<MolfileProviderDescriptor>>
  discover(const MolfileDiscoveryRequest& request);

  [[nodiscard]] std::vector<MolfileProviderDescriptor> descriptors() const;

  [[nodiscard]] operation::Result<StructureDocument> read_structure(
      const std::filesystem::path& path, std::string provider_id,
      MolfileReadLimits limits = {}) const;

  [[nodiscard]] operation::Result<StructureDocument> read_structure(
      const std::filesystem::path& path, std::string provider_id,
      operation::TaskContext& context, MolfileReadLimits limits = {}) const;

  // Reads one scalar volume (`dx`, `ccp4`). The ABI reports axis extents
  // (first to last grid point) in Angstrom; deltas are extent / (count - 1)
  // and the x-fastest plugin block is transposed to MolShredder's z-fastest
  // layout. `coordinate_unit` is the caller's declaration, as for native
  // OpenDX, because the ABI carries no unit.
  [[nodiscard]] operation::Result<VolumeDocument> read_volume(
      const std::filesystem::path& path, std::string provider_id,
      operation::TaskContext& context,
      operation::LengthUnit coordinate_unit = operation::LengthUnit::angstrom,
      std::optional<std::string> name = std::nullopt,
      MolfileVolumeLimits limits = {}) const;

  // Opens a trajectory channel (dcd, trr, xtc, crd, crdbox, lammpstrj, netcdf,
  // binpos, rst7, vcf) after counting frames with one bounded sequential scan.
  // `expected_atom_count` is passed to plugins whose files do not record it.
  [[nodiscard]] operation::Result<std::shared_ptr<const MolfileTrajectorySource>>
  open_trajectory(const std::filesystem::path& path, std::string provider_id,
                  std::size_t expected_atom_count,
                  operation::TaskContext& context,
                  MolfileTrajectoryLimits limits = {}) const;

 private:
  class Impl;
  // Shared so trajectory sources keep the plugin library loaded.
  std::shared_ptr<Impl> impl_;
};

[[nodiscard]] std::string_view to_string(MolfileProviderTrust trust) noexcept;

}  // namespace molshredder::io
