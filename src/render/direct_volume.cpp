#include "molshredder/render/direct_volume.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "molshredder/operation/error.hpp"
#include "molshredder/scene/math.hpp"

namespace molshredder::render {
namespace {

constexpr double kEpsilon = 1.0e-12;

operation::Error invalid(std::string message) {
  return {operation::ErrorCode::invalid_argument, std::move(message), {}};
}

operation::Error exhausted(std::string message) {
  return {operation::ErrorCode::resource_exhausted, std::move(message),
          "increase the texture budget or use a smaller volume"};
}

operation::Result<std::size_t> checked_texture_bytes(std::size_t count,
                                                     std::size_t element_size,
                                                     std::string_view name) {
  if (count != 0U &&
      element_size > std::numeric_limits<std::size_t>::max() / count) {
    return operation::Result<std::size_t>::failure(
        exhausted(std::string{name} + " byte count overflows size_t"));
  }
  return operation::Result<std::size_t>::success(count * element_size);
}

bool cancelled(operation::TaskContext *context) noexcept {
  return context != nullptr && context->cancellation.is_cancelled();
}

struct Ray {
  model::Vec3d origin;
  model::Vec3d direction;
};

Ray pixel_ray(const scene::Camera &camera, std::size_t x, std::size_t y,
              std::size_t width, std::size_t height) {
  using scene::operator+;
  using scene::operator*;
  const auto &parameters = camera.parameters();
  const auto nx = 2.0 * (static_cast<double>(x) + 0.5) /
                      static_cast<double>(width) -
                  1.0;
  const auto ny = 1.0 - 2.0 * (static_cast<double>(y) + 0.5) /
                            static_cast<double>(height);
  if (parameters.projection == scene::ProjectionMode::orthographic) {
    const auto half_height = parameters.orthographic_height * 0.5;
    return {camera.position() +
                camera.right() *
                    (nx * half_height * parameters.aspect_ratio) +
                camera.up() * (ny * half_height),
            camera.forward()};
  }
  const auto tangent =
      std::tan(parameters.vertical_field_of_view_radians * 0.5);
  return {camera.position(),
          scene::normalized(camera.forward() +
                            camera.right() *
                                (nx * tangent * parameters.aspect_ratio) +
                            camera.up() * (ny * tangent))};
}

model::Vec3d world_to_logical_vector(
    model::Vec3d value, const std::array<model::Vec3d, 3U> &deltas) {
  const auto reciprocal_x = scene::cross(deltas[1], deltas[2]);
  const auto reciprocal_y = scene::cross(deltas[2], deltas[0]);
  const auto reciprocal_z = scene::cross(deltas[0], deltas[1]);
  const auto determinant = scene::dot(deltas[0], reciprocal_x);
  return {scene::dot(value, reciprocal_x) / determinant,
          scene::dot(value, reciprocal_y) / determinant,
          scene::dot(value, reciprocal_z) / determinant};
}

bool intersect_box(model::Vec3d origin, model::Vec3d direction,
                   model::Vec3d maximum, double &near_t, double &far_t) {
  near_t = 0.0;
  far_t = std::numeric_limits<double>::infinity();
  const std::array origins{origin.x, origin.y, origin.z};
  const std::array directions{direction.x, direction.y, direction.z};
  const std::array maxima{maximum.x, maximum.y, maximum.z};
  for (std::size_t axis = 0; axis < 3U; ++axis) {
    if (std::abs(directions[axis]) < kEpsilon) {
      if (origins[axis] < 0.0 || origins[axis] > maxima[axis])
        return false;
      continue;
    }
    auto first = -origins[axis] / directions[axis];
    auto second = (maxima[axis] - origins[axis]) / directions[axis];
    if (first > second)
      std::swap(first, second);
    near_t = std::max(near_t, first);
    far_t = std::min(far_t, second);
    if (near_t > far_t)
      return false;
  }
  return far_t >= 0.0;
}

float trilinear(const DirectVolumeData &volume, model::Vec3d logical) {
  const auto lower = [](double value, std::size_t extent) {
    return std::min(static_cast<std::size_t>(std::floor(std::max(0.0, value))),
                    extent - 1U);
  };
  const auto x0 = lower(logical.x, volume.shape.x);
  const auto y0 = lower(logical.y, volume.shape.y);
  const auto z0 = lower(logical.z, volume.shape.z);
  const auto x1 = std::min(x0 + 1U, volume.shape.x - 1U);
  const auto y1 = std::min(y0 + 1U, volume.shape.y - 1U);
  const auto z1 = std::min(z0 + 1U, volume.shape.z - 1U);
  const auto fx = std::clamp(logical.x - static_cast<double>(x0), 0.0, 1.0);
  const auto fy = std::clamp(logical.y - static_cast<double>(y0), 0.0, 1.0);
  const auto fz = std::clamp(logical.z - static_cast<double>(z0), 0.0, 1.0);
  const auto value = [&](std::size_t x, std::size_t y, std::size_t z) {
    return static_cast<double>(volume.normalized_scalars[
        (z * volume.shape.y + y) * volume.shape.x + x]);
  };
  const auto mix = [](double a, double b, double fraction) {
    return a + (b - a) * fraction;
  };
  const auto c00 = mix(value(x0, y0, z0), value(x1, y0, z0), fx);
  const auto c10 = mix(value(x0, y1, z0), value(x1, y1, z0), fx);
  const auto c01 = mix(value(x0, y0, z1), value(x1, y0, z1), fx);
  const auto c11 = mix(value(x0, y1, z1), value(x1, y1, z1), fx);
  return static_cast<float>(mix(mix(c00, c10, fy), mix(c01, c11, fy), fz));
}

ColorRgba lookup(const DirectVolumeData &volume, float normalized) {
  const auto coordinate = std::clamp(normalized, 0.0F, 1.0F) *
                          static_cast<float>(volume.transfer_lookup.size() - 1U);
  const auto lower = static_cast<std::size_t>(std::floor(coordinate));
  const auto upper = std::min(lower + 1U, volume.transfer_lookup.size() - 1U);
  const auto fraction = coordinate - static_cast<float>(lower);
  const auto &a = volume.transfer_lookup[lower];
  const auto &b = volume.transfer_lookup[upper];
  const auto mix = [fraction](float left, float right) {
    return left + (right - left) * fraction;
  };
  return {mix(a.red, b.red), mix(a.green, b.green),
          mix(a.blue, b.blue), mix(a.alpha, b.alpha)};
}

} // namespace

operation::Result<std::size_t>
direct_volume_texture_bytes(const model::VolumeGrid &grid,
                            const DirectVolumeStyle &style) {
  if (!std::isfinite(style.sampling_step) || style.sampling_step <= 0.0 ||
      style.maximum_steps == 0U || style.maximum_steps > 4096U ||
      style.lookup_table_samples < 2U || style.texture_budget_bytes == 0U) {
    return operation::Result<std::size_t>::failure(invalid(
        "direct volume style contains an invalid step, sample count or budget"));
  }
  if (style.mode == VolumeClassificationMode::pre_integrated) {
    return operation::Result<std::size_t>::failure(
        invalid("pre-integrated volume classification is not implemented"));
  }
  const auto shape = grid.shape();
  if (shape.x < 2U || shape.y < 2U || shape.z < 2U) {
    return operation::Result<std::size_t>::failure(
        invalid("direct volume requires at least 2 samples on every axis"));
  }
  const auto maximum_texture_extent =
      static_cast<std::size_t>(std::numeric_limits<int>::max());
  if (shape.x > maximum_texture_extent || shape.y > maximum_texture_extent ||
      shape.z > maximum_texture_extent ||
      style.lookup_table_samples > maximum_texture_extent) {
    return operation::Result<std::size_t>::failure(exhausted(
        "direct volume texture dimensions exceed the backend index range"));
  }
  const auto voxel_bytes =
      checked_texture_bytes(grid.value_count(), sizeof(float), "volume texture");
  if (!voxel_bytes.has_value()) return voxel_bytes;
  const auto lut_bytes = checked_texture_bytes(style.lookup_table_samples,
                                               sizeof(ColorRgba),
                                               "transfer-function texture");
  if (!lut_bytes.has_value()) return lut_bytes;
  if (voxel_bytes.value() > style.texture_budget_bytes ||
      lut_bytes.value() > style.texture_budget_bytes - voxel_bytes.value()) {
    return operation::Result<std::size_t>::failure(
        exhausted("direct volume textures exceed the texture budget"));
  }
  return operation::Result<std::size_t>::success(voxel_bytes.value() +
                                                 lut_bytes.value());
}

operation::Result<DirectVolumeData>
build_direct_volume(const DirectVolumeRequest &request) {
  if (request.grid == nullptr || request.transfer_function == nullptr)
    return operation::Result<DirectVolumeData>::failure(
        invalid("direct volume requires a scalar grid and transfer function"));
  const auto required =
      direct_volume_texture_bytes(*request.grid, request.style);
  if (!required.has_value()) {
    return operation::Result<DirectVolumeData>::failure(required.error());
  }
  const auto shape = request.grid->shape();
  const auto voxel_bytes = request.grid->value_count() * sizeof(float);
  const auto lut_bytes = required.value() - voxel_bytes;
  if (cancelled(request.context))
    return operation::Result<DirectVolumeData>::failure(
        {operation::ErrorCode::cancelled,
         "direct volume preparation was cancelled", {}});

  const auto [minimum, maximum] = request.grid->scalars().range();
  const auto span = maximum - minimum;
  std::vector<float> normalized(request.grid->value_count());
  if (request.context != nullptr && request.context->report_progress) {
    request.context->report_progress({0.05, "direct-volume-normalize"});
  }
  const auto progress_stride =
      std::max<std::size_t>(request.grid->value_count() / 128U, 1U);
  std::size_t completed{};
  for (std::size_t x = 0; x < shape.x; ++x) {
    for (std::size_t y = 0; y < shape.y; ++y) {
      for (std::size_t z = 0; z < shape.z; ++z) {
        if (cancelled(request.context)) {
          return operation::Result<DirectVolumeData>::failure(
              {operation::ErrorCode::cancelled,
               "direct volume preparation was cancelled", {}});
        }
        const auto source_index = (x * shape.y + y) * shape.z + z;
        const auto texture_index = (z * shape.y + y) * shape.x + x;
        normalized[texture_index] =
            span == 0.0
                ? 0.5F
                : static_cast<float>(
                      (request.grid->scalars().value(source_index) - minimum) /
                      span);
        ++completed;
        if (request.context != nullptr && request.context->report_progress &&
            (completed % progress_stride == 0U ||
             completed == request.grid->value_count())) {
          request.context->report_progress(
              {0.05 + 0.75 * static_cast<double>(completed) /
                          static_cast<double>(request.grid->value_count()),
               "direct-volume-normalize"});
        }
      }
    }
  }
  operation::TaskContext lookup_context;
  operation::TaskContext *lookup_context_pointer = nullptr;
  if (request.context != nullptr) {
    lookup_context.cancellation = request.context->cancellation;
    if (request.context->report_progress) {
      const auto report = request.context->report_progress;
      lookup_context.report_progress = [report](const auto &update) {
        report({0.8 + 0.2 * update.fraction, "direct-volume-transfer"});
      };
    }
    lookup_context_pointer = &lookup_context;
  }
  auto lookup = request.transfer_function->lookup_table(
      request.style.lookup_table_samples,
      request.style.texture_budget_bytes - voxel_bytes,
      lookup_context_pointer);
  if (!lookup.has_value())
    return operation::Result<DirectVolumeData>::failure(lookup.error());
  return operation::Result<DirectVolumeData>::success(
      {shape, request.grid->origin(), request.grid->deltas(),
       std::move(normalized), std::move(lookup.value()), minimum, maximum,
       request.style, request.scene_node_id, voxel_bytes + lut_bytes});
}

operation::Result<ImageRgba8> render_direct_volume_reference(
    const DirectVolumeData &volume, const scene::Camera &camera,
    ReferenceRenderSettings settings, std::uint64_t pick_id) {
  if (settings.width == 0U || settings.height == 0U ||
      !is_valid(settings.background) || volume.transfer_lookup.size() < 2U)
    return operation::Result<ImageRgba8>::failure(
        invalid("direct-volume reference render settings are invalid"));
  ImageRgba8 image;
  image.width = settings.width;
  image.height = settings.height;
  image.pixels.resize(settings.width * settings.height * 4U);
  image.depth.assign(settings.width * settings.height,
                     std::numeric_limits<double>::infinity());
  image.pick_ids.assign(settings.width * settings.height, 0U);
  using scene::operator-;
  using scene::operator+;
  using scene::operator*;
  const model::Vec3d logical_maximum{
      static_cast<double>(volume.shape.x - 1U),
      static_cast<double>(volume.shape.y - 1U),
      static_cast<double>(volume.shape.z - 1U)};
  for (std::size_t y = 0; y < settings.height; ++y) {
    for (std::size_t x = 0; x < settings.width; ++x) {
      const auto ray = pixel_ray(camera, x, y, settings.width, settings.height);
      const auto logical_origin =
          world_to_logical_vector(ray.origin - volume.origin, volume.deltas);
      const auto logical_direction =
          world_to_logical_vector(ray.direction, volume.deltas);
      double near_t{};
      double far_t{};
      float red{};
      float green{};
      float blue{};
      float alpha{};
      double first_hit = std::numeric_limits<double>::infinity();
      if (intersect_box(logical_origin, logical_direction, logical_maximum,
                        near_t, far_t)) {
        const auto logical_speed = scene::length(logical_direction);
        const auto world_step = volume.style.sampling_step / logical_speed;
        auto distance = std::max(0.0, near_t);
        for (std::size_t step = 0;
             step < volume.style.maximum_steps && distance <= far_t &&
             alpha < 0.995F;
             ++step, distance += world_step) {
          const auto logical = logical_origin + logical_direction * distance;
          const auto color = lookup(volume, trilinear(volume, logical));
          const auto corrected_alpha = static_cast<float>(
              1.0 - std::pow(1.0 - static_cast<double>(color.alpha),
                             volume.style.sampling_step));
          const auto weight = (1.0F - alpha) * corrected_alpha;
          red += weight * color.red;
          green += weight * color.green;
          blue += weight * color.blue;
          if (weight > 1.0e-4F && !std::isfinite(first_hit))
            first_hit = distance;
          alpha += weight;
        }
      }
      red += (1.0F - alpha) * settings.background.red;
      green += (1.0F - alpha) * settings.background.green;
      blue += (1.0F - alpha) * settings.background.blue;
      const auto pixel = y * settings.width + x;
      const auto offset = pixel * 4U;
      const auto byte = [](float value) {
        return static_cast<std::uint8_t>(
            std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
      };
      image.pixels[offset] = byte(red);
      image.pixels[offset + 1U] = byte(green);
      image.pixels[offset + 2U] = byte(blue);
      image.pixels[offset + 3U] = 255U;
      if (std::isfinite(first_hit)) {
        image.depth[pixel] = first_hit;
        image.pick_ids[pixel] = pick_id;
      }
    }
  }
  return operation::Result<ImageRgba8>::success(std::move(image));
}

} // namespace molshredder::render
