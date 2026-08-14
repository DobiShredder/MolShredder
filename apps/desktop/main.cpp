#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>

#include "molshredder/io/structure_reader.hpp"
#include "viewport_item.hpp"

int main(int argc, char *argv[]) {
  QGuiApplication application{argc, argv};
  QCoreApplication::setApplicationName(QStringLiteral("MolShredder"));
  QCoreApplication::setOrganizationName(QStringLiteral("MolShredder"));

  bool smoke = false;
  bool camera_smoke = false;
  bool picking_smoke = false;
  bool object_smoke = false;
  bool trajectory_smoke = false;
  bool amber_smoke = false;
  bool amber_mdcrd_smoke = false;
  bool amber_netcdf_smoke = false;
  bool h5md_smoke = false;
  bool lammps_smoke = false;
  bool binpos_smoke = false;
  bool save_all = false;
  bool save_smoke = false;
  std::optional<QString> screenshot;
  std::vector<QString> open_paths;
  std::optional<QString> representation;
  std::optional<QString> trajectory;
  QString trajectory_coordinate_unit{QStringLiteral("angstrom")};
  std::optional<QString> save_path;
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
    } else if (argument.starts_with("--save=")) {
      const auto path = argument.substr(std::string_view{"--save="}.size());
      save_path =
          QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
    }
  }

  QQmlApplicationEngine engine;
  engine.load(QUrl{QStringLiteral("qrc:/Main.qml")});
  if (engine.rootObjects().isEmpty())
    return EXIT_FAILURE;
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  if (window == nullptr)
    return EXIT_FAILURE;
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
  if (representation.has_value() &&
      !viewport->setRepresentation(*representation)) {
    qCritical("%s", viewport->statusText().toUtf8().constData());
    return EXIT_FAILURE;
  }
  for (const auto &open_path : open_paths) {
    if (!viewport->loadStructure(QUrl::fromLocalFile(open_path))) {
      qCritical("%s", viewport->statusText().toUtf8().constData());
      return EXIT_FAILURE;
    }
  }
  if (trajectory.has_value() &&
      !viewport->loadTrajectory(QUrl::fromLocalFile(*trajectory),
                                trajectory_coordinate_unit)) {
    qCritical("%s", viewport->statusText().toUtf8().constData());
    return EXIT_FAILURE;
  }
  if (save_path.has_value() &&
      !viewport->saveStructure(QUrl::fromLocalFile(*save_path), save_all)) {
    qCritical("%s", viewport->statusText().toUtf8().constData());
    return EXIT_FAILURE;
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
    qInfo("MolShredder desktop objects ready: objects=2 active=1 visible=1 "
          "spheres=1");
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
        !viewport->setTrajectoryFps(5.0) || !viewport->seekTrajectory(3U) ||
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
    if (!viewport->setTrajectoryPlaying(true))
      return EXIT_FAILURE;
    QTimer::singleShot(
        300, &application, [viewport, observed_frame_change, &application] {
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
        !viewport->seekTrajectory(1U) || viewport->trajectoryFrame() != 1U) {
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
        !viewport->seekTrajectory(1U) || viewport->trajectoryFrame() != 1U) {
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
        !viewport->seekTrajectory(1U) || viewport->trajectoryFrame() != 1U) {
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
        !viewport->seekTrajectory(1U) || viewport->trajectoryFrame() != 1U) {
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
  } else if (smoke && !picking_smoke && !trajectory_smoke) {
    QTimer::singleShot(1500, &application, &QCoreApplication::quit);
  }
  return application.exec();
}
