#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <QGuiApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QTemporaryDir>
#include <QUrl>
#include <rhi/qrhi.h>

#include "embedded_module.hpp"
#include "localization_controller.hpp"
#include "molshredder/io/structure_reader.hpp"
#include "viewport_item.hpp"

namespace {

std::string graphics_api_name(QSGRendererInterface::GraphicsApi api) {
  switch (api) {
  case QSGRendererInterface::Software:
    return "software";
  case QSGRendererInterface::OpenVG:
    return "openvg";
  case QSGRendererInterface::OpenGL:
    return "opengl";
  case QSGRendererInterface::Direct3D11:
    return "direct3d11";
  case QSGRendererInterface::Vulkan:
    return "vulkan";
  case QSGRendererInterface::Metal:
    return "metal";
  case QSGRendererInterface::Null:
    return "null";
  case QSGRendererInterface::Direct3D12:
    return "direct3d12";
  case QSGRendererInterface::Unknown:
    return "unknown";
  }
  return "unknown";
}

std::string device_type_name(QRhiDriverInfo::DeviceType type) {
  switch (type) {
  case QRhiDriverInfo::IntegratedDevice:
    return "integrated";
  case QRhiDriverInfo::DiscreteDevice:
    return "discrete";
  case QRhiDriverInfo::ExternalDevice:
    return "external";
  case QRhiDriverInfo::VirtualDevice:
    return "virtual";
  case QRhiDriverInfo::CpuDevice:
    return "cpu";
  case QRhiDriverInfo::UnknownDevice:
    return "unknown";
  }
  return "unknown";
}

molshredder::application::GraphicsRuntimeInfo
graphics_runtime_info(QQuickWindow &window) {
  molshredder::application::GraphicsRuntimeInfo result;
  auto *interface = window.rendererInterface();
  const auto api = interface->graphicsApi();
  result.api = graphics_api_name(api);
  result.rhi_based = QSGRendererInterface::isApiRhiBased(api);
  if (api == QSGRendererInterface::Unknown) {
    result.status = molshredder::application::RuntimeStatus::failed;
    result.failure_reason = "Qt Quick did not select a graphics API";
    return result;
  }
  if (!result.rhi_based) {
    result.status = molshredder::application::RuntimeStatus::ready;
    result.backend = result.api;
    result.failure_reason.reset();
    return result;
  }

  auto *rhi = static_cast<QRhi *>(interface->getResource(
      &window, QSGRendererInterface::RhiResource));
  if (rhi == nullptr) {
    result.status = molshredder::application::RuntimeStatus::failed;
    result.failure_reason = "Qt Quick did not expose its QRhi resource";
    return result;
  }
  result.status = molshredder::application::RuntimeStatus::ready;
  result.backend = rhi->backendName();
  const auto driver = rhi->driverInfo();
  if (!driver.deviceName.isEmpty())
    result.device_name = driver.deviceName.toStdString();
  if (driver.deviceId != 0U)
    result.device_id = driver.deviceId;
  if (driver.vendorId != 0U)
    result.vendor_id = driver.vendorId;
  result.device_type = device_type_name(driver.deviceType);
  result.failure_reason.reset();
  return result;
}

}  // namespace

