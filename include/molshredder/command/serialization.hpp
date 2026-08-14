#pragma once

#include <string>
#include <string_view>

#include "molshredder/command/registry.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::command {

inline constexpr unsigned int kInvocationSchemaVersion = 1;

[[nodiscard]] std::string serialize(const Invocation& invocation);

[[nodiscard]] operation::Result<Invocation> parse_canonical(
    std::string_view command);

[[nodiscard]] InvocationRecord make_record(const Invocation& invocation,
                                           InvocationSource source,
                                           unsigned long long sequence);

}  // namespace molshredder::command
