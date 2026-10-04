#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "molshredder/operation/result.hpp"
#include "molshredder/plugin/manifest.hpp"

namespace molshredder::plugin {

inline constexpr const char* kManifestFileName = "molshredder-plugin.json";

// One plugin the user explicitly approved, pinned by content hash. Nothing in
// the store is ever executed by discovery; the hashes are checked again right
// before every start or load.
struct TrustEntry {
  std::string id;
  std::string version;
  ExecutionMode mode{ExecutionMode::worker};
  std::string manifest_path;
  std::string manifest_sha256;
  std::string entry_path;
  std::string entry_sha256;
  // Extra pinned files (manifest `execution.files`): relative path, SHA-256.
  std::vector<std::pair<std::string, std::string>> pinned_files;
  bool allow_in_process{};
  std::string approved_at;
};

// The user's store location: $MOLSHREDDER_PLUGIN_TRUST_STORE, otherwise
// ~/.molshredder/plugin-trust.json (%USERPROFILE% on Windows). Empty when no
// home directory is known.
[[nodiscard]] std::filesystem::path default_trust_store_path();

[[nodiscard]] operation::Result<std::string> sha256_of_file(
    const std::filesystem::path& path);

class TrustStore {
 public:
  // A missing file is an empty store; an unreadable or malformed file is an
  // error (it is never silently treated as empty).
  [[nodiscard]] static operation::Result<TrustStore> load(
      const std::filesystem::path& path);

  [[nodiscard]] const std::vector<TrustEntry>& entries() const noexcept { return entries_; }
  [[nodiscard]] const TrustEntry* find(const std::string& id) const;

  // Hashes the manifest and entry file and records them, replacing an earlier
  // approval of the same id. Does not write the file.
  [[nodiscard]] operation::Result<TrustEntry> approve(
      const PluginManifest& manifest, const std::filesystem::path& manifest_path,
      bool allow_in_process, std::string approved_at);
  [[nodiscard]] bool revoke(const std::string& id);

  // Failure-atomic write (temporary file, then rename).
  [[nodiscard]] operation::Result<bool> save() const;
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
  std::vector<TrustEntry> entries_;
};

struct VerifiedPlugin {
  PluginManifest manifest;
  std::filesystem::path entry;
  TrustEntry trust;
};

// Reloads the manifest and re-hashes manifest and entry; any difference from
// the approved hashes (or a moved path) is an error naming what changed.
[[nodiscard]] operation::Result<VerifiedPlugin> verify_approved(const TrustEntry& entry);

enum class TrustState { approved, unapproved, changed, invalid };

struct DiscoveredPlugin {
  std::filesystem::path manifest_path;
  TrustState state{TrustState::invalid};
  std::string id;
  std::string version;
  std::string mode;
  std::string detail;
};

// Lists manifests directly inside `directory` and in its immediate
// subdirectories. Reads files only; nothing is executed or loaded.
[[nodiscard]] operation::Result<std::vector<DiscoveredPlugin>> discover(
    const std::filesystem::path& directory, const TrustStore& store);

[[nodiscard]] std::string to_string(TrustState state);

}  // namespace molshredder::plugin
