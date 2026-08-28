#include "viewport_item.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QFile>
#include <QEventLoop>
#include <QFileInfo>
#include <QMatrix4x4>
#include <QMetaObject>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantMap>
#include <QVector4D>
#include <rhi/qrhi.h>

#include "molshredder/analysis/secondary_structure.hpp"
#include "molshredder/automation/python_script.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/render/representation.hpp"
#include "molshredder/scene/pymol_view.hpp"

namespace molshredder::desktop {
namespace {

struct MeshVertexGpu {
  float position[3];
  float normal[3];
  float color[4];
};

struct PickMeshVertexGpu {
  float position[3];
  float pick_color[4];
};

struct SolidVertexGpu {
  float position[3];
  float normal[3];
};

struct SphereInstanceGpu {
  float center[3];
  float radius;
  float color[4];
  float pick_color[4];
};

struct CylinderInstanceGpu {
  float start[3];
  float radius;
  float end[3];
  float padding{};
  float color[4];
  float pick_color[4];
};

struct LineCornerGpu {
  float along;
  float side;
};

struct LineInstanceGpu {
  float start[3];
  float width_pixels;
  float end[3];
  float padding{};
  float start_color[4];
  float end_color[4];
  float pick_color[4];
};

struct PickReadbackState {
  QRhiReadbackResult result;
  bool completed{};
};

static_assert(sizeof(MeshVertexGpu) == 40U);
static_assert(sizeof(PickMeshVertexGpu) == 28U);
static_assert(sizeof(SolidVertexGpu) == 24U);
static_assert(sizeof(SphereInstanceGpu) == 48U);
static_assert(sizeof(CylinderInstanceGpu) == 64U);
static_assert(sizeof(LineCornerGpu) == 8U);
static_assert(sizeof(LineInstanceGpu) == 80U);

std::array<float, 4> encoded_pick_color(std::uint32_t id) {
  constexpr float byte_scale = 1.0F / 255.0F;
  return {static_cast<float>(id & 0xffU) * byte_scale,
          static_cast<float>((id >> 8U) & 0xffU) * byte_scale,
          static_cast<float>((id >> 16U) & 0xffU) * byte_scale,
          static_cast<float>((id >> 24U) & 0xffU) * byte_scale};
}

std::uint32_t decoded_pick_color(const QByteArray &data) {
  if (data.size() < 4)
    return 0U;
  const auto *bytes = reinterpret_cast<const unsigned char *>(data.constData());
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

QShader shader(const QString &resource) {
  QFile file{resource};
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return QShader::fromSerialized(file.readAll());
}

QString outcome_error(const application::DispatchOutcome &outcome) {
  const auto *error = std::get_if<operation::Error>(&outcome.envelope.payload);
  return error == nullptr ? QStringLiteral("unknown operation failure")
                          : QString::fromStdString(error->message);
}

std::string number_text(double value) {
  std::array<char, 64U> buffer{};
  const auto converted = std::to_chars(
      buffer.data(), buffer.data() + buffer.size(), value,
      std::chars_format::general, std::numeric_limits<double>::max_digits10);
  return converted.ec == std::errc{}
             ? std::string{buffer.data(), converted.ptr}
             : std::string{};
}

std::optional<std::uint64_t>
response_unsigned(const application::DispatchOutcome &outcome,
                  std::string_view name) {
  const auto *response =
      std::get_if<command::Response>(&outcome.envelope.payload);
  if (response == nullptr)
    return std::nullopt;
  const auto found = response->fields.find(name);
  if (found == response->fields.end())
    return std::nullopt;
  return std::get_if<std::uint64_t>(&found->second.data) == nullptr
             ? std::nullopt
             : std::optional<std::uint64_t>{
                   std::get<std::uint64_t>(found->second.data)};
}

QString representation_name(render::RepresentationKind kind) {
  switch (kind) {
  case render::RepresentationKind::lines:
    return QStringLiteral("lines");
  case render::RepresentationKind::sticks:
    return QStringLiteral("sticks");
  case render::RepresentationKind::spheres:
    return QStringLiteral("spheres");
  case render::RepresentationKind::ribbon:
    return QStringLiteral("ribbon");
  case render::RepresentationKind::cartoon:
    return QStringLiteral("cartoon");
  }
  return QStringLiteral("lines");
}

QString playback_mode_name(trajectory::PlaybackMode mode) {
  if (mode == trajectory::PlaybackMode::loop)
    return QStringLiteral("loop");
  if (mode == trajectory::PlaybackMode::rock)
    return QStringLiteral("rock");
  return QStringLiteral("once");
}

QString playback_direction_name(trajectory::PlaybackDirection direction) {
  return direction == trajectory::PlaybackDirection::reverse
             ? QStringLiteral("reverse")
             : QStringLiteral("forward");
}

QMatrix4x4 qt_matrix(const scene::Matrix4d &matrix) {
  QMatrix4x4 result;
  for (std::size_t row = 0; row < 4U; ++row) {
    for (std::size_t column = 0; column < 4U; ++column) {
      result(static_cast<int>(row), static_cast<int>(column)) =
          static_cast<float>(matrix(row, column));
    }
  }
  return result;
}

template <typename Value>
std::optional<quint32> byte_size(const std::vector<Value> &values) {
  if (values.empty() ||
      values.size() >
          static_cast<std::size_t>(std::numeric_limits<quint32>::max()) /
              sizeof(Value)) {
    return std::nullopt;
  }
  return static_cast<quint32>(values.size() * sizeof(Value));
}

void append_packet(render::RenderPacket &destination,
                   render::RenderPacket source) {
  std::map<std::uint64_t, std::uint64_t> remapped_ids;
  for (auto &[source_id, target] : source.pick_targets) {
    const auto destination_id =
        static_cast<std::uint64_t>(destination.pick_targets.size()) + 1U;
    remapped_ids.emplace(source_id, destination_id);
    destination.pick_targets.emplace(destination_id, std::move(target));
  }
  const auto remap = [&remapped_ids](std::uint64_t source_id) {
    const auto found = remapped_ids.find(source_id);
    return found == remapped_ids.end() ? std::uint64_t{} : found->second;
  };
  for (auto &line : source.lines) {
    line.pick_id = remap(line.pick_id);
    destination.lines.push_back(std::move(line));
  }
  for (auto &cylinder : source.cylinders) {
    cylinder.pick_id = remap(cylinder.pick_id);
    destination.cylinders.push_back(std::move(cylinder));
  }
  for (auto &sphere : source.spheres) {
    sphere.pick_id = remap(sphere.pick_id);
    destination.spheres.push_back(std::move(sphere));
  }
  for (auto &label : source.labels) {
    label.pick_id = remap(label.pick_id);
    destination.labels.push_back(std::move(label));
  }
  const auto vertex_offset =
      static_cast<std::uint32_t>(destination.mesh_vertices.size());
  destination.mesh_vertices.insert(
      destination.mesh_vertices.end(),
      std::make_move_iterator(source.mesh_vertices.begin()),
      std::make_move_iterator(source.mesh_vertices.end()));
  for (auto &triangle : source.mesh_triangles) {
    triangle.first += vertex_offset;
    triangle.second += vertex_offset;
    triangle.third += vertex_offset;
    triangle.pick_id = remap(triangle.pick_id);
    destination.mesh_triangles.push_back(triangle);
  }
  if (!source.bounds.empty) {
    render::include(destination.bounds, source.bounds.minimum);
    render::include(destination.bounds, source.bounds.maximum);
  }
}

bool incremental_packet_compatible(const render::RenderPacket &previous,
                                   const render::RenderPacket &next,
                                   QString &reason) {
  if (previous.lines.size() != next.lines.size() ||
      previous.cylinders.size() != next.cylinders.size() ||
      previous.spheres.size() != next.spheres.size() ||
      previous.labels.size() != next.labels.size() ||
      previous.mesh_vertices.size() != next.mesh_vertices.size() ||
      previous.mesh_triangles.size() != next.mesh_triangles.size()) {
    reason = QStringLiteral("primitive cardinality changed");
    return false;
  }
  if (previous.pick_targets != next.pick_targets) {
    reason = QStringLiteral("picking identity changed");
    return false;
  }
  for (std::size_t index = 0; index < previous.lines.size(); ++index) {
    const auto &left = previous.lines[index];
    const auto &right = next.lines[index];
    if (left.width_pixels != right.width_pixels ||
        left.start_color != right.start_color ||
        left.end_color != right.end_color || left.pick_id != right.pick_id) {
      reason = QStringLiteral("line material or identity changed");
      return false;
    }
  }
  for (std::size_t index = 0; index < previous.cylinders.size(); ++index) {
    const auto &left = previous.cylinders[index];
    const auto &right = next.cylinders[index];
    if (left.radius != right.radius || left.color != right.color ||
        left.pick_id != right.pick_id) {
      reason = QStringLiteral("cylinder material or identity changed");
      return false;
    }
  }
  for (std::size_t index = 0; index < previous.spheres.size(); ++index) {
    const auto &left = previous.spheres[index];
    const auto &right = next.spheres[index];
    if (left.radius != right.radius || left.color != right.color ||
        left.pick_id != right.pick_id) {
      reason = QStringLiteral("sphere material or identity changed");
      return false;
    }
  }
  for (std::size_t index = 0; index < previous.labels.size(); ++index) {
    const auto &left = previous.labels[index];
    const auto &right = next.labels[index];
    if (left.text != right.text || left.color != right.color ||
        left.pick_id != right.pick_id) {
      reason = QStringLiteral("label content or identity changed");
      return false;
    }
  }
  for (std::size_t index = 0; index < previous.mesh_vertices.size(); ++index) {
    if (previous.mesh_vertices[index].color !=
        next.mesh_vertices[index].color) {
      reason = QStringLiteral("mesh material changed");
      return false;
    }
  }
  for (std::size_t index = 0; index < previous.mesh_triangles.size(); ++index) {
    const auto &left = previous.mesh_triangles[index];
    const auto &right = next.mesh_triangles[index];
    if (left.first != right.first || left.second != right.second ||
        left.third != right.third || left.pick_id != right.pick_id) {
      reason = QStringLiteral("mesh topology or identity changed");
      return false;
    }
  }
  reason = QStringLiteral("stable topology, primitive layout and material");
  return true;
}

render::RenderPacket demo_packet() {
  model::TopologyBuilder builder;
  std::vector<model::Vec3f> positions;
  std::vector<render::AtomVisual> visuals;
  std::vector<model::AtomIndex> alpha_carbons;
  constexpr std::size_t residue_count = 10U;
  positions.reserve(residue_count * 2U);
  visuals.reserve(residue_count * 2U);
  alpha_carbons.reserve(residue_count);
  for (std::size_t index = 0; index < residue_count; ++index) {
    const auto residue = builder.add_residue(model::ResidueRecord{
        "ALA", static_cast<std::int64_t>(index + 1U), "", "A", ""});
    if (!residue.has_value())
      return {};
    const auto ca = builder.add_atom(
        model::AtomRecord{"CA", 6U, residue.value(), "", 0, std::nullopt});
    const auto oxygen = builder.add_atom(
        model::AtomRecord{"O", 8U, residue.value(), "", 0, std::nullopt});
    if (!ca.has_value() || !oxygen.has_value())
      return {};
    if (builder
            .add_bond(model::Bond{ca.value(), oxygen.value(),
                                  model::BondOrder::single})
            .has_value()) {
      return {};
    }
    if (!alpha_carbons.empty() &&
        builder
            .add_bond(model::Bond{alpha_carbons.back(), ca.value(),
                                  model::BondOrder::single})
            .has_value()) {
      return {};
    }
    alpha_carbons.push_back(ca.value());
    const auto phase = static_cast<float>(index) * 0.75F;
    const model::Vec3f ca_position{static_cast<float>(index) * 1.45F,
                                   std::sin(phase) * 0.65F,
                                   std::cos(phase) * 0.65F};
    positions.push_back(ca_position);
    positions.push_back(
        {ca_position.x, ca_position.y + 0.85F, ca_position.z + 0.20F});
    visuals.push_back({{0.15F, 0.55F, 0.95F, 1.0F}, 1.7});
    visuals.push_back({{0.95F, 0.20F, 0.20F, 1.0F}, 1.52});
  }
  const auto topology = builder.build();
  const auto frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::move(positions)});
  if (!topology.has_value() || !frame.has_value())
    return {};
  const std::vector<analysis::SecondaryStructureState> states(
      residue_count, analysis::SecondaryStructureState::alpha_helix);
  render::RenderPacket combined;
  combined.topology_version = topology.value()->version();
  combined.frame_index = 0U;
  combined.scene_node_id = 1U;

  const auto add_representation = [&](render::RepresentationKind kind,
                                      auto configure) -> bool {
    render::RepresentationRequest request{topology.value().get(),
                                          frame.value().get(),
                                          0U,
                                          1U,
                                          visuals,
                                          {},
                                          {},
                                          states,
                                          nullptr,
                                          0U,
                                          {}};
    request.style.kind = kind;
    configure(request.style);
    auto packet = render::build_representation(request);
    if (!packet.has_value())
      return false;
    append_packet(combined, std::move(packet.value()));
    return true;
  };
  if (!add_representation(render::RepresentationKind::cartoon,
                          [](render::RepresentationStyle &style) {
                            style.backbone_samples_per_residue = 8U;
                            style.cartoon_thickness = 0.10;
                          }) ||
      !add_representation(render::RepresentationKind::lines,
                          [](render::RepresentationStyle &style) {
                            style.line_width_pixels = 2.5F;
                          }) ||
      !add_representation(render::RepresentationKind::sticks,
                          [](render::RepresentationStyle &style) {
                            style.stick_radius = 0.075;
                          }) ||
      !add_representation(render::RepresentationKind::spheres,
                          [](render::RepresentationStyle &style) {
                            style.sphere_scale = 0.18;
                          })) {
    return {};
  }
  combined.provenance.emplace("desktop_demo", "all-gpu-primitives-v1");
  return combined;
}

void build_sphere_geometry(std::vector<SolidVertexGpu> &vertices,
                           std::vector<std::uint32_t> &indices) {
  constexpr std::size_t latitude_count = 12U;
  constexpr std::size_t longitude_count = 24U;
  vertices.reserve((latitude_count + 1U) * (longitude_count + 1U));
  for (std::size_t latitude = 0; latitude <= latitude_count; ++latitude) {
    const auto theta = std::numbers::pi_v<float> *
                       static_cast<float>(latitude) /
                       static_cast<float>(latitude_count);
    const auto y = std::cos(theta);
    const auto ring_radius = std::sin(theta);
    for (std::size_t longitude = 0; longitude <= longitude_count; ++longitude) {
      const auto phi = 2.0F * std::numbers::pi_v<float> *
                       static_cast<float>(longitude) /
                       static_cast<float>(longitude_count);
      const std::array<float, 3> normal{ring_radius * std::cos(phi), y,
                                        ring_radius * std::sin(phi)};
      vertices.push_back(SolidVertexGpu{{normal[0], normal[1], normal[2]},
                                        {normal[0], normal[1], normal[2]}});
    }
  }
  indices.reserve(latitude_count * longitude_count * 6U);
  const auto row = longitude_count + 1U;
  for (std::size_t latitude = 0; latitude < latitude_count; ++latitude) {
    for (std::size_t longitude = 0; longitude < longitude_count; ++longitude) {
      const auto first = static_cast<std::uint32_t>(latitude * row + longitude);
      const auto second =
          static_cast<std::uint32_t>((latitude + 1U) * row + longitude);
      indices.insert(indices.end(), {first, second, first + 1U, first + 1U,
                                     second, second + 1U});
    }
  }
}

void build_cylinder_geometry(std::vector<SolidVertexGpu> &vertices,
                             std::vector<std::uint32_t> &indices) {
  constexpr std::size_t segment_count = 20U;
  vertices.reserve((segment_count + 1U) * 2U + (segment_count + 1U) * 2U);
  indices.reserve(segment_count * 12U);
  for (std::size_t segment = 0; segment <= segment_count; ++segment) {
    const auto angle = 2.0F * std::numbers::pi_v<float> *
                       static_cast<float>(segment) /
                       static_cast<float>(segment_count);
    const auto x = std::cos(angle);
    const auto y = std::sin(angle);
    vertices.push_back(SolidVertexGpu{{x, y, 0.0F}, {x, y, 0.0F}});
    vertices.push_back(SolidVertexGpu{{x, y, 1.0F}, {x, y, 0.0F}});
  }
  for (std::size_t segment = 0; segment < segment_count; ++segment) {
    const auto first = static_cast<std::uint32_t>(segment * 2U);
    indices.insert(indices.end(), {first, first + 1U, first + 2U, first + 2U,
                                   first + 1U, first + 3U});
  }
  const auto bottom_center = static_cast<std::uint32_t>(vertices.size());
  vertices.push_back(SolidVertexGpu{{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}});
  const auto bottom_ring = static_cast<std::uint32_t>(vertices.size());
  for (std::size_t segment = 0; segment <= segment_count; ++segment) {
    const auto angle = 2.0F * std::numbers::pi_v<float> *
                       static_cast<float>(segment) /
                       static_cast<float>(segment_count);
    vertices.push_back(SolidVertexGpu{{std::cos(angle), std::sin(angle), 0.0F},
                                      {0.0F, 0.0F, -1.0F}});
  }
  const auto top_center = static_cast<std::uint32_t>(vertices.size());
  vertices.push_back(SolidVertexGpu{{0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}});
  const auto top_ring = static_cast<std::uint32_t>(vertices.size());
  for (std::size_t segment = 0; segment <= segment_count; ++segment) {
    const auto angle = 2.0F * std::numbers::pi_v<float> *
                       static_cast<float>(segment) /
                       static_cast<float>(segment_count);
    vertices.push_back(SolidVertexGpu{{std::cos(angle), std::sin(angle), 1.0F},
                                      {0.0F, 0.0F, 1.0F}});
  }
  for (std::uint32_t segment = 0;
       segment < static_cast<std::uint32_t>(segment_count); ++segment) {
    indices.insert(indices.end(),
                   {bottom_center, bottom_ring + segment + 1U,
                    bottom_ring + segment, top_center, top_ring + segment,
                    top_ring + segment + 1U});
  }
}

class MolecularViewportRenderer final : public QQuickRhiItemRenderer {
public:
  MolecularViewportRenderer() {
    line_corners_ = {{0.0F, -0.5F}, {1.0F, -0.5F}, {0.0F, 0.5F}, {1.0F, 0.5F}};
    line_indices_ = {0U, 1U, 2U, 2U, 1U, 3U};
    build_sphere_geometry(sphere_vertices_, sphere_indices_);
    build_cylinder_geometry(cylinder_vertices_, cylinder_indices_);
  }

  void initialize(QRhiCommandBuffer *) override {
    if (rhi_ != rhi()) {
      rhi_ = rhi();
      reset_gpu_resources();
      geometry_dirty_ = true;
    }
    const auto sample_count = renderTarget()->sampleCount();
    auto *final_texture = sample_count > 1 ? resolveTexture() : colorTexture();
    if (sample_count_ != sample_count ||
        texture_format_ != final_texture->format()) {
      sample_count_ = sample_count;
      texture_format_ = final_texture->format();
      reset_pipelines();
    }
    ensure_pick_target(renderTarget()->pixelSize());
    ensure_stereo_composite_resources(renderTarget()->pixelSize(),
                                      final_texture->format());
    if (!uniform_buffer_)
      create_uniform_resources();
    if (!mesh_pipeline_ || !sphere_pipeline_ || !cylinder_pipeline_ ||
        !line_pipeline_ || !pick_mesh_pipeline_ || !pick_sphere_pipeline_ ||
        !pick_cylinder_pipeline_ || !pick_line_pipeline_ ||
        !stereo_composite_pipeline_) {
      create_pipelines();
    }
    // synchronize() can replace the CPU packet later in this frame. Defer the
    // upload to render() so no submitted update batch references buffers that
    // another upload deletes before the frame is submitted.
  }

  void synchronize(QQuickRhiItem *item) override {
    auto *viewport = static_cast<MolecularViewport *>(item);
    viewport_item_ = viewport;
    if (const auto *window = viewport->window(); window != nullptr) {
      const auto origin = viewport->mapToGlobal(QPointF{});
      const auto scale = window->devicePixelRatio();
      stereo_output_origin_ = {
          static_cast<std::int32_t>(std::lround(origin.x() * scale)),
          static_cast<std::int32_t>(std::lround(origin.y() * scale))};
    }
    angle_ = viewport->angle();
    if (camera_revision_ != viewport->cameraRevision()) {
      camera_revision_ = viewport->cameraRevision();
      if (const auto *camera = viewport->camera(); camera != nullptr) {
        camera_parameters_ = camera->parameters();
      }
    }
    if (stereo_revision_ != viewport->stereoRevision()) {
      stereo_revision_ = viewport->stereoRevision();
      stereo_parameters_ = viewport->stereo();
    }
    if (pick_request_revision_ != viewport->pickRequestRevision()) {
      pick_request_revision_ = viewport->pickRequestRevision();
      pick_position_ = viewport->pickPosition();
      pick_item_size_ = QSizeF{viewport->width(), viewport->height()};
      pick_packet_revision_ = viewport->packetRevision();
      pick_pending_ = true;
    }
    if (revision_ == viewport->packetRevision())
      return;
    revision_ = viewport->packetRevision();
    mesh_vertices_.clear();
    mesh_indices_.clear();
    pick_mesh_vertices_.clear();
    line_instances_.clear();
    sphere_instances_.clear();
    cylinder_instances_.clear();
    const auto &packet = viewport->renderPacket();
    auto pick_source_ids = std::make_shared<std::vector<std::uint64_t>>();
    pick_source_ids->reserve(packet.pick_targets.size() + 1U);
    pick_source_ids->push_back(0U);
    std::unordered_map<std::uint64_t, std::uint32_t> gpu_pick_ids;
    gpu_pick_ids.reserve(packet.pick_targets.size());
    for (const auto &[source_id, unused_target] : packet.pick_targets) {
      static_cast<void>(unused_target);
      if (pick_source_ids->size() >=
          static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        break;
      }
      const auto gpu_id = static_cast<std::uint32_t>(pick_source_ids->size());
      gpu_pick_ids.emplace(source_id, gpu_id);
      pick_source_ids->push_back(source_id);
    }
    pick_source_ids_ = std::move(pick_source_ids);
    const auto pick_color = [&gpu_pick_ids](std::uint64_t source_id) {
      const auto found = gpu_pick_ids.find(source_id);
      return encoded_pick_color(found == gpu_pick_ids.end() ? 0U
                                                            : found->second);
    };
    const auto next_direct_volume = viewport->directVolumeLease();
    if (direct_volume_ != next_direct_volume) {
      direct_volume_ = next_direct_volume;
      volume_dirty_ = true;
      reported_volume_lease_.reset();
      reported_volume_state_.clear();
    }
    volume_pick_color_ = pick_color(viewport->directVolumePickId());
    mesh_vertices_.reserve(packet.mesh_vertices.size());
    for (const auto &vertex : packet.mesh_vertices) {
      mesh_vertices_.push_back(
          MeshVertexGpu{{static_cast<float>(vertex.position.x),
                         static_cast<float>(vertex.position.y),
                         static_cast<float>(vertex.position.z)},
                        {static_cast<float>(vertex.normal.x),
                         static_cast<float>(vertex.normal.y),
                         static_cast<float>(vertex.normal.z)},
                        {vertex.color.red, vertex.color.green,
                         vertex.color.blue, vertex.color.alpha}});
    }
    mesh_indices_.reserve(packet.mesh_triangles.size() * 3U);
    pick_mesh_vertices_.reserve(packet.mesh_triangles.size() * 3U);
    for (const auto &triangle : packet.mesh_triangles) {
      mesh_indices_.insert(mesh_indices_.end(),
                           {triangle.first, triangle.second, triangle.third});
      const auto color = pick_color(triangle.pick_id);
      for (const auto vertex_index :
           {triangle.first, triangle.second, triangle.third}) {
        const auto &vertex = packet.mesh_vertices[vertex_index];
        pick_mesh_vertices_.push_back(
            PickMeshVertexGpu{{static_cast<float>(vertex.position.x),
                               static_cast<float>(vertex.position.y),
                               static_cast<float>(vertex.position.z)},
                              {color[0], color[1], color[2], color[3]}});
      }
    }
    line_instances_.reserve(packet.lines.size());
    for (const auto &line : packet.lines) {
      line_instances_.push_back(LineInstanceGpu{
          {static_cast<float>(line.start.x), static_cast<float>(line.start.y),
           static_cast<float>(line.start.z)},
          line.width_pixels,
          {static_cast<float>(line.end.x), static_cast<float>(line.end.y),
           static_cast<float>(line.end.z)},
          0.0F,
          {line.start_color.red, line.start_color.green, line.start_color.blue,
           line.start_color.alpha},
          {line.end_color.red, line.end_color.green, line.end_color.blue,
           line.end_color.alpha},
          {pick_color(line.pick_id)[0], pick_color(line.pick_id)[1],
           pick_color(line.pick_id)[2], pick_color(line.pick_id)[3]}});
    }
    sphere_instances_.reserve(packet.spheres.size());
    for (const auto &sphere : packet.spheres) {
      sphere_instances_.push_back(SphereInstanceGpu{
          {static_cast<float>(sphere.center.x),
           static_cast<float>(sphere.center.y),
           static_cast<float>(sphere.center.z)},
          static_cast<float>(sphere.radius),
          {sphere.color.red, sphere.color.green, sphere.color.blue,
           sphere.color.alpha},
          {pick_color(sphere.pick_id)[0], pick_color(sphere.pick_id)[1],
           pick_color(sphere.pick_id)[2], pick_color(sphere.pick_id)[3]}});
    }
    cylinder_instances_.reserve(packet.cylinders.size());
    for (const auto &cylinder : packet.cylinders) {
      cylinder_instances_.push_back(CylinderInstanceGpu{
          {static_cast<float>(cylinder.start.x),
           static_cast<float>(cylinder.start.y),
           static_cast<float>(cylinder.start.z)},
          static_cast<float>(cylinder.radius),
          {static_cast<float>(cylinder.end.x),
           static_cast<float>(cylinder.end.y),
           static_cast<float>(cylinder.end.z)},
          0.0F,
          {cylinder.color.red, cylinder.color.green, cylinder.color.blue,
           cylinder.color.alpha},
          {pick_color(cylinder.pick_id)[0], pick_color(cylinder.pick_id)[1],
           pick_color(cylinder.pick_id)[2], pick_color(cylinder.pick_id)[3]}});
    }
    if (packet.bounds.empty) {
      center_ = {};
    } else {
      center_ = {(packet.bounds.minimum.x + packet.bounds.maximum.x) * 0.5,
                 (packet.bounds.minimum.y + packet.bounds.maximum.y) * 0.5,
                 (packet.bounds.minimum.z + packet.bounds.maximum.z) * 0.5};
    }
    primitive_logged_ = false;
    if (viewport->packetIncremental()) {
      dynamic_geometry_dirty_ = true;
    } else {
      geometry_dirty_ = true;
      dynamic_geometry_dirty_ = false;
    }
  }

