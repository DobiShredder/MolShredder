#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/task_service.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/gui/action_adapter.hpp"
#include "molshredder/operation/task_context.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

bool wait_for_state(
    const std::shared_ptr<molshredder::operation::TaskScheduler> &scheduler,
    std::uint64_t task_id, molshredder::operation::TaskState expected) {
  for (std::size_t attempt = 0; attempt < 2000U; ++attempt) {
    const auto snapshot = scheduler->snapshot(task_id);
    if (snapshot.has_value() && snapshot.value().state == expected) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return false;
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  bool passed = true;
  passed &= expect(argc == 3,
                   "volume workspace test requires OpenDX and MRC fixtures");
  if (argc != 3)
    return 1;

  auto workspace = std::make_shared<application::Workspace>();
  auto registry = application::make_default_registry(workspace);
  const application::Dispatcher dispatcher{registry};
  const gui::ActionAdapter actions{dispatcher};
  operation::TaskContext context;
  const auto loaded =
      actions.trigger({"volume load",
                       {{"path", std::filesystem::path{argv[1]}.string()},
                        {"name", "electrostatic"},
                        {"file-format", "opendx"},
                        {"coordinate-unit", "nanometer"}}},
                      context);
  passed &= expect(loaded.succeeded() && workspace->volume_count() == 1U &&
                       workspace->object_count() == 0U &&
                       workspace->scene()->node_count() == 2U,
                   "shared action must load an OpenDX volume scene object");
  if (workspace->volume_count() == 1U) {
    const auto &volume = workspace->volumes().front();
    const auto *node = workspace->scene()->find(volume.scene_node);
    passed &=
        expect(volume.id == 1U && volume.name == "electrostatic" &&
                   volume.grid->shape() == model::VolumeShape{2U, 2U, 3U} &&
                   volume.grid->metadata().coordinate_unit ==
                       operation::LengthUnit::nanometer &&
                   node != nullptr && node->kind() == scene::NodeKind::volume &&
                   node->volume() == volume.grid &&
                   workspace->scene()->selection().contains(volume.scene_node),
               "volume load must preserve identity, unit and scene ownership");
  }

  const auto mrc_loaded =
      actions.trigger({"volume load",
                       {{"path", std::filesystem::path{argv[2]}.string()},
                        {"name", "density"},
                        {"file-format", "mrc"},
                        {"coordinate-unit", "angstrom"}}},
                      context);
  passed &=
      expect(mrc_loaded.succeeded() && workspace->volume_count() == 2U &&
                 workspace->scene()->node_count() == 3U,
             "shared action must add an MRC volume without replacing OpenDX");
  if (workspace->volume_count() == 2U) {
    const auto &volume = workspace->volumes().back();
    const auto *node = workspace->scene()->find(volume.scene_node);
    passed &= expect(
        volume.id == 2U && volume.name == "density" &&
            volume.grid->shape() == model::VolumeShape{2U, 2U, 3U} &&
            volume.grid->metadata().fields.at("format") == "mrc" &&
            node != nullptr && node->volume() == volume.grid &&
            workspace->scene()->selection().contains(volume.scene_node),
        "MRC volume must preserve shared identity and typed scene ownership");
  }

  const auto before_scene = workspace->scene();
  const auto duplicate = workspace->load_volume(
      argv[1], std::string{"electrostatic"}, io::VolumeFormat::opendx,
      operation::LengthUnit::angstrom);
  passed &= expect(!duplicate.has_value() && workspace->volume_count() == 2U &&
                       workspace->scene() == before_scene,
                   "duplicate volume load must be failure-atomic");

  const auto listed = actions.trigger({"volume list", {}}, context);
  passed &= expect(listed.succeeded(),
                   "volume list must share the GUI/CLI/Python registry");

  const auto formats =
      actions.trigger({"format list", {{"family", "volume"}}}, context);
  const auto *format_response =
      std::get_if<command::Response>(&formats.envelope.payload);
  const auto *format_count = format_response == nullptr
                                 ? nullptr
                                 : std::get_if<std::uint64_t>(
                                       &format_response->fields.at(
                                            "format_count")
                                            .data);
  passed &= expect(
      format_response != nullptr && format_count != nullptr &&
          *format_count == 2U && format_response->table.has_value() &&
          format_response->table->rows.size() == *format_count,
      "format capability count must be captured before moving its table");

  const auto isosurface = actions.trigger(
      {"volume isosurface",
       {{"level", "2.5"}, {"color", "orange"}, {"opacity", "0.5"}}},
      context);
  passed &= expect(isosurface.succeeded() &&
                       workspace->volumes().back().representations.size() == 1U &&
                       !workspace->volumes().back()
                            .representations.front()
                            .mesh_triangles.empty(),
                   "shared action must create an active-volume isosurface");
  const auto representation_count =
      workspace->volumes().back().representations.size();
  const auto invalid_isosurface = actions.trigger(
      {"volume isosurface", {{"level", "2.5"}, {"opacity", "1.5"}}},
      context);
  passed &= expect(!invalid_isosurface.succeeded() &&
                       workspace->volumes().back().representations.size() ==
                           representation_count,
                   "invalid isosurface actions must be failure-atomic");

  const auto slice = actions.trigger(
      {"volume slice",
       {{"axis", "z"},
        {"index", "1"},
        {"minimum-color", "blue"},
        {"maximum-color", "orange"},
        {"opacity", "0.7"},
        {"memory-budget-bytes", "1048576"},
        {"replace", "true"}}},
      context);
  passed &= expect(
      slice.succeeded() &&
          workspace->volumes().back().representations.size() == 1U &&
          workspace->volumes().back().representations.front().provenance.at(
              "algorithm") == "orthogonal-grid-slice" &&
          workspace->volumes().back().representations.front().pick_targets.size() ==
              1U,
      "shared action must create a bounded pickable active-volume slice");
  const auto before_failed_slice_count =
      workspace->volumes().back().representations.size();
  const auto before_failed_slice_triangles =
      workspace->volumes().back().representations.front().mesh_triangles;
  const auto invalid_slice = actions.trigger(
      {"volume slice",
       {{"axis", "z"},
        {"index", "1"},
        {"memory-budget-bytes", "1"}}},
      context);
  passed &= expect(
      !invalid_slice.succeeded() &&
          workspace->volumes().back().representations.size() ==
              before_failed_slice_count &&
          workspace->volumes().back().representations.front().mesh_triangles ==
              before_failed_slice_triangles,
      "resource-exhausted slice must preserve the previous representation");

  const auto ramp_set =
      actions.trigger({"volume ramp set", {{"preset", "fire"}}}, context);
  const auto ramp_get = actions.trigger({"volume ramp get", {}}, context);
  passed &= expect(
      ramp_set.succeeded() && ramp_get.succeeded() &&
          workspace->volumes().back().transfer_function.has_value() &&
          workspace->volumes().back().transfer_function_name == "fire" &&
          workspace->volumes().back().transfer_function->points().size() == 4U,
      "volume ramp set/get must persist a versioned builtin on the active volume");
  const auto custom = actions.trigger(
      {"volume ramp define",
       {{"name", "focus"}, {"points", "0,0,0,0,0;100,1,0.5,0,0.8"}}},
      context);
  const auto before_invalid_ramp =
      workspace->volumes().back().transfer_function->points().size();
  const auto invalid_ramp = actions.trigger(
      {"volume ramp define",
       {{"name", "broken"}, {"points", "0,0,0,0,0;0,1,1,1,1"}}},
      context);
  passed &= expect(
      custom.succeeded() && !invalid_ramp.succeeded() &&
          workspace->volumes().back().transfer_function_name == "focus" &&
          workspace->volumes().back().transfer_function->points().size() ==
              before_invalid_ramp,
      "custom ramp definition must validate before replacing active state");
  const auto rendered = actions.trigger(
      {"volume render",
       {{"mode", "post-classified"},
        {"sampling-step", "0.5"},
        {"maximum-steps", "128"},
        {"lookup-table-samples", "64"},
        {"texture-budget-bytes", "1048576"}}},
      context);
  passed &= expect(rendered.succeeded() &&
                       workspace->volumes().back().direct_volume != nullptr,
                   "volume render must prepare bounded direct-volume data");
  const auto required_texture_bytes =
      workspace->volumes().back().direct_volume->required_texture_bytes;
  const auto invalid_render = actions.trigger(
      {"volume render", {{"texture-budget-bytes", "1"}}}, context);
  passed &= expect(!invalid_render.succeeded() &&
                       workspace->volumes().back().direct_volume != nullptr &&
                       workspace->volumes()
                               .back()
                               .direct_volume->required_texture_bytes ==
                           required_texture_bytes,
                   "failed direct-volume preparation must preserve active state");
  const auto hidden = actions.trigger({"volume hide", {}}, context);
  passed &= expect(hidden.succeeded() &&
                       workspace->volumes().back().direct_volume == nullptr,
                   "volume hide must remove only the direct-volume state");

  auto scheduler = operation::TaskScheduler::create(
      {1U, 4U, 16U * 1024U * 1024U, 2U, 16U})
                       .value();
  std::atomic_uint64_t generation{1U};
  application::ScheduledDirectVolumeRequest scheduled_request;
  scheduled_request.style.sampling_step = 0.5;
  scheduled_request.style.maximum_steps = 128U;
  scheduled_request.style.lookup_table_samples = 64U;
  scheduled_request.style.texture_budget_bytes = 1024U * 1024U;
  scheduled_request.preset = render::TransferPreset::fire;
  scheduled_request.replace_existing = true;
  scheduled_request.generation = 1U;
  scheduled_request.generation_is_current = [&](std::uint64_t value) {
    return value == generation.load();
  };
  const auto scheduled = application::schedule_direct_volume(
      workspace, scheduler, scheduled_request);
  passed &= expect(
      scheduled.has_value() && scheduled.value().reserved_memory_bytes > 0U &&
          wait_for_state(scheduler, scheduled.value().task_id,
                         operation::TaskState::ready_to_commit) &&
          workspace->volumes().back().direct_volume == nullptr,
      "direct-volume worker must prepare without mutating owner state");
  if (scheduled.has_value()) {
    const auto commit = scheduler->commit_ready(scheduled.value().task_id);
    const auto completion = scheduled.value().completion->result();
    passed &= expect(
        !commit.has_value() && completion.has_value() &&
            workspace->volumes().back().direct_volume != nullptr &&
            scheduler->scheduler_snapshot().reserved_memory_bytes == 0U,
        "owner-thread commit must atomically publish the bounded direct volume");
  }

  static_cast<void>(workspace->hide_direct_volume());
  generation = 2U;
  scheduled_request.generation = 2U;
  const auto generation_stale = application::schedule_direct_volume(
      workspace, scheduler, scheduled_request);
  passed &= expect(
      generation_stale.has_value() &&
          wait_for_state(scheduler, generation_stale.value().task_id,
                         operation::TaskState::ready_to_commit),
      "generation-stale fixture must reach an uncommitted candidate");
  generation = 3U;
  if (generation_stale.has_value()) {
    const auto commit =
        scheduler->commit_ready(generation_stale.value().task_id);
    passed &= expect(
        !commit.has_value() &&
            wait_for_state(scheduler, generation_stale.value().task_id,
                           operation::TaskState::stale) &&
            workspace->volumes().back().direct_volume == nullptr,
        "superseded generation must discard prepared direct-volume state");
  }

  scheduled_request.generation = 3U;
  const auto revision_stale = application::schedule_direct_volume(
      workspace, scheduler, scheduled_request);
  passed &= expect(
      revision_stale.has_value() &&
          wait_for_state(scheduler, revision_stale.value().task_id,
                         operation::TaskState::ready_to_commit),
      "revision-stale fixture must reach an uncommitted candidate");
  const auto ramp_changed =
      workspace->set_volume_ramp(render::TransferPreset::ice);
  if (revision_stale.has_value()) {
    const auto commit = scheduler->commit_ready(revision_stale.value().task_id);
    const auto snapshot = scheduler->snapshot(revision_stale.value().task_id);
    passed &= expect(
        !commit.has_value() && ramp_changed.has_value() &&
            snapshot.has_value() &&
            snapshot.value().state == operation::TaskState::failed &&
            snapshot.value().error.has_value() &&
            snapshot.value().error->code ==
                operation::ErrorCode::stale_result &&
            workspace->volumes().back().direct_volume == nullptr &&
            workspace->volumes().back().transfer_function_name == "ice",
        "presentation revision change must reject commit and preserve newer state");
  }

  scheduled_request.memory_reservation_bytes = 1U;
  const auto undersized = application::schedule_direct_volume(
      workspace, scheduler, scheduled_request);
  passed &= expect(
      !undersized.has_value() &&
          undersized.error().code == operation::ErrorCode::resource_exhausted &&
          workspace->volumes().back().direct_volume == nullptr,
      "undersized scheduler reservation must fail before worker submission");
  return passed ? 0 : 1;
}
