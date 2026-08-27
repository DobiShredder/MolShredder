#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/session.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/version.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

std::optional<std::string> response_extension(
    const molshredder::application::DispatchOutcome &outcome,
    std::string_view key) {
  const auto *response =
      std::get_if<molshredder::command::Response>(&outcome.envelope.payload);
  if (response == nullptr) return std::nullopt;
  const auto extensions_field = response->fields.find("extensions");
  if (extensions_field == response->fields.end()) return std::nullopt;
  const auto *extensions =
      std::get_if<molshredder::command::Value::Object>(
          &extensions_field->second.data);
  if (extensions == nullptr) return std::nullopt;
  const auto found = extensions->find(key);
  if (found == extensions->end()) return std::nullopt;
  const auto *value = std::get_if<std::string>(&found->second.data);
  return value == nullptr ? std::nullopt
                          : std::optional<std::string>{*value};
}

}  // namespace

int main(int argc, char** argv) {
  using namespace molshredder;
  bool passed = true;
  if (argc != 2) {
    std::cerr << "expected PDB fixture path\n";
    return 1;
  }
  const std::filesystem::path fixture{argv[1]};
  application::SessionDocument document;
  document.generator_version = std::string{version()};
  document.extensions = {{"future.panel.state", "opaque value = 17"},
                         {"vendor.example", "preserve-me"}};
  document.invocations = {
      {"load", {{"file-format", "pdb"},
                {"name", "session_object"},
                {"path", fixture.string()}}},
      {"select", {{"expression", "chain A"},
                  {"name", "chain_a"},
                  {"update", "false"}}},
      {"show", {{"representation", "spheres"},
                {"selection", "@chain_a"}}},
      {"hide", {{"representation", "spheres"},
                 {"selection", "index 1"}}},
      {"toggle", {{"representation", "lines"},
                   {"selection", "index 1"}}},
      {"as", {{"representation", "licorice"},
               {"selection", "index 2"}}},
      {"setting set", {{"name", "sphere_scale"},
                       {"object", "current"},
                       {"scope", "atom"},
                       {"state", "current"},
                       {"target", "1"},
                       {"value", "2.5"}}},
      {"measure distance", {{"from", "index 1"},
                            {"mode", "atom"},
                            {"pbc", "raw"},
                            {"precision", "6"},
                            {"to", "index 2"},
                            {"unit", "angstrom"}}}};

  const auto serialized = application::serialize_session(document);
  const auto parsed = serialized.has_value()
                          ? application::parse_session(serialized.value())
                          : operation::Result<application::SessionDocument>::failure(
                                serialized.error());
  passed &= expect(serialized.has_value() && parsed.has_value() &&
                       parsed.value() == document &&
                       serialized.value().starts_with(
                           "molshredder-session 2\ngenerator 0.1.0\n") &&
                       serialized.value().find(
                           "metadata invoke \"session metadata\"") !=
                           std::string::npos &&
                       serialized.value().find(
                           "invoke \"measure distance\"") != std::string::npos,
                   "session document must deterministically round-trip");

  auto workspace = std::make_shared<application::Workspace>();
  auto registry = application::make_default_registry(workspace);
  const application::Dispatcher dispatcher{registry};
  operation::TaskContext context;
  const auto replay =
      application::replay_session(parsed.value(), dispatcher, context);
  passed &= expect(
      replay.has_value() && replay.value().applied_count == 8U &&
          replay.value().outcomes.size() == 8U &&
          workspace->object_count() == 1U &&
          workspace->active_object()->representations.size() == 2U &&
          workspace->active_object()
                  ->representation_visibility
                  .visible_count(render::RepresentationKind::lines)
                  .value() == 1U &&
          workspace->active_object()
                  ->representation_visibility
                  .visible_count(render::RepresentationKind::sticks)
                  .value() == 1U &&
          workspace->active_object()
                  ->representation_visibility
                  .visible_count(render::RepresentationKind::spheres)
                  .value() == 0U &&
          std::get<double>(workspace->resolve_render_setting(
              "sphere_scale", {workspace->active_object()->id, 0U, 1U, 0U})
                               .value()
                               .value) == 2.5 &&
          workspace->measurements().size() == 1U &&
          workspace->analysis_results().size() == 1U &&
          workspace->analysis_results()[0].kind ==
              application::AnalysisResultKind::distance &&
          workspace->analysis_results()[0].overlay_visible,
      "session replay must reconstruct exact representation visibility and foundation Workspace state");

  // Product registries record only successful normalized state mutations.
  // Query commands and failed changes must not pollute a restorable document.
  auto captured_workspace = std::make_shared<application::Workspace>();
  auto captured_registry =
      application::make_default_registry(captured_workspace);
  const application::Dispatcher captured_dispatcher{captured_registry};
  operation::TaskContext captured_context;
  const auto captured_load = captured_dispatcher.dispatch(
      {"load", {{"file-format", "pdb"},
                {"name", "captured"},
                {"path", fixture.string()}}},
      captured_context);
  const auto captured_show = captured_dispatcher.dispatch(
      {"show", {{"representation", "spheres"}, {"selection", "all"}}},
      captured_context);
  const auto captured_query = captured_dispatcher.dispatch(
      {"object list", {}}, captured_context);
  const auto captured_failure = captured_dispatcher.dispatch(
      {"object visibility", {{"id", "999"}, {"visible", "false"}}},
      captured_context);
  const auto captured_document = application::capture_session_document(
      *captured_workspace, std::string{version()},
      {{"ui.active-panel", "objects"}});
  auto captured_restored = std::make_shared<application::Workspace>();
  operation::TaskContext captured_replay_context;
  const auto captured_replay =
      captured_document.has_value()
          ? application::replay_session_atomically(
                captured_document.value(), captured_restored,
                captured_replay_context)
          : operation::Result<application::SessionReplayResult>::failure(
                captured_document.error());
  passed &= expect(
      captured_load.succeeded() && captured_show.succeeded() &&
          captured_query.succeeded() && !captured_failure.succeeded() &&
          captured_document.has_value() &&
          captured_document.value().extensions.at("ui.active-panel") ==
              "objects" &&
          captured_document.value().invocations.size() == 4U &&
          captured_document.value().invocations[0].canonical_name == "load" &&
          captured_document.value().invocations[1].canonical_name == "show" &&
          captured_document.value().invocations[2].canonical_name ==
              "view set" &&
          captured_document.value().invocations[3].canonical_name ==
              "stereo set" &&
          captured_replay.has_value() &&
          captured_replay.value().applied_count == 4U &&
          captured_restored->object_count() == 1U &&
          captured_restored->active_object()->representations.size() == 1U &&
          captured_restored->session_invocations().size() == 4U,
      "captured session must include successful canonical mutations and terminal view state only");

  const auto stored_scene = captured_dispatcher.dispatch(
      {"scene store", {{"name", "baseline"}}}, captured_context);
  const auto hidden_after_scene = captured_dispatcher.dispatch(
      {"object visibility", {{"id", "1"}, {"visible", "false"}}},
      captured_context);
  const auto moved_after_scene = captured_dispatcher.dispatch(
      {"view set", {{"target-x", "99"}}}, captured_context);
  const auto recalled_scene = captured_dispatcher.dispatch(
      {"scene recall", {{"name", "baseline"}}}, captured_context);
  const auto before_missing_scene = captured_workspace->camera().parameters();
  const auto missing_scene = captured_dispatcher.dispatch(
      {"scene recall", {{"name", "missing"}}}, captured_context);
  const auto scene_document = application::capture_session_document(
      *captured_workspace, std::string{version()});
  auto scene_restored_workspace =
      std::make_shared<application::Workspace>();
  operation::TaskContext scene_replay_context;
  const auto scene_replay =
      scene_document.has_value()
          ? application::replay_session_atomically(
                scene_document.value(), scene_restored_workspace,
                scene_replay_context)
          : operation::Result<application::SessionReplayResult>::failure(
                scene_document.error());
  const auto live_scenes = captured_workspace->list_named_scenes();
  const auto restored_scenes = scene_restored_workspace->list_named_scenes();
  passed &= expect(
      stored_scene.succeeded() && hidden_after_scene.succeeded() &&
          moved_after_scene.succeeded() && recalled_scene.succeeded() &&
          !missing_scene.succeeded() &&
          captured_workspace->camera().parameters() == before_missing_scene &&
          captured_workspace->camera().parameters().target.x != 99.0 &&
          captured_workspace->list_objects()[0].visible &&
          live_scenes.size() == 1U && live_scenes[0].name == "baseline" &&
          live_scenes[0].current && scene_document.has_value() &&
          scene_document.value().invocations.size() == 8U &&
          scene_replay.has_value() &&
          scene_replay.value().applied_count == 8U &&
          scene_restored_workspace->list_objects()[0].visible &&
          scene_restored_workspace->camera().parameters() ==
              captured_workspace->camera().parameters() &&
          restored_scenes == live_scenes,
      "named scene recall and session replay must restore full state atomically with stable identity");

  const auto configured_movie = captured_dispatcher.dispatch(
      {"movie configure", {{"fps", "24"}, {"frames", "3"},
                           {"loop", "true"}}},
      captured_context);
  const auto stored_movie_key = captured_dispatcher.dispatch(
      {"movie keyframe", {{"frame", "2"}, {"scene", "baseline"}}},
      captured_context);
  const auto hidden_before_movie = captured_dispatcher.dispatch(
      {"object visibility", {{"id", "1"}, {"visible", "false"}}},
      captured_context);
  const auto sought_movie = captured_dispatcher.dispatch(
      {"movie seek", {{"frame", "2"}}}, captured_context);
  const auto stepped_movie = captured_dispatcher.dispatch(
      {"movie step", {{"steps", "2"}}}, captured_context);
  const auto played_movie = captured_dispatcher.dispatch(
      {"movie play", {}}, captured_context);
  const auto paused_movie = captured_dispatcher.dispatch(
      {"movie pause", {}}, captured_context);
  const auto movie_status = captured_dispatcher.dispatch(
      {"movie status", {}}, captured_context);
  const auto movie_before_failure = captured_workspace->movie();
  const auto failed_movie_seek = captured_dispatcher.dispatch(
      {"movie seek", {{"frame", "4"}}}, captured_context);
  const auto movie_document = application::capture_session_document(
      *captured_workspace, std::string{version()});
  auto movie_restored_workspace =
      std::make_shared<application::Workspace>();
  operation::TaskContext movie_replay_context;
  const auto movie_replay =
      movie_document.has_value()
          ? application::replay_session_atomically(
                movie_document.value(), movie_restored_workspace,
                movie_replay_context)
          : operation::Result<application::SessionReplayResult>::failure(
                movie_document.error());
  passed &= expect(
      configured_movie.succeeded() && stored_movie_key.succeeded() &&
          hidden_before_movie.succeeded() && sought_movie.succeeded() &&
          stepped_movie.succeeded() && played_movie.succeeded() &&
          paused_movie.succeeded() && movie_status.succeeded() &&
          !failed_movie_seek.succeeded() &&
          captured_workspace->movie() == movie_before_failure &&
          captured_workspace->movie().has_value() &&
          captured_workspace->movie()->frame_count == 3U &&
          captured_workspace->movie()->current_frame == 1U &&
          captured_workspace->movie()->frames_per_second == 24.0 &&
          captured_workspace->movie()->loop &&
          !captured_workspace->movie()->playing &&
          captured_workspace->movie()->keyframes.size() == 1U &&
          captured_workspace->list_objects()[0].visible &&
          movie_document.has_value() && movie_replay.has_value() &&
          movie_restored_workspace->movie() == captured_workspace->movie() &&
          movie_restored_workspace->list_named_scenes() ==
              captured_workspace->list_named_scenes() &&
          movie_restored_workspace->list_objects()[0].visible,
      "typed movie scene keyframes must seek/loop and replay without arbitrary command execution");

  const auto command_file_root = std::filesystem::temp_directory_path() /
      ("molshredder-session-command-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  std::error_code command_file_error;
  std::filesystem::create_directory(command_file_root, command_file_error);
  const auto command_session_path = command_file_root / "saved.msess";
  const auto command_primary_path = command_file_root / "primary.msess";
  const auto command_saved = command_file_error
      ? application::DispatchOutcome{}
      : captured_dispatcher.dispatch(
            {"session save", {{"maximum-mib", "1"},
                              {"path", command_session_path.string()},
                              {"ui-visible-panels", "analysis,views"}}},
            captured_context);
  const auto command_mutated = captured_dispatcher.dispatch(
      {"object visibility", {{"id", "1"}, {"visible", "false"}}},
      captured_context);
  const auto command_loaded = captured_dispatcher.dispatch(
      {"session load", {{"maximum-mib", "1"},
                        {"path", command_session_path.string()}}},
      captured_context);
  {
    std::ofstream corrupt{command_primary_path, std::ios::binary};
    corrupt << "molshredder-session 2\ngenerator";
  }
  const auto command_recovered = captured_dispatcher.dispatch(
      {"session load", {{"maximum-mib", "1"},
                        {"path", command_primary_path.string()},
                        {"recovery", command_session_path.string()}}},
      captured_context);
  application::SessionDocument unsafe_document;
  unsafe_document.generator_version = std::string{version()};
  unsafe_document.invocations = {
      {"save", {{"all-frames", "false"}, {"decimal-places", "3"},
                {"file-format", "pdb"}, {"overwrite", "true"},
                {"path", (command_file_root / "unsafe.pdb").string()},
                {"provider", "native"}}}};
  operation::TaskContext unsafe_context;
  const auto unsafe_before_count = captured_workspace->object_count();
  const auto unsafe_replay = application::replay_session_atomically(
      unsafe_document, captured_workspace, unsafe_context);
  const auto command_visible_panels =
      response_extension(command_loaded, "ui.visible-panels");
  passed &= expect(
      command_saved.succeeded() && command_mutated.succeeded() &&
          command_loaded.succeeded() && command_recovered.succeeded() &&
          command_visible_panels == "analysis,views" &&
          captured_workspace->list_objects()[0].visible &&
          !unsafe_replay.has_value() &&
          unsafe_replay.error().code == operation::ErrorCode::unsupported &&
          captured_workspace->object_count() == unsafe_before_count &&
          !std::filesystem::exists(command_file_root / "unsafe.pdb"),
      "canonical session save/load/recovery must be atomic and reject unsafe export commands");

  const auto autosave_primary = command_file_root / "autosave.msess";
  const auto autosave_recovery = command_file_root / "autosave.previous.msess";
  const auto first_autosave = captured_dispatcher.dispatch(
      {"session autosave", {{"maximum-mib", "1"},
                            {"path", autosave_primary.string()},
                            {"recovery", autosave_recovery.string()},
                            {"ui-visible-panels", "analysis,views"}}},
      captured_context);
  const auto autosave_mutation = captured_dispatcher.dispatch(
      {"object visibility", {{"id", "1"}, {"visible", "false"}}},
      captured_context);
  const auto second_autosave = captured_dispatcher.dispatch(
      {"session autosave", {{"maximum-mib", "1"},
                            {"path", autosave_primary.string()},
                            {"recovery", autosave_recovery.string()},
                            {"ui-visible-panels", "views"}}},
      captured_context);
  {
    std::ofstream corrupt{autosave_primary,
                          std::ios::binary | std::ios::trunc};
    corrupt << "corrupt";
  }
  const auto rotated_recovery = captured_dispatcher.dispatch(
      {"session load", {{"maximum-mib", "1"},
                        {"path", autosave_primary.string()},
                        {"recovery", autosave_recovery.string()}}},
      captured_context);
  const auto recovery_visible_panels =
      response_extension(rotated_recovery, "ui.visible-panels");
  passed &= expect(
      first_autosave.succeeded() && autosave_mutation.succeeded() &&
          second_autosave.succeeded() && rotated_recovery.succeeded() &&
          recovery_visible_panels == "analysis,views" &&
          std::filesystem::is_regular_file(autosave_recovery) &&
          captured_workspace->list_objects()[0].visible,
      "two-generation autosave must rotate primary to explicit recovery and recover the prior state");
  std::filesystem::remove_all(command_file_root, command_file_error);

  auto final_camera_parameters = workspace->camera().parameters();
  final_camera_parameters.target = {3.25, -2.5, 7.75};
  final_camera_parameters.model_origin = {1.0, 2.0, 3.0};
  final_camera_parameters.distance = 42.5;
  final_camera_parameters.projection = scene::ProjectionMode::orthographic;
  final_camera_parameters.orthographic_height = 18.25;
  final_camera_parameters.aspect_ratio = 1.75;
  final_camera_parameters.near_clip = 0.25;
  final_camera_parameters.far_clip = 900.0;
  const auto final_camera = workspace->set_camera(final_camera_parameters);
  scene::StereoParameters final_stereo;
  final_stereo.enabled = true;
  final_stereo.mode = scene::StereoMode::checkerboard;
  final_stereo.swap_eyes = true;
  final_stereo.shift_percent = 3.0;
  final_stereo.anaglyph_mode = scene::AnaglyphMode::gray;
  const auto configured_stereo = workspace->set_stereo(final_stereo);
  const auto stored_view = workspace->store_named_view("not a full scene");

  application::SessionDocument camera_journal;
  camera_journal.generator_version = std::string{version()};
  const auto finalized = application::finalize_session_camera_snapshot(
      camera_journal, *workspace);
  const auto finalized_twice =
      finalized.has_value()
          ? application::finalize_session_camera_snapshot(finalized.value(),
                                                          *workspace)
          : operation::Result<application::SessionDocument>::failure(
                finalized.error());
  const auto camera_text =
      application::serialize_session(camera_journal, *workspace);
  const auto camera_parsed =
      camera_text.has_value()
          ? application::parse_session(camera_text.value())
          : operation::Result<application::SessionDocument>::failure(
                camera_text.error());
  auto camera_workspace = std::make_shared<application::Workspace>();
  auto camera_registry =
      application::make_default_registry(camera_workspace);
  const application::Dispatcher camera_dispatcher{camera_registry};
  operation::TaskContext camera_context;
  const auto camera_replay =
      camera_parsed.has_value()
          ? application::replay_session(camera_parsed.value(),
                                        camera_dispatcher, camera_context)
          : operation::Result<application::SessionReplayResult>::failure(
                camera_parsed.error());
  passed &= expect(
      final_camera.has_value() && configured_stereo.has_value() &&
          stored_view.has_value() &&
          finalized.has_value() && finalized_twice.has_value() &&
          finalized.value() == finalized_twice.value() &&
          finalized.value().invocations.size() == 2U &&
          finalized.value().invocations.front().canonical_name == "view set" &&
          finalized.value().invocations.back().canonical_name == "stereo set" &&
          finalized.value().invocations.front().arguments.size() == 17U &&
          camera_text.has_value() && camera_parsed.has_value() &&
          camera_replay.has_value() &&
          camera_replay.value().applied_count == 2U &&
          camera_workspace->camera().parameters() ==
              workspace->camera().parameters() &&
          camera_workspace->stereo() == workspace->stereo() &&
          camera_workspace->object_count() == 0U &&
          camera_workspace->measurements().empty() &&
          camera_workspace->list_named_views().empty(),
      "final session view snapshot must be idempotent and preserve camera/stereo state");

  auto replacement_parameters = workspace->camera().parameters();
  replacement_parameters.target.x = 12.0;
  const auto replacement_camera =
      workspace->set_camera(replacement_parameters);
  const auto replaced_snapshot =
      application::finalize_session_camera_snapshot(finalized.value(),
                                                    *workspace);
  passed &= expect(
      replacement_camera.has_value() && replaced_snapshot.has_value() &&
          replaced_snapshot.value().invocations.size() == 2U &&
          replaced_snapshot.value().invocations.front().arguments.at(
              "target-x") == "12",
      "recapture must replace a trailing complete snapshot with current camera");

  application::SessionDocument partial_camera_journal;
  partial_camera_journal.generator_version = std::string{version()};
  partial_camera_journal.invocations = {
      {"view set", {{"target-x", "1"}}}};
  const auto partial_finalized =
      application::finalize_session_camera_snapshot(partial_camera_journal,
                                                    *workspace);
  application::SessionDocument wrong_schema;
  wrong_schema.schema_version = 3U;
  wrong_schema.generator_version = std::string{version()};
  passed &= expect(
      partial_finalized.has_value() &&
          partial_finalized.value().invocations.size() == 3U &&
          !application::finalize_session_camera_snapshot(wrong_schema,
                                                         *workspace)
               .has_value(),
      "partial camera commands must be preserved and unsupported schema rejected");

  passed &= expect(
      !application::parse_session(
           "molshredder-session 3\ngenerator 0.1.0\n")
           .has_value() &&
          !application::parse_session(
               "molshredder-session 2\nmissing 0.1.0\n")
               .has_value() &&
          !application::parse_session(
               "molshredder-session 2\ngenerator 0.1.0\nnot-invoke\n")
               .has_value(),
      "unknown schema and malformed session lines must fail honestly");

  const auto migrated = application::parse_session(
      "molshredder-session 1\ngenerator 0.1.0\ninvoke \"version\"\n");
  const auto migrated_text =
      migrated.has_value() ? application::serialize_session(migrated.value())
                           : operation::Result<std::string>::failure(
                                 migrated.error());
  passed &= expect(
      migrated.has_value() && migrated.value().schema_version == 2U &&
          migrated.value().source_schema_version == 1U &&
          migrated.value().migration_notes.size() == 1U &&
          migrated_text.has_value() &&
          migrated_text.value().starts_with("molshredder-session 2\n"),
      "schema 1 must migrate deterministically to schema 2");
  passed &= expect(
      !application::parse_session(
           "molshredder-session 2\ngenerator 0.1.0\nmetadata invoke \"session metadata\" --key \"x\" --value \"1\"\nmetadata invoke \"session metadata\" --key \"x\" --value \"2\"\n")
           .has_value(),
      "duplicate schema 2 metadata must fail closed");

  const auto temporary_root = std::filesystem::temp_directory_path() /
      ("molshredder-session-v2-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  std::error_code temporary_error;
  std::filesystem::create_directory(temporary_root, temporary_error);
  const auto recovery_path = temporary_root / "recovery.msess";
  const auto primary_path = temporary_root / "primary.msess";
  const auto written = temporary_error
                           ? operation::Result<std::size_t>::failure(
                                 {operation::ErrorCode::internal,
                                  temporary_error.message(), {}})
                           : application::write_session_file_atomic(
                                 recovery_path, document);
  {
    std::ofstream truncated{primary_path, std::ios::binary};
    truncated << "molshredder-session 2\ngenerator";
  }
  const auto recovered = application::read_session_file_with_recovery(
      primary_path, recovery_path);
  const auto replaced =
      application::write_session_file_atomic(recovery_path, document);
  const auto replacement_read =
      application::read_session_file_with_recovery(recovery_path);
  passed &= expect(
      written.has_value() && recovered.has_value() &&
          recovered.value().recovered &&
          recovered.value().source_path == recovery_path &&
          recovered.value().document == document &&
          !recovered.value().primary_error.empty() &&
          replaced.has_value() && replacement_read.has_value() &&
          replacement_read.value().document == document,
      "failure-atomic replacement and explicit corrupt-primary recovery drifted");

  application::SessionDocument relink_document;
  relink_document.generator_version = std::string{version()};
  relink_document.invocations = {
      {"load", {{"file-format", "pdb"},
                {"name", "relinked"},
                {"path", "missing/input.pdb"}}}};
  const auto unresolved = application::relink_session_paths(
      relink_document, {}, temporary_root);
  const auto relinked = application::relink_session_paths(
      relink_document, {{"missing/input.pdb", fixture}}, temporary_root);
  passed &= expect(
      unresolved.has_value() && unresolved.value().relinked_count == 0U &&
          unresolved.value().unresolved_paths ==
              std::vector<std::string>{"missing/input.pdb"} &&
          relinked.has_value() && relinked.value().relinked_count == 1U &&
          relinked.value().unresolved_paths.empty() &&
          std::filesystem::path{
              relinked.value().document.invocations[0].arguments.at("path")} ==
              std::filesystem::canonical(fixture),
      "session relink must replace only explicit missing-path mappings");
  std::filesystem::remove_all(temporary_root, temporary_error);

  application::SessionDocument lifecycle_journal;
  lifecycle_journal.generator_version = std::string{version()};
  lifecycle_journal.invocations = {
      {"load", {{"file-format", "pdb"}, {"name", "alpha"},
                {"path", fixture.string()}}},
      {"load", {{"file-format", "pdb"}, {"name", "beta"},
                {"path", fixture.string()}}},
      {"load", {{"file-format", "pdb"}, {"name", "gamma"},
                {"path", fixture.string()}}},
      {"object visibility", {{"id", "2"}, {"visible", "false"}}},
      {"object activate", {{"id", "1"}}},
      {"object rename", {{"name", "delta"}, {"object", "2"}}},
      {"object reorder", {{"object", "3"}, {"position", "1"}}},
      {"object delete", {{"object", "current"}}},
      {"object topology-retain",
       {{"atom-ids", "3,1"}, {"expected-version", "1"}}}};
  const auto lifecycle_text =
      application::serialize_session(lifecycle_journal);
  const auto lifecycle_parsed =
      lifecycle_text.has_value()
          ? application::parse_session(lifecycle_text.value())
          : operation::Result<application::SessionDocument>::failure(
                lifecycle_text.error());
  auto lifecycle_workspace = std::make_shared<application::Workspace>();
  const auto lifecycle_registry =
      application::make_default_registry(lifecycle_workspace);
  const application::Dispatcher lifecycle_dispatcher{lifecycle_registry};
  operation::TaskContext lifecycle_context;
  const auto lifecycle_replay = lifecycle_parsed.has_value()
                                    ? application::replay_session(
                                          lifecycle_parsed.value(),
                                          lifecycle_dispatcher,
                                          lifecycle_context)
                                    : operation::Result<
                                          application::SessionReplayResult>::
                                          failure(lifecycle_parsed.error());
  const auto lifecycle_objects = lifecycle_workspace->list_objects();
  passed &= expect(
      lifecycle_replay.has_value() &&
          lifecycle_replay.value().applied_count == 9U &&
          lifecycle_objects.size() == 2U && lifecycle_objects[0].id == 3U &&
          lifecycle_objects[0].name == "gamma" &&
          lifecycle_objects[1].id == 2U &&
          lifecycle_objects[1].name == "delta" &&
          lifecycle_objects[1].atom_count == 2U &&
          lifecycle_workspace->objects()[1].system->topology()->version() == 2U &&
          lifecycle_objects[1].active && !lifecycle_objects[1].visible &&
          lifecycle_workspace->scene()->selection().contains(
              lifecycle_workspace->objects()[1].scene_node),
      "session replay must preserve object rename/delete/reorder order, active identity and visibility");

  application::SessionDocument editing_journal;
  editing_journal.generator_version = std::string{version()};
  editing_journal.invocations = {
      {"build molecule",
       {{"name", "session-carbonyl"},
        {"atoms", "C,6,0,0,0,0;O,8,1.2,0,0,0"},
        {"bonds", "1,2,double"}, {"residue-name", "LIG"},
        {"chain", "A"}, {"residue-number", "1"},
        {"unit", "angstrom"}, {"memory-budget-bytes", "1048576"}}},
      {"edit atom-properties",
       {{"atom-id", "1"}, {"name", "C1"}, {"formal-charge", "1"},
        {"expected-topology-version", "1"},
        {"expected-coordinate-source-revision", "1"}}},
      {"edit residue-properties",
       {{"atom-id", "1"}, {"name", "CRB"}, {"chain", "B"},
        {"residue-number", "7"}, {"expected-topology-version", "2"},
        {"expected-coordinate-source-revision", "2"}}},
      {"edit bond-order",
       {{"bond-id", "1"}, {"order", "single"},
        {"expected-topology-version", "3"},
        {"expected-coordinate-source-revision", "3"}}},
      {"edit undo", {}},
      {"edit redo", {}}};
  const auto editing_text = application::serialize_session(editing_journal);
  const auto editing_parsed =
      editing_text.has_value()
          ? application::parse_session(editing_text.value())
          : operation::Result<application::SessionDocument>::failure(
                editing_text.error());
  auto editing_workspace = std::make_shared<application::Workspace>();
  const auto editing_registry =
      application::make_default_registry(editing_workspace);
  const application::Dispatcher editing_dispatcher{editing_registry};
  operation::TaskContext editing_context;
  const auto editing_replay =
      editing_parsed.has_value()
          ? application::replay_session(editing_parsed.value(),
                                        editing_dispatcher, editing_context)
          : operation::Result<application::SessionReplayResult>::failure(
                editing_parsed.error());
  const auto *editing_object = editing_workspace->active_object();
  passed &= expect(
      editing_text.has_value() && editing_parsed.has_value() &&
          editing_replay.has_value() &&
          editing_replay.value().applied_count == 6U &&
          editing_object != nullptr && editing_workspace->object_count() == 1U &&
          editing_object->coordinate_source_revision == 6U &&
          editing_object->system->topology()->version() == 4U &&
          editing_object->system->topology()->atoms()[0].name == "C1" &&
          editing_object->system->topology()->atoms()[0].formal_charge == 1 &&
          editing_object->system->topology()->residues()[0].name == "CRB" &&
          editing_object->system->topology()->residues()[0].chain_id == "B" &&
          editing_object->system->topology()->residues()[0].sequence_number ==
              7 &&
          editing_object->system->topology()->bonds()[0].order ==
              model::BondOrder::single &&
          editing_workspace->edit_history_status().undo_count == 4U &&
          editing_workspace->edit_history_status().redo_count == 0U,
      "session journal must replay builder and atom/residue/bond edit transactions with bounded undo history");

  application::SessionDocument failing;
  failing.generator_version = std::string{version()};
  failing.invocations = {{"show", {{"representation", "lines"},
                                    {"selection", "all"}}}};
  auto empty_workspace = std::make_shared<application::Workspace>();
  auto empty_registry = application::make_default_registry(empty_workspace);
  const application::Dispatcher empty_dispatcher{empty_registry};
  operation::TaskContext failure_context;
  const auto failed = application::replay_session(
      failing, empty_dispatcher, failure_context);
  passed &= expect(!failed.has_value() &&
                       failed.error().message.starts_with(
                           "session command 1 failed:"),
                   "replay failure must identify the failing command");

  operation::TaskContext seed_context;
  const auto seeded = empty_dispatcher.dispatch(
      {"load", {{"file-format", "pdb"},
                {"name", "preserved"},
                {"path", fixture.string()}}},
      seed_context);
  application::SessionDocument atomic_failure;
  atomic_failure.generator_version = std::string{version()};
  atomic_failure.invocations = {
      {"load", {{"file-format", "pdb"},
                {"name", "candidate"},
                {"path", fixture.string()}}},
      {"show", {{"representation", "not-a-representation"},
                {"selection", "all"}}}};
  operation::TaskContext atomic_failure_context;
  const auto atomic_failed = application::replay_session_atomically(
      atomic_failure, empty_workspace, atomic_failure_context);
  passed &= expect(
      seeded.succeeded() && !atomic_failed.has_value() &&
          empty_workspace->object_count() == 1U &&
          empty_workspace->active_object()->system->name() == "preserved",
      "atomic replay failure must preserve the caller Workspace");

  auto atomic_workspace = std::make_shared<application::Workspace>();
  operation::TaskContext atomic_success_context;
  const auto atomic_success = application::replay_session_atomically(
      document, atomic_workspace, atomic_success_context);
  passed &= expect(
      atomic_success.has_value() && atomic_workspace->object_count() == 1U &&
          atomic_workspace->active_object()->system->name() ==
              "session_object",
      "atomic replay success must publish the complete candidate Workspace");

  operation::TaskContext cancelled_context;
  cancelled_context.cancellation.request_cancel();
  const auto cancelled = application::replay_session(
      document, empty_dispatcher, cancelled_context);
  passed &= expect(!cancelled.has_value() &&
                       cancelled.error().code ==
                           operation::ErrorCode::cancelled &&
                       empty_workspace->object_count() == 1U &&
                       empty_workspace->active_object()->system->name() ==
                           "preserved",
                   "pre-cancelled replay must not mutate the Workspace");

  return passed ? 0 : 1;
}
