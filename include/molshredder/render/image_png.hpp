#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "molshredder/operation/error.hpp"
#include "molshredder/render/reference_renderer.hpp"

namespace molshredder::render {

// Encodes an RGBA8 image as a non-interlaced 8-bit RGBA PNG using stored
// (uncompressed) deflate blocks, so no compression dependency is required.
// A positive dpi adds a pHYs chunk in pixels per metre (floor(dpi / 0.0254)).
[[nodiscard]] std::vector<std::uint8_t>
encode_png_rgba8(const ImageRgba8 &image, std::optional<double> dpi = {});

// Writes through a sibling temporary file and renames it into place, so an
// existing destination is untouched on failure.
[[nodiscard]] std::optional<operation::Error>
write_png_atomic(const std::filesystem::path &path, const ImageRgba8 &image,
                 std::optional<double> dpi = {});

} // namespace molshredder::render
