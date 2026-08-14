#include "molshredder/render/reference_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

#include "molshredder/scene/math.hpp"

namespace molshredder::render {
namespace {

constexpr double kEpsilon = 1.0e-12;

struct Ray {
  model::Vec3d origin;
  model::Vec3d direction;
};

struct Hit {
  double distance{std::numeric_limits<double>::infinity()};
  model::Vec3d point;
  model::Vec3d normal;
  ColorRgba color;
  std::uint64_t pick_id{};
};

struct ProjectedPoint {
  double x{};
  double y{};
  double depth{};
};

operation::Error invalid(std::string message) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), {}};
}

bool finite(model::Vec3d value) noexcept {
  return scene::is_finite(value);
}

bool valid_instance(const SphereInstance& sphere) noexcept {
  return finite(sphere.center) && std::isfinite(sphere.radius) &&
         sphere.radius > 0.0 && is_valid(sphere.color);
}

bool valid_instance(const CylinderInstance& cylinder) noexcept {
  using scene::operator-;
  return finite(cylinder.start) && finite(cylinder.end) &&
         scene::length(cylinder.end - cylinder.start) > kEpsilon &&
         std::isfinite(cylinder.radius) && cylinder.radius > 0.0 &&
         is_valid(cylinder.color);
}

bool valid_instance(const LineInstance& line) noexcept {
  return finite(line.start) && finite(line.end) &&
         std::isfinite(line.width_pixels) && line.width_pixels > 0.0F &&
         is_valid(line.start_color) && is_valid(line.end_color);
}

bool valid_vertex(const MeshVertex& vertex) noexcept {
  return finite(vertex.position) && finite(vertex.normal) &&
         scene::length(vertex.normal) > kEpsilon && is_valid(vertex.color);
}

Ray pixel_ray(const scene::Camera& camera, std::size_t x, std::size_t y,
              std::size_t width, std::size_t height) {
  using scene::operator+;
  using scene::operator*;

  const auto& parameters = camera.parameters();
  const auto normalized_x =
      (2.0 * (static_cast<double>(x) + 0.5) / static_cast<double>(width)) -
      1.0;
  const auto normalized_y =
      1.0 - (2.0 * (static_cast<double>(y) + 0.5) /
             static_cast<double>(height));
  if (parameters.projection == scene::ProjectionMode::orthographic) {
    const auto half_height = parameters.orthographic_height * 0.5;
    const auto origin =
        camera.position() +
        camera.right() * (normalized_x * half_height * parameters.aspect_ratio) +
        camera.up() * (normalized_y * half_height);
    return Ray{origin, camera.forward()};
  }

  const auto tangent =
      std::tan(parameters.vertical_field_of_view_radians * 0.5);
  const auto direction =
      camera.forward() +
      camera.right() * (normalized_x * tangent * parameters.aspect_ratio) +
      camera.up() * (normalized_y * tangent);
  return Ray{camera.position(), scene::normalized(direction)};
}

std::optional<Hit> intersect_sphere(const Ray& ray,
                                    const SphereInstance& sphere) {
  using scene::operator+;
  using scene::operator-;
  using scene::operator*;

  const auto relative = ray.origin - sphere.center;
  const auto half_b = scene::dot(relative, ray.direction);
  const auto c = scene::dot(relative, relative) - sphere.radius * sphere.radius;
  const auto discriminant = half_b * half_b - c;
  if (discriminant < 0.0) {
    return std::nullopt;
  }
  const auto root = std::sqrt(discriminant);
  auto distance = -half_b - root;
  if (distance <= kEpsilon) {
    distance = -half_b + root;
  }
  if (distance <= kEpsilon) {
    return std::nullopt;
  }
  const auto point = ray.origin + ray.direction * distance;
  return Hit{distance, point,
             scene::normalized(point - sphere.center), sphere.color,
             sphere.pick_id};
}

