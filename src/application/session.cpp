#include "molshredder/application/session.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <initializer_list>
#include <iterator>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "molshredder/application/workspace.hpp"
#include "molshredder/application/default_registry.hpp"
#include "molshredder/command/serialization.hpp"
#include "molshredder/operation/error.hpp"

namespace molshredder::application {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

std::string_view next_line(std::string_view& remaining) {
  const auto end = remaining.find('\n');
  auto line = end == std::string_view::npos ? remaining
                                            : remaining.substr(0U, end);
  remaining = end == std::string_view::npos
                  ? std::string_view{}
                  : remaining.substr(end + 1U);
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
  return line;
}

operation::Result<std::string> number_text(double value) {
  std::array<char, 64U> buffer{};
  const auto converted = std::to_chars(
      buffer.data(), buffer.data() + buffer.size(), value,
      std::chars_format::general, std::numeric_limits<double>::max_digits10);
  if (converted.ec != std::errc{}) {
    return operation::Result<std::string>::failure(
        invalid("camera snapshot number could not be serialized"));
  }
  return operation::Result<std::string>::success(
      std::string{buffer.data(), converted.ptr});
}

constexpr std::array<std::string_view, 17U> kCompleteCameraArguments{
    "aspect-ratio",       "distance",      "far-clip",
    "field-of-view",      "model-origin-x", "model-origin-y",
    "model-origin-z",     "near-clip",     "orientation-w",
    "orientation-x",      "orientation-y", "orientation-z",
    "orthographic-height", "projection",    "target-x",
    "target-y",           "target-z"};

bool is_complete_camera_snapshot(const command::Invocation& invocation) {
  if (invocation.canonical_name != "view set" ||
      invocation.arguments.size() != kCompleteCameraArguments.size()) {
    return false;
  }
  return std::all_of(
      kCompleteCameraArguments.begin(), kCompleteCameraArguments.end(),
      [&invocation](std::string_view name) {
        return invocation.arguments.contains(name);
      });
}

constexpr std::array<std::string_view, 6U> kCompleteStereoArguments{
    "anaglyph-mode", "angle-scale", "enabled", "mode", "shift-percent",
    "swap-eyes"};

bool is_complete_stereo_snapshot(const command::Invocation& invocation) {
  if (invocation.canonical_name != "stereo set" ||
      invocation.arguments.size() != kCompleteStereoArguments.size())
    return false;
  return std::all_of(
      kCompleteStereoArguments.begin(), kCompleteStereoArguments.end(),
      [&invocation](std::string_view name) {
        return invocation.arguments.contains(name);
      });
}

operation::Result<command::Invocation> camera_snapshot_invocation(
    const scene::CameraParameters& camera) {
  command::Arguments arguments;
  const auto add_number = [&arguments](std::string name, double value)
      -> std::optional<operation::Error> {
    const auto encoded = number_text(value);
    if (!encoded.has_value()) return encoded.error();
    arguments.emplace(std::move(name), encoded.value());
    return std::nullopt;
  };
  for (const auto& [name, value] :
       std::initializer_list<std::pair<std::string, double>>{
           {"aspect-ratio", camera.aspect_ratio},
           {"distance", camera.distance},
           {"far-clip", camera.far_clip},
           {"field-of-view", camera.vertical_field_of_view_radians},
           {"model-origin-x", camera.model_origin.x},
           {"model-origin-y", camera.model_origin.y},
           {"model-origin-z", camera.model_origin.z},
           {"near-clip", camera.near_clip},
           {"orientation-w", camera.orientation.w},
           {"orientation-x", camera.orientation.x},
           {"orientation-y", camera.orientation.y},
           {"orientation-z", camera.orientation.z},
           {"orthographic-height", camera.orthographic_height},
           {"target-x", camera.target.x},
           {"target-y", camera.target.y},
           {"target-z", camera.target.z}}) {
    if (const auto error = add_number(name, value); error.has_value()) {
      return operation::Result<command::Invocation>::failure(*error);
    }
  }
  arguments.emplace(
      "projection", camera.projection == scene::ProjectionMode::orthographic
                        ? "orthographic"
                        : "perspective");
  return operation::Result<command::Invocation>::success(
      command::Invocation{"view set", std::move(arguments)});
}

operation::Result<command::Invocation> stereo_snapshot_invocation(
    const scene::StereoParameters& stereo) {
  const auto shift = number_text(stereo.shift_percent);
  if (!shift.has_value())
    return operation::Result<command::Invocation>::failure(shift.error());
  const auto angle = number_text(stereo.angle_scale);
  if (!angle.has_value())
    return operation::Result<command::Invocation>::failure(angle.error());
  return operation::Result<command::Invocation>::success(command::Invocation{
      "stereo set",
      {{"angle-scale", angle.value()},
       {"anaglyph-mode", std::string{scene::to_string(stereo.anaglyph_mode)}},
       {"enabled", stereo.enabled ? "true" : "false"},
       {"mode", std::string{scene::to_string(stereo.mode)}},
       {"shift-percent", shift.value()},
       {"swap-eyes", stereo.swap_eyes ? "true" : "false"}}});
}

}  // namespace

