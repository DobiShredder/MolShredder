#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
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
#include "redirected_render_smoke.hpp"
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
  const auto redirected_render_requested = [&] {
    for (int index = 1; index < argc; ++index) {
      if (std::string_view{argv[index]}.starts_with(
              "--redirected-render-smoke="))
        return true;
    }
    return false;
  }();
  if (redirected_render_requested) {
#ifdef Q_OS_WIN
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    qputenv("QT_QUICK_BACKEND", QByteArrayLiteral("rhi"));
    qputenv("QSG_RHI_PREFER_SOFTWARE_RENDERER", QByteArrayLiteral("1"));
#endif
    std::fprintf(stderr,
                 "MolShredder redirected renderer stage: process-entry\n");
    std::fflush(stderr);
  }
  molshredder::python::link_embedded_module();
  if (redirected_render_requested) {
    std::fprintf(stderr,
                 "MolShredder redirected renderer stage: embedded-module-linked\n");
    std::fflush(stderr);
  }
  QGuiApplication application{argc, argv};
  if (redirected_render_requested) {
    std::fprintf(
        stderr,
        "MolShredder redirected renderer stage: gui-application-ready\n");
    std::fflush(stderr);
  }
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
  bool isolated_script_smoke = false;
  bool graphics_info_smoke = false;
  bool system_info_panel_smoke = false;
  bool named_view_smoke = false;
  bool session_smoke = false;
  bool stereo_smoke = false;
  bool anaglyph_smoke = false;
  bool interleaved_smoke = false;
  bool representation_visibility_smoke = false;
  bool render_setting_smoke = false;
  bool edit_smoke = false;
  bool builder_smoke = false;
  bool analysis_smoke = false;
  bool daily_workflow_smoke = false;
  bool information_architecture_smoke = false;
  bool volume_slice_smoke = false;
  bool molecular_surface_smoke = false;
  bool direct_volume_smoke = false;
  std::optional<QString> redirected_render_smoke;
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
    } else if (argument == "--isolated-script-smoke") {
      isolated_script_smoke = true;
    } else if (argument == "--graphics-info-smoke") {
      graphics_info_smoke = true;
    } else if (argument == "--system-info-panel-smoke") {
      system_info_panel_smoke = true;
    } else if (argument == "--named-view-smoke") {
      named_view_smoke = true;
    } else if (argument == "--session-smoke") {
      session_smoke = true;
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
    } else if (argument == "--edit-smoke") {
      edit_smoke = true;
      smoke = true;
    } else if (argument == "--builder-smoke") {
      builder_smoke = true;
      smoke = true;
    } else if (argument == "--analysis-smoke") {
      analysis_smoke = true;
    } else if (argument == "--daily-workflow-smoke") {
      daily_workflow_smoke = true;
      smoke = true;
    } else if (argument == "--information-architecture-smoke") {
      information_architecture_smoke = true;
      smoke = true;
    } else if (argument == "--volume-slice-smoke") {
      volume_slice_smoke = true;
      smoke = true;
    } else if (argument == "--molecular-surface-smoke") {
      molecular_surface_smoke = true;
      smoke = true;
    } else if (argument == "--direct-volume-smoke") {
      direct_volume_smoke = true;
      smoke = true;
    } else if (argument.starts_with("--redirected-render-smoke=")) {
      const auto value =
          argument.substr(std::string_view{"--redirected-render-smoke="}.size());
      redirected_render_smoke =
          QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
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

  if (redirected_render_smoke.has_value())
    return molshredder::desktop::run_redirected_render_smoke(
        {.backend = redirected_render_smoke.value(),
         .open_paths = open_paths,
         .representation = representation,
         .trajectory = trajectory,
         .trajectory_coordinate_unit = trajectory_coordinate_unit,
         .trajectory_mapping = trajectory_mapping});

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
  auto *viewport = window->findChild<molshredder::desktop::MolecularViewport *>(
      QStringLiteral("molecularViewport"));
  if (viewport == nullptr)
    return EXIT_FAILURE;
  if (information_architecture_smoke) {
    const auto open_metadata =
        localization.actionMetadata(QStringLiteral("file.open"));
    const auto save_metadata =
        localization.actionMetadata(QStringLiteral("file.save"));
    const auto open_session_metadata =
        localization.actionMetadata(QStringLiteral("file.open-session"));
    const auto save_session_metadata =
        localization.actionMetadata(QStringLiteral("file.save-session"));
    const auto attach_metadata =
        localization.actionMetadata(QStringLiteral("trajectory.attach"));
    const auto script_metadata =
        localization.actionMetadata(QStringLiteral("tools.run-script"));
    const auto object_metadata =
        localization.actionMetadata(QStringLiteral("object.panel"));
    const auto select_expression_metadata =
        localization.actionMetadata(QStringLiteral("select.expression"));
    const auto select_metadata =
        localization.actionMetadata(QStringLiteral("select.all"));
    const auto playback_metadata =
        localization.actionMetadata(QStringLiteral("trajectory.play-pause"));
    const auto show_metadata =
        localization.actionMetadata(QStringLiteral("represent.show"));
    const auto hide_metadata =
        localization.actionMetadata(QStringLiteral("represent.hide"));
    const auto as_metadata =
        localization.actionMetadata(QStringLiteral("represent.as"));
    const auto toggle_metadata =
        localization.actionMetadata(QStringLiteral("represent.toggle"));
    const auto named_scenes_metadata =
        localization.actionMetadata(QStringLiteral("scene.named-scenes"));
    const auto movie_metadata =
        localization.actionMetadata(QStringLiteral("scene.movie"));
    auto *open_action =
        window->findChild<QObject *>(QStringLiteral("fileOpenAction"));
    auto *save_action =
        window->findChild<QObject *>(QStringLiteral("fileSaveAction"));
    auto *open_session_action = window->findChild<QObject *>(
        QStringLiteral("fileOpenSessionAction"));
    auto *save_session_action = window->findChild<QObject *>(
        QStringLiteral("fileSaveSessionAction"));
    auto *attach_action =
        window->findChild<QObject *>(QStringLiteral("trajectoryAttachAction"));
    auto *script_action =
        window->findChild<QObject *>(QStringLiteral("runScriptAction"));
    auto *lines_action =
        window->findChild<QObject *>(QStringLiteral("representLinesAction"));
    auto *spheres_action = window->findChild<QObject *>(
        QStringLiteral("representSpheresAction"));
    auto *ribbon_action =
        window->findChild<QObject *>(QStringLiteral("representRibbonAction"));
    auto *settings_action =
        window->findChild<QObject *>(QStringLiteral("renderSettingsAction"));
    auto *analyze_action =
        window->findChild<QObject *>(QStringLiteral("analyzePanelAction"));
    auto *views_action =
        window->findChild<QObject *>(QStringLiteral("sceneViewsAction"));
    auto *named_scenes_action =
        window->findChild<QObject *>(QStringLiteral("namedScenesAction"));
    auto *movie_action =
        window->findChild<QObject *>(QStringLiteral("movieTimelineAction"));
    auto *system_action = window->findChild<QObject *>(
        QStringLiteral("systemInformationAction"));
    auto *object_action =
        window->findChild<QObject *>(QStringLiteral("objectPanelAction"));
    auto *select_expression_action = window->findChild<QObject *>(
        QStringLiteral("selectExpressionAction"));
    auto *select_action =
        window->findChild<QObject *>(QStringLiteral("selectAllAction"));
    auto *playback_action = window->findChild<QObject *>(
        QStringLiteral("trajectoryPlaybackAction"));
    auto *palette_action = window->findChild<QObject *>(
        QStringLiteral("showCommandPaletteAction"));
    auto *show_action =
        window->findChild<QObject *>(QStringLiteral("representShowAction"));
    auto *hide_action =
        window->findChild<QObject *>(QStringLiteral("representHideAction"));
    auto *as_action =
        window->findChild<QObject *>(QStringLiteral("representAsAction"));
    auto *toggle_action = window->findChild<QObject *>(
        QStringLiteral("representToggleAction"));
    auto *menu_bar =
        window->findChild<QObject *>(QStringLiteral("mainMenuBar"));
    auto *compact_toolbar =
        window->findChild<QObject *>(QStringLiteral("compactToolbar"));
    const auto menu_names =
        std::array{QStringLiteral("fileMenu"), QStringLiteral("editMenu"),
                   QStringLiteral("objectMenu"), QStringLiteral("selectMenu"),
                   QStringLiteral("representMenu"),
                   QStringLiteral("analyzeMenu"),
                   QStringLiteral("trajectoryMenu"),
                   QStringLiteral("sceneMenu"), QStringLiteral("toolsMenu"),
                   QStringLiteral("helpMenu")};
    const auto all_menus_present =
        std::ranges::all_of(menu_names, [window](const QString &name) {
          return window->findChild<QObject *>(name) != nullptr;
        });
    auto *open_menu_item =
        window->findChild<QObject *>(QStringLiteral("fileOpenMenuItem"));
    auto *save_menu_item =
        window->findChild<QObject *>(QStringLiteral("fileSaveMenuItem"));
    auto *open_session_menu_item = window->findChild<QObject *>(
        QStringLiteral("fileOpenSessionMenuItem"));
    auto *save_session_menu_item = window->findChild<QObject *>(
        QStringLiteral("fileSaveSessionMenuItem"));
    auto *attach_menu_item = window->findChild<QObject *>(
        QStringLiteral("trajectoryAttachMenuItem"));
    auto *script_menu_item =
        window->findChild<QObject *>(QStringLiteral("runScriptMenuItem"));
    auto *lines_menu_item = window->findChild<QObject *>(
        QStringLiteral("representLinesMenuItem"));
    auto *ribbon_menu_item = window->findChild<QObject *>(
        QStringLiteral("representRibbonMenuItem"));
    auto *settings_menu_item = window->findChild<QObject *>(
        QStringLiteral("renderSettingsMenuItem"));
    auto *analyze_menu_item = window->findChild<QObject *>(
        QStringLiteral("analyzePanelMenuItem"));
    auto *views_menu_item =
        window->findChild<QObject *>(QStringLiteral("sceneViewsMenuItem"));
    auto *named_scenes_menu_item = window->findChild<QObject *>(
        QStringLiteral("namedScenesMenuItem"));
    auto *movie_menu_item = window->findChild<QObject *>(
        QStringLiteral("movieTimelineMenuItem"));
    auto *system_menu_item = window->findChild<QObject *>(
        QStringLiteral("systemInformationMenuItem"));
    auto *object_menu_item =
        window->findChild<QObject *>(QStringLiteral("objectPanelMenuItem"));
    auto *select_expression_menu_item = window->findChild<QObject *>(
        QStringLiteral("selectExpressionMenuItem"));
    auto *select_menu_item =
        window->findChild<QObject *>(QStringLiteral("selectAllMenuItem"));
    auto *playback_menu_item = window->findChild<QObject *>(
        QStringLiteral("trajectoryPlaybackMenuItem"));
    auto *show_menu_item = window->findChild<QObject *>(
        QStringLiteral("representShowMenuItem"));
    auto *hide_menu_item = window->findChild<QObject *>(
        QStringLiteral("representHideMenuItem"));
    auto *as_menu_item =
        window->findChild<QObject *>(QStringLiteral("representAsMenuItem"));
    auto *toggle_menu_item = window->findChild<QObject *>(
        QStringLiteral("representToggleMenuItem"));
    auto *open_toolbar =
        window->findChild<QObject *>(QStringLiteral("fileOpenToolbarButton"));
    auto *save_toolbar =
        window->findChild<QObject *>(QStringLiteral("fileSaveToolbarButton"));
    auto *attach_toolbar = window->findChild<QObject *>(
        QStringLiteral("trajectoryAttachToolbarButton"));
    auto *lines_toolbar = window->findChild<QObject *>(
        QStringLiteral("representLinesToolbarButton"));
    auto *analyze_toolbar = window->findChild<QObject *>(
        QStringLiteral("analyzePanelToolbarButton"));
    auto *views_toolbar = window->findChild<QObject *>(
        QStringLiteral("sceneViewsToolbarButton"));
    auto *palette =
        window->findChild<QObject *>(QStringLiteral("commandPaletteOverlay"));
    auto *open_palette =
        window->findChild<QObject *>(QStringLiteral("commandPaletteFileOpen"));
    auto *save_palette =
        window->findChild<QObject *>(QStringLiteral("commandPaletteFileSave"));
    auto *open_session_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteFileOpenSession"));
    auto *save_session_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteFileSaveSession"));
    auto *attach_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteTrajectoryAttach"));
    auto *script_palette =
        window->findChild<QObject *>(QStringLiteral("commandPaletteRunScript"));
    auto *lines_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteRepresentLines"));
    auto *ribbon_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteRepresentRibbon"));
    auto *settings_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteRenderSettings"));
    auto *analyze_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteAnalyzePanel"));
    auto *views_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteSceneViews"));
    auto *named_scenes_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteNamedScenes"));
    auto *movie_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteMovieTimeline"));
    auto *system_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteSystemInformation"));
    auto *object_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteObjectPanel"));
    auto *select_expression_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteSelectExpression"));
    auto *select_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteSelectAll"));
    auto *playback_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteTrajectoryPlayback"));
    auto *show_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteRepresentShow"));
    auto *hide_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteRepresentHide"));
    auto *as_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteRepresentAs"));
    auto *toggle_palette = window->findChild<QObject *>(
        QStringLiteral("commandPaletteRepresentToggle"));
    auto *palette_scroll =
        window->findChild<QObject *>(QStringLiteral("commandPaletteScroll"));
    auto *settings_panel =
        window->findChild<QObject *>(QStringLiteral("renderSettingsOverlay"));
    auto *analyze_panel =
        window->findChild<QObject *>(QStringLiteral("analysisOverlay"));
    auto *views_panel =
        window->findChild<QObject *>(QStringLiteral("viewsOverlay"));
    auto *system_panel =
        window->findChild<QObject *>(QStringLiteral("systemInfoOverlay"));
    auto *object_panel =
        window->findChild<QObject *>(QStringLiteral("objectPanel"));
    auto *select_expression_panel = window->findChild<QObject *>(
        QStringLiteral("selectionExpressionOverlay"));
    auto *trajectory_panel =
        window->findChild<QObject *>(QStringLiteral("trajectoryPanel"));
    auto *trajectory_import_panel = window->findChild<QObject *>(
        QStringLiteral("trajectoryImportOverlay"));
    auto *playback_panel_button = window->findChild<QObject *>(
        QStringLiteral("trajectoryPlaybackPanelButton"));
    auto *select_context_item = window->findChild<QObject *>(
        QStringLiteral("selectAllContextMenuItem"));
    auto *playback_context_item = window->findChild<QObject *>(
        QStringLiteral("trajectoryPlaybackContextMenuItem"));
    auto *viewport_context_menu = window->findChild<QObject *>(
        QStringLiteral("viewportContextMenu"));
    auto *show_context_item = window->findChild<QObject *>(
        QStringLiteral("representShowContextMenuItem"));
    auto *hide_context_item = window->findChild<QObject *>(
        QStringLiteral("representHideContextMenuItem"));
    auto *as_context_item = window->findChild<QObject *>(
        QStringLiteral("representAsContextMenuItem"));
    auto *toggle_context_item = window->findChild<QObject *>(
        QStringLiteral("representToggleContextMenuItem"));
    const auto palette_opened =
        palette_action != nullptr && QMetaObject::invokeMethod(
                                         palette_action, "trigger",
                                         Qt::DirectConnection);
    const auto all_surfaces =
        QStringList{QStringLiteral("menu"), QStringLiteral("toolbar"),
                    QStringLiteral("command-palette")};
    const auto menu_palette =
        QStringList{QStringLiteral("menu"), QStringLiteral("command-palette")};
    const auto menu_panel_palette =
        QStringList{QStringLiteral("menu"), QStringLiteral("command-palette"),
                    QStringLiteral("panel")};
    const auto menu_toolbar_panel_palette = QStringList{
        QStringLiteral("menu"), QStringLiteral("toolbar"),
        QStringLiteral("command-palette"), QStringLiteral("panel")};
    const auto menu_context_palette =
        QStringList{QStringLiteral("menu"), QStringLiteral("command-palette"),
                    QStringLiteral("context-menu")};
    const auto menu_panel_context_palette = QStringList{
        QStringLiteral("menu"), QStringLiteral("command-palette"),
        QStringLiteral("panel"), QStringLiteral("context-menu")};
    const auto workspace = QStringList{QStringLiteral("workspace")};
    const auto korean =
        localization.currentLanguage() == QStringLiteral("ko");
    const auto shared = [](QObject *surface, const char *property,
                           QObject *action) {
      return surface != nullptr &&
             surface->property(property).value<QObject *>() == action;
    };
    const auto unavailable_before_load =
        save_palette != nullptr && attach_palette != nullptr &&
        save_palette->property("translatedStatus").toString() ==
            localization.translateUi(
                save_metadata.value(QStringLiteral("unavailable")).toString()) &&
        attach_palette->property("translatedStatus").toString() ==
            localization.translateUi(attach_metadata
                                         .value(QStringLiteral("unavailable"))
                                         .toString());
    const auto action_ids = std::array{
        QStringLiteral("file.open"), QStringLiteral("file.save"),
        QStringLiteral("file.open-session"),
        QStringLiteral("file.save-session"),
        QStringLiteral("trajectory.attach"),
        QStringLiteral("tools.run-script"),
        QStringLiteral("represent.lines"),
        QStringLiteral("represent.sticks"),
        QStringLiteral("represent.spheres"),
        QStringLiteral("represent.ribbon"),
        QStringLiteral("represent.cartoon"),
        QStringLiteral("represent.settings"),
        QStringLiteral("analyze.open-panel"),
        QStringLiteral("scene.views"),
        QStringLiteral("scene.named-scenes"), QStringLiteral("scene.movie"),
        QStringLiteral("help.system-information"),
        QStringLiteral("object.panel"),
        QStringLiteral("select.expression"), QStringLiteral("select.all"),
        QStringLiteral("trajectory.play-pause"),
        QStringLiteral("represent.show"), QStringLiteral("represent.hide"),
        QStringLiteral("represent.as"), QStringLiteral("represent.toggle")};
    const auto localized_catalog_complete =
        std::ranges::all_of(action_ids, [&localization](const QString &id) {
          const auto metadata = localization.actionMetadata(id);
          return metadata.value(QStringLiteral("id")).toString() == id &&
                 !localization
                      .translateUi(metadata.value(QStringLiteral("label"))
                                       .toString())
                      .isEmpty() &&
                 !localization
                      .translateUi(metadata.value(QStringLiteral("status"))
                                       .toString())
                      .isEmpty() &&
                 !localization
                      .translateUi(metadata.value(QStringLiteral("error"))
                                       .toString())
                      .isEmpty();
        });
    auto passed =
        localized_catalog_complete &&
        open_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("load") &&
        save_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("save") &&
        open_session_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("session load") &&
        save_session_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("session save") &&
        attach_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("traj load") &&
        script_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("script run") &&
        object_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("object list") &&
        select_expression_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("select") &&
        select_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("select") &&
        playback_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("traj play") &&
        playback_metadata.value(QStringLiteral("alternateCommand")).toString() ==
            QStringLiteral("traj pause") &&
        show_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("show") &&
        hide_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("hide") &&
        as_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("as") &&
        toggle_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("toggle") &&
        named_scenes_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("scene list") &&
        movie_metadata.value(QStringLiteral("command")).toString() ==
            QStringLiteral("movie status") &&
        open_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            all_surfaces &&
        save_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            all_surfaces &&
        open_session_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            menu_panel_palette &&
        save_session_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            menu_panel_palette &&
        attach_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            menu_toolbar_panel_palette &&
        script_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            menu_palette &&
        object_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            menu_panel_palette &&
        select_expression_metadata.value(QStringLiteral("surfaces"))
                .toStringList() == menu_panel_palette &&
        select_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            menu_context_palette &&
        playback_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            menu_panel_context_palette &&
        show_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            menu_context_palette &&
        hide_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            menu_context_palette &&
        as_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            menu_context_palette &&
        toggle_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            menu_context_palette &&
        named_scenes_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            menu_panel_palette &&
        movie_metadata.value(QStringLiteral("surfaces")).toStringList() ==
            menu_panel_palette &&
        save_metadata.value(QStringLiteral("requirements")).toStringList() ==
            workspace &&
        attach_metadata.value(QStringLiteral("requirements")).toStringList() ==
            workspace &&
        open_action != nullptr && save_action != nullptr &&
        open_session_action != nullptr && save_session_action != nullptr &&
        attach_action != nullptr && script_action != nullptr &&
        lines_action != nullptr && spheres_action != nullptr &&
        ribbon_action != nullptr && settings_action != nullptr &&
        analyze_action != nullptr && views_action != nullptr &&
        named_scenes_action != nullptr && movie_action != nullptr &&
        system_action != nullptr && object_action != nullptr &&
        select_expression_action != nullptr && select_action != nullptr &&
        playback_action != nullptr &&
        palette_action != nullptr && viewport_context_menu != nullptr &&
        show_action != nullptr && hide_action != nullptr &&
        as_action != nullptr && toggle_action != nullptr &&
        menu_bar != nullptr && all_menus_present && palette != nullptr &&
        compact_toolbar != nullptr &&
        compact_toolbar->property("actionIdSequence").toString() ==
            QStringLiteral("file.open,trajectory.attach,file.save,scene.views,"
                           "analyze.open-panel,represent.lines,represent.sticks,"
                           "represent.spheres,represent.cartoon") &&
        compact_toolbar->property("projectedActionCount").toInt() == 9 &&
        palette_opened &&
        palette->property("visible").toBool() &&
        open_action->property("text").toString() ==
            (korean ? QStringLiteral("열기") : QStringLiteral("Open")) &&
        save_action->property("text").toString() ==
            (korean ? QStringLiteral("저장") : QStringLiteral("Save")) &&
        attach_action->property("text").toString() ==
            (korean ? QStringLiteral("Trajectory 연결")
                    : QStringLiteral("Attach Trajectory")) &&
        script_action->property("text").toString() ==
            (korean ? QStringLiteral("스크립트 실행")
                    : QStringLiteral("Run Script")) &&
        !open_action->property("shortcut").toString().isEmpty() &&
        !save_action->property("shortcut").toString().isEmpty() &&
        !select_action->property("shortcut").toString().isEmpty() &&
        !playback_action->property("shortcut").toString().isEmpty() &&
        !palette_action->property("shortcut").toString().isEmpty() &&
        !save_action->property("enabled").toBool() &&
        open_session_action->property("enabled").toBool() &&
        !save_session_action->property("enabled").toBool() &&
        !attach_action->property("enabled").toBool() &&
        !lines_action->property("enabled").toBool() &&
        !object_action->property("enabled").toBool() &&
        !select_expression_action->property("enabled").toBool() &&
        !select_action->property("enabled").toBool() &&
        !playback_action->property("enabled").toBool() &&
        !show_action->property("enabled").toBool() &&
        !hide_action->property("enabled").toBool() &&
        !as_action->property("enabled").toBool() &&
        !toggle_action->property("enabled").toBool() &&
        !analyze_action->property("enabled").toBool() &&
        !views_action->property("enabled").toBool() &&
        !named_scenes_action->property("enabled").toBool() &&
        !movie_action->property("enabled").toBool() &&
        settings_action->property("enabled").toBool() &&
        system_action->property("enabled").toBool() &&
        script_action->property("enabled").toBool() &&
        unavailable_before_load &&
        shared(open_menu_item, "action", open_action) &&
        shared(save_menu_item, "action", save_action) &&
        shared(open_session_menu_item, "action", open_session_action) &&
        shared(save_session_menu_item, "action", save_session_action) &&
        shared(attach_menu_item, "action", attach_action) &&
        shared(script_menu_item, "action", script_action) &&
        shared(lines_menu_item, "action", lines_action) &&
        shared(ribbon_menu_item, "action", ribbon_action) &&
        shared(settings_menu_item, "action", settings_action) &&
        shared(analyze_menu_item, "action", analyze_action) &&
        shared(views_menu_item, "action", views_action) &&
        shared(named_scenes_menu_item, "action", named_scenes_action) &&
        shared(movie_menu_item, "action", movie_action) &&
        shared(system_menu_item, "action", system_action) &&
        shared(object_menu_item, "action", object_action) &&
        shared(select_expression_menu_item, "action",
               select_expression_action) &&
        shared(select_menu_item, "action", select_action) &&
        shared(playback_menu_item, "action", playback_action) &&
        shared(show_menu_item, "action", show_action) &&
        shared(hide_menu_item, "action", hide_action) &&
        shared(as_menu_item, "action", as_action) &&
        shared(toggle_menu_item, "action", toggle_action) &&
        shared(open_toolbar, "action", open_action) &&
        shared(save_toolbar, "action", save_action) &&
        shared(attach_toolbar, "action", attach_action) &&
        shared(lines_toolbar, "action", lines_action) &&
        shared(analyze_toolbar, "action", analyze_action) &&
        shared(views_toolbar, "action", views_action) &&
        open_palette != nullptr && save_palette != nullptr &&
        attach_palette != nullptr && script_palette != nullptr &&
        shared(open_palette, "commandAction", open_action) &&
        shared(save_palette, "commandAction", save_action) &&
        shared(open_session_palette, "commandAction", open_session_action) &&
        shared(save_session_palette, "commandAction", save_session_action) &&
        shared(attach_palette, "commandAction", attach_action) &&
        shared(script_palette, "commandAction", script_action) &&
        shared(lines_palette, "commandAction", lines_action) &&
        shared(ribbon_palette, "commandAction", ribbon_action) &&
        shared(settings_palette, "commandAction", settings_action) &&
        shared(analyze_palette, "commandAction", analyze_action) &&
        shared(views_palette, "commandAction", views_action) &&
        shared(named_scenes_palette, "commandAction", named_scenes_action) &&
        shared(movie_palette, "commandAction", movie_action) &&
        shared(system_palette, "commandAction", system_action) &&
        shared(object_palette, "commandAction", object_action) &&
        shared(select_expression_palette, "commandAction",
               select_expression_action) &&
        shared(select_palette, "commandAction", select_action) &&
        shared(playback_palette, "commandAction", playback_action) &&
        shared(show_palette, "commandAction", show_action) &&
        shared(hide_palette, "commandAction", hide_action) &&
        shared(as_palette, "commandAction", as_action) &&
        shared(toggle_palette, "commandAction", toggle_action) &&
        shared(settings_panel, "entryAction", settings_action) &&
        shared(analyze_panel, "entryAction", analyze_action) &&
        shared(views_panel, "entryAction", views_action) &&
        shared(system_panel, "entryAction", system_action) &&
        shared(object_panel, "entryAction", object_action) &&
        shared(select_expression_panel, "entryAction",
               select_expression_action) &&
        shared(trajectory_import_panel, "entryAction", attach_action) &&
        shared(trajectory_panel, "entryAction", playback_action) &&
        shared(playback_panel_button, "action", playback_action) &&
        shared(select_context_item, "action", select_action) &&
        shared(playback_context_item, "action", playback_action) &&
        shared(show_context_item, "action", show_action) &&
        shared(hide_context_item, "action", hide_action) &&
        shared(as_context_item, "action", as_action) &&
        shared(toggle_context_item, "action", toggle_action) &&
        palette_scroll != nullptr &&
        palette_scroll->property("contentHeight").toReal() >
            palette_scroll->property("height").toReal();
    if (passed && open_paths.size() == 1U) {
      passed = viewport->loadStructure(QUrl::fromLocalFile(open_paths.front()));
      QCoreApplication::processEvents();
      const auto select_triggered = QMetaObject::invokeMethod(
          select_action, "trigger", Qt::DirectConnection);
      const auto lines_triggered = QMetaObject::invokeMethod(
          lines_action, "trigger", Qt::DirectConnection);
      passed = passed && save_action->property("enabled").toBool() &&
               open_session_action->property("enabled").toBool() &&
               save_session_action->property("enabled").toBool() &&
               attach_action->property("enabled").toBool() &&
               lines_action->property("enabled").toBool() &&
               object_action->property("enabled").toBool() &&
               select_expression_action->property("enabled").toBool() &&
               select_action->property("enabled").toBool() &&
               !playback_action->property("enabled").toBool() &&
               show_action->property("enabled").toBool() &&
               hide_action->property("enabled").toBool() &&
               as_action->property("enabled").toBool() &&
               toggle_action->property("enabled").toBool() &&
               analyze_action->property("enabled").toBool() &&
               views_action->property("enabled").toBool() &&
               named_scenes_action->property("enabled").toBool() &&
               movie_action->property("enabled").toBool() &&
               QMetaObject::invokeMethod(select_expression_action, "trigger",
                                         Qt::DirectConnection) &&
               select_triggered && lines_triggered;
      QCoreApplication::processEvents();
      passed = passed &&
               viewport->selectionText() == QStringLiteral("All atoms selected") &&
               lines_action->property("checked").toBool() &&
               QMetaObject::invokeMethod(settings_action, "trigger",
                                         Qt::DirectConnection) &&
               QMetaObject::invokeMethod(analyze_action, "trigger",
                                         Qt::DirectConnection) &&
               QMetaObject::invokeMethod(views_action, "trigger",
                                         Qt::DirectConnection) &&
               QMetaObject::invokeMethod(system_action, "trigger",
                                         Qt::DirectConnection);
      QCoreApplication::processEvents();
      passed = passed && settings_panel->property("visible").toBool() &&
               analyze_panel->property("visible").toBool() &&
               views_panel->property("visible").toBool() &&
               system_panel->property("visible").toBool();
      passed = passed &&
               select_expression_panel->property("visible").toBool();
      bool selection_defined{};
      passed = passed && QMetaObject::invokeMethod(
                             viewport, "defineSelection",
                             Qt::DirectConnection,
                             Q_RETURN_ARG(bool, selection_defined),
                             Q_ARG(QString, QStringLiteral("focus")),
                             Q_ARG(QString, QStringLiteral("b < 30")),
                             Q_ARG(bool, true)) &&
               selection_defined &&
               viewport->selectionText() ==
                   QStringLiteral("Selection defined · focus · b < 30");
      if (passed && trajectory.has_value()) {
        passed = viewport->loadTrajectory(QUrl::fromLocalFile(*trajectory),
                                          trajectory_coordinate_unit,
                                          trajectory_mapping) &&
                 viewport->waitForTrajectoryTask(10000);
        QCoreApplication::processEvents();
        passed = passed && playback_action->property("enabled").toBool() &&
                 QMetaObject::invokeMethod(playback_action, "trigger",
                                           Qt::DirectConnection);
        QCoreApplication::processEvents();
        passed = passed && viewport->trajectoryPlaying() &&
                 playback_action->property("checked").toBool();
        trajectory.reset();
      } else {
        passed = false;
      }
      open_paths.clear();
    } else {
      passed = false;
    }
    if (!passed) {
      qCritical("MolShredder information architecture smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder information architecture ready: actions=25 "
          "availability=workspace surfaces=shared language=%s",
          qUtf8Printable(localization.currentLanguage()));
  }
  // Automated exits can stop the GUI event loop while the native window and
  // its QRhi scene graph are still live. Release them while QGuiApplication is
  // still active so QQuickRhiItemRenderer and its backend resources are
  // destroyed on Qt's designated rendering thread. This is particularly
  // important for the Windows D3D11 render loops.
  QObject::connect(&application, &QCoreApplication::aboutToQuit, window,
                   [window] {
                     window->hide();
                     window->releaseResources();
                   });
  const auto seek_and_wait = [viewport](qulonglong frame) {
    return viewport->seekTrajectory(frame) &&
           viewport->waitForTrajectoryTask(5000) &&
           viewport->trajectoryFrame() == frame;
  };
  QPointer<molshredder::desktop::MolecularViewport> viewport_guard{viewport};
  auto graphics_probe_started = std::make_shared<std::atomic_bool>(false);
  QObject::connect(
      window, &QQuickWindow::afterRendering, window,
      [window, viewport_guard, graphics_probe_started] {
        if (graphics_probe_started->exchange(true)) return;
        qInfo("MolShredder graphics API: %d",
              static_cast<int>(window->rendererInterface()->graphicsApi()));
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
  // Main.qml creates a visible Window, so sceneGraphInitialized can precede
  // the C++ connection above on fast render loops. Request another frame and
  // probe from afterRendering, which is both repeatable and on the render
  // thread where QRhi resources are valid.
  window->update();
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
  if (volume_slice_smoke) {
    const auto sliced = viewport->hasVolume() &&
                        viewport->setVolumeSlice(QStringLiteral("z"), 1U);
    const auto &packet = viewport->renderPacket();
    const auto pick = packet.pick_targets.begin();
    if (sliced && pick != packet.pick_targets.end()) {
      viewport->deliverPickResult(viewport->pickRequestRevision(),
                                  viewport->packetRevision(), pick->first);
    }
    auto *panel =
        window->findChild<QObject *>(QStringLiteral("volumePanel"));
    const auto passed =
        sliced && viewport->volumeMode() == QStringLiteral("slice") &&
        viewport->volumeSliceAxis() == QStringLiteral("z") &&
        viewport->volumeSliceIndex() == 1U &&
        packet.mesh_vertices.size() == 4U &&
        packet.mesh_triangles.size() == 2U &&
        packet.pick_targets.size() == 1U &&
        viewport->selectionText().contains(QStringLiteral("Volume")) &&
        panel != nullptr &&
        panel->property("actionId").toString() ==
            QStringLiteral("represent.volume-slice") &&
        panel->property("entryAction").value<QObject *>() != nullptr;
    if (!passed) {
      qCritical("MolShredder desktop volume slice smoke failed: %s",
                viewport->statusText().toUtf8().constData());
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop volume slice ready: axis=z index=1 vertices=4 triangles=2 picking=volume canonical=shared panel=visible");
  }
  if (molecular_surface_smoke) {
    const auto shown = viewport->setMolecularSurface(
        QStringLiteral("vdw"), QStringLiteral("all"), 0.0, 0.5,
        8U * 1024U * 1024U, 512U * 1024U * 1024U);
    const auto &packet = viewport->renderPacket();
    const auto pick = packet.pick_targets.begin();
    if (shown && pick != packet.pick_targets.end()) {
      viewport->deliverPickResult(viewport->pickRequestRevision(),
                                  viewport->packetRevision(), pick->first);
    }
    QCoreApplication::processEvents();
    auto *action = window->findChild<QObject *>(
        QStringLiteral("representSurfaceAction"));
    auto *panel = window->findChild<QObject *>(QStringLiteral("surfacePanel"));
    const auto passed = shown && viewport->representation() ==
                                     QStringLiteral("surface") &&
                        !packet.mesh_vertices.empty() &&
                        !packet.mesh_triangles.empty() &&
                        !packet.pick_targets.empty() &&
                        viewport->selectionText().contains(
                            QStringLiteral("Atom 1")) &&
                        action != nullptr && panel != nullptr &&
                        panel->property("actionId").toString() ==
                            QStringLiteral("represent.surface") &&
                        panel->property("entryAction").value<QObject *>() !=
                            nullptr;
    if (!passed) {
      qCritical("MolShredder desktop molecular surface smoke failed: shown=%d representation=%s vertices=%llu triangles=%llu picks=%llu selection=%s action=%d status=%s",
                shown ? 1 : 0,
                viewport->representation().toUtf8().constData(),
                static_cast<unsigned long long>(packet.mesh_vertices.size()),
                static_cast<unsigned long long>(packet.mesh_triangles.size()),
                static_cast<unsigned long long>(packet.pick_targets.size()),
                viewport->selectionText().toUtf8().constData(),
                action != nullptr ? 1 : 0,
                viewport->statusText().toUtf8().constData());
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop molecular surface workflow ready: kind=vdw picking=atom canonical=shared action=represent.surface");
  }
  if (direct_volume_smoke) {
    const auto cancellation_started =
        viewport->hasVolume() && viewport->setDirectVolume(
                                     QStringLiteral("density"), 0.5, 4096U,
                                     256U, 512U * 1024U * 1024U);
    viewport->cancelDirectVolumeTask();
    QCoreApplication::processEvents();
    const auto cancelled =
        cancellation_started && !viewport->volumeTaskRunning() &&
        viewport->volumeTaskStage() == QStringLiteral("cancelled") &&
        viewport->directVolumeData() == nullptr;
    const auto shown = viewport->hasVolume() && viewport->setDirectVolume(
        QStringLiteral("fire"), 0.5, 4096U, 256U,
        512U * 1024U * 1024U);
    const auto completed = shown && viewport->waitForDirectVolumeTask(5000);
    const auto *data = viewport->directVolumeData();
    auto *action = window->findChild<QObject *>(
        QStringLiteral("directVolumeAction"));
    auto *panel = window->findChild<QObject *>(QStringLiteral("volumePanel"));
    auto *cancel_button = window->findChild<QObject *>(
        QStringLiteral("directVolumeTaskCancelButton"));
    const auto stereo_enabled = viewport->setStereo(
        true, QStringLiteral("side_by_side"), false, 2.0, 2.1,
        QStringLiteral("optimized"));
    const auto passed = cancelled && shown && completed && data != nullptr &&
                        viewport->volumeMode() == QStringLiteral("direct") &&
                        !viewport->volumeTaskRunning() &&
                        viewport->volumeTaskProgress() == 1.0 &&
                        viewport->volumeTaskStage() ==
                            QStringLiteral("complete") &&
                        data->transfer_lookup.size() == 256U &&
                        data->required_texture_bytes <=
                            512U * 1024U * 1024U &&
                        viewport->directVolumePickId() != 0U &&
                        stereo_enabled && viewport->stereoEnabled() &&
                        action != nullptr && panel != nullptr &&
                        cancel_button != nullptr;
    if (!passed) {
      qCritical("MolShredder desktop direct volume smoke failed: cancelled=%d shown=%d completed=%d stereo=%d stage=%s status=%s",
                cancelled ? 1 : 0, shown ? 1 : 0, completed ? 1 : 0,
                stereo_enabled ? 1 : 0,
                viewport->volumeTaskStage().toUtf8().constData(),
                viewport->statusText().toUtf8().constData());
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop direct volume workflow ready: ramp=fire mode=post-classified picking=volume stereo=side_by_side canonical=shared action=represent.volume-render");
    auto *direct_pick_retry = new QTimer{viewport};
    direct_pick_retry->setInterval(60);
    const auto direct_pick_in_flight_revision =
        std::make_shared<std::uint64_t>(0U);
    QObject::connect(
        direct_pick_retry, &QTimer::timeout, viewport,
        [viewport, direct_pick_retry, direct_pick_in_flight_revision,
         &application] {
          if (viewport->selectionText().contains(QStringLiteral("Volume")) &&
              viewport->volumeGpuState() == QStringLiteral("ready")) {
            direct_pick_retry->stop();
            qInfo("MolShredder direct volume GPU picking ready: target=volume state=ready");
            application.quit();
            return;
          }
          if (*direct_pick_in_flight_revision != 0U &&
              viewport->lastPickCompletionRevision() <
                  *direct_pick_in_flight_revision) {
            return;
          }
          viewport->pickAt(viewport->width() * 0.5,
                           viewport->height() * 0.5);
          *direct_pick_in_flight_revision = viewport->pickRequestRevision();
        });
    direct_pick_retry->start();
    QTimer::singleShot(5000, direct_pick_retry,
                       [direct_pick_retry, &application] {
      direct_pick_retry->stop();
      qCritical("MolShredder direct volume GPU picking timed out");
      application.exit(EXIT_FAILURE);
    });
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
  if (edit_smoke) {
    const auto before = viewport->renderPacket();
    const auto edited = viewport->editAtomPosition(1U, 9.0, 8.0, 7.0);
    const auto edited_packet = viewport->renderPacket();
    const auto moved = edited && !before.spheres.empty() &&
                       !edited_packet.spheres.empty() &&
                       edited_packet.spheres.front().center.x == 9.0F &&
                       edited_packet.spheres.front().center.y == 8.0F &&
                       edited_packet.spheres.front().center.z == 7.0F;
    const auto undone = viewport->undoEdit();
    const auto undo_packet = viewport->renderPacket();
    const auto restored = undone && !undo_packet.spheres.empty() &&
                          undo_packet.spheres.front().center ==
                              before.spheres.front().center;
    const auto redone = viewport->redoEdit();
    const auto redo_packet = viewport->renderPacket();
    const auto replayed = redone && !redo_packet.spheres.empty() &&
                          redo_packet.spheres.front().center.x == 9.0F;
    const auto opened = QMetaObject::invokeMethod(
        window, "openCoordinateEditor", Qt::DirectConnection);
    auto *overlay = window->findChild<QObject *>(
        QStringLiteral("coordinateEditOverlay"));
    auto *apply_button = window->findChild<QObject *>(
        QStringLiteral("coordinateApplyButton"));
    auto *undo_button = window->findChild<QObject *>(
        QStringLiteral("coordinateUndoButton"));
    auto *redo_button = window->findChild<QObject *>(
        QStringLiteral("coordinateRedoButton"));
    const auto history = viewport->editHistoryJson();
    const auto passed = moved && restored && replayed && opened &&
                        overlay != nullptr && apply_button != nullptr &&
                        undo_button != nullptr && redo_button != nullptr &&
                        overlay->property("visible").toBool() &&
                        history.contains(QStringLiteral("\"undo_count\":1")) &&
                        history.contains(QStringLiteral("\"redo_count\":0"));
    if (!passed) {
      qCritical("MolShredder desktop edit smoke failed: moved=%d restored=%d replayed=%d history=%s status=%s",
                moved ? 1 : 0, restored ? 1 : 0, replayed ? 1 : 0,
                history.toUtf8().constData(),
                viewport->statusText().toUtf8().constData());
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop coordinate edit ready: apply=shared undo=shared redo=shared history=bounded editor=visible");
  }
  if (builder_smoke) {
    const auto built = viewport->buildMolecule(
        QStringLiteral("carbonyl"),
        QStringLiteral("C,6,0,0,0,0;O,8,1.2,0,0,0"),
        QStringLiteral("1,2,double"), QStringLiteral("LIG"),
        QStringLiteral("A"), 1, QStringLiteral("angstrom"), 1048576U);
    const auto object_count = viewport->objectItems().size();
    const auto undone = viewport->undoEdit();
    const auto removed_count = viewport->objectItems().size();
    const auto redone = viewport->redoEdit();
    const auto restored_count = viewport->objectItems().size();
    const auto atom_edited = viewport->editAtomProperties(
        1U, QStringLiteral("C1"), QStringLiteral("6"),
        QStringLiteral("1"));
    const auto residue_edited = viewport->editResidueProperties(
        1U, QStringLiteral("CRB"), QStringLiteral("B"),
        QStringLiteral("7"));
    const auto bond_edited =
        viewport->editBondOrder(1U, QStringLiteral("single"));
    const auto bond_undone = viewport->undoEdit();
    const auto bond_redone = viewport->redoEdit();
    const auto malformed = viewport->buildMolecule(
        QStringLiteral("invalid"), QStringLiteral("C,6,0,0,0,0"),
        QStringLiteral("1,2,single"), QStringLiteral("LIG"),
        QStringLiteral("A"), 1, QStringLiteral("angstrom"), 1048576U);
    const auto opened = QMetaObject::invokeMethod(
        window, "openMoleculeBuilder", Qt::DirectConnection);
    auto *atom_properties_action = window->findChild<QObject *>(
        QStringLiteral("atomPropertiesAction"));
    const auto topology_opened =
        atom_properties_action != nullptr &&
        QMetaObject::invokeMethod(atom_properties_action, "trigger",
                                  Qt::DirectConnection);
    auto *overlay = window->findChild<QObject *>(
        QStringLiteral("moleculeBuilderOverlay"));
    auto *apply_button = window->findChild<QObject *>(
        QStringLiteral("moleculeBuilderApplyButton"));
    auto *topology_overlay = window->findChild<QObject *>(
        QStringLiteral("topologyEditOverlay"));
    auto *topology_apply_button = window->findChild<QObject *>(
        QStringLiteral("topologyEditApplyButton"));
    const auto chemistry = viewport->chemicalSemanticsJson();
    const auto &packet = viewport->renderPacket();
    const auto passed = built && undone && redone && atom_edited &&
                        residue_edited && bond_edited && bond_undone &&
                        bond_redone && !malformed &&
                        object_count == 1 && removed_count == 0 &&
                        restored_count == 1 &&
                        viewport->objectItems().size() == object_count &&
                        viewport->atomCount() == 2U &&
                        packet.spheres.size() == 2U && opened &&
                        overlay != nullptr && apply_button != nullptr &&
                        overlay->property("visible").toBool() &&
                        overlay->property("actionId").toString() ==
                            QStringLiteral("build.molecule") &&
                        topology_opened && topology_overlay != nullptr &&
                        topology_apply_button != nullptr &&
                        topology_overlay->property("visible").toBool() &&
                        topology_overlay->property("actionId").toString() ==
                            QStringLiteral("edit.atom-properties") &&
                        chemistry.contains(
                            QStringLiteral("\"topology_version\":4")) &&
                        chemistry.contains(
                            QStringLiteral("\"formal_charge_present_count\":2"));
    if (!passed) {
      qCritical("MolShredder desktop molecule builder smoke failed: built=%d undo=%d redo=%d atom=%d residue=%d bond=%d malformed=%d objects=%lld atoms=%llu status=%s",
                built ? 1 : 0, undone ? 1 : 0, redone ? 1 : 0,
                atom_edited ? 1 : 0, residue_edited ? 1 : 0,
                bond_edited ? 1 : 0,
                malformed ? 1 : 0,
                static_cast<long long>(viewport->objectItems().size()),
                static_cast<unsigned long long>(viewport->atomCount()),
                viewport->statusText().toUtf8().constData());
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop molecule builder ready: residue=1 atoms=2 bonds=1 properties=atom-residue-bond canonical=shared undo=bounded panels=visible");
  }
  if (analysis_smoke) {
    const auto wait_for_analysis = [&] {
      if (!viewport->analysisTaskRunning())
        return viewport->analysisTaskStage() == QStringLiteral("complete");
      QEventLoop wait;
      QTimer timeout;
      timeout.setSingleShot(true);
      QObject::connect(&timeout, &QTimer::timeout, &wait, &QEventLoop::quit);
      QObject::connect(
          viewport,
          &molshredder::desktop::MolecularViewport::analysisTaskChanged,
          &wait, [&] {
            if (!viewport->analysisTaskRunning()) wait.quit();
          });
      timeout.start(5000);
      wait.exec();
      return !viewport->analysisTaskRunning() &&
             viewport->analysisTaskStage() == QStringLiteral("complete");
    };
    const auto center = viewport->analyzeCenter(
        QStringLiteral("all"), QStringLiteral("com"),
        QStringLiteral("desktop-com"));
    const auto distance = viewport->analyzeDistance(
        QStringLiteral("index 1"), QStringLiteral("index 2"),
        QStringLiteral("raw"), QStringLiteral("desktop-distance"));
    const auto sasa = viewport->analyzeSasa(
        QStringLiteral("all"), 1.4, 256U, 100000U,
        QStringLiteral("desktop-sasa"));
    const auto sasa_complete = sasa && wait_for_analysis();
    const auto rdf = viewport->analyzeRdf(
        QStringLiteral("all"), QString{}, 5.0, 1.0,
        QStringLiteral("count"), QStringLiteral("raw"), 100U,
        QStringLiteral("desktop-rdf"));
    const auto rdf_complete = rdf && wait_for_analysis();
    const auto detail = viewport->analysisResultJson(2U);
    const auto sasa_detail = viewport->analysisResultJson(3U);
    const auto rdf_detail = viewport->analysisResultJson(4U);
    const auto opened = QMetaObject::invokeMethod(
        window, "openAnalyze", Qt::DirectConnection);
    auto *overlay =
        window->findChild<QObject *>(QStringLiteral("analysisOverlay"));
    auto *plot_canvas=
        window->findChild<QObject *>(QStringLiteral("analysisPlotCanvas"));
    auto *cancel_button=window->findChild<QObject *>(
        QStringLiteral("analysisTaskCancelButton"));
    auto *progress_text=window->findChild<QObject *>(
        QStringLiteral("analysisTaskProgress"));
    const auto before_hide = viewport->renderPacket();
    const auto hidden = viewport->setAnalysisResultVisible(2U, false);
    const auto &after_hide = viewport->renderPacket();
    const auto passed =
        center && distance && sasa_complete && rdf_complete &&
        viewport->analysisItems().size() == 4U &&
        viewport->analysisLabelItems().size() == 1U &&
        before_hide.labels.size() == 2U && before_hide.lines.size() >= 8U &&
        after_hide.labels.size() == 1U && hidden &&
        detail.contains(QStringLiteral("molshredder-distance-v1")) &&
        sasa_detail.contains(QStringLiteral(
            "molshredder-shrake-rupley-fibonacci-v1")) &&
        rdf_detail.contains(QStringLiteral("molshredder-rdf-histogram-v1")) &&
        detail.contains(QStringLiteral("source_status")) && opened &&
        overlay != nullptr && overlay->property("visible").toBool() &&
        plot_canvas != nullptr && cancel_button != nullptr &&
        progress_text != nullptr;
    if (!passed) {
      qCritical("MolShredder desktop analysis result smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop analysis ready: results=4 overlay=marker-dash-label sasa-rdf=bounded canonical=shared panel=visible");
  }
  if (save_path.has_value() &&
      !viewport->saveStructure(QUrl::fromLocalFile(*save_path), save_all)) {
    qCritical("%s", viewport->statusText().toUtf8().constData());
    return EXIT_FAILURE;
  }
  if ((script_smoke || script_cancel_smoke || isolated_script_smoke) &&
      !script_path.has_value()) {
    qCritical("Desktop script smoke requires --script");
    return EXIT_FAILURE;
  }
  if (script_smoke || script_cancel_smoke || isolated_script_smoke) {
    QObject::connect(
        viewport, &molshredder::desktop::MolecularViewport::scriptFinished,
        &application,
        [viewport, script_smoke, script_cancel_smoke, isolated_script_smoke,
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
          } else if (isolated_script_smoke) {
            const auto passed =
                succeeded && !viewport->scriptRunning() &&
                viewport->objectItems().isEmpty() &&
                viewport->scriptOutput().contains(
                    QStringLiteral("gui-isolated-output"));
            if (!passed) {
              qCritical("MolShredder desktop isolated Python script smoke "
                        "failed");
              application.exit(EXIT_FAILURE);
              return;
            }
            qInfo("MolShredder desktop isolated Python script ready: "
                  "objects=0 output=captured parent=unchanged");
          }
          application.quit();
        });
    QTimer::singleShot(5000, &application, [&application] {
      qCritical("MolShredder desktop Python script smoke timed out");
      application.exit(EXIT_FAILURE);
    });
  }
  if (script_path.has_value() &&
      !viewport->runPythonScript(QUrl::fromLocalFile(*script_path),
                                 isolated_script_smoke)) {
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
  if (session_smoke) {
    QTemporaryDir session_directory;
    const auto session_path =
        session_directory.filePath(QStringLiteral("desktop.msess"));
    const auto autosave_path =
        session_directory.filePath(QStringLiteral("desktop.autosave.msess"));
    const auto recovery_path = session_directory.filePath(
        QStringLiteral("desktop.autosave.previous.msess"));
    const auto scene_stored =
        viewport->storeNamedScene(QStringLiteral("desktop baseline"));
    const auto movie_configured = viewport->configureMovie(3U, 24.0, true);
    const auto key_stored = viewport->setMovieKeyframe(
        2U, QStringLiteral("desktop baseline"), -1);
    const auto hidden = viewport->setObjectVisible(1U, false);
    const auto movie_sought = viewport->seekMovie(2U);
    const auto session_saved = viewport->saveSession(
        QUrl::fromLocalFile(session_path), QStringLiteral("views"));
    const auto hidden_after_save = viewport->setObjectVisible(1U, false);
    const auto session_loaded = viewport->loadSession(
        QUrl::fromLocalFile(session_path));
    const auto first_autosave = viewport->autosaveSession(
        QUrl::fromLocalFile(autosave_path),
        QUrl::fromLocalFile(recovery_path));
    const auto second_autosave = viewport->autosaveSession(
        QUrl::fromLocalFile(autosave_path),
        QUrl::fromLocalFile(recovery_path));
    const auto invoked = QMetaObject::invokeMethod(
        window, "restoreSessionVisiblePanels", Qt::DirectConnection);
    auto *scene_list =
        window->findChild<QObject *>(QStringLiteral("namedSceneList"));
    auto *store_scene_button = window->findChild<QObject *>(
        QStringLiteral("storeNamedSceneButton"));
    auto *configure_movie_button = window->findChild<QObject *>(
        QStringLiteral("configureMovieButton"));
    auto *store_key_button = window->findChild<QObject *>(
        QStringLiteral("storeMovieKeyframeButton"));
    auto *play_movie_button = window->findChild<QObject *>(
        QStringLiteral("playMovieButton"));
    const auto movie = viewport->movieState();
    const auto objects = viewport->objectItems();
    if (!session_directory.isValid() || !scene_stored || !movie_configured ||
        !key_stored || !hidden || !movie_sought || !session_saved ||
        !hidden_after_save || !session_loaded || !first_autosave ||
        !second_autosave || !invoked || scene_list == nullptr ||
        store_scene_button == nullptr || configure_movie_button == nullptr ||
        store_key_button == nullptr || play_movie_button == nullptr ||
        viewport->sceneItems().size() != 1 ||
        viewport->sessionVisiblePanels() != QStringLiteral("views") ||
        !movie.value(QStringLiteral("configured")).toBool() ||
        movie.value(QStringLiteral("currentFrame")).toULongLong() != 2U ||
        movie.value(QStringLiteral("keyframeCount")).toULongLong() != 1U ||
        objects.isEmpty() ||
        !objects.front().toMap().value(QStringLiteral("visible")).toBool() ||
        !QFileInfo::exists(session_path) || !QFileInfo::exists(autosave_path) ||
        !QFileInfo::exists(recovery_path)) {
      qCritical("MolShredder desktop session/movie smoke failed");
      return EXIT_FAILURE;
    }
    qInfo("MolShredder desktop session ready: scenes=1 movie=typed "
          "save-load=atomic autosave=rotated panel=visible");
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
    auto *pick_retry = new QTimer(viewport);
    pick_retry->setInterval(500);
    QObject::connect(
        viewport, &molshredder::desktop::MolecularViewport::selectionChanged,
        &application, [viewport, pick_retry, &application, capture_requested] {
          if (viewport->selectionText().startsWith(QStringLiteral("Atom "))) {
            pick_retry->stop();
            qInfo("MolShredder desktop GPU picking ready: %s",
                  viewport->selectionText().toUtf8().constData());
            if (!capture_requested)
              application.quit();
          }
        });
    QObject::connect(pick_retry, &QTimer::timeout, viewport, [viewport] {
      viewport->pickAt(viewport->width() * 0.5, viewport->height() * 0.5);
    });
    QTimer::singleShot(750, pick_retry, [pick_retry] { pick_retry->start(); });
    QTimer::singleShot(12000, &application, [&application] {
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
            QStringLiteral("trajectoryMappingSelector")) == nullptr) {
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
             !script_cancel_smoke && !isolated_script_smoke &&
             !graphics_info_smoke &&
             !system_info_panel_smoke && !stereo_smoke && !anaglyph_smoke &&
             !interleaved_smoke && !direct_volume_smoke) {
    QTimer::singleShot(1500, &application, &QCoreApplication::quit);
  }
  return application.exec();
}
