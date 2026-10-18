#pragma once

#include <string_view>

#include "molshredder/io/volume_reader.hpp"

namespace molshredder::io::detail {

[[nodiscard]] bool is_mrc(std::string_view content) noexcept;

[[nodiscard]] operation::Result<VolumeDocument>
read_mrc(std::string_view content, VolumeReadOptions options);

[[nodiscard]] operation::Result<VolumeDocument>
read_xplor(std::string_view content, VolumeReadOptions options);
[[nodiscard]] operation::Result<VolumeDocument>
read_brix(std::string_view content, VolumeReadOptions options);
[[nodiscard]] operation::Result<VolumeDocument>
read_phi(std::string_view content, VolumeReadOptions options);
[[nodiscard]] operation::Result<VolumeDocument>
read_grd(std::string_view content, VolumeReadOptions options);
[[nodiscard]] operation::Result<VolumeDocument>
read_fld(std::string_view content, VolumeReadOptions options);
[[nodiscard]] operation::Result<VolumeDocument>
read_acnt(std::string_view content, VolumeReadOptions options);

} // namespace molshredder::io::detail