bool is_session_persistent_descriptor(
    const command::Descriptor& descriptor) noexcept {
  const auto& name = descriptor.canonical_name;
  return descriptor.undo_policy == command::UndoPolicy::undoable ||
         name.starts_with("analyze ") || name.starts_with("measure ") ||
         name == "edit undo" || name == "edit redo" ||
         name == "edit history";
}

operation::Result<SessionDocument> capture_session_document(
    const Workspace& workspace, std::string generator_version,
    std::map<std::string, std::string, std::less<>> extensions) {
  SessionDocument document;
  document.generator_version = std::move(generator_version);
  document.extensions = std::move(extensions);
  document.invocations.assign(workspace.session_invocations().begin(),
                              workspace.session_invocations().end());
  return finalize_session_camera_snapshot(std::move(document), workspace);
}

operation::Result<std::string> serialize_session(
    const SessionDocument& document) {
  if (document.schema_version != kSessionSchemaVersion) {
    return operation::Result<std::string>::failure(invalid(
        "cannot serialize unsupported session schema version " +
            std::to_string(document.schema_version),
            "migrate the session document to schema version 2"));
  }
  if (document.generator_version.empty() ||
      document.generator_version.find_first_of("\r\n \t") !=
          std::string::npos) {
    return operation::Result<std::string>::failure(invalid(
        "session generator version must be one non-empty token"));
  }
  std::string result = "molshredder-session " +
                       std::to_string(document.schema_version) + "\n" +
                       "generator " + document.generator_version + "\n";
  for (const auto& [key, value] : document.extensions) {
    if (key.empty() || key.find_first_of("\r\n") != std::string::npos ||
        value.find_first_of("\r\n") != std::string::npos) {
      return operation::Result<std::string>::failure(invalid(
          "session extension key/value must be non-empty-line-safe text"));
    }
    result += "metadata ";
    result += command::serialize(command::Invocation{
        "session metadata", {{"key", key}, {"value", value}}});
    result += '\n';
  }
  for (const auto& invocation : document.invocations) {
    if (invocation.canonical_name.empty()) {
      return operation::Result<std::string>::failure(
          invalid("session contains an empty command name"));
    }
    result += command::serialize(invocation);
    result += '\n';
  }
  return operation::Result<std::string>::success(std::move(result));
}

operation::Result<SessionDocument> finalize_session_camera_snapshot(
    SessionDocument document, const Workspace& workspace) {
  if (document.schema_version != kSessionSchemaVersion) {
    return operation::Result<SessionDocument>::failure(invalid(
        "cannot finalize unsupported session schema version " +
        std::to_string(document.schema_version)));
  }
  const auto snapshot =
      camera_snapshot_invocation(workspace.camera().parameters());
  if (!snapshot.has_value()) {
    return operation::Result<SessionDocument>::failure(snapshot.error());
  }
  const auto stereo_snapshot = stereo_snapshot_invocation(workspace.stereo());
  if (!stereo_snapshot.has_value())
    return operation::Result<SessionDocument>::failure(
        stereo_snapshot.error());
  if (!document.invocations.empty() &&
      is_complete_stereo_snapshot(document.invocations.back()))
    document.invocations.pop_back();
  if (!document.invocations.empty() &&
      is_complete_camera_snapshot(document.invocations.back())) {
    document.invocations.back() = snapshot.value();
  } else {
    document.invocations.push_back(snapshot.value());
  }
  document.invocations.push_back(stereo_snapshot.value());
  return operation::Result<SessionDocument>::success(std::move(document));
}

operation::Result<std::string> serialize_session(
    const SessionDocument& document, const Workspace& workspace) {
  const auto finalized = finalize_session_camera_snapshot(document, workspace);
  if (!finalized.has_value()) {
    return operation::Result<std::string>::failure(finalized.error());
  }
  return serialize_session(finalized.value());
}

