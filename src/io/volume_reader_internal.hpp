#pragma once

#include <string_view>

#include "molshredder/io/volume_reader.hpp"

namespace molshredder::io::detail {

[[nodiscard]] bool is_mrc(std::string_view content) noexcept;

[[nodiscard]] operation::Result<VolumeDocument>
read_mrc(std::string_view content, VolumeReadOptions options);

} // namespace molshredder::io::detail
