#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "molshredder/command/result.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::command {

struct JsonLimits {
  std::size_t max_bytes{8U * 1024U * 1024U};
  std::size_t max_depth{64U};
};

// Strict RFC 8259 parser: valid UTF-8, no duplicate object keys, no trailing
// content, finite numbers only. Integers become int64/uint64, the rest double.
[[nodiscard]] operation::Result<Value> parse_json(std::string_view text,
                                                  JsonLimits limits = {});

// Compact serialization; fails for non-finite numbers and invalid UTF-8.
[[nodiscard]] operation::Result<std::string> to_json(const Value& value);

}  // namespace molshredder::command