operation::Result<SessionDocument> parse_session(std::string_view text) {
  auto remaining = text;
  const auto header = next_line(remaining);
  constexpr std::string_view prefix{"molshredder-session "};
  if (!header.starts_with(prefix)) {
    return operation::Result<SessionDocument>::failure(invalid(
        "session header is missing or invalid",
        "use a file beginning with 'molshredder-session VERSION'"));
  }
  unsigned int schema_version{};
  const auto version_text = header.substr(prefix.size());
  const auto converted = molshredder::core::from_chars(
      version_text.data(), version_text.data() + version_text.size(),
      schema_version);
  if (converted.ec != std::errc{} ||
      converted.ptr != version_text.data() + version_text.size()) {
    return operation::Result<SessionDocument>::failure(
        invalid("session schema version is not an unsigned integer"));
  }
  if (schema_version != kSessionSchemaVersion &&
      schema_version != kLegacySessionSchemaVersion) {
    return operation::Result<SessionDocument>::failure(invalid(
        "unsupported session schema version " +
            std::to_string(schema_version),
        "open the session with a compatible MolShredder version or migrate it"));
  }

  const auto generator_line = next_line(remaining);
  constexpr std::string_view generator_prefix{"generator "};
  if (!generator_line.starts_with(generator_prefix) ||
      generator_line.size() == generator_prefix.size() ||
      generator_line.substr(generator_prefix.size()).find_first_of(" \t") !=
          std::string_view::npos) {
    return operation::Result<SessionDocument>::failure(
        invalid("session generator line is missing or invalid"));
  }
  SessionDocument document;
  document.source_schema_version = schema_version;
  document.schema_version = kSessionSchemaVersion;
  if (schema_version == kLegacySessionSchemaVersion) {
    document.migration_notes.push_back(
        "migrated session schema 1 command journal to schema 2");
  }
  document.generator_version =
      std::string{generator_line.substr(generator_prefix.size())};
  std::size_t line_number = 3U;
  while (!remaining.empty()) {
    const auto line = next_line(remaining);
    if (line.empty()) {
      ++line_number;
      continue;
    }
    constexpr std::string_view metadata_prefix{"metadata "};
    if (schema_version == kSessionSchemaVersion &&
        line.starts_with(metadata_prefix)) {
      const auto metadata =
          command::parse_canonical(line.substr(metadata_prefix.size()));
      if (!metadata.has_value() ||
          metadata.value().canonical_name != "session metadata" ||
          metadata.value().arguments.size() != 2U ||
          !metadata.value().arguments.contains("key") ||
          !metadata.value().arguments.contains("value") ||
          metadata.value().arguments.at("key").empty()) {
        return operation::Result<SessionDocument>::failure(invalid(
            "session line " + std::to_string(line_number) +
            ": malformed schema 2 metadata record"));
      }
      const auto& key = metadata.value().arguments.at("key");
      if (!document.extensions
               .emplace(key, metadata.value().arguments.at("value"))
               .second) {
        return operation::Result<SessionDocument>::failure(invalid(
            "session line " + std::to_string(line_number) +
            ": duplicate metadata key " + key));
      }
      ++line_number;
      continue;
    }
    const auto invocation = command::parse_canonical(line);
    if (!invocation.has_value()) {
      auto error = invocation.error();
      error.message = "session line " + std::to_string(line_number) +
                      ": " + error.message;
      return operation::Result<SessionDocument>::failure(std::move(error));
    }
    document.invocations.push_back(invocation.value());
    ++line_number;
  }
  return operation::Result<SessionDocument>::success(std::move(document));
}

operation::Result<SessionReplayResult> replay_session(
    const SessionDocument& document, const Dispatcher& dispatcher,
    operation::TaskContext& context) {
  if (document.schema_version != kSessionSchemaVersion) {
    return operation::Result<SessionReplayResult>::failure(invalid(
        "cannot replay unsupported session schema version " +
        std::to_string(document.schema_version)));
  }
  SessionReplayResult replay;
  replay.outcomes.reserve(document.invocations.size());
  for (std::size_t index = 0; index < document.invocations.size(); ++index) {
    if (context.cancellation.is_cancelled()) {
      return operation::Result<SessionReplayResult>::failure(operation::Error{
          operation::ErrorCode::cancelled,
          "session replay cancelled before command " +
              std::to_string(index + 1U),
          {}});
    }
    auto outcome = dispatcher.dispatch(document.invocations[index], context);
    if (!outcome.succeeded()) {
      auto error = std::get<operation::Error>(outcome.envelope.payload);
      error.message = "session command " + std::to_string(index + 1U) +
                      " failed: " + error.message;
      return operation::Result<SessionReplayResult>::failure(std::move(error));
    }
    replay.outcomes.push_back(std::move(outcome));
    ++replay.applied_count;
  }
  return operation::Result<SessionReplayResult>::success(std::move(replay));
}

