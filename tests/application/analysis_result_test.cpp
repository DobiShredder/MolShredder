#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "molshredder/application/analysis_result.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

molshredder::application::AnalysisResultDraft center_draft(
    std::string name = "center-one") {
  using namespace molshredder;
  application::AnalysisResultDraft draft;
  draft.name = std::move(name);
  draft.kind = application::AnalysisResultKind::center;
  draft.provenance.source = {model::kTopologyReferenceSchemaVersion, 1U, 3U};
  draft.provenance.object_name = "protein";
  draft.provenance.canonical_command = "analyze center";
  draft.provenance.canonical_arguments = {{"mode", "com"},
                                          {"selection", "protein"}};
  draft.provenance.algorithm = "weighted center";
  draft.provenance.algorithm_version = "molshredder-center-v1";
  draft.provenance.coordinate_unit = "angstrom";
  draft.provenance.pbc_policy = "not_applicable";
  draft.provenance.missing_data_policy = "error";
  draft.response = {"com calculated",
                    {{"position", command::Value::Array{1.0, 2.0, 3.0}},
                     {"unit", "angstrom"}}};
  draft.export_table.columns = {"component", "value", "unit"};
  draft.export_table.rows = {{"x", 1.0, "angstrom"},
                             {"y", 2.0, "angstrom"},
                             {"z", 3.0, "angstrom"}};
  draft.overlay = application::PointAnalysisOverlay{
      {1.0, 2.0, 3.0}, "COM (1.000, 2.000, 3.000) A"};
  return draft;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace molshredder;
  bool passed = true;
  if (argc != 2) {
    std::cerr << "analysis result test requires an output directory\n";
    return 1;
  }
  const std::filesystem::path output_directory{argv[1]};
  const auto json_path = output_directory / "analysis-result-test.json";
  const auto csv_path = output_directory / "analysis-result-test.csv";
  std::error_code ignored;
  std::filesystem::remove(json_path, ignored);
  std::filesystem::remove(csv_path, ignored);

  application::AnalysisResultStore store{
      [] { return "2026-08-25T12:34:56.789Z"; }};
  const auto added = store.add(center_draft());
  passed &= expect(
      added.has_value() && added.value().result_id == 1U &&
          added.value().name == "center-one" &&
          added.value().provenance.created_at_utc ==
              "2026-08-25T12:34:56.789Z" &&
          added.value().overlay_visible &&
          std::get<std::uint64_t>(
              added.value().response.fields.at("result_id").data) == 1U,
      "result store must assign stable identity, timestamp and overlay state");
  const auto duplicate = store.add(center_draft());
  passed &= expect(!duplicate.has_value() && store.records().size() == 1U,
                   "duplicate name must preserve the store");
  const auto hidden = store.set_overlay_visible(1U, false);
  passed &= expect(hidden.has_value() && !hidden.value().overlay_visible,
                   "overlay visibility must be persistent state");

  const auto snapshot = store.snapshot();
  application::AnalysisResultStore restored;
  passed &= expect(!restored.restore(snapshot).has_value() &&
                       restored.records().size() == 1U &&
                       restored.records()[0].name == "center-one",
                   "typed session snapshot must round-trip result state");
  const auto hidden_packet =
      application::build_analysis_overlay_packet(restored.records());
  passed &= expect(hidden_packet.lines.empty() && hidden_packet.spheres.empty() &&
                       hidden_packet.labels.empty(),
                   "hidden analysis overlays must not enter the render packet");
  const auto shown = restored.set_overlay_visible(1U, true);
  const auto shown_packet =
      application::build_analysis_overlay_packet(restored.records());
  passed &= expect(shown.has_value() && shown_packet.lines.empty() &&
                       shown_packet.spheres.size() == 1U &&
                       shown_packet.labels.size() == 1U &&
                       shown_packet.spheres[0].pick_id == 0U &&
                       shown_packet.labels[0].pick_id == 0U &&
                       !shown_packet.bounds.empty &&
                       shown_packet.provenance.at("analysis_overlay") ==
                           "persistent-result-v1",
                   "point analysis overlay packet must be deterministic and "
                   "separate from molecular picking");
  auto malformed = snapshot;
  malformed.records[0].result_id = 0U;
  passed &= expect(restored.restore(malformed).has_value() &&
                       restored.records().size() == 1U,
                   "malformed snapshot must not replace valid state");

  const auto response = application::analysis_result_response(
      restored.records()[0], application::AnalysisSourceStatus::topology_changed);
  passed &= expect(
      std::get<std::string>(response.fields.at("source_status").data) ==
          "topology_changed" &&
          std::holds_alternative<command::Value::Object>(
              response.fields.at("original_fields").data),
      "result query must expose source status and original typed fields");

  const auto json = application::export_analysis_result(
      restored.records()[0], application::AnalysisSourceStatus::current,
      json_path, operation::OutputFormat::json, false);
  const auto csv = application::export_analysis_result(
      restored.records()[0], application::AnalysisSourceStatus::current,
      csv_path, operation::OutputFormat::csv, false);
  std::ifstream json_input{json_path, std::ios::binary};
  std::ifstream csv_input{csv_path, std::ios::binary};
  const std::string json_text{std::istreambuf_iterator<char>{json_input}, {}};
  const std::string csv_text{std::istreambuf_iterator<char>{csv_input}, {}};
  passed &= expect(json.has_value() && csv.has_value() &&
                       json_text.find("created_at_utc") != std::string::npos &&
                       csv_text == "component,value,unit\r\n"
                                   "x,1,angstrom\r\n"
                                   "y,2,angstrom\r\n"
                                   "z,3,angstrom\r\n",
                   "JSON and scalar CSV export must preserve provenance/data");
  const auto collision = application::export_analysis_result(
      restored.records()[0], application::AnalysisSourceStatus::current,
      json_path, operation::OutputFormat::json, false);
  passed &= expect(!collision.has_value(),
                   "export collision must not overwrite an existing file");
  std::filesystem::remove(json_path, ignored);
  std::filesystem::remove(csv_path, ignored);
  return passed ? 0 : 1;
}