std::optional<Hit> intersect_cylinder(const Ray& ray,
                                      const CylinderInstance& cylinder) {
  using scene::operator+;
  using scene::operator-;
  using scene::operator*;

  const auto axis_vector = cylinder.end - cylinder.start;
  const auto axis_length = scene::length(axis_vector);
  const auto axis = axis_vector * (1.0 / axis_length);
  const auto relative = ray.origin - cylinder.start;
  const auto ray_axis = scene::dot(ray.direction, axis);
  const auto relative_axis = scene::dot(relative, axis);
  const auto ray_perpendicular = ray.direction - axis * ray_axis;
  const auto relative_perpendicular = relative - axis * relative_axis;
  const auto a = scene::dot(ray_perpendicular, ray_perpendicular);
  const auto b = 2.0 * scene::dot(ray_perpendicular, relative_perpendicular);
  const auto c = scene::dot(relative_perpendicular, relative_perpendicular) -
                 cylinder.radius * cylinder.radius;

  std::optional<Hit> nearest;
  if (a > kEpsilon) {
    const auto discriminant = b * b - 4.0 * a * c;
    if (discriminant >= 0.0) {
      const auto root = std::sqrt(discriminant);
      const std::array<double, 2> distances{
          (-b - root) / (2.0 * a), (-b + root) / (2.0 * a)};
      for (const auto distance : distances) {
        const auto along = relative_axis + distance * ray_axis;
        if (distance > kEpsilon && along >= 0.0 && along <= axis_length &&
            (!nearest.has_value() || distance < nearest->distance)) {
          const auto point = ray.origin + ray.direction * distance;
          const auto centerline = cylinder.start + axis * along;
          nearest = Hit{distance, point,
                        scene::normalized(point - centerline), cylinder.color,
                        cylinder.pick_id};
        }
      }
    }
  }

  if (std::abs(ray_axis) > kEpsilon) {
    const std::array<std::pair<model::Vec3d, model::Vec3d>, 2> caps{{
        {cylinder.start, axis * -1.0}, {cylinder.end, axis}}};
    for (const auto& [center, normal] : caps) {
      const auto distance = scene::dot(center - ray.origin, axis) / ray_axis;
      const auto point = ray.origin + ray.direction * distance;
      if (distance > kEpsilon &&
          scene::length(point - center) <= cylinder.radius &&
          (!nearest.has_value() || distance < nearest->distance)) {
        nearest = Hit{distance, point, normal, cylinder.color,
                      cylinder.pick_id};
      }
    }
  }
  return nearest;
}

std::optional<Hit> intersect_triangle(const Ray& ray,
                                      const MeshVertex& first,
                                      const MeshVertex& second,
                                      const MeshVertex& third,
                                      std::uint64_t pick_id) {
  using scene::operator+;
  using scene::operator-;
  using scene::operator*;
  const auto edge1 = second.position - first.position;
  const auto edge2 = third.position - first.position;
  const auto p = scene::cross(ray.direction, edge2);
  const auto determinant = scene::dot(edge1, p);
  if (std::abs(determinant) <= kEpsilon) return std::nullopt;
  const auto inverse = 1.0 / determinant;
  const auto relative = ray.origin - first.position;
  const auto u = scene::dot(relative, p) * inverse;
  if (u < 0.0 || u > 1.0) return std::nullopt;
  const auto q = scene::cross(relative, edge1);
  const auto v = scene::dot(ray.direction, q) * inverse;
  if (v < 0.0 || u + v > 1.0) return std::nullopt;
  const auto distance = scene::dot(edge2, q) * inverse;
  if (distance <= kEpsilon) return std::nullopt;
  const auto w = 1.0 - u - v;
  const auto mix = [w, u, v](float a, float b, float c) {
    return static_cast<float>(w * static_cast<double>(a) +
                              u * static_cast<double>(b) +
                              v * static_cast<double>(c));
  };
  auto normal = scene::normalized(first.normal * w + second.normal * u +
                                  third.normal * v);
  if (scene::dot(normal, ray.direction) > 0.0) normal = normal * -1.0;
  return Hit{distance, ray.origin + ray.direction * distance, normal,
             ColorRgba{mix(first.color.red, second.color.red, third.color.red),
                       mix(first.color.green, second.color.green,
                           third.color.green),
                       mix(first.color.blue, second.color.blue,
                           third.color.blue),
                       mix(first.color.alpha, second.color.alpha,
                           third.color.alpha)},
             pick_id};
}