operation::Result<SessionReplayResult> replay_session_atomically(
    const SessionDocument& document, std::shared_ptr<Workspace> workspace,
    operation::TaskContext& context) {
  if (!workspace) {
    return operation::Result<SessionReplayResult>::failure(
        invalid("atomic session replay requires a Workspace"));
  }
  auto candidate = std::make_shared<Workspace>();
  auto registry = make_default_registry(candidate);
  std::map<std::string, bool, std::less<>> safe_commands;
  for (const auto& descriptor : registry.descriptors()) {
    if (is_session_persistent_descriptor(descriptor))
      safe_commands.emplace(descriptor.canonical_name, true);
  }
  for (std::size_t index = 0; index < document.invocations.size(); ++index) {
    if (!safe_commands.contains(document.invocations[index].canonical_name)) {
      return operation::Result<SessionReplayResult>::failure(operation::Error{
          operation::ErrorCode::unsupported,
          "session command " + std::to_string(index + 1U) +
              " is not allowed by the product replay policy: " +
              document.invocations[index].canonical_name,
          "remove query, export, script or process-launch commands from the session"});
    }
  }
  const Dispatcher dispatcher{registry};
  auto replay = replay_session(document, dispatcher, context);
  if (!replay.has_value()) {
    return operation::Result<SessionReplayResult>::failure(replay.error());
  }
  *workspace = std::move(*candidate);
  return replay;
}

operation::Result<std::size_t> write_session_file_atomic(
    const std::filesystem::path& path, const SessionDocument& document,
    std::size_t maximum_bytes) {
  namespace fs = std::filesystem;
  if (path.empty() || maximum_bytes == 0U) {
    return operation::Result<std::size_t>::failure(
        invalid("session output path and byte budget must be non-empty"));
  }
  const auto serialized = serialize_session(document);
  if (!serialized.has_value())
    return operation::Result<std::size_t>::failure(serialized.error());
  if (serialized.value().size() > maximum_bytes) {
    return operation::Result<std::size_t>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "serialized session exceeds the file byte budget", {}});
  }
  std::error_code error;
  const auto target_exists = fs::exists(path, error);
  if (error ||
      (target_exists && (!fs::is_regular_file(path, error) || error))) {
    return operation::Result<std::size_t>::failure(invalid(
        "session output cannot be inspected or is not a regular file: " +
        path.string()));
  }
  const auto parent = path.has_parent_path() ? path.parent_path() : fs::path{"."};
  if (!fs::is_directory(parent, error) || error) {
    return operation::Result<std::size_t>::failure(
        invalid("session output parent directory does not exist"));
  }
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto temporary = fs::path{path.string() + ".tmp-" +
                                  std::to_string(nonce)};
  const auto backup = fs::path{path.string() + ".previous-" +
                               std::to_string(nonce)};
  std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
  if (!stream) {
    return operation::Result<std::size_t>::failure(
        invalid("failed to create temporary session file"));
  }
  stream.write(serialized.value().data(),
               static_cast<std::streamsize>(serialized.value().size()));
  stream.flush();
  if (!stream) {
    stream.close();
    fs::remove(temporary, error);
    return operation::Result<std::size_t>::failure(
        invalid("failed while writing temporary session file"));
  }
  stream.close();
  if (target_exists) {
    fs::rename(path, backup, error);
    if (error) {
      std::error_code cleanup_error;
      fs::remove(temporary, cleanup_error);
      return operation::Result<std::size_t>::failure(invalid(
          "failed to stage the previous session file: " + error.message()));
    }
  }
  fs::rename(temporary, path, error);
  if (error) {
    const auto publish_error = error.message();
    std::error_code cleanup_error;
    fs::remove(temporary, cleanup_error);
    if (target_exists) {
      std::error_code rollback_error;
      fs::rename(backup, path, rollback_error);
      if (rollback_error) {
        return operation::Result<std::size_t>::failure(operation::Error{
            operation::ErrorCode::internal,
            "failed to publish or restore the previous session file; "
            "recovery copy remains at " +
                backup.string(),
            "preserve the recovery copy and restore it manually"});
      }
    }
    return operation::Result<std::size_t>::failure(invalid(
        target_exists
            ? "failed to publish the session file; the previous file was "
                  "restored: " +
                  publish_error
            : "failed to publish the session file: " + publish_error));
  }
  if (target_exists) fs::remove(backup, error);
  return operation::Result<std::size_t>::success(serialized.value().size());
}

