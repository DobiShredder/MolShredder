#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <QElapsedTimer>
#include <QPointF>
#include <QQuickRhiItem>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/runtime_diagnostics.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/command/registry.hpp"
#include "molshredder/gui/action_adapter.hpp"
#include "molshredder/render/packet.hpp"
#include "molshredder/scene/camera.hpp"
#include "molshredder/scene/stereo.hpp"

namespace molshredder::desktop {

class MolecularViewport : public QQuickRhiItem {
  Q_OBJECT
  QML_NAMED_ELEMENT(MolecularViewport)
  Q_PROPERTY(float angle READ angle WRITE setAngle NOTIFY angleChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(
      QString representation READ representation NOTIFY representationChanged)
  Q_PROPERTY(qulonglong atomCount READ atomCount NOTIFY statusChanged)
  Q_PROPERTY(qulonglong primitiveCount READ primitiveCount NOTIFY statusChanged)
  Q_PROPERTY(QString selectionText READ selectionText NOTIFY selectionChanged)
  Q_PROPERTY(QVariantList objectItems READ objectItems NOTIFY objectsChanged)
  Q_PROPERTY(bool hasTrajectory READ hasTrajectory NOTIFY trajectoryChanged)
  Q_PROPERTY(
      qulonglong trajectoryFrame READ trajectoryFrame NOTIFY trajectoryChanged)
  Q_PROPERTY(qulonglong trajectoryFrameCount READ trajectoryFrameCount NOTIFY
                 trajectoryChanged)
  Q_PROPERTY(
      bool trajectoryPlaying READ trajectoryPlaying NOTIFY trajectoryChanged)
  Q_PROPERTY(QString playbackMode READ playbackMode NOTIFY trajectoryChanged)
  Q_PROPERTY(
      QString playbackDirection READ playbackDirection NOTIFY trajectoryChanged)
  Q_PROPERTY(double trajectoryFps READ trajectoryFps NOTIFY trajectoryChanged)
  Q_PROPERTY(bool hasVolume READ hasVolume NOTIFY volumeChanged)
  Q_PROPERTY(double volumeLevel READ volumeLevel NOTIFY volumeChanged)
  Q_PROPERTY(double volumeMinimum READ volumeMinimum NOTIFY volumeChanged)
  Q_PROPERTY(double volumeMaximum READ volumeMaximum NOTIFY volumeChanged)
  Q_PROPERTY(QString scriptOutput READ scriptOutput NOTIFY scriptOutputChanged)
  Q_PROPERTY(bool scriptRunning READ scriptRunning NOTIFY scriptRunningChanged)
  Q_PROPERTY(QVariantList viewItems READ viewItems NOTIFY viewsChanged)

public:
  explicit MolecularViewport(QQuickItem *parent = nullptr);
  ~MolecularViewport() override;

  [[nodiscard]] QQuickRhiItemRenderer *createRenderer() override;

  [[nodiscard]] float angle() const noexcept { return angle_; }
  void setAngle(float angle);
  [[nodiscard]] const QString &statusText() const noexcept {
    return status_text_;
  }
  [[nodiscard]] const QString &representation() const noexcept {
    return representation_;
  }
  [[nodiscard]] qulonglong atomCount() const noexcept { return atom_count_; }
  [[nodiscard]] qulonglong primitiveCount() const noexcept {
    return primitive_count_;
  }
  [[nodiscard]] const QString &selectionText() const noexcept {
    return selection_text_;
  }
  [[nodiscard]] QVariantList objectItems() const;
  [[nodiscard]] bool hasTrajectory() const noexcept { return has_trajectory_; }
  [[nodiscard]] qulonglong trajectoryFrame() const noexcept {
    return trajectory_frame_;
  }
  [[nodiscard]] qulonglong trajectoryFrameCount() const noexcept {
    return trajectory_frame_count_;
  }
  [[nodiscard]] bool trajectoryPlaying() const noexcept {
    return trajectory_playing_;
  }
  [[nodiscard]] const QString &playbackMode() const noexcept {
    return playback_mode_;
  }
  [[nodiscard]] const QString &playbackDirection() const noexcept {
    return playback_direction_;
  }
  [[nodiscard]] double trajectoryFps() const noexcept {
    return trajectory_fps_;
  }
  [[nodiscard]] bool hasVolume() const noexcept { return has_volume_; }
  [[nodiscard]] double volumeLevel() const noexcept { return volume_level_; }
  [[nodiscard]] double volumeMinimum() const noexcept { return volume_minimum_; }
  [[nodiscard]] double volumeMaximum() const noexcept { return volume_maximum_; }
  [[nodiscard]] const QString &scriptOutput() const noexcept {
    return script_output_;
  }
  [[nodiscard]] bool scriptRunning() const noexcept { return script_running_; }
  [[nodiscard]] QVariantList viewItems() const;

  Q_INVOKABLE bool loadStructure(const QUrl &url);
  Q_INVOKABLE bool saveStructure(const QUrl &url, bool all_frames);
  Q_INVOKABLE bool loadTrajectory(const QUrl &url,
                                  const QString &coordinate_unit);
  Q_INVOKABLE bool seekTrajectory(qulonglong frame);
  Q_INVOKABLE bool setTrajectoryPlaying(bool playing);
  Q_INVOKABLE bool stepTrajectory(int direction);
  Q_INVOKABLE bool setPlaybackMode(const QString &mode);
  Q_INVOKABLE bool setPlaybackDirection(const QString &direction);
  Q_INVOKABLE bool setTrajectoryFps(double frames_per_second);
  Q_INVOKABLE bool tickTrajectory(double elapsed_milliseconds);
  Q_INVOKABLE bool setRepresentation(const QString &representation);
  Q_INVOKABLE bool setVolumeIsosurface(double level);
  Q_INVOKABLE void orbit(double delta_x, double delta_y);
  Q_INVOKABLE void pan(double delta_x, double delta_y);
  Q_INVOKABLE void dolly(double delta);
  Q_INVOKABLE void resetView();
  Q_INVOKABLE bool resetViewAnimated(double duration_seconds, int hand);
  Q_INVOKABLE bool centerSelection(const QString &selection, bool move_origin,
                                   const QString &state,
                                   double duration_seconds, int hand);
  Q_INVOKABLE bool zoomSelection(const QString &selection, double buffer,
                                 bool complete, const QString &state,
                                 double duration_seconds, int hand);
  Q_INVOKABLE bool orientSelection(const QString &selection,
                                   const QString &state,
                                   double duration_seconds, int hand);
  Q_INVOKABLE bool setOriginSelection(const QString &selection,
                                      const QString &state);
  Q_INVOKABLE bool setOriginPosition(double x, double y, double z,
                                     const QString &object_reference);
  Q_INVOKABLE bool setObjectOriginSelection(
      const QString &object_reference, const QString &selection,
      const QString &state);
  Q_INVOKABLE bool resetObjectTransform(const QString &object_reference);
  Q_INVOKABLE bool clipCamera(const QString &mode, double distance,
                              const QString &selection,
                              const QString &state);
  Q_INVOKABLE QString clipRangeText() const;
  Q_INVOKABLE bool moveCamera(const QString &axis, double distance);
  Q_INVOKABLE bool turnCamera(const QString &axis, double angle_degrees);
  Q_INVOKABLE bool setProjection(const QString &mode,
                                 double field_of_view_degrees,
                                 bool preserve_scale);
  Q_INVOKABLE QString projectionModeText() const;
  Q_INVOKABLE double fieldOfViewDegrees() const;
  Q_INVOKABLE bool setStereo(bool enabled, const QString &mode,
                             bool swap_eyes, double shift_percent,
                             double angle_scale,
                             const QString &anaglyph_mode);
  Q_INVOKABLE bool stereoEnabled() const noexcept;
  Q_INVOKABLE QString stereoModeText() const;
  Q_INVOKABLE bool stereoSwapEyes() const noexcept;
  Q_INVOKABLE double stereoShiftPercent() const noexcept;
  Q_INVOKABLE double stereoAngleScale() const noexcept;
  Q_INVOKABLE QString anaglyphModeText() const;
  Q_INVOKABLE void pickAt(double x, double y);
  Q_INVOKABLE bool activateObject(qulonglong object_id);
  Q_INVOKABLE bool setObjectVisible(qulonglong object_id, bool visible);
  Q_INVOKABLE bool runPythonScript(const QUrl &url);
  Q_INVOKABLE void cancelPythonScript();
  Q_INVOKABLE void clearScriptOutput();
  Q_INVOKABLE QString systemInfoJson() const;
  Q_INVOKABLE bool storeNamedView(const QString &name);
  Q_INVOKABLE bool recallNamedView(const QString &name);
  Q_INVOKABLE bool recallNamedViewAnimated(const QString &name,
                                           double duration_seconds,
                                           int hand);
  Q_INVOKABLE bool deleteNamedView(const QString &name);
  Q_INVOKABLE bool clearNamedViews();
  Q_INVOKABLE QString pymolViewText() const;
  Q_INVOKABLE bool importPymolView(const QString &values);
  Q_INVOKABLE bool importPymolViewAnimated(const QString &values,
                                           double duration_seconds,
                                           int hand);

  void setGraphicsRuntimeInfo(application::GraphicsRuntimeInfo info);

  void setRenderPacket(render::RenderPacket packet);
  [[nodiscard]] const render::RenderPacket &renderPacket() const noexcept {
    return packet_;
  }
  [[nodiscard]] std::uint64_t packetRevision() const noexcept {
    return packet_revision_;
  }
  [[nodiscard]] const scene::Camera *camera() const noexcept {
    return camera_.has_value() ? &camera_.value() : nullptr;
  }
  [[nodiscard]] std::uint64_t cameraRevision() const noexcept {
    return camera_revision_;
  }
  [[nodiscard]] const scene::StereoParameters &stereo() const noexcept {
    return workspace_->stereo();
  }
  [[nodiscard]] std::uint64_t stereoRevision() const noexcept {
    return stereo_revision_;
  }
  [[nodiscard]] std::uint64_t pickRequestRevision() const noexcept {
    return pick_request_revision_;
  }
  [[nodiscard]] QPointF pickPosition() const noexcept { return pick_position_; }
  void deliverPickResult(std::uint64_t request_revision,
                         std::uint64_t packet_revision, std::uint64_t pick_id);

signals:
  void angleChanged();
  void statusChanged();
  void representationChanged();
  void selectionChanged();
  void objectsChanged();
  void trajectoryChanged();
  void volumeChanged();
  void scriptOutputChanged();
  void scriptRunningChanged();
  void scriptFinished(bool succeeded);
  void graphicsDiagnosticsChanged();
  void viewsChanged();

private:
  [[nodiscard]] bool rebuildRepresentation();
  [[nodiscard]] bool rebuildScenePacket();
  void syncActiveRepresentationName();
  void syncTrajectoryState();
  void onPlaybackTick();
  void onCameraAnimationTick();
  [[nodiscard]] bool invokeTrajectoryAction(
      std::string command_name,
      std::map<std::string, std::string, std::less<>> parameters,
      QString failure_prefix, bool rebuild_scene);
  void setStatus(QString status, qulonglong atom_count,
                 qulonglong primitive_count);
  void finishPythonScript(application::DispatchOutcome outcome);
  [[nodiscard]] bool setCameraThroughAction(const scene::Camera &camera);
  [[nodiscard]] bool invokeCameraAction(
      std::string command_name,
      std::map<std::string, std::string, std::less<>> parameters,
      double duration_seconds, int hand, QString success_status,
      QString failure_prefix);
  void syncCameraState();
  void cancelCameraAnimation();
  void startCameraAnimation(const scene::Camera &start,
                            const scene::Camera &end,
                            double duration_seconds, int hand);

  std::shared_ptr<application::Workspace> workspace_;
  std::shared_ptr<application::RuntimeDiagnostics> diagnostics_;
  command::Registry registry_;
  application::Dispatcher dispatcher_;
  gui::ActionAdapter actions_;
  render::RenderPacket packet_;
  std::uint64_t packet_revision_{1U};
  float angle_{};
  QString status_text_{
      QStringLiteral("Demo packet · open a PDB or mmCIF file")};
  QString representation_{QStringLiteral("spheres")};
  qulonglong atom_count_{};
  qulonglong primitive_count_{};
  QString selection_text_{QStringLiteral("No selection")};
  std::optional<scene::Camera> camera_;
  std::uint64_t camera_revision_{1U};
  std::uint64_t stereo_revision_{1U};
  QTimer camera_animation_timer_;
  QElapsedTimer camera_animation_elapsed_;
  std::optional<scene::Camera> camera_animation_start_;
  std::optional<scene::Camera> camera_animation_end_;
  double camera_animation_duration_seconds_{};
  int camera_animation_hand_{1};
  QPointF pick_position_;
  std::uint64_t pick_request_revision_{};
  QTimer playback_timer_;
  QElapsedTimer playback_elapsed_;
  bool has_trajectory_{};
  qulonglong trajectory_frame_{};
  qulonglong trajectory_frame_count_{};
  bool trajectory_playing_{};
  QString playback_mode_{QStringLiteral("once")};
  QString playback_direction_{QStringLiteral("forward")};
  double trajectory_fps_{30.0};
  bool has_volume_{};
  double volume_level_{};
  double volume_minimum_{};
  double volume_maximum_{};
  QString script_output_;
  operation::CancellationToken script_cancellation_;
  std::thread script_worker_;
  bool script_running_{};
};

} // namespace molshredder::desktop