std::optional<ProjectedPoint> project(const scene::Camera& camera,
                                      model::Vec3d point,
                                      std::size_t width,
                                      std::size_t height) {
  const auto camera_point =
      scene::transform_point(camera.view_matrix(), point);
  const auto depth = -camera_point.z;
  const auto& parameters = camera.parameters();
  if (depth < parameters.near_clip || depth > parameters.far_clip) {
    return std::nullopt;
  }

  double normalized_x{};
  double normalized_y{};
  if (parameters.projection == scene::ProjectionMode::perspective) {
    const auto tangent =
        std::tan(parameters.vertical_field_of_view_radians * 0.5);
    normalized_x = camera_point.x /
                   (depth * tangent * parameters.aspect_ratio);
    normalized_y = camera_point.y / (depth * tangent);
  } else {
    const auto half_height = parameters.orthographic_height * 0.5;
    normalized_x = camera_point.x /
                   (half_height * parameters.aspect_ratio);
    normalized_y = camera_point.y / half_height;
  }
  return ProjectedPoint{
      (normalized_x * 0.5 + 0.5) * static_cast<double>(width),
      (0.5 - normalized_y * 0.5) * static_cast<double>(height), depth};
}

std::uint8_t channel(float value) noexcept {
  const auto scaled = std::clamp(value, 0.0F, 1.0F) * 255.0F;
  return static_cast<std::uint8_t>(std::lround(scaled));
}

void set_pixel(ImageRgba8& image, std::size_t index, ColorRgba color,
               double depth, std::uint64_t pick_id) {
  const auto alpha = std::clamp(color.alpha, 0.0F, 1.0F);
  const auto offset = index * 4U;
  const auto blend = [&](float foreground, std::uint8_t background) {
    const auto result = foreground * alpha +
                        (static_cast<float>(background) / 255.0F) *
                            (1.0F - alpha);
    return channel(result);
  };
  image.pixels[offset] = blend(color.red, image.pixels[offset]);
  image.pixels[offset + 1U] = blend(color.green, image.pixels[offset + 1U]);
  image.pixels[offset + 2U] = blend(color.blue, image.pixels[offset + 2U]);
  image.pixels[offset + 3U] = 255U;
  image.depth[index] = depth;
  image.pick_ids[index] = pick_id;
}

ColorRgba shaded(ColorRgba color, model::Vec3d normal,
                 model::Vec3d light_direction, double ambient) {
  const auto diffuse =
      std::max(0.0, scene::dot(normal, light_direction));
  const auto intensity = std::clamp(ambient + (1.0 - ambient) * diffuse,
                                    0.0, 1.0);
  color.red = static_cast<float>(static_cast<double>(color.red) * intensity);
  color.green =
      static_cast<float>(static_cast<double>(color.green) * intensity);
  color.blue = static_cast<float>(static_cast<double>(color.blue) * intensity);
  return color;
}

