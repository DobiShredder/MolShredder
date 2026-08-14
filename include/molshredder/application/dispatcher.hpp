#pragma once

#include <optional>

#include "molshredder/command/registry.hpp"
#include "molshredder/command/result.hpp"

namespace molshredder::application {

struct DispatchOutcome {
  command::ResultEnvelope envelope;
  std::optional<command::Invocation> canonical_invocation;

  [[nodiscard]] bool succeeded() const noexcept {
    return envelope.succeeded();
  }
};

class Dispatcher {
 public:
  explicit Dispatcher(const command::Registry& registry)
      : registry_{registry} {}

  [[nodiscard]] DispatchOutcome dispatch(
      const command::Invocation& invocation,
      operation::TaskContext& context) const;

 private:
  const command::Registry& registry_;
};

}  // namespace molshredder::application