operation::Result<SessionAutosaveResult> write_session_autosave_atomic(
    const std::filesystem::path& primary,
    const std::filesystem::path& recovery, const SessionDocument& document,
    std::size_t maximum_bytes) {
  namespace fs = std::filesystem;
  if (primary.empty() || recovery.empty() || primary == recovery ||
      maximum_bytes == 0U) {
    return operation::Result<SessionAutosaveResult>::failure(invalid(
        "autosave primary/recovery must be distinct non-empty paths with a positive budget"));
  }
  const auto primary_parent =
      primary.has_parent_path() ? primary.parent_path() : fs::path{"."};
  const auto recovery_parent =
      recovery.has_parent_path() ? recovery.parent_path() : fs::path{"."};
  std::error_code error;
  if (fs::weakly_canonical(primary_parent, error) !=
          fs::weakly_canonical(recovery_parent, error) ||
      error) {
    return operation::Result<SessionAutosaveResult>::failure(invalid(
        "autosave primary and recovery must share one existing directory"));
  }
  if (!fs::is_directory(primary_parent, error) || error) {
    return operation::Result<SessionAutosaveResult>::failure(
        invalid("autosave parent directory does not exist"));
  }
  const auto serialized = serialize_session(document);
  if (!serialized.has_value())
    return operation::Result<SessionAutosaveResult>::failure(
        serialized.error());
  if (serialized.value().size() > maximum_bytes) {
    return operation::Result<SessionAutosaveResult>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "serialized autosave exceeds the file byte budget", {}});
  }
  const auto nonce = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto candidate =
      primary_parent / (primary.filename().string() + ".new-" + nonce);
  const auto stale_recovery =
      primary_parent / (recovery.filename().string() + ".old-" + nonce);
  {
    std::ofstream output{candidate, std::ios::binary | std::ios::trunc};
    if (!output) {
      return operation::Result<SessionAutosaveResult>::failure(
          invalid("failed to create autosave candidate"));
    }
    output.write(serialized.value().data(),
                 static_cast<std::streamsize>(serialized.value().size()));
    output.flush();
    if (!output) {
      output.close();
      fs::remove(candidate, error);
      return operation::Result<SessionAutosaveResult>::failure(
          invalid("failed while writing autosave candidate"));
    }
  }

  const auto primary_exists = fs::exists(primary, error);
  if (error) {
    fs::remove(candidate, error);
    return operation::Result<SessionAutosaveResult>::failure(
        invalid("autosave primary could not be inspected"));
  }
  const auto recovery_exists = fs::exists(recovery, error);
  if (error) {
    fs::remove(candidate, error);
    return operation::Result<SessionAutosaveResult>::failure(
        invalid("autosave recovery could not be inspected"));
  }
  if ((primary_exists && !fs::is_regular_file(primary, error)) ||
      (recovery_exists && !fs::is_regular_file(recovery, error)) || error) {
    fs::remove(candidate, error);
    return operation::Result<SessionAutosaveResult>::failure(invalid(
        "autosave primary/recovery targets must be regular files when present"));
  }

  if (primary_exists && recovery_exists) {
    fs::rename(recovery, stale_recovery, error);
    if (error) {
      fs::remove(candidate, error);
      return operation::Result<SessionAutosaveResult>::failure(
          invalid("failed to stage the previous autosave recovery"));
    }
  }
  if (primary_exists) {
    fs::rename(primary, recovery, error);
    if (error) {
      if (primary_exists && recovery_exists) {
        std::error_code rollback_error;
        fs::rename(stale_recovery, recovery, rollback_error);
      }
      fs::remove(candidate, error);
      return operation::Result<SessionAutosaveResult>::failure(
          invalid("failed to rotate autosave primary to recovery"));
    }
  }
  fs::rename(candidate, primary, error);
  if (error) {
    std::error_code rollback_error;
    if (primary_exists) fs::rename(recovery, primary, rollback_error);
    if (primary_exists && recovery_exists)
      fs::rename(stale_recovery, recovery, rollback_error);
    fs::remove(candidate, rollback_error);
    return operation::Result<SessionAutosaveResult>::failure(invalid(
        "failed to publish autosave primary; previous generation was restored"));
  }
  if (primary_exists && recovery_exists)
    fs::remove(stale_recovery, error);
  return operation::Result<SessionAutosaveResult>::success(
      {serialized.value().size(), primary, recovery, primary_exists});
}

