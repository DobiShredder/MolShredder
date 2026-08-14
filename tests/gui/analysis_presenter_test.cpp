#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>
#include <variant>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
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
  bool passed = true;
  if (argc != 2) {
    std::cerr << "expected PDB fixture path\n";
    return 1;
  }
  auto workspace = std::make_shared<application::Workspace>();
  auto registry = application::make_default_registry(workspace);
  const application::Dispatcher dispatcher{registry};
  const gui::ActionAdapter actions{dispatcher};
  const auto fixture = std::filesystem::path{argv[1]};

  passed &= expect(
      trigger(actions, "load", {{"path", fixture.string()}}).succeeded(),
      "fixture load must succeed");
  const auto center = trigger(
      actions, "analyze center",
      {{"selection", "chain A"}, {"mode", "centroid"},
       {"precision", "3"}, {"unit", "nanometer"}});
  const auto center_view = gui::make_analysis_presentation(center);
  const auto* point =
      center_view.has_value() && center_view.value().marker.has_value()
          ? std::get_if<gui::PointMarker>(&center_view.value().marker.value())
          : nullptr;
  passed &= expect(
      point != nullptr && center_view.value().succeeded &&
          center_view.value().title == "Center analysis" &&
          point->position == model::Vec3d{0.15, 0.25, 0.35} &&
          point->coordinate_unit == operation::LengthUnit::nanometer &&
          point->label == "centroid (0.150, 0.250, 0.350) nm" &&
          center_view.value().text.find("centroid calculated") !=
              std::string::npos &&
          center_view.value().json.find("\"status\":\"ok\"") !=
              std::string::npos,
      "center presentation must expose result text, JSON and a point marker");

  const auto distance = trigger(
      actions, "measure distance",
      {{"from", "index 1"}, {"to", "index 2"}, {"mode", "atom"},
       {"pbc", "minimum-image"}, {"precision", "6"},
       {"unit", "nanometer"}});
  const auto distance_view = gui::make_analysis_presentation(distance);
  const auto* line =
      distance_view.has_value() && distance_view.value().marker.has_value()
          ? std::get_if<gui::AtomDistanceMarker>(
                &distance_view.value().marker.value())
          : nullptr;
  passed &= expect(
      line != nullptr && line->measurement_id == 1U && line->object_id == 1U &&
          line->first == model::AtomIndex{0U} &&
          line->second == model::AtomIndex{1U} &&
          line->label == "0.173205 nm" &&
          distance_view.value().fields.contains("distance") &&
          distance_view.value().fields.at("pbc") ==
              command::Value{"minimum-image"} &&
          workspace->measurements().size() == 1U,
      "distance presentation must expose persistent atom anchors and label");

  auto empty_registry = application::make_default_registry();
  const application::Dispatcher empty_dispatcher{empty_registry};
  const gui::ActionAdapter empty_actions{empty_dispatcher};
  const auto failed = trigger(empty_actions, "com", {{"selection", "all"}});
  const auto failed_view = gui::make_analysis_presentation(failed);
  passed &= expect(
      failed_view.has_value() && !failed_view.value().succeeded &&
          !failed_view.value().marker.has_value() &&
          failed_view.value().text.find("no active molecular object") !=
              std::string::npos &&
          failed_view.value().json.find("\"status\":\"error\"") !=
              std::string::npos,
      "failed analysis must remain presentable without creating a marker");

  const auto version = trigger(actions, "version");
  const auto unsupported = gui::make_analysis_presentation(version);
  passed &= expect(
      !unsupported.has_value() &&
          unsupported.error().code == operation::ErrorCode::unsupported,
      "non-analysis commands must use the generic result presenter");

  return passed ? 0 : 1;
}