void draw_lines(const RenderPacket& packet, const scene::Camera& camera,
                ImageRgba8& image) {
  for (const auto& line : packet.lines) {
    const auto first = project(camera, line.start, image.width, image.height);
    const auto second = project(camera, line.end, image.width, image.height);
    if (!first.has_value() || !second.has_value()) {
      continue;
    }
    const auto dx = second->x - first->x;
    const auto dy = second->y - first->y;
    const auto squared_length = dx * dx + dy * dy;
    const auto radius = static_cast<double>(line.width_pixels) * 0.5;
    const auto minimum_x = static_cast<std::int64_t>(
        std::floor(std::min(first->x, second->x) - radius));
    const auto maximum_x = static_cast<std::int64_t>(
        std::ceil(std::max(first->x, second->x) + radius));
    const auto minimum_y = static_cast<std::int64_t>(
        std::floor(std::min(first->y, second->y) - radius));
    const auto maximum_y = static_cast<std::int64_t>(
        std::ceil(std::max(first->y, second->y) + radius));
    for (auto y = minimum_y; y <= maximum_y; ++y) {
      if (y < 0 || static_cast<std::size_t>(y) >= image.height) {
        continue;
      }
      for (auto x = minimum_x; x <= maximum_x; ++x) {
        if (x < 0 || static_cast<std::size_t>(x) >= image.width) {
          continue;
        }
        const auto pixel_x = static_cast<double>(x) + 0.5;
        const auto pixel_y = static_cast<double>(y) + 0.5;
        const auto parameter = squared_length <= kEpsilon
                                   ? 0.0
                                   : std::clamp(((pixel_x - first->x) * dx +
                                                 (pixel_y - first->y) * dy) /
                                                    squared_length,
                                                0.0, 1.0);
        const auto closest_x = first->x + parameter * dx;
        const auto closest_y = first->y + parameter * dy;
        const auto distance_x = pixel_x - closest_x;
        const auto distance_y = pixel_y - closest_y;
        if (distance_x * distance_x + distance_y * distance_y >
            radius * radius) {
          continue;
        }
        const auto depth =
            first->depth + parameter * (second->depth - first->depth);
        const auto index = static_cast<std::size_t>(y) * image.width +
                           static_cast<std::size_t>(x);
        if (depth >= image.depth[index]) {
          continue;
        }
        const auto mix = [parameter](float start, float end) {
          return static_cast<float>(static_cast<double>(start) *
                                        (1.0 - parameter) +
                                    static_cast<double>(end) * parameter);
        };
        set_pixel(image, index,
                  ColorRgba{mix(line.start_color.red, line.end_color.red),
                            mix(line.start_color.green, line.end_color.green),
                            mix(line.start_color.blue, line.end_color.blue),
                            mix(line.start_color.alpha, line.end_color.alpha)},
                  depth, line.pick_id);
      }
    }
  }
}

}  // namespace

