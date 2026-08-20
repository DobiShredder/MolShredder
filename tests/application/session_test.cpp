#include <filesystem>
#include <iostream>
#include <memory>
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
  document.invocations = {
      {"load", {{"file-format", "pdb"},
                {"name", "session_object"},
                {"path", fixture.string()}}},
      {"select", {{"expression", "chain A"},
                  {"name", "chain_a"},
                  {"update", "false"}}},
      {"show", {{"representation", "spheres"},
                {"selection", "@chain_a"}}},
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
                           "molshredder-session 1\ngenerator 0.1.0\n") &&
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
      replay.has_value() && replay.value().applied_count == 4U &&
          replay.value().outcomes.size() == 4U &&
          workspace->object_count() == 1U &&
          workspace->active_object()->representations.size() == 1U &&
          workspace->measurements().size() == 1U,
      "session replay must reconstruct foundation Workspace state");

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
  final_stereo.mode = scene::StereoMode::walleye;
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
  wrong_schema.schema_version = 2U;
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
           "molshredder-session 2\ngenerator 0.1.0\n")
           .has_value() &&
          !application::parse_session(
               "molshredder-session 1\nmissing 0.1.0\n")
               .has_value() &&
          !application::parse_session(
               "molshredder-session 1\ngenerator 0.1.0\nnot-invoke\n")
               .has_value(),
      "unknown schema and malformed session lines must fail honestly");

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

  operation::TaskContext cancelled_context;
  cancelled_context.cancellation.request_cancel();
  const auto cancelled = application::replay_session(
      document, empty_dispatcher, cancelled_context);
  passed &= expect(!cancelled.has_value() &&
                       cancelled.error().code ==
                           operation::ErrorCode::cancelled &&
                       empty_workspace->object_count() == 0U,
                   "pre-cancelled replay must not mutate the Workspace");

  return passed ? 0 : 1;
}
