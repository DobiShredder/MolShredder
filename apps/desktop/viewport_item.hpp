#pragma once

#include <atomic>
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
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/runtime_diagnostics.hpp"
#include "molshredder/application/task_service.hpp"
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
  Q_PROPERTY(bool trajectoryTaskRunning READ trajectoryTaskRunning NOTIFY
                 trajectoryTaskChanged)
  Q_PROPERTY(double trajectoryTaskProgress READ trajectoryTaskProgress NOTIFY
                 trajectoryTaskChanged)
  Q_PROPERTY(QString trajectoryTaskStage READ trajectoryTaskStage NOTIFY
                 trajectoryTaskChanged)
  Q_PROPERTY(bool analysisTaskRunning READ analysisTaskRunning NOTIFY
                 analysisTaskChanged)
  Q_PROPERTY(double analysisTaskProgress READ analysisTaskProgress NOTIFY
                 analysisTaskChanged)
  Q_PROPERTY(QString analysisTaskStage READ analysisTaskStage NOTIFY
                 analysisTaskChanged)
  Q_PROPERTY(bool hasVolume READ hasVolume NOTIFY volumeChanged)
  Q_PROPERTY(double volumeLevel READ volumeLevel NOTIFY volumeChanged)
  Q_PROPERTY(double volumeMinimum READ volumeMinimum NOTIFY volumeChanged)
  Q_PROPERTY(double volumeMaximum READ volumeMaximum NOTIFY volumeChanged)
  Q_PROPERTY(QString volumeMode READ volumeMode NOTIFY volumeChanged)
  Q_PROPERTY(bool volumeTaskRunning READ volumeTaskRunning NOTIFY
                 volumeTaskChanged)
  Q_PROPERTY(double volumeTaskProgress READ volumeTaskProgress NOTIFY
                 volumeTaskChanged)
  Q_PROPERTY(QString volumeTaskStage READ volumeTaskStage NOTIFY
                 volumeTaskChanged)
  Q_PROPERTY(QString volumeGpuState READ volumeGpuState NOTIFY
                 volumeGpuStatusChanged)
  Q_PROPERTY(QString volumeGpuMessage READ volumeGpuMessage NOTIFY
                 volumeGpuStatusChanged)
  Q_PROPERTY(QString volumeSliceAxis READ volumeSliceAxis NOTIFY volumeChanged)
  Q_PROPERTY(qulonglong volumeSliceIndex READ volumeSliceIndex NOTIFY
                 volumeChanged)
  Q_PROPERTY(qulonglong volumeSliceMaximum READ volumeSliceMaximum NOTIFY
                 volumeChanged)
  Q_PROPERTY(QString scriptOutput READ scriptOutput NOTIFY scriptOutputChanged)
  Q_PROPERTY(bool scriptRunning READ scriptRunning NOTIFY scriptRunningChanged)
  Q_PROPERTY(QVariantList viewItems READ viewItems NOTIFY viewsChanged)
  Q_PROPERTY(QVariantList sceneItems READ sceneItems NOTIFY sessionChanged)
  Q_PROPERTY(QVariantMap movieState READ movieState NOTIFY sessionChanged)
  Q_PROPERTY(QString sessionVisiblePanels READ sessionVisiblePanels NOTIFY
                 sessionChanged)
  Q_PROPERTY(QVariantList analysisItems READ analysisItems NOTIFY
                 analysisResultsChanged)
  Q_PROPERTY(QVariantList analysisLabelItems READ analysisLabelItems NOTIFY
                 analysisResultsChanged)

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
  [[nodiscard]] std::uint64_t lastPickId() const noexcept {
    return last_pick_id_;
  }
  [[nodiscard]] std::uint64_t lastPickCompletionRevision() const noexcept {
    return last_pick_completion_revision_;
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
  [[nodiscard]] bool trajectoryTaskRunning() const noexcept {
    return trajectory_task_running_;
  }
  [[nodiscard]] double trajectoryTaskProgress() const noexcept {
    return trajectory_task_progress_;
  }
  [[nodiscard]] const QString &trajectoryTaskStage() const noexcept {
    return trajectory_task_stage_;
  }
  [[nodiscard]] bool analysisTaskRunning() const noexcept {
    return analysis_task_running_;
  }
  [[nodiscard]] double analysisTaskProgress() const noexcept {
    return analysis_task_progress_;
  }
  [[nodiscard]] const QString &analysisTaskStage() const noexcept {
    return analysis_task_stage_;
  }
  [[nodiscard]] bool hasVolume() const noexcept { return has_volume_; }
  [[nodiscard]] double volumeLevel() const noexcept { return volume_level_; }
  [[nodiscard]] double volumeMinimum() const noexcept { return volume_minimum_; }
  [[nodiscard]] double volumeMaximum() const noexcept { return volume_maximum_; }
  [[nodiscard]] const QString &volumeMode() const noexcept {
    return volume_mode_;
  }
  [[nodiscard]] bool volumeTaskRunning() const noexcept {
    return volume_task_running_;
  }
  [[nodiscard]] double volumeTaskProgress() const noexcept {
    return volume_task_progress_;
  }
  [[nodiscard]] const QString &volumeTaskStage() const noexcept {
    return volume_task_stage_;
  }
  [[nodiscard]] const QString &volumeGpuState() const noexcept {
    return volume_gpu_state_;
  }
  [[nodiscard]] const QString &volumeGpuMessage() const noexcept {
    return volume_gpu_message_;
  }
  [[nodiscard]] const QString &volumeSliceAxis() const noexcept {
    return volume_slice_axis_;
  }
  [[nodiscard]] qulonglong volumeSliceIndex() const noexcept {
    return volume_slice_index_;
  }
  [[nodiscard]] qulonglong volumeSliceMaximum() const noexcept {
    return volume_slice_maximum_;
  }
  [[nodiscard]] const QString &scriptOutput() const noexcept {
    return script_output_;
  }
  [[nodiscard]] bool scriptRunning() const noexcept { return script_running_; }
  [[nodiscard]] QVariantList viewItems() const;
  [[nodiscard]] QVariantList sceneItems() const;
  [[nodiscard]] QVariantMap movieState() const;
  [[nodiscard]] const QString &sessionVisiblePanels() const noexcept {
    return session_visible_panels_;
  }
  [[nodiscard]] QVariantList analysisItems() const;
  [[nodiscard]] QVariantList analysisLabelItems() const;
  Q_INVOKABLE QString trajectoryMappingText() const;

  Q_INVOKABLE bool loadStructure(const QUrl &url);
  Q_INVOKABLE bool loadStructures(const QVariantList &urls);
  Q_INVOKABLE bool saveStructure(const QUrl &url, bool all_frames);
  Q_INVOKABLE bool loadTrajectory(const QUrl &url,
                                  const QString &coordinate_unit,
                                  const QString &mapping =
                                      QStringLiteral("index"),
                                  const QString &atom_map = {});
  Q_INVOKABLE bool seekTrajectory(qulonglong frame);
  Q_INVOKABLE void cancelTrajectoryTask();
  [[nodiscard]] bool waitForTrajectoryTask(int timeout_milliseconds);
  Q_INVOKABLE bool setTrajectoryPlaying(bool playing);
  Q_INVOKABLE bool setVolumeIsosurface(double level);
  Q_INVOKABLE bool setVolumeSlice(const QString &axis, qulonglong index);
  Q_INVOKABLE bool setDirectVolume(const QString &preset,
                                   double sampling_step,
                                   qulonglong maximum_steps,
                                   qulonglong lookup_table_samples,
                                   qulonglong texture_budget_bytes);
  Q_INVOKABLE void cancelDirectVolumeTask();
  [[nodiscard]] bool waitForDirectVolumeTask(int timeout_milliseconds);
  Q_INVOKABLE bool hideDirectVolume();
  Q_INVOKABLE bool setMolecularSurface(const QString &kind,
                                       const QString &selection,
                                       double probe_radius_angstrom,
                                       double grid_spacing_angstrom,
                                       qulonglong voxel_budget,
                                       qulonglong memory_budget_bytes);
  Q_INVOKABLE bool hideMolecularSurface();
  Q_INVOKABLE bool stepTrajectory(int direction);
  Q_INVOKABLE bool setPlaybackMode(const QString &mode);
  Q_INVOKABLE bool setPlaybackDirection(const QString &direction);
  Q_INVOKABLE bool setTrajectoryFps(double frames_per_second);
  Q_INVOKABLE bool tickTrajectory(double elapsed_milliseconds);
  Q_INVOKABLE bool setRepresentation(const QString &representation);
  Q_INVOKABLE bool applyRepresentationVisibility(const QString &operation,
                                                  const QString &selection);
  Q_INVOKABLE bool applyRenderSetting(const QString &operation,
                                      const QString &name,
                                      const QString &value,
                                      const QString &scope,
                                      const QString &target);
  Q_INVOKABLE QString renderSettingJson(const QString &name,
                                        const QString &scope,
                                        const QString &target) const;
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
  Q_INVOKABLE bool defineSelection(const QString &name,
                                   const QString &expression, bool dynamic);
  Q_INVOKABLE bool selectAll();
  Q_INVOKABLE bool activateObject(qulonglong object_id);
  Q_INVOKABLE bool setObjectVisible(qulonglong object_id, bool visible);
  Q_INVOKABLE bool renameObject(qulonglong object_id, const QString &name);
  Q_INVOKABLE bool deleteObject(qulonglong object_id);
  Q_INVOKABLE bool reorderObject(qulonglong object_id,
                                 qulonglong one_based_position);
  Q_INVOKABLE bool editAtomPosition(qulonglong atom_id, double x, double y,
                                    double z);
  Q_INVOKABLE bool editAtomProperties(qulonglong atom_id,
                                      const QString &name,
                                      const QString &atomic_number,
                                      const QString &formal_charge);
  Q_INVOKABLE bool editResidueProperties(qulonglong atom_id,
                                         const QString &name,
                                         const QString &chain,
                                         const QString &residue_number);
  Q_INVOKABLE bool editBondOrder(qulonglong bond_id, const QString &order);
  Q_INVOKABLE bool undoEdit();
  Q_INVOKABLE bool redoEdit();
  Q_INVOKABLE QString editHistoryJson() const;
  Q_INVOKABLE bool buildMolecule(const QString &name, const QString &atoms,
                                 const QString &bonds,
                                 const QString &residue_name,
                                 const QString &chain,
                                 qlonglong residue_number,
                                 const QString &unit,
                                 qulonglong memory_budget_bytes);
  Q_INVOKABLE bool runPythonScript(const QUrl &url, bool isolated = false);
  Q_INVOKABLE void cancelPythonScript();
  Q_INVOKABLE void clearScriptOutput();
  Q_INVOKABLE QString systemInfoJson() const;
  Q_INVOKABLE QString chemicalSemanticsJson() const;
  Q_INVOKABLE QString chemicalPerceptionJson(bool apply) const;
  Q_INVOKABLE bool analyzeCenter(const QString &selection,
                                 const QString &mode,
                                 const QString &result_name);
  Q_INVOKABLE bool analyzeDistance(const QString &from, const QString &to,
                                   const QString &pbc,
                                   const QString &result_name);
  Q_INVOKABLE bool analyzeAngle(const QString &first, const QString &vertex,
                                const QString &third, const QString &pbc,
                                const QString &result_name);
  Q_INVOKABLE bool analyzeDihedral(const QString &first, const QString &second,
                                   const QString &third,
                                   const QString &fourth, const QString &pbc,
                                   const QString &result_name);
  Q_INVOKABLE bool analyzeSasa(const QString &selection, double probe_radius,
                               qulonglong samples,
                               qulonglong evaluation_budget,
                               const QString &result_name);
  Q_INVOKABLE bool analyzeRdf(const QString &first, const QString &second,
                              double maximum_radius, double bin_width,
                              const QString &normalization,
                              const QString &pbc, qulonglong evaluation_budget,
                              const QString &result_name);
  Q_INVOKABLE bool analyzeContacts(const QString &first,
                                   const QString &second, double cutoff,
                                   const QString &pbc,
                                   const QString &result_name);
  Q_INVOKABLE bool analyzeTrajectoryRmsd(const QString &selection,
                                         qulonglong reference,
                                         const QString &result_name);
  Q_INVOKABLE bool analyzeTrajectoryRmsdMatrix(
      const QString &selection, qulonglong frame_pair_budget,
      const QString &result_name);
  Q_INVOKABLE void cancelAnalysisTask();
  Q_INVOKABLE QString analysisResultJson(qulonglong result_id) const;
  Q_INVOKABLE bool setAnalysisResultVisible(qulonglong result_id,
                                            bool visible);
  Q_INVOKABLE bool deleteAnalysisResult(qulonglong result_id);
  Q_INVOKABLE bool exportAnalysisResult(qulonglong result_id,
                                        const QUrl &url,
                                        const QString &format);
  Q_INVOKABLE bool storeNamedView(const QString &name);
  Q_INVOKABLE bool recallNamedView(const QString &name);
  Q_INVOKABLE bool recallNamedViewAnimated(const QString &name,
                                           double duration_seconds,
                                           int hand);
  Q_INVOKABLE bool deleteNamedView(const QString &name);
  Q_INVOKABLE bool clearNamedViews();
  Q_INVOKABLE bool storeNamedScene(const QString &name);
  Q_INVOKABLE bool recallNamedScene(const QString &name);
  Q_INVOKABLE bool deleteNamedScene(const QString &name);
  Q_INVOKABLE bool clearNamedScenes();
  Q_INVOKABLE bool configureMovie(qulonglong frames, double fps, bool loop);
  Q_INVOKABLE bool setMovieKeyframe(qulonglong frame,
                                    const QString &scene_name,
                                    qlonglong trajectory_frame = -1);
  Q_INVOKABLE bool seekMovie(qulonglong frame);
  Q_INVOKABLE bool setMoviePlaying(bool playing);
  Q_INVOKABLE bool stepMovie(qulonglong steps = 1U);
  Q_INVOKABLE bool clearMovie();
  Q_INVOKABLE bool saveSession(const QUrl &url,
                               const QString &visible_panels = {});
  Q_INVOKABLE bool loadSession(const QUrl &url,
                               const QUrl &recovery = {});
  Q_INVOKABLE bool autosaveSession(const QUrl &primary,
                                   const QUrl &recovery,
                                   const QString &visible_panels = {});
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
  [[nodiscard]] const render::DirectVolumeData *directVolumeData() const noexcept {
    const auto *volume = workspace_->active_volume();
    return volume != nullptr && volume->direct_volume != nullptr
               ? volume->direct_volume.get()
               : nullptr;
  }
  [[nodiscard]] std::shared_ptr<const render::DirectVolumeData>
  directVolumeLease() const noexcept {
    const auto *volume = workspace_->active_volume();
    return volume != nullptr ? volume->direct_volume : nullptr;
  }
  [[nodiscard]] std::uint64_t directVolumePickId() const noexcept {
    return direct_volume_pick_id_;
  }
  [[nodiscard]] std::uint64_t packetRevision() const noexcept {
    return packet_revision_;
  }
  [[nodiscard]] bool packetIncremental() const noexcept {
    return packet_incremental_;
  }
  [[nodiscard]] const QString &packetUpdateMode() const noexcept {
    return packet_update_mode_;
  }
  [[nodiscard]] const QString &packetUpdateReason() const noexcept {
    return packet_update_reason_;
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
  void deliverDirectVolumeGpuStatus(
      const std::shared_ptr<const render::DirectVolumeData> &source,
      QString state, QString message);

signals:
  void angleChanged();
  void statusChanged();
  void representationChanged();
  void selectionChanged();
  void objectsChanged();
  void trajectoryChanged();
  void trajectoryTaskChanged();
  void analysisTaskChanged();
  void volumeChanged();
  void volumeTaskChanged();
  void volumeGpuStatusChanged();
  void scriptOutputChanged();
  void scriptRunningChanged();
  void scriptFinished(bool succeeded);
  void graphicsDiagnosticsChanged();
  void viewsChanged();
  void sessionChanged();
  void analysisResultsChanged();

private:
  [[nodiscard]] bool rebuildRepresentation();
  [[nodiscard]] bool rebuildScenePacket();
  [[nodiscard]] bool refreshWorkspacePresentation();
  void syncActiveRepresentationName();
  void syncTrajectoryState();
  void onPlaybackTick();
  void onTrajectoryTaskPoll();
  void onAnalysisTaskPoll();
  void onDirectVolumeTaskPoll();
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
  std::uint64_t direct_volume_pick_id_{};
  std::uint64_t packet_revision_{1U};
  bool packet_incremental_{};
  QString packet_update_mode_{QStringLiteral("full")};
  QString packet_update_reason_{QStringLiteral("initial packet")};
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
  std::uint64_t last_pick_id_{};
  std::uint64_t last_pick_completion_revision_{};
  QTimer playback_timer_;
  QElapsedTimer playback_elapsed_;
  bool has_trajectory_{};
  qulonglong trajectory_frame_{};
  qulonglong trajectory_frame_count_{};
  bool trajectory_playing_{};
  QString playback_mode_{QStringLiteral("once")};
  QString playback_direction_{QStringLiteral("forward")};
  double trajectory_fps_{30.0};
  std::shared_ptr<operation::TaskScheduler> trajectory_task_scheduler_;
  std::shared_ptr<std::atomic_uint64_t> trajectory_task_generation_;
  std::optional<application::ScheduledTrajectoryFrame>
      pending_trajectory_frame_;
  std::optional<application::ScheduledTrajectoryLoad>
      pending_trajectory_load_;
  QTimer trajectory_task_timer_;
  bool trajectory_task_running_{};
  double trajectory_task_progress_{};
  QString trajectory_task_stage_{QStringLiteral("idle")};
  std::shared_ptr<operation::TaskScheduler> analysis_task_scheduler_;
  std::shared_ptr<std::atomic_uint64_t> analysis_task_generation_;
  std::optional<application::ScheduledAnalysis> pending_analysis_;
  QTimer analysis_task_timer_;
  bool analysis_task_running_{};
  double analysis_task_progress_{};
  QString analysis_task_stage_{QStringLiteral("idle")};
  bool has_volume_{};
  double volume_level_{};
  double volume_minimum_{};
  double volume_maximum_{};
  QString volume_mode_{QStringLiteral("isosurface")};
  std::shared_ptr<operation::TaskScheduler> volume_task_scheduler_;
  std::shared_ptr<std::atomic_uint64_t> volume_task_generation_;
  std::optional<application::ScheduledDirectVolume> pending_direct_volume_;
  QTimer volume_task_timer_;
  bool volume_task_running_{};
  double volume_task_progress_{};
  QString volume_task_stage_{QStringLiteral("idle")};
  QString volume_gpu_state_{QStringLiteral("idle")};
  QString volume_gpu_message_{QStringLiteral("No direct volume is active")};
  QString volume_slice_axis_{QStringLiteral("z")};
  qulonglong volume_slice_index_{};
  qulonglong volume_slice_maximum_{};
  QString script_output_;
  QString session_visible_panels_;
  operation::CancellationToken script_cancellation_;
  std::thread script_worker_;
  bool script_running_{};
};

} // namespace molshredder::desktop
