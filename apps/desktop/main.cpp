#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <QGuiApplication>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>

#include "viewport_item.hpp"

int main(int argc, char* argv[]) {
  QGuiApplication application{argc, argv};
  QCoreApplication::setApplicationName(QStringLiteral("MolShredder"));
  QCoreApplication::setOrganizationName(QStringLiteral("MolShredder"));

  bool smoke = false;
  bool camera_smoke = false;
  bool picking_smoke = false;
  bool object_smoke = false;
  bool trajectory_smoke = false;
  std::optional<QString> screenshot;
  std::vector<QString> open_paths;
  std::optional<QString> representation;
  std::optional<QString> trajectory;
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
    } else if (argument == "--trajectory-smoke") {
      trajectory_smoke = true;
    } else if (argument.starts_with("--screenshot=")) {
      const auto path =
          argument.substr(std::string_view{"--screenshot="}.size());
      screenshot = QString::fromUtf8(
          path.data(), static_cast<qsizetype>(path.size()));
    } else if (argument.starts_with("--open=")) {
      const auto path = argument.substr(std::string_view{"--open="}.size());
      open_paths.push_back(QString::fromUtf8(
          path.data(), static_cast<qsizetype>(path.size())));
    } else if (argument == "--open" && index + 1 < argc) {
      ++index;
      open_paths.push_back(QString::fromUtf8(argv[index]));
    } else if (argument.starts_with("--representation=")) {
      const auto value =
          argument.substr(std::string_view{"--representation="}.size());
      representation = QString::fromUtf8(
          value.data(), static_cast<qsizetype>(value.size()));
    } else if (argument.starts_with("--trajectory=")) {
      const auto path =
          argument.substr(std::string_view{"--trajectory="}.size());
      trajectory = QString::fromUtf8(
          path.data(), static_cast<qsizetype>(path.size()));
    }
  }

  QQmlApplicationEngine engine;
  engine.load(QUrl{QStringLiteral("qrc:/Main.qml")});
  if (engine.rootObjects().isEmpty()) return EXIT_FAILURE;
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
  if (window == nullptr) return EXIT_FAILURE;
  QObject::connect(
      window, &QQuickWindow::sceneGraphInitialized, window,
      [window] {
        qInfo("MolShredder graphics API: %d",
              static_cast<int>(window->rendererInterface()->graphicsApi()));
      },
      Qt::DirectConnection);
  auto* viewport = window->findChild<molshredder::desktop::MolecularViewport*>(
      QStringLiteral("molecularViewport"));
  if (viewport == nullptr) return EXIT_FAILURE;
  if (representation.has_value() &&
      !viewport->setRepresentation(*representation)) {
    qCritical("%s", viewport->statusText().toUtf8().constData());
    return EXIT_FAILURE;
  }
  for (const auto& open_path : open_paths) {
    if (!viewport->loadStructure(QUrl::fromLocalFile(open_path))) {
      qCritical("%s", viewport->statusText().toUtf8().constData());
      return EXIT_FAILURE;
    }
  }
  if (trajectory.has_value() &&
      !viewport->loadTrajectory(QUrl::fromLocalFile(*trajectory))) {
    qCritical("%s", viewport->statusText().toUtf8().constData());
    return EXIT_FAILURE;
  }
  if (object_smoke) {
    const auto before = viewport->objectItems();
    const auto composed_spheres = viewport->renderPacket().spheres.size();
    if (before.size() != 2 || composed_spheres != 4U ||
        !viewport->activateObject(1U) ||
        !viewport->setObjectVisible(2U, false)) {
      qCritical("MolShredder desktop object panel smoke failed");
      return EXIT_FAILURE;
    }
    const auto after = viewport->objectItems();
    const auto first = after[0].toMap();
    const auto second = after[1].toMap();
    if (!first.value(QStringLiteral("active")).toBool() ||
        !first.value(QStringLiteral("visible")).toBool() ||
        second.value(QStringLiteral("active")).toBool() ||
        second.value(QStringLiteral("visible")).toBool() ||
        viewport->renderPacket().spheres.size() != 1U) {
      qCritical("MolShredder desktop object state smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop objects ready: objects=2 active=1 visible=1 spheres=1");
  }
  if (camera_smoke) {
    const auto* initial = viewport->camera();
    if (initial == nullptr) return EXIT_FAILURE;
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
    viewport->resetView();
    qInfo("MolShredder desktop camera ready: representation=%s atoms=%llu",
          viewport->representation().toUtf8().constData(),
          static_cast<unsigned long long>(viewport->atomCount()));
  }
  if (picking_smoke) {
    const auto capture_requested = screenshot.has_value();
    QObject::connect(
        viewport, &molshredder::desktop::MolecularViewport::selectionChanged,
        &application, [viewport, &application, capture_requested] {
          if (viewport->selectionText().startsWith(QStringLiteral("Atom "))) {
            qInfo("MolShredder desktop GPU picking ready: %s",
                  viewport->selectionText().toUtf8().constData());
            if (!capture_requested) application.quit();
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
        !viewport->setTrajectoryFps(5.0) ||
        !viewport->seekTrajectory(3U) ||
        !viewport->setTrajectoryPlaying(true) ||
        !viewport->tickTrajectory(200.0) || viewport->trajectoryFrame() != 0U ||
        !viewport->setTrajectoryPlaying(false) ||
        !viewport->setPlaybackMode(QStringLiteral("rock")) ||
        !viewport->seekTrajectory(3U) ||
        !viewport->setPlaybackDirection(QStringLiteral("forward")) ||
        !viewport->setTrajectoryPlaying(true) ||
        !viewport->tickTrajectory(200.0) || viewport->trajectoryFrame() != 2U ||
        viewport->playbackDirection() != QStringLiteral("reverse") ||
        !viewport->setTrajectoryPlaying(false) ||
        !viewport->setPlaybackMode(QStringLiteral("loop")) ||
        !viewport->setTrajectoryFps(30.0) || !viewport->seekTrajectory(0U)) {
      qCritical("MolShredder desktop trajectory deterministic smoke failed");
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
    if (!viewport->setTrajectoryPlaying(true)) return EXIT_FAILURE;
    QTimer::singleShot(
        300, &application,
        [viewport, observed_frame_change, &application] {
          const auto passed = *observed_frame_change &&
                              viewport->trajectoryFrame() < 4U &&
                              viewport->setTrajectoryPlaying(false);
          if (!passed) {
            qCritical("MolShredder desktop trajectory scheduler smoke failed");
            application.exit(EXIT_FAILURE);
            return;
          }
          qInfo("MolShredder desktop trajectory ready: frames=4 scheduler=qt mode=loop");
          application.quit();
        });
    QTimer::singleShot(5000, &application, [&application] {
      qCritical("MolShredder desktop trajectory smoke timed out");
      application.exit(EXIT_FAILURE);
    });
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
  } else if (smoke && !picking_smoke && !trajectory_smoke) {
    QTimer::singleShot(1500, &application, &QCoreApplication::quit);
  }
  return application.exec();
}