  void render(QRhiCommandBuffer *command_buffer) override {
    if (!uniform_buffer_ || !bindings_ || !stereo_uniform_buffer_ ||
        !stereo_bindings_)
      return;
    if (geometry_dirty_)
      upload_geometry(command_buffer);
    else if (dynamic_geometry_dirty_)
      update_dynamic_geometry(command_buffer);
    if (volume_dirty_)
      upload_volume(command_buffer);
    auto *updates = rhi_->nextResourceUpdateBatch();
    QMatrix4x4 model;
    model.translate(static_cast<float>(center_.x),
                    static_cast<float>(center_.y),
                    static_cast<float>(center_.z));
    model.rotate(angle_, 0.0F, 1.0F, 0.0F);
    model.translate(static_cast<float>(-center_.x),
                    static_cast<float>(-center_.y),
                    static_cast<float>(-center_.z));
    const auto size = renderTarget()->pixelSize();
    const auto update_uniform = [updates, &model](QRhiBuffer *buffer,
                                                  const QMatrix4x4 &vp,
                                                  const QSize &viewport_size) {
      const auto mvp = vp * model;
      const std::array<float, 4> viewport{
          static_cast<float>(viewport_size.width()),
          static_cast<float>(viewport_size.height()), 0.0F, 0.0F};
      updates->updateDynamicBuffer(buffer, 0U, 64U, mvp.constData());
      updates->updateDynamicBuffer(buffer, 64U, 64U, model.constData());
      updates->updateDynamicBuffer(buffer, 128U, 16U, viewport.data());
    };
    std::optional<scene::StereoPair> stereo_pair;
    const auto base_camera = scene::Camera::create(camera_parameters_);
    if (stereo_parameters_.enabled && base_camera.has_value()) {
      const auto generated =
          scene::make_stereo_pair(base_camera.value(), stereo_parameters_);
      if (generated.has_value()) stereo_pair = generated.value();
    }
    std::int32_t composite_mode = -1;
    switch (stereo_parameters_.mode) {
    case scene::StereoMode::anaglyph: composite_mode = 0; break;
    case scene::StereoMode::row_interleaved: composite_mode = 1; break;
    case scene::StereoMode::column_interleaved: composite_mode = 2; break;
    case scene::StereoMode::checkerboard: composite_mode = 3; break;
    default: break;
    }
    const auto composite_requested =
        stereo_pair.has_value() && composite_mode >= 0;
    const auto composite_active =
        composite_requested &&
        left_eye_target_.target && right_eye_target_.target &&
        stereo_composite_uniform_buffer_ && stereo_composite_bindings_ &&
        stereo_composite_pipeline_;
    const auto adjacent_active =
        stereo_pair.has_value() &&
        (stereo_parameters_.mode == scene::StereoMode::side_by_side ||
         stereo_parameters_.mode == scene::StereoMode::crosseye ||
         stereo_parameters_.mode == scene::StereoMode::walleye);
    const auto stereo_active = composite_active || adjacent_active;
    QSize left_size{size.width() / 2, size.height()};
    QSize right_size{size.width() - left_size.width(), size.height()};
    if (composite_active) {
      const auto &pair = stereo_pair.value();
      const auto &left_camera =
          stereo_parameters_.swap_eyes ? pair.right.camera : pair.left.camera;
      const auto &right_camera =
          stereo_parameters_.swap_eyes ? pair.left.camera : pair.right.camera;
      update_uniform(uniform_buffer_.get(),
                     view_projection(left_camera, size), size);
      update_uniform(stereo_uniform_buffer_.get(),
                     view_projection(right_camera, size), size);
      update_volume_uniform(*updates, volume_uniform_buffer_.get(),
                            left_camera, model, size);
      update_volume_uniform(*updates, stereo_volume_uniform_buffer_.get(),
                            right_camera, model, size);
      const std::array<std::int32_t, 8> composite_parameters{
          composite_mode,
          static_cast<std::int32_t>(stereo_parameters_.anaglyph_mode),
          stereo_output_origin_[0], stereo_output_origin_[1], size.height(), 0,
          0, 0};
      updates->updateDynamicBuffer(stereo_composite_uniform_buffer_.get(), 0U,
                                   32U, composite_parameters.data());
    } else if (adjacent_active) {
      const auto &pair = stereo_pair.value();
      const auto &left_camera =
          pair.presentation_order[0] == scene::StereoEye::left
              ? pair.left.camera
              : pair.right.camera;
      const auto &right_camera =
          pair.presentation_order[1] == scene::StereoEye::left
              ? pair.left.camera
              : pair.right.camera;
      update_uniform(uniform_buffer_.get(),
                     view_projection(left_camera, left_size), left_size);
      update_uniform(stereo_uniform_buffer_.get(),
                     view_projection(right_camera, right_size), right_size);
      update_volume_uniform(*updates, volume_uniform_buffer_.get(),
                            left_camera, model, left_size);
      update_volume_uniform(*updates, stereo_volume_uniform_buffer_.get(),
                            right_camera, model, right_size);
    } else {
      if (!base_camera.has_value()) return;
      update_uniform(uniform_buffer_.get(),
                     view_projection(base_camera.value(), size), size);
      update_volume_uniform(*updates, volume_uniform_buffer_.get(),
                            base_camera.value(), model, size);
    }
    const auto background = QColor::fromRgbF(0.018F, 0.025F, 0.04F);
    if (composite_requested && !composite_active) {
      command_buffer->beginPass(renderTarget(), background, {1.0F, 0U},
                                updates);
      command_buffer->endPass();
      if (!stereo_composite_failure_logged_) {
        qWarning("MolShredder stereo composite resources are unavailable; "
                 "refusing monoscopic fallback");
        stereo_composite_failure_logged_ = true;
      }
      return;
    }
    stereo_composite_failure_logged_ = false;
    if (composite_active) {
      command_buffer->beginPass(left_eye_target_.target.get(), background,
                                {1.0F, 0U}, updates);
      command_buffer->setViewport(
          QRhiViewport{0.0F, 0.0F, static_cast<float>(size.width()),
                       static_cast<float>(size.height())});
      draw_scene(command_buffer, bindings_.get());
      draw_volume(command_buffer, volume_bindings_.get());
      command_buffer->endPass();

      command_buffer->beginPass(right_eye_target_.target.get(), background,
                                {1.0F, 0U});
      command_buffer->setViewport(
          QRhiViewport{0.0F, 0.0F, static_cast<float>(size.width()),
                       static_cast<float>(size.height())});
      draw_scene(command_buffer, stereo_bindings_.get());
      draw_volume(command_buffer, stereo_volume_bindings_.get());
      command_buffer->endPass();

      command_buffer->beginPass(renderTarget(), background, {1.0F, 0U});
      command_buffer->setGraphicsPipeline(stereo_composite_pipeline_.get());
      command_buffer->setShaderResources(stereo_composite_bindings_.get());
      command_buffer->setViewport(
          QRhiViewport{0.0F, 0.0F, static_cast<float>(size.width()),
                       static_cast<float>(size.height())});
      command_buffer->draw(3U);
      command_buffer->endPass();
    } else {
      command_buffer->beginPass(renderTarget(), background, {1.0F, 0U},
                                updates);
    }
    if (adjacent_active) {
      command_buffer->setViewport(
          QRhiViewport{0.0F, 0.0F, static_cast<float>(left_size.width()),
                       static_cast<float>(left_size.height())});
      draw_scene(command_buffer, bindings_.get());
      draw_volume(command_buffer, volume_bindings_.get());
      command_buffer->setViewport(
          QRhiViewport{static_cast<float>(left_size.width()), 0.0F,
                       static_cast<float>(right_size.width()),
                       static_cast<float>(right_size.height())});
      draw_scene(command_buffer, stereo_bindings_.get());
      draw_volume(command_buffer, stereo_volume_bindings_.get());
    } else if (!composite_active) {
      command_buffer->setViewport(
          QRhiViewport{0.0F, 0.0F, static_cast<float>(size.width()),
                       static_cast<float>(size.height())});
      draw_scene(command_buffer, bindings_.get());
      draw_volume(command_buffer, volume_bindings_.get());
    }
    if (!composite_active) command_buffer->endPass();
    if (stereo_active && direct_volume_ && volume_pipeline_ &&
        !volume_stereo_logged_) {
      volume_stereo_logged_ = true;
      qInfo("MolShredder direct-volume GPU stereo ready: mode=%s eyes=2 bindings=per-eye",
            scene::to_string(stereo_parameters_.mode).data());
    }
    if (pick_pending_ && stereo_active && base_camera.has_value()) {
      auto *pick_updates = rhi_->nextResourceUpdateBatch();
      const auto pick_mvp = view_projection(base_camera.value(), size) * model;
      const std::array<float, 4> viewport{
          static_cast<float>(size.width()), static_cast<float>(size.height()),
          0.0F, 0.0F};
      pick_updates->updateDynamicBuffer(uniform_buffer_.get(), 0U, 64U,
                                        pick_mvp.constData());
      pick_updates->updateDynamicBuffer(uniform_buffer_.get(), 64U, 64U,
                                        model.constData());
      pick_updates->updateDynamicBuffer(uniform_buffer_.get(), 128U, 16U,
                                        viewport.data());
      update_volume_uniform(*pick_updates, volume_uniform_buffer_.get(),
                            base_camera.value(), model, size);
      command_buffer->resourceUpdate(pick_updates);
    }
    if (pick_pending_)
      render_pick(command_buffer);
    if (!primitive_logged_ && !mesh_indices_.empty() &&
        !cylinder_instances_.empty() && !sphere_instances_.empty() &&
        !line_instances_.empty() && mesh_pipeline_ && cylinder_pipeline_ &&
        sphere_pipeline_ && line_pipeline_ && mesh_vertex_buffer_ &&
        cylinder_instance_buffer_ && sphere_instance_buffer_ &&
        line_instance_buffer_) {
      primitive_logged_ = true;
      qInfo("MolShredder GPU primitives ready: mesh=%llu lines=%llu "
            "cylinders=%llu spheres=%llu",
            static_cast<unsigned long long>(mesh_indices_.size() / 3U),
            static_cast<unsigned long long>(line_instances_.size()),
            static_cast<unsigned long long>(cylinder_instances_.size()),
            static_cast<unsigned long long>(sphere_instances_.size()));
    }
  }

private:
  QMatrix4x4 view_projection(const scene::Camera &camera,
                             const QSize &output_size) const {
    const auto &parameters = camera.parameters();
    QMatrix4x4 projection = rhi_->clipSpaceCorrMatrix();
    const auto aspect = output_size.height() == 0
                            ? 1.0F
                            : static_cast<float>(output_size.width()) /
                                  static_cast<float>(output_size.height());
    if (parameters.projection == scene::ProjectionMode::perspective) {
      projection.perspective(
          static_cast<float>(parameters.vertical_field_of_view_radians *
                             180.0 / std::numbers::pi),
          aspect, static_cast<float>(parameters.near_clip),
          static_cast<float>(parameters.far_clip));
    } else {
      const auto half_height =
          static_cast<float>(parameters.orthographic_height * 0.5);
      const auto half_width = half_height * aspect;
      projection.ortho(-half_width, half_width, -half_height, half_height,
                       static_cast<float>(parameters.near_clip),
                       static_cast<float>(parameters.far_clip));
    }
    return projection * qt_matrix(camera.view_matrix());
  }

  void reset_pipelines() {
    mesh_pipeline_.reset();
    sphere_pipeline_.reset();
    cylinder_pipeline_.reset();
    line_pipeline_.reset();
    volume_pipeline_.reset();
    volume_dirty_ = true;
    stereo_composite_pipeline_.reset();
    reset_pick_pipelines();
  }

  void reset_pick_pipelines() {
    pick_mesh_pipeline_.reset();
    pick_sphere_pipeline_.reset();
    pick_cylinder_pipeline_.reset();
    pick_line_pipeline_.reset();
    volume_pick_pipeline_.reset();
    volume_dirty_ = true;
  }

  void reset_gpu_resources() {
    reset_pipelines();
    mesh_vertex_buffer_.reset();
    mesh_index_buffer_.reset();
    sphere_vertex_buffer_.reset();
    sphere_index_buffer_.reset();
    sphere_instance_buffer_.reset();
    cylinder_vertex_buffer_.reset();
    cylinder_index_buffer_.reset();
    cylinder_instance_buffer_.reset();
    line_vertex_buffer_.reset();
    line_index_buffer_.reset();
    line_instance_buffer_.reset();
    volume_uniform_buffer_.reset();
    volume_texture_.reset();
    volume_transfer_texture_.reset();
    volume_sampler_.reset();
    volume_bindings_.reset();
    volume_dirty_ = true;
    pick_mesh_vertex_buffer_.reset();
    uniform_buffer_.reset();
    bindings_.reset();
    stereo_uniform_buffer_.reset();
    stereo_bindings_.reset();
    reset_stereo_composite_resources();
    pick_render_target_.reset();
    pick_render_pass_.reset();
    pick_depth_buffer_.reset();
    pick_color_texture_.reset();
    pick_readback_texture_.reset();
    pick_target_size_ = {};
  }

