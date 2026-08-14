#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "molshredder/operation/error.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/render/packet.hpp"
#include "molshredder/scene/camera.hpp"

namespace molshredder::render {

struct ImageRgba8 {
  std::size_t width{};
  std::size_t height{};
  std::vector<std::uint8_t> pixels;
  std::vector<double> depth;
  std::vector<std::uint64_t> pick_ids;
};

struct ReferenceRenderSettings {
  std::size_t width{640};
  std::size_t height{480};
  ColorRgba background{0.0F, 0.0F, 0.0F, 1.0F};
  model::Vec3d light_direction{0.3, 0.4, 1.0};
  double ambient_light{0.25};
};

[[nodiscard]] operation::Result<ImageRgba8> render_reference(
    const RenderPacket& packet, const scene::Camera& camera,
    ReferenceRenderSettings settings = {});

[[nodiscard]] std::optional<operation::Error> write_ppm(
    const std::filesystem::path& path, const ImageRgba8& image);

[[nodiscard]] std::uint64_t image_checksum(const ImageRgba8& image) noexcept;

}  // namespace molshredder::render
