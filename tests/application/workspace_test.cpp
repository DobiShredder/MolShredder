#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/cli/console.hpp"
#include "molshredder/gui/action_adapter.hpp"
#include "molshredder/gui/analysis_presenter.hpp"
#include "molshredder/operation/task_context.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

molshredder::application::DispatchOutcome trigger(
    const molshredder::gui::ActionAdapter& gui, std::string command,
    molshredder::command::Arguments arguments = {}) {
  molshredder::operation::TaskContext context;
  return gui.trigger({std::move(command), std::move(arguments)}, context);
}

}  // namespace

int main(int argc, char** argv) {
  using namespace molshredder;
  using scene::operator-;
  bool passed = true;
  if (argc != 12) {
    std::cerr <<
        "expected general, PBC, H-bond PDB, PQR, SDF, MOL2, GRO, G96, PSF, PRMTOP and RST7 fixture paths\n";
    return 1;
  }
  const std::filesystem::path fixture{argv[1]};
  const std::filesystem::path pbc_fixture{argv[2]};
  const std::filesystem::path hbond_fixture{argv[3]};
  const std::filesystem::path pqr_fixture{argv[4]};
  const std::filesystem::path sdf_fixture{argv[5]};
  const std::filesystem::path mol2_fixture{argv[6]};
  const std::filesystem::path gro_fixture{argv[7]};
  const std::filesystem::path g96_fixture{argv[8]};
  const std::filesystem::path psf_fixture{argv[9]};
  const std::filesystem::path prmtop_fixture{argv[10]};
  const std::filesystem::path rst7_fixture{argv[11]};

  {
    auto amber_workspace = std::make_shared<application::Workspace>();
    auto amber_registry = application::make_default_registry(amber_workspace);
    const application::Dispatcher amber_dispatcher{amber_registry};
    const gui::ActionAdapter amber_gui{amber_dispatcher};
    const auto loaded = trigger(
        amber_gui, "load",
        {{"path", prmtop_fixture.string()}, {"file-format", "prmtop"},
         {"name", "amber"}});
    const auto attached = trigger(
        amber_gui, "traj load",
        {{"path", rst7_fixture.string()}, {"file-format", "rst7"},
         {"cache-mib", "1"}, {"prefetch-frames", "0"}});
    const auto shown = trigger(
        amber_gui, "show",
        {{"representation", "sticks"}, {"selection", "all"}});
    const auto* object = amber_workspace->active_object();
    passed &= expect(
        loaded.succeeded() && attached.succeeded() && shown.succeeded() &&
            object != nullptr && object->system->coordinates()->frame_count() ==
                                     1U &&
            object->trajectory.has_value() &&
            object->trajectory->format == io::TrajectoryFormat::rst7 &&
            object->representations.size() == 1U,
        "shared GUI operation must attach RST7 coordinates to PRMTOP and render them");
  }

  {
    auto psf_workspace = std::make_shared<application::Workspace>();
    auto psf_registry = application::make_default_registry(psf_workspace);
    const application::Dispatcher psf_dispatcher{psf_registry};
    const gui::ActionAdapter psf_gui{psf_dispatcher};
    const auto loaded = trigger(
        psf_gui, "load",
        {{"path", psf_fixture.string()}, {"file-format", "psf"},
         {"name", "topology"}});
    const auto shown = trigger(
        psf_gui, "show",
        {{"representation", "lines"}, {"selection", "all"}});
    passed &= expect(
        loaded.succeeded() && psf_workspace->active_object() != nullptr &&
            psf_workspace->active_object()->system->coordinates()->frame_count() ==
                0U &&
            !shown.succeeded() &&
            std::get<operation::Error>(shown.envelope.payload).code ==
                operation::ErrorCode::not_found,
        "shared GUI action must load PSF topology without inventing coordinates");
  }

  {
    auto g96_workspace = std::make_shared<application::Workspace>();
    const auto loaded = g96_workspace->load_structure(
        g96_fixture, std::string{"gromos"}, io::StructureFormat::auto_detect);
    const auto current_extent = g96_workspace->selection_extent("all");
    const auto all_state_extent = g96_workspace->selection_extent(
        "all", {application::CameraStateScopeKind::all, 0U});
    const auto second_state_extent = g96_workspace->selection_extent(
        "all", {application::CameraStateScopeKind::explicit_state, 1U});
    operation::TaskContext clip_context;
    const auto all_state_clip = g96_workspace->clip_camera(
        application::CameraClipMode::atoms, 0.1, std::string{"all"},
        {application::CameraStateScopeKind::all, 0U}, &clip_context);
    operation::TaskContext orient_context;
    const auto all_state_orient = g96_workspace->orient_camera(
        "all", {application::CameraStateScopeKind::all, 0U},
        &orient_context);
    operation::TaskContext cancelled_orient_context;
    cancelled_orient_context.cancellation.request_cancel();
    const auto camera_before_failed_orient =
        g96_workspace->camera().parameters();
    const auto cancelled_orient = g96_workspace->orient_camera(
        "all", {application::CameraStateScopeKind::all, 0U},
        &cancelled_orient_context);
    const auto out_of_range_orient = g96_workspace->orient_camera(
        "all", {application::CameraStateScopeKind::explicit_state, 8U});
    operation::TaskContext cancelled_context;
    cancelled_context.cancellation.request_cancel();
    const auto camera_before_cancel = g96_workspace->camera().parameters();
    const auto cancelled_center = g96_workspace->center_camera(
        "all", true, {application::CameraStateScopeKind::all, 0U},
        &cancelled_context);
    passed &= expect(
        loaded.has_value() && loaded.value().objects.size() == 1U &&
            loaded.value().objects[0].object_name == "gromos" &&
            loaded.value().objects[0].frame_count == 2U &&
            g96_workspace->active_object() != nullptr &&
            g96_workspace->active_object()->system->coordinates()->
                    frame_count() == 2U && current_extent.has_value() &&
            all_state_extent.has_value() && second_state_extent.has_value() &&
            scene::length(current_extent.value().center -
                          model::Vec3d{0.125, 0.225, 0.325}) < 1.0e-12 &&
            scene::length(all_state_extent.value().center -
                          model::Vec3d{0.13, 0.23, 0.33}) < 1.0e-12 &&
            scene::length(second_state_extent.value().center -
                          model::Vec3d{0.135, 0.235, 0.335}) < 1.0e-12 &&
            all_state_extent.value().selected_atom_count == 2U &&
            all_state_extent.value().used_atom_count == 4U &&
            all_state_extent.value().evaluated_frame_count == 2U &&
            all_state_clip.has_value() &&
            all_state_clip.value().extent.has_value() &&
            all_state_clip.value().extent->spatial.evaluated_frame_count == 2U &&
            all_state_orient.has_value() &&
            all_state_orient.value().principal_axes.sample_count == 4U &&
            all_state_orient.value().extent.evaluated_frame_count == 2U &&
            all_state_orient.value().extent.used_atom_count == 4U &&
            std::abs(std::abs(scene::dot(
                         all_state_orient.value().principal_axes.axes[0],
                         scene::normalized(model::Vec3d{1.0, 1.0, 1.0}))) -
                     1.0) < 1.0e-10 &&
            all_state_orient.value().camera.parameters().target ==
                all_state_orient.value().oriented_center &&
            !cancelled_orient.has_value() &&
            cancelled_orient.error().code == operation::ErrorCode::cancelled &&
            !out_of_range_orient.has_value() &&
            g96_workspace->camera().parameters() ==
                camera_before_failed_orient &&
            !cancelled_center.has_value() &&
            cancelled_center.error().code == operation::ErrorCode::cancelled &&
            g96_workspace->camera().parameters() == camera_before_cancel,
        "multi-frame G96 must expose current/all/explicit camera extents and cancellable framing");
  }

  {
    auto gro_workspace = std::make_shared<application::Workspace>();
    const auto loaded = gro_workspace->load_structure(
        gro_fixture, std::string{"gromacs"}, io::StructureFormat::auto_detect);
    passed &= expect(
        loaded.has_value() && loaded.value().objects.size() == 1U &&
            loaded.value().objects[0].object_name == "gromacs" &&
            loaded.value().objects[0].frame_count == 2U &&
            gro_workspace->active_object() != nullptr &&
            gro_workspace->active_object()->system->coordinates()->
                    frame_count() == 2U,
        "multi-frame GRO must load through the shared Workspace path");
  }

  {
    auto mol2_workspace = std::make_shared<application::Workspace>();
    const auto loaded = mol2_workspace->load_structure(
        mol2_fixture, std::nullopt, io::StructureFormat::auto_detect);
    passed &= expect(
        loaded.has_value() && loaded.value().objects.size() == 2U &&
            loaded.value().objects[0].object_name == "Acetamide" &&
            loaded.value().objects[1].object_name == "Benzene" &&
            mol2_workspace->object_count() == 2U &&
            mol2_workspace->active_object() != nullptr &&
            mol2_workspace->active_object()->system->name() == "Benzene",
        "multi-molecule MOL2 must use the shared failure-atomic batch path");
  }

  {
    auto chemistry_workspace = std::make_shared<application::Workspace>();
    const auto loaded = chemistry_workspace->load_structure(
        sdf_fixture, std::nullopt, io::StructureFormat::auto_detect);
    passed &= expect(
        loaded.has_value() && loaded.value().objects.size() == 2U &&
            loaded.value().objects[0].object_name == "Charged aromatic" &&
            loaded.value().objects[0].source_record_index == 0U &&
            loaded.value().objects[1].object_name == "Nitrile" &&
            loaded.value().objects[1].source_record_index == 1U &&
            loaded.value().object_id == 2U &&
            chemistry_workspace->object_count() == 2U &&
            chemistry_workspace->active_object() != nullptr &&
            chemistry_workspace->active_object()->system->name() == "Nitrile" &&
            chemistry_workspace->scene()->node_count() == 3U,
        "multi-record SDF must create ordered objects and activate the last record");
    const auto previous_scene = chemistry_workspace->scene();
    const auto duplicate = chemistry_workspace->load_structure(
        sdf_fixture, std::nullopt, io::StructureFormat::sdf);
    passed &= expect(
        !duplicate.has_value() && chemistry_workspace->object_count() == 2U &&
            chemistry_workspace->scene() == previous_scene &&
            chemistry_workspace->active_object()->id == 2U,
        "failed multi-record import must preserve the complete Workspace");
    const auto empty_name = chemistry_workspace->load_structure(
        sdf_fixture, std::string{}, io::StructureFormat::sdf);
    passed &= expect(!empty_name.has_value() &&
                         chemistry_workspace->object_count() == 2U &&
                         chemistry_workspace->scene() == previous_scene,
                     "explicit empty batch name must fail without mutation");

    auto named_workspace = std::make_shared<application::Workspace>();
    const auto named = named_workspace->load_structure(
        sdf_fixture, std::string{"chemistry"}, io::StructureFormat::sdf);
    passed &= expect(
        named.has_value() && named.value().objects.size() == 2U &&
            named.value().objects[0].object_name == "chemistry_1" &&
            named.value().objects[1].object_name == "chemistry_2",
        "explicit multi-record name must expand to deterministic unique names");
  }

  {
    auto pqr_workspace = std::make_shared<application::Workspace>();
    auto pqr_registry = application::make_default_registry(pqr_workspace);
    const application::Dispatcher pqr_dispatcher{pqr_registry};
    const gui::ActionAdapter pqr_gui{pqr_dispatcher};
    const auto pqr_loaded = trigger(
        pqr_gui, "load", {{"path", pqr_fixture.string()},
                           {"name", "electrostatics"}});
    const auto pqr_shown = trigger(
        pqr_gui, "show", {{"representation", "spheres"},
                           {"selection", "all"}});
    const auto* pqr_object = pqr_workspace->active_object();
    passed &= expect(
        pqr_loaded.succeeded() && pqr_shown.succeeded() &&
            pqr_object != nullptr && pqr_object->representations.size() == 1U &&
            pqr_object->representations.front().packet.spheres.size() == 3U &&
            std::abs(pqr_object->representations.front()
                         .packet.spheres[0]
                         .radius -
                     1.55) < 1.0e-12 &&
            std::abs(pqr_object->representations.front()
                         .packet.spheres[2]
                         .radius -
                     1.8) < 1.0e-12,
        "PQR radius property must drive sphere representation radii");
  }

  auto workspace = std::make_shared<application::Workspace>();
  auto registry = application::make_default_registry(workspace);
  const application::Dispatcher dispatcher{registry};
  const gui::ActionAdapter gui{dispatcher};

  const auto no_object = trigger(
      gui, "show", {{"representation", "spheres"}, {"selection", "all"}});
  passed &= expect(!no_object.succeeded(),
                   "show before load must fail through shared state");

  const auto loaded = trigger(
      gui, "load", {{"path", fixture.string()}, {"name", "protein"}});
  passed &= expect(loaded.succeeded() && workspace->object_count() == 1U &&
                       workspace->active_object() != nullptr &&
                       workspace->active_object()->system->name() == "protein" &&
                       workspace->scene()->node_count() == 2U &&
                       workspace->scene()->selection().contains(
                           workspace->active_object()->scene_node),
                   "GUI load must create active object and scene node");

  const auto all_extent = workspace->selection_extent("all");
  const auto first_extent = workspace->selection_extent("index 1");
  passed &= expect(all_extent.has_value() && first_extent.has_value() &&
                       all_extent.value().selected_atom_count > 1U &&
                       all_extent.value().used_atom_count ==
                           all_extent.value().selected_atom_count &&
                       first_extent.value().used_atom_count == 1U,
                   "camera framing extent must use present active-frame coordinates");

  const auto all_state_center = trigger(
      gui, "view center",
      {{"selection", "all"}, {"state", "0"}, {"move-origin", "true"}});
  const auto *all_state_response =
      all_state_center.succeeded()
          ? std::get_if<command::Response>(&all_state_center.envelope.payload)
          : nullptr;
  const auto before_bad_state = workspace->camera().parameters();
  const auto bad_state = trigger(
      gui, "view zoom", {{"selection", "all"}, {"state", "banana"}});
  passed &= expect(
      all_state_response != nullptr &&
          std::get<std::string>(all_state_response->fields.at("state").data) ==
              "all" &&
          !bad_state.succeeded() &&
          workspace->camera().parameters() == before_bad_state,
      "camera commands must normalize PyMOL state aliases and reject invalid scopes atomically");

  const auto seeded_camera = trigger(
      gui, "view set",
      {{"model-origin-x", "8"}, {"model-origin-y", "9"},
       {"model-origin-z", "10"}, {"target-x", "-4"}});
  const auto centered = trigger(
      gui, "view center",
      {{"selection", "index 1"}, {"move-origin", "false"},
       {"duration", "0.25"}, {"hand", "-1"}});
  const auto *center_response =
      centered.succeeded()
          ? std::get_if<command::Response>(&centered.envelope.payload)
          : nullptr;
  const auto *center_animation =
      center_response == nullptr
          ? nullptr
          : std::get_if<command::Value::Object>(
                &center_response->fields.at("animation").data);
  passed &= expect(
      seeded_camera.succeeded() && center_animation != nullptr &&
          workspace->camera().parameters().target ==
              first_extent.value().center &&
          workspace->camera().parameters().model_origin ==
              model::Vec3d{8.0, 9.0, 10.0} &&
          std::get<bool>(center_animation->at("active").data) &&
          std::get<std::int64_t>(center_animation->at("hand").data) == -1,
      "view center must preserve the pivot on request and expose animation metadata");

  const auto origin =
      trigger(gui, "view origin", {{"selection", "all"}});
  const auto zoomed = trigger(
      gui, "view zoom",
      {{"selection", "all"}, {"buffer", "1.0"},
       {"complete", "true"}, {"duration", "0.2"}});
  passed &= expect(
      origin.succeeded() && zoomed.succeeded() &&
          workspace->camera().parameters().target == all_extent.value().center &&
          workspace->camera().parameters().model_origin ==
              all_extent.value().center &&
          workspace->camera().parameters().near_clip <
              workspace->camera().parameters().distance &&
          workspace->camera().parameters().far_clip >
              workspace->camera().parameters().distance,
      "view origin/zoom must share selection semantics and frame valid clips");

  const auto target_before_explicit_origin =
      workspace->camera().parameters().target;
  const auto explicit_origin = trigger(
      gui, "view origin", {{"position", "1.5,-2,3.25"}});
  const auto camera_before_object_origin = workspace->camera().parameters();
  const auto object_origin = trigger(
      gui, "view origin",
      {{"object", "protein"}, {"selection", "index 1"}});
  const auto *object_node =
      workspace->scene()->find(workspace->active_object()->scene_node);
  const auto object_transform = object_node == nullptr
                                    ? scene::Transform{}
                                    : object_node->local_transform();
  const auto camera_after_object_origin = workspace->camera().parameters();
  const auto object_reset =
      trigger(gui, "view reset", {{"object", "current"}});
  const auto *reset_object_node =
      workspace->scene()->find(workspace->active_object()->scene_node);
  const auto camera_after_object_reset = workspace->camera().parameters();
  const auto camera_before_invalid_origin = workspace->camera().parameters();
  const auto invalid_explicit_origin = workspace->set_camera_origin(
      model::Vec3d{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0});
  passed &= expect(
      explicit_origin.succeeded() && object_origin.succeeded() &&
          object_node != nullptr && object_reset.succeeded() &&
          reset_object_node != nullptr &&
          workspace->camera().parameters().target ==
              target_before_explicit_origin &&
          camera_before_object_origin.model_origin ==
              model::Vec3d{1.5, -2.0, 3.25} &&
          camera_after_object_origin == camera_before_object_origin &&
          object_transform.pivot == first_extent.value().center &&
          reset_object_node->local_transform() == scene::Transform{} &&
          camera_after_object_reset == camera_before_object_origin &&
          !invalid_explicit_origin.has_value() &&
          workspace->camera().parameters() == camera_before_invalid_origin,
      "explicit camera origins and per-object selection pivots/reset must be failure-atomic and camera independent");

  const auto before_empty_camera = workspace->camera().parameters();
  const auto empty_zoom =
      trigger(gui, "view zoom", {{"selection", "none"}});
  passed &= expect(!empty_zoom.succeeded() &&
                       workspace->camera().parameters() == before_empty_camera,
                   "empty camera selections must fail without changing state");

  const auto rotated = trigger(
      gui, "view set",
      {{"orientation-w", "0.9238795325"},
       {"orientation-y", "0.3826834324"}});
  const auto reset = trigger(
      gui, "view reset", {{"duration", "0.3"}, {"hand", "1"}});
  const auto *reset_response =
      reset.succeeded()
          ? std::get_if<command::Response>(&reset.envelope.payload)
          : nullptr;
  passed &= expect(
      rotated.succeeded() && reset_response != nullptr &&
          workspace->camera().parameters().orientation == scene::Quaterniond{} &&
          workspace->camera().parameters().target == all_extent.value().center &&
          std::get<std::uint64_t>(
              reset_response->fields.at("molecular_object_count").data) == 1U,
      "view reset must restore identity orientation and cover visible scene data");

  const auto seeded_clip = trigger(
      gui, "view set", {{"near-clip", "10"}, {"far-clip", "30"}});
  const auto clip_near =
      trigger(gui, "view clip", {{"mode", "near"}, {"distance", "2"}});
  const auto clip_far =
      trigger(gui, "view clip", {{"mode", "far"}, {"distance", "3"}});
  const auto clip_move = trigger(
      gui, "view clip", {{"mode", "move"}, {"distance", "-2"}});
  const auto clip_slab =
      trigger(gui, "view clip", {{"mode", "slab"}, {"distance", "8"}});
  passed &= expect(
      seeded_clip.succeeded() && clip_near.succeeded() &&
          clip_far.succeeded() && clip_move.succeeded() &&
          clip_slab.succeeded() &&
          workspace->camera().parameters().near_clip == 15.5 &&
          workspace->camera().parameters().far_clip == 23.5,
      "relative near/far/move and centered slab clip modes must match their signed semantics");

  const auto first_depth =
      workspace->selection_camera_depth_extent("index 1");
  const auto selection_slab = trigger(
      gui, "view clip",
      {{"mode", "slab"}, {"distance", "6"},
       {"selection", "index 1"}});
  const auto selected_center_depth =
      first_depth.has_value()
          ? scene::dot(first_depth.value().spatial.center -
                           workspace->camera().position(),
                       workspace->camera().forward())
          : 0.0;
  passed &= expect(
      first_depth.has_value() && selection_slab.succeeded() &&
          std::abs(workspace->camera().parameters().near_clip -
                   (selected_center_depth - 3.0)) < 1.0e-12 &&
          std::abs(workspace->camera().parameters().far_clip -
                   (selected_center_depth + 3.0)) < 1.0e-12,
      "selection slab must center clipping depth on the projected AABB midpoint");

  const auto all_depth = workspace->selection_camera_depth_extent("all");
  const auto clip_atoms = trigger(
      gui, "view clip",
      {{"mode", "atoms"}, {"distance", "1"}, {"selection", "all"}});
  passed &= expect(
      all_depth.has_value() && clip_atoms.succeeded() &&
          std::abs(workspace->camera().parameters().near_clip -
                   (all_depth.value().minimum_depth - 1.0)) < 1.0e-12 &&
          std::abs(workspace->camera().parameters().far_clip -
                   (all_depth.value().maximum_depth + 1.0)) < 1.0e-12,
      "atoms clip must fit the camera-projected selection depth with buffer");

  const auto absolute_near = workspace->camera().parameters().near_clip + 0.25;
  const auto absolute_far = workspace->camera().parameters().far_clip + 0.75;
  const auto near_set = trigger(
      gui, "view clip",
      {{"mode", "near-set"}, {"distance", std::to_string(absolute_near)}});
  const auto far_set = trigger(
      gui, "view clip",
      {{"mode", "far-set"}, {"distance", std::to_string(absolute_far)}});
  const auto queried_clip = trigger(gui, "view get-clip");
  const auto *clip_response =
      queried_clip.succeeded()
          ? std::get_if<command::Response>(&queried_clip.envelope.payload)
          : nullptr;
  passed &= expect(
      near_set.succeeded() && far_set.succeeded() && clip_response != nullptr &&
          std::abs(workspace->camera().parameters().near_clip -
                   absolute_near) < 1.0e-6 &&
          std::abs(workspace->camera().parameters().far_clip - absolute_far) <
              1.0e-6,
      "absolute clip setters and typed clip query must share Workspace state");

  const auto before_bad_clip = workspace->camera().parameters();
  const auto bad_clip = trigger(
      gui, "view clip",
      {{"mode", "far-set"},
       {"distance", std::to_string(before_bad_clip.near_clip - 1.0)}});
  const auto bad_slab =
      trigger(gui, "view clip", {{"mode", "slab"}, {"distance", "0"}});
  passed &= expect(!bad_clip.succeeded() && !bad_slab.succeeded() &&
                       workspace->camera().parameters() == before_bad_clip,
                   "invalid clip ranges and slab thickness must be failure-atomic");

  const auto navigation_seed = trigger(
      gui, "view set",
      {{"target-x", "2"}, {"target-y", "0"}, {"target-z", "0"},
       {"model-origin-x", "0"}, {"model-origin-y", "0"},
       {"model-origin-z", "0"}, {"distance", "10"},
       {"near-clip", "2"}, {"far-clip", "20"}});
  const auto moved_x = trigger(
      gui, "view move", {{"axis", "x"}, {"distance", "3"}});
  const auto moved_z = trigger(
      gui, "view move", {{"axis", "z"}, {"distance", "1"}});
  const auto turned_z = trigger(
      gui, "view turn", {{"axis", "z"}, {"angle", "90"}});
  const auto *turn_response =
      turned_z.succeeded()
          ? std::get_if<command::Response>(&turned_z.envelope.payload)
          : nullptr;
  passed &= expect(
      navigation_seed.succeeded() && moved_x.succeeded() &&
          moved_z.succeeded() && turn_response != nullptr &&
          std::abs(workspace->camera().parameters().target.x) < 1.0e-12 &&
          std::abs(workspace->camera().parameters().target.y + 1.0) <
              1.0e-12 &&
          workspace->camera().parameters().model_origin == model::Vec3d{} &&
          workspace->camera().parameters().distance == 9.0 &&
          workspace->camera().parameters().near_clip == 1.0 &&
          workspace->camera().parameters().far_clip == 19.0 &&
          std::get<std::string>(turn_response->fields.at("axis").data) == "z",
      "shared move/turn commands must preserve pivot and rotate around it");

  const auto before_bad_move = workspace->camera().parameters();
  const auto bad_move = trigger(
      gui, "view move", {{"axis", "z"}, {"distance", "2"}});
  passed &= expect(!bad_move.succeeded() &&
                       workspace->camera().parameters() == before_bad_move,
                   "z movement that crosses the near plane must be failure-atomic");

  const auto initial_objects = trigger(gui, "object list");
  const auto* initial_object_response = initial_objects.succeeded()
                                            ? std::get_if<command::Response>(
                                                  &initial_objects.envelope.payload)
                                            : nullptr;
  passed &= expect(initial_object_response != nullptr &&
                       initial_object_response->table.has_value() &&
                       initial_object_response->table->rows.size() == 1U,
                   "object list must expose active Workspace state");
  passed &= expect(loaded.envelope.canonical_command.find(
                       "--file-format \"auto\"") != std::string::npos,
                   "load must normalize the explicit format default");

  const auto selected = trigger(
      gui, "select", {{"name", "chain_a"},
                       {"expression", "chain A"},
                       {"update", "true"}});
  passed &= expect(selected.succeeded(),
                   "GUI select must create a dynamic named selection");
  const auto shown = trigger(gui, "show",
                             {{"representation", "spheres"},
                              {"selection", "@chain_a"}});
  passed &= expect(shown.succeeded() &&
                       workspace->active_object()->representations.size() == 1U &&
                       workspace->active_object()
                               ->representations[0]
                               .packet.spheres.size() == 2U &&
                       workspace->active_object()
                               ->representations[0]
                               .packet.pick_targets.size() == 2U,
                   "GUI show must evaluate selection and store render packet");
  const auto failed_replace =
      trigger(gui, "show", {{"representation", "sticks"},
                             {"selection", "unknown X"},
                             {"replace", "true"}});
  passed &= expect(!failed_replace.succeeded() &&
                       workspace->active_object()->representations.size() == 1U &&
                       workspace->active_object()
                               ->representations.front()
                               .packet.spheres.size() == 2U,
                   "failed replacement must preserve the existing packet");
  const auto replaced =
      trigger(gui, "show", {{"representation", "sticks"},
                             {"selection", "@chain_a"},
                             {"replace", "true"}});
  passed &= expect(replaced.succeeded() &&
                       workspace->active_object()->representations.size() == 1U &&
                       workspace->active_object()
                               ->representations.front()
                               .kind == render::RepresentationKind::sticks,
                   "replace show must atomically retain only the new packet");

  const auto center = trigger(
      gui, "analyze center",
      {{"selection", "@chain_a"}, {"mode", "centroid"},
       {"precision", "3"}, {"unit", "angstrom"}});
  const auto distance = trigger(
      gui, "measure distance",
      {{"from", "index 1"}, {"to", "index 2"}, {"mode", "atom"},
       {"pbc", "raw"}, {"precision", "6"}, {"unit", "angstrom"}});
  passed &= expect(center.succeeded() && distance.succeeded() &&
                       workspace->measurements().size() == 1U &&
                       workspace->measurements()[0].measurement_id == 1U,
                   "analysis must be pure while measurement persists once");
  const auto secondary=trigger(gui,"analyze secondary-structure",
      {{"selection","all"},{"energy-cutoff","-0.5"},
       {"helix-propensity","0.05"},{"beta-propensity","0.02"},
       {"precision","3"}});
  const auto* secondary_response=secondary.succeeded()
      ? std::get_if<command::Response>(&secondary.envelope.payload) : nullptr;
  passed &= expect(secondary_response!=nullptr && secondary_response->table.has_value() &&
      std::get<std::string>(secondary_response->fields.at("method").data)==
          "molshredder-stride-method-v0" &&
      !std::get<bool>(secondary_response->fields.at("exact_stride_parity").data),
      "secondary-structure command must expose independent-method provenance");
  const auto secondary_presentation=gui::make_analysis_presentation(secondary);
  passed &= expect(secondary_presentation.has_value() &&
      secondary_presentation.value().title=="Secondary-structure assignment",
      "GUI presenter must expose secondary-structure table");
  const auto minimum_image = trigger(
      gui, "measure distance",
      {{"from", "index 1"}, {"to", "index 2"}, {"mode", "atom"},
       {"pbc", "minimum-image"}, {"precision", "6"},
       {"unit", "angstrom"}});
  passed &= expect(
      minimum_image.succeeded() && workspace->measurements().size() == 2U &&
          workspace->measurements()[1].boundary ==
              analysis::DistanceBoundary::minimum_image,
      "minimum-image PBC must execute and persist its boundary mode");

  const auto duplicate = trigger(
      gui, "load", {{"path", fixture.string()}, {"name", "protein"}});
  passed &= expect(!duplicate.succeeded() && workspace->object_count() == 1U,
                   "duplicate load failure must leave workspace unchanged");
  const auto second = trigger(
      gui, "load", {{"path", fixture.string()}, {"name", "ligand"}});
  passed &= expect(second.succeeded() && workspace->object_count() == 2U &&
                       workspace->active_object()->id == 2U,
                   "second load must append and activate a distinct object");
  const auto hidden = trigger(
      gui, "object visibility", {{"id", "1"}, {"visible", "false"}});
  passed &= expect(hidden.succeeded() &&
                       !workspace->scene()->effectively_visible(
                           workspace->objects()[0].scene_node),
                   "object visibility must commit through immutable scene");
  const auto activated = trigger(gui, "object activate", {{"id", "1"}});
  passed &= expect(activated.succeeded() &&
                       workspace->active_object()->id == 1U &&
                       workspace->scene()->selection().contains(
                           workspace->active_object()->scene_node),
                   "object activation must update Workspace and scene selection");
  const auto previous_scene = workspace->scene();
  const auto missing_object =
      trigger(gui, "object activate", {{"id", "999"}});
  passed &= expect(!missing_object.succeeded() &&
                       workspace->active_object()->id == 1U &&
                       workspace->scene() == previous_scene,
                   "failed activation must preserve active object and scene");
  passed &= expect(trigger(gui, "object visibility",
                           {{"id", "1"}, {"visible", "true"}})
                       .succeeded(),
                   "hidden object must be showable without losing state");
  const auto bad_selection = trigger(
      gui, "select", {{"name", "bad"}, {"expression", "unknown X"}});
  passed &= expect(!bad_selection.succeeded(),
                   "invalid selection must fail through GUI action");

  auto console_workspace = std::make_shared<application::Workspace>();
  auto console_registry =
      application::make_default_registry(console_workspace);
  cli::Console console{console_registry};
  std::istringstream input{
      "format json\n"
      "invoke \"load\" --file-format \"pdb\" --name \"cli_object\" --path \"" +
      fixture.string() +
      "\"\n"
      "invoke \"select\" --expression \"chain A\" --name \"chain_a\" "
      "--update \"false\"\n"
      "invoke \"show\" --representation \"lines\" --selection "
      "\"@chain_a\"\n"
      "history\n"
      "exit\n"};
  std::ostringstream output;
  std::ostringstream error;
  passed &= expect(console.run(input, output, error) == 0 && error.str().empty() &&
                       console_workspace->object_count() == 1U &&
                       console_workspace->active_object()
                               ->representations.size() == 1U &&
                       console.history().size() == 3U &&
                       output.str().find("\"primitive_count\":2") !=
                           std::string::npos,
                   "native console must preserve load/select/show state");

  auto pbc_workspace = std::make_shared<application::Workspace>();
  auto pbc_registry = application::make_default_registry(pbc_workspace);
  const application::Dispatcher pbc_dispatcher{pbc_registry};
  const gui::ActionAdapter pbc_gui{pbc_dispatcher};
  passed &= expect(
      trigger(pbc_gui, "load",
              {{"path", pbc_fixture.string()}, {"name", "pbc_fixture"}})
          .succeeded(),
      "PBC distance fixture must load");
  const auto pbc_raw = trigger(
      pbc_gui, "measure distance",
      {{"from", "index 1"}, {"to", "index 2"}, {"mode", "atom"},
       {"pbc", "raw"}, {"precision", "6"}, {"unit", "angstrom"}});
  const auto pbc_minimum = trigger(
      pbc_gui, "measure distance",
      {{"from", "index 1"}, {"to", "index 2"}, {"mode", "atom"},
       {"pbc", "minimum-image"}, {"precision", "6"},
       {"unit", "angstrom"}});
  const auto pbc_contacts = trigger(
      pbc_gui, "analyze contacts",
      {{"first", "all"}, {"cutoff", "3.0"},
       {"pbc", "minimum-image"}, {"exclude-bonded", "false"},
       {"precision", "3"}, {"unit", "angstrom"}});
  const auto* contact_response = pbc_contacts.succeeded()
      ? &std::get<command::Response>(pbc_contacts.envelope.payload) : nullptr;
  passed &= expect(
      pbc_raw.succeeded() && pbc_minimum.succeeded() &&
          contact_response != nullptr && contact_response->table.has_value() &&
          contact_response->table->rows.size() == 1U &&
          pbc_workspace->measurements().size() == 2U &&
          pbc_workspace->measurements()[0].distance.distance == 8.0 &&
          pbc_workspace->measurements()[0].distance.displacement.x == -8.0 &&
          pbc_workspace->measurements()[1].distance.distance == 2.0 &&
          pbc_workspace->measurements()[1].distance.displacement.x == 2.0 &&
          pbc_workspace->measurements()[1].boundary ==
              analysis::DistanceBoundary::minimum_image,
      "shared command path must distinguish raw and minimum-image distance");

  auto hbond_workspace=std::make_shared<application::Workspace>();
  auto hbond_registry=application::make_default_registry(hbond_workspace);
  const application::Dispatcher hbond_dispatcher{hbond_registry};
  const gui::ActionAdapter hbond_gui{hbond_dispatcher};
  passed &= expect(trigger(hbond_gui,"load",{{"path",hbond_fixture.string()},
      {"name","hbond_fixture"}}).succeeded(),"H-bond fixture must load");
  const auto hbonds=trigger(hbond_gui,"analyze hbonds",
      {{"donors","chain A"},{"acceptors","chain B"},{"cutoff","3.5"},
       {"angle","30"},{"pbc","raw"},{"precision","3"},{"unit","angstrom"}});
  const auto* hbond_response=hbonds.succeeded()
      ? &std::get<command::Response>(hbonds.envelope.payload) : nullptr;
  passed &= expect(hbond_response!=nullptr && hbond_response->table.has_value() &&
      hbond_response->table->rows.size()==1U &&
      std::get<std::string>(hbond_response->fields.at("donor_typing_source").data)=="element-bond-v1" &&
      std::get<bool>(hbond_response->fields.at("typing_estimated").data),
      "GUI H-bond command must expose geometry table and typing provenance");

  return passed ? 0 : 1;
}
