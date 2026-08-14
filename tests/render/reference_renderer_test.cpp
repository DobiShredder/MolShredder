#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

#include "molshredder/render/reference_renderer.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace molshredder;
  bool passed = true;
  if (argc != 2) {
    std::cerr << "expected output directory\n";
    return 1;
  }

  auto parameters = scene::Camera::create().value().parameters();
  parameters.distance = 10.0;
  parameters.projection = scene::ProjectionMode::orthographic;
  parameters.orthographic_height = 8.0;
  parameters.aspect_ratio = 1.0;
  parameters.near_clip = 0.1;
  parameters.far_clip = 100.0;
  const auto camera = scene::Camera::create(parameters);
  passed &= expect(camera.has_value(), "reference camera must be valid");

  render::RenderPacket packet;
  packet.spheres.push_back(
      {{0.0, 0.0, -2.0}, 1.0, {0.0F, 0.0F, 1.0F, 1.0F}, 12U});
  packet.spheres.push_back(
      {{0.0, 0.0, 0.0}, 1.0, {1.0F, 0.0F, 0.0F, 1.0F}, 11U});
  packet.cylinders.push_back(
      {{2.0, -1.0, 0.0}, {2.0, 1.0, 0.0}, 0.35,
       {0.0F, 1.0F, 0.0F, 1.0F}, 21U});
  packet.lines.push_back(
      {{-3.0, -1.0, 0.0}, {-2.0, 1.0, 0.0},
       {1.0F, 1.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 1.0F, 1.0F}, 3.0F, 31U});
  packet.mesh_vertices = {
      {{-1.0, -3.0, 0.0}, {0.0, 0.0, 1.0}, {0.0F, 1.0F, 1.0F, 1.0F}},
      {{1.0, -3.0, 0.0}, {0.0, 0.0, 1.0}, {0.0F, 1.0F, 1.0F, 1.0F}},
      {{0.0, -1.5, 0.0}, {0.0, 0.0, 1.0}, {0.0F, 1.0F, 1.0F, 1.0F}}};
  packet.mesh_triangles.push_back({0U, 1U, 2U, 41U});

  render::ReferenceRenderSettings settings;
  settings.width = 64U;
  settings.height = 64U;
  settings.background = {0.05F, 0.05F, 0.05F, 1.0F};
  settings.light_direction = {0.0, 0.0, 1.0};
  settings.ambient_light = 0.2;
  const auto rendered =
      render::render_reference(packet, camera.value(), settings);
  passed &= expect(rendered.has_value(), "valid packet must render");
  if (!rendered.has_value()) {
    return 1;
  }
  const auto& image = rendered.value();
  const auto center = 32U * image.width + 32U;
  const auto center_offset = center * 4U;
  passed &= expect(image.width == 64U && image.height == 64U &&
                       image.pixels.size() == 64U * 64U * 4U &&
                       image.depth.size() == 64U * 64U &&
                       image.pick_ids.size() == 64U * 64U,
                   "image buffers must match dimensions");
  passed &= expect(image.pick_ids[center] == 11U &&
                       image.pixels[center_offset] > 200U &&
                       image.pixels[center_offset + 2U] < 20U,
                   "near red sphere must win center depth and pick");
  passed &= expect(image.pick_ids[0] == 0U && image.pixels[0] == 13U,
                   "uncovered corner must retain background");
  passed &= expect(image.pick_ids[32U * image.width + 48U] == 21U,
                   "cylinder must be rasterized and pickable");

  bool found_line = false;
  for (const auto pick_id : image.pick_ids) {
    found_line = found_line || pick_id == 31U;
  }
  passed &= expect(found_line, "screen-space line must be rasterized");
  bool found_mesh = false;
  for (const auto pick_id : image.pick_ids) {
    found_mesh = found_mesh || pick_id == 41U;
  }
  passed &= expect(found_mesh, "triangle mesh must be shaded and pickable");

  const auto repeated =
      render::render_reference(packet, camera.value(), settings);
  passed &= expect(repeated.has_value() &&
                       repeated.value().pixels == image.pixels &&
                       repeated.value().pick_ids == image.pick_ids &&
                       render::image_checksum(repeated.value()) ==
                           render::image_checksum(image) &&
                       render::image_checksum(image) != 0U,
                   "reference rendering must be deterministic");

  const auto ppm_path = std::filesystem::path{argv[1]} / "reference_scene.ppm";
  passed &= expect(!render::write_ppm(ppm_path, image).has_value(),
                   "valid image must write as PPM");
  std::ifstream ppm{ppm_path, std::ios::binary};
  const std::string ppm_bytes{std::istreambuf_iterator<char>{ppm},
                              std::istreambuf_iterator<char>{}};
  passed &= expect(ppm_bytes.starts_with("P6\n64 64\n255\n") &&
                       ppm_bytes.size() == 13U + 64U * 64U * 3U,
                   "PPM must contain the expected header and RGB payload");

  auto invalid_settings = settings;
  invalid_settings.width = 0U;
  passed &= expect(!render::render_reference(packet, camera.value(),
                                              invalid_settings)
                        .has_value(),
                   "zero-sized render target must fail");
  auto invalid_packet = packet;
  invalid_packet.spheres[0].radius =
      std::numeric_limits<double>::quiet_NaN();
  passed &= expect(!render::render_reference(invalid_packet, camera.value(),
                                              settings)
                        .has_value(),
                   "invalid primitive must fail");
  invalid_packet = packet;
  invalid_packet.mesh_triangles[0].third = 99U;
  passed &= expect(!render::render_reference(invalid_packet, camera.value(),
                                              settings)
                        .has_value(),
                   "out-of-range mesh index must fail");
  auto invalid_image = image;
  invalid_image.pixels.pop_back();
  passed &= expect(render::write_ppm(ppm_path, invalid_image).has_value(),
                   "mismatched image storage must fail");

  return passed ? 0 : 1;
}
