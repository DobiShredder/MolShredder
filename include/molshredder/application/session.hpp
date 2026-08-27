#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/application/dispatcher.hpp"
#include "molshredder/command/registry.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::application {

class Workspace;

inline constexpr unsigned int kSessionSchemaVersion = 2;
inline constexpr unsigned int kLegacySessionSchemaVersion = 1;

struct SessionDocument {
  unsigned int schema_version{kSessionSchemaVersion};
  unsigned int source_schema_version{kSessionSchemaVersion};
  std::string generator_version;
  // Forward-compatible metadata owned by extensions or newer producers.
  // Core preserves unknown keys byte-for-byte without interpreting them.
  std::map<std::string, std::string, std::less<>> extensions;
  std::vector<std::string> migration_notes;
  std::vector<command::Invocation> invocations;

  friend bool operator==(const SessionDocument&, const SessionDocument&) =
      default;
};

struct SessionReplayResult {
  std::size_t applied_count{};
  std::vector<DispatchOutcome> outcomes;
};

struct SessionFileLoadResult {
  SessionDocument document;
  std::filesystem::path source_path;
  bool recovered{};
  std::string primary_error;
};

struct SessionRelinkResult {
  SessionDocument document;
  std::size_t relinked_count{};
  std::vector<std::string> unresolved_paths;
};

struct SessionAutosaveResult {
  std::size_t bytes{};
  std::filesystem::path primary_path;
  std::filesystem::path recovery_path;
  bool recovery_updated{};
};

// Authoritative replay allowlist shared by product journal capture and atomic
// session restore. Export/query/process-launch operations are never replayed.
[[nodiscard]] bool is_session_persistent_descriptor(
    const command::Descriptor& descriptor) noexcept;

// Captures the normalized product mutation journal and terminal camera/stereo
// endpoint. Direct low-level Workspace mutations deliberately do not appear:
// product frontends must use the canonical operation path.
[[nodiscard]] operation::Result<SessionDocument> capture_session_document(
    const Workspace& workspace, std::string generator_version,
    std::map<std::string, std::string, std::less<>> extensions = {});

[[nodiscard]] operation::Result<std::string> serialize_session(
    const SessionDocument& document);
// Product-facing session producer. It appends (or replaces an existing
// trailing complete snapshots with) the current camera and stereo endpoint
// before serialization. It deliberately does not infer a full scene.
[[nodiscard]] operation::Result<SessionDocument>
finalize_session_camera_snapshot(SessionDocument document,
                                 const Workspace& workspace);
[[nodiscard]] operation::Result<std::string> serialize_session(
    const SessionDocument& document, const Workspace& workspace);
[[nodiscard]] operation::Result<SessionDocument> parse_session(
    std::string_view text);
[[nodiscard]] operation::Result<SessionReplayResult> replay_session(
    const SessionDocument& document, const Dispatcher& dispatcher,
    operation::TaskContext& context);
// Replays into a detached Workspace and publishes it only after every command
// succeeds. The caller's Workspace remains byte-for-byte logically unchanged
// on validation, cancellation or command failure.
[[nodiscard]] operation::Result<SessionReplayResult> replay_session_atomically(
    const SessionDocument& document, std::shared_ptr<Workspace> workspace,
    operation::TaskContext& context);
[[nodiscard]] operation::Result<std::size_t> write_session_file_atomic(
    const std::filesystem::path& path, const SessionDocument& document,
    std::size_t maximum_bytes = 64U * 1024U * 1024U);
[[nodiscard]] operation::Result<SessionAutosaveResult>
write_session_autosave_atomic(
    const std::filesystem::path& primary,
    const std::filesystem::path& recovery, const SessionDocument& document,
    std::size_t maximum_bytes = 64U * 1024U * 1024U);
[[nodiscard]] operation::Result<SessionFileLoadResult>
read_session_file_with_recovery(
    const std::filesystem::path& primary,
    std::optional<std::filesystem::path> recovery = std::nullopt,
    std::size_t maximum_bytes = 64U * 1024U * 1024U);
[[nodiscard]] operation::Result<SessionRelinkResult> relink_session_paths(
    SessionDocument document,
    const std::map<std::string, std::filesystem::path, std::less<>>& mappings,
    const std::filesystem::path& base_directory = {});

}  // namespace molshredder::application
