#pragma once

#include <string>
#include <string_view>

namespace molshredder::core {

// Lowercase hexadecimal SHA-256 (FIPS 180-4) of the given bytes.
[[nodiscard]] std::string sha256_hex(std::string_view bytes);

}  // namespace molshredder::core
