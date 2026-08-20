#include "molshredder/application/session.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "molshredder/application/workspace.hpp"
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

operation::Result<std::string> serialize_session(
    const SessionDocument& document) {
  if (document.schema_version != kSessionSchemaVersion) {
    return operation::Result<std::string>::failure(invalid(
        "cannot serialize unsupported session schema version " +
            std::to_string(document.schema_version),
        "migrate the session document to schema version 1"));
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
  const auto converted = std::from_chars(
      version_text.data(), version_text.data() + version_text.size(),
      schema_version);
  if (converted.ec != std::errc{} ||
      converted.ptr != version_text.data() + version_text.size()) {
    return operation::Result<SessionDocument>::failure(
        invalid("session schema version is not an unsigned integer"));
  }
  if (schema_version != kSessionSchemaVersion) {
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
  document.generator_version =
      std::string{generator_line.substr(generator_prefix.size())};
  std::size_t line_number = 3U;
  while (!remaining.empty()) {
    const auto line = next_line(remaining);
    if (line.empty()) {
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

}  // namespace molshredder::application
