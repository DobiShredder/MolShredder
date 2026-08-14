#include "molshredder/application/session.hpp"

#include <charconv>
#include <string>
#include <utility>

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