operation::Result<ImageRgba8> render_reference(
    const RenderPacket& packet, const scene::Camera& camera,
    ReferenceRenderSettings settings) {
  if (settings.width == 0U || settings.height == 0U ||
      settings.width > std::numeric_limits<std::size_t>::max() /
                           settings.height ||
      settings.width * settings.height >
          std::numeric_limits<std::size_t>::max() / 4U ||
      !is_valid(settings.background) || !finite(settings.light_direction) ||
      scene::length(settings.light_direction) <= kEpsilon ||
      !std::isfinite(settings.ambient_light) || settings.ambient_light < 0.0 ||
      settings.ambient_light > 1.0) {
    return operation::Result<ImageRgba8>::failure(
        invalid("reference render settings are invalid"));
  }
  if (!std::all_of(packet.spheres.begin(), packet.spheres.end(),
                   [](const SphereInstance& value) {
                     return valid_instance(value);
                   }) ||
      !std::all_of(packet.cylinders.begin(), packet.cylinders.end(),
                   [](const CylinderInstance& value) {
                     return valid_instance(value);
                   }) ||
      !std::all_of(packet.lines.begin(), packet.lines.end(),
                   [](const LineInstance& value) {
                     return valid_instance(value);
                   }) ||
      !std::all_of(packet.mesh_vertices.begin(), packet.mesh_vertices.end(),
                   [](const MeshVertex& value) { return valid_vertex(value); }) ||
      !std::all_of(packet.mesh_triangles.begin(), packet.mesh_triangles.end(),
                   [&](const MeshTriangle& value) {
                     return value.first < packet.mesh_vertices.size() &&
                            value.second < packet.mesh_vertices.size() &&
                            value.third < packet.mesh_vertices.size() &&
                            value.first != value.second &&
                            value.second != value.third &&
                            value.first != value.third;
                   })) {
    return operation::Result<ImageRgba8>::failure(
        invalid("render packet contains an invalid primitive"));
  }

  const auto pixel_count = settings.width * settings.height;
  ImageRgba8 image;
  image.width = settings.width;
  image.height = settings.height;
  image.pixels.resize(pixel_count * 4U);
  image.depth.assign(pixel_count, std::numeric_limits<double>::infinity());
  image.pick_ids.assign(pixel_count, 0U);
  for (std::size_t index = 0; index < pixel_count; ++index) {
    const auto offset = index * 4U;
    image.pixels[offset] = channel(settings.background.red);
    image.pixels[offset + 1U] = channel(settings.background.green);
    image.pixels[offset + 2U] = channel(settings.background.blue);
    image.pixels[offset + 3U] = channel(settings.background.alpha);
  }

  const auto light_direction = scene::normalized(settings.light_direction);
  for (std::size_t y = 0; y < image.height; ++y) {
    for (std::size_t x = 0; x < image.width; ++x) {
      const auto ray = pixel_ray(camera, x, y, image.width, image.height);
      std::optional<Hit> nearest;
      for (const auto& sphere : packet.spheres) {
        const auto hit = intersect_sphere(ray, sphere);
        if (hit.has_value() &&
            (!nearest.has_value() || hit->distance < nearest->distance)) {
          nearest = hit;
        }
      }
      for (const auto& cylinder : packet.cylinders) {
        const auto hit = intersect_cylinder(ray, cylinder);
        if (hit.has_value() &&
            (!nearest.has_value() || hit->distance < nearest->distance)) {
          nearest = hit;
        }
      }
      for (const auto& triangle : packet.mesh_triangles) {
        const auto hit = intersect_triangle(
            ray, packet.mesh_vertices[triangle.first],
            packet.mesh_vertices[triangle.second],
            packet.mesh_vertices[triangle.third], triangle.pick_id);
        if (hit.has_value() &&
            (!nearest.has_value() || hit->distance < nearest->distance)) {
          nearest = hit;
        }
      }
      if (!nearest.has_value()) {
        continue;
      }
      using scene::operator-;
      const auto depth =
          scene::dot(nearest->point - camera.position(), camera.forward());
      const auto& parameters = camera.parameters();
      if (depth < parameters.near_clip || depth > parameters.far_clip) {
        continue;
      }
      const auto index = y * image.width + x;
      set_pixel(image, index,
                shaded(nearest->color, nearest->normal, light_direction,
                       settings.ambient_light),
                depth, nearest->pick_id);
    }
  }
  draw_lines(packet, camera, image);
  return operation::Result<ImageRgba8>::success(std::move(image));
}

std::optional<operation::Error> write_ppm(const std::filesystem::path& path,
                                          const ImageRgba8& image) {
  if (image.width == 0U || image.height == 0U ||
      image.width >
          std::numeric_limits<std::size_t>::max() / image.height) {
    return invalid("PPM image dimensions and pixel storage do not match");
  }
  const auto pixel_count = image.width * image.height;
  if (pixel_count > std::numeric_limits<std::size_t>::max() / 4U ||
      image.pixels.size() != pixel_count * 4U) {
    return invalid("PPM image dimensions and pixel storage do not match");
  }
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    return operation::Error{operation::ErrorCode::internal,
                            "could not open PPM output", {}};
  }
  output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
  for (std::size_t index = 0; index < pixel_count; ++index) {
    const auto offset = index * 4U;
    const std::array<char, 3> rgb{
        static_cast<char>(image.pixels[offset]),
        static_cast<char>(image.pixels[offset + 1U]),
        static_cast<char>(image.pixels[offset + 2U])};
    output.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
  }
  if (!output) {
    return operation::Error{operation::ErrorCode::internal,
                            "could not write PPM output", {}};
  }
  return std::nullopt;
}

std::uint64_t image_checksum(const ImageRgba8& image) noexcept {
  constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
  constexpr std::uint64_t prime = 1099511628211ULL;
  auto hash = offset_basis;
  for (const auto value : image.pixels) {
    hash ^= static_cast<std::uint64_t>(value);
    hash *= prime;
  }
  return hash;
}

}  // namespace molshredder::render
