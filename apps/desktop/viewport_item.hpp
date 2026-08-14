#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <QQuickRhiItem>
#include <QElapsedTimer>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/command/registry.hpp"
#include "molshredder/gui/action_adapter.hpp"
#include "molshredder/render/packet.hpp"
#include "molshredder/scene/camera.hpp"

namespace molshredder::desktop {

class MolecularViewport : public QQuickRhiItem {
  Q_OBJECT
  QML_NAMED_ELEMENT(MolecularViewport)
  Q_PROPERTY(float angle READ angle WRITE setAngle NOTIFY angleChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString representation READ representation NOTIFY
                 representationChanged)
  Q_PROPERTY(qulonglong atomCount READ atomCount NOTIFY statusChanged)
  Q_PROPERTY(qulonglong primitiveCount READ primitiveCount NOTIFY statusChanged)
  Q_PROPERTY(QString selectionText READ selectionText NOTIFY selectionChanged)
  Q_PROPERTY(QVariantList objectItems READ objectItems NOTIFY objectsChanged)
  Q_PROPERTY(bool hasTrajectory READ hasTrajectory NOTIFY trajectoryChanged)
  Q_PROPERTY(qulonglong trajectoryFrame READ trajectoryFrame NOTIFY
                 trajectoryChanged)
  Q_PROPERTY(qulonglong trajectoryFrameCount READ trajectoryFrameCount NOTIFY
                 trajectoryChanged)
  Q_PROPERTY(bool trajectoryPlaying READ trajectoryPlaying NOTIFY
                 trajectoryChanged)
  Q_PROPERTY(QString playbackMode READ playbackMode NOTIFY trajectoryChanged)
  Q_PROPERTY(QString playbackDirection READ playbackDirection NOTIFY
                 trajectoryChanged)
  Q_PROPERTY(double trajectoryFps READ trajectoryFps NOTIFY trajectoryChanged)

 public:
  explicit MolecularViewport(QQuickItem* parent = nullptr);

  [[nodiscard]] QQuickRhiItemRenderer* createRenderer() override;

  [[nodiscard]] float angle() const noexcept { return angle_; }
  void setAngle(float angle);
  [[nodiscard]] const QString& statusText() const noexcept {
    return status_text_;
  }
  [[nodiscard]] const QString& representation() const noexcept {
    return representation_;
  }
  [[nodiscard]] qulonglong atomCount() const noexcept { return atom_count_; }
  [[nodiscard]] qulonglong primitiveCount() const noexcept {
    return primitive_count_;
  }
  [[nodiscard]] const QString& selectionText() const noexcept {
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
  [[nodiscard]] const QString& playbackMode() const noexcept {
    return playback_mode_;
  }
  [[nodiscard]] const QString& playbackDirection() const noexcept {
    return playback_direction_;
  }
  [[nodiscard]] double trajectoryFps() const noexcept {
    return trajectory_fps_;
  }

  Q_INVOKABLE bool loadStructure(const QUrl& url);
  Q_INVOKABLE bool loadTrajectory(const QUrl& url);
  Q_INVOKABLE bool seekTrajectory(qulonglong frame);
  Q_INVOKABLE bool setTrajectoryPlaying(bool playing);
  Q_INVOKABLE bool stepTrajectory(int direction);
  Q_INVOKABLE bool setPlaybackMode(const QString& mode);
  Q_INVOKABLE bool setPlaybackDirection(const QString& direction);
  Q_INVOKABLE bool setTrajectoryFps(double frames_per_second);
  Q_INVOKABLE bool tickTrajectory(double elapsed_milliseconds);
  Q_INVOKABLE bool setRepresentation(const QString& representation);
  Q_INVOKABLE void orbit(double delta_x, double delta_y);
  Q_INVOKABLE void pan(double delta_x, double delta_y);
  Q_INVOKABLE void dolly(double delta);
  Q_INVOKABLE void resetView();
  Q_INVOKABLE void pickAt(double x, double y);
  Q_INVOKABLE bool activateObject(qulonglong object_id);
  Q_INVOKABLE bool setObjectVisible(qulonglong object_id, bool visible);

  void setRenderPacket(render::RenderPacket packet);
  [[nodiscard]] const render::RenderPacket& renderPacket() const noexcept {
    return packet_;
  }
  [[nodiscard]] std::uint64_t packetRevision() const noexcept {
    return packet_revision_;
  }
  [[nodiscard]] const scene::Camera* camera() const noexcept {
    return camera_.has_value() ? &camera_.value() : nullptr;
  }
  [[nodiscard]] std::uint64_t cameraRevision() const noexcept {
    return camera_revision_;
  }
  [[nodiscard]] std::uint64_t pickRequestRevision() const noexcept {
    return pick_request_revision_;
  }
  [[nodiscard]] QPointF pickPosition() const noexcept { return pick_position_; }
  void deliverPickResult(std::uint64_t request_revision,
                         std::uint64_t packet_revision,
                         std::uint64_t pick_id);

 signals:
  void angleChanged();
  void statusChanged();
  void representationChanged();
  void selectionChanged();
  void objectsChanged();
  void trajectoryChanged();

 private:
  [[nodiscard]] bool rebuildRepresentation();
  [[nodiscard]] bool rebuildScenePacket();
  void syncActiveRepresentationName();
  void syncTrajectoryState();
  void onPlaybackTick();
  [[nodiscard]] bool invokeTrajectoryAction(
      std::string command_name,
      std::map<std::string, std::string, std::less<>> parameters,
      QString failure_prefix, bool rebuild_scene);
  void setStatus(QString status, qulonglong atom_count,
                 qulonglong primitive_count);

  std::shared_ptr<application::Workspace> workspace_;
  command::Registry registry_;
  application::Dispatcher dispatcher_;
  gui::ActionAdapter actions_;
  render::RenderPacket packet_;
  std::uint64_t packet_revision_{1U};
  float angle_{};
  QString status_text_{QStringLiteral("Demo packet · open a PDB or mmCIF file")};
  QString representation_{QStringLiteral("spheres")};
  qulonglong atom_count_{};
  qulonglong primitive_count_{};
  QString selection_text_{QStringLiteral("No selection")};
  std::optional<scene::Camera> camera_;
  std::uint64_t camera_revision_{1U};
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
};

}  // namespace molshredder::desktop