namespace {

operation::Result<std::string> read_bounded_session_file(
    const std::filesystem::path& path, std::size_t maximum_bytes) {
  namespace fs = std::filesystem;
  std::error_code error;
  if (!fs::is_regular_file(path, error) || error) {
    return operation::Result<std::string>::failure(
        invalid("session path is not a readable regular file: " +
                path.string()));
  }
  const auto size = fs::file_size(path, error);
  if (error || size > maximum_bytes) {
    return operation::Result<std::string>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "session file exceeds the read byte budget", {}});
  }
  std::ifstream stream{path, std::ios::binary};
  std::string text{std::istreambuf_iterator<char>{stream},
                   std::istreambuf_iterator<char>{}};
  if (!stream || text.size() != size) {
    return operation::Result<std::string>::failure(
        invalid("session file could not be read completely"));
  }
  return operation::Result<std::string>::success(std::move(text));
}

bool is_external_path_command(std::string_view command_name) {
  return command_name == "load" || command_name == "traj load" ||
         command_name == "volume load";
}

}  // namespace

operation::Result<SessionFileLoadResult> read_session_file_with_recovery(
    const std::filesystem::path& primary,
    std::optional<std::filesystem::path> recovery, std::size_t maximum_bytes) {
  const auto primary_text = read_bounded_session_file(primary, maximum_bytes);
  if (primary_text.has_value()) {
    const auto parsed = parse_session(primary_text.value());
    if (parsed.has_value()) {
      return operation::Result<SessionFileLoadResult>::success(
          {parsed.value(), primary, false, {}});
    }
    if (!recovery.has_value())
      return operation::Result<SessionFileLoadResult>::failure(parsed.error());
    const auto recovery_text =
        read_bounded_session_file(*recovery, maximum_bytes);
    if (!recovery_text.has_value())
      return operation::Result<SessionFileLoadResult>::failure(
          recovery_text.error());
    const auto recovered = parse_session(recovery_text.value());
    if (!recovered.has_value())
      return operation::Result<SessionFileLoadResult>::failure(
          recovered.error());
    return operation::Result<SessionFileLoadResult>::success(
        {recovered.value(), *recovery, true, parsed.error().message});
  }
  if (!recovery.has_value())
    return operation::Result<SessionFileLoadResult>::failure(
        primary_text.error());
  const auto recovery_text = read_bounded_session_file(*recovery, maximum_bytes);
  if (!recovery_text.has_value())
    return operation::Result<SessionFileLoadResult>::failure(
        recovery_text.error());
  const auto recovered = parse_session(recovery_text.value());
  if (!recovered.has_value())
    return operation::Result<SessionFileLoadResult>::failure(recovered.error());
  return operation::Result<SessionFileLoadResult>::success(
      {recovered.value(), *recovery, true, primary_text.error().message});
}

operation::Result<SessionRelinkResult> relink_session_paths(
    SessionDocument document,
    const std::map<std::string, std::filesystem::path, std::less<>>& mappings,
    const std::filesystem::path& base_directory) {
  namespace fs = std::filesystem;
  SessionRelinkResult result{std::move(document), 0U, {}};
  for (auto& invocation : result.document.invocations) {
    if (!is_external_path_command(invocation.canonical_name)) continue;
    const auto found = invocation.arguments.find("path");
    if (found == invocation.arguments.end()) continue;
    auto current = fs::path{found->second};
    if (current.is_relative() && !base_directory.empty())
      current = base_directory / current;
    std::error_code error;
    if (fs::is_regular_file(current, error) && !error) continue;
    const auto mapping = mappings.find(found->second);
    if (mapping == mappings.end()) {
      result.unresolved_paths.push_back(found->second);
      continue;
    }
    const auto replacement = fs::canonical(mapping->second, error);
    if (error || !fs::is_regular_file(replacement, error) || error) {
      return operation::Result<SessionRelinkResult>::failure(invalid(
          "session relink target is not a readable regular file: " +
              mapping->second.string()));
    }
    found->second = replacement.string();
    ++result.relinked_count;
  }
  return operation::Result<SessionRelinkResult>::success(std::move(result));
}

}  // namespace molshredder::application
