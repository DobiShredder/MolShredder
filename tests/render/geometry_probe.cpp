// Runs canonical invocations against a fresh Workspace and prints the
// sphere, cylinder, line and mesh-vertex primitives of every object's
// representation packets as JSON, for primitive-level comparison with
// reference geometry exports (docs/ARCHITECTURE.md, Q-019 a).
//
// Usage: molshredder_geometry_probe SCRIPT
// SCRIPT has one invocation per line: NAME<TAB>key=value<TAB>key=value...
// The values {topology_version} and {coordinate_source_revision} are replaced
// with the active object's current revisions.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/operation/task_context.hpp"

namespace {

using namespace molshredder;

std::string number(double value) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.6f", value);
  return text;
}

std::string point(const model::Vec3d &value) {
  return number(value.x) + "," + number(value.y) + "," + number(value.z);
}

std::string color(const render::ColorRgba &value) {
  return number(static_cast<double>(value.red)) + "," +
         number(static_cast<double>(value.green)) + "," +
         number(static_cast<double>(value.blue)) + "," +
         number(static_cast<double>(value.alpha));
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: molshredder_geometry_probe SCRIPT\n";
    return 2;
  }
  auto workspace = std::make_shared<application::Workspace>();
  auto registry = application::make_default_registry(workspace);
  const application::Dispatcher dispatcher{registry};
  operation::TaskContext context;
  std::ifstream script{argv[1]};
  for (std::string line; std::getline(script, line);) {
    if (line.empty())
      continue;
    std::istringstream fields{line};
    std::string name;
    std::getline(fields, name, '\t');
    command::Arguments arguments;
    for (std::string field; std::getline(fields, field, '\t');) {
      const auto separator = field.find('=');
      if (separator == std::string::npos) {
        std::cerr << "malformed argument: " << field << '\n';
        return 2;
      }
      auto value = field.substr(separator + 1U);
      // Edit invocations need the active object's current revisions.
      if (const auto *object = workspace->active_object(); object != nullptr) {
        if (value == "{topology_version}")
          value = std::to_string(object->system->topology()->version());
        else if (value == "{coordinate_source_revision}")
          value = std::to_string(object->coordinate_source_revision);
      }
      arguments.emplace(field.substr(0, separator), std::move(value));
    }
    const auto outcome = dispatcher.dispatch({name, std::move(arguments)}, context);
    if (!outcome.succeeded()) {
      std::cerr << "invocation failed: " << line << '\n';
      return 1;
    }
  }
  // Every representation packet of molecular objects, then volume contours.
  std::vector<const render::RenderPacket *> packets;
  for (const auto &object : workspace->objects())
    for (const auto &record : object.representations)
      packets.push_back(&record.packet);
  for (const auto &volume : workspace->volumes())
    for (const auto &contour : volume.contours)
      packets.push_back(&contour.packet);
  bool first = true;
  const auto separator = [&] {
    std::cout << (first ? "" : ",");
    first = false;
  };
  std::cout << "{\"spheres\":[";
  for (const auto *packet : packets)
    for (const auto &sphere : packet->spheres) {
      separator();
      std::cout << '[' << point(sphere.center) << ',' << number(sphere.radius) << ','
                << color(sphere.color) << ']';
    }
  std::cout << "],\"cylinders\":[";
  first = true;
  for (const auto *packet : packets)
    for (const auto &cylinder : packet->cylinders) {
      separator();
      std::cout << '[' << point(cylinder.start) << ',' << point(cylinder.end) << ','
                << number(cylinder.radius) << ',' << color(cylinder.color) << ']';
    }
  std::cout << "],\"lines\":[";
  first = true;
  for (const auto *packet : packets)
    for (const auto &segment : packet->lines) {
      separator();
      std::cout << '[' << point(segment.start) << ',' << point(segment.end) << ','
                << number(static_cast<double>(segment.width_pixels)) << ','
                << color(segment.start_color) << ']';
    }
  std::cout << "],\"mesh_vertices\":[";
  first = true;
  for (const auto *packet : packets)
    for (const auto &vertex : packet->mesh_vertices) {
      separator();
      std::cout << '[' << point(vertex.position) << ',' << color(vertex.color) << ']';
    }
  // Measurement overlay (distance/angle/dihedral dashes).
  std::cout << "],\"overlay_lines\":[";
  first = true;
  for (const auto &segment : workspace->analysis_overlay_packet().lines) {
    separator();
    std::cout << '[' << point(segment.start) << ',' << point(segment.end) << ','
              << number(static_cast<double>(segment.width_pixels)) << ','
              << color(segment.start_color) << ']';
  }
  // Triangles index the concatenated vertex list above.
  std::cout << "],\"mesh_triangles\":[";
  first = true;
  std::size_t base{};
  for (const auto *packet : packets) {
    for (const auto &triangle : packet->mesh_triangles) {
      separator();
      std::cout << '[' << base + triangle.first << ',' << base + triangle.second << ','
                << base + triangle.third << ']';
    }
    base += packet->mesh_vertices.size();
  }
  std::cout << "]}\n";
  return 0;
}