int main(int argc, char *argv[]) {
  molshredder::python::link_embedded_module();
  QGuiApplication application{argc, argv};
  QCoreApplication::setApplicationName(QStringLiteral("MolShredder"));
  QCoreApplication::setOrganizationName(QStringLiteral("MolShredder"));

  bool smoke = false;
  bool camera_smoke = false;
  bool picking_smoke = false;
  bool object_smoke = false;
  bool batch_load_smoke = false;
  bool trajectory_smoke = false;
  bool amber_smoke = false;
  bool amber_mdcrd_smoke = false;
  bool amber_netcdf_smoke = false;
  bool h5md_smoke = false;
  bool lammps_smoke = false;
  bool binpos_smoke = false;
  bool save_all = false;
  bool save_smoke = false;
  bool script_smoke = false;
  bool script_cancel_smoke = false;
  bool graphics_info_smoke = false;
  bool system_info_panel_smoke = false;
  bool named_view_smoke = false;
  bool stereo_smoke = false;
  bool anaglyph_smoke = false;
  bool interleaved_smoke = false;
  bool representation_visibility_smoke = false;
  bool render_setting_smoke = false;
  bool analysis_smoke = false;
  bool daily_workflow_smoke = false;
  std::optional<QString> screenshot;
  std::vector<QString> open_paths;
  std::optional<QString> representation;
  std::optional<QString> trajectory;
  QString trajectory_coordinate_unit{QStringLiteral("angstrom")};
  QString trajectory_mapping{QStringLiteral("index")};
  std::optional<QString> save_path;
  std::optional<QString> script_path;
  QString language;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--smoke") {
      smoke = true;
    } else if (argument == "--camera-smoke") {
      camera_smoke = true;
    } else if (argument == "--picking-smoke") {
      picking_smoke = true;
    } else if (argument == "--object-smoke") {
      object_smoke = true;
    } else if (argument == "--batch-load-smoke") {
      batch_load_smoke = true;
    } else if (argument == "--trajectory-smoke") {
      trajectory_smoke = true;
    } else if (argument == "--amber-smoke") {
      amber_smoke = true;
    } else if (argument == "--amber-mdcrd-smoke") {
      amber_mdcrd_smoke = true;
    } else if (argument == "--amber-netcdf-smoke") {
      amber_netcdf_smoke = true;
    } else if (argument == "--h5md-smoke") {
      h5md_smoke = true;
    } else if (argument == "--lammps-smoke") {
      lammps_smoke = true;
    } else if (argument == "--binpos-smoke") {
      binpos_smoke = true;
    } else if (argument == "--save-all") {
      save_all = true;
    } else if (argument == "--save-smoke") {
      save_smoke = true;
    } else if (argument == "--script-smoke") {
      script_smoke = true;
    } else if (argument == "--script-cancel-smoke") {
      script_cancel_smoke = true;
    } else if (argument == "--graphics-info-smoke") {
      graphics_info_smoke = true;
    } else if (argument == "--system-info-panel-smoke") {
      system_info_panel_smoke = true;
    } else if (argument == "--named-view-smoke") {
      named_view_smoke = true;
    } else if (argument == "--stereo-smoke") {
      stereo_smoke = true;
    } else if (argument == "--anaglyph-smoke") {
      anaglyph_smoke = true;
    } else if (argument == "--interleaved-smoke") {
      interleaved_smoke = true;
    } else if (argument == "--representation-visibility-smoke") {
      representation_visibility_smoke = true;
    } else if (argument == "--render-setting-smoke") {
      render_setting_smoke = true;
    } else if (argument == "--analysis-smoke") {
      analysis_smoke = true;
    } else if (argument == "--daily-workflow-smoke") {
      daily_workflow_smoke = true;
      smoke = true;
    } else if (argument.starts_with("--screenshot=")) {
      const auto path =
          argument.substr(std::string_view{"--screenshot="}.size());
      screenshot =
          QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
    } else if (argument.starts_with("--open=")) {
      const auto path = argument.substr(std::string_view{"--open="}.size());
      open_paths.push_back(
          QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size())));
    } else if (argument == "--open" && index + 1 < argc) {
      ++index;
      open_paths.push_back(QString::fromUtf8(argv[index]));
    } else if (argument.starts_with("--representation=")) {
      const auto value =
          argument.substr(std::string_view{"--representation="}.size());
      representation =
          QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    } else if (argument.starts_with("--trajectory=")) {
      const auto path =
          argument.substr(std::string_view{"--trajectory="}.size());
      trajectory =
          QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
    } else if (argument.starts_with("--trajectory-unit=")) {
      const auto value =
          argument.substr(std::string_view{"--trajectory-unit="}.size());
      trajectory_coordinate_unit =
          QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    } else if (argument.starts_with("--trajectory-mapping=")) {
      const auto value =
          argument.substr(std::string_view{"--trajectory-mapping="}.size());
      trajectory_mapping =
          QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    } else if (argument.starts_with("--save=")) {
      const auto path = argument.substr(std::string_view{"--save="}.size());
      save_path =
          QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
    } else if (argument.starts_with("--script=")) {
      const auto path = argument.substr(std::string_view{"--script="}.size());
      script_path =
          QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
    } else if (argument.starts_with("--language=")) {
      const auto value = argument.substr(std::string_view{"--language="}.size());
      language =
          QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    }
  }

  // Keep regression output deterministic regardless of the runner locale or
  // a developer's persisted preference. Localization smoke tests opt in to
  // their language explicitly.
  if (smoke && language.isEmpty()) language = QStringLiteral("en");
  molshredder::desktop::LocalizationController localization;
  if (!localization.applyInitialLanguage(language)) {
    qCritical("Unsupported UI language. Use en, ko, or system.");
    return EXIT_FAILURE;
  }
  QQmlApplicationEngine engine;
  localization.setEngine(&engine);
  engine.rootContext()->setContextProperty(QStringLiteral("localization"),
                                           &localization);
  engine.load(QUrl{QStringLiteral("qrc:/Main.qml")});
  if (engine.rootObjects().isEmpty())
    return EXIT_FAILURE;
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  if (window == nullptr)
    return EXIT_FAILURE;
  qInfo("MolShredder localization ready: language=%s title=%s",
        qUtf8Printable(localization.currentLanguage()),
        qUtf8Printable(window->title()));
  QObject::connect(
      window, &QQuickWindow::sceneGraphInitialized, window,
      [window] {
        qInfo("MolShredder graphics API: %d",
              static_cast<int>(window->rendererInterface()->graphicsApi()));
      },
      Qt::DirectConnection);
  auto *viewport = window->findChild<molshredder::desktop::MolecularViewport *>(
      QStringLiteral("molecularViewport"));
  if (viewport == nullptr)
    return EXIT_FAILURE;
  const auto seek_and_wait = [viewport](qulonglong frame) {
    return viewport->seekTrajectory(frame) &&
           viewport->waitForTrajectoryTask(5000) &&
           viewport->trajectoryFrame() == frame;
  };
  QPointer<molshredder::desktop::MolecularViewport> viewport_guard{viewport};
  QObject::connect(
      window, &QQuickWindow::sceneGraphInitialized, window,
      [window, viewport_guard] {
        auto info = graphics_runtime_info(*window);
        if (viewport_guard.isNull())
          return;
        QMetaObject::invokeMethod(
            viewport_guard.data(),
            [viewport_guard, info = std::move(info)]() mutable {
              if (!viewport_guard.isNull())
                viewport_guard->setGraphicsRuntimeInfo(std::move(info));
            },
            Qt::QueuedConnection);
      },
      Qt::DirectConnection);
  QObject::connect(
      window, &QQuickWindow::sceneGraphInvalidated, window,
      [viewport_guard] {
        molshredder::application::GraphicsRuntimeInfo info;
        info.status =
            molshredder::application::RuntimeStatus::not_initialized;
        info.failure_reason = "Qt Quick scene graph was invalidated";
        if (viewport_guard.isNull())
          return;
        QMetaObject::invokeMethod(
            viewport_guard.data(),
            [viewport_guard, info = std::move(info)]() mutable {
              if (!viewport_guard.isNull())
                viewport_guard->setGraphicsRuntimeInfo(std::move(info));
            },
            Qt::QueuedConnection);
      },
      Qt::DirectConnection);
  QObject::connect(
      window, &QQuickWindow::sceneGraphError, viewport,
      [viewport](QQuickWindow::SceneGraphError, const QString &message) {
        molshredder::application::GraphicsRuntimeInfo info;
        info.status = molshredder::application::RuntimeStatus::failed;
        info.failure_reason = message.toStdString();
        viewport->setGraphicsRuntimeInfo(std::move(info));
      });
  if (graphics_info_smoke || system_info_panel_smoke) {
    QObject::connect(
        viewport,
        &molshredder::desktop::MolecularViewport::graphicsDiagnosticsChanged,
        &application,
        [viewport, window, system_info_panel_smoke, &application] {
          const auto info = viewport->systemInfoJson();
          const auto passed =
              info.contains(QStringLiteral("\"status\":\"ready\"")) &&
              info.contains(QStringLiteral("\"rhi_based\":true")) &&
              !info.contains(QStringLiteral("\"device_name\":null"));
          if (!passed) {
            qCritical("MolShredder desktop graphics diagnostics smoke failed: "
                      "%s",
                      info.toUtf8().constData());
            application.exit(EXIT_FAILURE);
            return;
          }
          if (system_info_panel_smoke) {
            const auto invoked = QMetaObject::invokeMethod(
                window, "openSystemInfo", Qt::DirectConnection);
            auto *overlay = window->findChild<QObject *>(
                QStringLiteral("systemInfoOverlay"));
            auto *summary = window->findChild<QObject *>(
                QStringLiteral("systemInfoSummary"));
            const auto panel_source =
                window->property("systemInfoPanelSourceJson").toString();
            const auto panel_passed =
                invoked && overlay != nullptr && summary != nullptr &&
                overlay->property("visible").toBool() && panel_source == info &&
                summary->property("text")
                    .toString()
                    .contains(QStringLiteral("canonical system info operation"));
            if (!panel_passed) {
              qCritical("MolShredder system information panel smoke failed");
              application.exit(EXIT_FAILURE);
              return;
            }
            qInfo("MolShredder system information panel ready: typed-source=exact");
          }
          qInfo("MolShredder desktop graphics diagnostics ready: %s",
                info.toUtf8().constData());
          application.quit();
        });
    QTimer::singleShot(5000, &application, [&application] {
      qCritical("MolShredder desktop system diagnostics timed out");
      application.exit(EXIT_FAILURE);
    });
  }
  if (representation.has_value() &&
      !viewport->setRepresentation(*representation)) {
    qCritical("%s", viewport->statusText().toUtf8().constData());
    return EXIT_FAILURE;
  }
  if (batch_load_smoke) {
    QVariantList urls;
    for (const auto &open_path : open_paths)
      urls.push_back(QUrl::fromLocalFile(open_path));
    if (!viewport->loadStructures(urls) || viewport->objectItems().size() != 2) {
      qCritical("MolShredder desktop atomic batch load smoke failed: %s",
                viewport->statusText().toUtf8().constData());
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop atomic batch load ready: inputs=2 objects=2 canonical=shared");
  } else {
    for (const auto &open_path : open_paths) {
      if (!viewport->loadStructure(QUrl::fromLocalFile(open_path))) {
        qCritical("%s", viewport->statusText().toUtf8().constData());
        return EXIT_FAILURE;
      }
    }
  }
  if (trajectory.has_value() &&
      (!viewport->loadTrajectory(QUrl::fromLocalFile(*trajectory),
                                 trajectory_coordinate_unit,
                                 trajectory_mapping) ||
       !viewport->waitForTrajectoryTask(10000))) {
    qCritical("%s", viewport->statusText().toUtf8().constData());
    return EXIT_FAILURE;
  }
  if (camera_smoke || analysis_smoke) {
    // QML object creation is synchronous, but the first layout pass is not.
    // Exercise viewport-dependent actions only after Qt has assigned its size;
    // otherwise a fast headless runner can observe a zero-height viewport.
    QEventLoop layout_wait;
    QTimer::singleShot(50, &layout_wait, &QEventLoop::quit);
    layout_wait.exec();
  }
  if (representation_visibility_smoke) {
    const auto passed =
        viewport->applyRepresentationVisibility(QStringLiteral("show"),
                                                QStringLiteral("index 1")) &&
        viewport->applyRepresentationVisibility(QStringLiteral("hide"),
                                                QStringLiteral("index 1")) &&
        viewport->applyRepresentationVisibility(QStringLiteral("as"),
                                                QStringLiteral("index 2")) &&
        viewport->applyRepresentationVisibility(QStringLiteral("toggle"),
                                                QStringLiteral("index 2"));
    if (!passed) {
      qCritical("MolShredder desktop representation visibility smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop representation visibility ready: operations=4 canonical=shared");
  }
  if (render_setting_smoke) {
    const auto passed =
        viewport->applyRenderSetting(QStringLiteral("set"),
                                     QStringLiteral("sphere_scale"),
                                     QStringLiteral("2.5"),
                                     QStringLiteral("atom"),
                                     QStringLiteral("1")) &&
        viewport->applyRenderSetting(QStringLiteral("set"),
                                     QStringLiteral("sphere_transparency"),
                                     QStringLiteral("0.5"),
                                     QStringLiteral("atom"),
                                     QStringLiteral("1")) &&
        viewport->applyRenderSetting(QStringLiteral("set"),
                                     QStringLiteral("sphere_color"),
                                     QStringLiteral("oxygen"),
                                     QStringLiteral("atom"),
                                     QStringLiteral("1"));
    const auto query = viewport->renderSettingJson(
        QStringLiteral("sphere_scale"), QStringLiteral("atom"),
        QStringLiteral("1"));
    const auto &packet = viewport->renderPacket();
    const auto geometry_passed =
        !packet.spheres.empty() && packet.spheres.front().radius > 3.0 &&
        packet.spheres.front().color.red > 0.9F &&
        packet.spheres.front().color.alpha == 0.5F;
    const auto opened = QMetaObject::invokeMethod(
        window, "openRenderSettings", Qt::DirectConnection);
    auto *overlay = window->findChild<QObject *>(
        QStringLiteral("renderSettingsOverlay"));
    auto *result = window->findChild<QObject *>(
        QStringLiteral("renderSettingResult"));
    if (!passed || !geometry_passed ||
        !query.contains(QStringLiteral("\"value\":2.5")) ||
        !query.contains(QStringLiteral("\"source_scope\":\"atom\"")) ||
        !opened || overlay == nullptr || result == nullptr ||
        !overlay->property("visible").toBool()) {
      qCritical("MolShredder desktop render setting smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop render setting ready: typed=shared geometry=updated editor=visible");
  }
  if (analysis_smoke) {
    const auto center = viewport->analyzeCenter(
        QStringLiteral("all"), QStringLiteral("com"),
        QStringLiteral("desktop-com"));
    const auto distance = viewport->analyzeDistance(
        QStringLiteral("index 1"), QStringLiteral("index 2"),
        QStringLiteral("raw"), QStringLiteral("desktop-distance"));
    const auto detail = viewport->analysisResultJson(2U);
    const auto opened = QMetaObject::invokeMethod(
        window, "openAnalyze", Qt::DirectConnection);
    auto *overlay =
        window->findChild<QObject *>(QStringLiteral("analysisOverlay"));
    const auto before_hide = viewport->renderPacket();
    const auto hidden = viewport->setAnalysisResultVisible(2U, false);
    const auto &after_hide = viewport->renderPacket();
    const auto passed =
        center && distance && viewport->analysisItems().size() == 2U &&
        viewport->analysisLabelItems().size() == 1U &&
        before_hide.labels.size() == 2U && before_hide.lines.size() >= 8U &&
        after_hide.labels.size() == 1U && hidden &&
        detail.contains(QStringLiteral("molshredder-distance-v1")) &&
        detail.contains(QStringLiteral("source_status")) && opened &&
        overlay != nullptr && overlay->property("visible").toBool();
    if (!passed) {
      qCritical("MolShredder desktop analysis result smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop analysis ready: results=2 overlay=marker-dash-label canonical=shared panel=visible");
  }
  if (save_path.has_value() &&
      !viewport->saveStructure(QUrl::fromLocalFile(*save_path), save_all)) {
    qCritical("%s", viewport->statusText().toUtf8().constData());
    return EXIT_FAILURE;
  }
  if ((script_smoke || script_cancel_smoke) && !script_path.has_value()) {
    qCritical("Desktop script smoke requires --script");
    return EXIT_FAILURE;
  }
  if (script_smoke || script_cancel_smoke) {
    QObject::connect(
        viewport, &molshredder::desktop::MolecularViewport::scriptFinished,
        &application,
        [viewport, script_smoke, script_cancel_smoke,
         &application](bool succeeded) {
          if (script_smoke) {
            const auto passed = succeeded && !viewport->scriptRunning() &&
                                viewport->objectItems().size() == 1 &&
                                viewport->scriptOutput().contains(
                                    QStringLiteral("gui-script-output")) &&
                                viewport->atomCount() == 3U;
            if (!passed) {
              qCritical("MolShredder desktop Python script smoke failed");
              application.exit(EXIT_FAILURE);
              return;
            }
            qInfo("MolShredder desktop Python script ready: objects=1 atoms=3 "
                  "output=captured async=true");
          } else if (script_cancel_smoke) {
            const auto passed = !succeeded && !viewport->scriptRunning() &&
                                viewport->statusText().contains(
                                    QStringLiteral("cancelled")) &&
                                viewport->scriptOutput().contains(
                                    QStringLiteral("cancel-smoke-started"));
            if (!passed) {
              qCritical("MolShredder desktop Python script cancellation smoke "
                        "failed");
              application.exit(EXIT_FAILURE);
              return;
            }
            qInfo("MolShredder desktop Python script cancellation ready: "
                  "checkpoint=post-execution output=captured");
          }
          application.quit();
        });
    QTimer::singleShot(5000, &application, [&application] {
      qCritical("MolShredder desktop Python script smoke timed out");
      application.exit(EXIT_FAILURE);
    });
  }
  if (script_path.has_value() &&
      !viewport->runPythonScript(QUrl::fromLocalFile(*script_path))) {
    qCritical("%s", viewport->statusText().toUtf8().constData());
    return EXIT_FAILURE;
  }
  if (script_cancel_smoke) {
    QTimer::singleShot(
        50, viewport,
        &molshredder::desktop::MolecularViewport::cancelPythonScript);
  }
  if (save_smoke) {
    if (!save_path.has_value())
      return EXIT_FAILURE;
    const auto saved =
        molshredder::io::read_structure_file(save_path->toStdString());
    const auto expected_frames =
        saved.has_value() &&
                saved.value().format == molshredder::io::StructureFormat::psf
            ? 0U
        : save_all ? 2U
                   : 1U;
    if (!saved.has_value() || saved.value().structures.size() != 1U ||
        saved.value().structures.front().topology->atom_count() == 0U ||
        saved.value().structures.front().coordinates->frame_count() !=
            expected_frames) {
      qCritical("MolShredder desktop structure save smoke failed");
      return EXIT_FAILURE;
    }
    const auto atom_count =
        saved.value().structures.front().topology->atom_count();
    auto format_name =
        std::string{molshredder::io::to_string(saved.value().format)};
    std::transform(format_name.begin(), format_name.end(), format_name.begin(),
                   [](unsigned char value) {
                     return static_cast<char>(std::toupper(value));
                   });
    qInfo("MolShredder desktop %s save ready: atoms=%llu frames=%llu "
          "loss-report=true",
          format_name.c_str(), static_cast<unsigned long long>(atom_count),
          static_cast<unsigned long long>(expected_frames));
  }
  if (object_smoke) {
    const auto before = viewport->objectItems();
    const auto composed_spheres = viewport->renderPacket().spheres.size();
    if (before.size() != 2 || composed_spheres != 4U ||
        !viewport->activateObject(1U) ||
        !viewport->setObjectVisible(2U, false) ||
        !viewport->renameObject(2U, QStringLiteral("renamed")) ||
        !viewport->reorderObject(2U, 1U) || !viewport->deleteObject(1U)) {
      qCritical("MolShredder desktop object panel smoke failed");
      return EXIT_FAILURE;
    }
    const auto after = viewport->objectItems();
    const auto first = after[0].toMap();
    if (after.size() != 1 ||
        first.value(QStringLiteral("id")).toULongLong() != 2U ||
        first.value(QStringLiteral("name")).toString() !=
            QStringLiteral("renamed") ||
        !first.value(QStringLiteral("active")).toBool() ||
        first.value(QStringLiteral("visible")).toBool() ||
        viewport->renderPacket().spheres.size() != 0U) {
      qCritical("MolShredder desktop object state smoke failed: count=%lld id=%llu name=%s active=%d visible=%d spheres=%llu",
                static_cast<long long>(after.size()),
                first.value(QStringLiteral("id")).toULongLong(),
                first.value(QStringLiteral("name")).toString().toUtf8().constData(),
                first.value(QStringLiteral("active")).toBool(),
                first.value(QStringLiteral("visible")).toBool(),
                static_cast<unsigned long long>(viewport->renderPacket().spheres.size()));
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop objects ready: objects=1 active=2 visible=0 "
          "rename-delete-reorder=true spheres=0");
  }
  if (camera_smoke) {
    const auto *initial = viewport->camera();
    if (initial == nullptr)
      return EXIT_FAILURE;
    const auto initial_parameters = initial->parameters();
    viewport->orbit(30.0, -15.0);
    const auto orbit_parameters = viewport->camera()->parameters();
    viewport->pan(12.0, 8.0);
    const auto pan_parameters = viewport->camera()->parameters();
    viewport->dolly(-1.0);
    const auto dolly_parameters = viewport->camera()->parameters();
    if (orbit_parameters.orientation == initial_parameters.orientation ||
        pan_parameters.target == orbit_parameters.target ||
        dolly_parameters.distance >= pan_parameters.distance) {
      qCritical("MolShredder desktop camera interaction smoke failed");
      return EXIT_FAILURE;
    }
    const auto centered = viewport->centerSelection(
        QStringLiteral("index 1"), false, QStringLiteral("current"), 0.0, 1);
    const auto origin =
        viewport->setOriginSelection(QStringLiteral("all"), QStringLiteral("1"));
    const auto coordinate_origin = viewport->setOriginPosition(
        1.5, -2.0, 3.25, QString{});
    const auto object_origin = viewport->setObjectOriginSelection(
        QStringLiteral("current"), QStringLiteral("all"),
        QStringLiteral("current"));
    const auto object_reset =
        viewport->resetObjectTransform(QStringLiteral("current"));
    const auto projection_span_before =
        viewport->camera()->vertical_span_at_target();
    const auto projected = viewport->setProjection(
        QStringLiteral("orthographic"), 60.0, true);
    const auto projection_parameters = viewport->camera()->parameters();
    const auto projection_span_after =
        viewport->camera()->vertical_span_at_target();
    const auto zoomed = viewport->zoomSelection(
        QStringLiteral("all"), 0.0, true, QStringLiteral("all"), 0.0, 1);
    const auto oriented = viewport->orientSelection(
        QStringLiteral("all"), QStringLiteral("all"), 0.0, 1);
    const auto clipped = viewport->clipCamera(
        QStringLiteral("atoms"), 1.0, QStringLiteral("all"),
        QStringLiteral("all"));
    const auto moved =
        viewport->moveCamera(QStringLiteral("x"), 1.0);
    const auto turned =
        viewport->turnCamera(QStringLiteral("y"), 15.0);
    const auto reset = viewport->resetViewAnimated(0.0, 1);
    if (!centered || !origin || !coordinate_origin || !object_origin ||
        !object_reset || !projected || !zoomed || !oriented || !clipped || !moved ||
        !turned || !reset ||
        projection_parameters.projection !=
            molshredder::scene::ProjectionMode::orthographic ||
        std::abs(projection_span_after - projection_span_before) > 1.0e-10) {
      qCritical("MolShredder desktop shared camera action smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop camera ready: representation=%s atoms=%llu",
          viewport->representation().toUtf8().constData(),
          static_cast<unsigned long long>(viewport->atomCount()));
  }
  if (named_view_smoke) {
    viewport->orbit(18.0, -11.0);
    const auto stored_camera = viewport->camera()->parameters();
    const auto stored = viewport->storeNamedView(QStringLiteral("smoke view"));
    viewport->orbit(-42.0, 3.0);
    const auto recalled = viewport->recallNamedViewAnimated(
        QStringLiteral("smoke view"), 0.05, 1);
    QEventLoop recall_wait;
    QTimer::singleShot(120, &recall_wait, &QEventLoop::quit);
    recall_wait.exec();
    const auto recall_exact =
        recalled && viewport->camera()->parameters() == stored_camera;
    const auto invoked =
        QMetaObject::invokeMethod(window, "openViews", Qt::DirectConnection);
    auto *overlay =
        window->findChild<QObject *>(QStringLiteral("viewsOverlay"));
    auto *pymol_input =
        window->findChild<QObject *>(QStringLiteral("pymolViewInput"));
    auto *camera_selection_input =
        window->findChild<QObject *>(QStringLiteral("cameraSelectionInput"));
    auto *camera_state_mode_button =
        window->findChild<QObject *>(QStringLiteral("cameraStateModeButton"));
    auto *camera_state_input =
        window->findChild<QObject *>(QStringLiteral("cameraStateInput"));
    auto *camera_orient_button =
        window->findChild<QObject *>(QStringLiteral("cameraOrientButton"));
    auto *object_origin_input =
        window->findChild<QObject *>(QStringLiteral("objectOriginInput"));
    auto *object_origin_x =
        window->findChild<QObject *>(QStringLiteral("objectOriginX"));
    auto *projection_mode_button =
        window->findChild<QObject *>(QStringLiteral("projectionModeButton"));
    auto *projection_fov_input =
        window->findChild<QObject *>(QStringLiteral("projectionFovInput"));
    auto *projection_preserve_button = window->findChild<QObject *>(
        QStringLiteral("projectionPreserveButton"));
    auto *stereo_enabled_button = window->findChild<QObject *>(
        QStringLiteral("stereoEnabledButton"));
    auto *stereo_mode_button =
        window->findChild<QObject *>(QStringLiteral("stereoModeButton"));
    auto *stereo_swap_button =
        window->findChild<QObject *>(QStringLiteral("stereoSwapButton"));
    auto *stereo_shift_input =
        window->findChild<QObject *>(QStringLiteral("stereoShiftInput"));
    auto *stereo_angle_input =
        window->findChild<QObject *>(QStringLiteral("stereoAngleInput"));
    auto *stereo_apply_button =
        window->findChild<QObject *>(QStringLiteral("stereoApplyButton"));
    auto *clip_mode_button =
        window->findChild<QObject *>(QStringLiteral("clipModeButton"));
    auto *clip_distance_input =
        window->findChild<QObject *>(QStringLiteral("clipDistanceInput"));
    auto *clip_selection_input =
        window->findChild<QObject *>(QStringLiteral("clipSelectionInput"));
    auto *navigation_axis_button =
        window->findChild<QObject *>(QStringLiteral("navigationAxisButton"));
    auto *navigation_step_input =
        window->findChild<QObject *>(QStringLiteral("navigationStepInput"));
    const auto pymol_export = viewport->pymolViewText();
    const auto clip_range = viewport->clipRangeText();
    const auto pymol_import = viewport->importPymolViewAnimated(
        QStringLiteral(
            "0,1,0,-1,0,0,0,0,1,2,-3,-40,10,20,30,.5,100,-60"),
        0.05, -1);
    QEventLoop import_wait;
    QTimer::singleShot(120, &import_wait, &QEventLoop::quit);
    import_wait.exec();
    const auto pymol_camera = viewport->camera()->parameters();
    if (!stored || !recall_exact || stored_camera == pymol_camera ||
        viewport->viewItems().size() != 1 ||
        !invoked || overlay == nullptr || pymol_input == nullptr ||
        camera_orient_button == nullptr || object_origin_input == nullptr ||
        object_origin_x == nullptr || projection_mode_button == nullptr ||
        projection_fov_input == nullptr ||
        projection_preserve_button == nullptr ||
        stereo_enabled_button == nullptr || stereo_mode_button == nullptr ||
        stereo_swap_button == nullptr || stereo_shift_input == nullptr ||
        stereo_angle_input == nullptr || stereo_apply_button == nullptr ||
        camera_selection_input == nullptr ||
        camera_state_mode_button == nullptr || camera_state_input == nullptr ||
        clip_mode_button == nullptr || clip_distance_input == nullptr ||
        clip_selection_input == nullptr ||
        navigation_axis_button == nullptr || navigation_step_input == nullptr ||
        !overlay->property("visible").toBool() || pymol_export.isEmpty() ||
        !clip_range.startsWith(QStringLiteral("near ")) ||
        !pymol_import ||
        pymol_camera.target != molshredder::model::Vec3d{13.0, 22.0, 30.0} ||
        pymol_camera.model_origin !=
            molshredder::model::Vec3d{10.0, 20.0, 30.0}) {
      qCritical("MolShredder desktop named view smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop named view ready: stored=1 "
          "recalled=animated-exact panel=visible "
          "pymol18=animated-roundtrip");
  }
  if (stereo_smoke) {
    const auto enabled = viewport->setStereo(
        true, QStringLiteral("side_by_side"), false, 2.0, 2.1,
        QStringLiteral("optimized"));
    if (!enabled || !viewport->stereoEnabled() ||
        viewport->stereoModeText() != QStringLiteral("side_by_side") ||
        viewport->stereoSwapEyes() ||
        std::abs(viewport->stereoShiftPercent() - 2.0) > 1.0e-12 ||
        std::abs(viewport->stereoAngleScale() - 2.1) > 1.0e-12) {
      qCritical("MolShredder desktop stereo smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop stereo ready: mode=side_by_side eyes=2 "
          "shift=2.0 angle=2.1");
    if (!screenshot.has_value())
      QTimer::singleShot(1000, &application, &QCoreApplication::quit);
  }
  if (anaglyph_smoke) {
    const auto enabled = viewport->setStereo(
        true, QStringLiteral("anaglyph"), false, 2.0, 2.1,
        QStringLiteral("optimized"));
    if (!enabled || !viewport->stereoEnabled() ||
        viewport->stereoModeText() != QStringLiteral("anaglyph") ||
        viewport->anaglyphModeText() != QStringLiteral("optimized") ||
        viewport->stereoSwapEyes()) {
      qCritical("MolShredder desktop anaglyph smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop anaglyph ready: mode=optimized eyes=2 compositor=QRhi");
    if (!screenshot.has_value())
      QTimer::singleShot(1000, &application, &QCoreApplication::quit);
  }
  if (interleaved_smoke) {
    const auto row_enabled = viewport->setStereo(
        true, QStringLiteral("row_interleaved"), false, 2.0, 2.1,
        QStringLiteral("optimized"));
    const auto column_enabled = viewport->setStereo(
        true, QStringLiteral("column_interleaved"), false, 2.0, 2.1,
        QStringLiteral("optimized"));
    const auto checkerboard_enabled = viewport->setStereo(
        true, QStringLiteral("checkerboard"), false, 2.0, 2.1,
        QStringLiteral("optimized"));
    if (!row_enabled || !column_enabled || !checkerboard_enabled ||
        !viewport->stereoEnabled() ||
        viewport->stereoModeText() != QStringLiteral("checkerboard") ||
        viewport->stereoSwapEyes()) {
      qCritical("MolShredder desktop interleaved smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop interleaved ready: modes=3 final=checkerboard eyes=2 compositor=QRhi");
    if (!screenshot.has_value())
      QTimer::singleShot(1000, &application, &QCoreApplication::quit);
  }
  if (picking_smoke) {
    const auto capture_requested = screenshot.has_value();
    QObject::connect(
        viewport, &molshredder::desktop::MolecularViewport::selectionChanged,
        &application, [viewport, &application, capture_requested] {
          if (viewport->selectionText().startsWith(QStringLiteral("Atom "))) {
            qInfo("MolShredder desktop GPU picking ready: %s",
                  viewport->selectionText().toUtf8().constData());
            if (!capture_requested)
              application.quit();
          }
        });
    QTimer::singleShot(750, viewport, [viewport] {
      viewport->pickAt(viewport->width() * 0.5, viewport->height() * 0.5);
    });
    QTimer::singleShot(5000, &application, [&application] {
      qCritical("MolShredder desktop GPU picking smoke timed out");
      application.exit(EXIT_FAILURE);
    });
  }
  if (trajectory_smoke) {
    if (!viewport->hasTrajectory() || viewport->trajectoryFrameCount() != 4U ||
        !viewport->setPlaybackMode(QStringLiteral("loop")) ||
        !viewport->setPlaybackDirection(QStringLiteral("forward")) ||
        !viewport->setTrajectoryFps(5.0) || !seek_and_wait(3U) ||
        !viewport->setTrajectoryPlaying(true) ||
        !viewport->tickTrajectory(200.0) || viewport->trajectoryFrame() != 0U ||
        !viewport->setTrajectoryPlaying(false) ||
        !viewport->setPlaybackMode(QStringLiteral("rock")) ||
        !seek_and_wait(3U) ||
        !viewport->setPlaybackDirection(QStringLiteral("forward")) ||
        !viewport->setTrajectoryPlaying(true) ||
        !viewport->tickTrajectory(200.0) || viewport->trajectoryFrame() != 2U ||
        viewport->playbackDirection() != QStringLiteral("reverse") ||
        !viewport->setTrajectoryPlaying(false) ||
        !viewport->setPlaybackMode(QStringLiteral("loop")) ||
        !viewport->setTrajectoryFps(30.0) || !seek_and_wait(0U)) {
      qCritical("MolShredder desktop trajectory deterministic smoke failed");
      return EXIT_FAILURE;
    }
    const auto *task_progress = window->findChild<QObject *>(
        QStringLiteral("trajectoryTaskProgress"));
    const auto *task_cancel = window->findChild<QObject *>(
        QStringLiteral("trajectoryTaskCancelButton"));
    if (!viewport->seekTrajectory(1U) || !viewport->seekTrajectory(2U) ||
        !viewport->seekTrajectory(3U) ||
        !viewport->waitForTrajectoryTask(5000) ||
        viewport->trajectoryFrame() != 3U ||
        viewport->packetUpdateMode() != QStringLiteral("incremental") ||
        task_progress == nullptr || task_cancel == nullptr) {
      qCritical("MolShredder desktop rapid latest-wins seek smoke failed");
      return EXIT_FAILURE;
    }
    if (!viewport->setRepresentation(QStringLiteral("sticks")) ||
        viewport->packetUpdateMode() != QStringLiteral("full") ||
        !viewport->setRepresentation(QStringLiteral("spheres")) ||
        !seek_and_wait(3U) ||
        viewport->packetUpdateMode() != QStringLiteral("incremental")) {
      qCritical("MolShredder desktop trajectory buffer fallback smoke failed");
      return EXIT_FAILURE;
    }
    if (!viewport->seekTrajectory(0U)) return EXIT_FAILURE;
    viewport->cancelTrajectoryTask();
    if (viewport->trajectoryFrame() != 3U ||
        viewport->trajectoryTaskStage() != QStringLiteral("cancelled") ||
        !seek_and_wait(0U)) {
      qCritical("MolShredder desktop trajectory cancellation smoke failed");
      return EXIT_FAILURE;
    }
    const auto observed_frame_change = std::make_shared<bool>(false);
    QObject::connect(
        viewport, &molshredder::desktop::MolecularViewport::trajectoryChanged,
        &application, [viewport, observed_frame_change] {
          if (viewport->trajectoryFrame() != 0U) {
            *observed_frame_change = true;
          }
        });
    if (!viewport->setTrajectoryPlaying(true))
      return EXIT_FAILURE;
    QTimer::singleShot(
        750, &application, [viewport, observed_frame_change, &application] {
          const auto passed = *observed_frame_change &&
                              viewport->trajectoryFrame() < 4U &&
                              viewport->setTrajectoryPlaying(false);
          if (!passed) {
            qCritical("MolShredder desktop trajectory scheduler smoke failed");
            application.exit(EXIT_FAILURE);
            return;
          }
          qInfo("MolShredder desktop trajectory ready: frames=4 scheduler=qt "
                "mode=loop");
          application.quit();
        });
    QTimer::singleShot(5000, &application, [&application] {
      qCritical("MolShredder desktop trajectory smoke timed out");
      application.exit(EXIT_FAILURE);
    });
  }
  if (daily_workflow_smoke) {
    QTemporaryDir output_directory;
    const auto initial_frame = viewport->trajectoryFrame();
    const auto invalid_seek_preserved =
        !viewport->seekTrajectory(viewport->trajectoryFrameCount()) &&
        viewport->trajectoryFrame() == initial_frame;
    const auto prepared_view =
        output_directory.isValid() && viewport->hasTrajectory() &&
        viewport->trajectoryFrameCount() == 4U &&
        viewport->setRepresentation(QStringLiteral("sticks")) &&
        viewport->applyRepresentationVisibility(QStringLiteral("as"),
                                                QStringLiteral("index 1")) &&
        viewport->applyRepresentationVisibility(QStringLiteral("show"),
                                                QStringLiteral("all"));
    const auto pick = viewport->renderPacket().pick_targets.begin();
    if (prepared_view && pick != viewport->renderPacket().pick_targets.end())
      viewport->deliverPickResult(viewport->pickRequestRevision(),
                                  viewport->packetRevision(), pick->first);
    const auto prepared =
        prepared_view &&
        viewport->selectionText() != QStringLiteral("No selection") &&
        viewport->analyzeCenter(QStringLiteral("all"), QStringLiteral("com"),
                                QStringLiteral("daily-com")) &&
        viewport->analyzeDistance(QStringLiteral("index 1"),
                                  QStringLiteral("index 3"),
                                  QStringLiteral("raw"),
                                  QStringLiteral("daily-distance"));
    const auto results = viewport->analysisItems();
    const auto center_id =
        results.size() > 0 ? results[0].toMap().value(QStringLiteral("id"))
                                 .toULongLong()
                           : 0U;
    const auto distance_id =
        results.size() > 1 ? results[1].toMap().value(QStringLiteral("id"))
                                 .toULongLong()
                           : 0U;
    const auto center_path = output_directory.filePath(QStringLiteral("com.json"));
    const auto distance_path =
        output_directory.filePath(QStringLiteral("distance.csv"));
    const auto structure_path =
        output_directory.filePath(QStringLiteral("selected-frame.pdb"));
    const auto exported =
        center_id != 0U && distance_id != 0U &&
        viewport->exportAnalysisResult(
            center_id, QUrl::fromLocalFile(center_path), QStringLiteral("json")) &&
        viewport->exportAnalysisResult(distance_id,
                                       QUrl::fromLocalFile(distance_path),
                                       QStringLiteral("csv")) &&
        viewport->saveStructure(QUrl::fromLocalFile(structure_path), false);
    const auto played =
        viewport->seekTrajectory(1U) && viewport->seekTrajectory(2U) &&
        viewport->seekTrajectory(3U) &&
        viewport->waitForTrajectoryTask(5000) &&
        viewport->trajectoryFrame() == 3U &&
        viewport->setPlaybackMode(QStringLiteral("loop")) &&
        viewport->setPlaybackDirection(QStringLiteral("forward")) &&
        viewport->setTrajectoryFps(5.0) &&
        viewport->setTrajectoryPlaying(true) &&
        viewport->tickTrajectory(200.0) &&
        viewport->trajectoryFrame() == 0U &&
        viewport->setTrajectoryPlaying(false);
    const auto before_cancel = viewport->trajectoryFrame();
    const auto cancelled = viewport->seekTrajectory(1U);
    viewport->cancelTrajectoryTask();
    const auto cancellation_preserved =
        cancelled && viewport->trajectoryFrame() == before_cancel &&
        viewport->trajectoryTaskStage() == QStringLiteral("cancelled");
    const auto outputs_exist = QFileInfo{center_path}.size() > 0 &&
                               QFileInfo{distance_path}.size() > 0 &&
                               QFileInfo{structure_path}.size() > 0;
    if (!invalid_seek_preserved || !prepared || results.size() != 2 ||
        !exported || !played || !cancellation_preserved || !outputs_exist) {
      qCritical("MolShredder desktop daily workflow smoke failed: invalid=%d prepared=%d results=%lld exported=%d played=%d cancelled=%d outputs=%d status=%s",
                invalid_seek_preserved, prepared,
                static_cast<long long>(results.size()), exported, played,
                cancellation_preserved, outputs_exist,
                viewport->statusText().toUtf8().constData());
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop daily workflow ready: open=1 representation=sticks selection=1 analysis=2 trajectory=4 exports=3 cancellation=preserved");
  }
  if (amber_smoke) {
    if (!viewport->hasTrajectory() || viewport->trajectoryFrameCount() != 1U ||
        viewport->atomCount() != 4U ||
        viewport->representation() != QStringLiteral("sticks") ||
        viewport->renderPacket().cylinders.size() != 6U) {
      qCritical("MolShredder desktop Amber PRMTOP/RST7 smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop Amber ready: atoms=4 frames=1 "
          "representation=sticks");
  }
  if (amber_mdcrd_smoke) {
    if (!viewport->hasTrajectory() || viewport->trajectoryFrameCount() != 2U ||
        viewport->atomCount() != 4U ||
        viewport->representation() != QStringLiteral("sticks") ||
        viewport->renderPacket().cylinders.size() != 6U) {
      qCritical("MolShredder desktop Amber PRMTOP/MDCRD smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop Amber MDCRD ready: atoms=4 frames=2 "
          "representation=sticks");
  }
  if (amber_netcdf_smoke) {
    if (!viewport->hasTrajectory() || viewport->trajectoryFrameCount() != 2U ||
        viewport->atomCount() != 4U ||
        viewport->representation() != QStringLiteral("sticks") ||
        viewport->renderPacket().cylinders.size() != 6U ||
        !seek_and_wait(1U)) {
      qCritical("MolShredder desktop Amber NetCDF smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop Amber NetCDF ready: atoms=4 frames=2 "
          "representation=sticks");
  }
  if (h5md_smoke) {
    if (!viewport->hasTrajectory() || viewport->trajectoryFrameCount() != 2U ||
        viewport->atomCount() != 3U ||
        viewport->representation() != QStringLiteral("sticks") ||
        !seek_and_wait(1U) ||
        viewport->trajectoryMappingText() != trajectory_mapping ||
        window->findChild<QObject *>(
            QStringLiteral("trajectoryMappingButton")) == nullptr) {
      qCritical("MolShredder desktop H5MD trajectory smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop H5MD ready: atoms=3 frames=2 "
          "mapping=source-id unit=angstrom");
  }
  if (lammps_smoke) {
    if (!viewport->hasTrajectory() || viewport->trajectoryFrameCount() != 2U ||
        viewport->atomCount() != 3U ||
        viewport->representation() != QStringLiteral("sticks") ||
        !seek_and_wait(1U) ||
        viewport->trajectoryMappingText() != trajectory_mapping) {
      qCritical("MolShredder desktop LAMMPS trajectory smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop LAMMPS ready: atoms=3 frames=2 "
          "mapping=source-id unit=angstrom");
  }
  if (binpos_smoke) {
    if (!viewport->hasTrajectory() || viewport->trajectoryFrameCount() != 2U ||
        viewport->atomCount() != 3U ||
        viewport->representation() != QStringLiteral("sticks") ||
        !seek_and_wait(1U)) {
      qCritical("MolShredder desktop BINPOS trajectory smoke failed");
      return EXIT_FAILURE;
    }
    qInfo(
        "MolShredder desktop BINPOS ready: atoms=3 frames=2 precision=float32");
  }
  if (screenshot.has_value()) {
    QTimer::singleShot(1000, window, [window, screenshot, &application] {
      const auto image = window->grabWindow();
      if (image.isNull() || !image.save(*screenshot)) {
        application.exit(EXIT_FAILURE);
        return;
      }
      application.quit();
    });
  } else if (smoke && !picking_smoke && !trajectory_smoke && !script_smoke &&
             !script_cancel_smoke && !graphics_info_smoke &&
             !system_info_panel_smoke && !stereo_smoke && !anaglyph_smoke &&
             !interleaved_smoke) {
    QTimer::singleShot(1500, &application, &QCoreApplication::quit);
  }
  return application.exec();
}
