#include "molshredder/application/dispatcher.hpp"

#include "molshredder/command/serialization.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::application {

DispatchOutcome Dispatcher::dispatch(
    const command::Invocation& invocation,
    operation::TaskContext& context) const {
  const auto normalized = registry_.normalize(invocation);
  if (!normalized.has_value()) {
    const auto failure =
        operation::Result<command::Response>::failure(normalized.error());
    return DispatchOutcome{
        command::make_envelope(command::serialize(invocation), failure),
        std::nullopt};
  }
  const auto result = registry_.invoke(normalized.value(), context);
  return DispatchOutcome{
      command::make_envelope(command::serialize(normalized.value()), result),
      normalized.value()};
}

}  // namespace molshredder::application