  void ensure_pick_target(const QSize &size) {
    if (size.isEmpty() || pick_target_size_ == size)
      return;
    reset_pick_pipelines();
    pick_render_target_.reset();
    pick_render_pass_.reset();
    pick_depth_buffer_.reset();
    pick_color_texture_.reset();
    pick_readback_texture_.reset();
    pick_target_size_ = {};

    pick_color_texture_.reset(rhi_->newTexture(
        QRhiTexture::RGBA8, size, 1,
        QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    pick_depth_buffer_.reset(
        rhi_->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
    pick_readback_texture_.reset(rhi_->newTexture(
        QRhiTexture::RGBA8, QSize{1, 1}, 1, QRhiTexture::UsedAsTransferSource));
    if (!pick_color_texture_->create() || !pick_depth_buffer_->create() ||
        !pick_readback_texture_->create()) {
      pick_color_texture_.reset();
      pick_depth_buffer_.reset();
      pick_readback_texture_.reset();
      return;
    }
    pick_render_target_.reset(
        rhi_->newTextureRenderTarget(QRhiTextureRenderTargetDescription{
            QRhiColorAttachment{pick_color_texture_.get()},
            pick_depth_buffer_.get()}));
    pick_render_pass_.reset(
        pick_render_target_->newCompatibleRenderPassDescriptor());
    pick_render_target_->setRenderPassDescriptor(pick_render_pass_.get());
    if (!pick_render_target_->create()) {
      pick_render_target_.reset();
      pick_render_pass_.reset();
      pick_depth_buffer_.reset();
      pick_color_texture_.reset();
      pick_readback_texture_.reset();
      return;
    }
    pick_target_size_ = size;
  }

  struct EyeRenderTarget {
    std::unique_ptr<QRhiTexture> color;
    std::unique_ptr<QRhiTexture> resolve;
    std::unique_ptr<QRhiRenderBuffer> depth;
    std::unique_ptr<QRhiRenderPassDescriptor> render_pass;
    std::unique_ptr<QRhiTextureRenderTarget> target;

    [[nodiscard]] QRhiTexture *sampled_texture() const noexcept {
      return resolve ? resolve.get() : color.get();
    }
  };

  void reset_eye_target(EyeRenderTarget &eye) {
    eye.target.reset();
    eye.render_pass.reset();
    eye.depth.reset();
    eye.resolve.reset();
    eye.color.reset();
  }

  void reset_stereo_composite_resources() {
    stereo_composite_pipeline_.reset();
    stereo_composite_bindings_.reset();
    stereo_composite_sampler_.reset();
    stereo_composite_uniform_buffer_.reset();
    reset_eye_target(left_eye_target_);
    reset_eye_target(right_eye_target_);
    stereo_composite_target_size_ = {};
    stereo_composite_target_format_ = QRhiTexture::UnknownFormat;
    stereo_composite_sample_count_ = 0;
  }

  bool create_eye_target(EyeRenderTarget &eye, const QSize &size,
                         QRhiTexture::Format format) {
    eye.color.reset(rhi_->newTexture(format, size, sample_count_,
                                     QRhiTexture::RenderTarget));
    eye.depth.reset(rhi_->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size,
                                          sample_count_));
    if (!eye.color->create() || !eye.depth->create()) {
      reset_eye_target(eye);
      return false;
    }
    QRhiColorAttachment attachment{eye.color.get()};
    if (sample_count_ > 1) {
      eye.resolve.reset(rhi_->newTexture(format, size));
      if (!eye.resolve->create()) {
        reset_eye_target(eye);
        return false;
      }
      attachment.setResolveTexture(eye.resolve.get());
    }
    eye.target.reset(rhi_->newTextureRenderTarget(
        QRhiTextureRenderTargetDescription{attachment, eye.depth.get()}));
    eye.render_pass.reset(eye.target->newCompatibleRenderPassDescriptor());
    eye.target->setRenderPassDescriptor(eye.render_pass.get());
    if (!eye.target->create()) {
      reset_eye_target(eye);
      return false;
    }
    return true;
  }

  void ensure_stereo_composite_resources(const QSize &size,
                                         QRhiTexture::Format format) {
    if (size.isEmpty()) return;
    if (stereo_composite_target_size_ == size &&
        stereo_composite_target_format_ == format &&
        stereo_composite_sample_count_ == sample_count_ &&
        left_eye_target_.target && right_eye_target_.target &&
        stereo_composite_bindings_)
      return;
    reset_stereo_composite_resources();
    if (!create_eye_target(left_eye_target_, size, format) ||
        !create_eye_target(right_eye_target_, size, format))
      return;
    if (!left_eye_target_.render_pass->isCompatible(
            renderTarget()->renderPassDescriptor())) {
      qWarning(
          "MolShredder stereo composite target is not render-pass compatible");
      reset_stereo_composite_resources();
      return;
    }
    stereo_composite_uniform_buffer_.reset(
        rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32U));
    stereo_composite_sampler_.reset(rhi_->newSampler(
        QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    if (!stereo_composite_uniform_buffer_->create() ||
        !stereo_composite_sampler_->create()) {
      reset_stereo_composite_resources();
      return;
    }
    stereo_composite_bindings_.reset(rhi_->newShaderResourceBindings());
    stereo_composite_bindings_->setBindings(
        {QRhiShaderResourceBinding::uniformBuffer(
             0, QRhiShaderResourceBinding::FragmentStage,
             stereo_composite_uniform_buffer_.get()),
         QRhiShaderResourceBinding::sampledTexture(
             1, QRhiShaderResourceBinding::FragmentStage,
             left_eye_target_.sampled_texture(),
             stereo_composite_sampler_.get()),
         QRhiShaderResourceBinding::sampledTexture(
             2, QRhiShaderResourceBinding::FragmentStage,
             right_eye_target_.sampled_texture(),
             stereo_composite_sampler_.get())});
    if (!stereo_composite_bindings_->create()) {
      reset_stereo_composite_resources();
      return;
    }
    stereo_composite_target_size_ = size;
    stereo_composite_target_format_ = format;
    stereo_composite_sample_count_ = sample_count_;
  }

  void create_uniform_resources() {
    uniform_buffer_.reset(
        rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 144U));
    if (!uniform_buffer_->create()) {
      uniform_buffer_.reset();
      return;
    }
    bindings_.reset(rhi_->newShaderResourceBindings());
    bindings_->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage, uniform_buffer_.get())});
    if (!bindings_->create())
      bindings_.reset();
    stereo_uniform_buffer_.reset(
        rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 144U));
    if (!stereo_uniform_buffer_->create()) {
      stereo_uniform_buffer_.reset();
      return;
    }
    stereo_bindings_.reset(rhi_->newShaderResourceBindings());
    stereo_bindings_->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage,
        stereo_uniform_buffer_.get())});
    if (!stereo_bindings_->create())
      stereo_bindings_.reset();
  }

  void configure_pipeline(QRhiGraphicsPipeline &pipeline) {
    pipeline.setSampleCount(sample_count_);
    pipeline.setDepthTest(true);
    pipeline.setDepthWrite(true);
    pipeline.setCullMode(QRhiGraphicsPipeline::None);
    pipeline.setShaderResourceBindings(bindings_.get());
    pipeline.setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
  }

  void configure_pick_pipeline(QRhiGraphicsPipeline &pipeline,
                               bool depth_write = true) {
    pipeline.setSampleCount(1);
    pipeline.setDepthTest(true);
    pipeline.setDepthWrite(depth_write);
    pipeline.setDepthOp(depth_write ? QRhiGraphicsPipeline::Less
                                    : QRhiGraphicsPipeline::LessOrEqual);
    pipeline.setCullMode(QRhiGraphicsPipeline::None);
    pipeline.setShaderResourceBindings(bindings_.get());
    pipeline.setRenderPassDescriptor(pick_render_pass_.get());
  }

  void create_pipelines() {
    if (!bindings_)
      return;
    if (!mesh_pipeline_) {
      mesh_pipeline_.reset(rhi_->newGraphicsPipeline());
      mesh_pipeline_->setShaderStages(
          {{QRhiShaderStage::Vertex,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/molecule.vert.qsb"))},
           {QRhiShaderStage::Fragment,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/molecule.frag.qsb"))}});
      QRhiVertexInputLayout layout;
      layout.setBindings({{sizeof(MeshVertexGpu)}});
      layout.setAttributes(
          {{0, 0, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(MeshVertexGpu, position))},
           {0, 1, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(MeshVertexGpu, normal))},
           {0, 2, QRhiVertexInputAttribute::Float4,
            static_cast<quint32>(offsetof(MeshVertexGpu, color))}});
      mesh_pipeline_->setVertexInputLayout(layout);
      configure_pipeline(*mesh_pipeline_);
      if (!mesh_pipeline_->create())
        mesh_pipeline_.reset();
    }
    if (!sphere_pipeline_) {
      sphere_pipeline_.reset(rhi_->newGraphicsPipeline());
      sphere_pipeline_->setShaderStages(
          {{QRhiShaderStage::Vertex,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/sphere.vert.qsb"))},
           {QRhiShaderStage::Fragment,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/solid.frag.qsb"))}});
      QRhiVertexInputLayout layout;
      layout.setBindings(
          {{sizeof(SolidVertexGpu)},
           {sizeof(SphereInstanceGpu), QRhiVertexInputBinding::PerInstance}});
      layout.setAttributes(
          {{0, 0, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(SolidVertexGpu, position))},
           {0, 1, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(SolidVertexGpu, normal))},
           {1, 2, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(SphereInstanceGpu, center))},
           {1, 3, QRhiVertexInputAttribute::Float,
            static_cast<quint32>(offsetof(SphereInstanceGpu, radius))},
           {1, 4, QRhiVertexInputAttribute::Float4,
            static_cast<quint32>(offsetof(SphereInstanceGpu, color))}});
      sphere_pipeline_->setVertexInputLayout(layout);
      configure_pipeline(*sphere_pipeline_);
      if (!sphere_pipeline_->create())
        sphere_pipeline_.reset();
    }
    if (!cylinder_pipeline_) {
      cylinder_pipeline_.reset(rhi_->newGraphicsPipeline());
      cylinder_pipeline_->setShaderStages(
          {{QRhiShaderStage::Vertex,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/cylinder.vert.qsb"))},
           {QRhiShaderStage::Fragment,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/solid.frag.qsb"))}});
      QRhiVertexInputLayout layout;
      layout.setBindings(
          {{sizeof(SolidVertexGpu)},
           {sizeof(CylinderInstanceGpu), QRhiVertexInputBinding::PerInstance}});
      layout.setAttributes(
          {{0, 0, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(SolidVertexGpu, position))},
           {0, 1, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(SolidVertexGpu, normal))},
           {1, 2, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(CylinderInstanceGpu, start))},
           {1, 3, QRhiVertexInputAttribute::Float,
            static_cast<quint32>(offsetof(CylinderInstanceGpu, radius))},
           {1, 4, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(CylinderInstanceGpu, end))},
           {1, 5, QRhiVertexInputAttribute::Float4,
            static_cast<quint32>(offsetof(CylinderInstanceGpu, color))}});
      cylinder_pipeline_->setVertexInputLayout(layout);
      configure_pipeline(*cylinder_pipeline_);
      if (!cylinder_pipeline_->create())
        cylinder_pipeline_.reset();
    }
    if (!line_pipeline_) {
      line_pipeline_.reset(rhi_->newGraphicsPipeline());
      line_pipeline_->setShaderStages(
          {{QRhiShaderStage::Vertex,
            shader(
                QLatin1String(":/molshredder/desktop/shaders/line.vert.qsb"))},
           {QRhiShaderStage::Fragment,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/line.frag.qsb"))}});
      QRhiVertexInputLayout layout;
      layout.setBindings(
          {{sizeof(LineCornerGpu)},
           {sizeof(LineInstanceGpu), QRhiVertexInputBinding::PerInstance}});
      layout.setAttributes(
          {{0, 0, QRhiVertexInputAttribute::Float2,
            static_cast<quint32>(offsetof(LineCornerGpu, along))},
           {1, 1, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(LineInstanceGpu, start))},
           {1, 2, QRhiVertexInputAttribute::Float,
            static_cast<quint32>(offsetof(LineInstanceGpu, width_pixels))},
           {1, 3, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(LineInstanceGpu, end))},
           {1, 4, QRhiVertexInputAttribute::Float4,
            static_cast<quint32>(offsetof(LineInstanceGpu, start_color))},
           {1, 5, QRhiVertexInputAttribute::Float4,
            static_cast<quint32>(offsetof(LineInstanceGpu, end_color))}});
      line_pipeline_->setVertexInputLayout(layout);
      configure_pipeline(*line_pipeline_);
      line_pipeline_->setDepthWrite(false);
      line_pipeline_->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
      if (!line_pipeline_->create())
        line_pipeline_.reset();
    }
    if (!stereo_composite_pipeline_ && stereo_composite_bindings_) {
      stereo_composite_pipeline_.reset(rhi_->newGraphicsPipeline());
      stereo_composite_pipeline_->setShaderStages(
          {{QRhiShaderStage::Vertex,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/stereo_composite.vert.qsb"))},
           {QRhiShaderStage::Fragment,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/stereo_composite.frag.qsb"))}});
      stereo_composite_pipeline_->setTopology(QRhiGraphicsPipeline::Triangles);
      stereo_composite_pipeline_->setSampleCount(sample_count_);
      stereo_composite_pipeline_->setDepthTest(false);
      stereo_composite_pipeline_->setDepthWrite(false);
      stereo_composite_pipeline_->setCullMode(QRhiGraphicsPipeline::None);
      stereo_composite_pipeline_->setShaderResourceBindings(
          stereo_composite_bindings_.get());
      stereo_composite_pipeline_->setRenderPassDescriptor(
          renderTarget()->renderPassDescriptor());
      if (!stereo_composite_pipeline_->create())
        stereo_composite_pipeline_.reset();
    }
    if (!pick_render_pass_)
      return;
    if (!pick_mesh_pipeline_) {
      pick_mesh_pipeline_.reset(rhi_->newGraphicsPipeline());
      pick_mesh_pipeline_->setShaderStages(
          {{QRhiShaderStage::Vertex,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/pick_mesh.vert.qsb"))},
           {QRhiShaderStage::Fragment,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/pick.frag.qsb"))}});
      QRhiVertexInputLayout layout;
      layout.setBindings({{sizeof(PickMeshVertexGpu)}});
      layout.setAttributes(
          {{0, 0, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(PickMeshVertexGpu, position))},
           {0, 1, QRhiVertexInputAttribute::Float4,
            static_cast<quint32>(offsetof(PickMeshVertexGpu, pick_color))}});
      pick_mesh_pipeline_->setVertexInputLayout(layout);
      configure_pick_pipeline(*pick_mesh_pipeline_);
      if (!pick_mesh_pipeline_->create())
        pick_mesh_pipeline_.reset();
    }
    if (!pick_sphere_pipeline_) {
      pick_sphere_pipeline_.reset(rhi_->newGraphicsPipeline());
      pick_sphere_pipeline_->setShaderStages(
          {{QRhiShaderStage::Vertex,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/pick_sphere.vert.qsb"))},
           {QRhiShaderStage::Fragment,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/pick.frag.qsb"))}});
      QRhiVertexInputLayout layout;
      layout.setBindings(
          {{sizeof(SolidVertexGpu)},
           {sizeof(SphereInstanceGpu), QRhiVertexInputBinding::PerInstance}});
      layout.setAttributes(
          {{0, 0, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(SolidVertexGpu, position))},
           {1, 2, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(SphereInstanceGpu, center))},
           {1, 3, QRhiVertexInputAttribute::Float,
            static_cast<quint32>(offsetof(SphereInstanceGpu, radius))},
           {1, 5, QRhiVertexInputAttribute::Float4,
            static_cast<quint32>(offsetof(SphereInstanceGpu, pick_color))}});
      pick_sphere_pipeline_->setVertexInputLayout(layout);
      configure_pick_pipeline(*pick_sphere_pipeline_);
      if (!pick_sphere_pipeline_->create())
        pick_sphere_pipeline_.reset();
    }
    if (!pick_cylinder_pipeline_) {
      pick_cylinder_pipeline_.reset(rhi_->newGraphicsPipeline());
      pick_cylinder_pipeline_->setShaderStages(
          {{QRhiShaderStage::Vertex,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/pick_cylinder.vert.qsb"))},
           {QRhiShaderStage::Fragment,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/pick.frag.qsb"))}});
      QRhiVertexInputLayout layout;
      layout.setBindings(
          {{sizeof(SolidVertexGpu)},
           {sizeof(CylinderInstanceGpu), QRhiVertexInputBinding::PerInstance}});
      layout.setAttributes(
          {{0, 0, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(SolidVertexGpu, position))},
           {1, 2, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(CylinderInstanceGpu, start))},
           {1, 3, QRhiVertexInputAttribute::Float,
            static_cast<quint32>(offsetof(CylinderInstanceGpu, radius))},
           {1, 4, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(CylinderInstanceGpu, end))},
           {1, 6, QRhiVertexInputAttribute::Float4,
            static_cast<quint32>(offsetof(CylinderInstanceGpu, pick_color))}});
      pick_cylinder_pipeline_->setVertexInputLayout(layout);
      configure_pick_pipeline(*pick_cylinder_pipeline_);
      if (!pick_cylinder_pipeline_->create())
        pick_cylinder_pipeline_.reset();
    }
    if (!pick_line_pipeline_) {
      pick_line_pipeline_.reset(rhi_->newGraphicsPipeline());
      pick_line_pipeline_->setShaderStages(
          {{QRhiShaderStage::Vertex,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/pick_line.vert.qsb"))},
           {QRhiShaderStage::Fragment,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/pick.frag.qsb"))}});
      QRhiVertexInputLayout layout;
      layout.setBindings(
          {{sizeof(LineCornerGpu)},
           {sizeof(LineInstanceGpu), QRhiVertexInputBinding::PerInstance}});
      layout.setAttributes(
          {{0, 0, QRhiVertexInputAttribute::Float2,
            static_cast<quint32>(offsetof(LineCornerGpu, along))},
           {1, 1, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(LineInstanceGpu, start))},
           {1, 2, QRhiVertexInputAttribute::Float,
            static_cast<quint32>(offsetof(LineInstanceGpu, width_pixels))},
           {1, 3, QRhiVertexInputAttribute::Float3,
            static_cast<quint32>(offsetof(LineInstanceGpu, end))},
           {1, 6, QRhiVertexInputAttribute::Float4,
            static_cast<quint32>(offsetof(LineInstanceGpu, pick_color))}});
      pick_line_pipeline_->setVertexInputLayout(layout);
      configure_pick_pipeline(*pick_line_pipeline_, false);
      if (!pick_line_pipeline_->create())
        pick_line_pipeline_.reset();
    }
  }

  bool upload_buffer(std::unique_ptr<QRhiBuffer> &buffer,
                     QRhiBuffer::Type type, QRhiBuffer::UsageFlags usage,
                     quint32 bytes,
                     const void *data, QRhiResourceUpdateBatch &updates) {
    buffer.reset(rhi_->newBuffer(type, usage, bytes));
    if (!buffer->create()) {
      buffer.reset();
      return false;
    }
    if (type == QRhiBuffer::Dynamic) {
      updates.updateDynamicBuffer(buffer.get(), 0U, bytes, data);
    } else {
      updates.uploadStaticBuffer(buffer.get(), 0U, bytes, data);
    }
    return true;
  }

  template <typename Value>
  bool upload_vector(std::unique_ptr<QRhiBuffer> &buffer,
                     QRhiBuffer::Type type, QRhiBuffer::UsageFlags usage,
                     const std::vector<Value> &values,
                     QRhiResourceUpdateBatch &updates) {
    const auto bytes = byte_size(values);
    if (!bytes.has_value()) {
      buffer.reset();
      return values.empty();
    }
    return upload_buffer(buffer, type, usage, *bytes, values.data(), updates);
  }

  void upload_geometry(QRhiCommandBuffer *command_buffer) {
    geometry_dirty_ = false;
    dynamic_geometry_dirty_ = false;
    auto *updates = rhi_->nextResourceUpdateBatch();
    const auto mesh_ok =
        upload_vector(mesh_vertex_buffer_, QRhiBuffer::Dynamic,
                      QRhiBuffer::VertexBuffer,
                      mesh_vertices_, *updates) &&
        upload_vector(mesh_index_buffer_, QRhiBuffer::Immutable,
                      QRhiBuffer::IndexBuffer,
                      mesh_indices_, *updates);
    const auto pick_mesh_ok =
        upload_vector(pick_mesh_vertex_buffer_, QRhiBuffer::Dynamic,
                      QRhiBuffer::VertexBuffer,
                      pick_mesh_vertices_, *updates);
    const auto sphere_ok =
        upload_vector(sphere_vertex_buffer_, QRhiBuffer::Immutable,
                      QRhiBuffer::VertexBuffer,
                      sphere_vertices_, *updates) &&
        upload_vector(sphere_index_buffer_, QRhiBuffer::Immutable,
                      QRhiBuffer::IndexBuffer,
                      sphere_indices_, *updates) &&
        upload_vector(sphere_instance_buffer_, QRhiBuffer::Dynamic,
                      QRhiBuffer::VertexBuffer,
                      sphere_instances_, *updates);
    const auto cylinder_ok =
        upload_vector(cylinder_vertex_buffer_, QRhiBuffer::Immutable,
                      QRhiBuffer::VertexBuffer,
                      cylinder_vertices_, *updates) &&
        upload_vector(cylinder_index_buffer_, QRhiBuffer::Immutable,
                      QRhiBuffer::IndexBuffer,
                      cylinder_indices_, *updates) &&
        upload_vector(cylinder_instance_buffer_, QRhiBuffer::Dynamic,
                      QRhiBuffer::VertexBuffer,
                      cylinder_instances_, *updates);
    const auto line_ok =
        upload_vector(line_vertex_buffer_, QRhiBuffer::Immutable,
                      QRhiBuffer::VertexBuffer,
                      line_corners_, *updates) &&
        upload_vector(line_index_buffer_, QRhiBuffer::Immutable,
                      QRhiBuffer::IndexBuffer,
                      line_indices_, *updates) &&
        upload_vector(line_instance_buffer_, QRhiBuffer::Dynamic,
                      QRhiBuffer::VertexBuffer,
                      line_instances_, *updates);
    if (!mesh_ok) {
      mesh_vertex_buffer_.reset();
      mesh_index_buffer_.reset();
    }
    if (!pick_mesh_ok)
      pick_mesh_vertex_buffer_.reset();
    if (!sphere_ok)
      sphere_instance_buffer_.reset();
    if (!cylinder_ok)
      cylinder_instance_buffer_.reset();
    if (!line_ok)
      line_instance_buffer_.reset();
    command_buffer->resourceUpdate(updates);
  }

  void reset_volume_resources() {
    volume_pipeline_.reset();
    volume_pick_pipeline_.reset();
    volume_bindings_.reset();
    stereo_volume_bindings_.reset();
    volume_sampler_.reset();
    volume_transfer_texture_.reset();
    volume_texture_.reset();
    volume_uniform_buffer_.reset();
    stereo_volume_uniform_buffer_.reset();
  }

  void report_volume_status(QString state, QString message) {
    if (reported_volume_lease_ == direct_volume_ &&
        reported_volume_state_ == state) {
      return;
    }
    reported_volume_lease_ = direct_volume_;
    reported_volume_state_ = state;
    const QPointer<MolecularViewport> viewport = viewport_item_;
    const auto lease = direct_volume_;
    if (viewport == nullptr) return;
    QMetaObject::invokeMethod(
        viewport.data(),
        [viewport, lease, state = std::move(state),
         message = std::move(message)]() mutable {
          if (viewport != nullptr) {
            viewport->deliverDirectVolumeGpuStatus(
                lease, std::move(state), std::move(message));
          }
        },
        Qt::QueuedConnection);
  }

  void upload_volume(QRhiCommandBuffer *command_buffer) {
    volume_dirty_ = false;
    volume_stereo_logged_ = false;
    reset_volume_resources();
    if (!direct_volume_) {
      report_volume_status(QStringLiteral("idle"),
                           QStringLiteral("No direct volume is active"));
      return;
    }
    if (!rhi_->isFeatureSupported(QRhi::ThreeDimensionalTextures) ||
        !rhi_->isTextureFormatSupported(
            QRhiTexture::R32F, QRhiTexture::ThreeDimensional) ||
        !rhi_->isTextureFormatSupported(QRhiTexture::RGBA32F)) {
      qWarning("MolShredder direct-volume GPU unavailable: backend lacks R32F 3D or RGBA32F textures");
      report_volume_status(
          QStringLiteral("unavailable"),
          QStringLiteral("The graphics backend lacks required 3D R32F or RGBA32F texture support"));
      return;
    }
    const auto &data = *direct_volume_;
    volume_texture_.reset(rhi_->newTexture(
        QRhiTexture::R32F, static_cast<int>(data.shape.x),
        static_cast<int>(data.shape.y), static_cast<int>(data.shape.z), 1,
        QRhiTexture::ThreeDimensional));
    volume_transfer_texture_.reset(rhi_->newTexture(
        QRhiTexture::RGBA32F,
        QSize{static_cast<int>(data.transfer_lookup.size()), 1}));
    volume_sampler_.reset(rhi_->newSampler(
        QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge,
        QRhiSampler::ClampToEdge));
    volume_uniform_buffer_.reset(rhi_->newBuffer(
        QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 176U));
    stereo_volume_uniform_buffer_.reset(rhi_->newBuffer(
        QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 176U));
    if (!volume_texture_->create() || !volume_transfer_texture_->create() ||
        !volume_sampler_->create() || !volume_uniform_buffer_->create() ||
        !stereo_volume_uniform_buffer_->create()) {
      qWarning("MolShredder direct-volume GPU resource creation failed");
      report_volume_status(
          QStringLiteral("failed"),
          QStringLiteral("Direct-volume GPU resource creation failed"));
      reset_volume_resources();
      return;
    }
    volume_bindings_.reset(rhi_->newShaderResourceBindings());
    volume_bindings_->setBindings(
        {QRhiShaderResourceBinding::uniformBuffer(
             0, QRhiShaderResourceBinding::FragmentStage,
             volume_uniform_buffer_.get()),
         QRhiShaderResourceBinding::sampledTexture(
             1, QRhiShaderResourceBinding::FragmentStage,
             volume_texture_.get(), volume_sampler_.get()),
         QRhiShaderResourceBinding::sampledTexture(
             2, QRhiShaderResourceBinding::FragmentStage,
             volume_transfer_texture_.get(), volume_sampler_.get())});
    if (!volume_bindings_->create()) {
      qWarning("MolShredder direct-volume GPU bindings failed");
      report_volume_status(
          QStringLiteral("failed"),
          QStringLiteral("Direct-volume GPU resource binding failed"));
      reset_volume_resources();
      return;
    }
    stereo_volume_bindings_.reset(rhi_->newShaderResourceBindings());
    stereo_volume_bindings_->setBindings(
        {QRhiShaderResourceBinding::uniformBuffer(
             0, QRhiShaderResourceBinding::FragmentStage,
             stereo_volume_uniform_buffer_.get()),
         QRhiShaderResourceBinding::sampledTexture(
             1, QRhiShaderResourceBinding::FragmentStage,
             volume_texture_.get(), volume_sampler_.get()),
         QRhiShaderResourceBinding::sampledTexture(
             2, QRhiShaderResourceBinding::FragmentStage,
             volume_transfer_texture_.get(), volume_sampler_.get())});
    if (!stereo_volume_bindings_->create()) {
      qWarning("MolShredder direct-volume stereo bindings failed");
      report_volume_status(
          QStringLiteral("failed"),
          QStringLiteral("Direct-volume stereo resource binding failed"));
      reset_volume_resources();
      return;
    }
    volume_pipeline_.reset(rhi_->newGraphicsPipeline());
    volume_pipeline_->setShaderStages(
        {{QRhiShaderStage::Vertex,
          shader(QLatin1String(
              ":/molshredder/desktop/shaders/volume.vert.qsb"))},
         {QRhiShaderStage::Fragment,
          shader(QLatin1String(
              ":/molshredder/desktop/shaders/volume.frag.qsb"))}});
    volume_pipeline_->setTopology(QRhiGraphicsPipeline::Triangles);
    volume_pipeline_->setSampleCount(sample_count_);
    volume_pipeline_->setDepthTest(false);
    volume_pipeline_->setDepthWrite(false);
    volume_pipeline_->setCullMode(QRhiGraphicsPipeline::None);
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    volume_pipeline_->setTargetBlends({blend});
    volume_pipeline_->setShaderResourceBindings(volume_bindings_.get());
    volume_pipeline_->setRenderPassDescriptor(
        renderTarget()->renderPassDescriptor());
    if (!volume_pipeline_->create()) {
      qWarning("MolShredder direct-volume GPU pipeline creation failed");
      report_volume_status(
          QStringLiteral("failed"),
          QStringLiteral("Direct-volume GPU pipeline creation failed"));
      reset_volume_resources();
      return;
    }
    if (pick_render_pass_) {
      volume_pick_pipeline_.reset(rhi_->newGraphicsPipeline());
      volume_pick_pipeline_->setShaderStages(
          {{QRhiShaderStage::Vertex,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/volume.vert.qsb"))},
           {QRhiShaderStage::Fragment,
            shader(QLatin1String(
                ":/molshredder/desktop/shaders/volume_pick.frag.qsb"))}});
      volume_pick_pipeline_->setTopology(QRhiGraphicsPipeline::Triangles);
      volume_pick_pipeline_->setSampleCount(1);
      volume_pick_pipeline_->setDepthTest(false);
      volume_pick_pipeline_->setDepthWrite(false);
      volume_pick_pipeline_->setCullMode(QRhiGraphicsPipeline::None);
      volume_pick_pipeline_->setShaderResourceBindings(volume_bindings_.get());
      volume_pick_pipeline_->setRenderPassDescriptor(pick_render_pass_.get());
      if (!volume_pick_pipeline_->create()) {
        qWarning("MolShredder direct-volume GPU pick pipeline creation failed");
        volume_pick_pipeline_.reset();
      }
    }
    auto *updates = rhi_->nextResourceUpdateBatch();
    const auto scalar_bytes = static_cast<quint32>(
        data.normalized_scalars.size() * sizeof(float));
    const auto transfer_bytes = static_cast<quint32>(
        data.transfer_lookup.size() * sizeof(render::ColorRgba));
    updates->uploadTexture(
        volume_texture_.get(),
        QRhiTextureUploadDescription{QRhiTextureUploadEntry{
            0, 0, QRhiTextureSubresourceUploadDescription{
                      data.normalized_scalars.data(), scalar_bytes}}});
    updates->uploadTexture(
        volume_transfer_texture_.get(),
        QRhiTextureUploadDescription{QRhiTextureUploadEntry{
            0, 0, QRhiTextureSubresourceUploadDescription{
                      data.transfer_lookup.data(), transfer_bytes}}});
    command_buffer->resourceUpdate(updates);
    report_volume_status(
        volume_pick_pipeline_ ? QStringLiteral("ready")
                              : QStringLiteral("degraded"),
        volume_pick_pipeline_
            ? QStringLiteral("Direct-volume GPU rendering and picking are ready")
            : QStringLiteral("Direct-volume GPU rendering is ready but picking is unavailable"));
    qInfo("MolShredder direct-volume GPU ready: voxels=%llu lut=%llu bytes=%llu backend=qrhi-3d-texture",
          static_cast<unsigned long long>(data.normalized_scalars.size()),
          static_cast<unsigned long long>(data.transfer_lookup.size()),
          static_cast<unsigned long long>(data.required_texture_bytes));
  }

  void update_volume_uniform(QRhiResourceUpdateBatch &updates,
                             QRhiBuffer *uniform_buffer,
                             const scene::Camera &camera,
                             const QMatrix4x4 &model,
                             const QSize &viewport_size) {
    if (!direct_volume_ || uniform_buffer == nullptr || viewport_size.isEmpty())
      return;
    const auto vp = view_projection(camera, viewport_size);
    bool invertible{};
    const auto inverse_vp = vp.inverted(&invertible);
    if (!invertible)
      return;
    const auto &data = *direct_volume_;
    QMatrix4x4 texture_to_model;
    texture_to_model.setColumn(
        0, QVector4D{static_cast<float>(data.deltas[0].x *
                                       static_cast<double>(data.shape.x - 1U)),
                     static_cast<float>(data.deltas[0].y *
                                       static_cast<double>(data.shape.x - 1U)),
                     static_cast<float>(data.deltas[0].z *
                                       static_cast<double>(data.shape.x - 1U)),
                     0.0F});
    texture_to_model.setColumn(
        1, QVector4D{static_cast<float>(data.deltas[1].x *
                                       static_cast<double>(data.shape.y - 1U)),
                     static_cast<float>(data.deltas[1].y *
                                       static_cast<double>(data.shape.y - 1U)),
                     static_cast<float>(data.deltas[1].z *
                                       static_cast<double>(data.shape.y - 1U)),
                     0.0F});
    texture_to_model.setColumn(
        2, QVector4D{static_cast<float>(data.deltas[2].x *
                                       static_cast<double>(data.shape.z - 1U)),
                     static_cast<float>(data.deltas[2].y *
                                       static_cast<double>(data.shape.z - 1U)),
                     static_cast<float>(data.deltas[2].z *
                                       static_cast<double>(data.shape.z - 1U)),
                     0.0F});
    texture_to_model.setColumn(
        3, QVector4D{static_cast<float>(data.origin.x),
                     static_cast<float>(data.origin.y),
                     static_cast<float>(data.origin.z), 1.0F});
    const auto world_to_texture = (model * texture_to_model).inverted();
    const auto position = camera.position();
    const std::array<float, 4U> camera_data{
        static_cast<float>(position.x), static_cast<float>(position.y),
        static_cast<float>(position.z), 1.0F};
    const auto maximum_extent = static_cast<float>(
        std::max({data.shape.x, data.shape.y, data.shape.z}) - 1U);
    const std::array<float, 4U> sampling{
        static_cast<float>(data.style.sampling_step) / maximum_extent,
        static_cast<float>(data.style.maximum_steps),
        camera.parameters().projection == scene::ProjectionMode::perspective
            ? 1.0F
            : 0.0F,
        static_cast<float>(data.style.sampling_step)};
    updates.updateDynamicBuffer(uniform_buffer, 0U, 64U,
                                inverse_vp.constData());
    updates.updateDynamicBuffer(uniform_buffer, 64U, 64U,
                                world_to_texture.constData());
    updates.updateDynamicBuffer(uniform_buffer, 128U, 16U,
                                camera_data.data());
    updates.updateDynamicBuffer(uniform_buffer, 144U, 16U,
                                sampling.data());
    updates.updateDynamicBuffer(uniform_buffer, 160U, 16U,
                                volume_pick_color_.data());
  }

  void draw_volume(QRhiCommandBuffer *command_buffer,
                   QRhiShaderResourceBindings *bindings) {
    if (!volume_pipeline_ || bindings == nullptr)
      return;
    command_buffer->setGraphicsPipeline(volume_pipeline_.get());
    command_buffer->setShaderResources(bindings);
    command_buffer->draw(3U);
  }

  void draw_pick_volume(QRhiCommandBuffer *command_buffer) {
    if (!volume_pick_pipeline_ || !volume_bindings_)
      return;
    command_buffer->setGraphicsPipeline(volume_pick_pipeline_.get());
    command_buffer->setShaderResources(volume_bindings_.get());
    command_buffer->draw(3U);
  }

  template <typename Value>
  bool dynamic_buffer_matches(const std::unique_ptr<QRhiBuffer> &buffer,
                              const std::vector<Value> &values) const {
    const auto bytes = byte_size(values);
    return bytes.has_value() ? buffer != nullptr && buffer->size() == *bytes
                             : values.empty() && buffer == nullptr;
  }

  void update_dynamic_geometry(QRhiCommandBuffer *command_buffer) {
    dynamic_geometry_dirty_ = false;
    if (!dynamic_buffer_matches(mesh_vertex_buffer_, mesh_vertices_) ||
        !dynamic_buffer_matches(pick_mesh_vertex_buffer_,
                                pick_mesh_vertices_) ||
        !dynamic_buffer_matches(sphere_instance_buffer_, sphere_instances_) ||
        !dynamic_buffer_matches(cylinder_instance_buffer_,
                                cylinder_instances_) ||
        !dynamic_buffer_matches(line_instance_buffer_, line_instances_)) {
      geometry_dirty_ = true;
      upload_geometry(command_buffer);
      return;
    }
    auto *updates = rhi_->nextResourceUpdateBatch();
    const auto update = [updates](QRhiBuffer *buffer, const auto &values) {
      if (buffer == nullptr || values.empty()) return;
      const auto bytes = byte_size(values);
      updates->updateDynamicBuffer(buffer, 0U, *bytes, values.data());
    };
    update(mesh_vertex_buffer_.get(), mesh_vertices_);
    update(pick_mesh_vertex_buffer_.get(), pick_mesh_vertices_);
    update(sphere_instance_buffer_.get(), sphere_instances_);
    update(cylinder_instance_buffer_.get(), cylinder_instances_);
    update(line_instance_buffer_.get(), line_instances_);
    command_buffer->resourceUpdate(updates);
  }

  void draw_mesh(QRhiCommandBuffer *command_buffer,
                 QRhiShaderResourceBindings *bindings) {
    if (!mesh_pipeline_ || !mesh_vertex_buffer_ || !mesh_index_buffer_ ||
        mesh_indices_.empty()) {
      return;
    }
    command_buffer->setGraphicsPipeline(mesh_pipeline_.get());
    command_buffer->setShaderResources(bindings);
    const QRhiCommandBuffer::VertexInput input{mesh_vertex_buffer_.get(), 0U};
    command_buffer->setVertexInput(0, 1, &input, mesh_index_buffer_.get(), 0U,
                                   QRhiCommandBuffer::IndexUInt32);
    command_buffer->drawIndexed(static_cast<quint32>(mesh_indices_.size()));
  }

  void draw_spheres(QRhiCommandBuffer *command_buffer,
                    QRhiShaderResourceBindings *bindings) {
    if (!sphere_pipeline_ || !sphere_vertex_buffer_ || !sphere_index_buffer_ ||
        !sphere_instance_buffer_ || sphere_instances_.empty()) {
      return;
    }
    command_buffer->setGraphicsPipeline(sphere_pipeline_.get());
    command_buffer->setShaderResources(bindings);
    const std::array<QRhiCommandBuffer::VertexInput, 2> inputs{
        QRhiCommandBuffer::VertexInput{sphere_vertex_buffer_.get(), 0U},
        QRhiCommandBuffer::VertexInput{sphere_instance_buffer_.get(), 0U}};
    command_buffer->setVertexInput(0, static_cast<int>(inputs.size()),
                                   inputs.data(), sphere_index_buffer_.get(),
                                   0U, QRhiCommandBuffer::IndexUInt32);
    command_buffer->drawIndexed(static_cast<quint32>(sphere_indices_.size()),
                                static_cast<quint32>(sphere_instances_.size()));
  }

  void draw_cylinders(QRhiCommandBuffer *command_buffer,
                      QRhiShaderResourceBindings *bindings) {
    if (!cylinder_pipeline_ || !cylinder_vertex_buffer_ ||
        !cylinder_index_buffer_ || !cylinder_instance_buffer_ ||
        cylinder_instances_.empty()) {
      return;
    }
    command_buffer->setGraphicsPipeline(cylinder_pipeline_.get());
    command_buffer->setShaderResources(bindings);
    const std::array<QRhiCommandBuffer::VertexInput, 2> inputs{
        QRhiCommandBuffer::VertexInput{cylinder_vertex_buffer_.get(), 0U},
        QRhiCommandBuffer::VertexInput{cylinder_instance_buffer_.get(), 0U}};
    command_buffer->setVertexInput(0, static_cast<int>(inputs.size()),
                                   inputs.data(), cylinder_index_buffer_.get(),
                                   0U, QRhiCommandBuffer::IndexUInt32);
    command_buffer->drawIndexed(
        static_cast<quint32>(cylinder_indices_.size()),
        static_cast<quint32>(cylinder_instances_.size()));
  }

  void draw_lines(QRhiCommandBuffer *command_buffer,
                  QRhiShaderResourceBindings *bindings) {
    if (!line_pipeline_ || !line_vertex_buffer_ || !line_index_buffer_ ||
        !line_instance_buffer_ || line_instances_.empty()) {
      return;
    }
    command_buffer->setGraphicsPipeline(line_pipeline_.get());
    command_buffer->setShaderResources(bindings);
    const std::array<QRhiCommandBuffer::VertexInput, 2> inputs{
        QRhiCommandBuffer::VertexInput{line_vertex_buffer_.get(), 0U},
        QRhiCommandBuffer::VertexInput{line_instance_buffer_.get(), 0U}};
    command_buffer->setVertexInput(0, static_cast<int>(inputs.size()),
                                   inputs.data(), line_index_buffer_.get(), 0U,
                                   QRhiCommandBuffer::IndexUInt32);
    command_buffer->drawIndexed(static_cast<quint32>(line_indices_.size()),
                                static_cast<quint32>(line_instances_.size()));
  }

  void draw_scene(QRhiCommandBuffer *command_buffer,
                  QRhiShaderResourceBindings *bindings) {
    draw_mesh(command_buffer, bindings);
    draw_cylinders(command_buffer, bindings);
    draw_spheres(command_buffer, bindings);
    draw_lines(command_buffer, bindings);
  }

  void draw_pick_mesh(QRhiCommandBuffer *command_buffer) {
    if (!pick_mesh_pipeline_ || !pick_mesh_vertex_buffer_ ||
        pick_mesh_vertices_.empty()) {
      return;
    }
    command_buffer->setGraphicsPipeline(pick_mesh_pipeline_.get());
    command_buffer->setShaderResources();
    const QRhiCommandBuffer::VertexInput input{pick_mesh_vertex_buffer_.get(),
                                               0U};
    command_buffer->setVertexInput(0, 1, &input);
    command_buffer->draw(static_cast<quint32>(pick_mesh_vertices_.size()));
  }

  void draw_pick_spheres(QRhiCommandBuffer *command_buffer) {
    if (!pick_sphere_pipeline_ || !sphere_vertex_buffer_ ||
        !sphere_index_buffer_ || !sphere_instance_buffer_ ||
        sphere_instances_.empty()) {
      return;
    }
    command_buffer->setGraphicsPipeline(pick_sphere_pipeline_.get());
    command_buffer->setShaderResources();
    const std::array<QRhiCommandBuffer::VertexInput, 2> inputs{
        QRhiCommandBuffer::VertexInput{sphere_vertex_buffer_.get(), 0U},
        QRhiCommandBuffer::VertexInput{sphere_instance_buffer_.get(), 0U}};
    command_buffer->setVertexInput(0, static_cast<int>(inputs.size()),
                                   inputs.data(), sphere_index_buffer_.get(),
                                   0U, QRhiCommandBuffer::IndexUInt32);
    command_buffer->drawIndexed(static_cast<quint32>(sphere_indices_.size()),
                                static_cast<quint32>(sphere_instances_.size()));
  }

  void draw_pick_cylinders(QRhiCommandBuffer *command_buffer) {
    if (!pick_cylinder_pipeline_ || !cylinder_vertex_buffer_ ||
        !cylinder_index_buffer_ || !cylinder_instance_buffer_ ||
        cylinder_instances_.empty()) {
      return;
    }
    command_buffer->setGraphicsPipeline(pick_cylinder_pipeline_.get());
    command_buffer->setShaderResources();
    const std::array<QRhiCommandBuffer::VertexInput, 2> inputs{
        QRhiCommandBuffer::VertexInput{cylinder_vertex_buffer_.get(), 0U},
        QRhiCommandBuffer::VertexInput{cylinder_instance_buffer_.get(), 0U}};
    command_buffer->setVertexInput(0, static_cast<int>(inputs.size()),
                                   inputs.data(), cylinder_index_buffer_.get(),
                                   0U, QRhiCommandBuffer::IndexUInt32);
    command_buffer->drawIndexed(
        static_cast<quint32>(cylinder_indices_.size()),
        static_cast<quint32>(cylinder_instances_.size()));
  }

  void draw_pick_lines(QRhiCommandBuffer *command_buffer) {
    if (!pick_line_pipeline_ || !line_vertex_buffer_ || !line_index_buffer_ ||
        !line_instance_buffer_ || line_instances_.empty()) {
      return;
    }
    command_buffer->setGraphicsPipeline(pick_line_pipeline_.get());
    command_buffer->setShaderResources();
    const std::array<QRhiCommandBuffer::VertexInput, 2> inputs{
        QRhiCommandBuffer::VertexInput{line_vertex_buffer_.get(), 0U},
        QRhiCommandBuffer::VertexInput{line_instance_buffer_.get(), 0U}};
    command_buffer->setVertexInput(0, static_cast<int>(inputs.size()),
                                   inputs.data(), line_index_buffer_.get(), 0U,
                                   QRhiCommandBuffer::IndexUInt32);
    command_buffer->drawIndexed(static_cast<quint32>(line_indices_.size()),
                                static_cast<quint32>(line_instances_.size()));
  }

  void render_pick(QRhiCommandBuffer *command_buffer) {
    if (!pick_render_target_ || !pick_color_texture_ ||
        !pick_readback_texture_ || pick_target_size_.isEmpty() ||
        pick_item_size_.width() <= 0.0 || pick_item_size_.height() <= 0.0) {
      return;
    }
    std::erase_if(pending_pick_readbacks_, [](const auto &state) {
      return state->completed;
    });
    // Coalesce rapid cursor updates while the GPU owns the staging texture.
    // Reusing it for multiple outstanding readbacks can race teardown on slow
    // backends and guarantees that every older result is stale anyway.
    if (!pending_pick_readbacks_.empty()) return;
    pick_pending_ = false;
    const auto x =
        std::clamp(static_cast<int>(std::floor(
                       pick_position_.x() / pick_item_size_.width() *
                       static_cast<double>(pick_target_size_.width()))),
                   0, pick_target_size_.width() - 1);
    const auto y =
        std::clamp(static_cast<int>(std::floor(
                       pick_position_.y() / pick_item_size_.height() *
                       static_cast<double>(pick_target_size_.height()))),
                   0, pick_target_size_.height() - 1);
    command_buffer->beginPass(pick_render_target_.get(), Qt::transparent,
                              {1.0F, 0U});
    command_buffer->setViewport(
        QRhiViewport{0.0F, 0.0F, static_cast<float>(pick_target_size_.width()),
                     static_cast<float>(pick_target_size_.height())});
    draw_pick_mesh(command_buffer);
    draw_pick_cylinders(command_buffer);
    draw_pick_spheres(command_buffer);
    draw_pick_lines(command_buffer);
    draw_pick_volume(command_buffer);
    command_buffer->endPass();

    auto state = std::make_shared<PickReadbackState>();
    pending_pick_readbacks_.push_back(state);
    const auto viewport = viewport_item_;
    const auto source_ids = pick_source_ids_;
    const auto request_revision = pick_request_revision_;
    const auto packet_revision = pick_packet_revision_;
    const std::weak_ptr<PickReadbackState> weak_state{state};
    state->result.completed = [weak_state, viewport, source_ids,
                               request_revision, packet_revision] {
      const auto state = weak_state.lock();
      if (!state)
        return;
      const auto gpu_id = decoded_pick_color(state->result.data);
      const auto source_id =
          source_ids && gpu_id < source_ids->size()
              ? (*source_ids)[static_cast<std::size_t>(gpu_id)]
              : std::uint64_t{};
      state->completed = true;
      if (viewport != nullptr) {
        QMetaObject::invokeMethod(
            viewport.data(),
            [viewport, request_revision, packet_revision, source_id] {
              if (viewport != nullptr) {
                viewport->deliverPickResult(request_revision, packet_revision,
                                            source_id);
                viewport->update();
                if (viewport->window() != nullptr)
                  viewport->window()->update();
              }
            },
            Qt::QueuedConnection);
      }
    };
    auto *updates = rhi_->nextResourceUpdateBatch();
    QRhiTextureCopyDescription copy;
    copy.setPixelSize(QSize{1, 1});
    copy.setSourceTopLeft(QPoint{x, y});
    updates->copyTexture(pick_readback_texture_.get(),
                         pick_color_texture_.get(), copy);
    updates->readBackTexture(
        QRhiReadbackDescription{pick_readback_texture_.get()}, &state->result);
    command_buffer->resourceUpdate(updates);
  }

  QRhi *rhi_{};
  int sample_count_{1};
  QRhiTexture::Format texture_format_{QRhiTexture::RGBA8};
  std::unique_ptr<QRhiBuffer> uniform_buffer_;
  std::unique_ptr<QRhiShaderResourceBindings> bindings_;
  std::unique_ptr<QRhiBuffer> stereo_uniform_buffer_;
  std::unique_ptr<QRhiShaderResourceBindings> stereo_bindings_;
  EyeRenderTarget left_eye_target_;
  EyeRenderTarget right_eye_target_;
  std::unique_ptr<QRhiBuffer> stereo_composite_uniform_buffer_;
  std::unique_ptr<QRhiSampler> stereo_composite_sampler_;
  std::unique_ptr<QRhiShaderResourceBindings> stereo_composite_bindings_;
  std::unique_ptr<QRhiGraphicsPipeline> stereo_composite_pipeline_;
  QSize stereo_composite_target_size_;
  QRhiTexture::Format stereo_composite_target_format_{
      QRhiTexture::UnknownFormat};
  int stereo_composite_sample_count_{};
  std::unique_ptr<QRhiGraphicsPipeline> mesh_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> sphere_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> cylinder_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> line_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> pick_mesh_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> pick_sphere_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> pick_cylinder_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> pick_line_pipeline_;
  std::unique_ptr<QRhiBuffer> volume_uniform_buffer_;
  std::unique_ptr<QRhiBuffer> stereo_volume_uniform_buffer_;
  std::unique_ptr<QRhiTexture> volume_texture_;
  std::unique_ptr<QRhiTexture> volume_transfer_texture_;
  std::unique_ptr<QRhiSampler> volume_sampler_;
  std::unique_ptr<QRhiShaderResourceBindings> volume_bindings_;
  std::unique_ptr<QRhiShaderResourceBindings> stereo_volume_bindings_;
  std::unique_ptr<QRhiGraphicsPipeline> volume_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> volume_pick_pipeline_;
  std::unique_ptr<QRhiBuffer> mesh_vertex_buffer_;
  std::unique_ptr<QRhiBuffer> mesh_index_buffer_;
  std::unique_ptr<QRhiBuffer> sphere_vertex_buffer_;
  std::unique_ptr<QRhiBuffer> sphere_index_buffer_;
  std::unique_ptr<QRhiBuffer> sphere_instance_buffer_;
  std::unique_ptr<QRhiBuffer> cylinder_vertex_buffer_;
  std::unique_ptr<QRhiBuffer> cylinder_index_buffer_;
  std::unique_ptr<QRhiBuffer> cylinder_instance_buffer_;
  std::unique_ptr<QRhiBuffer> line_vertex_buffer_;
  std::unique_ptr<QRhiBuffer> line_index_buffer_;
  std::unique_ptr<QRhiBuffer> line_instance_buffer_;
  std::unique_ptr<QRhiBuffer> pick_mesh_vertex_buffer_;
  std::unique_ptr<QRhiTexture> pick_color_texture_;
  std::unique_ptr<QRhiTexture> pick_readback_texture_;
  std::unique_ptr<QRhiRenderBuffer> pick_depth_buffer_;
  std::unique_ptr<QRhiRenderPassDescriptor> pick_render_pass_;
  std::unique_ptr<QRhiTextureRenderTarget> pick_render_target_;
  std::vector<std::shared_ptr<PickReadbackState>> pending_pick_readbacks_;
  std::vector<MeshVertexGpu> mesh_vertices_;
  std::vector<std::uint32_t> mesh_indices_;
  std::vector<PickMeshVertexGpu> pick_mesh_vertices_;
  std::vector<SolidVertexGpu> sphere_vertices_;
  std::vector<std::uint32_t> sphere_indices_;
  std::vector<SphereInstanceGpu> sphere_instances_;
  std::vector<SolidVertexGpu> cylinder_vertices_;
  std::vector<std::uint32_t> cylinder_indices_;
  std::vector<CylinderInstanceGpu> cylinder_instances_;
  std::vector<LineCornerGpu> line_corners_;
  std::vector<std::uint32_t> line_indices_;
  std::vector<LineInstanceGpu> line_instances_;
  std::shared_ptr<const render::DirectVolumeData> direct_volume_;
  std::shared_ptr<const render::DirectVolumeData> reported_volume_lease_;
  QString reported_volume_state_;
  std::array<float, 4U> volume_pick_color_{};
  scene::CameraParameters camera_parameters_;
  scene::StereoParameters stereo_parameters_;
  std::array<std::int32_t, 2> stereo_output_origin_{};
  bool stereo_composite_failure_logged_{};
  model::Vec3d center_{};
  std::uint64_t revision_{};
  std::uint64_t camera_revision_{};
  std::uint64_t stereo_revision_{};
  QPointer<MolecularViewport> viewport_item_;
  QPointF pick_position_;
  QSizeF pick_item_size_;
  QSize pick_target_size_;
  std::shared_ptr<const std::vector<std::uint64_t>> pick_source_ids_;
  std::uint64_t pick_request_revision_{};
  std::uint64_t pick_packet_revision_{};
  float angle_{};
  bool geometry_dirty_{true};
  bool dynamic_geometry_dirty_{};
  bool primitive_logged_{false};
  bool pick_pending_{};
  bool volume_dirty_{};
  bool volume_stereo_logged_{};
};

} // namespace

MolecularViewport::MolecularViewport(QQuickItem *parent)
    : QQuickRhiItem{parent},
      workspace_{std::make_shared<application::Workspace>()},
      diagnostics_{std::make_shared<application::RuntimeDiagnostics>()},
      registry_{application::make_default_registry(workspace_, diagnostics_)},
      dispatcher_{registry_}, actions_{dispatcher_}, packet_{demo_packet()} {
  application::GraphicsRuntimeInfo pending;
  pending.status = application::RuntimeStatus::not_initialized;
  pending.failure_reason = "Qt Quick scene graph is not initialized";
  diagnostics_->set_graphics(std::move(pending));
  if (automation::register_python_script_command(registry_, workspace_)
          .has_value()) {
    std::terminate();
  }
  setSampleCount(4);
  playback_timer_.setInterval(16);
  playback_timer_.setTimerType(Qt::PreciseTimer);
  connect(&playback_timer_, &QTimer::timeout, this,
          &MolecularViewport::onPlaybackTick);
  auto trajectory_scheduler = operation::TaskScheduler::create(
      {2U, 2U, 512U * 1024U * 1024U, 2U, 32U});
  if (!trajectory_scheduler.has_value()) std::terminate();
  trajectory_task_scheduler_ = std::move(trajectory_scheduler.value());
  trajectory_task_generation_ = std::make_shared<std::atomic_uint64_t>(0U);
  trajectory_task_timer_.setInterval(4);
  trajectory_task_timer_.setTimerType(Qt::PreciseTimer);
  connect(&trajectory_task_timer_, &QTimer::timeout, this,
          &MolecularViewport::onTrajectoryTaskPoll);
  auto analysis_scheduler = operation::TaskScheduler::create(
      {1U, 2U, 512U * 1024U * 1024U, 2U, 16U});
  if (!analysis_scheduler.has_value()) std::terminate();
  analysis_task_scheduler_ = std::move(analysis_scheduler.value());
  analysis_task_generation_ = std::make_shared<std::atomic_uint64_t>(0U);
  analysis_task_timer_.setInterval(4);
  analysis_task_timer_.setTimerType(Qt::PreciseTimer);
  connect(&analysis_task_timer_, &QTimer::timeout, this,
          &MolecularViewport::onAnalysisTaskPoll);
  auto volume_scheduler = operation::TaskScheduler::create(
      {1U, 2U, 1024U * 1024U * 1024U, 2U, 16U});
  if (!volume_scheduler.has_value()) std::terminate();
  volume_task_scheduler_ = std::move(volume_scheduler.value());
  volume_task_generation_ = std::make_shared<std::atomic_uint64_t>(0U);
  volume_task_timer_.setInterval(4);
  volume_task_timer_.setTimerType(Qt::PreciseTimer);
  connect(&volume_task_timer_, &QTimer::timeout, this,
          &MolecularViewport::onDirectVolumeTaskPoll);
  camera_animation_timer_.setInterval(16);
  camera_animation_timer_.setTimerType(Qt::PreciseTimer);
  connect(&camera_animation_timer_, &QTimer::timeout, this,
          &MolecularViewport::onCameraAnimationTick);
  connect(this, &QQuickItem::widthChanged, this,
          &MolecularViewport::analysisResultsChanged);
  connect(this, &QQuickItem::heightChanged, this,
          &MolecularViewport::analysisResultsChanged);
  resetView();
}

MolecularViewport::~MolecularViewport() {
  cancelAnalysisTask();
  cancelDirectVolumeTask();
  cancelTrajectoryTask();
  // Join analysis workers before Workspace, registry and UI state are torn
  // down; task commit closures may reference all three.
  analysis_task_scheduler_.reset();
  volume_task_scheduler_.reset();
  // Join trajectory workers while every viewport member they can reference is
  // still alive. Relying on reverse member destruction would tear down the
  // polling timer and task state before the scheduler joins its workers.
  trajectory_task_scheduler_.reset();
  script_cancellation_.request_cancel();
  if (script_worker_.joinable())
    script_worker_.join();
}

bool MolecularViewport::runPythonScript(const QUrl &url, bool isolated) {
  if (script_running_) {
    setStatus(QStringLiteral("A Python script is already running"), atom_count_,
              primitive_count_);
    return false;
  }
  if (!url.isLocalFile()) {
    setStatus(QStringLiteral("Only local Python scripts can be executed"),
              atom_count_, primitive_count_);
    return false;
  }

  if (trajectory_playing_ && !setTrajectoryPlaying(false))
    return false;
  if (script_worker_.joinable())
    script_worker_.join();

  script_cancellation_ = operation::CancellationToken{};
  const auto cancellation = script_cancellation_;
  const auto path = url.toLocalFile().toStdString();
  script_running_ = true;
  script_output_ = isolated
                       ? QStringLiteral("Running isolated Python script…")
                       : QStringLiteral("Running trusted Python script…");
  emit scriptRunningChanged();
  emit scriptOutputChanged();
  setStatus(QStringLiteral("Python script running"), atom_count_,
            primitive_count_);

  QPointer<MolecularViewport> guard{this};
  script_worker_ = std::thread([this, guard, cancellation, path, isolated] {
    operation::TaskContext context{cancellation, {}};
    auto outcome = actions_.trigger(
        gui::Action{isolated ? "script run-isolated" : "script run",
                    {{"path", path}, {"trust", "true"}}},
        context);
    if (guard.isNull())
      return;
    QMetaObject::invokeMethod(
        guard.data(),
        [guard, outcome = std::move(outcome)]() mutable {
          if (!guard.isNull())
            guard->finishPythonScript(std::move(outcome));
        },
        Qt::QueuedConnection);
  });
  return true;
}

void MolecularViewport::cancelPythonScript() {
  if (!script_running_)
    return;
  script_cancellation_.request_cancel();
  script_output_ = QStringLiteral("Cancellation requested…");
  emit scriptOutputChanged();
  setStatus(QStringLiteral("Python script cancellation requested"), atom_count_,
            primitive_count_);
}

void MolecularViewport::finishPythonScript(
    application::DispatchOutcome outcome) {
  if (script_worker_.joinable())
    script_worker_.join();

  QString output;
  if (outcome.succeeded()) {
    const auto &response = std::get<command::Response>(outcome.envelope.payload);
    const auto append_field = [&response, &output](std::string_view name,
                                                   const QString &label) {
      const auto found = response.fields.find(name);
      if (found == response.fields.end())
        return;
      const auto *value = std::get_if<std::string>(&found->second.data);
      if (value == nullptr || value->empty())
        return;
      if (!output.isEmpty())
        output += QStringLiteral("\n");
      output += label + QString::fromStdString(*value);
    };
    append_field("stdout", QStringLiteral("stdout:\n"));
    append_field("stderr", QStringLiteral("stderr:\n"));
    script_output_ = output.isEmpty()
                         ? QStringLiteral("Script completed without output")
                         : std::move(output);
    syncCameraState();
    emit viewsChanged();
    static_cast<void>(rebuildScenePacket());
    syncTrajectoryState();
    emit objectsChanged();
    emit volumeChanged();
    emit scriptOutputChanged();
    script_running_ = false;
    emit scriptRunningChanged();
    setStatus(QStringLiteral("Python script completed"), atom_count_,
              primitive_count_);
    emit scriptFinished(true);
    return;
  }

  const auto &failure = std::get<operation::Error>(outcome.envelope.payload);
  const auto append_detail = [&failure, &output](std::string_view name,
                                                 const QString &label) {
    const auto found = failure.details.find(name);
    if (found == failure.details.end() || found->second.empty())
      return;
    if (!output.isEmpty())
      output += QStringLiteral("\n");
    output += label + QString::fromStdString(found->second);
  };
  append_detail("stdout", QStringLiteral("stdout:\n"));
  append_detail("stderr", QStringLiteral("stderr:\n"));
  script_output_ = output.isEmpty()
                       ? QString::fromStdString(failure.message)
                       : std::move(output);
  syncCameraState();
  emit viewsChanged();
  static_cast<void>(rebuildScenePacket());
  syncTrajectoryState();
  emit objectsChanged();
  emit volumeChanged();
  emit scriptOutputChanged();
  script_running_ = false;
  emit scriptRunningChanged();
  setStatus(QStringLiteral("Python script failed: ") +
                QString::fromStdString(failure.message),
            atom_count_, primitive_count_);
  emit scriptFinished(false);
}

void MolecularViewport::clearScriptOutput() {
  if (script_output_.isEmpty())
    return;
  script_output_.clear();
  emit scriptOutputChanged();
}

QString MolecularViewport::systemInfoJson() const {
  operation::TaskContext context;
  const auto outcome = actions_.trigger(gui::Action{"system info", {}}, context);
  const auto rendered = command::render(
      outcome.envelope, operation::OutputFormat::json);
  return rendered.has_value() ? QString::fromStdString(rendered.value())
                              : QString{};
}

QString MolecularViewport::chemicalSemanticsJson() const {
  operation::TaskContext context;
  const auto outcome =
      actions_.trigger(gui::Action{"object chemistry", {}}, context);
  const auto rendered =
      command::render(outcome.envelope, operation::OutputFormat::json);
  return rendered.has_value() ? QString::fromStdString(rendered.value())
                              : QString{};
}

QString MolecularViewport::chemicalPerceptionJson(bool apply) const {
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"object perceive-chemistry",
                  {{"apply", apply ? "true" : "false"}}},
      context);
  const auto rendered =
      command::render(outcome.envelope, operation::OutputFormat::json);
  return rendered.has_value() ? QString::fromStdString(rendered.value())
                              : QString{};
}

QVariantList MolecularViewport::viewItems() const {
  QVariantList items;
  const auto views = workspace_->list_named_views();
  items.reserve(static_cast<qsizetype>(views.size()));
  for (const auto &view : views) {
    QVariantMap item;
    item.insert(QStringLiteral("name"), QString::fromStdString(view.name));
    item.insert(QStringLiteral("projection"),
                view.camera.projection == scene::ProjectionMode::orthographic
                    ? QStringLiteral("orthographic")
                    : QStringLiteral("perspective"));
    item.insert(QStringLiteral("distance"), view.camera.distance);
    items.push_back(std::move(item));
  }
  return items;
}

QVariantList MolecularViewport::sceneItems() const {
  QVariantList items;
  const auto scenes = workspace_->list_named_scenes();
  items.reserve(static_cast<qsizetype>(scenes.size()));
  for (const auto &scene : scenes) {
    QVariantMap item;
    item.insert(QStringLiteral("name"), QString::fromStdString(scene.name));
    item.insert(QStringLiteral("objectCount"),
                static_cast<qulonglong>(scene.molecular_object_count));
    item.insert(QStringLiteral("volumeCount"),
                static_cast<qulonglong>(scene.volume_object_count));
    item.insert(QStringLiteral("current"), scene.current);
    items.push_back(std::move(item));
  }
  return items;
}

QVariantMap MolecularViewport::movieState() const {
  QVariantMap result;
  if (!workspace_->movie().has_value()) {
    result.insert(QStringLiteral("configured"), false);
    return result;
  }
  const auto &movie = *workspace_->movie();
  result.insert(QStringLiteral("configured"), true);
  result.insert(QStringLiteral("frameCount"),
                static_cast<qulonglong>(movie.frame_count));
  result.insert(QStringLiteral("currentFrame"),
                static_cast<qulonglong>(movie.current_frame));
  result.insert(QStringLiteral("fps"), movie.frames_per_second);
  result.insert(QStringLiteral("loop"), movie.loop);
  result.insert(QStringLiteral("playing"), movie.playing);
  result.insert(QStringLiteral("keyframeCount"),
                static_cast<qulonglong>(movie.keyframes.size()));
  return result;
}

bool MolecularViewport::storeNamedView(const QString &name) {
  const auto trimmed = name.trimmed();
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"view store", {{"name", trimmed.toStdString()}}}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Store view failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  emit viewsChanged();
  setStatus(QStringLiteral("Stored view · ") + trimmed, atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::recallNamedView(const QString &name) {
  return recallNamedViewAnimated(name, 0.0, 1);
}

bool MolecularViewport::recallNamedViewAnimated(const QString &name,
                                                double duration_seconds,
                                                int hand) {
  const auto start = camera_;
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"view recall",
                  {{"duration", number_text(duration_seconds)},
                   {"hand", std::to_string(hand)},
                   {"name", name.toStdString()}}},
      context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Recall view failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  if (start.has_value() && duration_seconds > 0.0) {
    startCameraAnimation(start.value(), workspace_->camera(), duration_seconds,
                         hand);
    setStatus(QStringLiteral("Animating to view · ") + name, atom_count_,
              primitive_count_);
  } else {
    syncCameraState();
    setStatus(QStringLiteral("Recalled view · ") + name, atom_count_,
              primitive_count_);
  }
  return true;
}

bool MolecularViewport::deleteNamedView(const QString &name) {
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"view delete", {{"name", name.toStdString()}}}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Delete view failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  emit viewsChanged();
  setStatus(QStringLiteral("Deleted view · ") + name, atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::clearNamedViews() {
  operation::TaskContext context;
  const auto outcome =
      actions_.trigger(gui::Action{"view clear", {}}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Clear views failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  emit viewsChanged();
  setStatus(QStringLiteral("Cleared named views"), atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::storeNamedScene(const QString &name) {
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"scene store", {{"name", name.trimmed().toStdString()}}},
      context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Store scene failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  emit sessionChanged();
  setStatus(QStringLiteral("Stored scene · ") + name.trimmed(), atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::recallNamedScene(const QString &name) {
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"scene recall", {{"name", name.toStdString()}}}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Recall scene failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  if (!refreshWorkspacePresentation()) return false;
  emit sessionChanged();
  setStatus(QStringLiteral("Recalled scene · ") + name, atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::deleteNamedScene(const QString &name) {
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"scene delete", {{"name", name.toStdString()}}}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Delete scene failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  emit sessionChanged();
  return true;
}

bool MolecularViewport::clearNamedScenes() {
  operation::TaskContext context;
  const auto outcome =
      actions_.trigger(gui::Action{"scene clear", {}}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Clear scenes failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  emit sessionChanged();
  return true;
}

bool MolecularViewport::configureMovie(qulonglong frames, double fps,
                                       bool loop) {
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"movie configure",
                  {{"fps", number_text(fps)},
                   {"frames", std::to_string(frames)},
                   {"loop", loop ? "true" : "false"}}},
      context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Configure movie failed: ") +
                  outcome_error(outcome), atom_count_, primitive_count_);
    return false;
  }
  emit sessionChanged();
  return true;
}

bool MolecularViewport::setMovieKeyframe(qulonglong frame,
                                         const QString &scene_name,
                                         qlonglong trajectory_frame) {
  command::Arguments arguments{{"frame", std::to_string(frame)}};
  if (!scene_name.trimmed().isEmpty())
    arguments.emplace("scene", scene_name.trimmed().toStdString());
  if (trajectory_frame >= 0)
    arguments.emplace("trajectory-frame", std::to_string(trajectory_frame));
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"movie keyframe", std::move(arguments)}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Store movie keyframe failed: ") +
                  outcome_error(outcome), atom_count_, primitive_count_);
    return false;
  }
  emit sessionChanged();
  return true;
}

bool MolecularViewport::seekMovie(qulonglong frame) {
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"movie seek", {{"frame", std::to_string(frame)}}}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Movie seek failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  if (!refreshWorkspacePresentation()) return false;
  emit sessionChanged();
  return true;
}

bool MolecularViewport::setMoviePlaying(bool playing) {
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{playing ? "movie play" : "movie pause", {}}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Movie playback failed: ") +
                  outcome_error(outcome), atom_count_, primitive_count_);
    return false;
  }
  emit sessionChanged();
  return true;
}

bool MolecularViewport::stepMovie(qulonglong steps) {
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"movie step", {{"steps", std::to_string(steps)}}}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Movie step failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  if (!refreshWorkspacePresentation()) return false;
  emit sessionChanged();
  return true;
}

bool MolecularViewport::clearMovie() {
  operation::TaskContext context;
  const auto outcome =
      actions_.trigger(gui::Action{"movie clear", {}}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Clear movie failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  emit sessionChanged();
  return true;
}

bool MolecularViewport::saveSession(const QUrl &url,
                                    const QString &visible_panels) {
  if (!url.isLocalFile()) return false;
  operation::TaskContext context;
  const auto path = QFileInfo{url.toLocalFile()}.filesystemFilePath().string();
  command::Arguments arguments{{"path", path}};
  if (!visible_panels.trimmed().isEmpty())
    arguments.emplace("ui-visible-panels",
                      visible_panels.trimmed().toStdString());
  const auto outcome = actions_.trigger(
      gui::Action{"session save", std::move(arguments)}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Save session failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  setStatus(QStringLiteral("Session saved"), atom_count_, primitive_count_);
  return true;
}

bool MolecularViewport::loadSession(const QUrl &url, const QUrl &recovery) {
  if (!url.isLocalFile() || (!recovery.isEmpty() && !recovery.isLocalFile()))
    return false;
  command::Arguments arguments{{"path", QFileInfo{url.toLocalFile()}
                                           .filesystemFilePath().string()}};
  if (!recovery.isEmpty())
    arguments.emplace("recovery", QFileInfo{recovery.toLocalFile()}
                                      .filesystemFilePath().string());
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"session load", std::move(arguments)}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Load session failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  session_visible_panels_.clear();
  if (const auto *response =
          std::get_if<command::Response>(&outcome.envelope.payload)) {
    if (const auto extensions_found = response->fields.find("extensions");
        extensions_found != response->fields.end()) {
      if (const auto *extensions = std::get_if<command::Value::Object>(
              &extensions_found->second.data)) {
        if (const auto panels_found = extensions->find("ui.visible-panels");
            panels_found != extensions->end()) {
          if (const auto *panels =
                  std::get_if<std::string>(&panels_found->second.data)) {
            session_visible_panels_ = QString::fromStdString(*panels);
          }
        }
      }
    }
  }
  if (!refreshWorkspacePresentation()) return false;
  emit viewsChanged();
  emit sessionChanged();
  emit analysisResultsChanged();
  setStatus(QStringLiteral("Session loaded"), atom_count_, primitive_count_);
  return true;
}

bool MolecularViewport::autosaveSession(const QUrl &primary,
                                        const QUrl &recovery,
                                        const QString &visible_panels) {
  if (!primary.isLocalFile() || !recovery.isLocalFile()) return false;
  operation::TaskContext context;
  command::Arguments arguments{
      {"path", QFileInfo{primary.toLocalFile()}.filesystemFilePath().string()},
      {"recovery",
       QFileInfo{recovery.toLocalFile()}.filesystemFilePath().string()}};
  if (!visible_panels.trimmed().isEmpty())
    arguments.emplace("ui-visible-panels",
                      visible_panels.trimmed().toStdString());
  const auto outcome = actions_.trigger(
      gui::Action{"session autosave", std::move(arguments)},
      context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Autosave failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  return true;
}

QString MolecularViewport::pymolViewText() const {
  operation::TaskContext context;
  const auto outcome =
      actions_.trigger(gui::Action{"view export-pymol", {}}, context);
  if (!outcome.succeeded())
    return {};
  const auto &response = std::get<command::Response>(outcome.envelope.payload);
  const auto found = response.fields.find("text");
  if (found == response.fields.end())
    return {};
  const auto *text = std::get_if<std::string>(&found->second.data);
  return text == nullptr ? QString{} : QString::fromStdString(*text);
}

bool MolecularViewport::importPymolView(const QString &values) {
  return importPymolViewAnimated(values, 0.0, 1);
}

bool MolecularViewport::importPymolViewAnimated(const QString &values,
                                                double duration_seconds,
                                                int hand) {
  const auto start = camera_;
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"view import-pymol",
                  {{"duration", number_text(duration_seconds)},
                   {"hand", std::to_string(hand)},
                   {"values", values.toStdString()}}},
      context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("PyMOL view import failed: ") +
                  outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  if (start.has_value() && duration_seconds > 0.0) {
    startCameraAnimation(start.value(), workspace_->camera(), duration_seconds,
                         hand);
    setStatus(QStringLiteral("Animating imported PyMOL view"), atom_count_,
              primitive_count_);
  } else {
    syncCameraState();
    setStatus(QStringLiteral("Imported PyMOL 18-value view"), atom_count_,
              primitive_count_);
  }
  return true;
}

void MolecularViewport::setGraphicsRuntimeInfo(
    application::GraphicsRuntimeInfo info) {
  diagnostics_->set_graphics(std::move(info));
  emit graphicsDiagnosticsChanged();
}

QQuickRhiItemRenderer *MolecularViewport::createRenderer() {
  return new MolecularViewportRenderer;
}

void MolecularViewport::setAngle(float angle) {
  if (qFuzzyCompare(angle_, angle))
    return;
  angle_ = angle;
  emit angleChanged();
  update();
}

QVariantList MolecularViewport::objectItems() const {
  QVariantList items;
  const auto objects = workspace_->list_objects();
  const auto workspace_objects = workspace_->objects();
  items.reserve(static_cast<qsizetype>(objects.size()));
  for (const auto &object : objects) {
    QString object_representation{QStringLiteral("none")};
    const auto source = std::find_if(
        workspace_objects.begin(), workspace_objects.end(),
        [&object](const auto &candidate) { return candidate.id == object.id; });
    if (source != workspace_objects.end() && !source->representations.empty()) {
      object_representation =
          representation_name(source->representations.back().kind);
    }
    QVariantMap item;
    item.insert(QStringLiteral("id"), QVariant::fromValue<qulonglong>(
                                          static_cast<qulonglong>(object.id)));
    item.insert(QStringLiteral("name"), QString::fromStdString(object.name));
    item.insert(QStringLiteral("atoms"),
                QVariant::fromValue<qulonglong>(
                    static_cast<qulonglong>(object.atom_count)));
    item.insert(QStringLiteral("active"), object.active);
    item.insert(QStringLiteral("visible"), object.visible);
    item.insert(QStringLiteral("representation"), object_representation);
    items.push_back(std::move(item));
  }
  return items;
}

QVariantList MolecularViewport::analysisItems() const {
  QVariantList items;
  const auto records = workspace_->analysis_results();
  items.reserve(static_cast<qsizetype>(records.size()));
  for (const auto &record : records) {
    QVariantMap item;
    item.insert(QStringLiteral("id"), QVariant::fromValue<qulonglong>(
                                           static_cast<qulonglong>(record.result_id)));
    item.insert(QStringLiteral("name"), QString::fromStdString(record.name));
    item.insert(QStringLiteral("kind"), QString::fromStdString(
                std::string{application::to_string(record.kind)}));
    item.insert(QStringLiteral("sourceStatus"),
                QString::fromStdString(std::string{application::to_string(
                    workspace_->analysis_source_status(record))}));
    item.insert(QStringLiteral("visible"), record.overlay_visible);
    item.insert(QStringLiteral("hasOverlay"),
                !std::holds_alternative<std::monostate>(record.overlay));
    item.insert(QStringLiteral("createdAt"),
                QString::fromStdString(record.provenance.created_at_utc));
    items.push_back(std::move(item));
  }
  return items;
}

QVariantList MolecularViewport::analysisLabelItems() const {
  QVariantList items;
  if (!camera_.has_value() || width() <= 0.0 || height() <= 0.0) return items;
  QMatrix4x4 projection;
  const auto &parameters = camera_->parameters();
  const auto aspect = static_cast<float>(width() / height());
  if (parameters.projection == scene::ProjectionMode::perspective) {
    projection.perspective(
        static_cast<float>(parameters.vertical_field_of_view_radians *
                           180.0 / std::numbers::pi),
        aspect, static_cast<float>(parameters.near_clip),
        static_cast<float>(parameters.far_clip));
  } else {
    const auto half_height =
        static_cast<float>(parameters.orthographic_height * 0.5);
    const auto half_width = half_height * aspect;
    projection.ortho(-half_width, half_width, -half_height, half_height,
                     static_cast<float>(parameters.near_clip),
                     static_cast<float>(parameters.far_clip));
  }
  const auto transform = projection * qt_matrix(camera_->view_matrix());
  for (const auto &label : packet_.labels) {
    const QVector4D clip = transform * QVector4D{
        static_cast<float>(label.anchor.x), static_cast<float>(label.anchor.y),
        static_cast<float>(label.anchor.z), 1.0F};
    if (!(clip.w() > 0.0F)) continue;
    const auto x = clip.x() / clip.w();
    const auto y = clip.y() / clip.w();
    const auto z = clip.z() / clip.w();
    if (z < -1.0F || z > 1.0F) continue;
    QVariantMap item;
    item.insert(QStringLiteral("x"),
                (static_cast<double>(x) + 1.0) * width() * 0.5);
    item.insert(QStringLiteral("y"),
                (1.0 - static_cast<double>(y)) * height() * 0.5);
    item.insert(QStringLiteral("text"), QString::fromStdString(label.text));
    item.insert(QStringLiteral("color"), QStringLiteral("#fff0ad"));
    items.push_back(std::move(item));
  }
  return items;
}

bool MolecularViewport::analyzeCenter(const QString &selection,
                                      const QString &mode,
                                      const QString &result_name) {
  gui::Action action{"analyze center",
                     {{"selection", selection.trimmed().toStdString()},
                      {"mode", mode.trimmed().toLower().toStdString()}}};
  if (!result_name.trimmed().isEmpty())
    action.parameters.emplace("result-name",
                              result_name.trimmed().toStdString());
  operation::TaskContext context;
  const auto outcome = actions_.trigger(action, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Center analysis failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  emit analysisResultsChanged();
  static_cast<void>(rebuildScenePacket());
  setStatus(QStringLiteral("Analysis result stored"), atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::analyzeDistance(const QString &from, const QString &to,
                                        const QString &pbc,
                                        const QString &result_name) {
  gui::Action action{"measure distance",
                     {{"from", from.trimmed().toStdString()},
                      {"to", to.trimmed().toStdString()},
                      {"mode", "atom"},
                      {"pbc", pbc.trimmed().toLower().toStdString()}}};
  if (!result_name.trimmed().isEmpty())
    action.parameters.emplace("result-name",
                              result_name.trimmed().toStdString());
  operation::TaskContext context;
  const auto outcome = actions_.trigger(action, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Distance analysis failed: ") +
                  outcome_error(outcome), atom_count_, primitive_count_);
    return false;
  }
  emit analysisResultsChanged();
  static_cast<void>(rebuildScenePacket());
  setStatus(QStringLiteral("Distance result stored"), atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::analyzeAngle(const QString &first,
                                     const QString &vertex,
                                     const QString &third, const QString &pbc,
                                     const QString &result_name) {
  gui::Action action{"measure angle",
                     {{"first", first.trimmed().toStdString()},
                      {"vertex", vertex.trimmed().toStdString()},
                      {"third", third.trimmed().toStdString()},
                      {"pbc", pbc.trimmed().toLower().toStdString()}}};
  if (!result_name.trimmed().isEmpty())
    action.parameters.emplace("result-name",
                              result_name.trimmed().toStdString());
  operation::TaskContext context;
  const auto outcome = actions_.trigger(action, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Angle analysis failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  emit analysisResultsChanged();
  static_cast<void>(rebuildScenePacket());
  setStatus(QStringLiteral("Angle result stored"), atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::analyzeDihedral(
    const QString &first, const QString &second, const QString &third,
    const QString &fourth, const QString &pbc,
    const QString &result_name) {
  gui::Action action{"measure dihedral",
                     {{"first", first.trimmed().toStdString()},
                      {"second", second.trimmed().toStdString()},
                      {"third", third.trimmed().toStdString()},
                      {"fourth", fourth.trimmed().toStdString()},
                      {"pbc", pbc.trimmed().toLower().toStdString()}}};
  if (!result_name.trimmed().isEmpty())
    action.parameters.emplace("result-name",
                              result_name.trimmed().toStdString());
  operation::TaskContext context;
  const auto outcome = actions_.trigger(action, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Dihedral analysis failed: ") +
                  outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  emit analysisResultsChanged();
  static_cast<void>(rebuildScenePacket());
  setStatus(QStringLiteral("Dihedral result stored"), atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::analyzeSasa(const QString &selection,
                                    double probe_radius, qulonglong samples,
                                    qulonglong evaluation_budget,
                                    const QString &result_name) {
  gui::Action action{
      "analyze sasa",
      {{"selection", selection.trimmed().toStdString()},
       {"probe-radius", number_text(probe_radius)},
       {"samples", std::to_string(static_cast<std::uint64_t>(samples))},
       {"evaluation-budget",
        std::to_string(static_cast<std::uint64_t>(evaluation_budget))},
       {"unit", "square-angstrom"}}};
  if (!result_name.trimmed().isEmpty())
    action.parameters.emplace("result-name",
                              result_name.trimmed().toStdString());
  const auto normalized = registry_.normalize(
      command::Invocation{action.command_name, action.parameters});
  if (!normalized.has_value()) {
    setStatus(QStringLiteral("SASA analysis failed: ") +
                  QString::fromStdString(normalized.error().message),
              atom_count_, primitive_count_);
    return false;
  }
  cancelAnalysisTask();
  const auto generation = analysis_task_generation_->fetch_add(1U) + 1U;
  application::ScheduledSasaRequest request;
  request.arguments = normalized.value().arguments;
  request.selection_expression = selection.trimmed().toStdString();
  request.probe_radius_angstrom = probe_radius;
  request.samples_per_atom = static_cast<std::size_t>(samples);
  request.evaluation_budget = static_cast<std::size_t>(evaluation_budget);
  request.generation = generation;
  const auto token = analysis_task_generation_;
  request.generation_is_current = [token](std::uint64_t value) {
    return value == token->load();
  };
  auto scheduled = application::schedule_sasa_analysis(
      workspace_, analysis_task_scheduler_, std::move(request));
  if (!scheduled.has_value()) {
    setStatus(QStringLiteral("SASA analysis failed: ") +
                  QString::fromStdString(scheduled.error().message),
              atom_count_, primitive_count_);
    return false;
  }
  pending_analysis_ = std::move(scheduled.value());
  analysis_task_running_ = true;
  analysis_task_progress_ = 0.0;
  analysis_task_stage_ = QStringLiteral("queued");
  setStatus(QStringLiteral("Calculating SASA…"), atom_count_, primitive_count_);
  emit analysisTaskChanged();
  analysis_task_timer_.start();
  return true;
}

bool MolecularViewport::analyzeRdf(
    const QString &first,const QString &second,double maximum_radius,
    double bin_width,const QString &normalization,const QString &pbc,
    qulonglong evaluation_budget,const QString &result_name) {
  if(!std::isfinite(maximum_radius)||maximum_radius<=0.0||
     !std::isfinite(bin_width)||bin_width<=0.0||bin_width>maximum_radius)
    return false;
  gui::Action action{"analyze rdf",
      {{"first",first.trimmed().toStdString()},
       {"maximum-radius",number_text(maximum_radius)},
       {"bin-width",number_text(bin_width)},
       {"normalization",normalization.trimmed().toLower().toStdString()},
       {"pbc",pbc.trimmed().toLower().toStdString()},
       {"evaluation-budget",std::to_string(static_cast<std::uint64_t>(evaluation_budget))},
       {"unit","angstrom"}}};
  if(!second.trimmed().isEmpty()) action.parameters.emplace("second",second.trimmed().toStdString());
  if(!result_name.trimmed().isEmpty()) action.parameters.emplace("result-name",result_name.trimmed().toStdString());
  const auto normalized=registry_.normalize(command::Invocation{action.command_name,action.parameters});
  if(!normalized.has_value()) {
    setStatus(QStringLiteral("RDF analysis failed: ")+QString::fromStdString(normalized.error().message),atom_count_,primitive_count_);
    return false;
  }
  cancelAnalysisTask();
  const auto generation=analysis_task_generation_->fetch_add(1U)+1U;
  application::ScheduledRdfRequest request;
  request.arguments=normalized.value().arguments;
  request.first_expression=first.trimmed().toStdString();
  request.second_expression=second.trimmed().isEmpty()?request.first_expression:second.trimmed().toStdString();
  request.maximum_radius=maximum_radius; request.bin_width=bin_width;
  request.boundary=pbc.trimmed().toLower()==QStringLiteral("minimum-image")
      ?analysis::DistanceBoundary::minimum_image:analysis::DistanceBoundary::raw;
  request.normalization=normalization.trimmed().toLower()==QStringLiteral("g-r")
      ?analysis::RdfNormalization::radial_distribution:analysis::RdfNormalization::pair_count;
  request.same_selection=second.trimmed().isEmpty();
  request.evaluation_budget=static_cast<std::uint64_t>(evaluation_budget);
  request.generation=generation;
  const auto token=analysis_task_generation_;
  request.generation_is_current=[token](std::uint64_t value){return value==token->load();};
  auto scheduled=application::schedule_rdf_analysis(workspace_,analysis_task_scheduler_,std::move(request));
  if(!scheduled.has_value()) {
    setStatus(QStringLiteral("RDF analysis failed: ")+QString::fromStdString(scheduled.error().message),atom_count_,primitive_count_);
    return false;
  }
  pending_analysis_=std::move(scheduled.value()); analysis_task_running_=true;
  analysis_task_progress_=0.0; analysis_task_stage_=QStringLiteral("queued");
  setStatus(QStringLiteral("Calculating RDF…"),atom_count_,primitive_count_);
  emit analysisTaskChanged(); analysis_task_timer_.start();
  return true;
}

bool MolecularViewport::analyzeContacts(const QString &first,
                                        const QString &second, double cutoff,
                                        const QString &pbc,
                                        const QString &result_name) {
  if (!std::isfinite(cutoff) || cutoff <= 0.0) return false;
  gui::Action action{"analyze contacts",
                     {{"first", first.trimmed().toStdString()},
                      {"cutoff", number_text(cutoff)},
                      {"pbc", pbc.trimmed().toLower().toStdString()}}};
  if (!second.trimmed().isEmpty())
    action.parameters.emplace("second", second.trimmed().toStdString());
  if (!result_name.trimmed().isEmpty())
    action.parameters.emplace("result-name",
                              result_name.trimmed().toStdString());
  operation::TaskContext context;
  const auto outcome = actions_.trigger(action, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Contact analysis failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  emit analysisResultsChanged();
  static_cast<void>(rebuildScenePacket());
  setStatus(QStringLiteral("Contact result stored"), atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::analyzeTrajectoryRmsd(const QString &selection,
                                              qulonglong reference,
                                              const QString &result_name) {
  gui::Action action{
      "analyze trajectory rmsd",
      {{"selection", selection.trimmed().toStdString()},
       {"reference", std::to_string(static_cast<std::uint64_t>(reference))}}};
  if (!result_name.trimmed().isEmpty())
    action.parameters.emplace("result-name",
                              result_name.trimmed().toStdString());
  operation::TaskContext context;
  const auto outcome = actions_.trigger(action, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Trajectory RMSD failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  emit analysisResultsChanged();
  setStatus(QStringLiteral("Trajectory RMSD result stored"), atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::analyzeTrajectoryRmsdMatrix(
    const QString &selection,qulonglong frame_pair_budget,
    const QString &result_name) {
  gui::Action action{"analyze trajectory rmsd-matrix",
      {{"selection",selection.trimmed().toStdString()},
       {"frame-pair-budget",std::to_string(static_cast<std::uint64_t>(frame_pair_budget))}}};
  if(!result_name.trimmed().isEmpty()) action.parameters.emplace("result-name",result_name.trimmed().toStdString());
  const auto normalized=registry_.normalize(command::Invocation{action.command_name,action.parameters});
  const auto *object=workspace_->active_object();
  const auto frame_count=object!=nullptr&&object->trajectory.has_value()
      ?object->trajectory->cache->frame_count():std::optional<std::size_t>{};
  if(!normalized.has_value()||!frame_count.has_value()||*frame_count==0U) {
    const auto message=!normalized.has_value()?normalized.error().message:
        std::string{"active object has no non-empty trajectory"};
    setStatus(QStringLiteral("Trajectory RMSD matrix failed: ")+QString::fromStdString(message),atom_count_,primitive_count_);
    return false;
  }
  cancelAnalysisTask();
  const auto generation=analysis_task_generation_->fetch_add(1U)+1U;
  application::ScheduledRmsdMatrixRequest request;
  request.arguments=normalized.value().arguments;
  request.selection_expression=selection.trimmed().toStdString();
  request.fit_selection_expression=request.selection_expression;
  request.range={0U,*frame_count-1U,1U};
  request.fit=analysis::FitMode::rigid;
  request.weight_mode=analysis::WeightMode::uniform;
  request.missing_atom_policy=analysis::MissingAtomPolicy::error;
  request.frame_pair_budget=static_cast<std::uint64_t>(frame_pair_budget);
  request.generation=generation;
  const auto token=analysis_task_generation_;
  request.generation_is_current=[token](std::uint64_t value){return value==token->load();};
  auto scheduled=application::schedule_rmsd_matrix_analysis(
      workspace_,analysis_task_scheduler_,std::move(request));
  if(!scheduled.has_value()) {
    setStatus(QStringLiteral("Trajectory RMSD matrix failed: ")+QString::fromStdString(scheduled.error().message),atom_count_,primitive_count_);
    return false;
  }
  pending_analysis_=std::move(scheduled.value()); analysis_task_running_=true;
  analysis_task_progress_=0.0; analysis_task_stage_=QStringLiteral("queued");
  setStatus(QStringLiteral("Calculating trajectory RMSD matrix…"),atom_count_,primitive_count_);
  emit analysisTaskChanged(); analysis_task_timer_.start();
  return true;
}

QString MolecularViewport::analysisResultJson(qulonglong result_id) const {
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"result get", {{"id", std::to_string(result_id)}}}, context);
  const auto rendered = command::render(outcome.envelope,
                                        operation::OutputFormat::json);
  return rendered.has_value() ? QString::fromStdString(rendered.value())
                              : QString{};
}

bool MolecularViewport::setAnalysisResultVisible(qulonglong result_id,
                                                 bool visible) {
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{visible ? "result show" : "result hide",
                  {{"id", std::to_string(result_id)}}}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Analysis overlay failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  emit analysisResultsChanged();
  return rebuildScenePacket();
}

bool MolecularViewport::deleteAnalysisResult(qulonglong result_id) {
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"result delete", {{"id", std::to_string(result_id)}}},
      context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Delete analysis result failed: ") +
                  outcome_error(outcome), atom_count_, primitive_count_);
    return false;
  }
  emit analysisResultsChanged();
  return rebuildScenePacket();
}

bool MolecularViewport::exportAnalysisResult(qulonglong result_id,
                                             const QUrl &url,
                                             const QString &format) {
  if (!url.isLocalFile()) return false;
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"result export",
                  {{"id", std::to_string(result_id)},
                   {"path", QFileInfo{url.toLocalFile()}
                                .filesystemFilePath().string()},
                   {"output-format", format.trimmed().toLower().toStdString()},
                   {"overwrite", "false"}}}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Export analysis result failed: ") +
                  outcome_error(outcome), atom_count_, primitive_count_);
    return false;
  }
  setStatus(QStringLiteral("Analysis result exported"), atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::loadStructure(const QUrl &url) {
  if (!url.isLocalFile()) {
    setStatus(
        QStringLiteral("Only local molecular structure and scalar volume files "
                       "are supported"),
        atom_count_, primitive_count_);
    return false;
  }
  const QFileInfo file_info{url.toLocalFile()};
  gui::Action action;
  const auto suffix = file_info.suffix().toLower();
  const auto is_opendx = suffix == QStringLiteral("dx");
  const auto is_mrc =
      suffix == QStringLiteral("mrc") || suffix == QStringLiteral("map") ||
      suffix == QStringLiteral("ccp4") || suffix == QStringLiteral("mrcs");
  if (is_opendx || is_mrc) {
    action.command_name = "volume load";
    action.parameters.emplace("path", file_info.filesystemFilePath().string());
    action.parameters.emplace("file-format", is_opendx ? "opendx" : "mrc");
    action.parameters.emplace("coordinate-unit", "angstrom");
    operation::TaskContext context;
    const auto loaded = actions_.trigger(action, context);
    if (!loaded.succeeded()) {
      setStatus(QStringLiteral("Volume load failed: ") + outcome_error(loaded),
                atom_count_, primitive_count_);
      return false;
    }
    if (workspace_->volumes().empty()) {
      setStatus(QStringLiteral("Volume load succeeded without a grid"),
                atom_count_, primitive_count_);
      return false;
    }
    const auto &volume = workspace_->volumes().back();
    const auto shape = volume.grid->shape();
    const auto [minimum, maximum] = volume.grid->scalars().range();
    has_volume_ = true;
    volume_minimum_ = minimum;
    volume_maximum_ = maximum;
    volume_level_ = minimum + (maximum - minimum) * 0.5;
    volume_mode_ = QStringLiteral("isosurface");
    volume_slice_axis_ = QStringLiteral("z");
    volume_slice_index_ = static_cast<qulonglong>((shape.z - 1U) / 2U);
    volume_slice_maximum_ = static_cast<qulonglong>(shape.z - 1U);
    emit volumeChanged();
    if (!setVolumeIsosurface(volume_level_)) return false;
    resetView();
    qInfo("MolShredder desktop volume loaded: voxels=%llu "
          "dimensions=%llux%llux%llu format=%s",
          static_cast<unsigned long long>(volume.grid->value_count()),
          static_cast<unsigned long long>(shape.x),
          static_cast<unsigned long long>(shape.y),
          static_cast<unsigned long long>(shape.z),
          is_opendx ? "opendx" : "mrc");
    return true;
  }
  action.command_name = "load";
  action.parameters.emplace("path", file_info.filesystemFilePath().string());
  operation::TaskContext context;
  const auto loaded = actions_.trigger(action, context);
  if (!loaded.succeeded()) {
    setStatus(QStringLiteral("Load failed: ") + outcome_error(loaded),
              atom_count_, primitive_count_);
    return false;
  }
  const auto *object = workspace_->active_object();
  if (object == nullptr) {
    setStatus(QStringLiteral("Load succeeded without an active object"), 0U,
              0U);
    return false;
  }
  atom_count_ =
      static_cast<qulonglong>(object->system->topology()->atom_count());
  selection_text_ = QStringLiteral("No selection");
  emit selectionChanged();
  const auto frame_count =
      object->system->coordinates()->frame_count().value_or(0U);
  if (frame_count == 0U) {
    if (!rebuildScenePacket())
      return false;
    syncTrajectoryState();
    setStatus(QStringLiteral("Topology loaded · ") +
                  QString::number(atom_count_) +
                  QStringLiteral(" atoms · attach a DCD/XTC/TRR/RST7 "
                                 "trajectory or save the topology"),
              atom_count_, 0U);
    qInfo("MolShredder desktop topology loaded: atoms=%llu frames=0",
          static_cast<unsigned long long>(atom_count_));
    return true;
  }
  if (!rebuildRepresentation())
    return false;
  syncTrajectoryState();
  resetView();
  qInfo("MolShredder desktop loaded: atoms=%llu representation=%s "
        "primitives=%llu",
        static_cast<unsigned long long>(atom_count_),
        representation_.toUtf8().constData(),
        static_cast<unsigned long long>(primitive_count_));
  return true;
}

bool MolecularViewport::loadStructures(const QVariantList &urls) {
  if (urls.empty()) {
    setStatus(QStringLiteral("Select at least one molecular structure"),
              atom_count_, primitive_count_);
    return false;
  }
  if (urls.size() == 1) return loadStructure(urls.front().toUrl());
  std::string paths;
  std::string names;
  std::map<std::string, std::size_t, std::less<>> name_counts;
  for (const auto &value : urls) {
    const auto url = value.toUrl();
    if (!url.isLocalFile()) {
      setStatus(QStringLiteral("Batch load supports local files only"),
                atom_count_, primitive_count_);
      return false;
    }
    const QFileInfo file_info{url.toLocalFile()};
    const auto suffix = file_info.suffix().toLower();
    if (suffix == QStringLiteral("dx") || suffix == QStringLiteral("mrc") ||
        suffix == QStringLiteral("map") || suffix == QStringLiteral("ccp4") ||
        suffix == QStringLiteral("mrcs")) {
      setStatus(QStringLiteral("Open scalar volumes one at a time"),
                atom_count_, primitive_count_);
      return false;
    }
    const auto path = file_info.filesystemFilePath().string();
    if (path.find(';') != std::string::npos) {
      setStatus(QStringLiteral("Batch paths containing ';' are unsupported"),
                atom_count_, primitive_count_);
      return false;
    }
    if (!paths.empty()) paths += ';';
    paths += path;
    auto object_name = file_info.completeBaseName().toStdString();
    auto &name_count = name_counts[object_name];
    ++name_count;
    if (name_count > 1U)
      object_name += "_" + std::to_string(name_count);
    if (!names.empty()) names += ';';
    names += object_name;
  }
  gui::Action action;
  action.command_name = "load batch";
  action.parameters.emplace("paths", std::move(paths));
  action.parameters.emplace("names", std::move(names));
  operation::TaskContext context{
      {}, [this](const operation::ProgressUpdate &update) {
        setStatus(QStringLiteral("Loading structures · ") +
                      QString::number(update.fraction * 100.0, 'f', 0) +
                      QStringLiteral("%"),
                  atom_count_, primitive_count_);
      }};
  const auto loaded = actions_.trigger(action, context);
  if (!loaded.succeeded()) {
    setStatus(QStringLiteral("Batch load failed: ") + outcome_error(loaded),
              atom_count_, primitive_count_);
    return false;
  }
  const auto *object = workspace_->active_object();
  if (object == nullptr) {
    setStatus(QStringLiteral("Batch load produced no active object"), 0U, 0U);
    return false;
  }
  atom_count_ =
      static_cast<qulonglong>(object->system->topology()->atom_count());
  selection_text_ = QStringLiteral("No selection");
  emit selectionChanged();
  if (object->system->coordinates()->frame_count().value_or(0U) == 0U) {
    if (!rebuildScenePacket()) return false;
  } else if (!rebuildRepresentation()) {
    return false;
  }
  syncTrajectoryState();
  resetView();
  setStatus(QStringLiteral("Atomic batch loaded · ") +
                QString::number(urls.size()) + QStringLiteral(" inputs"),
            atom_count_, primitive_count_);
  return true;
}

bool MolecularViewport::saveStructure(const QUrl &url, bool all_frames) {
  if (!url.isLocalFile()) {
    setStatus(
        QStringLiteral("Only local PDB/mmCIF/G96/GRO/PQR/MOL/MOL2/PSF/SDF/XYZ "
                       "output is supported"),
        atom_count_, primitive_count_);
    return false;
  }
  if (workspace_->active_object() == nullptr) {
    setStatus(QStringLiteral("Open a structure before saving"), atom_count_,
              primitive_count_);
    return false;
  }
  const QFileInfo file_info{url.toLocalFile()};
  gui::Action action;
  action.command_name = "save";
  action.parameters.emplace("path", file_info.filesystemFilePath().string());
  const auto suffix = file_info.suffix().toLower();
  std::string format{"xyz"};
  if (suffix == QStringLiteral("pdb") || suffix == QStringLiteral("ent")) {
    format = "pdb";
  } else if (suffix == QStringLiteral("cif") ||
             suffix == QStringLiteral("mmcif")) {
    format = "mmcif";
  } else if (suffix == QStringLiteral("bcif")) {
    format = "bcif";
  } else if (suffix == QStringLiteral("pqr")) {
    format = "pqr";
  } else if (suffix == QStringLiteral("mol")) {
    format = "mol";
  } else if (suffix == QStringLiteral("mol2")) {
    format = "mol2";
  } else if (suffix == QStringLiteral("psf")) {
    format = "psf";
  } else if (suffix == QStringLiteral("gro")) {
    format = "gro";
  } else if (suffix == QStringLiteral("g96")) {
    format = "g96";
  } else if (suffix == QStringLiteral("sdf") ||
             suffix == QStringLiteral("sd")) {
    format = "sdf";
  }
  action.parameters.emplace("file-format", std::move(format));
  action.parameters.emplace("frames", all_frames ? "all" : "current");
  action.parameters.emplace("overwrite", "true");
  operation::TaskContext context;
  const auto saved = actions_.trigger(action, context);
  if (!saved.succeeded()) {
    setStatus(QStringLiteral("Save failed: ") + outcome_error(saved),
              atom_count_, primitive_count_);
    return false;
  }
  const auto losses = response_unsigned(saved, "loss_item_count").value_or(0U);
  setStatus(QStringLiteral("Saved ") + file_info.fileName() +
                QStringLiteral(" · ") + QString::number(losses) +
                QStringLiteral(" semantic loss items reported"),
            atom_count_, primitive_count_);
  return true;
}

bool MolecularViewport::loadTrajectory(const QUrl &url,
                                       const QString &coordinate_unit,
                                       const QString &mapping,
                                       const QString &atom_map) {
  if (!url.isLocalFile()) {
    setStatus(
        QStringLiteral("Only local DCD/XTC/TRR/MDCRD/NetCDF/RST7/LAMMPS/BINPOS "
                       "trajectories are supported"),
        atom_count_, primitive_count_);
    return false;
  }
  if (workspace_->active_object() == nullptr) {
    setStatus(QStringLiteral("Open a topology before attaching a trajectory"),
              atom_count_, primitive_count_);
    return false;
  }
  const QFileInfo file_info{url.toLocalFile()};
  command::Arguments arguments{
      {"path", file_info.filesystemFilePath().string()},
      {"coordinate-unit", coordinate_unit.toStdString()},
      {"mapping", mapping.toStdString()}};
  if (mapping == QStringLiteral("explicit")) {
    if (atom_map.trimmed().isEmpty()) {
      setStatus(QStringLiteral("Explicit mapping requires stable atom IDs in "
                               "trajectory source order"),
                atom_count_, primitive_count_);
      return false;
    }
    arguments.emplace("atom-map", atom_map.trimmed().toStdString());
    arguments.emplace(
        "expected-topology-version",
        std::to_string(workspace_->active_object()->system->topology()->version()));
  }
  const auto normalized = registry_.normalize({"traj load", arguments});
  if (!normalized.has_value()) {
    setStatus(QStringLiteral("Trajectory load failed: ") +
                  QString::fromStdString(normalized.error().message),
              atom_count_, primitive_count_);
    return false;
  }
  const auto &values = normalized.value().arguments;
  const auto parse_size = [&](std::string_view name,
                              bool positive)
      -> std::optional<std::size_t> {
    const auto found = values.find(name);
    if (found == values.end()) return std::nullopt;
    std::size_t value{};
    const auto parsed = std::from_chars(
        found->second.data(), found->second.data() + found->second.size(),
        value);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != found->second.data() + found->second.size() ||
        (positive && value == 0U))
      return std::nullopt;
    return value;
  };
  const auto cache_mib = parse_size("cache-mib", true);
  const auto prefetch_frames = parse_size("prefetch-frames", false);
  constexpr std::size_t bytes_per_mib = 1024U * 1024U;
  if (!cache_mib.has_value() || !prefetch_frames.has_value() ||
      *cache_mib > std::numeric_limits<std::size_t>::max() / bytes_per_mib) {
    setStatus(QStringLiteral("Trajectory load failed: invalid resource limit"),
              atom_count_, primitive_count_);
    return false;
  }
  const auto parse_format = [](std::string_view value) {
    if (value == "dcd") return io::TrajectoryFormat::dcd;
    if (value == "trr") return io::TrajectoryFormat::trr;
    if (value == "xtc") return io::TrajectoryFormat::xtc;
    if (value == "rst7") return io::TrajectoryFormat::rst7;
    if (value == "mdcrd" || value == "crd")
      return io::TrajectoryFormat::mdcrd;
    if (value == "crdbox") return io::TrajectoryFormat::crdbox;
    if (value == "netcdf" || value == "nc" || value == "ncdf" ||
        value == "ncrst")
      return io::TrajectoryFormat::amber_netcdf;
    if (value == "h5md") return io::TrajectoryFormat::h5md;
    if (value == "lammps" || value == "lammpstrj" || value == "dump")
      return io::TrajectoryFormat::lammps_dump;
    if (value == "binpos") return io::TrajectoryFormat::binpos;
    return io::TrajectoryFormat::auto_detect;
  };
  const auto policy = values.at("mapping") == "exact"
                          ? trajectory::AtomMappingPolicy::exact
                          : (values.at("mapping") == "explicit"
                                 ? trajectory::AtomMappingPolicy::explicit_map
                                 : trajectory::AtomMappingPolicy::index_order);
  std::vector<model::AtomId> atom_ids;
  if (policy == trajectory::AtomMappingPolicy::explicit_map) {
    std::string_view remaining{values.at("atom-map")};
    while (!remaining.empty()) {
      const auto comma = remaining.find(',');
      const auto token = remaining.substr(0U, comma);
      std::uint64_t id{};
      const auto parsed =
          std::from_chars(token.data(), token.data() + token.size(), id);
      if (token.empty() || parsed.ec != std::errc{} ||
          parsed.ptr != token.data() + token.size() || id == 0U) {
        setStatus(QStringLiteral("Trajectory load failed: invalid atom map"),
                  atom_count_, primitive_count_);
        return false;
      }
      atom_ids.push_back(model::AtomId{id});
      if (comma == std::string_view::npos) break;
      remaining.remove_prefix(comma + 1U);
    }
  }
  if (pending_trajectory_frame_.has_value()) {
    static_cast<void>(trajectory_task_scheduler_->cancel(
        pending_trajectory_frame_->task_id));
    pending_trajectory_frame_.reset();
  }
  if (pending_trajectory_load_.has_value()) {
    static_cast<void>(trajectory_task_scheduler_->cancel(
        pending_trajectory_load_->task_id));
    pending_trajectory_load_.reset();
  }
  const auto generation =
      trajectory_task_generation_->fetch_add(1U) + 1U;
  application::ScheduledTrajectoryLoadRequest request;
  request.path = file_info.filesystemFilePath();
  request.format = parse_format(values.at("file-format"));
  request.cache_budget_bytes = *cache_mib * bytes_per_mib;
  request.prefetch_frame_count = *prefetch_frames;
  request.coordinate_unit =
      values.at("coordinate-unit") == "auto"
          ? std::nullopt
          : std::optional<operation::LengthUnit>{
                values.at("coordinate-unit") == "nanometer"
                    ? operation::LengthUnit::nanometer
                    : operation::LengthUnit::angstrom};
  if (const auto found = values.find("particle-group");
      found != values.end() && !found->second.empty())
    request.h5md_particle_group = found->second;
  request.mapping_policy = policy;
  request.source_to_target_atom_ids = std::move(atom_ids);
  if (policy == trajectory::AtomMappingPolicy::explicit_map)
    request.expected_topology_version =
        workspace_->active_object()->system->topology()->version();
  request.generation = generation;
  const auto generation_token = trajectory_task_generation_;
  request.generation_is_current =
      [generation_token](std::uint64_t value) {
        return value == generation_token->load();
      };
  auto scheduled = application::schedule_trajectory_load(
      workspace_, trajectory_task_scheduler_, std::move(request));
  if (!scheduled.has_value()) {
    setStatus(QStringLiteral("Trajectory load failed: ") +
                  QString::fromStdString(scheduled.error().message),
              atom_count_, primitive_count_);
    return false;
  }
  pending_trajectory_load_ = std::move(scheduled.value());
  trajectory_task_running_ = true;
  trajectory_task_progress_ = 0.0;
  trajectory_task_stage_ = QStringLiteral("queued");
  setStatus(QStringLiteral("Opening trajectory ") + file_info.fileName() +
                QStringLiteral("…"),
            atom_count_, primitive_count_);
  emit trajectoryTaskChanged();
  trajectory_task_timer_.start();
  return true;
}

QString MolecularViewport::trajectoryMappingText() const {
  const auto *object = workspace_->active_object();
  if (object == nullptr || !object->trajectory.has_value()) return {};
  return QString::fromUtf8(
      trajectory::to_string(object->trajectory->mapping.policy).data(),
      static_cast<qsizetype>(
          trajectory::to_string(object->trajectory->mapping.policy).size()));
}

bool MolecularViewport::seekTrajectory(qulonglong frame) {
  if (!has_trajectory_ || frame >= trajectory_frame_count_)
    return false;
  if (pending_trajectory_load_.has_value()) {
    static_cast<void>(trajectory_task_scheduler_->cancel(
        pending_trajectory_load_->task_id));
    pending_trajectory_load_.reset();
  }
  if (pending_trajectory_frame_.has_value()) {
    static_cast<void>(trajectory_task_scheduler_->cancel(
        pending_trajectory_frame_->task_id));
    pending_trajectory_frame_.reset();
  }
  const auto generation =
      trajectory_task_generation_->fetch_add(1U) + 1U;
  application::ScheduledTrajectoryFrameRequest request;
  request.frame_index = static_cast<std::size_t>(frame);
  request.generation = generation;
  const auto generation_token = trajectory_task_generation_;
  request.generation_is_current =
      [generation_token](std::uint64_t value) {
        return value == generation_token->load();
      };
  auto scheduled = application::schedule_trajectory_frame(
      workspace_, trajectory_task_scheduler_, std::move(request));
  if (!scheduled.has_value()) {
    setStatus(QStringLiteral("Trajectory seek failed: ") +
                  QString::fromStdString(scheduled.error().message),
              atom_count_, primitive_count_);
    trajectory_task_running_ = false;
    trajectory_task_progress_ = 0.0;
    trajectory_task_stage_ = QStringLiteral("failed");
    emit trajectoryTaskChanged();
    return false;
  }
  pending_trajectory_frame_ = std::move(scheduled.value());
  trajectory_task_running_ = true;
  trajectory_task_progress_ = 0.0;
  trajectory_task_stage_ = QStringLiteral("queued");
  setStatus(QStringLiteral("Seeking trajectory frame ") +
                QString::number(frame) + QStringLiteral("…"),
            atom_count_, primitive_count_);
  emit trajectoryTaskChanged();
  trajectory_task_timer_.start();
  return true;
}

void MolecularViewport::cancelTrajectoryTask() {
  const auto had_task = pending_trajectory_frame_.has_value() ||
                        pending_trajectory_load_.has_value() ||
                        trajectory_task_running_;
  if (trajectory_task_generation_ != nullptr)
    trajectory_task_generation_->fetch_add(1U);
  if (pending_trajectory_frame_.has_value() &&
      trajectory_task_scheduler_ != nullptr) {
    static_cast<void>(trajectory_task_scheduler_->cancel(
        pending_trajectory_frame_->task_id));
  }
  pending_trajectory_frame_.reset();
  if (pending_trajectory_load_.has_value() &&
      trajectory_task_scheduler_ != nullptr) {
    static_cast<void>(trajectory_task_scheduler_->cancel(
        pending_trajectory_load_->task_id));
  }
  pending_trajectory_load_.reset();
  trajectory_task_timer_.stop();
  const auto changed = trajectory_task_running_ ||
                       trajectory_task_stage_ != QStringLiteral("cancelled");
  trajectory_task_running_ = false;
  trajectory_task_progress_ = 0.0;
  trajectory_task_stage_ = QStringLiteral("cancelled");
  if (had_task)
    setStatus(QStringLiteral("Trajectory task cancelled"), atom_count_,
              primitive_count_);
  if (changed) emit trajectoryTaskChanged();
}

bool MolecularViewport::waitForTrajectoryTask(int timeout_milliseconds) {
  if (!trajectory_task_running_) return true;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  connect(this, &MolecularViewport::trajectoryTaskChanged, &loop, [&] {
    if (!trajectory_task_running_) loop.quit();
  });
  timeout.start(std::max(timeout_milliseconds, 1));
  loop.exec();
  return !trajectory_task_running_ &&
         trajectory_task_stage_ == QStringLiteral("complete");
}

bool MolecularViewport::setTrajectoryPlaying(bool playing) {
  if (!has_trajectory_)
    return false;
  if (trajectory_task_running_) cancelTrajectoryTask();
  if (!playing) {
    return invokeTrajectoryAction(
        "traj pause", {}, QStringLiteral("Trajectory pause failed: "), false);
  }
  const auto started =
      invokeTrajectoryAction("traj play",
                             {{"mode", playback_mode_.toStdString()},
                              {"direction", playback_direction_.toStdString()},
                              {"steps", "0"}},
                             QStringLiteral("Trajectory play failed: "), false);
  if (started) {
    playback_elapsed_.restart();
    playback_timer_.start();
  }
  return started;
}

bool MolecularViewport::stepTrajectory(int direction) {
  if (!has_trajectory_ || direction == 0)
    return false;
  if (trajectory_task_running_) cancelTrajectoryTask();
  if (trajectory_playing_ &&
      !invokeTrajectoryAction("traj pause", {},
                              QStringLiteral("Trajectory pause failed: "),
                              false)) {
    return false;
  }
  const auto selected_direction =
      direction < 0 ? std::string{"reverse"} : std::string{"forward"};
  if (!invokeTrajectoryAction("traj play",
                              {{"mode", playback_mode_.toStdString()},
                               {"direction", selected_direction},
                               {"steps", "1"}},
                              QStringLiteral("Trajectory step failed: "),
                              true)) {
    return false;
  }
  return invokeTrajectoryAction(
      "traj pause", {}, QStringLiteral("Trajectory pause failed: "), false);
}

bool MolecularViewport::setPlaybackMode(const QString &mode) {
  const auto normalized = mode.trimmed().toLower();
  if (!has_trajectory_ || (normalized != QStringLiteral("once") &&
                           normalized != QStringLiteral("loop") &&
                           normalized != QStringLiteral("rock"))) {
    return false;
  }
  if (trajectory_task_running_) cancelTrajectoryTask();
  const auto was_playing = trajectory_playing_;
  if (!invokeTrajectoryAction("traj play",
                              {{"mode", normalized.toStdString()},
                               {"direction", playback_direction_.toStdString()},
                               {"steps", "0"}},
                              QStringLiteral("Playback mode failed: "),
                              false)) {
    return false;
  }
  if (!was_playing) {
    return invokeTrajectoryAction(
        "traj pause", {}, QStringLiteral("Playback mode failed: "), false);
  }
  return true;
}

bool MolecularViewport::setPlaybackDirection(const QString &direction) {
  const auto normalized = direction.trimmed().toLower();
  if (!has_trajectory_ || (normalized != QStringLiteral("forward") &&
                           normalized != QStringLiteral("reverse"))) {
    return false;
  }
  if (trajectory_task_running_) cancelTrajectoryTask();
  const auto was_playing = trajectory_playing_;
  if (!invokeTrajectoryAction("traj play",
                              {{"mode", playback_mode_.toStdString()},
                               {"direction", normalized.toStdString()},
                               {"steps", "0"}},
                              QStringLiteral("Playback direction failed: "),
                              false)) {
    return false;
  }
  if (!was_playing) {
    return invokeTrajectoryAction(
        "traj pause", {}, QStringLiteral("Playback direction failed: "), false);
  }
  return true;
}

bool MolecularViewport::setTrajectoryFps(double frames_per_second) {
  if (!has_trajectory_ || !std::isfinite(frames_per_second) ||
      frames_per_second <= 0.0) {
    return false;
  }
  if (trajectory_task_running_) cancelTrajectoryTask();
  return invokeTrajectoryAction(
      "traj speed", {{"fps", std::to_string(frames_per_second)}},
      QStringLiteral("Playback speed failed: "), false);
}

bool MolecularViewport::tickTrajectory(double elapsed_milliseconds) {
  if (!has_trajectory_ || !std::isfinite(elapsed_milliseconds) ||
      elapsed_milliseconds < 0.0) {
    return false;
  }
  if (trajectory_task_running_) cancelTrajectoryTask();
  const auto previous_frame = trajectory_frame_;
  if (!invokeTrajectoryAction(
          "traj tick", {{"elapsed-ms", std::to_string(elapsed_milliseconds)}},
          QStringLiteral("Trajectory playback failed: "), false)) {
    return false;
  }
  return trajectory_frame_ == previous_frame || rebuildScenePacket();
}

bool MolecularViewport::setRepresentation(const QString &representation) {
  static const std::array<QString, 5> supported{
      QStringLiteral("lines"), QStringLiteral("sticks"),
      QStringLiteral("spheres"), QStringLiteral("ribbon"),
      QStringLiteral("cartoon")};
  const auto normalized = representation.trimmed().toLower();
  if (std::find(supported.begin(), supported.end(), normalized) ==
      supported.end()) {
    setStatus(QStringLiteral("Unsupported representation: ") + representation,
              atom_count_, primitive_count_);
    return false;
  }
  if (workspace_->active_object() != nullptr &&
      workspace_->active_object()->molecular_surface.has_value()) {
    gui::Action hide_surface;
    hide_surface.command_name = "surface hide";
    operation::TaskContext hide_context;
    const auto hidden = actions_.trigger(hide_surface, hide_context);
    if (!hidden.succeeded()) {
      setStatus(QStringLiteral("Molecular surface hide failed: ") +
                    outcome_error(hidden),
                atom_count_, primitive_count_);
      return false;
    }
  }
  if (representation_ != normalized) {
    representation_ = normalized;
    emit representationChanged();
  }
  if (workspace_->active_object() == nullptr) {
    setStatus(QStringLiteral("Representation selected; open a structure"), 0U,
              0U);
    return true;
  }
  const auto *object = workspace_->active_object();
  if (object->system->coordinates()->frame_count().value_or(0U) == 0U) {
    setStatus(QStringLiteral("Representation selected · attach coordinates or "
                             "a trajectory to render"),
              atom_count_, primitive_count_);
    return true;
  }
  return rebuildRepresentation();
}

bool MolecularViewport::applyRepresentationVisibility(
    const QString &operation, const QString &selection) {
  static const std::array<QString, 4U> operations{
      QStringLiteral("show"), QStringLiteral("hide"), QStringLiteral("as"),
      QStringLiteral("toggle")};
  const auto normalized = operation.trimmed().toLower();
  if (std::find(operations.begin(), operations.end(), normalized) ==
          operations.end() ||
      selection.trimmed().isEmpty()) {
    setStatus(QStringLiteral("Invalid representation visibility operation"),
              atom_count_, primitive_count_);
    return false;
  }
  if (workspace_->active_object() == nullptr) {
    setStatus(QStringLiteral("Open a structure before changing visibility"),
              0U, 0U);
    return false;
  }
  gui::Action action;
  action.command_name = normalized.toStdString();
  action.parameters.emplace("representation", representation_.toStdString());
  action.parameters.emplace("selection", selection.toStdString());
  operation::TaskContext context;
  const auto changed = actions_.trigger(action, context);
  if (!changed.succeeded()) {
    setStatus(QStringLiteral("Representation visibility failed: ") +
                  outcome_error(changed),
              atom_count_, primitive_count_);
    return false;
  }
  if (!rebuildScenePacket())
    return false;
  setStatus(QStringLiteral("Representation ") + normalized +
                QStringLiteral(" · ") + representation_ +
                QStringLiteral(" · ") + selection,
            atom_count_, primitive_count_);
  return true;
}

bool MolecularViewport::applyRenderSetting(
    const QString &operation, const QString &name, const QString &value,
    const QString &scope, const QString &target) {
  static const std::array<QString, 3U> operations{
      QStringLiteral("set"), QStringLiteral("unset"),
      QStringLiteral("reset")};
  static const std::array<QString, 5U> scopes{
      QStringLiteral("global"), QStringLiteral("object"),
      QStringLiteral("state"), QStringLiteral("atom"),
      QStringLiteral("bond")};
  const auto normalized_operation = operation.trimmed().toLower();
  const auto normalized_scope = scope.trimmed().toLower();
  if (std::find(operations.begin(), operations.end(), normalized_operation) ==
          operations.end() ||
      std::find(scopes.begin(), scopes.end(), normalized_scope) ==
          scopes.end() ||
      (normalized_operation != QStringLiteral("reset") &&
       name.trimmed().isEmpty()) ||
      (normalized_operation == QStringLiteral("set") &&
       value.trimmed().isEmpty()) ||
      ((normalized_scope == QStringLiteral("atom") ||
        normalized_scope == QStringLiteral("bond")) &&
       target.trimmed().isEmpty())) {
    setStatus(QStringLiteral("Invalid render setting editor input"),
              atom_count_, primitive_count_);
    return false;
  }
  gui::Action action;
  action.command_name = "setting " + normalized_operation.toStdString();
  action.parameters.emplace("scope", normalized_scope.toStdString());
  if (normalized_operation != QStringLiteral("reset"))
    action.parameters.emplace("name", name.trimmed().toStdString());
  if (normalized_operation == QStringLiteral("set"))
    action.parameters.emplace("value", value.trimmed().toStdString());
  if ((normalized_scope == QStringLiteral("atom") ||
       normalized_scope == QStringLiteral("bond")) &&
      !target.trimmed().isEmpty())
    action.parameters.emplace("target", target.trimmed().toStdString());
  operation::TaskContext context;
  const auto changed = actions_.trigger(action, context);
  if (!changed.succeeded()) {
    setStatus(QStringLiteral("Render setting failed: ") +
                  outcome_error(changed),
              atom_count_, primitive_count_);
    return false;
  }
  if (workspace_->active_object() != nullptr && !rebuildScenePacket())
    return false;
  setStatus(QStringLiteral("Render setting ") + normalized_operation +
                QStringLiteral(" · ") + name.trimmed() +
                QStringLiteral(" · ") + normalized_scope,
            atom_count_, primitive_count_);
  return true;
}

QString MolecularViewport::renderSettingJson(const QString &name,
                                              const QString &scope,
                                              const QString &target) const {
  gui::Action action;
  action.command_name = "setting get";
  action.parameters.emplace("name", name.trimmed().toStdString());
  const auto normalized_scope = scope.trimmed().toLower();
  action.parameters.emplace("scope", normalized_scope.toStdString());
  if ((normalized_scope == QStringLiteral("atom") ||
       normalized_scope == QStringLiteral("bond")) &&
      !target.trimmed().isEmpty())
    action.parameters.emplace("target", target.trimmed().toStdString());
  operation::TaskContext context;
  const auto outcome = actions_.trigger(action, context);
  const auto rendered = command::render(
      outcome.envelope, operation::OutputFormat::json);
  return rendered.has_value() ? QString::fromStdString(rendered.value())
                              : QString{};
}

bool MolecularViewport::activateObject(qulonglong object_id) {
  gui::Action action;
  action.command_name = "object activate";
  action.parameters.emplace("id", std::to_string(object_id));
  operation::TaskContext context;
  const auto activated = actions_.trigger(action, context);
  if (!activated.succeeded()) {
    setStatus(QStringLiteral("Activation failed: ") + outcome_error(activated),
              atom_count_, primitive_count_);
    return false;
  }
  syncActiveRepresentationName();
  selection_text_ = QStringLiteral("No selection");
  emit selectionChanged();
  const auto *object = workspace_->active_object();
  if (object == nullptr)
    return false;
  const auto topology_only =
      object->system->coordinates()->frame_count().value_or(0U) == 0U;
  const auto rebuilt = topology_only ? rebuildScenePacket()
                       : object->representations.empty()
                           ? rebuildRepresentation()
                           : rebuildScenePacket();
  if (topology_only && rebuilt) {
    setStatus(
        QStringLiteral("Topology active · ") +
            QString::number(object->system->topology()->atom_count()) +
            QStringLiteral(" atoms · attach a DCD/XTC/TRR/RST7 trajectory"),
        atom_count_, 0U);
  }
  syncTrajectoryState();
  return rebuilt;
}

bool MolecularViewport::setObjectVisible(qulonglong object_id, bool visible) {
  gui::Action action;
  action.command_name = "object visibility";
  action.parameters.emplace("id", std::to_string(object_id));
  action.parameters.emplace("visible", visible ? "true" : "false");
  operation::TaskContext context;
  const auto changed = actions_.trigger(action, context);
  if (!changed.succeeded()) {
    setStatus(QStringLiteral("Visibility failed: ") + outcome_error(changed),
              atom_count_, primitive_count_);
    return false;
  }
  return rebuildScenePacket();
}

bool MolecularViewport::renameObject(qulonglong object_id,
                                     const QString &name) {
  gui::Action action;
  action.command_name = "object rename";
  action.parameters.emplace("object", std::to_string(object_id));
  action.parameters.emplace("name", name.trimmed().toStdString());
  operation::TaskContext context;
  const auto changed = actions_.trigger(action, context);
  if (!changed.succeeded()) {
    setStatus(QStringLiteral("Rename failed: ") + outcome_error(changed),
              atom_count_, primitive_count_);
    return false;
  }
  return rebuildScenePacket();
}

bool MolecularViewport::deleteObject(qulonglong object_id) {
  gui::Action action;
  action.command_name = "object delete";
  action.parameters.emplace("object", std::to_string(object_id));
  operation::TaskContext context;
  const auto changed = actions_.trigger(action, context);
  if (!changed.succeeded()) {
    setStatus(QStringLiteral("Delete failed: ") + outcome_error(changed),
              atom_count_, primitive_count_);
    return false;
  }
  selection_text_ = QStringLiteral("No selection");
  emit selectionChanged();
  syncTrajectoryState();
  if (workspace_->active_object() != nullptr ||
      workspace_->active_volume() != nullptr) {
    syncActiveRepresentationName();
    return rebuildScenePacket();
  }
  atom_count_ = 0U;
  primitive_count_ = 0U;
  setRenderPacket({});
  setStatus(QStringLiteral("Workspace has no active object"), 0U, 0U);
  emit objectsChanged();
  return true;
}

bool MolecularViewport::reorderObject(qulonglong object_id,
                                      qulonglong one_based_position) {
  gui::Action action;
  action.command_name = "object reorder";
  action.parameters.emplace("object", std::to_string(object_id));
  action.parameters.emplace("position", std::to_string(one_based_position));
  operation::TaskContext context;
  const auto changed = actions_.trigger(action, context);
  if (!changed.succeeded()) {
    setStatus(QStringLiteral("Reorder failed: ") + outcome_error(changed),
              atom_count_, primitive_count_);
    return false;
  }
  return rebuildScenePacket();
}

bool MolecularViewport::editAtomPosition(qulonglong atom_id, double x,
                                         double y, double z) {
  const auto *object = workspace_->active_object();
  if (object == nullptr || !std::isfinite(x) || !std::isfinite(y) ||
      !std::isfinite(z)) {
    setStatus(QStringLiteral("Invalid atom coordinate edit"), atom_count_,
              primitive_count_);
    return false;
  }
  const auto frame = object->system->coordinates()->read_frame(0U);
  if (!frame.has_value()) {
    setStatus(QStringLiteral("Coordinate edit failed: ") +
                  QString::fromStdString(frame.error().message),
              atom_count_, primitive_count_);
    return false;
  }
  const auto coordinate_unit = frame.value()->metadata().coordinate_unit ==
                                       operation::LengthUnit::nanometer
                                   ? "nanometer"
                                   : "angstrom";
  gui::Action action;
  action.command_name = "edit atom-position";
  action.parameters = {
      {"atom-id", std::to_string(atom_id)},
      {"x", number_text(x)},
      {"y", number_text(y)},
      {"z", number_text(z)},
      {"expected-topology-version",
       std::to_string(object->system->topology()->version())},
      {"expected-coordinate-source-revision",
       std::to_string(object->coordinate_source_revision)},
      {"unit", coordinate_unit}};
  operation::TaskContext context;
  const auto edited = actions_.trigger(action, context);
  if (!edited.succeeded()) {
    setStatus(QStringLiteral("Coordinate edit failed: ") +
                  outcome_error(edited),
              atom_count_, primitive_count_);
    return false;
  }
  if (!rebuildScenePacket()) return false;
  emit objectsChanged();
  emit analysisResultsChanged();
  setStatus(QStringLiteral("Atom position updated · undo available"),
            atom_count_, primitive_count_);
  return true;
}

bool MolecularViewport::editAtomProperties(
    qulonglong atom_id, const QString &name, const QString &atomic_number,
    const QString &formal_charge) {
  const auto *object = workspace_->active_object();
  if (object == nullptr || atom_id == 0U ||
      (name.trimmed().isEmpty() && atomic_number.trimmed().isEmpty() &&
       formal_charge.trimmed().isEmpty())) {
    setStatus(QStringLiteral("Invalid atom property edit"), atom_count_,
              primitive_count_);
    return false;
  }
  gui::Action action;
  action.command_name = "edit atom-properties";
  action.parameters = {
      {"atom-id", std::to_string(atom_id)},
      {"expected-topology-version",
       std::to_string(object->system->topology()->version())},
      {"expected-coordinate-source-revision",
       std::to_string(object->coordinate_source_revision)}};
  if (!name.trimmed().isEmpty())
    action.parameters.emplace("name", name.trimmed().toStdString());
  if (!atomic_number.trimmed().isEmpty())
    action.parameters.emplace("atomic-number",
                              atomic_number.trimmed().toStdString());
  if (!formal_charge.trimmed().isEmpty())
    action.parameters.emplace("formal-charge",
                              formal_charge.trimmed().toStdString());
  operation::TaskContext context;
  const auto edited = actions_.trigger(action, context);
  if (!edited.succeeded()) {
    setStatus(QStringLiteral("Atom property edit failed: ") +
                  outcome_error(edited),
              atom_count_, primitive_count_);
    return false;
  }
  if (!rebuildScenePacket()) return false;
  emit objectsChanged();
  emit analysisResultsChanged();
  setStatus(QStringLiteral("Atom properties updated · undo available"),
            atom_count_, primitive_count_);
  return true;
}

bool MolecularViewport::editResidueProperties(
    qulonglong atom_id, const QString &name, const QString &chain,
    const QString &residue_number) {
  const auto *object = workspace_->active_object();
  if (object == nullptr || atom_id == 0U ||
      (name.trimmed().isEmpty() && chain.trimmed().isEmpty() &&
       residue_number.trimmed().isEmpty())) {
    setStatus(QStringLiteral("Invalid residue property edit"), atom_count_,
              primitive_count_);
    return false;
  }
  gui::Action action;
  action.command_name = "edit residue-properties";
  action.parameters = {
      {"atom-id", std::to_string(atom_id)},
      {"expected-topology-version",
       std::to_string(object->system->topology()->version())},
      {"expected-coordinate-source-revision",
       std::to_string(object->coordinate_source_revision)}};
  if (!name.trimmed().isEmpty())
    action.parameters.emplace("name", name.trimmed().toStdString());
  if (!chain.trimmed().isEmpty())
    action.parameters.emplace("chain", chain.trimmed().toStdString());
  if (!residue_number.trimmed().isEmpty())
    action.parameters.emplace("residue-number",
                              residue_number.trimmed().toStdString());
  operation::TaskContext context;
  const auto edited = actions_.trigger(action, context);
  if (!edited.succeeded()) {
    setStatus(QStringLiteral("Residue property edit failed: ") +
                  outcome_error(edited),
              atom_count_, primitive_count_);
    return false;
  }
  if (!rebuildScenePacket()) return false;
  emit objectsChanged();
  emit analysisResultsChanged();
  setStatus(QStringLiteral("Residue properties updated · undo available"),
            atom_count_, primitive_count_);
  return true;
}

bool MolecularViewport::editBondOrder(qulonglong bond_id,
                                      const QString &order) {
  const auto *object = workspace_->active_object();
  if (object == nullptr || bond_id == 0U || order.trimmed().isEmpty()) {
    setStatus(QStringLiteral("Invalid bond order edit"), atom_count_,
              primitive_count_);
    return false;
  }
  gui::Action action;
  action.command_name = "edit bond-order";
  action.parameters = {
      {"bond-id", std::to_string(bond_id)},
      {"order", order.trimmed().toLower().toStdString()},
      {"expected-topology-version",
       std::to_string(object->system->topology()->version())},
      {"expected-coordinate-source-revision",
       std::to_string(object->coordinate_source_revision)}};
  operation::TaskContext context;
  const auto edited = actions_.trigger(action, context);
  if (!edited.succeeded()) {
    setStatus(QStringLiteral("Bond order edit failed: ") +
                  outcome_error(edited),
              atom_count_, primitive_count_);
    return false;
  }
  if (!rebuildScenePacket()) return false;
  emit objectsChanged();
  emit analysisResultsChanged();
  setStatus(QStringLiteral("Bond order updated · undo available"), atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::undoEdit() {
  operation::TaskContext context;
  const auto undone =
      actions_.trigger(gui::Action{"edit undo", {}}, context);
  if (!undone.succeeded()) {
    setStatus(QStringLiteral("Undo failed: ") + outcome_error(undone),
              atom_count_, primitive_count_);
    return false;
  }
  if (workspace_->active_object() == nullptr &&
      workspace_->active_volume() == nullptr) {
    atom_count_ = 0U;
    primitive_count_ = 0U;
    setRenderPacket({});
  } else if (workspace_->active_object() != nullptr &&
             workspace_->active_object()->representations.empty()) {
    if (!rebuildRepresentation()) return false;
  } else if (!rebuildScenePacket()) {
    return false;
  }
  emit objectsChanged();
  emit analysisResultsChanged();
  setStatus(QStringLiteral("Molecular edit undone"), atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::redoEdit() {
  operation::TaskContext context;
  const auto redone =
      actions_.trigger(gui::Action{"edit redo", {}}, context);
  if (!redone.succeeded()) {
    setStatus(QStringLiteral("Redo failed: ") + outcome_error(redone),
              atom_count_, primitive_count_);
    return false;
  }
  if (workspace_->active_object() != nullptr &&
      workspace_->active_object()->representations.empty()) {
    if (!rebuildRepresentation()) return false;
  } else if (!rebuildScenePacket()) {
    return false;
  }
  emit objectsChanged();
  emit analysisResultsChanged();
  setStatus(QStringLiteral("Molecular edit redone"), atom_count_,
            primitive_count_);
  return true;
}

QString MolecularViewport::editHistoryJson() const {
  operation::TaskContext context;
  const auto outcome =
      actions_.trigger(gui::Action{"edit history", {}}, context);
  const auto rendered =
      command::render(outcome.envelope, operation::OutputFormat::json);
  return rendered.has_value() ? QString::fromStdString(rendered.value())
                              : QString{};
}

bool MolecularViewport::buildMolecule(
    const QString &name, const QString &atoms, const QString &bonds,
    const QString &residue_name, const QString &chain,
    qlonglong residue_number, const QString &unit,
    qulonglong memory_budget_bytes) {
  if (name.trimmed().isEmpty() || atoms.trimmed().isEmpty() ||
      residue_name.trimmed().isEmpty() || memory_budget_bytes == 0U) {
    setStatus(QStringLiteral("Invalid molecule builder input"), atom_count_,
              primitive_count_);
    return false;
  }
  gui::Action action;
  action.command_name = "build molecule";
  action.parameters = {
      {"name", name.trimmed().toStdString()},
      {"atoms", atoms.trimmed().toStdString()},
      {"bonds", bonds.trimmed().isEmpty() ? "none"
                                          : bonds.trimmed().toStdString()},
      {"residue-name", residue_name.trimmed().toStdString()},
      {"chain", chain.trimmed().toStdString()},
      {"residue-number", std::to_string(residue_number)},
      {"unit", unit.trimmed().toLower().toStdString()},
      {"memory-budget-bytes", std::to_string(memory_budget_bytes)}};
  operation::TaskContext context;
  const auto built = actions_.trigger(action, context);
  if (!built.succeeded()) {
    setStatus(QStringLiteral("Molecule builder failed: ") +
                  outcome_error(built),
              atom_count_, primitive_count_);
    return false;
  }
  selection_text_ = QStringLiteral("No selection");
  emit selectionChanged();
  emit objectsChanged();
  syncTrajectoryState();
  if (!rebuildRepresentation()) return false;
  setStatus(QStringLiteral("Molecular fragment built · ") + name.trimmed(),
            atom_count_, primitive_count_);
  return true;
}

bool MolecularViewport::setCameraThroughAction(const scene::Camera &camera) {
  cancelCameraAnimation();
  const auto &parameters = camera.parameters();
  command::Arguments arguments{
      {"aspect-ratio", number_text(parameters.aspect_ratio)},
      {"distance", number_text(parameters.distance)},
      {"far-clip", number_text(parameters.far_clip)},
      {"field-of-view",
       number_text(parameters.vertical_field_of_view_radians)},
      {"near-clip", number_text(parameters.near_clip)},
      {"model-origin-x", number_text(parameters.model_origin.x)},
      {"model-origin-y", number_text(parameters.model_origin.y)},
      {"model-origin-z", number_text(parameters.model_origin.z)},
      {"orientation-w", number_text(parameters.orientation.w)},
      {"orientation-x", number_text(parameters.orientation.x)},
      {"orientation-y", number_text(parameters.orientation.y)},
      {"orientation-z", number_text(parameters.orientation.z)},
      {"orthographic-height", number_text(parameters.orthographic_height)},
      {"projection",
       parameters.projection == scene::ProjectionMode::orthographic
           ? "orthographic"
           : "perspective"},
      {"target-x", number_text(parameters.target.x)},
      {"target-y", number_text(parameters.target.y)},
      {"target-z", number_text(parameters.target.z)}};
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"view set", std::move(arguments)}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Camera update failed: ") +
                  outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  syncCameraState();
  return true;
}

bool MolecularViewport::invokeCameraAction(
    std::string command_name,
    std::map<std::string, std::string, std::less<>> parameters,
    double duration_seconds, int hand, QString success_status,
    QString failure_prefix) {
  const auto start = camera_;
  parameters.emplace("duration", number_text(duration_seconds));
  parameters.emplace("hand", std::to_string(hand));
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{std::move(command_name), std::move(parameters)}, context);
  if (!outcome.succeeded()) {
    setStatus(std::move(failure_prefix) + QStringLiteral(": ") +
                  outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  if (start.has_value() && duration_seconds > 0.0) {
    startCameraAnimation(start.value(), workspace_->camera(), duration_seconds,
                         hand);
  } else {
    syncCameraState();
  }
  setStatus(std::move(success_status), atom_count_, primitive_count_);
  return true;
}

void MolecularViewport::syncCameraState() {
  cancelCameraAnimation();
  camera_ = workspace_->camera();
  ++camera_revision_;
  emit analysisResultsChanged();
  update();
}

void MolecularViewport::cancelCameraAnimation() {
  camera_animation_timer_.stop();
  camera_animation_start_.reset();
  camera_animation_end_.reset();
  camera_animation_duration_seconds_ = 0.0;
}

void MolecularViewport::startCameraAnimation(const scene::Camera &start,
                                             const scene::Camera &end,
                                             double duration_seconds,
                                             int hand) {
  cancelCameraAnimation();
  camera_animation_start_ = start;
  camera_animation_end_ = end;
  camera_animation_duration_seconds_ = duration_seconds;
  camera_animation_hand_ = hand;
  camera_ = start;
  ++camera_revision_;
  emit analysisResultsChanged();
  update();
  camera_animation_elapsed_.start();
  camera_animation_timer_.start();
}

void MolecularViewport::onCameraAnimationTick() {
  if (!camera_animation_start_.has_value() ||
      !camera_animation_end_.has_value() ||
      !(camera_animation_duration_seconds_ > 0.0)) {
    syncCameraState();
    return;
  }
  const auto elapsed_seconds =
      static_cast<double>(camera_animation_elapsed_.elapsed()) / 1000.0;
  const auto fraction =
      std::clamp(elapsed_seconds / camera_animation_duration_seconds_, 0.0,
                 1.0);
  if (fraction >= 1.0) {
    syncCameraState();
    return;
  }
  const auto interpolated = scene::interpolate_pymol_camera(
      camera_animation_start_.value(), camera_animation_end_.value(),
      fraction, camera_animation_hand_);
  if (!interpolated.has_value()) {
    syncCameraState();
    setStatus(QStringLiteral("Camera animation failed: ") +
                  QString::fromStdString(interpolated.error().message),
              atom_count_, primitive_count_);
    return;
  }
  camera_ = interpolated.value();
  ++camera_revision_;
  emit analysisResultsChanged();
  update();
}

void MolecularViewport::orbit(double delta_x, double delta_y) {
  if (!camera_.has_value())
    return;
  const auto updated = camera_->orbit_pixels(delta_x, delta_y);
  if (!updated.has_value())
    return;
  static_cast<void>(setCameraThroughAction(updated.value()));
}

void MolecularViewport::pan(double delta_x, double delta_y) {
  if (!camera_.has_value() || height() <= 0.0)
    return;
  const auto updated = camera_->pan_pixels(delta_x, delta_y, height());
  if (!updated.has_value())
    return;
  static_cast<void>(setCameraThroughAction(updated.value()));
}

void MolecularViewport::dolly(double delta) {
  if (!camera_.has_value())
    return;
  const auto updated = camera_->dolly(delta);
  if (!updated.has_value())
    return;
  static_cast<void>(setCameraThroughAction(updated.value()));
}

void MolecularViewport::resetView() {
  static_cast<void>(resetViewAnimated(0.0, 1));
}

bool MolecularViewport::resetViewAnimated(double duration_seconds, int hand) {
  return invokeCameraAction("view reset", {}, duration_seconds, hand,
                            QStringLiteral("Reset camera to visible scene"),
                            QStringLiteral("Reset view failed"));
}

bool MolecularViewport::centerSelection(const QString &selection,
                                        bool move_origin,
                                        const QString &state,
                                        double duration_seconds, int hand) {
  const auto expression = selection.trimmed().isEmpty()
                              ? QStringLiteral("all")
                              : selection.trimmed();
  const auto state_scope =
      state.trimmed().isEmpty() ? QStringLiteral("current") : state.trimmed();
  return invokeCameraAction(
      "view center",
      {{"move-origin", move_origin ? "true" : "false"},
       {"selection", expression.toStdString()},
       {"state", state_scope.toStdString()}},
      duration_seconds, hand,
      QStringLiteral("Centered camera · ") + expression,
      QStringLiteral("Center selection failed"));
}

bool MolecularViewport::zoomSelection(const QString &selection, double buffer,
                                      bool complete,
                                      const QString &state,
                                      double duration_seconds, int hand) {
  const auto expression = selection.trimmed().isEmpty()
                              ? QStringLiteral("all")
                              : selection.trimmed();
  const auto state_scope =
      state.trimmed().isEmpty() ? QStringLiteral("current") : state.trimmed();
  return invokeCameraAction(
      "view zoom",
      {{"buffer", number_text(buffer)},
       {"complete", complete ? "true" : "false"},
       {"selection", expression.toStdString()},
       {"state", state_scope.toStdString()}},
      duration_seconds, hand,
      QStringLiteral("Fit selection · ") + expression,
      QStringLiteral("Fit selection failed"));
}

bool MolecularViewport::orientSelection(const QString &selection,
                                        const QString &state,
                                        double duration_seconds, int hand) {
  const auto expression = selection.trimmed().isEmpty()
                              ? QStringLiteral("all")
                              : selection.trimmed();
  const auto state_scope =
      state.trimmed().isEmpty() ? QStringLiteral("current") : state.trimmed();
  return invokeCameraAction(
      "view orient",
      {{"selection", expression.toStdString()},
       {"state", state_scope.toStdString()}},
      duration_seconds, hand,
      QStringLiteral("Oriented principal axes · ") + expression,
      QStringLiteral("Orient selection failed"));
}

bool MolecularViewport::setOriginSelection(const QString &selection,
                                            const QString &state) {
  const auto expression = selection.trimmed().isEmpty()
                              ? QStringLiteral("all")
                              : selection.trimmed();
  const auto state_scope =
      state.trimmed().isEmpty() ? QStringLiteral("current") : state.trimmed();
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"view origin",
                  {{"selection", expression.toStdString()},
                   {"state", state_scope.toStdString()}}},
      context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Set pivot failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  syncCameraState();
  setStatus(QStringLiteral("Set rotation pivot · ") + expression, atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::setOriginPosition(double x, double y, double z,
                                          const QString &object_reference) {
  command::Arguments arguments{{"position", number_text(x) + "," +
                                                number_text(y) + "," +
                                                number_text(z)}};
  const auto object = object_reference.trimmed();
  if (!object.isEmpty())
    arguments.emplace("object", object.toStdString());
  operation::TaskContext context;
  const auto outcome =
      actions_.trigger(gui::Action{"view origin", std::move(arguments)}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Set coordinate pivot failed: ") +
                  outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  syncCameraState();
  setStatus(object.isEmpty() ? QStringLiteral("Set camera coordinate pivot")
                             : QStringLiteral("Set object coordinate pivot · ") +
                                   object,
            atom_count_, primitive_count_);
  return true;
}

bool MolecularViewport::setObjectOriginSelection(
    const QString &object_reference, const QString &selection,
    const QString &state) {
  const auto object = object_reference.trimmed().isEmpty()
                          ? QStringLiteral("current")
                          : object_reference.trimmed();
  const auto expression = selection.trimmed().isEmpty()
                              ? QStringLiteral("all")
                              : selection.trimmed();
  const auto state_scope =
      state.trimmed().isEmpty() ? QStringLiteral("current") : state.trimmed();
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"view origin",
                  {{"object", object.toStdString()},
                   {"selection", expression.toStdString()},
                   {"state", state_scope.toStdString()}}},
      context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Set object pivot failed: ") +
                  outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  setStatus(QStringLiteral("Set object pivot · ") + object, atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::resetObjectTransform(
    const QString &object_reference) {
  const auto object = object_reference.trimmed().isEmpty()
                          ? QStringLiteral("current")
                          : object_reference.trimmed();
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"view reset", {{"object", object.toStdString()}}}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Reset object transform failed: ") +
                  outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  setStatus(QStringLiteral("Reset object transform · ") + object, atom_count_,
            primitive_count_);
  return true;
}

bool MolecularViewport::clipCamera(const QString &mode, double distance,
                                   const QString &selection,
                                   const QString &state) {
  const auto normalized_mode = mode.trimmed().toLower();
  command::Arguments arguments{{"distance", number_text(distance)},
                               {"mode", normalized_mode.toStdString()}};
  const auto expression = selection.trimmed();
  if (!expression.isEmpty())
    arguments.emplace("selection", expression.toStdString());
  const auto state_scope =
      state.trimmed().isEmpty() ? QStringLiteral("current") : state.trimmed();
  arguments.emplace("state", state_scope.toStdString());
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"view clip", std::move(arguments)}, context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Clipping failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  syncCameraState();
  setStatus(QStringLiteral("Updated clipping · ") + normalized_mode,
            atom_count_, primitive_count_);
  return true;
}

QString MolecularViewport::clipRangeText() const {
  operation::TaskContext context;
  const auto outcome =
      actions_.trigger(gui::Action{"view get-clip", {}}, context);
  if (!outcome.succeeded())
    return QStringLiteral("unavailable");
  const auto &response = std::get<command::Response>(outcome.envelope.payload);
  const auto near_found = response.fields.find("near_clip");
  const auto far_found = response.fields.find("far_clip");
  if (near_found == response.fields.end() || far_found == response.fields.end())
    return QStringLiteral("unavailable");
  const auto *near_value = std::get_if<double>(&near_found->second.data);
  const auto *far_value = std::get_if<double>(&far_found->second.data);
  if (near_value == nullptr || far_value == nullptr)
    return QStringLiteral("unavailable");
  return QStringLiteral("near %1 Å · far %2 Å")
      .arg(*near_value, 0, 'g', 6)
      .arg(*far_value, 0, 'g', 6);
}

bool MolecularViewport::moveCamera(const QString &axis, double distance) {
  const auto normalized_axis = axis.trimmed().toLower();
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"view move",
                  {{"axis", normalized_axis.toStdString()},
                   {"distance", number_text(distance)}}},
      context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Camera move failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  syncCameraState();
  setStatus(QStringLiteral("Moved camera on ") + normalized_axis,
            atom_count_, primitive_count_);
  return true;
}

bool MolecularViewport::turnCamera(const QString &axis,
                                   double angle_degrees) {
  const auto normalized_axis = axis.trimmed().toLower();
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"view turn",
                  {{"angle", number_text(angle_degrees)},
                   {"axis", normalized_axis.toStdString()}}},
      context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Camera turn failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  syncCameraState();
  setStatus(QStringLiteral("Turned camera on ") + normalized_axis,
            atom_count_, primitive_count_);
  return true;
}

bool MolecularViewport::setProjection(const QString &mode,
                                      double field_of_view_degrees,
                                      bool preserve_scale) {
  const auto normalized_mode = mode.trimmed().toLower();
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{
          "view projection",
          {{"field-of-view-degrees", number_text(field_of_view_degrees)},
           {"mode", normalized_mode.toStdString()},
           {"preserve-scale", preserve_scale ? "true" : "false"}}},
      context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Projection failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  syncCameraState();
  setStatus(QStringLiteral("Projection · ") + normalized_mode, atom_count_,
            primitive_count_);
  return true;
}

QString MolecularViewport::projectionModeText() const {
  return camera_.has_value() &&
                 camera_->parameters().projection ==
                     scene::ProjectionMode::orthographic
             ? QStringLiteral("orthographic")
             : QStringLiteral("perspective");
}

double MolecularViewport::fieldOfViewDegrees() const {
  return camera_.has_value()
             ? camera_->parameters().vertical_field_of_view_radians * 180.0 /
                   std::numbers::pi
             : 45.0;
}

bool MolecularViewport::setStereo(bool enabled, const QString &mode,
                                  bool swap_eyes, double shift_percent,
                                  double angle_scale,
                                  const QString &anaglyph_mode) {
  operation::TaskContext context;
  const auto outcome = actions_.trigger(
      gui::Action{"stereo set",
                  {{"angle-scale", number_text(angle_scale)},
                   {"anaglyph-mode",
                    anaglyph_mode.trimmed().toLower().toStdString()},
                   {"enabled", enabled ? "true" : "false"},
                   {"mode", mode.trimmed().toLower().toStdString()},
                   {"shift-percent", number_text(shift_percent)},
                   {"swap-eyes", swap_eyes ? "true" : "false"}}},
      context);
  if (!outcome.succeeded()) {
    setStatus(QStringLiteral("Stereo failed: ") + outcome_error(outcome),
              atom_count_, primitive_count_);
    return false;
  }
  ++stereo_revision_;
  update();
  setStatus(enabled ? QStringLiteral("Stereo · ") + mode
                    : QStringLiteral("Stereo disabled"),
            atom_count_, primitive_count_);
  return true;
}

bool MolecularViewport::stereoEnabled() const noexcept {
  return workspace_->stereo().enabled;
}

QString MolecularViewport::stereoModeText() const {
  const auto mode = scene::to_string(workspace_->stereo().mode);
  return QString::fromUtf8(mode.data(), static_cast<qsizetype>(mode.size()));
}

bool MolecularViewport::stereoSwapEyes() const noexcept {
  return workspace_->stereo().swap_eyes;
}

double MolecularViewport::stereoShiftPercent() const noexcept {
  return workspace_->stereo().shift_percent;
}

double MolecularViewport::stereoAngleScale() const noexcept {
  return workspace_->stereo().angle_scale;
}

QString MolecularViewport::anaglyphModeText() const {
  const auto mode = scene::to_string(workspace_->stereo().anaglyph_mode);
  return QString::fromUtf8(mode.data(), static_cast<qsizetype>(mode.size()));
}

void MolecularViewport::pickAt(double x, double y) {
  if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || y < 0.0 ||
      x >= width() || y >= height()) {
    return;
  }
  pick_position_ = QPointF{x, y};
  ++pick_request_revision_;
  update();
  for (const auto delay : {16, 32, 64, 128}) {
    QTimer::singleShot(delay, this, [this] {
      update();
      if (window() != nullptr)
        window()->update();
    });
  }
}

bool MolecularViewport::selectAll() {
  return defineSelection(QStringLiteral("all_atoms"), QStringLiteral("all"),
                         false);
}

bool MolecularViewport::defineSelection(const QString &name,
                                        const QString &expression,
                                        bool dynamic) {
  const auto normalized_name = name.trimmed();
  const auto normalized_expression = expression.trimmed();
  if (normalized_name.isEmpty() || normalized_expression.isEmpty()) {
    setStatus(QStringLiteral("Selection name and expression are required"),
              atom_count_, primitive_count_);
    return false;
  }
  gui::Action action;
  action.command_name = "select";
  action.parameters.emplace("name", normalized_name.toStdString());
  action.parameters.emplace("expression", normalized_expression.toStdString());
  action.parameters.emplace("update", dynamic ? "true" : "false");
  operation::TaskContext context;
  const auto selected = actions_.trigger(action, context);
  if (!selected.succeeded()) {
    setStatus(QStringLiteral("Selection failed: ") + outcome_error(selected),
              atom_count_, primitive_count_);
    return false;
  }
  selection_text_ = normalized_name == QStringLiteral("all_atoms") &&
                            normalized_expression == QStringLiteral("all")
                        ? QStringLiteral("All atoms selected")
                        : QStringLiteral("Selection defined · ") +
                              normalized_name + QStringLiteral(" · ") +
                              normalized_expression;
  emit selectionChanged();
  return true;
}

void MolecularViewport::deliverPickResult(std::uint64_t request_revision,
                                          std::uint64_t packet_revision,
                                          std::uint64_t pick_id) {
  if (request_revision != pick_request_revision_ ||
      packet_revision != packet_revision_) {
    return;
  }
  last_pick_completion_revision_ = request_revision;
  last_pick_id_ = pick_id;
  const auto found = packet_.pick_targets.find(pick_id);
  const std::optional<render::PickTarget> target =
      found == packet_.pick_targets.end()
          ? std::nullopt
          : std::optional<render::PickTarget>{found->second};
  std::vector<std::size_t> atom_indices;
  QString description{QStringLiteral("No molecular target at cursor")};
  QString kind{QStringLiteral("none")};
  if (target.has_value() && target->kind == render::PickKind::volume) {
    const auto found_volume = std::find_if(
        workspace_->volumes().begin(), workspace_->volumes().end(),
        [&target](const auto &volume) {
          return volume.scene_node.value == target->scene_node_id;
        });
    if (found_volume != workspace_->volumes().end()) {
      kind = QStringLiteral("volume");
      description = QStringLiteral("Volume %1").arg(
          QString::fromStdString(found_volume->name));
      if (target->volume_sample.has_value()) {
        const auto &sample = *target->volume_sample;
        description += QStringLiteral(" · sample %1, %2, %3")
                           .arg(static_cast<qulonglong>(sample[0]))
                           .arg(static_cast<qulonglong>(sample[1]))
                           .arg(static_cast<qulonglong>(sample[2]));
      }
    }
  }
  const auto *object =
      target.has_value()
          ? workspace_->object_by_scene_node(target->scene_node_id)
          : workspace_->active_object();
  bool activated_by_pick{};
  if (object != nullptr && workspace_->active_object() != nullptr &&
      object->id != workspace_->active_object()->id) {
    gui::Action activation;
    activation.command_name = "object activate";
    activation.parameters.emplace("id", std::to_string(object->id));
    operation::TaskContext activation_context;
    const auto activated = actions_.trigger(activation, activation_context);
    if (!activated.succeeded()) {
      selection_text_ =
          QStringLiteral("Pick activation failed: ") + outcome_error(activated);
      emit selectionChanged();
      return;
    }
    object = workspace_->active_object();
    activated_by_pick = true;
  }
  const model::Topology *topology =
      object == nullptr ? nullptr : object->system->topology().get();
  if (target.has_value() && topology != nullptr &&
      target->scene_node_id == object->scene_node.value) {
    if (target->kind == render::PickKind::atom && target->atom.has_value() &&
        target->atom->value < topology->atom_count()) {
      const auto index = target->atom->value;
      const auto &atom = topology->atoms()[index];
      const auto &residue = topology->residues()[atom.residue.value];
      atom_indices.push_back(index);
      kind = QStringLiteral("atom");
      description = QStringLiteral("Atom %1 · %2 · %3 %4%5")
                        .arg(static_cast<qulonglong>(index + 1U))
                        .arg(QString::fromStdString(atom.name))
                        .arg(QString::fromStdString(residue.name))
                        .arg(QString::fromStdString(residue.chain_id))
                        .arg(residue.sequence_number);
    } else if (target->kind == render::PickKind::bond &&
               target->bond_index.has_value() &&
               *target->bond_index < topology->bonds().size()) {
      const auto &bond = topology->bonds()[*target->bond_index];
      atom_indices = {bond.first.value, bond.second.value};
      kind = QStringLiteral("bond");
      description = QStringLiteral("Bond %1 · atoms %2–%3")
                        .arg(static_cast<qulonglong>(*target->bond_index + 1U))
                        .arg(static_cast<qulonglong>(bond.first.value + 1U))
                        .arg(static_cast<qulonglong>(bond.second.value + 1U));
    } else if (target->kind == render::PickKind::residue &&
               target->residue.has_value() &&
               target->residue->value < topology->residue_count()) {
      const auto residue_index = target->residue->value;
      const auto &residue = topology->residues()[residue_index];
      for (std::size_t index = 0; index < topology->atom_count(); ++index) {
        if (topology->atoms()[index].residue.value == residue_index) {
          atom_indices.push_back(index);
        }
      }
      kind = QStringLiteral("residue");
      description = QStringLiteral("Residue %1 · %2 %3%4")
                        .arg(static_cast<qulonglong>(residue_index + 1U))
                        .arg(QString::fromStdString(residue.name))
                        .arg(QString::fromStdString(residue.chain_id))
                        .arg(residue.sequence_number);
    }
  }

  if (object != nullptr) {
    std::string expression = "none";
    if (!atom_indices.empty()) {
      expression.clear();
      for (const auto index : atom_indices) {
        if (!expression.empty())
          expression += " or ";
        expression += "index " + std::to_string(index + 1U);
      }
    }
    gui::Action action;
    action.command_name = "select";
    action.parameters.emplace("name", "picked");
    action.parameters.emplace("expression", std::move(expression));
    action.parameters.emplace("update", "false");
    operation::TaskContext context;
    const auto selected = actions_.trigger(action, context);
    if (!selected.succeeded()) {
      description =
          QStringLiteral("Pick selection failed: ") + outcome_error(selected);
      kind = QStringLiteral("error");
    }
  }
  if (activated_by_pick) {
    syncActiveRepresentationName();
    static_cast<void>(rebuildScenePacket());
  }
  selection_text_ = std::move(description);
  emit selectionChanged();
  qInfo("MolShredder GPU pick ready: kind=%s id=%llu selection=%s",
        kind.toUtf8().constData(), static_cast<unsigned long long>(pick_id),
        selection_text_.toUtf8().constData());
}

void MolecularViewport::deliverDirectVolumeGpuStatus(
    const std::shared_ptr<const render::DirectVolumeData> &source,
    QString state, QString message) {
  if (directVolumeLease() != source) return;
  if (volume_gpu_state_ == state && volume_gpu_message_ == message) return;
  volume_gpu_state_ = std::move(state);
  volume_gpu_message_ = std::move(message);
  emit volumeGpuStatusChanged();
  if (volume_gpu_state_ == QStringLiteral("unavailable") ||
      volume_gpu_state_ == QStringLiteral("failed") ||
      volume_gpu_state_ == QStringLiteral("degraded")) {
    setStatus(volume_gpu_message_, atom_count_, primitive_count_);
  }
}

bool MolecularViewport::rebuildRepresentation() {
  static const std::map<QString, render::RepresentationKind> kinds{
      {QStringLiteral("lines"), render::RepresentationKind::lines},
      {QStringLiteral("sticks"), render::RepresentationKind::sticks},
      {QStringLiteral("spheres"), render::RepresentationKind::spheres},
      {QStringLiteral("ribbon"), render::RepresentationKind::ribbon},
      {QStringLiteral("cartoon"), render::RepresentationKind::cartoon}};
  const auto found = kinds.find(representation_);
  if (found == kinds.end())
    return false;
  gui::Action action;
  action.command_name = "show";
  action.parameters.emplace("representation", representation_.toStdString());
  action.parameters.emplace("selection", "all");
  action.parameters.emplace("replace", "true");
  operation::TaskContext context;
  const auto shown = actions_.trigger(action, context);
  if (!shown.succeeded()) {
    setStatus(QStringLiteral("Representation failed: ") + outcome_error(shown),
              atom_count_, primitive_count_);
    return false;
  }
  const auto *object = workspace_->active_object();
  if (object == nullptr || object->representations.empty()) {
    setStatus(QStringLiteral("Representation produced no packet"), atom_count_,
              0U);
    return false;
  }
  if (!rebuildScenePacket())
    return false;
  qInfo("MolShredder desktop representation: atoms=%llu representation=%s "
        "primitives=%llu",
        static_cast<unsigned long long>(atom_count_),
        representation_.toUtf8().constData(),
        static_cast<unsigned long long>(primitive_count_));
  return true;
}

bool MolecularViewport::setVolumeIsosurface(double level) {
  if (!std::isfinite(level) || workspace_->active_volume() == nullptr) {
    setStatus(QStringLiteral("Open a scalar volume and provide a finite contour level"),
              atom_count_, primitive_count_);
    return false;
  }
  gui::Action action;
  action.command_name = "volume isosurface";
  action.parameters.emplace("level", std::to_string(level));
  action.parameters.emplace("color", "cyan");
  action.parameters.emplace("opacity", "0.72");
  action.parameters.emplace("replace", "true");
  operation::TaskContext context;
  const auto shown = actions_.trigger(action, context);
  if (!shown.succeeded()) {
    setStatus(QStringLiteral("Isosurface failed: ") + outcome_error(shown),
              atom_count_, primitive_count_);
    return false;
  }
  volume_level_ = level;
  volume_mode_ = QStringLiteral("isosurface");
  has_volume_ = true;
  emit volumeChanged();
  if (!rebuildScenePacket()) return false;
  const auto *volume = workspace_->active_volume();
  const auto vertices = volume == nullptr || volume->representations.empty()
                            ? 0U
                            : volume->representations.back().mesh_vertices.size();
  const auto triangles = volume == nullptr || volume->representations.empty()
                             ? 0U
                             : volume->representations.back().mesh_triangles.size();
  qInfo("MolShredder desktop isosurface ready: level=%.6g vertices=%llu triangles=%llu",
        level, static_cast<unsigned long long>(vertices),
        static_cast<unsigned long long>(triangles));
  return true;
}

bool MolecularViewport::setVolumeSlice(const QString &axis, qulonglong index) {
  const auto *volume = workspace_->active_volume();
  const auto normalized_axis = axis.trimmed().toLower();
  if (volume == nullptr || (normalized_axis != QStringLiteral("x") &&
                            normalized_axis != QStringLiteral("y") &&
                            normalized_axis != QStringLiteral("z"))) {
    setStatus(QStringLiteral("Open a scalar volume and choose the X, Y, or Z slice axis"),
              atom_count_, primitive_count_);
    return false;
  }
  const auto shape = volume->grid->shape();
  const auto extent = normalized_axis == QStringLiteral("x")
                          ? shape.x
                          : normalized_axis == QStringLiteral("y") ? shape.y
                                                                    : shape.z;
  if (normalized_axis != volume_slice_axis_)
    index = static_cast<qulonglong>((extent - 1U) / 2U);
  if (index >= extent) {
    setStatus(QStringLiteral("Slice index is outside the selected volume axis"),
              atom_count_, primitive_count_);
    return false;
  }
  gui::Action action;
  action.command_name = "volume slice";
  action.parameters.emplace("axis", normalized_axis.toStdString());
  action.parameters.emplace("index", std::to_string(index));
  action.parameters.emplace("minimum-color", "blue");
  action.parameters.emplace("maximum-color", "orange");
  action.parameters.emplace("opacity", "0.78");
  action.parameters.emplace("memory-budget-bytes", "67108864");
  action.parameters.emplace("replace", "true");
  operation::TaskContext context;
  const auto shown = actions_.trigger(action, context);
  if (!shown.succeeded()) {
    setStatus(QStringLiteral("Volume slice failed: ") + outcome_error(shown),
              atom_count_, primitive_count_);
    return false;
  }
  volume_mode_ = QStringLiteral("slice");
  volume_slice_axis_ = normalized_axis;
  volume_slice_index_ = index;
  volume_slice_maximum_ = static_cast<qulonglong>(extent - 1U);
  has_volume_ = true;
  emit volumeChanged();
  if (!rebuildScenePacket())
    return false;
  const auto *active_volume = workspace_->active_volume();
  const auto vertices =
      active_volume == nullptr || active_volume->representations.empty()
          ? 0U
          : active_volume->representations.back().mesh_vertices.size();
  const auto triangles =
      active_volume == nullptr || active_volume->representations.empty()
          ? 0U
          : active_volume->representations.back().mesh_triangles.size();
  qInfo("MolShredder desktop volume slice ready: axis=%s index=%llu vertices=%llu triangles=%llu",
        normalized_axis.toUtf8().constData(),
        static_cast<unsigned long long>(index),
        static_cast<unsigned long long>(vertices),
        static_cast<unsigned long long>(triangles));
  return true;
}

bool MolecularViewport::setDirectVolume(
    const QString &preset, double sampling_step, qulonglong maximum_steps,
    qulonglong lookup_table_samples, qulonglong texture_budget_bytes) {
  const auto normalized = preset.trimmed().toLower();
  const auto valid_preset =
      normalized == QStringLiteral("density") ||
      normalized == QStringLiteral("fire") ||
      normalized == QStringLiteral("grayscale") ||
      normalized == QStringLiteral("ice") ||
      normalized == QStringLiteral("spectrum");
  if (workspace_->active_volume() == nullptr || !valid_preset ||
      !std::isfinite(sampling_step) || sampling_step <= 0.0 ||
      maximum_steps == 0U || lookup_table_samples < 2U ||
      texture_budget_bytes == 0U) {
    setStatus(QStringLiteral("Open a scalar volume and provide valid direct-volume parameters"),
              atom_count_, primitive_count_);
    return false;
  }
  if (pending_direct_volume_.has_value()) {
    static_cast<void>(volume_task_scheduler_->cancel(
        pending_direct_volume_->task_id));
    pending_direct_volume_.reset();
  }
  const auto generation = volume_task_generation_->fetch_add(1U) + 1U;
  application::ScheduledDirectVolumeRequest request;
  request.style.mode = render::VolumeClassificationMode::post_classified;
  request.style.sampling_step = sampling_step;
  request.style.maximum_steps = static_cast<std::size_t>(maximum_steps);
  request.style.lookup_table_samples =
      static_cast<std::size_t>(lookup_table_samples);
  request.style.texture_budget_bytes =
      static_cast<std::size_t>(texture_budget_bytes);
  request.preset = normalized == QStringLiteral("fire")
                       ? render::TransferPreset::fire
                   : normalized == QStringLiteral("grayscale")
                       ? render::TransferPreset::grayscale
                   : normalized == QStringLiteral("ice")
                       ? render::TransferPreset::ice
                   : normalized == QStringLiteral("spectrum")
                       ? render::TransferPreset::spectrum
                       : render::TransferPreset::density;
  request.replace_existing = true;
  request.generation = generation;
  const auto generation_token = volume_task_generation_;
  request.generation_is_current = [generation_token](std::uint64_t value) {
    return value == generation_token->load();
  };
  auto scheduled = application::schedule_direct_volume(
      workspace_, volume_task_scheduler_, std::move(request));
  if (!scheduled.has_value()) {
    setStatus(QStringLiteral("Direct volume failed: ") +
                  QString::fromStdString(scheduled.error().message),
              atom_count_, primitive_count_);
    volume_task_running_ = false;
    volume_task_progress_ = 0.0;
    volume_task_stage_ = QStringLiteral("failed");
    emit volumeTaskChanged();
    return false;
  }
  pending_direct_volume_ = std::move(scheduled.value());
  volume_task_running_ = true;
  volume_task_progress_ = 0.0;
  volume_task_stage_ = QStringLiteral("queued");
  setStatus(QStringLiteral("Preparing direct volume…"), atom_count_,
            primitive_count_);
  emit volumeTaskChanged();
  volume_task_timer_.start();
  return true;
}

void MolecularViewport::cancelDirectVolumeTask() {
  const auto had_task = pending_direct_volume_.has_value() ||
                        volume_task_running_;
  if (volume_task_generation_ != nullptr) {
    volume_task_generation_->fetch_add(1U);
  }
  if (pending_direct_volume_.has_value() &&
      volume_task_scheduler_ != nullptr) {
    static_cast<void>(volume_task_scheduler_->cancel(
        pending_direct_volume_->task_id));
  }
  pending_direct_volume_.reset();
  volume_task_timer_.stop();
  volume_task_running_ = false;
  volume_task_progress_ = 0.0;
  volume_task_stage_ = QStringLiteral("cancelled");
  if (had_task) {
    setStatus(QStringLiteral("Direct-volume task cancelled"), atom_count_,
              primitive_count_);
    emit volumeTaskChanged();
  }
}

bool MolecularViewport::waitForDirectVolumeTask(int timeout_milliseconds) {
  if (!volume_task_running_) return true;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  connect(this, &MolecularViewport::volumeTaskChanged, &loop, [&] {
    if (!volume_task_running_) loop.quit();
  });
  timeout.start(std::max(timeout_milliseconds, 1));
  loop.exec();
  return !volume_task_running_ &&
         volume_task_stage_ == QStringLiteral("complete");
}

bool MolecularViewport::hideDirectVolume() {
  if (volume_task_running_) cancelDirectVolumeTask();
  gui::Action action;
  action.command_name = "volume hide";
  operation::TaskContext context;
  const auto hidden = actions_.trigger(action, context);
  if (!hidden.succeeded()) {
    setStatus(QStringLiteral("Direct volume hide failed: ") +
                  outcome_error(hidden),
              atom_count_, primitive_count_);
    return false;
  }
  volume_mode_ = QStringLiteral("isosurface");
  volume_gpu_state_ = QStringLiteral("idle");
  volume_gpu_message_ = QStringLiteral("No direct volume is active");
  emit volumeGpuStatusChanged();
  emit volumeChanged();
  return rebuildScenePacket();
}

bool MolecularViewport::setMolecularSurface(
    const QString &kind, const QString &selection,
    double probe_radius_angstrom, double grid_spacing_angstrom,
    qulonglong voxel_budget, qulonglong memory_budget_bytes) {
  const auto normalized_kind = kind.trimmed().toLower();
  const auto normalized_selection = selection.trimmed();
  if (workspace_->active_object() == nullptr ||
      (normalized_kind != QStringLiteral("vdw") &&
       normalized_kind != QStringLiteral("sas")) ||
      normalized_selection.isEmpty() || voxel_budget == 0U ||
      memory_budget_bytes == 0U ||
      !std::isfinite(probe_radius_angstrom) ||
      !std::isfinite(grid_spacing_angstrom)) {
    setStatus(QStringLiteral("Open a molecular structure and provide valid surface parameters"),
              atom_count_, primitive_count_);
    return false;
  }
  gui::Action action;
  action.command_name = "surface show";
  action.parameters.emplace("kind", normalized_kind.toStdString());
  action.parameters.emplace("selection", normalized_selection.toStdString());
  action.parameters.emplace("probe-radius",
                            std::to_string(probe_radius_angstrom));
  action.parameters.emplace("grid-spacing",
                            std::to_string(grid_spacing_angstrom));
  action.parameters.emplace("color", "cyan");
  action.parameters.emplace("opacity", "0.72");
  action.parameters.emplace("voxel-budget", std::to_string(voxel_budget));
  action.parameters.emplace("memory-budget-bytes",
                            std::to_string(memory_budget_bytes));
  operation::TaskContext context;
  const auto shown = actions_.trigger(action, context);
  if (!shown.succeeded()) {
    setStatus(QStringLiteral("Molecular surface failed: ") +
                  outcome_error(shown),
              atom_count_, primitive_count_);
    return false;
  }
  representation_ = QStringLiteral("surface");
  emit representationChanged();
  if (!rebuildScenePacket())
    return false;
  const auto *object = workspace_->active_object();
  const auto vertices = object == nullptr || !object->molecular_surface.has_value()
                            ? 0U
                            : object->molecular_surface->mesh_vertices.size();
  const auto triangles = object == nullptr || !object->molecular_surface.has_value()
                             ? 0U
                             : object->molecular_surface->mesh_triangles.size();
  qInfo("MolShredder desktop molecular surface ready: kind=%s probe=%.3g spacing=%.3g vertices=%llu triangles=%llu",
        normalized_kind.toUtf8().constData(), probe_radius_angstrom,
        grid_spacing_angstrom, static_cast<unsigned long long>(vertices),
        static_cast<unsigned long long>(triangles));
  return true;
}

bool MolecularViewport::hideMolecularSurface() {
  gui::Action action;
  action.command_name = "surface hide";
  operation::TaskContext context;
  const auto hidden = actions_.trigger(action, context);
  if (!hidden.succeeded()) {
    setStatus(QStringLiteral("Molecular surface hide failed: ") +
                  outcome_error(hidden),
              atom_count_, primitive_count_);
    return false;
  }
  if (representation_ == QStringLiteral("surface")) {
    representation_ = QStringLiteral("lines");
    emit representationChanged();
  }
  return rebuildScenePacket();
}

void MolecularViewport::syncActiveRepresentationName() {
  const auto *object = workspace_->active_object();
  if (object == nullptr || object->representations.empty())
    return;
  const auto name = representation_name(object->representations.back().kind);
  if (representation_ == name)
    return;
  representation_ = name;
  emit representationChanged();
}

bool MolecularViewport::refreshWorkspacePresentation() {
  syncCameraState();
  syncTrajectoryState();
  syncActiveRepresentationName();
  const auto *volume = workspace_->active_volume();
  has_volume_ = volume != nullptr;
  if (volume != nullptr) {
    const auto range = volume->grid->scalars().range();
    volume_minimum_ = range.first;
    volume_maximum_ = range.second;
    const auto shape = volume->grid->shape();
    volume_slice_maximum_ = static_cast<qulonglong>(shape.z - 1U);
  }
  emit volumeChanged();
  emit objectsChanged();
  emit analysisResultsChanged();
  if (workspace_->active_object() == nullptr && volume == nullptr) {
    setRenderPacket({});
    setStatus(QStringLiteral("Workspace is empty"), 0U, 0U);
    return true;
  }
  return rebuildScenePacket();
}

bool MolecularViewport::rebuildScenePacket() {
  const auto *active = workspace_->active_object();
  const auto *active_volume = workspace_->active_volume();
  if (active == nullptr && active_volume == nullptr) {
    setStatus(QStringLiteral("Workspace has no active object"), 0U, 0U);
    return false;
  }
  render::RenderPacket combined;
  direct_volume_pick_id_ = 0U;
  combined.provenance.emplace("desktop_composite", "visible-objects-v1");
  std::size_t visible_object_count{};
  for (const auto &object : workspace_->objects()) {
    if (!workspace_->scene()->effectively_visible(object.scene_node))
      continue;
    ++visible_object_count;
    for (const auto &representation : object.representations) {
      append_packet(combined, representation.packet);
    }
    if (object.molecular_surface.has_value())
      append_packet(combined, *object.molecular_surface);
  }
  for (const auto &volume : workspace_->volumes()) {
    if (!workspace_->scene()->effectively_visible(volume.scene_node)) continue;
    ++visible_object_count;
    for (const auto &representation : volume.representations) {
      append_packet(combined, representation);
    }
    if (active_volume != nullptr && volume.id == active_volume->id &&
        volume.direct_volume != nullptr) {
      direct_volume_pick_id_ = combined.pick_targets.empty()
                                   ? 1U
                                   : combined.pick_targets.rbegin()->first + 1U;
      combined.pick_targets.emplace(
          direct_volume_pick_id_,
          render::PickTarget{render::PickKind::volume,
                             volume.scene_node.value});
      const auto &data = *volume.direct_volume;
      using scene::operator+;
      using scene::operator*;
      for (const auto x : {0.0, static_cast<double>(data.shape.x - 1U)}) {
        for (const auto y : {0.0, static_cast<double>(data.shape.y - 1U)}) {
          for (const auto z : {0.0, static_cast<double>(data.shape.z - 1U)}) {
            render::include(combined.bounds,
                            data.origin + data.deltas[0] * x +
                                data.deltas[1] * y + data.deltas[2] * z);
          }
        }
      }
    }
  }
  append_packet(combined,
                build_analysis_overlay_packet(workspace_->analysis_results()));
  const auto primitive_count =
      combined.lines.size() + combined.cylinders.size() +
      combined.spheres.size() + combined.mesh_triangles.size() +
      (direct_volume_pick_id_ == 0U ? 0U : 1U);
  atom_count_ = active == nullptr
                    ? 0U
                    : static_cast<qulonglong>(
                          active->system->topology()->atom_count());
  primitive_count_ = static_cast<qulonglong>(primitive_count);
  setRenderPacket(std::move(combined));
  const auto label = active != nullptr
                         ? QString::fromStdString(active->system->name()) +
                               QStringLiteral(" · ") + representation_ +
                               QStringLiteral(" · ") +
                               QString::number(atom_count_) +
                               QStringLiteral(" active atoms")
                         : QString::fromStdString(active_volume->name) +
                               (volume_mode_ == QStringLiteral("slice")
                                    ? QStringLiteral(" · slice ") +
                                          volume_slice_axis_.toUpper() +
                                          QStringLiteral(" ") +
                                          QString::number(volume_slice_index_)
                                    : volume_mode_ == QStringLiteral("direct")
                                          ? QStringLiteral(" · direct volume")
                                          : QStringLiteral(" · isosurface ") +
                                                QString::number(volume_level_, 'g', 6));
  setStatus(label + QStringLiteral(" · ") +
                QString::number(visible_object_count) +
                QStringLiteral(" visible objects · ") +
                QString::number(primitive_count_) +
                QStringLiteral(" primitives"),
            atom_count_, primitive_count_);
  emit objectsChanged();
  return true;
}

void MolecularViewport::syncTrajectoryState() {
  bool has_trajectory{};
  qulonglong frame{};
  qulonglong frame_count{};
  bool playing{};
  QString mode{QStringLiteral("once")};
  QString direction{QStringLiteral("forward")};
  double frames_per_second{30.0};
  const auto *object = workspace_->active_object();
  if (object != nullptr && object->trajectory.has_value()) {
    has_trajectory = true;
    const auto snapshot = object->trajectory->timeline.snapshot();
    frame = static_cast<qulonglong>(snapshot.frame);
    frame_count = static_cast<qulonglong>(
        object->trajectory->cache->frame_count().value_or(0U));
    playing = snapshot.playing;
    mode = playback_mode_name(snapshot.mode);
    direction = playback_direction_name(snapshot.direction);
    frames_per_second = object->trajectory->clock.frames_per_second();
  }
  const auto changed =
      has_trajectory_ != has_trajectory || trajectory_frame_ != frame ||
      trajectory_frame_count_ != frame_count ||
      trajectory_playing_ != playing || playback_mode_ != mode ||
      playback_direction_ != direction ||
      !qFuzzyCompare(trajectory_fps_, frames_per_second);
  has_trajectory_ = has_trajectory;
  trajectory_frame_ = frame;
  trajectory_frame_count_ = frame_count;
  trajectory_playing_ = playing;
  playback_mode_ = std::move(mode);
  playback_direction_ = std::move(direction);
  trajectory_fps_ = frames_per_second;
  if (trajectory_playing_) {
    if (!playback_elapsed_.isValid())
      playback_elapsed_.start();
    if (!playback_timer_.isActive())
      playback_timer_.start();
  } else {
    playback_timer_.stop();
    playback_elapsed_.invalidate();
  }
  if (changed)
    emit trajectoryChanged();
}

void MolecularViewport::onPlaybackTick() {
  if (!has_trajectory_ || !trajectory_playing_) {
    playback_timer_.stop();
    return;
  }
  const auto elapsed_ms = playback_elapsed_.restart();
  if (elapsed_ms < 0)
    return;
  if (!tickTrajectory(static_cast<double>(elapsed_ms))) {
    static_cast<void>(invokeTrajectoryAction(
        "traj pause", {}, QStringLiteral("Trajectory pause failed: "), false));
  }
}

void MolecularViewport::onTrajectoryTaskPoll() {
  const bool is_load = pending_trajectory_load_.has_value();
  if (!is_load && !pending_trajectory_frame_.has_value()) {
    trajectory_task_timer_.stop();
    return;
  }
  const auto task_id = is_load ? pending_trajectory_load_->task_id
                               : pending_trajectory_frame_->task_id;
  const auto operation_name =
      is_load ? QStringLiteral("Trajectory load")
              : QStringLiteral("Trajectory seek");
  auto snapshot = trajectory_task_scheduler_->snapshot(task_id);
  if (!snapshot.has_value()) {
    setStatus(operation_name + QStringLiteral(" failed: task state was lost"),
              atom_count_, primitive_count_);
    pending_trajectory_load_.reset();
    pending_trajectory_frame_.reset();
    trajectory_task_timer_.stop();
    trajectory_task_running_ = false;
    trajectory_task_stage_ = QStringLiteral("failed");
    emit trajectoryTaskChanged();
    return;
  }
  trajectory_task_progress_ = snapshot.value().progress_fraction;
  trajectory_task_stage_ =
      QString::fromUtf8(operation::to_string(snapshot.value().state).data(),
                        static_cast<qsizetype>(operation::to_string(
                                                   snapshot.value().state)
                                                   .size()));
  if (snapshot.value().state == operation::TaskState::ready_to_commit) {
    if (const auto error = trajectory_task_scheduler_->commit_ready(task_id);
        error.has_value()) {
      setStatus(operation_name + QStringLiteral(" commit failed: ") +
                    QString::fromStdString(error->message),
                atom_count_, primitive_count_);
      pending_trajectory_load_.reset();
      pending_trajectory_frame_.reset();
      trajectory_task_timer_.stop();
      trajectory_task_running_ = false;
      trajectory_task_stage_ = QStringLiteral("failed");
      emit trajectoryTaskChanged();
      return;
    }
    snapshot = trajectory_task_scheduler_->snapshot(task_id);
  }
  if (!snapshot.has_value()) return;
  const auto state = snapshot.value().state;
  if (state != operation::TaskState::succeeded &&
      state != operation::TaskState::failed &&
      state != operation::TaskState::cancelled &&
      state != operation::TaskState::stale) {
    emit trajectoryTaskChanged();
    return;
  }
  const auto task_error = snapshot.value().error;
  trajectory_task_timer_.stop();
  trajectory_task_running_ = false;
  if (is_load) {
    auto completed = pending_trajectory_load_->completion->result();
    pending_trajectory_load_.reset();
    if (state == operation::TaskState::succeeded && completed.has_value()) {
      const auto *object = workspace_->active_object();
      const auto rendered = object != nullptr && object->representations.empty()
                                ? rebuildRepresentation()
                                : rebuildScenePacket();
      if (!rendered) {
        trajectory_task_stage_ = QStringLiteral("failed");
        emit trajectoryTaskChanged();
        return;
      }
      syncTrajectoryState();
      resetView();
      trajectory_task_progress_ = 1.0;
      trajectory_task_stage_ = QStringLiteral("complete");
      setStatus(QStringLiteral("Trajectory attached · ") +
                    QString::number(completed.value().frame_count) +
                    QStringLiteral(" frames · ") + trajectoryMappingText(),
                atom_count_, primitive_count_);
      qInfo("MolShredder desktop background trajectory load committed: frames=%llu task=%llu mapping=%s",
            static_cast<unsigned long long>(completed.value().frame_count),
            static_cast<unsigned long long>(task_id),
            trajectoryMappingText().toUtf8().constData());
    } else {
      trajectory_task_progress_ = 0.0;
      trajectory_task_stage_ =
          state == operation::TaskState::cancelled
              ? QStringLiteral("cancelled")
              : (state == operation::TaskState::stale
                     ? QStringLiteral("stale")
                     : QStringLiteral("failed"));
      const auto message =
          task_error.has_value()
              ? QString::fromStdString(task_error->message)
              : (completed.has_value()
                     ? QStringLiteral("unknown task completion failure")
                     : QString::fromStdString(completed.error().message));
      setStatus(QStringLiteral("Trajectory load failed: ") + message,
                atom_count_, primitive_count_);
    }
    emit trajectoryTaskChanged();
    return;
  }

  auto completed = pending_trajectory_frame_->completion->result();
  pending_trajectory_frame_.reset();
  if (state == operation::TaskState::succeeded && completed.has_value()) {
    if (!rebuildScenePacket()) {
      trajectory_task_stage_ = QStringLiteral("failed");
      emit trajectoryTaskChanged();
      return;
    }
    syncTrajectoryState();
    trajectory_task_progress_ = 1.0;
    trajectory_task_stage_ = QStringLiteral("complete");
    qInfo("MolShredder desktop background seek committed: frame=%llu task=%llu update=%s reason=%s",
          static_cast<unsigned long long>(completed.value().playback.frame),
          static_cast<unsigned long long>(task_id),
          packet_update_mode_.toUtf8().constData(),
          packet_update_reason_.toUtf8().constData());
  } else {
    trajectory_task_progress_ = 0.0;
    trajectory_task_stage_ =
        state == operation::TaskState::cancelled
            ? QStringLiteral("cancelled")
            : (state == operation::TaskState::stale ? QStringLiteral("stale")
                                                    : QStringLiteral("failed"));
    const auto message =
        task_error.has_value()
            ? QString::fromStdString(task_error->message)
            : (completed.has_value()
                   ? QStringLiteral("unknown task completion failure")
                   : QString::fromStdString(completed.error().message));
    setStatus(operation_name + QStringLiteral(" failed: ") + message,
              atom_count_, primitive_count_);
  }
  emit trajectoryTaskChanged();
}

void MolecularViewport::onAnalysisTaskPoll() {
  if (!pending_analysis_.has_value()) {
    analysis_task_timer_.stop();
    return;
  }
  const auto task_id = pending_analysis_->task_id;
  auto snapshot = analysis_task_scheduler_->snapshot(task_id);
  if (!snapshot.has_value()) {
    pending_analysis_.reset();
    analysis_task_timer_.stop();
    analysis_task_running_ = false;
    analysis_task_progress_ = 0.0;
    analysis_task_stage_ = QStringLiteral("failed");
    setStatus(QStringLiteral("Analysis failed: task state was lost"),
              atom_count_, primitive_count_);
    emit analysisTaskChanged();
    return;
  }
  analysis_task_progress_ = snapshot.value().progress_fraction;
  analysis_task_stage_ = QString::fromUtf8(
      operation::to_string(snapshot.value().state).data(),
      static_cast<qsizetype>(
          operation::to_string(snapshot.value().state).size()));
  if (snapshot.value().state == operation::TaskState::ready_to_commit) {
    if (const auto error = analysis_task_scheduler_->commit_ready(task_id);
        error.has_value()) {
      setStatus(QStringLiteral("Analysis commit failed: ") +
                    QString::fromStdString(error->message),
                atom_count_, primitive_count_);
    }
    snapshot = analysis_task_scheduler_->snapshot(task_id);
  }
  if (!snapshot.has_value()) return;
  const auto state = snapshot.value().state;
  if (state != operation::TaskState::succeeded &&
      state != operation::TaskState::failed &&
      state != operation::TaskState::cancelled &&
      state != operation::TaskState::stale) {
    emit analysisTaskChanged();
    return;
  }
  const auto task_error = snapshot.value().error;
  auto completed = pending_analysis_->completion->result();
  pending_analysis_.reset();
  analysis_task_timer_.stop();
  analysis_task_running_ = false;
  if (state == operation::TaskState::succeeded && completed.has_value()) {
    analysis_task_progress_ = 1.0;
    analysis_task_stage_ = QStringLiteral("complete");
    emit analysisResultsChanged();
    setStatus(QStringLiteral("Analysis result stored"), atom_count_,
              primitive_count_);
  } else {
    analysis_task_progress_ = 0.0;
    analysis_task_stage_ =
        state == operation::TaskState::cancelled
            ? QStringLiteral("cancelled")
            : (state == operation::TaskState::stale
                   ? QStringLiteral("stale")
                   : QStringLiteral("failed"));
    const auto message =
        task_error.has_value()
            ? QString::fromStdString(task_error->message)
            : (completed.has_value()
                   ? QStringLiteral("unknown analysis completion failure")
                   : QString::fromStdString(completed.error().message));
    setStatus(QStringLiteral("Analysis failed: ") + message, atom_count_,
              primitive_count_);
  }
  emit analysisTaskChanged();
}

void MolecularViewport::cancelAnalysisTask() {
  const auto had_task = pending_analysis_.has_value() || analysis_task_running_;
  if (analysis_task_generation_ != nullptr)
    analysis_task_generation_->fetch_add(1U);
  if (pending_analysis_.has_value() && analysis_task_scheduler_ != nullptr)
    static_cast<void>(
        analysis_task_scheduler_->cancel(pending_analysis_->task_id));
  pending_analysis_.reset();
  analysis_task_timer_.stop();
  analysis_task_running_ = false;
  analysis_task_progress_ = 0.0;
  analysis_task_stage_ = QStringLiteral("cancelled");
  if (had_task) {
    setStatus(QStringLiteral("Analysis task cancelled"), atom_count_,
              primitive_count_);
    emit analysisTaskChanged();
  }
}

void MolecularViewport::onDirectVolumeTaskPoll() {
  if (!pending_direct_volume_.has_value()) {
    volume_task_timer_.stop();
    return;
  }
  const auto task_id = pending_direct_volume_->task_id;
  auto snapshot = volume_task_scheduler_->snapshot(task_id);
  if (!snapshot.has_value()) {
    setStatus(QStringLiteral("Direct volume failed: task state was lost"),
              atom_count_, primitive_count_);
    pending_direct_volume_.reset();
    volume_task_timer_.stop();
    volume_task_running_ = false;
    volume_task_stage_ = QStringLiteral("failed");
    emit volumeTaskChanged();
    return;
  }
  volume_task_progress_ = snapshot.value().progress_fraction;
  volume_task_stage_ = QString::fromUtf8(
      operation::to_string(snapshot.value().state).data(),
      static_cast<qsizetype>(
          operation::to_string(snapshot.value().state).size()));
  if (snapshot.value().state == operation::TaskState::ready_to_commit) {
    if (const auto error = volume_task_scheduler_->commit_ready(task_id);
        error.has_value()) {
      setStatus(QStringLiteral("Direct volume commit failed: ") +
                    QString::fromStdString(error->message),
                atom_count_, primitive_count_);
      pending_direct_volume_.reset();
      volume_task_timer_.stop();
      volume_task_running_ = false;
      volume_task_stage_ = QStringLiteral("failed");
      emit volumeTaskChanged();
      return;
    }
    snapshot = volume_task_scheduler_->snapshot(task_id);
  }
  if (!snapshot.has_value()) return;
  const auto state = snapshot.value().state;
  if (state != operation::TaskState::succeeded &&
      state != operation::TaskState::failed &&
      state != operation::TaskState::cancelled &&
      state != operation::TaskState::stale) {
    emit volumeTaskChanged();
    return;
  }
  const auto task_error = snapshot.value().error;
  auto completed = pending_direct_volume_->completion->result();
  pending_direct_volume_.reset();
  volume_task_timer_.stop();
  volume_task_running_ = false;
  if (state == operation::TaskState::succeeded && completed.has_value()) {
    volume_mode_ = QStringLiteral("direct");
    volume_gpu_state_ = QStringLiteral("uploading");
    volume_gpu_message_ =
        QStringLiteral("Uploading direct-volume GPU resources");
    emit volumeGpuStatusChanged();
    emit volumeChanged();
    if (!rebuildScenePacket()) {
      volume_task_progress_ = 0.0;
      volume_task_stage_ = QStringLiteral("failed");
      emit volumeTaskChanged();
      return;
    }
    volume_task_progress_ = 1.0;
    volume_task_stage_ = QStringLiteral("complete");
    setStatus(QStringLiteral("Direct volume ready"), atom_count_,
              primitive_count_);
    qInfo("MolShredder desktop background direct volume committed: ramp=%s step=%.3g maximum_steps=%llu lut=%llu budget=%llu task=%llu canonical=shared",
          completed.value().transfer_function_name.c_str(),
          completed.value().sampling_step,
          static_cast<unsigned long long>(completed.value().maximum_steps),
          static_cast<unsigned long long>(
              completed.value().lookup_table_samples),
          static_cast<unsigned long long>(
              completed.value().texture_budget_bytes),
          static_cast<unsigned long long>(task_id));
  } else {
    volume_task_progress_ = 0.0;
    volume_task_stage_ =
        state == operation::TaskState::cancelled
            ? QStringLiteral("cancelled")
            : (state == operation::TaskState::stale
                   ? QStringLiteral("stale")
                   : QStringLiteral("failed"));
    const auto message =
        task_error.has_value()
            ? QString::fromStdString(task_error->message)
            : (completed.has_value()
                   ? QStringLiteral("unknown task completion failure")
                   : QString::fromStdString(completed.error().message));
    setStatus(QStringLiteral("Direct volume failed: ") + message,
              atom_count_, primitive_count_);
  }
  emit volumeTaskChanged();
}

bool MolecularViewport::invokeTrajectoryAction(
    std::string command_name,
    std::map<std::string, std::string, std::less<>> parameters,
    QString failure_prefix, bool rebuild_scene) {
  gui::Action action;
  action.command_name = std::move(command_name);
  action.parameters = std::move(parameters);
  operation::TaskContext context;
  const auto outcome = actions_.trigger(action, context);
  if (!outcome.succeeded()) {
    setStatus(std::move(failure_prefix) + outcome_error(outcome), atom_count_,
              primitive_count_);
    return false;
  }
  if (rebuild_scene && !rebuildScenePacket())
    return false;
  syncTrajectoryState();
  return true;
}

void MolecularViewport::setStatus(QString status, qulonglong atom_count,
                                  qulonglong primitive_count) {
  status_text_ = std::move(status);
  atom_count_ = atom_count;
  primitive_count_ = primitive_count;
  emit statusChanged();
}

void MolecularViewport::setRenderPacket(render::RenderPacket packet) {
  QString update_reason;
  packet_incremental_ =
      incremental_packet_compatible(packet_, packet, update_reason);
  packet_update_mode_ = packet_incremental_ ? QStringLiteral("incremental")
                                             : QStringLiteral("full");
  packet_update_reason_ = std::move(update_reason);
  packet_ = std::move(packet);
  ++packet_revision_;
  emit analysisResultsChanged();
  update();
}

} // namespace molshredder::desktop
