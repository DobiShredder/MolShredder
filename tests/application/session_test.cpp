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
