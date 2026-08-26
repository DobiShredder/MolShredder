#include "redirected_render_smoke.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>

#include <QCoreApplication>
#include <QUrl>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSize>
#include <rhi/qrhi.h>

#include "viewport_item.hpp"

namespace molshredder::desktop {
namespace {

void report_stage(const char *stage) {
  std::fprintf(stderr, "MolShredder redirected renderer stage: %s\n", stage);
  std::fflush(stderr);
}

std::optional<QSGRendererInterface::GraphicsApi>
graphics_api(const QString &backend) {
  if (backend == QStringLiteral("d3d11"))
    return QSGRendererInterface::Direct3D11;
  if (backend == QStringLiteral("d3d12"))
    return QSGRendererInterface::Direct3D12;
  if (backend == QStringLiteral("metal"))
    return QSGRendererInterface::Metal;
  if (backend == QStringLiteral("vulkan"))
    return QSGRendererInterface::Vulkan;
  if (backend == QStringLiteral("opengl"))
    return QSGRendererInterface::OpenGL;
  return std::nullopt;
}

bool has_pixel_variation(const QByteArray &data) {
  constexpr qsizetype pixel_stride = 4;
  if (data.size() < pixel_stride * 2)
    return false;
  const auto *bytes = reinterpret_cast<const unsigned char *>(data.constData());
  for (qsizetype offset = pixel_stride; offset + pixel_stride <= data.size();
       offset += pixel_stride) {
    if (!std::equal(bytes, bytes + pixel_stride, bytes + offset))
      return true;
  }
  return false;
}

bool backend_matches(const QString &backend, QRhi::Implementation actual) {
  if (backend == QStringLiteral("d3d11"))
    return actual == QRhi::D3D11;
  if (backend == QStringLiteral("d3d12"))
    return actual == QRhi::D3D12;
  if (backend == QStringLiteral("metal"))
    return actual == QRhi::Metal;
  if (backend == QStringLiteral("vulkan"))
    return actual == QRhi::Vulkan;
  if (backend == QStringLiteral("opengl"))
    return actual == QRhi::OpenGLES2;
  return false;
}

std::optional<QByteArray>
render_and_readback(QQuickRenderControl &render_control, QRhi &rhi,
                    QRhiTexture &color_texture, const QSize &output_size) {
  QCoreApplication::processEvents();
  render_control.polishItems();
  render_control.beginFrame();
  render_control.sync();
  render_control.render();

  QRhiReadbackResult readback;
  bool readback_complete = false;
  readback.completed = [&readback_complete] { readback_complete = true; };
  auto *updates = rhi.nextResourceUpdateBatch();
  updates->readBackTexture(QRhiReadbackDescription{&color_texture}, &readback);
  auto *command_buffer = render_control.commandBuffer();
  if (command_buffer == nullptr) {
    qCritical("MolShredder redirected renderer command buffer unavailable");
    render_control.endFrame();
    return std::nullopt;
  }
  command_buffer->resourceUpdate(updates);
  render_control.endFrame();
  if (rhi.finish() != QRhi::FrameOpSuccess) {
    qCritical("MolShredder redirected renderer GPU completion failed");
    return std::nullopt;
  }
  QCoreApplication::processEvents();

  const auto expected_bytes = static_cast<qsizetype>(output_size.width()) *
                              output_size.height() * 4;
  if (!readback_complete || readback.pixelSize != output_size ||
      readback.data.size() < expected_bytes) {
    qCritical("MolShredder redirected renderer readback failed: complete=%d width=%d height=%d bytes=%lld",
              readback_complete, readback.pixelSize.width(),
              readback.pixelSize.height(),
              static_cast<long long>(readback.data.size()));
    return std::nullopt;
  }
  return readback.data;
}

} // namespace

int run_redirected_render_smoke(const RedirectedRenderSmokeOptions &options) {
  report_stage("options-ready");
  const auto api = graphics_api(options.backend);
  if (!api.has_value()) {
    qCritical("Unsupported redirected renderer backend: %s",
              qUtf8Printable(options.backend));
    return EXIT_FAILURE;
  }
  QQuickWindow::setGraphicsApi(api.value());

  constexpr QSize output_size{320, 240};
  QQuickRenderControl render_control;
  QQuickWindow window{&render_control};
  report_stage("render-control-window-ready");
  window.setColor(Qt::black);
  window.setGeometry(0, 0, output_size.width(), output_size.height());
  window.contentItem()->setSize(output_size);

  MolecularViewport viewport{window.contentItem()};
  viewport.setSize(output_size);

  if (options.representation.has_value() &&
      !viewport.setRepresentation(*options.representation)) {
    qCritical("MolShredder redirected renderer representation failed: %s",
              qUtf8Printable(viewport.statusText()));
    return EXIT_FAILURE;
  }
  for (const auto &open_path : options.open_paths) {
    if (!viewport.loadStructure(QUrl::fromLocalFile(open_path))) {
      qCritical("MolShredder redirected renderer structure load failed: %s",
                qUtf8Printable(viewport.statusText()));
      return EXIT_FAILURE;
    }
  }
  if (!options.open_paths.empty() && viewport.atomCount() == 0U) {
    qCritical("MolShredder redirected renderer loaded an empty structure");
    return EXIT_FAILURE;
  }
  if (options.trajectory.has_value() &&
      (!viewport.loadTrajectory(QUrl::fromLocalFile(*options.trajectory),
                                options.trajectory_coordinate_unit,
                                options.trajectory_mapping) ||
       !viewport.waitForTrajectoryTask(10000) || !viewport.hasTrajectory() ||
       viewport.trajectoryFrameCount() < 2U ||
       !viewport.seekTrajectory(1U))) {
    qCritical("MolShredder redirected renderer trajectory load failed: %s",
              qUtf8Printable(viewport.statusText()));
    return EXIT_FAILURE;
  }

  report_stage("scene-data-ready");
  if (!render_control.initialize()) {
    qCritical("MolShredder redirected renderer initialization failed: backend=%s",
              qUtf8Printable(options.backend));
    return EXIT_FAILURE;
  }
  report_stage("render-control-initialized");
  auto *rhi = render_control.rhi();
  if (rhi == nullptr) {
    qCritical("MolShredder redirected renderer did not expose QRhi: backend=%s",
              qUtf8Printable(options.backend));
    return EXIT_FAILURE;
  }
  if (!backend_matches(options.backend, rhi->backend())) {
    qCritical("MolShredder redirected renderer selected the wrong backend: requested=%s actual=%s",
              qUtf8Printable(options.backend), rhi->backendName());
    return EXIT_FAILURE;
  }

  std::unique_ptr<QRhiTexture> color_texture{rhi->newTexture(
      QRhiTexture::RGBA8, output_size, 1,
      QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource)};
  if (!color_texture || !color_texture->create()) {
    qCritical("MolShredder redirected renderer color texture failed");
    return EXIT_FAILURE;
  }
  QRhiTextureRenderTargetDescription target_description{
      QRhiColorAttachment{color_texture.get()}};
  std::unique_ptr<QRhiTextureRenderTarget> render_target{
      rhi->newTextureRenderTarget(target_description)};
  if (!render_target) {
    qCritical("MolShredder redirected renderer target allocation failed");
    return EXIT_FAILURE;
  }
  std::unique_ptr<QRhiRenderPassDescriptor> render_pass{
      render_target->newCompatibleRenderPassDescriptor()};
  render_target->setRenderPassDescriptor(render_pass.get());
  if (!render_target->create()) {
    qCritical("MolShredder redirected renderer target creation failed");
    return EXIT_FAILURE;
  }
  window.setRenderTarget(
      QQuickRenderTarget::fromRhiRenderTarget(render_target.get()));
  report_stage("render-target-ready");
  const auto fail_after_target = [&render_control] {
    render_control.invalidate();
    return EXIT_FAILURE;
  };

  const auto first = render_and_readback(render_control, *rhi, *color_texture,
                                         output_size);
  const auto second = render_and_readback(render_control, *rhi, *color_texture,
                                          output_size);
  report_stage("determinism-frames-ready");
  if (!first.has_value() || !second.has_value() ||
      !has_pixel_variation(first.value()) || first.value() != second.value()) {
    qCritical("MolShredder redirected renderer deterministic readback failed: first=%d second=%d variation=%d equal=%d",
              first.has_value(), second.has_value(),
              first.has_value() && has_pixel_variation(first.value()),
              first.has_value() && second.has_value() &&
                  first.value() == second.value());
    return fail_after_target();
  }

  const auto selection_before_pick = viewport.selectionText();
  viewport.pickAt(static_cast<double>(output_size.width()) * 0.5,
                  static_cast<double>(output_size.height()) * 0.5);
  const auto pick_frame = render_and_readback(render_control, *rhi,
                                              *color_texture, output_size);
  report_stage("pick-frame-ready");
  QCoreApplication::processEvents();
  if (!pick_frame.has_value() || viewport.lastPickId() == 0U ||
      viewport.selectionText() == selection_before_pick) {
    qCritical("MolShredder redirected renderer GPU picking failed: frame=%d id=%llu selection=%s",
              pick_frame.has_value(),
              static_cast<unsigned long long>(viewport.lastPickId()),
              qUtf8Printable(viewport.selectionText()));
    return fail_after_target();
  }

  qInfo("MolShredder redirected renderer ready: requested=%s backend=%s pixels=%dx%d bytes=%lld atoms=%llu frames=%llu frame=%llu variation=true deterministic=true picking=true",
        qUtf8Printable(options.backend), rhi->backendName(),
        output_size.width(), output_size.height(),
        static_cast<long long>(first->size()),
        static_cast<unsigned long long>(viewport.atomCount()),
        static_cast<unsigned long long>(viewport.trajectoryFrameCount()),
        static_cast<unsigned long long>(viewport.trajectoryFrame()));
  render_control.invalidate();
  return EXIT_SUCCESS;
}

} // namespace molshredder::desktop
