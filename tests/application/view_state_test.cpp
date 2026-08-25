#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/session.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/operation/task_context.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

} // namespace

int main() {
  using namespace molshredder;
  bool passed = true;

  auto workspace = std::make_shared<application::Workspace>();
  auto registry = application::make_default_registry(workspace);
  const application::Dispatcher dispatcher{registry};
  operation::TaskContext context;
  const auto dispatch = [&dispatcher, &context](
                            std::string name,
                            command::Arguments arguments = {}) {
    return dispatcher.dispatch(
        command::Invocation{std::move(name), std::move(arguments)}, context);
  };

  const auto updated = dispatch(
      "view set", {{"target-x", "3"}, {"target-y", "-2"},
                   {"target-z", "1"}, {"distance", "25"},
                   {"projection", "orthographic"},
                   {"orthographic-height", "50"}, {"near-clip", "0.5"},
                   {"far-clip", "500"}});
  const auto stored = dispatch("view store", {{"name", "front detail"}});
  passed &= expect(updated.succeeded() && stored.succeeded() &&
                       workspace->camera().parameters().target ==
                           model::Vec3d{3.0, -2.0, 1.0} &&
                       workspace->camera().parameters().projection ==
                           scene::ProjectionMode::orthographic &&
                       workspace->list_named_views().size() == 1U,
                   "typed view set/store must update shared Workspace state");

  passed &= expect(dispatch("view set", {{"target-x", "-7"}}).succeeded() &&
                       dispatch("view store", {{"name", "alternate"}})
                           .succeeded() &&
                       dispatch("view recall", {{"name", "front detail"}})
                           .succeeded() &&
                       workspace->camera().parameters().target.x == 3.0 &&
                       workspace->list_named_views().front().name ==
                           "alternate",
                   "named views must list deterministically and recall exactly");

  const auto changed_for_replace =
      dispatch("view set", {{"target-x", "9"}});
  const auto replaced =
      dispatch("view store", {{"name", "front detail"}});
  const auto listed = dispatch("view list");
  const auto *replaced_response =
      replaced.succeeded()
          ? &std::get<command::Response>(replaced.envelope.payload)
          : nullptr;
  const auto *listed_response =
      listed.succeeded()
          ? &std::get<command::Response>(listed.envelope.payload)
          : nullptr;
  passed &= expect(
      changed_for_replace.succeeded() && replaced_response != nullptr &&
          listed_response != nullptr &&
          std::get<bool>(replaced_response->fields.at("replaced").data) &&
          std::get<std::uint64_t>(listed_response->fields.at("count").data) ==
              2U &&
          dispatch("view recall", {{"name", "front detail"}}).succeeded() &&
          workspace->camera().parameters().target.x == 9.0,
      "same-name store must replace atomically without growing inventory");

  const auto before_invalid = workspace->camera().parameters();
  const auto invalid = dispatch(
      "view set", {{"orientation-w", "0"}, {"orientation-x", "0"},
                   {"orientation-y", "0"}, {"orientation-z", "0"}});
  const auto missing =
      dispatch("view recall", {{"name", "does not exist"}});
  passed &= expect(!invalid.succeeded() && !missing.succeeded() &&
                       workspace->camera().parameters() == before_invalid &&
                       std::get<operation::Error>(missing.envelope.payload)
                               .code == operation::ErrorCode::not_found,
                   "invalid or missing view operations must preserve state");

  const std::string pymol_values =
      "0,1,0,-1,0,0,0,0,1,2,-3,-40,10,20,30,.5,100,-60";
  const auto imported_pymol =
      dispatch("view import-pymol",
               {{"duration", "0.4"}, {"hand", "-1"},
                {"values", pymol_values}});
  const auto exported_pymol = dispatch("view export-pymol");
  const auto *export_response =
      exported_pymol.succeeded()
          ? &std::get<command::Response>(exported_pymol.envelope.payload)
          : nullptr;
  const auto *import_response =
      imported_pymol.succeeded()
          ? &std::get<command::Response>(imported_pymol.envelope.payload)
          : nullptr;
  const auto *animation =
      import_response == nullptr
          ? nullptr
          : &std::get<command::Value::Object>(
                import_response->fields.at("animation").data);
  passed &= expect(
      imported_pymol.succeeded() && import_response != nullptr &&
          export_response != nullptr && animation != nullptr &&
          workspace->camera().parameters().target ==
              model::Vec3d{13.0, 22.0, 30.0} &&
          workspace->camera().parameters().model_origin ==
              model::Vec3d{10.0, 20.0, 30.0} &&
          std::get<std::string>(export_response->fields.at("layout").data) ==
              "pymol-get-view-18" &&
          std::get<command::Value::Array>(
              export_response->fields.at("values").data)
                  .size() == 18U &&
          std::get<bool>(animation->at("active").data) &&
          std::get<bool>(animation->at("committed_endpoint").data) &&
          std::get<double>(animation->at("duration_seconds").data) == 0.4 &&
          std::get<std::int64_t>(animation->at("hand").data) == -1,
      "PyMOL import/export must expose a typed endpoint animation contract");

  const auto before_bad_pymol = workspace->camera().parameters();
  const auto bad_pymol = dispatch(
      "view import-pymol",
      {{"values", "2,0,0,0,1,0,0,0,1,0,0,-10,0,0,0,.1,100,-45"}});
  const auto bad_duration = dispatch(
      "view import-pymol",
      {{"duration", "-0.1"}, {"values", pymol_values}});
  const auto bad_hand = dispatch(
      "view import-pymol", {{"hand", "2"}, {"values", pymol_values}});
  passed &= expect(!bad_pymol.succeeded() && !bad_duration.succeeded() &&
                       !bad_hand.succeeded() &&
                       workspace->camera().parameters() == before_bad_pymol,
                   "invalid PyMOL animation inputs must be failure-atomic");

  passed &= expect(dispatch("view delete", {{"name", "alternate"}})
                           .succeeded() &&
                       workspace->list_named_views().size() == 1U &&
                       dispatch("view clear").succeeded() &&
                       workspace->list_named_views().empty(),
                   "named views must support one-view delete and clear-all");

  application::SessionDocument document;
  document.generator_version = "0.1.0";
  document.invocations = {
      {"view set", {{"distance", "20"}, {"target-x", "5"}}},
      {"view store", {{"name", "session front"}}},
      {"view set", {{"target-x", "-2"}}},
      {"view recall", {{"name", "session front"}}}};
  const auto text = application::serialize_session(document);
  const auto parsed = text.has_value()
                          ? application::parse_session(text.value())
                          : operation::Result<application::SessionDocument>::
                                failure(text.error());
  auto replay_workspace = std::make_shared<application::Workspace>();
  auto replay_registry =
      application::make_default_registry(replay_workspace);
  const application::Dispatcher replay_dispatcher{replay_registry};
  operation::TaskContext replay_context;
  const auto replay = parsed.has_value()
                          ? application::replay_session(
                                parsed.value(), replay_dispatcher, replay_context)
                          : operation::Result<application::SessionReplayResult>::
                                failure(parsed.error());
  passed &= expect(text.has_value() && parsed.has_value() &&
                       replay.has_value() &&
                       replay.value().applied_count == 4U &&
                       replay_workspace->camera().parameters().target.x == 5.0 &&
                       replay_workspace->camera().parameters().distance == 20.0 &&
                       replay_workspace->list_named_views().size() == 1U &&
                       replay_workspace->list_named_views().front().name ==
                           "session front",
                   "session replay must restore camera and named-view state");

  application::SessionDocument navigation_document;
  navigation_document.generator_version = "0.1.0";
  navigation_document.invocations = {
      {"view set",
       {{"distance", "10"}, {"far-clip", "20"},
        {"model-origin-x", "0"}, {"model-origin-y", "0"},
        {"model-origin-z", "0"}, {"near-clip", "2"},
        {"target-x", "2"}, {"target-y", "0"}, {"target-z", "0"}}},
      {"view move", {{"axis", "x"}, {"distance", "3"}}},
      {"view turn", {{"angle", "90"}, {"axis", "z"}}},
      {"view projection", {{"mode", "orthographic"},
                            {"preserve-scale", "true"}}}};
  const auto navigation_text =
      application::serialize_session(navigation_document);
  const auto navigation_parsed =
      navigation_text.has_value()
          ? application::parse_session(navigation_text.value())
          : operation::Result<application::SessionDocument>::failure(
                navigation_text.error());
  auto navigation_workspace = std::make_shared<application::Workspace>();
  auto navigation_registry =
      application::make_default_registry(navigation_workspace);
  const application::Dispatcher navigation_dispatcher{navigation_registry};
  operation::TaskContext navigation_context;
  const auto navigation_replay =
      navigation_parsed.has_value()
          ? application::replay_session(navigation_parsed.value(),
                                        navigation_dispatcher,
                                        navigation_context)
          : operation::Result<application::SessionReplayResult>::failure(
                navigation_parsed.error());
  passed &= expect(
      navigation_text.has_value() && navigation_parsed.has_value() &&
          navigation_replay.has_value() &&
          navigation_replay.value().applied_count == 4U &&
          std::abs(navigation_workspace->camera().parameters().target.x) <
              1.0e-12 &&
          std::abs(navigation_workspace->camera().parameters().target.y +
                   1.0) < 1.0e-12 &&
          navigation_workspace->camera().parameters().model_origin ==
              model::Vec3d{} &&
          navigation_workspace->camera().parameters().projection ==
              scene::ProjectionMode::orthographic,
      "camera navigation/projection canonical commands must replay the same state");

  auto projection_workspace = std::make_shared<application::Workspace>();
  auto projection_registry =
      application::make_default_registry(projection_workspace);
  const application::Dispatcher projection_dispatcher{projection_registry};
  operation::TaskContext projection_context;
  const auto projection_dispatch =
      [&projection_dispatcher, &projection_context](
          std::string name, command::Arguments arguments = {}) {
        return projection_dispatcher.dispatch(
            command::Invocation{std::move(name), std::move(arguments)},
            projection_context);
      };
  const auto previous_span =
      projection_workspace->camera().vertical_span_at_target();
  const auto orthoscopic = projection_dispatch("orthoscopic");
  const auto perspective = projection_dispatch(
      "perspective", {{"field-of-view-degrees", "60"}});
  const auto before_invalid_projection =
      projection_workspace->camera().parameters();
  const auto invalid_projection = projection_dispatch(
      "view projection",
      {{"field-of-view-degrees", "180"}, {"mode", "perspective"}});
  passed &= expect(
      orthoscopic.succeeded() && perspective.succeeded() &&
          projection_workspace->camera().parameters().projection ==
              scene::ProjectionMode::perspective &&
          std::abs(projection_workspace->camera().vertical_span_at_target() -
                   previous_span) < 1.0e-10 &&
          std::abs(projection_workspace->camera()
                       .parameters()
                       .vertical_field_of_view_radians -
                   std::numbers::pi / 3.0) < 1.0e-12 &&
          !invalid_projection.succeeded() &&
          projection_workspace->camera().parameters() ==
              before_invalid_projection,
      "projection aliases must preserve scale and reject invalid degree FOV atomically");

  const auto stereo_set = projection_dispatch(
      "stereo set", {{"angle-scale", "2.1"}, {"enabled", "true"},
                     {"mode", "crosseye"}, {"shift-percent", "2.5"},
                     {"swap-eyes", "true"}});
  const auto stereo_get = projection_dispatch("stereo get");
  const auto stereo_modes = projection_dispatch("stereo modes");
  const auto anaglyph_stereo = projection_dispatch(
      "stereo set", {{"anaglyph-mode", "half_color"},
                     {"enabled", "true"},
                     {"mode", "anaglyph"}});
  const auto interleaved_stereo = projection_dispatch(
      "stereo set", {{"enabled", "true"},
                     {"mode", "row_interleaved"},
                     {"swap-eyes", "true"}});
  const auto before_invalid_stereo = projection_workspace->stereo();
  const auto invalid_stereo = projection_dispatch(
      "stereo set", {{"mode", "quad_buffer"}});
  passed &= expect(
      stereo_set.succeeded() && stereo_get.succeeded() &&
          stereo_modes.succeeded() && anaglyph_stereo.succeeded() &&
          interleaved_stereo.succeeded() &&
          !invalid_stereo.succeeded() &&
          projection_workspace->stereo() == before_invalid_stereo &&
          projection_workspace->stereo().enabled &&
          projection_workspace->stereo().mode ==
              scene::StereoMode::row_interleaved &&
          projection_workspace->stereo().anaglyph_mode ==
              scene::AnaglyphMode::optimized &&
          projection_workspace->stereo().swap_eyes &&
          std::abs(projection_workspace->stereo().shift_percent - 2.0) <
              1.0e-12,
      "stereo commands must share composite state and reject unsupported compositors atomically");

  return passed ? 0 : 1;
}
