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
[[nodiscard]] operation::Result<SessionDocument> parse_session(
    std::string_view text);
[[nodiscard]] operation::Result<SessionReplayResult> replay_session(
    const SessionDocument& document, const Dispatcher& dispatcher,
    operation::TaskContext& context);

}  // namespace molshredder::application
