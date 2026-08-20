#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/application/dispatcher.hpp"
#include "molshredder/command/registry.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::application {

class Workspace;

inline constexpr unsigned int kSessionSchemaVersion = 1;

struct SessionDocument {
  unsigned int schema_version{kSessionSchemaVersion};
  std::string generator_version;
  std::vector<command::Invocation> invocations;

  friend bool operator==(const SessionDocument&, const SessionDocument&) =
      default;
};

struct SessionReplayResult {
  std::size_t applied_count{};
  std::vector<DispatchOutcome> outcomes;
};

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

}  // namespace molshredder::application
