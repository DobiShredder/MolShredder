#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/label_expression.hpp"
#include "molshredder/application/session.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/version.hpp"

// Expected labels: docs/research/pymol-label-measurement-blackbox_20261003T030000Z
// (Open-Source PyMOL 3.1.0, same 4-atom structure).

namespace {

using namespace molshredder;

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

constexpr std::string_view kStructure =
    "ATOM      1  N   ALA A   1       1.200   0.300   0.100  1.00 10.00           N\n"
    "ATOM      2  CA  ALA A   1       2.400   1.200   0.100  0.50 20.50           C\n"
    "ATOM      3  C   ALA A   1       3.600   2.700   0.100  0.25 30.25           C\n"
    "ATOM      4  O   GLY B   2       4.800   4.800   0.100  1.00 40.12           O\n"
    "END\n";

struct Harness {
  std::shared_ptr<application::Workspace> workspace =
      std::make_shared<application::Workspace>();
  command::Registry registry = application::make_default_registry(workspace);
  application::Dispatcher dispatcher{registry};
  operation::TaskContext context;

  application::DispatchOutcome run(std::string name,
                                   command::Arguments arguments) {
    return dispatcher.dispatch({std::move(name), std::move(arguments)},
                               context);
  }
};

std::vector<std::string> labels(const application::Workspace &workspace) {
  std::vector<std::string> result(4U);
  const auto *object = workspace.active_object();
  if (object == nullptr || object->atom_labels == nullptr)
    return result;
  const auto &topology = *object->system->topology();
  for (const auto &[id, text] : *object->atom_labels) {
    const auto index = topology.atom_index(model::AtomId{id});
    if (index.has_value() && index->value < result.size())
      result[index->value] = text;
  }
  return result;
}

std::size_t label_membership(const application::Workspace &workspace) {
  return workspace.active_object()
      ->representation_visibility.visible_count(render::RepresentationKind::labels)
      .value();
}

struct Case {
  std::string_view expression;
  bool succeeds;
  std::array<std::string_view, 4> expected;
  std::size_t membership;
};

struct DigitsCase {
  int digits;
  std::array<std::string_view, 4> expected;
};

} // namespace

