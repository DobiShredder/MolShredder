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
  draft.provenance.scientific = {
      application::kScientificResultContractSchemaVersion,
      {model::kTopologyReferenceSchemaVersion, 1U, 3U},
      5U,
      7U,
      true,
      "current_frame",
      "weighted center",
      "molshredder-center-v1",
      "angstrom",
      "angstrom",
      "float64",
      6U,
      "not_applicable",
      false,
      "error",
      {1.0e-10, 1.0e-12, "angstrom"},
      true};
  draft.provenance.object_name = "protein";
  draft.provenance.canonical_command = "analyze center";
  draft.provenance.canonical_arguments = {{"mode", "com"},
                                          {"selection", "protein"}};
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
          added.value().schema_version ==
              application::kAnalysisResultSchemaVersion &&
          added.value().provenance.scientific.coordinate_revision == 7U &&
          added.value().provenance.created_at_utc ==
              "2026-08-25T12:34:56.789Z" &&
          added.value().overlay_visible &&
          std::get<std::uint64_t>(
              added.value().response.fields.at("result_id").data) == 1U,
      "result store must assign stable identity, timestamp and overlay state");
  const auto duplicate = store.add(center_draft());
  passed &= expect(!duplicate.has_value() && store.records().size() == 1U,
                   "duplicate name must preserve the store");
  auto invalid_tolerance = center_draft("invalid-tolerance");
  invalid_tolerance.provenance.scientific.tolerance.absolute = -1.0;
  passed &= expect(!store.add(std::move(invalid_tolerance)).has_value() &&
                       store.records().size() == 1U,
                   "invalid tolerance must fail atomically");
  auto invalid_pbc = center_draft("invalid-pbc");
  invalid_pbc.provenance.scientific.pbc_policy = "minimum-image";
  auto invalid_precision = center_draft("invalid-precision");
  invalid_precision.provenance.scientific.presentation_precision = 16U;
  auto invalid_range = center_draft("invalid-range");
  invalid_range.provenance.scientific.coordinate_scope = "trajectory_range";
  passed &= expect(
      !store.add(std::move(invalid_pbc)).has_value() &&
          !store.add(std::move(invalid_precision)).has_value() &&
          !store.add(std::move(invalid_range)).has_value() &&
          store.records().size() == 1U,
      "inconsistent PBC, precision and trajectory scope must fail atomically");
  const auto hidden = store.set_overlay_visible(1U, false);
  passed &= expect(hidden.has_value() && !hidden.value().overlay_visible,
                   "overlay visibility must be persistent state");

  const auto snapshot = store.snapshot();
  application::AnalysisResultStore restored;
  passed &= expect(!restored.restore(snapshot).has_value() &&
                       restored.records().size() == 1U &&
                       restored.records()[0].name == "center-one",
                   "typed session snapshot must round-trip result state");
  application::LegacyAnalysisResultStoreSnapshotV1 legacy_snapshot;
  legacy_snapshot.next_result_id = 2U;
  application::LegacyPersistentAnalysisResultV1 legacy_record;
  legacy_record.result_id = 1U;
  legacy_record.name = "legacy-center";
  legacy_record.kind = application::AnalysisResultKind::center;
  legacy_record.provenance = {
      {model::kTopologyReferenceSchemaVersion, 1U, 3U},
      "protein",
      "analyze center",
      {{"mode", "com"}, {"precision", "6"}},
      "weighted center",
      "molshredder-center-v1",
      "angstrom",
      "not_applicable",
      "error",
      std::nullopt,
      std::nullopt,
      std::nullopt,
      "2026-08-25T12:34:56.789Z"};
  legacy_record.response = snapshot.records[0].response;
  legacy_record.export_table = snapshot.records[0].export_table;
  legacy_record.overlay = snapshot.records[0].overlay;
  legacy_record.overlay_visible = snapshot.records[0].overlay_visible;
  legacy_snapshot.records.push_back(std::move(legacy_record));
  application::AnalysisResultStore migrated;
  passed &= expect(
      !migrated.restore(legacy_snapshot).has_value() &&
          migrated.records().size() == 1U &&
          migrated.records()[0].schema_version ==
              application::kAnalysisResultSchemaVersion &&
          !migrated.records()[0]
               .provenance.scientific.coordinate_revision_known &&
          !migrated.records()[0].provenance.scientific.tolerance_known &&
          application::assess_analysis_result(
              migrated.records()[0],
              model::TopologySnapshotReference{
                  model::kTopologyReferenceSchemaVersion, 1U, 3U},
              1U, 1U, "molshredder-center-v1") ==
              application::AnalysisSourceStatus::coordinate_changed,
      "schema v1 migration must preserve data and mark unknown revision/tolerance stale");
  auto malformed_legacy = legacy_snapshot;
  malformed_legacy.records[0].provenance.algorithm_version.clear();
  passed &= expect(migrated.restore(malformed_legacy).has_value() &&
                       migrated.records().size() == 1U &&
                       migrated.records()[0].name == "legacy-center",
                   "malformed legacy migration must preserve current store");
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
  auto angle_draft = center_draft("angle-one");
  angle_draft.kind = application::AnalysisResultKind::angle;
  angle_draft.provenance.canonical_command = "measure angle";
  angle_draft.provenance.scientific.algorithm =
      "stable atan2 angle between two vectors";
  angle_draft.provenance.scientific.algorithm_version =
      "molshredder-angle-v1";
  angle_draft.provenance.scientific.output_unit = "degree";
  angle_draft.provenance.scientific.pbc_policy = "raw";
  angle_draft.provenance.scientific.tolerance.unit = "degree";
  angle_draft.response = {"angle measured", {{"angle_degrees", 90.0}}};
  angle_draft.export_table.columns = {"angle_degrees", "unit"};
  angle_draft.export_table.rows = {{90.0, "degree"}};
  const auto topology = angle_draft.provenance.scientific.topology;
  angle_draft.overlay = application::GeometryAnalysisOverlay{
      {{topology, model::AtomId{1U}}, {topology, model::AtomId{2U}},
       {topology, model::AtomId{3U}}},
      {{1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}},
      "90 deg"};
  const auto angle_added = restored.add(angle_draft);
  const auto geometry_packet =
      application::build_analysis_overlay_packet(restored.records());
  auto invalid_geometry = angle_draft;
  invalid_geometry.name = "invalid-geometry";
  std::get<application::GeometryAnalysisOverlay>(invalid_geometry.overlay)
      .atoms[0]
      .atom_id = model::AtomId{};
  passed &= expect(
      angle_added.has_value() && restored.records().size() == 2U &&
          geometry_packet.lines.size() == 2U &&
          geometry_packet.spheres.size() == 4U &&
          geometry_packet.labels.size() == 2U &&
          !restored.add(std::move(invalid_geometry)).has_value() &&
          restored.records().size() == 2U,
      "geometry results must validate stable atom references and render a "
      "deterministic persistent overlay");
  auto malformed = snapshot;
  malformed.records[0].result_id = 0U;
  passed &= expect(restored.restore(malformed).has_value() &&
                       restored.records().size() == 2U,
                   "malformed snapshot must not replace valid state");

  const auto response = application::analysis_result_response(
      restored.records()[0], application::AnalysisSourceStatus::topology_changed);
  passed &= expect(
      std::get<std::string>(response.fields.at("source_status").data) ==
          "topology_changed" &&
          std::holds_alternative<command::Value::Object>(
              response.fields.at("original_fields").data),
      "result query must expose source status and original typed fields");
  const auto current_topology = model::TopologySnapshotReference{
      model::kTopologyReferenceSchemaVersion, 1U, 3U};
  passed &= expect(
      application::assess_analysis_result(
          restored.records()[0], current_topology, 5U, 7U,
          "molshredder-center-v1") ==
              application::AnalysisSourceStatus::current &&
          application::assess_analysis_result(
              restored.records()[0], current_topology, 5U, 8U,
              "molshredder-center-v1") ==
              application::AnalysisSourceStatus::coordinate_changed &&
          application::assess_analysis_result(
              restored.records()[0], current_topology, 5U, 7U,
              "molshredder-center-v2") ==
              application::AnalysisSourceStatus::method_changed &&
          application::assess_analysis_result(
              restored.records()[0],
              model::TopologySnapshotReference{
                  model::kTopologyReferenceSchemaVersion, 1U, 4U},
              5U, 7U, "molshredder-center-v1") ==
              application::AnalysisSourceStatus::topology_changed,
      "stale assessment must distinguish coordinate, topology and method revisions");
  auto trajectory_record = restored.records()[0];
  trajectory_record.provenance.scientific.coordinate_scope =
      "trajectory_range";
  passed &= expect(
      application::assess_analysis_result(
          trajectory_record, current_topology, 5U, 99U,
          "molshredder-center-v1") ==
          application::AnalysisSourceStatus::current,
      "trajectory-range results must not stale on viewport-only frame changes");

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
                       json.value().losses.empty() &&
                       csv.value().losses.size() == 1U &&
                       json_text.find("created_at_utc") != std::string::npos &&
                       json_text.find("coordinate_revision") !=
                           std::string::npos &&
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
