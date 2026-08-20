#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "molshredder/io/structure_reader.hpp"
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

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string_view to_string(MolfileProviderTrust trust) noexcept;

}  // namespace molshredder::io
