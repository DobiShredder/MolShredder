#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/command/registry.hpp"
#include "molshredder/operation/result.hpp"

// MolShredder plugin SDK v1: manifest, compatibility and capability
// negotiation. A plugin adds typed, read-only operations; it runs either as an
// out-of-process worker (default, the only mode for unapproved code paths) or,
// for explicitly trusted GPL-compatible plugins, in-process through the C ABI.
namespace molshredder::plugin {

inline constexpr unsigned kManifestSchemaVersion = 1;
inline constexpr unsigned kSdkAbiMajor = 1;
inline constexpr unsigned kSdkAbiMinor = 0;

enum class ExecutionMode { worker, in_process };

struct ParameterDeclaration {
  std::string name;
  command::ParameterType type{command::ParameterType::text};
  bool required{false};
  std::optional<std::string> default_value;
  std::vector<std::string> choices;
};

struct ExampleInvocation {
  std::string operation;
  command::Arguments arguments;
};

struct OperationDeclaration {
  std::string name;
  std::string summary;
  std::vector<ParameterDeclaration> parameters;
};

struct PluginManifest {
  std::string id;
  // Prefix of the operations' canonical names: `plugin <namespace> <operation>`.
  std::string name_space;
  std::string display_name;
  std::string version;
  std::string license;
  unsigned sdk_major{};
  unsigned sdk_minor{};
  ExecutionMode mode{ExecutionMode::worker};
  // Worker argv; element 0 is a path relative to the manifest directory.
  std::vector<std::string> worker_command;
  // In-process library path relative to the manifest directory.
  std::string library;
  // Extra files (scripts, data) whose content is pinned together with the
  // entry, relative to the manifest directory.
  std::vector<std::string> pinned_files;
  std::vector<std::string> required_capabilities;
  std::vector<std::string> optional_capabilities;
  std::vector<OperationDeclaration> operations;
  std::vector<ExampleInvocation> examples;
  std::filesystem::path directory;
};

// Parses and validates manifest text. `directory` is the manifest's folder;
// executable paths are resolved against it and must stay inside it.
[[nodiscard]] operation::Result<PluginManifest> parse_manifest(
    std::string_view json, const std::filesystem::path& directory);

[[nodiscard]] operation::Result<PluginManifest> load_manifest_file(
    const std::filesystem::path& manifest_path);

// The executable the host would start or load, resolved inside the manifest
// directory (no symlink escape).
[[nodiscard]] operation::Result<std::filesystem::path> resolve_entry_path(
    const PluginManifest& manifest);

struct HostInfo {
  unsigned abi_major{kSdkAbiMajor};
  unsigned abi_minor{kSdkAbiMinor};
  std::set<std::string, std::less<>> capabilities;
};

[[nodiscard]] HostInfo default_host_info();

struct Negotiation {
  std::set<std::string, std::less<>> capabilities;
  unsigned abi_major{};
  unsigned abi_minor{};
};

// Major versions must match, the plugin's minor must not exceed the host's,
// every required capability must be offered. Optional ones are dropped when
// missing.
[[nodiscard]] operation::Result<Negotiation> negotiate(
    const PluginManifest& manifest, const HostInfo& host);

// SPDX-style identifiers known to be compatible with linking into a
// GPL-3.0-or-later host (required for in-process plugins).
[[nodiscard]] bool is_gpl_compatible_license(std::string_view license);

[[nodiscard]] std::string canonical_operation_name(const PluginManifest& manifest,
                                                   const OperationDeclaration& operation);

}  // namespace molshredder::plugin
