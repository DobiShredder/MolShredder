#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/application/task_service.hpp"
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

bool wait_for_state(
    const std::shared_ptr<molshredder::operation::TaskScheduler> &scheduler,
    std::uint64_t task_id, molshredder::operation::TaskState expected) {
  for (std::size_t attempt = 0; attempt < 2000U; ++attempt) {
    const auto snapshot = scheduler->snapshot(task_id);
    if (snapshot.has_value() && snapshot.value().state == expected)
      return true;
    if (snapshot.has_value() &&
        (snapshot.value().state == molshredder::operation::TaskState::failed ||
         snapshot.value().state ==
             molshredder::operation::TaskState::cancelled ||
         snapshot.value().state == molshredder::operation::TaskState::stale))
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return false;
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
    auto batch_workspace = std::make_shared<application::Workspace>();
    double progress{};
    operation::TaskContext batch_context{
        {}, [&](const auto& update) { progress = update.fraction; }};
    const std::vector<application::StructureLoadRequest> batch_requests{
        {fixture, std::string{"batch-pdb"}, io::StructureFormat::pdb},
        {pqr_fixture, std::string{"batch-pqr"}, io::StructureFormat::pqr}};
    const auto loaded =
        batch_workspace->load_structure_batch(batch_requests, batch_context);
    passed &= expect(
        loaded.has_value() && loaded.value().input_count == 2U &&
            loaded.value().objects.size() == 2U &&
            loaded.value().objects[0].object_name == "batch-pdb" &&
            loaded.value().objects[1].object_name == "batch-pqr" &&
            loaded.value().formats ==
                std::vector<io::StructureFormat>{io::StructureFormat::pdb,
                                                 io::StructureFormat::pqr} &&
            batch_workspace->object_count() == 2U &&
            batch_workspace->active_object()->id == 2U && progress == 1.0,
        "multi-input structure batch must parse, build and commit once");

    const auto scene_before_failure = batch_workspace->scene();
    const auto active_before_failure = batch_workspace->active_object()->id;
    const std::vector<application::StructureLoadRequest> invalid_requests{
        {fixture, std::string{"would-be-added"}, io::StructureFormat::pdb},
        {fixture.parent_path() / "missing-batch-input.pdb",
         std::string{"missing"}, io::StructureFormat::pdb}};
    operation::TaskContext failure_context;
    const auto failed = batch_workspace->load_structure_batch(
        invalid_requests, failure_context);
    passed &= expect(
        !failed.has_value() && failed.error().details.at("input_index") == "1" &&
            batch_workspace->object_count() == 2U &&
            batch_workspace->active_object()->id == active_before_failure &&
            batch_workspace->scene() == scene_before_failure,
        "middle parse failure must preserve objects, active state and scene exactly");

    operation::TaskContext cancelled_context;
    cancelled_context.cancellation.request_cancel();
    const auto cancelled = batch_workspace->load_structure_batch(
        batch_requests, cancelled_context);
    passed &= expect(!cancelled.has_value() &&
                         cancelled.error().code ==
                             operation::ErrorCode::cancelled &&
                         batch_workspace->object_count() == 2U &&
                         batch_workspace->scene() == scene_before_failure,
                     "cancelled structure batch must not publish partial state");
  }

  {
    auto async_workspace = std::make_shared<application::Workspace>();
    auto scheduler = operation::TaskScheduler::create(
                         {1U, 4U, 1024U * 1024U, 2U})
                         .value();
    std::atomic_uint64_t generation{1U};
    std::atomic<double> progress{};
    application::ScheduledStructureBatchRequest scheduled;
    scheduled.inputs = {
        {fixture, std::string{"async-one"}, io::StructureFormat::pdb},
        {pqr_fixture, std::string{"async-two"}, io::StructureFormat::pqr}};
    scheduled.memory_reservation_bytes = 4096U;
    scheduled.generation = 1U;
    scheduled.generation_is_current = [&](std::uint64_t value) {
      return value == generation.load();
    };
    scheduled.report_progress = [&](const auto& update) {
      progress = update.fraction;
    };
    const auto task = application::schedule_structure_batch(
        async_workspace, scheduler, std::move(scheduled));
    bool ready{};
    for (std::size_t attempt = 0; attempt < 500U; ++attempt) {
      const auto snapshot = scheduler->snapshot(task.value());
      if (snapshot.has_value() &&
          snapshot.value().state == operation::TaskState::ready_to_commit) {
        ready = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    passed &= expect(ready && async_workspace->object_count() == 0U &&
                         progress.load() >= 0.8,
                     "bounded worker must parse without mutating Workspace");
    const auto commit_error = scheduler->commit_ready(task.value());
    const auto completion = scheduler->wait(
        task.value(), std::chrono::seconds{2});
    passed &= expect(!commit_error.has_value() && completion.has_value() &&
                         completion.value().state ==
                             operation::TaskState::succeeded &&
                         async_workspace->object_count() == 2U &&
                         progress.load() == 1.0,
                     "owner-thread commit must publish the complete batch");

    application::ScheduledStructureBatchRequest stale_request;
    stale_request.inputs = {
        {fixture, std::string{"stale-one"}, io::StructureFormat::pdb}};
    stale_request.memory_reservation_bytes = 1024U;
    stale_request.generation = 1U;
    stale_request.generation_is_current = [&](std::uint64_t value) {
      return value == generation.load();
    };
    const auto stale = application::schedule_structure_batch(
        async_workspace, scheduler, std::move(stale_request));
    bool stale_ready{};
    for (std::size_t attempt = 0; attempt < 500U; ++attempt) {
      const auto snapshot = scheduler->snapshot(stale.value());
      if (snapshot.has_value() &&
          snapshot.value().state == operation::TaskState::ready_to_commit) {
        stale_ready = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    generation = 2U;
    const auto stale_commit = scheduler->commit_ready(stale.value());
    const auto stale_completion = scheduler->wait(
        stale.value(), std::chrono::seconds{2});
    passed &= expect(stale_ready && !stale_commit.has_value() &&
                         stale_completion.has_value() &&
                         stale_completion.value().state ==
                             operation::TaskState::stale &&
                         async_workspace->object_count() == 2U,
                     "generation change must discard parsed batch before commit");
  }

  {
    auto analysis_workspace = std::make_shared<application::Workspace>();
    const auto loaded = analysis_workspace->load_structure(
        fixture, std::string{"scheduled-analysis"}, io::StructureFormat::pdb);
    auto scheduler = operation::TaskScheduler::create(
                         {1U, 4U, 16U * 1024U * 1024U, 2U, 16U})
                         .value();
    std::atomic_uint64_t generation{1U};
    application::ScheduledSasaRequest sasa;
    sasa.arguments = {{"selection", "all"},
                      {"probe-radius", "1.4"},
                      {"samples", "32"},
                      {"evaluation-budget", "1000000"},
                      {"unit", "square-angstrom"},
                      {"precision", "6"}};
    sasa.selection_expression = "all";
    sasa.probe_radius_angstrom = 1.4;
    sasa.samples_per_atom = 32U;
    sasa.evaluation_budget = 1000000U;
    sasa.generation = 1U;
    sasa.generation_is_current = [&](std::uint64_t value) {
      return value == generation.load();
    };
    const auto scheduled_sasa = application::schedule_sasa_analysis(
        analysis_workspace, scheduler, std::move(sasa));
    passed &= expect(
        loaded.has_value() && scheduled_sasa.has_value() &&
            wait_for_state(scheduler, scheduled_sasa.value().task_id,
                           operation::TaskState::ready_to_commit) &&
            analysis_workspace->analysis_results().empty(),
        "analysis worker must build SASA without mutating the result store");
    if (scheduled_sasa.has_value()) {
      const auto committed =
          scheduler->commit_ready(scheduled_sasa.value().task_id);
      const auto completed = scheduled_sasa.value().completion->result();
      passed &= expect(!committed.has_value() && completed.has_value() &&
                           analysis_workspace->analysis_results().size() == 1U &&
                           analysis_workspace->analysis_results().back().kind ==
                               application::AnalysisResultKind::sasa,
                       "owner-thread analysis commit must publish canonical SASA once");
    }

    application::ScheduledRdfRequest rdf;
    rdf.arguments = {{"first", "all"},
                     {"maximum-radius", "10.0"},
                     {"bin-width", "0.5"},
                     {"normalization", "count"},
                     {"pbc", "raw"},
                     {"evaluation-budget", "1000000"},
                     {"precision", "6"},
                     {"unit", "angstrom"}};
    rdf.first_expression = "all";
    rdf.second_expression = "all";
    rdf.maximum_radius = 10.0;
    rdf.bin_width = 0.5;
    rdf.same_selection = true;
    rdf.evaluation_budget = 1000000U;
    rdf.generation = 1U;
    rdf.generation_is_current = [&](std::uint64_t value) {
      return value == generation.load();
    };
    const auto stale = application::schedule_rdf_analysis(
        analysis_workspace, scheduler, std::move(rdf));
    passed &= expect(
        stale.has_value() &&
            wait_for_state(scheduler, stale.value().task_id,
                           operation::TaskState::ready_to_commit),
        "scheduled RDF must produce an owner-thread candidate");
    generation = 2U;
    if (stale.has_value()) {
      const auto discarded = scheduler->commit_ready(stale.value().task_id);
      const auto state = scheduler->snapshot(stale.value().task_id);
      passed &= expect(!discarded.has_value() && state.has_value() &&
                           state.value().state == operation::TaskState::stale &&
                           analysis_workspace->analysis_results().size() == 1U,
                       "superseded analysis generation must not publish a result");
    }

    application::ScheduledRdfRequest revision_rdf;
    revision_rdf.arguments = {{"first", "all"},
                              {"maximum-radius", "10.0"},
                              {"bin-width", "0.5"},
                              {"normalization", "count"},
                              {"pbc", "raw"},
                              {"evaluation-budget", "1000000"},
                              {"precision", "6"},
                              {"unit", "angstrom"}};
    revision_rdf.first_expression = "all";
    revision_rdf.second_expression = "all";
    revision_rdf.maximum_radius = 10.0;
    revision_rdf.bin_width = 0.5;
    revision_rdf.same_selection = true;
    revision_rdf.evaluation_budget = 1000000U;
    revision_rdf.generation = 2U;
    revision_rdf.generation_is_current = [&](std::uint64_t value) {
      return value == generation.load();
    };
    const auto revision_stale = application::schedule_rdf_analysis(
        analysis_workspace, scheduler, std::move(revision_rdf));
    passed &= expect(
        revision_stale.has_value() &&
            wait_for_state(scheduler, revision_stale.value().task_id,
                           operation::TaskState::ready_to_commit),
        "revision-stale RDF fixture must prepare a candidate");
    const auto newer_object = analysis_workspace->load_structure(
        fixture, std::string{"newer-active"}, io::StructureFormat::pdb);
    if (revision_stale.has_value()) {
      const auto rejected_commit =
          scheduler->commit_ready(revision_stale.value().task_id);
      const auto state = scheduler->snapshot(revision_stale.value().task_id);
      passed &= expect(
          !rejected_commit.has_value() && newer_object.has_value() &&
              state.has_value() &&
              state.value().state == operation::TaskState::failed &&
              state.value().error.has_value() &&
              state.value().error->code == operation::ErrorCode::stale_result &&
              analysis_workspace->analysis_results().size() == 1U,
          "active input revision change must reject analysis commit atomically");
    }

    application::ScheduledSasaRequest undersized;
    undersized.arguments = {{"selection", "all"},
                            {"probe-radius", "1.4"},
                            {"samples", "32"},
                            {"evaluation-budget", "1000000"},
                            {"unit", "square-angstrom"},
                            {"precision", "6"}};
    undersized.selection_expression = "all";
    undersized.probe_radius_angstrom = 1.4;
    undersized.samples_per_atom = 32U;
    undersized.evaluation_budget = 1000000U;
    undersized.memory_reservation_bytes = 1U;
    const auto rejected = application::schedule_sasa_analysis(
        analysis_workspace, scheduler, std::move(undersized));
    passed &= expect(
        !rejected.has_value() &&
            rejected.error().code == operation::ErrorCode::resource_exhausted &&
            analysis_workspace->analysis_results().size() == 1U,
        "undersized analysis reservation must fail before submission");

    application::ScheduledSasaRequest cancellable;
    cancellable.arguments = {{"selection", "all"},
                             {"probe-radius", "1.4"},
                             {"samples", "10000000"},
                             {"evaluation-budget", "100000000"},
                             {"unit", "square-angstrom"},
                             {"precision", "6"}};
    cancellable.selection_expression = "all";
    cancellable.probe_radius_angstrom = 1.4;
    cancellable.samples_per_atom = 10000000U;
    cancellable.evaluation_budget = 100000000U;
    cancellable.generation = 2U;
    cancellable.generation_is_current = [&](std::uint64_t value) {
      return value == generation.load();
    };
    const auto cancelled = application::schedule_sasa_analysis(
        analysis_workspace, scheduler, std::move(cancellable));
    if (cancelled.has_value())
      static_cast<void>(scheduler->cancel(cancelled.value().task_id));
    passed &= expect(
        cancelled.has_value() &&
            wait_for_state(scheduler, cancelled.value().task_id,
                           operation::TaskState::cancelled) &&
            analysis_workspace->analysis_results().size() == 1U,
        "cancelled analysis must publish neither partial table nor result");
  }

  {
    auto edit_workspace = std::make_shared<application::Workspace>();
    auto edit_registry = application::make_default_registry(edit_workspace);
    const application::Dispatcher edit_dispatcher{edit_registry};
    const gui::ActionAdapter edit_gui{edit_dispatcher};
    const auto edit_loaded = trigger(
        edit_gui, "load", {{"path", pbc_fixture.string()}, {"name", "editable"}});
    const auto *before_object = edit_workspace->active_object();
    const auto atom_id = before_object->system->topology()->atom_ids().front();
    const auto before_frame =
        before_object->system->coordinates()->read_frame(0U).value();
    const auto before_position = std::visit(
        [](const auto &values) {
          return model::Vec3d{static_cast<double>(values[0].x),
                              static_cast<double>(values[0].y),
                              static_cast<double>(values[0].z)};
        },
        before_frame->positions().values());
    const auto tiny_budget = trigger(
        edit_gui, "edit history", {{"memory-budget-bytes", "1"}});
    const auto rejected = trigger(
        edit_gui, "edit atom-position",
        {{"atom-id", std::to_string(atom_id.value)},
         {"x", "9"}, {"y", "8"}, {"z", "7"},
         {"expected-topology-version", "1"},
         {"expected-coordinate-source-revision", "1"},
         {"unit", "angstrom"}});
    const auto restored_budget = trigger(
        edit_gui, "edit history", {{"memory-budget-bytes", "1048576"}});
    const auto edited = trigger(
        edit_gui, "edit atom-position",
        {{"atom-id", std::to_string(atom_id.value)},
         {"x", "9"}, {"y", "8"}, {"z", "7"},
         {"expected-topology-version", "1"},
         {"expected-coordinate-source-revision", "1"},
         {"unit", "angstrom"}});
    const auto *edited_object = edit_workspace->active_object();
    const auto edited_frame0 =
        edited_object->system->coordinates()->read_frame(0U).value();
    const auto position_of = [](const auto &frame) {
      return std::visit(
          [](const auto &values) {
            return model::Vec3d{static_cast<double>(values[0].x),
                                static_cast<double>(values[0].y),
                                static_cast<double>(values[0].z)};
          },
          frame->positions().values());
    };
    const auto stale = trigger(
        edit_gui, "edit atom-position",
        {{"atom-id", std::to_string(atom_id.value)},
         {"x", "1"}, {"y", "1"}, {"z", "1"},
         {"expected-topology-version", "1"},
         {"expected-coordinate-source-revision", "1"}});
    const auto undone = trigger(edit_gui, "edit undo");
    const auto undo_position = position_of(
        edit_workspace->active_object()->system->coordinates()->read_frame(0U)
            .value());
    const auto redone = trigger(edit_gui, "edit redo");
    const auto redo_position = position_of(
        edit_workspace->active_object()->system->coordinates()->read_frame(0U)
            .value());
    const auto history = trigger(edit_gui, "edit history");
    const auto system_before_invalid = edit_workspace->active_object()->system;
    const auto invalid_position = edit_workspace->set_active_atom_position(
        atom_id,
        {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, 1U, 4U);
    const auto missing_atom = edit_workspace->set_active_atom_position(
        model::AtomId{999999U}, {0.0, 0.0, 0.0}, 1U, 4U);
    const auto invalid_unchanged =
        edit_workspace->active_object()->system == system_before_invalid &&
        edit_workspace->active_object()->coordinate_source_revision == 4U;
    const auto branch_undo = trigger(edit_gui, "edit undo");
    const auto branch_edit = trigger(
        edit_gui, "edit atom-position",
        {{"atom-id", std::to_string(atom_id.value)},
         {"x", "6"}, {"y", "5"}, {"z", "4"},
         {"expected-topology-version", "1"},
         {"expected-coordinate-source-revision", "5"},
         {"unit", "angstrom"}});
    const auto invalidated_redo = trigger(edit_gui, "edit redo");
    const auto second_edit = trigger(
        edit_gui, "edit atom-position",
        {{"atom-id", std::to_string(atom_id.value)},
         {"x", "3"}, {"y", "2"}, {"z", "1"},
         {"expected-topology-version", "1"},
         {"expected-coordinate-source-revision", "6"},
         {"unit", "angstrom"}});
    const auto two_record_status = edit_workspace->edit_history_status();
    const auto reduced = edit_workspace->set_edit_history_budget(
        two_record_status.memory_used_bytes / 2U);
    const auto reduced_status = edit_workspace->edit_history_status();
    const auto evicted_all = edit_workspace->set_edit_history_budget(1U);
    const auto no_history_system = edit_workspace->active_object()->system;
    const auto evicted_undo = edit_workspace->undo_edit();
    passed &= expect(
        edit_loaded.succeeded() && tiny_budget.succeeded() &&
            !rejected.succeeded() && restored_budget.succeeded() &&
            edited.succeeded() && !stale.succeeded() && undone.succeeded() &&
            redone.succeeded() && history.succeeded() &&
            position_of(edited_frame0) == model::Vec3d{9.0, 8.0, 7.0} &&
            undo_position == before_position &&
            redo_position == model::Vec3d{9.0, 8.0, 7.0} &&
            !invalid_position.has_value() && !missing_atom.has_value() &&
            invalid_unchanged,
        "bounded coordinate transaction must reject unrecordable/stale edits and round-trip static coordinates through undo/redo");
    passed &= expect(
        system_before_invalid != nullptr && branch_undo.succeeded() &&
            branch_edit.succeeded() && !invalidated_redo.succeeded() &&
            second_edit.succeeded() && two_record_status.undo_count == 2U &&
            reduced.has_value() && reduced_status.undo_count == 1U &&
            reduced_status.redo_count == 0U && evicted_all.has_value() &&
            edit_workspace->edit_history_status().undo_count == 0U &&
            !evicted_undo.has_value() &&
            edit_workspace->active_object()->system == no_history_system &&
            edit_workspace->active_object()->coordinate_source_revision == 7U,
        "new edits must invalidate redo and deterministic budget eviction must preserve the current Workspace when history is exhausted");
  }

  {
    auto topology_workspace = std::make_shared<application::Workspace>();
    auto topology_registry =
        application::make_default_registry(topology_workspace);
    const application::Dispatcher topology_dispatcher{topology_registry};
    const gui::ActionAdapter topology_gui{topology_dispatcher};
    const auto built = trigger(
        topology_gui, "build molecule",
        {{"name", "editable-carbonyl"},
         {"atoms", "C,6,0,0,0,0;O,8,1.2,0,0,0"},
         {"bonds", "1,2,double"},
         {"residue-name", "LIG"}, {"chain", "A"},
         {"residue-number", "1"}, {"unit", "angstrom"},
         {"memory-budget-bytes", "1048576"}});
    const auto *initial_object = topology_workspace->active_object();
    const auto initial_system = initial_object->system;
    const auto atom_ids = initial_system->topology()->atom_ids();
    const auto bond_ids = initial_system->topology()->bond_ids();
    const auto static_selection = topology_workspace->set_named_selection(
        "static_carbon", "name C", false);
    const auto dynamic_selection = topology_workspace->set_named_selection(
        "dynamic_carbon", "name C", true);
    const auto measured = trigger(
        topology_gui, "measure distance",
        {{"from", "index 1"}, {"to", "index 2"}, {"mode", "atom"},
         {"pbc", "raw"}, {"precision", "6"}, {"unit", "angstrom"}});
    operation::TaskContext cancelled_context;
    cancelled_context.cancellation.request_cancel();
    const auto cancelled_edit = topology_gui.trigger(
        gui::Action{"edit atom-properties",
                    {{"atom-id", "1"}, {"name", "cancelled"},
                     {"expected-topology-version", "1"},
                     {"expected-coordinate-source-revision", "1"}}},
        cancelled_context);
    const auto cancelled_build = topology_gui.trigger(
        gui::Action{"build molecule",
                    {{"name", "cancelled-builder"},
                     {"atoms", "H,1,0,0,0,0"}, {"bonds", "none"},
                     {"residue-name", "LIG"}, {"chain", "A"},
                     {"residue-number", "1"}, {"unit", "angstrom"},
                     {"memory-budget-bytes", "1048576"}}},
        cancelled_context);
    const auto stale = trigger(
        topology_gui, "edit atom-properties",
        {{"atom-id", "1"}, {"name", "stale"},
         {"expected-topology-version", "1"},
         {"expected-coordinate-source-revision", "0"}});
    const auto invalid_element = trigger(
        topology_gui, "edit atom-properties",
        {{"atom-id", "1"}, {"atomic-number", "119"},
         {"expected-topology-version", "1"},
         {"expected-coordinate-source-revision", "1"}});
    const auto missing_residue = trigger(
        topology_gui, "edit residue-properties",
        {{"atom-id", "999"}, {"name", "BAD"},
         {"expected-topology-version", "1"},
         {"expected-coordinate-source-revision", "1"}});
    const auto missing_bond = trigger(
        topology_gui, "edit bond-order",
        {{"bond-id", "999"}, {"order", "single"},
         {"expected-topology-version", "1"},
         {"expected-coordinate-source-revision", "1"}});
    const auto failure_atomic =
        topology_workspace->active_object()->system == initial_system &&
        topology_workspace->active_object()->coordinate_source_revision == 1U &&
        topology_workspace->object_count() == 1U &&
        topology_workspace->measurements().size() == 1U &&
        topology_workspace->analysis_results().size() == 1U &&
        topology_workspace->edit_history_status().undo_count == 1U;
    const auto tiny_budget =
        topology_workspace->set_edit_history_budget(1U);
    const auto unrecordable = trigger(
        topology_gui, "edit atom-properties",
        {{"atom-id", "1"}, {"name", "C1"},
         {"expected-topology-version", "1"},
         {"expected-coordinate-source-revision", "1"}});
    const auto unrecordable_unchanged =
        topology_workspace->active_object()->system == initial_system &&
        topology_workspace->active_object()->coordinate_source_revision == 1U;
    const auto restored_budget =
        topology_workspace->set_edit_history_budget(1048576U);
    const auto atom_edited = trigger(
        topology_gui, "edit atom-properties",
        {{"atom-id", "1"}, {"name", "C1"}, {"formal-charge", "1"},
         {"expected-topology-version", "1"},
         {"expected-coordinate-source-revision", "1"}});
    const auto static_after_atom =
        topology_workspace->selection_extent("@static_carbon");
    const auto dynamic_after_atom =
        topology_workspace->selection_extent("@dynamic_carbon");
    const auto residue_edited = trigger(
        topology_gui, "edit residue-properties",
        {{"atom-id", "1"}, {"name", "CRB"}, {"chain", "B"},
         {"residue-number", "7"}, {"expected-topology-version", "2"},
         {"expected-coordinate-source-revision", "2"}});
    const auto bond_edited = trigger(
        topology_gui, "edit bond-order",
        {{"bond-id", "1"}, {"order", "single"},
         {"expected-topology-version", "3"},
         {"expected-coordinate-source-revision", "3"}});
    const auto undone = trigger(topology_gui, "edit undo");
    const auto static_after_undo =
        topology_workspace->selection_extent("@static_carbon");
    const auto redone = trigger(topology_gui, "edit redo");
    const auto *final_object = topology_workspace->active_object();
    const auto &final_topology = *final_object->system->topology();
    passed &= expect(
        built.succeeded() && initial_object != nullptr &&
            !static_selection.has_value() && !dynamic_selection.has_value() &&
            measured.succeeded() && !stale.succeeded() &&
            !cancelled_edit.succeeded() && !cancelled_build.succeeded() &&
            std::get<operation::Error>(cancelled_edit.envelope.payload).code ==
                operation::ErrorCode::cancelled &&
            std::get<operation::Error>(cancelled_build.envelope.payload).code ==
                operation::ErrorCode::cancelled &&
            !invalid_element.succeeded() && !missing_residue.succeeded() &&
            !missing_bond.succeeded() && failure_atomic && tiny_budget.has_value() &&
            !unrecordable.succeeded() && unrecordable_unchanged &&
            restored_budget.has_value(),
        "topology edit validation, stale checks and history reservation must be failure-atomic");
    passed &= expect(
        atom_edited.succeeded() && residue_edited.succeeded() &&
            bond_edited.succeeded() && undone.succeeded() && redone.succeeded() &&
            static_after_atom.has_value() &&
            static_after_atom.value().selected_atom_count == 1U &&
            !dynamic_after_atom.has_value() && static_after_undo.has_value() &&
            final_topology.version() == 4U &&
            final_topology.atom_ids() == atom_ids &&
            final_topology.bond_ids() == bond_ids &&
            final_topology.atoms()[0].name == "C1" &&
            final_topology.atoms()[0].formal_charge == 1 &&
            final_topology.residues()[0].name == "CRB" &&
            final_topology.residues()[0].chain_id == "B" &&
            final_topology.residues()[0].sequence_number == 7 &&
            final_topology.bonds()[0].order == model::BondOrder::single &&
            topology_workspace->measurements().empty() &&
            topology_workspace->analysis_results().size() == 1U &&
            topology_workspace->analysis_source_status(
                topology_workspace->analysis_results()[0]) ==
                application::AnalysisSourceStatus::topology_changed &&
            topology_workspace->edit_history_status().undo_count == 3U &&
            topology_workspace->edit_history_status().redo_count == 0U,
        "atom/residue/bond edits must preserve stable identity, remap static selections, reevaluate dynamic selections, stale derived results and round-trip through undo/redo");
  }

  {
    auto frontend_workspace = std::make_shared<application::Workspace>();
    const auto frontend_registry =
        application::make_default_registry(frontend_workspace);
    const application::Dispatcher frontend_dispatcher{frontend_registry};
    const gui::ActionAdapter frontend_gui{frontend_dispatcher};
    double frontend_progress{};
    operation::TaskContext frontend_context{
        {}, [&](const auto& update) { frontend_progress = update.fraction; }};
    const gui::Action batch_action{
        "load batch",
        {{"file-format", "pdb"},
         {"names", "frontend-one;frontend-two"},
         {"paths", fixture.string() + ";" + fixture.string()}}};
    const auto loaded = frontend_gui.trigger(batch_action, frontend_context);
    passed &= expect(loaded.succeeded() && frontend_progress == 1.0 &&
                         frontend_workspace->object_count() == 2U,
                     "GUI adapter must observe canonical batch progress");

    auto cancelled_workspace = std::make_shared<application::Workspace>();
    const auto cancelled_registry =
        application::make_default_registry(cancelled_workspace);
    const application::Dispatcher cancelled_dispatcher{cancelled_registry};
    const gui::ActionAdapter cancelled_gui{cancelled_dispatcher};
    operation::TaskContext cancelled_context;
    cancelled_context.cancellation.request_cancel();
    const auto cancelled =
        cancelled_gui.trigger(batch_action, cancelled_context);
    passed &= expect(!cancelled.succeeded() &&
                         std::get<operation::Error>(
                             cancelled.envelope.payload).code ==
                             operation::ErrorCode::cancelled &&
                         cancelled_workspace->object_count() == 0U,
                     "GUI adapter cancellation must preserve an empty Workspace");
  }

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
         {"cache-mib", "1"}, {"mapping", "index"},
         {"prefetch-frames", "0"}});
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

  const render::RenderSettingScope setting_global{};
  const render::RenderSettingScope setting_object{
      render::RenderSettingScopeLevel::object, 1U};
  const render::RenderSettingScope setting_state{
      render::RenderSettingScopeLevel::object_state, 1U, 1U};
  const render::RenderSettingScope setting_atom{
      render::RenderSettingScopeLevel::atom, 1U, 1U, 1U};
  const render::RenderSettingScope setting_bond{
      render::RenderSettingScopeLevel::bond, 1U, 1U, 0U, 1U};
  passed &= expect(
      !workspace->set_render_setting("line_width", setting_global, 2.0)
           .has_value() &&
          !workspace->set_render_setting("line_width", setting_object, 3.0)
               .has_value() &&
          !workspace->set_render_setting("line_width", setting_state, 4.0)
               .has_value() &&
          !workspace->set_render_setting("line_width", setting_atom, 5.0)
               .has_value() &&
          !workspace->set_render_setting("line_width", setting_bond, 6.0)
               .has_value() &&
          std::get<double>(workspace
                               ->resolve_render_setting(
                                   "line_width", {1U, 1U, 1U, 1U})
                               .value()
                               .value) == 6.0,
      "Workspace must resolve validated bond-to-global setting precedence");
  const auto setting_snapshot = workspace->render_setting_snapshot();
  const auto setting_text = render::serialize_render_settings(setting_snapshot);
  const auto parsed_settings =
      setting_text.has_value()
          ? render::parse_render_settings(setting_text.value())
          : operation::Result<render::RenderSettingSnapshot>::failure(
                setting_text.error());
  const render::RenderSettingScope missing_setting_atom{
      render::RenderSettingScopeLevel::atom, 1U, 1U, 99U};
  const auto invalid_setting_target = workspace->set_render_setting(
      "line_width", missing_setting_atom, 9.0);
  const auto restored_settings =
      parsed_settings.has_value()
          ? workspace->restore_render_settings(parsed_settings.value())
          : std::optional<operation::Error>{parsed_settings.error()};
  passed &= expect(
      setting_text.has_value() && parsed_settings.has_value() &&
          invalid_setting_target.has_value() &&
          workspace->render_setting_snapshot() == setting_snapshot &&
          !restored_settings.has_value(),
      "Workspace setting target validation and session restore must be failure-atomic");

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
  const auto original_sphere_radius = workspace->active_object()
                                          ->representations.front()
                                          .packet.spheres.front()
                                          .radius;
  const auto setting_applied = trigger(
      gui, "setting set", {{"name", "sphere_scale"},
                            {"value", "2"},
                            {"scope", "atom"},
                            {"target", "1"}});
  const auto updated_sphere_radius = workspace->active_object()
                                         ->representations.front()
                                         .packet.spheres.front()
                                         .radius;
  const auto setting_before_failure = workspace->render_setting_snapshot();
  const auto failed_setting = trigger(
      gui, "setting set", {{"name", "sphere_color"},
                            {"value", "not-a-color"},
                            {"scope", "atom"},
                            {"target", "1"}});
  passed &= expect(
      setting_applied.succeeded() &&
          std::abs(updated_sphere_radius - original_sphere_radius * 2.0) <
              1.0e-12 &&
          !failed_setting.succeeded() &&
          workspace->render_setting_snapshot() == setting_before_failure &&
          workspace->active_object()
                  ->representations.front()
                  .packet.spheres.front()
                  .radius == updated_sphere_radius,
      "canonical setting operation must rebuild geometry and preserve scene on invalid update");
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

  const auto additive_visibility = workspace->mutate_representation_visibility(
      render::RepresentationKind::spheres, "index 1",
      application::RepresentationVisibilityMutation::show);
  const auto selective_hide = workspace->mutate_representation_visibility(
      render::RepresentationKind::sticks, "index 1",
      application::RepresentationVisibilityMutation::hide);
  const auto visibility_snapshot =
      workspace->representation_visibility_session_snapshot();
  const auto visibility_text =
      visibility_snapshot.has_value()
          ? application::serialize_representation_visibility_session(
                visibility_snapshot.value())
          : operation::Result<std::string>::failure(
                visibility_snapshot.error());
  const auto visibility_parsed =
      visibility_text.has_value()
          ? application::parse_representation_visibility_session(
                visibility_text.value())
          : operation::Result<
                application::RepresentationVisibilitySessionSnapshot>::
                failure(visibility_text.error());
  const auto exclusive_visibility =
      workspace->mutate_representation_visibility(
          render::RepresentationKind::cartoon, "index 2",
          application::RepresentationVisibilityMutation::exclusive);
  const auto restored_visibility =
      visibility_parsed.has_value()
          ? workspace->restore_representation_visibility_session(
                visibility_parsed.value())
          : operation::Result<application::RepresentationVisibilityResult>::
                failure(visibility_parsed.error());
  passed &= expect(
      additive_visibility.has_value() && selective_hide.has_value() &&
          additive_visibility.value().representation_count == 2U &&
          selective_hide.value().visible_atom_count == 1U &&
          visibility_snapshot.has_value() && visibility_text.has_value() &&
          visibility_parsed.has_value() && exclusive_visibility.has_value() &&
          exclusive_visibility.value().representation_count == 2U &&
          restored_visibility.has_value() &&
          restored_visibility.value().representation_count == 2U &&
          workspace->active_object()->representations.size() == 2U,
      "Workspace visibility must support additive show, selective hide, exclusive replacement and session-fragment restore");

  const auto stable_visibility =
      workspace->representation_visibility_snapshot().value();
  auto wrong_identity_snapshot =
      workspace->representation_visibility_session_snapshot().value();
  ++wrong_identity_snapshot.object_id;
  const auto failed_identity_restore =
      workspace->restore_representation_visibility_session(
          wrong_identity_snapshot);
  auto wrong_object_snapshot = stable_visibility;
  ++wrong_object_snapshot.atom_count;
  for (auto &mask : wrong_object_snapshot.masks)
    mask.resize((wrong_object_snapshot.atom_count + 63U) / 64U, 0U);
  const auto failed_visibility_restore =
      workspace->restore_representation_visibility(wrong_object_snapshot);
  passed &= expect(
      !failed_identity_restore.has_value() &&
          !failed_visibility_restore.has_value() &&
          workspace->representation_visibility_snapshot().value() ==
              stable_visibility &&
          workspace->active_object()->representations.size() == 2U,
      "visibility restore for another topology must fail without mutating Workspace state");

  const auto center = trigger(
      gui, "analyze center",
      {{"selection", "@chain_a"}, {"mode", "centroid"},
       {"precision", "3"}, {"unit", "angstrom"}});
  const auto distance = trigger(
      gui, "measure distance",
      {{"from", "index 1"}, {"to", "index 2"}, {"mode", "atom"},
       {"pbc", "raw"}, {"precision", "6"}, {"unit", "angstrom"}});
  const auto result_list = trigger(gui, "result list");
  const auto result_get = trigger(gui, "result get", {{"id", "2"}});
  const auto result_hidden = trigger(gui, "result hide", {{"id", "2"}});
  const auto result_shown = trigger(gui, "result show", {{"id", "2"}});
  const auto measurements_before_duplicate_result =
      workspace->measurements().size();
  const auto named_center = trigger(
      gui, "analyze center",
      {{"selection", "all"}, {"mode", "centroid"},
       {"precision", "6"}, {"unit", "angstrom"},
       {"result-name", "stable-center"}});
  const auto duplicate_result_name = trigger(
      gui, "measure distance",
      {{"from", "index 1"}, {"to", "index 2"}, {"mode", "atom"},
       {"pbc", "raw"}, {"precision", "6"}, {"unit", "angstrom"},
       {"result-name", "stable-center"}});
  const auto *get_response = result_get.succeeded()
                                 ? std::get_if<command::Response>(
                                       &result_get.envelope.payload)
                                 : nullptr;
  passed &= expect(center.succeeded() && distance.succeeded() &&
                       result_list.succeeded() && result_get.succeeded() &&
                       result_hidden.succeeded() && result_shown.succeeded() &&
                       named_center.succeeded() &&
                       !duplicate_result_name.succeeded() &&
                       workspace->measurements().size() == 1U &&
                       workspace->measurements().size() ==
                           measurements_before_duplicate_result &&
                       workspace->measurements()[0].measurement_id == 1U &&
                       workspace->analysis_results().size() == 3U &&
                       get_response != nullptr &&
                       std::get<std::string>(
                           get_response->fields.at("source_status").data) ==
                           "current" &&
                       std::get<std::string>(
                           get_response->fields.at("algorithm_version").data) ==
                           "molshredder-distance-v1" &&
                       workspace->analysis_results()[1].overlay_visible,
                   "analysis operations must persist queryable results and duplicate-name failure must be atomic");
  const auto secondary=trigger(gui,"analyze secondary-structure",
      {{"selection","all"},{"energy-cutoff","-0.5"},
       {"helix-propensity","0.05"},{"beta-propensity","0.02"},
       {"precision","3"}});
  const auto* secondary_response=secondary.succeeded()
      ? std::get_if<command::Response>(&secondary.envelope.payload) : nullptr;
  passed &= expect(secondary_response!=nullptr && secondary_response->table.has_value() &&
      std::get<std::string>(secondary_response->fields.at("method").data)==
          "molshredder-stride-method-v0" &&
      std::get<std::string>(secondary_response->fields.at("algorithm_version").data)==
          "molshredder-stride-method-v0" &&
      std::get<std::string>(secondary_response->fields.at("coordinate_scope").data)==
          "current_frame" &&
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
  const auto second_node = workspace->objects()[1].scene_node;
  const auto renamed = trigger(
      gui, "object rename", {{"object", "2"}, {"name", "renamed ligand"}});
  const auto scene_after_rename = workspace->scene();
  const auto duplicate_rename = trigger(
      gui, "object rename", {{"object", "2"}, {"name", "protein"}});
  passed &= expect(
      renamed.succeeded() && !duplicate_rename.succeeded() &&
          workspace->objects()[1].id == 2U &&
          workspace->objects()[1].scene_node == second_node &&
          workspace->objects()[1].system->name() == "renamed ligand" &&
          workspace->scene()->find(second_node)->name() == "renamed ligand" &&
          workspace->scene() == scene_after_rename,
      "rename must preserve object/scene identity and duplicate failure must be atomic");

  const auto third = trigger(
      gui, "load", {{"path", fixture.string()}, {"name", "third"}});
  const auto third_node = workspace->objects()[2].scene_node;
  const auto third_measurement = trigger(
      gui, "measure distance",
      {{"from", "index 1"}, {"to", "index 2"}, {"mode", "atom"},
       {"pbc", "raw"}, {"precision", "6"}, {"unit", "angstrom"}});
  const auto third_setting = trigger(
      gui, "setting set",
      {{"name", "sphere_scale"}, {"value", "2.0"},
       {"scope", "object"}});
  const auto reordered = trigger(
      gui, "object reorder", {{"object", "3"}, {"position", "1"}});
  const auto scene_after_reorder = workspace->scene();
  const auto invalid_reorder = trigger(
      gui, "object reorder", {{"object", "3"}, {"position", "9"}});
  passed &= expect(
      third.succeeded() && third_measurement.succeeded() &&
          third_setting.succeeded() && reordered.succeeded() &&
          !invalid_reorder.succeeded() && workspace->objects()[0].id == 3U &&
          workspace->objects()[1].id == 1U &&
          workspace->objects()[2].id == 2U &&
          workspace->active_object()->id == 3U &&
          workspace->scene()->find(workspace->scene()->root())->children()[0] ==
              workspace->objects()[0].scene_node &&
          workspace->scene() == scene_after_reorder,
      "reorder must preserve stable identity/active object, update scene order and reject invalid positions atomically");
  const auto measurements_before_delete = workspace->measurements().size();
  const auto deleted =
      trigger(gui, "object delete", {{"object", "current"}});
  const auto setting_snapshot_after_delete = workspace->render_setting_snapshot();
  passed &= expect(
      deleted.succeeded() && workspace->object_count() == 2U &&
          workspace->objects()[0].id == 1U && workspace->objects()[1].id == 2U &&
          workspace->active_object()->id == 1U &&
          workspace->scene()->find(third_node) == nullptr &&
          workspace->measurements().size() + 1U == measurements_before_delete &&
          std::any_of(workspace->analysis_results().begin(),
                      workspace->analysis_results().end(),
                      [&](const auto &record) {
                        return record.provenance.scientific.topology.object_id ==
                                   3U &&
                               workspace->analysis_source_status(record) ==
                                   application::AnalysisSourceStatus::object_deleted;
                      }) &&
          std::none_of(setting_snapshot_after_delete.overrides.begin(),
                       setting_snapshot_after_delete.overrides.end(),
                       [](const auto &entry) {
                         return entry.scope.object_id == 3U;
                       }),
      "delete must remove scene/object/dependent state and choose the next active object in one transaction");
  const auto object_one_visibility_before_hide =
      workspace->objects()[0].representation_visibility.snapshot();
  const auto hidden = trigger(
      gui, "object visibility", {{"id", "1"}, {"visible", "false"}});
  passed &= expect(hidden.succeeded() &&
                       !workspace->scene()->effectively_visible(
                           workspace->objects()[0].scene_node),
                   "object visibility must commit through immutable scene");
  const auto activated = trigger(gui, "object activate", {{"id", "1"}});
  passed &= expect(activated.succeeded() &&
                       workspace->active_object()->id == 1U &&
                       workspace->active_object()
                               ->representation_visibility.snapshot() ==
                           object_one_visibility_before_hide &&
                       workspace->scene()->selection().contains(
                           workspace->active_object()->scene_node),
                   "object activation must preserve representation visibility while updating scene selection");
  const auto previous_scene = workspace->scene();
  const auto missing_object =
      trigger(gui, "object activate", {{"id", "999"}});
  const auto above_uint32_object = trigger(
      gui, "object activate", {{"id", "4294967296"}});
  const auto max_uint64_object = trigger(
      gui, "object activate", {{"id", "18446744073709551615"}});
  const auto overflow_uint64_object = trigger(
      gui, "object activate", {{"id", "18446744073709551616"}});
  passed &= expect(!missing_object.succeeded() &&
                       !above_uint32_object.succeeded() &&
                       !max_uint64_object.succeeded() &&
                       !overflow_uint64_object.succeeded() &&
                       std::get<operation::Error>(above_uint32_object.envelope.payload)
                               .code == operation::ErrorCode::not_found &&
                       std::get<operation::Error>(max_uint64_object.envelope.payload)
                               .code == operation::ErrorCode::not_found &&
                       std::get<operation::Error>(overflow_uint64_object.envelope.payload)
                               .code == operation::ErrorCode::invalid_argument &&
                       workspace->active_object()->id == 1U &&
                       workspace->scene() == previous_scene,
                   "object ID parser must preserve uint32/uint64 boundaries and failed activation must preserve Workspace state");
  passed &= expect(trigger(gui, "object visibility",
                           {{"id", "1"}, {"visible", "true"}})
                       .succeeded(),
                   "hidden object must be showable without losing state");
  passed &= expect(
      !workspace->set_named_selection(
           "stable_first", "index 1", false)
           .has_value(),
      "static selection fixture must be created");
  const auto topology_visibility =
      workspace->mutate_representation_visibility(
          render::RepresentationKind::spheres, "index 1",
          application::RepresentationVisibilityMutation::show);
  const auto deleted_target_setting = trigger(
      gui, "setting set", {{"name", "sphere_scale"}, {"value", "3"},
                            {"scope", "atom"}, {"target", "2"}});
  const auto measurements_before_topology = workspace->measurements().size();
  const auto topology_changed = trigger(
      gui, "object topology-retain",
      {{"atom-ids", "3,1"}, {"expected-version", "1"}});
  const auto topology_scene = workspace->scene();
  const auto topology_system = workspace->active_object()->system;
  const auto stale_visibility_restore =
      workspace->restore_representation_visibility_session(
          visibility_snapshot.value());
  const auto stale_topology_change = trigger(
      gui, "object topology-retain",
      {{"atom-ids", "1"}, {"expected-version", "1"}});
  const auto remapped_static = workspace->active_object()->selections.evaluate(
      "stable_first", *workspace->active_object()->system->topology());
  const auto remapped_spheres = workspace->active_object()
                                    ->representation_visibility.selection_mask(
                                        render::RepresentationKind::spheres);
  const auto settings_after_topology = workspace->render_setting_snapshot();
  passed &= expect(
      topology_visibility.has_value() && deleted_target_setting.succeeded() &&
          topology_changed.succeeded() &&
          !stale_visibility_restore.has_value() &&
          !stale_topology_change.succeeded() &&
          workspace->active_object()->system == topology_system &&
          workspace->scene() == topology_scene &&
          topology_system->topology()->version() == 2U &&
          topology_system->topology()->atom_ids() ==
              std::vector<model::AtomId>{{3U}, {1U}} &&
          topology_system->coordinates()->read_frame(0U).value()->atom_count() ==
              2U &&
          remapped_static.has_value() &&
          remapped_static.value() == selection::Mask({0U, 1U}) &&
          remapped_spheres.has_value() && remapped_spheres.value()[1] == 1U &&
          workspace->measurements().empty() &&
          measurements_before_topology > 0U &&
          std::any_of(workspace->analysis_results().begin(),
                      workspace->analysis_results().end(),
                      [&](const auto &record) {
                        return record.provenance.scientific.topology.object_id ==
                                   1U &&
                               workspace->analysis_source_status(record) ==
                                   application::AnalysisSourceStatus::topology_changed;
                      }) &&
          std::any_of(settings_after_topology.overrides.begin(),
                      settings_after_topology.overrides.end(),
                      [](const auto &entry) {
                        return entry.scope.level ==
                                   render::RenderSettingScopeLevel::atom &&
                               entry.scope.atom_id == 1U;
                      }) &&
          std::none_of(settings_after_topology.overrides.begin(),
                       settings_after_topology.overrides.end(),
                       [](const auto &entry) {
                         return entry.scope.level ==
                                    render::RenderSettingScopeLevel::atom &&
                                entry.scope.atom_id == 2U;
                       }),
      "topology transaction must remap stable selection/visibility/settings/coordinates, invalidate measurements and reject stale completion atomically");
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
  const auto console_exit = console.run(input, output, error);
  const auto console_ok = console_exit == 0 && error.str().empty() &&
                       console_workspace->object_count() == 1U &&
                       console_workspace->active_object()
                               ->representations.size() == 1U &&
                       console.history().size() == 3U &&
                       output.str().find("\"primitive_count\":2") !=
                           std::string::npos;
  if (!console_ok) {
    std::cerr << "console_exit=" << console_exit
              << " error=" << error.str()
              << " objects=" << console_workspace->object_count()
              << " representations="
              << console_workspace->active_object()->representations.size()
              << " history=" << console.history().size()
              << " output=" << output.str() << '\n';
  }
  passed &= expect(console_ok,
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
      std::get<std::string>(hbond_response->fields.at("algorithm_version").data)==
          "molshredder-hbond-v1" &&
      std::get<bool>(hbond_response->fields.at("typing_estimated").data),
      "GUI H-bond command must expose geometry table and typing provenance");

  return passed ? 0 : 1;
}
