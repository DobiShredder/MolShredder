#include "viewport_item.hpp"

#include <algorithm>
#include <array>
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
#include <QFileInfo>
#include <QMatrix4x4>
#include <QMetaObject>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantMap>
#include <rhi/qrhi.h>

#include "molshredder/analysis/secondary_structure.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/render/representation.hpp"

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
                                          states};
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

  void initialize(QRhiCommandBuffer *command_buffer) override {
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
    if (!uniform_buffer_)
      create_uniform_resources();
    if (!mesh_pipeline_ || !sphere_pipeline_ || !cylinder_pipeline_ ||
        !line_pipeline_ || !pick_mesh_pipeline_ || !pick_sphere_pipeline_ ||
        !pick_cylinder_pipeline_ || !pick_line_pipeline_) {
      create_pipelines();
    }
    if (geometry_dirty_)
      upload_geometry(command_buffer);
  }

  void synchronize(QQuickRhiItem *item) override {
    auto *viewport = static_cast<MolecularViewport *>(item);
    viewport_item_ = viewport;
    angle_ = viewport->angle();
    if (camera_revision_ != viewport->cameraRevision()) {
      camera_revision_ = viewport->cameraRevision();
      if (const auto *camera = viewport->camera(); camera != nullptr) {
        camera_parameters_ = camera->parameters();
        camera_view_ = qt_matrix(camera->view_matrix());
      }
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
    geometry_dirty_ = true;
  }

  void render(QRhiCommandBuffer *command_buffer) override {
    if (!uniform_buffer_ || !bindings_)
      return;
    if (geometry_dirty_)
      upload_geometry(command_buffer);
    update_view_projection();
    auto *updates = rhi_->nextResourceUpdateBatch();
    QMatrix4x4 model;
    model.translate(static_cast<float>(center_.x),
                    static_cast<float>(center_.y),
                    static_cast<float>(center_.z));
    model.rotate(angle_, 0.0F, 1.0F, 0.0F);
    model.translate(static_cast<float>(-center_.x),
                    static_cast<float>(-center_.y),
                    static_cast<float>(-center_.z));
    const auto mvp = view_projection_ * model;
    const auto size = renderTarget()->pixelSize();
    const std::array<float, 4> viewport{static_cast<float>(size.width()),
                                        static_cast<float>(size.height()), 0.0F,
                                        0.0F};
    updates->updateDynamicBuffer(uniform_buffer_.get(), 0U, 64U,
                                 mvp.constData());
    updates->updateDynamicBuffer(uniform_buffer_.get(), 64U, 64U,
                                 model.constData());
    updates->updateDynamicBuffer(uniform_buffer_.get(), 128U, 16U,
                                 viewport.data());
    command_buffer->beginPass(renderTarget(),
                              QColor::fromRgbF(0.018F, 0.025F, 0.04F),
                              {1.0F, 0U}, updates);
    command_buffer->setViewport(
        QRhiViewport{0.0F, 0.0F, static_cast<float>(size.width()),
                     static_cast<float>(size.height())});
    draw_mesh(command_buffer);
    draw_cylinders(command_buffer);
    draw_spheres(command_buffer);
    draw_lines(command_buffer);
    command_buffer->endPass();
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
  void update_view_projection() {
    const QSize output_size = renderTarget()->pixelSize();
    QMatrix4x4 projection = rhi_->clipSpaceCorrMatrix();
    const auto aspect = output_size.height() == 0
                            ? 1.0F
                            : static_cast<float>(output_size.width()) /
                                  static_cast<float>(output_size.height());
    if (camera_parameters_.projection == scene::ProjectionMode::perspective) {
      projection.perspective(
          static_cast<float>(camera_parameters_.vertical_field_of_view_radians *
                             180.0 / std::numbers::pi),
          aspect, static_cast<float>(camera_parameters_.near_clip),
          static_cast<float>(camera_parameters_.far_clip));
    } else {
      const auto half_height =
          static_cast<float>(camera_parameters_.orthographic_height * 0.5);
      const auto half_width = half_height * aspect;
      projection.ortho(-half_width, half_width, -half_height, half_height,
                       static_cast<float>(camera_parameters_.near_clip),
                       static_cast<float>(camera_parameters_.far_clip));
    }
    view_projection_ = projection * camera_view_;
  }

  void reset_pipelines() {
    mesh_pipeline_.reset();
    sphere_pipeline_.reset();
    cylinder_pipeline_.reset();
    line_pipeline_.reset();
    reset_pick_pipelines();
  }

  void reset_pick_pipelines() {
    pick_mesh_pipeline_.reset();
    pick_sphere_pipeline_.reset();
    pick_cylinder_pipeline_.reset();
    pick_line_pipeline_.reset();
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
    pick_mesh_vertex_buffer_.reset();
    uniform_buffer_.reset();
    bindings_.reset();
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
                     QRhiBuffer::UsageFlags usage, quint32 bytes,
                     const void *data, QRhiResourceUpdateBatch &updates) {
    buffer.reset(rhi_->newBuffer(QRhiBuffer::Immutable, usage, bytes));
    if (!buffer->create()) {
      buffer.reset();
      return false;
    }
    updates.uploadStaticBuffer(buffer.get(), data);
    return true;
  }

  template <typename Value>
  bool upload_vector(std::unique_ptr<QRhiBuffer> &buffer,
                     QRhiBuffer::UsageFlags usage,
                     const std::vector<Value> &values,
                     QRhiResourceUpdateBatch &updates) {
    const auto bytes = byte_size(values);
    if (!bytes.has_value()) {
      buffer.reset();
      return values.empty();
    }
    return upload_buffer(buffer, usage, *bytes, values.data(), updates);
  }

  void upload_geometry(QRhiCommandBuffer *command_buffer) {
    geometry_dirty_ = false;
    auto *updates = rhi_->nextResourceUpdateBatch();
    const auto mesh_ok =
        upload_vector(mesh_vertex_buffer_, QRhiBuffer::VertexBuffer,
                      mesh_vertices_, *updates) &&
        upload_vector(mesh_index_buffer_, QRhiBuffer::IndexBuffer,
                      mesh_indices_, *updates);
    const auto pick_mesh_ok =
        upload_vector(pick_mesh_vertex_buffer_, QRhiBuffer::VertexBuffer,
                      pick_mesh_vertices_, *updates);
    const auto sphere_ok =
        upload_vector(sphere_vertex_buffer_, QRhiBuffer::VertexBuffer,
                      sphere_vertices_, *updates) &&
        upload_vector(sphere_index_buffer_, QRhiBuffer::IndexBuffer,
                      sphere_indices_, *updates) &&
        upload_vector(sphere_instance_buffer_, QRhiBuffer::VertexBuffer,
                      sphere_instances_, *updates);
    const auto cylinder_ok =
        upload_vector(cylinder_vertex_buffer_, QRhiBuffer::VertexBuffer,
                      cylinder_vertices_, *updates) &&
        upload_vector(cylinder_index_buffer_, QRhiBuffer::IndexBuffer,
                      cylinder_indices_, *updates) &&
        upload_vector(cylinder_instance_buffer_, QRhiBuffer::VertexBuffer,
                      cylinder_instances_, *updates);
    const auto line_ok =
        upload_vector(line_vertex_buffer_, QRhiBuffer::VertexBuffer,
                      line_corners_, *updates) &&
        upload_vector(line_index_buffer_, QRhiBuffer::IndexBuffer,
                      line_indices_, *updates) &&
        upload_vector(line_instance_buffer_, QRhiBuffer::VertexBuffer,
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

  void draw_mesh(QRhiCommandBuffer *command_buffer) {
    if (!mesh_pipeline_ || !mesh_vertex_buffer_ || !mesh_index_buffer_ ||
        mesh_indices_.empty()) {
      return;
    }
    command_buffer->setGraphicsPipeline(mesh_pipeline_.get());
    command_buffer->setShaderResources();
    const QRhiCommandBuffer::VertexInput input{mesh_vertex_buffer_.get(), 0U};
    command_buffer->setVertexInput(0, 1, &input, mesh_index_buffer_.get(), 0U,
                                   QRhiCommandBuffer::IndexUInt32);
    command_buffer->drawIndexed(static_cast<quint32>(mesh_indices_.size()));
  }

  void draw_spheres(QRhiCommandBuffer *command_buffer) {
    if (!sphere_pipeline_ || !sphere_vertex_buffer_ || !sphere_index_buffer_ ||
        !sphere_instance_buffer_ || sphere_instances_.empty()) {
      return;
    }
    command_buffer->setGraphicsPipeline(sphere_pipeline_.get());
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

  void draw_cylinders(QRhiCommandBuffer *command_buffer) {
    if (!cylinder_pipeline_ || !cylinder_vertex_buffer_ ||
        !cylinder_index_buffer_ || !cylinder_instance_buffer_ ||
        cylinder_instances_.empty()) {
      return;
    }
    command_buffer->setGraphicsPipeline(cylinder_pipeline_.get());
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

  void draw_lines(QRhiCommandBuffer *command_buffer) {
    if (!line_pipeline_ || !line_vertex_buffer_ || !line_index_buffer_ ||
        !line_instance_buffer_ || line_instances_.empty()) {
      return;
    }
    command_buffer->setGraphicsPipeline(line_pipeline_.get());
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
    command_buffer->endPass();

    auto state = std::make_shared<PickReadbackState>();
    const auto viewport = viewport_item_;
    const auto source_ids = pick_source_ids_;
    const auto request_revision = pick_request_revision_;
    const auto packet_revision = pick_packet_revision_;
    state->result.completed = [state, viewport, source_ids, request_revision,
                               packet_revision] {
      const auto gpu_id = decoded_pick_color(state->result.data);
      const auto source_id =
          source_ids && gpu_id < source_ids->size()
              ? (*source_ids)[static_cast<std::size_t>(gpu_id)]
              : std::uint64_t{};
      if (viewport != nullptr) {
        QMetaObject::invokeMethod(
            viewport.data(),
            [viewport, request_revision, packet_revision, source_id] {
              if (viewport != nullptr) {
                viewport->deliverPickResult(request_revision, packet_revision,
                                            source_id);
              }
            },
            Qt::QueuedConnection);
      }
      state->result.completed = {};
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
  std::unique_ptr<QRhiGraphicsPipeline> mesh_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> sphere_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> cylinder_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> line_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> pick_mesh_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> pick_sphere_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> pick_cylinder_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> pick_line_pipeline_;
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
  QMatrix4x4 view_projection_;
  QMatrix4x4 camera_view_;
  scene::CameraParameters camera_parameters_;
  model::Vec3d center_{};
  std::uint64_t revision_{};
  std::uint64_t camera_revision_{};
  QPointer<MolecularViewport> viewport_item_;
  QPointF pick_position_;
  QSizeF pick_item_size_;
  QSize pick_target_size_;
  std::shared_ptr<const std::vector<std::uint64_t>> pick_source_ids_;
  std::uint64_t pick_request_revision_{};
  std::uint64_t pick_packet_revision_{};
  float angle_{};
  bool geometry_dirty_{true};
  bool primitive_logged_{false};
  bool pick_pending_{};
};

} // namespace

MolecularViewport::MolecularViewport(QQuickItem *parent)
    : QQuickRhiItem{parent},
      workspace_{std::make_shared<application::Workspace>()},
      registry_{application::make_default_registry(workspace_)},
      dispatcher_{registry_}, actions_{dispatcher_}, packet_{demo_packet()} {
  setSampleCount(4);
  playback_timer_.setInterval(16);
  playback_timer_.setTimerType(Qt::PreciseTimer);
  connect(&playback_timer_, &QTimer::timeout, this,
          &MolecularViewport::onPlaybackTick);
  resetView();
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
                                       const QString &coordinate_unit) {
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
  if (!invokeTrajectoryAction(
          "traj load",
          {{"path", file_info.filesystemFilePath().string()},
           {"coordinate-unit", coordinate_unit.toStdString()}},
          QStringLiteral("Trajectory load failed: "), true)) {
    return false;
  }
  const auto *object = workspace_->active_object();
  if (object != nullptr && object->representations.empty() &&
      !rebuildRepresentation()) {
    return false;
  }
  resetView();
  qInfo("MolShredder desktop trajectory attached: frames=%llu frame=%llu",
        static_cast<unsigned long long>(trajectory_frame_count_),
        static_cast<unsigned long long>(trajectory_frame_));
  return true;
}

bool MolecularViewport::seekTrajectory(qulonglong frame) {
  if (!has_trajectory_ || frame >= trajectory_frame_count_)
    return false;
  return invokeTrajectoryAction(
      "traj frame", {{"frame", std::to_string(frame)}},
      QStringLiteral("Trajectory seek failed: "), true);
}

bool MolecularViewport::setTrajectoryPlaying(bool playing) {
  if (!has_trajectory_)
    return false;
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
  return invokeTrajectoryAction(
      "traj speed", {{"fps", std::to_string(frames_per_second)}},
      QStringLiteral("Playback speed failed: "), false);
}

bool MolecularViewport::tickTrajectory(double elapsed_milliseconds) {
  if (!has_trajectory_ || !std::isfinite(elapsed_milliseconds) ||
      elapsed_milliseconds < 0.0) {
    return false;
  }
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

void MolecularViewport::orbit(double delta_x, double delta_y) {
  if (!camera_.has_value())
    return;
  const auto updated = camera_->orbit_pixels(delta_x, delta_y);
  if (!updated.has_value())
    return;
  camera_ = updated.value();
  ++camera_revision_;
  update();
}

void MolecularViewport::pan(double delta_x, double delta_y) {
  if (!camera_.has_value() || height() <= 0.0)
    return;
  const auto updated = camera_->pan_pixels(delta_x, delta_y, height());
  if (!updated.has_value())
    return;
  camera_ = updated.value();
  ++camera_revision_;
  update();
}

void MolecularViewport::dolly(double delta) {
  if (!camera_.has_value())
    return;
  const auto updated = camera_->dolly(delta);
  if (!updated.has_value())
    return;
  camera_ = updated.value();
  ++camera_revision_;
  update();
}

void MolecularViewport::resetView() {
  scene::CameraParameters parameters;
  model::Vec3d center{};
  double radius = 1.0;
  if (!packet_.bounds.empty) {
    center = {(packet_.bounds.minimum.x + packet_.bounds.maximum.x) * 0.5,
              (packet_.bounds.minimum.y + packet_.bounds.maximum.y) * 0.5,
              (packet_.bounds.minimum.z + packet_.bounds.maximum.z) * 0.5};
    const auto extent_x = packet_.bounds.maximum.x - packet_.bounds.minimum.x;
    const auto extent_y = packet_.bounds.maximum.y - packet_.bounds.minimum.y;
    const auto extent_z = packet_.bounds.maximum.z - packet_.bounds.minimum.z;
    radius = std::max(std::sqrt(extent_x * extent_x + extent_y * extent_y +
                                extent_z * extent_z) *
                          0.5,
                      1.0e-3);
  }
  parameters.distance = std::max(radius * 3.0, 1.0e-3);
  parameters.near_clip = std::max(radius * 1.0e-4, 1.0e-6);
  parameters.far_clip = std::max(radius * 100.0, parameters.near_clip * 10.0);
  auto camera = scene::Camera::create(parameters);
  if (!camera.has_value())
    return;
  if (width() > 0.0 && height() > 0.0) {
    auto with_viewport = camera.value().with_viewport(width(), height());
    if (with_viewport.has_value())
      camera = std::move(with_viewport);
  }
  auto framed = camera.value().frame_sphere(center, radius, 1.35);
  if (!framed.has_value())
    return;
  camera_ = std::move(framed.value());
  qInfo("MolShredder camera framed: center=(%.3f,%.3f,%.3f) radius=%.3f "
        "distance=%.3f aspect=%.3f",
        center.x, center.y, center.z, radius, camera_->parameters().distance,
        camera_->parameters().aspect_ratio);
  ++camera_revision_;
  update();
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

void MolecularViewport::deliverPickResult(std::uint64_t request_revision,
                                          std::uint64_t packet_revision,
                                          std::uint64_t pick_id) {
  if (request_revision != pick_request_revision_ ||
      packet_revision != packet_revision_) {
    return;
  }
  const auto found = packet_.pick_targets.find(pick_id);
  const std::optional<render::PickTarget> target =
      found == packet_.pick_targets.end()
          ? std::nullopt
          : std::optional<render::PickTarget>{found->second};
  std::vector<std::size_t> atom_indices;
  QString description{QStringLiteral("No molecular target at cursor")};
  QString kind{QStringLiteral("none")};
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

bool MolecularViewport::rebuildScenePacket() {
  const auto *active = workspace_->active_object();
  const auto *active_volume = workspace_->active_volume();
  if (active == nullptr && active_volume == nullptr) {
    setStatus(QStringLiteral("Workspace has no active object"), 0U, 0U);
    return false;
  }
  render::RenderPacket combined;
  combined.provenance.emplace("desktop_composite", "visible-objects-v1");
  std::size_t visible_object_count{};
  for (const auto &object : workspace_->objects()) {
    if (!workspace_->scene()->effectively_visible(object.scene_node))
      continue;
    ++visible_object_count;
    for (const auto &representation : object.representations) {
      append_packet(combined, representation.packet);
    }
  }
  for (const auto &volume : workspace_->volumes()) {
    if (!workspace_->scene()->effectively_visible(volume.scene_node)) continue;
    ++visible_object_count;
    for (const auto &representation : volume.representations) {
      append_packet(combined, representation);
    }
  }
  const auto primitive_count =
      combined.lines.size() + combined.cylinders.size() +
      combined.spheres.size() + combined.mesh_triangles.size();
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
                               QStringLiteral(" · isosurface ") +
                               QString::number(volume_level_, 'g', 6);
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
  packet_ = std::move(packet);
  ++packet_revision_;
  update();
}

} // namespace molshredder::desktop