int main() {
  bool passed = true;
  const auto directory =
      std::filesystem::temp_directory_path() / "molshredder-label-workspace-test";
  std::filesystem::create_directories(directory);
  const auto path = directory / "labels.pdb";
  {
    std::ofstream output{path};
    output << kStructure;
  }
  Harness harness;
  passed &= expect(harness.run("load", {{"file-format", "pdb"},
                                        {"name", "m"},
                                        {"path", path.string()}})
                       .succeeded(),
                   "label fixture must load");
  const auto reset = [&] {
    return harness.run("label", {{"expression", "''"}, {"selection", "all"}})
               .succeeded() &&
           harness.run("hide", {{"representation", "labels"}, {"selection", "all"}})
               .succeeded();
  };

  const std::vector<Case> cases{
      {"\"%s-%s\" % (resn, resi)", true, {"ALA-1", "ALA-1", "ALA-1", "GLY-2"}, 4},
      {"' '", true, {" ", " ", " ", " "}, 4},
      {"'%.1f' % b", true, {"10.0", "20.5", "30.2", "40.1"}, 4},
      {"''", true, {"", "", "", ""}, 4},
      {"'literal'", true, {"literal", "literal", "literal", "literal"}, 4},
      {"1.5", true, {"1.5", "1.5", "1.5", "1.5"}, 4},
      {"123", true, {"123", "123", "123", "123"}, 4},
      {"ID", true, {"1", "2", "3", "4"}, 4},
      {"None", true, {"None", "None", "None", "None"}, 4},
      {"alt", true, {"", "", "", ""}, 4},
      {"b", true, {"10.0", "20.5", "30.2", "40.1"}, 4},
      {"b * 2", true, {"20.0", "41.0", "60.5", "80.2"}, 4},
      {"chain", true, {"A", "A", "A", "B"}, 4},
      {"elem", true, {"N", "C", "C", "O"}, 4},
      {"formal_charge", true, {"0", "0", "0", "0"}, 4},
      {"index", true, {"1", "2", "3", "4"}, 4},
      {"model", true, {"m", "m", "m", "m"}, 4},
      {"name", true, {"N", "CA", "C", "O"}, 4},
      {"name + resi", true, {"N1", "CA1", "C1", "O2"}, 4},
      {"not_defined_name", false, {"", "", "", ""}, 0},
      {"partial_charge", true, {"0.0", "0.0", "0.0", "0.0"}, 4},
      {"q", true, {"1.0", "0.5", "0.2", "1.0"}, 4},
      {"q + 1", true, {"2.0", "1.5", "1.2", "2.0"}, 4},
      {"rank", true, {"0", "1", "2", "3"}, 4},
      {"resi", true, {"1", "1", "1", "2"}, 4},
      {"resn", true, {"ALA", "ALA", "ALA", "GLY"}, 4},
      {"resn+':'+resi", true, {"ALA:1", "ALA:1", "ALA:1", "GLY:2"}, 4},
      {"segi", true, {"", "", "", ""}, 4},
      {"ss", true, {"", "", "", ""}, 4},
      {"vdw", true, {"1.5", "1.7", "1.7", "1.5"}, 4},
      {"str(b)", true, {"10.0", "20.5", "30.25", "40.119998931884766"}, 4},
      {"text_type", true, {"", "", "", ""}, 4},
      {"type", true, {"ATOM", "ATOM", "ATOM", "ATOM"}, 4},
  };
  for (const auto &item : cases) {
    passed &= expect(reset(), "label reset must succeed");
    const auto outcome = harness.run(
        "label", {{"expression", std::string{item.expression}},
                  {"selection", "all"}});
    const auto actual = labels(*harness.workspace);
    bool same = outcome.succeeded() == item.succeeds &&
                label_membership(*harness.workspace) == item.membership;
    for (std::size_t index = 0; index < 4U; ++index)
      same = same && actual[index] == item.expected[index];
    passed &= expect(same, std::string{"label expression drifted: "} +
                               std::string{item.expression});
  }

  const std::vector<DigitsCase> digits{
      {0, {"10", "20", "30", "40"}},
      {1, {"10.0", "20.5", "30.2", "40.1"}},
      {2, {"10.00", "20.50", "30.25", "40.12"}},
      {4, {"10.0000", "20.5000", "30.2500", "40.1200"}},
  };
  for (const auto &item : digits) {
    passed &= expect(
        harness.run("setting set", {{"name", "label_digits"},
                                    {"scope", "object"},
                                    {"value", std::to_string(item.digits)}})
                .succeeded() &&
            harness.run("label", {{"expression", "b"}, {"selection", "all"}})
                .succeeded(),
        "label_digits update must succeed");
    const auto actual = labels(*harness.workspace);
    bool same = true;
    for (std::size_t index = 0; index < 4U; ++index)
      same = same && actual[index] == item.expected[index];
    passed &= expect(same, "label_digits formatting drifted: " +
                               std::to_string(item.digits));
  }
  harness.run("setting unset", {{"name", "label_digits"}, {"scope", "object"}});

  // Subset, clear, hide one and show all (capture: subset/clear/hide_one/show_all).
  reset();
  harness.run("label", {{"expression", "name"}, {"selection", "name CA"}});
  passed &= expect(labels(*harness.workspace) ==
                           std::vector<std::string>{"", "CA", "", ""} &&
                       label_membership(*harness.workspace) == 1U,
                   "subset labels must only touch the selection");
  harness.run("label", {{"expression", "''"}, {"selection", "name CA"}});
  passed &= expect(labels(*harness.workspace) == std::vector<std::string>(4U) &&
                       label_membership(*harness.workspace) == 1U,
                   "clearing keeps the labels representation shown");
  harness.run("label", {{"expression", "name"}, {"selection", "all"}});
  harness.run("hide", {{"representation", "labels"}, {"selection", "name N"}});
  passed &= expect(label_membership(*harness.workspace) == 3U &&
                       harness.workspace->active_object()
                               ->representations.back()
                               .packet.labels.size() == 3U,
                   "hidden atoms must not draw labels");
  harness.run("show", {{"representation", "labels"}, {"selection", "all"}});
  passed &= expect(label_membership(*harness.workspace) == 4U,
                   "show labels must restore membership");

  // label literal (PyMOL label2) stores text verbatim.
  for (const std::string literal : {"name", "hello world", "%s"}) {
    harness.run("label literal", {{"selection", "index 1"}, {"text", literal}});
    passed &= expect(labels(*harness.workspace)[0] == literal,
                     "literal labels must not be evaluated: " + literal);
  }

  // Label packet carries settings: color (front), size, font and offset.
  harness.run("background set", {{"color", "white"}});
  harness.run("setting set", {{"name", "label_size"}, {"value", "20"}});
  harness.run("setting set",
              {{"name", "label_position"}, {"value", "[1, 2, 3]"}});
  const auto *object = harness.workspace->active_object();
  const render::TextLabelInstance *first = nullptr;
  for (const auto &record : object->representations)
    if (record.kind == render::RepresentationKind::labels &&
        !record.packet.labels.empty())
      first = &record.packet.labels.front();
  passed &= expect(first != nullptr && first->color.red == 0.0F &&
                       first->size == 20.0 && first->font_id == 5 &&
                       first->camera_offset.x == 1.0 &&
                       first->camera_offset.z == 3.0,
                   "label packets must resolve label settings");

  const auto document = application::capture_session_document(
      *harness.workspace, std::string{version()});
  Harness restored;
  const auto replay =
      document.has_value()
          ? application::replay_session(document.value(), restored.dispatcher,
                                        restored.context)
          : operation::Result<application::SessionReplayResult>::failure(
                document.error());
  passed &= expect(replay.has_value() &&
                       labels(*restored.workspace) == labels(*harness.workspace),
                   "session replay must restore labels");
  passed &= expect(application::python_float_repr(40.119998931884766) ==
                           "40.119998931884766" &&
                       application::python_float_repr(20.0) == "20.0" &&
                       application::python_float_repr(1e16) == "1e+16" &&
                       application::python_float_repr(1.5e-5) == "1.5e-05" &&
                       application::python_float_repr(100000.0) == "100000.0" &&
                       application::python_float_repr(0.0001) == "0.0001",
                   "Python float repr must match");
  std::filesystem::remove_all(directory);
  return passed ? 0 : 1;
}
