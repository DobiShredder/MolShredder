#include "molshredder/application/analysis_result.hpp"
#include "molshredder/analysis/sasa.hpp"
#include "molshredder/analysis/rdf.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace molshredder::application {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return {operation::ErrorCode::invalid_argument, std::move(message),
          std::move(suggestion)};
}

std::string utc_now() {
  const auto now = std::chrono::system_clock::now();
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()) % 1000;
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
         << std::setw(3) << std::setfill('0') << milliseconds.count() << 'Z';
  return output.str();
}

bool valid_name(std::string_view name) {
  return !name.empty() &&
         std::none_of(name.begin(), name.end(), [](unsigned char value) {
           return value < 0x20U || value == 0x7fU;
         });
}

bool valid_utc_timestamp(std::string_view value) {
  if (value.size() != 24U || value[4] != '-' || value[7] != '-' ||
      value[10] != 'T' || value[13] != ':' || value[16] != ':' ||
      value[19] != '.' || value[23] != 'Z')
    return false;
  for (const auto index : {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U, 11U, 12U,
                           14U, 15U, 17U, 18U, 20U, 21U, 22U})
    if (value[index] < '0' || value[index] > '9') return false;
  return true;
}

bool finite(model::Vec3d value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

bool valid_scientific_policy(const ScientificResultContract& scientific) {
  const auto pbc_valid = scientific.pbc_policy == "raw" ||
                         scientific.pbc_policy == "minimum-image" ||
                         scientific.pbc_policy == "not_applicable";
  const auto pbc_requirement_valid =
      scientific.pbc_cell_required ==
      (scientific.pbc_policy == "minimum-image");
  const auto missing_valid = scientific.missing_data_policy == "error" ||
         scientific.missing_data_policy == "skip";
  return pbc_valid && pbc_requirement_valid && missing_valid &&
         scientific.calculation_precision == "float64" &&
         scientific.presentation_precision <= 15U;
}

bool valid_scientific_contract(const ScientificResultContract& scientific) {
  return scientific.schema_version == kScientificResultContractSchemaVersion &&
         scientific.topology.schema_version ==
             model::kTopologyReferenceSchemaVersion &&
         scientific.topology.object_id != 0U &&
         scientific.topology.topology_version != 0U &&
         (!scientific.coordinate_revision_known ||
          (scientific.coordinate_source_revision != 0U &&
           scientific.coordinate_revision != 0U)) &&
         (scientific.coordinate_scope == "current_frame" ||
          scientific.coordinate_scope == "trajectory_range") &&
         !scientific.algorithm.empty() &&
         !scientific.algorithm_version.empty() &&
         !scientific.input_coordinate_unit.empty() &&
         !scientific.output_unit.empty() &&
         !scientific.calculation_precision.empty() &&
         !scientific.pbc_policy.empty() &&
         !scientific.missing_data_policy.empty() &&
         (!scientific.tolerance_known ||
          (std::isfinite(scientific.tolerance.absolute) &&
           scientific.tolerance.absolute >= 0.0 &&
           std::isfinite(scientific.tolerance.relative) &&
           scientific.tolerance.relative >= 0.0 &&
           !scientific.tolerance.unit.empty())) &&
         valid_scientific_policy(scientific);
}

bool valid_provenance(const AnalysisResultProvenance& provenance,
                      bool require_time) {
  const auto& scientific = provenance.scientific;
  return valid_scientific_contract(scientific) &&
         !provenance.canonical_command.empty() &&
         (scientific.coordinate_scope != "trajectory_range" ||
          (provenance.frame_first.has_value() &&
           provenance.frame_last.has_value() &&
           provenance.frame_stride.has_value() &&
           *provenance.frame_first <= *provenance.frame_last &&
           *provenance.frame_stride > 0U)) &&
         (!require_time || valid_utc_timestamp(provenance.created_at_utc));
}

bool valid_overlay(const PersistentAnalysisResult& record) {
  if (const auto* point = std::get_if<PointAnalysisOverlay>(&record.overlay))
    return finite(point->position) && !point->label.empty();
  const auto* distance =
      std::get_if<DistanceAnalysisOverlay>(&record.overlay);
  if (distance != nullptr)
    return distance->first.snapshot.schema_version ==
             model::kTopologyReferenceSchemaVersion &&
         distance->second.snapshot.schema_version ==
             model::kTopologyReferenceSchemaVersion &&
         distance->first.snapshot.object_id ==
             record.provenance.scientific.topology.object_id &&
         distance->second.snapshot.object_id ==
             record.provenance.scientific.topology.object_id &&
         distance->first.snapshot.topology_version ==
             record.provenance.scientific.topology.topology_version &&
         distance->second.snapshot.topology_version ==
             record.provenance.scientific.topology.topology_version &&
         distance->first.atom_id.value != 0U &&
         distance->second.atom_id.value != 0U &&
         finite(distance->first_position) && finite(distance->second_position) &&
         !distance->label.empty();
  const auto* geometry =
      std::get_if<GeometryAnalysisOverlay>(&record.overlay);
  if (geometry == nullptr) return !record.overlay_visible;
  if ((geometry->atoms.size() != 3U && geometry->atoms.size() != 4U) ||
      geometry->positions.size() != geometry->atoms.size() ||
      geometry->label.empty() ||
      !std::ranges::all_of(geometry->positions, finite))
    return false;
  return std::ranges::all_of(geometry->atoms, [&](const auto& atom) {
    return atom.snapshot.schema_version ==
               model::kTopologyReferenceSchemaVersion &&
           atom.snapshot.object_id ==
               record.provenance.scientific.topology.object_id &&
           atom.snapshot.topology_version ==
               record.provenance.scientific.topology.topology_version &&
           atom.atom_id.value != 0U;
  });
}

command::Value optional_index(const std::optional<std::size_t>& value) {
  return value ? command::Value{static_cast<std::uint64_t>(*value)}
               : command::Value{nullptr};
}

command::Value arguments_value(const command::Arguments& arguments) {
  command::Value::Object result;
  for (const auto& [name, value] : arguments) result.emplace(name, value);
  return result;
}

command::Value overlay_value(const PersistentAnalysisResult& record) {
  if (const auto* point = std::get_if<PointAnalysisOverlay>(&record.overlay)) {
    return command::Value::Object{
        {"kind", "point"},
        {"label", point->label},
        {"position", command::Value::Array{point->position.x,
                                            point->position.y,
                                            point->position.z}}};
  }
  if (const auto* distance =
          std::get_if<DistanceAnalysisOverlay>(&record.overlay)) {
    return command::Value::Object{
        {"first_atom_id", distance->first.atom_id.value},
        {"first_position",
         command::Value::Array{distance->first_position.x,
                               distance->first_position.y,
                               distance->first_position.z}},
        {"kind", "distance"},
        {"label", distance->label},
        {"second_atom_id", distance->second.atom_id.value},
        {"second_position",
         command::Value::Array{distance->second_position.x,
                               distance->second_position.y,
                               distance->second_position.z}}};
  }
  if (const auto* geometry =
          std::get_if<GeometryAnalysisOverlay>(&record.overlay)) {
    command::Value::Array atom_ids;
    command::Value::Array positions;
    atom_ids.reserve(geometry->atoms.size());
    positions.reserve(geometry->positions.size());
    for (const auto& atom : geometry->atoms)
      atom_ids.emplace_back(atom.atom_id.value);
    for (const auto position : geometry->positions)
      positions.emplace_back(command::Value::Array{position.x, position.y,
                                                    position.z});
    return command::Value::Object{
        {"atom_ids", std::move(atom_ids)},
        {"kind", geometry->atoms.size() == 3U ? "angle" : "dihedral"},
        {"label", geometry->label},
        {"positions", std::move(positions)}};
  }
  return nullptr;
}

std::filesystem::path temporary_path(const std::filesystem::path& target) {
  static std::atomic_uint64_t counter{};
  for (std::size_t attempt = 0; attempt < 1024U; ++attempt) {
    auto candidate = target.parent_path() /
                     (target.filename().string() + ".molshredder.tmp." +
                      std::to_string(counter.fetch_add(1U)));
    std::error_code error;
    if (!std::filesystem::exists(candidate, error) && !error) return candidate;
  }
  return {};
}

bool replace_file(const std::filesystem::path& source,
                  const std::filesystem::path& target, bool overwrite) {
#ifdef _WIN32
  const auto flags = overwrite
                         ? MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
                         : MOVEFILE_WRITE_THROUGH;
  return MoveFileExW(source.c_str(), target.c_str(), flags) != 0;
#else
  if (overwrite) return std::rename(source.c_str(), target.c_str()) == 0;
  std::error_code error;
  std::filesystem::create_hard_link(source, target, error);
  if (error) return false;
  std::filesystem::remove(source, error);
  return true;
#endif
}

}  // namespace

AnalysisResultStore::AnalysisResultStore(TimestampProvider timestamp_provider)
    : timestamp_provider_{timestamp_provider ? std::move(timestamp_provider)
                                             : TimestampProvider{utc_now}} {}

std::optional<operation::Error> AnalysisResultStore::validate_name(
    const std::optional<std::string>& requested_name) const {
  if (!requested_name.has_value()) return std::nullopt;
  if (!valid_name(*requested_name))
    return invalid("analysis result name must be non-empty and contain no control characters");
  if (std::any_of(records_.begin(), records_.end(), [&](const auto& record) {
        return record.name == *requested_name;
      }))
    return invalid("analysis result name already exists: " + *requested_name,
                   "choose a unique result name");
  return std::nullopt;
}

operation::Result<PersistentAnalysisResult> AnalysisResultStore::add(
    AnalysisResultDraft draft) {
  if (!valid_provenance(draft.provenance, false)) {
    return operation::Result<PersistentAnalysisResult>::failure(
        invalid("analysis result provenance is incomplete"));
  }
  auto name = draft.name.value_or("result_" +
                                  std::to_string(next_result_id_));
  if (const auto error = validate_name(std::optional<std::string>{name});
      error.has_value())
    return operation::Result<PersistentAnalysisResult>::failure(*error);
  if (next_result_id_ == 0U) {
    return operation::Result<PersistentAnalysisResult>::failure(
        {operation::ErrorCode::resource_exhausted,
         "analysis result identifier space is exhausted", {}});
  }
  if (draft.provenance.created_at_utc.empty())
    draft.provenance.created_at_utc = timestamp_provider_();
  const auto overlay_visible =
      draft.overlay_visible &&
      !std::holds_alternative<std::monostate>(draft.overlay);
  PersistentAnalysisResult record{
      kAnalysisResultSchemaVersion, next_result_id_, std::move(name),
      draft.kind, std::move(draft.provenance), std::move(draft.response),
      std::move(draft.export_table), std::move(draft.overlay),
      overlay_visible};
  if (!valid_provenance(record.provenance, true) ||
      !valid_overlay(record))
    return operation::Result<PersistentAnalysisResult>::failure(
        invalid("analysis result provenance or overlay is invalid"));
  record.response.fields["result_id"] = record.result_id;
  record.response.fields["result_name"] = record.name;
  records_.push_back(record);
  next_result_id_ = next_result_id_ == std::numeric_limits<std::uint64_t>::max()
                        ? 0U
                        : next_result_id_ + 1U;
  return operation::Result<PersistentAnalysisResult>::success(
      std::move(record));
}

operation::Result<PersistentAnalysisResult> AnalysisResultStore::get(
    std::uint64_t result_id) const {
  const auto found = std::find_if(records_.begin(), records_.end(),
                                  [result_id](const auto& record) {
                                    return record.result_id == result_id;
                                  });
  if (found == records_.end()) {
    return operation::Result<PersistentAnalysisResult>::failure(
        {operation::ErrorCode::not_found,
         "analysis result does not exist: " + std::to_string(result_id), {}});
  }
  return operation::Result<PersistentAnalysisResult>::success(*found);
}

operation::Result<PersistentAnalysisResult> AnalysisResultStore::erase(
    std::uint64_t result_id) {
  const auto found = std::find_if(records_.begin(), records_.end(),
                                  [result_id](const auto& record) {
                                    return record.result_id == result_id;
                                  });
  if (found == records_.end()) return get(result_id);
  auto removed = *found;
  records_.erase(found);
  return operation::Result<PersistentAnalysisResult>::success(
      std::move(removed));
}

operation::Result<PersistentAnalysisResult>
AnalysisResultStore::set_overlay_visible(std::uint64_t result_id,
                                         bool visible) {
  const auto found = std::find_if(records_.begin(), records_.end(),
                                  [result_id](const auto& record) {
                                    return record.result_id == result_id;
                                  });
  if (found == records_.end()) return get(result_id);
  if (visible && std::holds_alternative<std::monostate>(found->overlay)) {
    return operation::Result<PersistentAnalysisResult>::failure(
        {operation::ErrorCode::unsupported,
         "analysis result has no viewport overlay", {}});
  }
  found->overlay_visible = visible;
  return operation::Result<PersistentAnalysisResult>::success(*found);
}

AnalysisResultStoreSnapshot AnalysisResultStore::snapshot() const {
  return {kAnalysisResultSchemaVersion, next_result_id_, records_};
}

std::optional<operation::Error> AnalysisResultStore::restore(
    const AnalysisResultStoreSnapshot& snapshot) {
  if (snapshot.schema_version != kAnalysisResultSchemaVersion ||
      snapshot.next_result_id == 0U) {
    return invalid("analysis result snapshot schema or next ID is invalid");
  }
  std::uint64_t previous{};
  std::vector<std::string> names;
  names.reserve(snapshot.records.size());
  for (const auto& record : snapshot.records) {
    if (record.schema_version != kAnalysisResultSchemaVersion ||
        record.result_id == 0U || record.result_id <= previous ||
        record.result_id >= snapshot.next_result_id ||
        !valid_name(record.name) ||
        std::find(names.begin(), names.end(), record.name) != names.end() ||
        record.provenance.scientific.topology.object_id == 0U ||
        !valid_provenance(record.provenance, true) ||
        !valid_overlay(record)) {
      return invalid("analysis result snapshot contains an invalid record");
    }
    previous = record.result_id;
    names.push_back(record.name);
  }
  records_ = snapshot.records;
  next_result_id_ = snapshot.next_result_id;
  return std::nullopt;
}

std::optional<operation::Error> AnalysisResultStore::restore(
    const LegacyAnalysisResultStoreSnapshotV1& snapshot) {
  if (snapshot.schema_version != 1U || snapshot.next_result_id == 0U)
    return invalid("legacy analysis result snapshot schema or next ID is invalid");
  AnalysisResultStoreSnapshot migrated;
  migrated.next_result_id = snapshot.next_result_id;
  migrated.records.reserve(snapshot.records.size());
  for (const auto& legacy : snapshot.records) {
    const auto& source = legacy.provenance;
    if (legacy.schema_version != 1U || legacy.result_id == 0U ||
        source.source.schema_version !=
            model::kTopologyReferenceSchemaVersion ||
        source.source.object_id == 0U ||
        source.source.topology_version == 0U ||
        source.canonical_command.empty() || source.algorithm.empty() ||
        source.algorithm_version.empty() || source.coordinate_unit.empty() ||
        source.pbc_policy.empty() || source.missing_data_policy.empty() ||
        !valid_utc_timestamp(source.created_at_utc))
      return invalid("legacy analysis result snapshot contains an invalid record");
    unsigned int presentation_precision{};
    if (const auto found = legacy.response.fields.find("precision");
        found != legacy.response.fields.end()) {
      if (const auto* value = std::get_if<std::uint64_t>(&found->second.data);
          value != nullptr && *value <= 15U)
        presentation_precision = static_cast<unsigned int>(*value);
    }
    const auto trajectory_scope =
        legacy.kind == AnalysisResultKind::rmsd_series ||
        legacy.kind == AnalysisResultKind::rmsd_matrix ||
        legacy.kind == AnalysisResultKind::rmsf_series ||
        legacy.kind == AnalysisResultKind::contact_series;
    ScientificResultContract contract{
        kScientificResultContractSchemaVersion,
        source.source,
        0U,
        0U,
        false,
        trajectory_scope ? "trajectory_range" : "current_frame",
        source.algorithm,
        source.algorithm_version,
        source.coordinate_unit,
        source.coordinate_unit,
        "float64",
        presentation_precision,
        source.pbc_policy,
        source.pbc_policy == "minimum-image",
        source.missing_data_policy,
        {0.0, 0.0, {}},
        false};
    AnalysisResultProvenance provenance{
        std::move(contract),
        source.object_name,
        source.canonical_command,
        source.canonical_arguments,
        source.frame_first,
        source.frame_last,
        source.frame_stride,
        source.created_at_utc};
    migrated.records.push_back(
        {kAnalysisResultSchemaVersion, legacy.result_id, legacy.name,
         legacy.kind, std::move(provenance), legacy.response,
         legacy.export_table, legacy.overlay, legacy.overlay_visible});
  }
  return restore(migrated);
}

std::string_view to_string(AnalysisResultKind kind) noexcept {
  switch (kind) {
    case AnalysisResultKind::center: return "center";
    case AnalysisResultKind::distance: return "distance";
    case AnalysisResultKind::angle: return "angle";
    case AnalysisResultKind::dihedral: return "dihedral";
    case AnalysisResultKind::sasa: return "sasa";
    case AnalysisResultKind::rdf: return "rdf";
    case AnalysisResultKind::rmsd_matrix: return "rmsd_matrix";
    case AnalysisResultKind::rmsd_series: return "rmsd_series";
    case AnalysisResultKind::rmsf_series: return "rmsf_series";
    case AnalysisResultKind::contacts: return "contacts";
    case AnalysisResultKind::contact_series: return "contact_series";
  }
  return "center";
}

std::string_view to_string(AnalysisSourceStatus status) noexcept {
  switch (status) {
    case AnalysisSourceStatus::current: return "current";
    case AnalysisSourceStatus::coordinate_changed: return "coordinate_changed";
    case AnalysisSourceStatus::topology_changed: return "topology_changed";
    case AnalysisSourceStatus::method_changed: return "method_changed";
    case AnalysisSourceStatus::object_deleted: return "object_deleted";
  }
  return "object_deleted";
}

std::string_view current_analysis_algorithm_version(
    AnalysisResultKind kind) noexcept {
  switch (kind) {
    case AnalysisResultKind::center: return "molshredder-center-v1";
    case AnalysisResultKind::distance: return "molshredder-distance-v1";
    case AnalysisResultKind::angle: return "molshredder-angle-v1";
    case AnalysisResultKind::dihedral: return "molshredder-dihedral-v1";
    case AnalysisResultKind::sasa:
      return analysis::kSasaAlgorithmVersion;
    case AnalysisResultKind::rdf:
      return analysis::kRdfAlgorithmVersion;
    case AnalysisResultKind::rmsd_matrix:
      return "molshredder-rmsd-matrix-v1";
    case AnalysisResultKind::rmsd_series:
      return "molshredder-rmsd-series-v1";
    case AnalysisResultKind::rmsf_series:
      return "molshredder-rmsf-series-v1";
    case AnalysisResultKind::contacts: return "molshredder-contacts-v1";
    case AnalysisResultKind::contact_series:
      return "molshredder-contact-series-v1";
  }
  return {};
}

std::optional<command::Value::Object> analysis_plot_projection(
    AnalysisResultKind kind,const command::Table& table) {
  const auto column=[&](std::string_view name)->std::optional<std::size_t>{
    const auto found=std::find(table.columns.begin(),table.columns.end(),name);
    if(found==table.columns.end()) return std::nullopt;
    return static_cast<std::size_t>(std::distance(table.columns.begin(),found));
  };
  std::string plot_kind="line";
  std::string x_name; std::string y_name; std::string value_name;
  if(kind==AnalysisResultKind::rdf) { x_name="center"; value_name="value"; }
  else if(kind==AnalysisResultKind::rmsd_series) { x_name="frame"; value_name="rmsd"; }
  else if(kind==AnalysisResultKind::rmsf_series) { x_name="atom_index"; value_name="rmsf"; }
  else if(kind==AnalysisResultKind::rmsd_matrix) {
    plot_kind="heatmap"; x_name="first_frame"; y_name="second_frame"; value_name="rmsd";
  } else return std::nullopt;
  const auto x=column(x_name); const auto value=column(value_name);
  const auto y=y_name.empty()?std::optional<std::size_t>{}:column(y_name);
  if(!x.has_value()||!value.has_value()||(!y_name.empty()&&!y.has_value())) return std::nullopt;
  command::Value::Array samples;
  samples.reserve(table.rows.size());
  for(const auto& row:table.rows) {
    const auto maximum=std::max({*x,*value,y.value_or(0U)});
    if(row.size()<=maximum) return std::nullopt;
    command::Value::Array sample{row[*x]};
    if(y.has_value()) sample.push_back(row[*y]);
    sample.push_back(row[*value]);
    samples.emplace_back(std::move(sample));
  }
  command::Value::Object result{{"kind",plot_kind},{"schema_version",1U},
                                {"value_label",value_name},{"x_label",x_name},
                                {"samples",std::move(samples)}};
  if(!y_name.empty()) result.emplace("y_label",y_name);
  return result;
}

AnalysisSourceStatus assess_analysis_result(
    const PersistentAnalysisResult& record,
    const std::optional<model::TopologySnapshotReference>& current_topology,
    std::optional<std::uint64_t> current_coordinate_source_revision,
    std::optional<std::uint64_t> current_coordinate_revision,
    std::string_view current_algorithm_version) noexcept {
  if (!current_topology.has_value()) return AnalysisSourceStatus::object_deleted;
  const auto& contract = record.provenance.scientific;
  if (current_topology->object_id != contract.topology.object_id)
    return AnalysisSourceStatus::object_deleted;
  if (current_topology->topology_version != contract.topology.topology_version)
    return AnalysisSourceStatus::topology_changed;
  if (!contract.coordinate_revision_known)
    return AnalysisSourceStatus::coordinate_changed;
  if (!current_coordinate_source_revision.has_value() ||
      *current_coordinate_source_revision !=
          contract.coordinate_source_revision)
    return AnalysisSourceStatus::coordinate_changed;
  if (contract.coordinate_scope == "current_frame" &&
      (!current_coordinate_revision.has_value() ||
       *current_coordinate_revision != contract.coordinate_revision))
    return AnalysisSourceStatus::coordinate_changed;
  if (current_algorithm_version.empty() ||
      current_algorithm_version != contract.algorithm_version)
    return AnalysisSourceStatus::method_changed;
  return AnalysisSourceStatus::current;
}

operation::Result<command::Value::Object> scientific_contract_fields(
    const ScientificResultContract& scientific) {
  if (!valid_scientific_contract(scientific))
    return operation::Result<command::Value::Object>::failure(
        invalid("scientific result contract is incomplete or inconsistent"));
  return operation::Result<command::Value::Object>::success({
      {"absolute_tolerance", scientific.tolerance.absolute},
      {"algorithm", scientific.algorithm},
      {"algorithm_version", scientific.algorithm_version},
      {"calculation_precision", scientific.calculation_precision},
      {"contract_schema_version", scientific.schema_version},
      {"coordinate_revision", scientific.coordinate_revision},
      {"coordinate_revision_known", scientific.coordinate_revision_known},
      {"coordinate_scope", scientific.coordinate_scope},
      {"coordinate_source_revision",
       scientific.coordinate_source_revision},
      {"input_coordinate_unit", scientific.input_coordinate_unit},
      {"missing_data_policy", scientific.missing_data_policy},
      {"object_id", scientific.topology.object_id},
      {"output_unit", scientific.output_unit},
      {"pbc_cell_required", scientific.pbc_cell_required},
      {"pbc_policy", scientific.pbc_policy},
      {"presentation_precision",
       static_cast<std::uint64_t>(scientific.presentation_precision)},
      {"relative_tolerance", scientific.tolerance.relative},
      {"tolerance_unit", scientific.tolerance.unit},
      {"tolerance_known", scientific.tolerance_known},
      {"topology_version", scientific.topology.topology_version}});
}

command::Response analysis_result_response(
    const PersistentAnalysisResult& record, AnalysisSourceStatus status) {
  const auto& provenance = record.provenance;
  const auto& scientific = provenance.scientific;
  auto fields = scientific_contract_fields(scientific).value();
  fields.insert({
      {"canonical_arguments", arguments_value(provenance.canonical_arguments)},
      {"canonical_command", provenance.canonical_command},
      {"coordinate_unit", scientific.output_unit},
      {"created_at_utc", provenance.created_at_utc},
      {"frame_first", optional_index(provenance.frame_first)},
      {"frame_last", optional_index(provenance.frame_last)},
      {"frame_stride", optional_index(provenance.frame_stride)},
      {"kind", std::string{to_string(record.kind)}},
      {"missing_data_policy", scientific.missing_data_policy},
      {"name", record.name},
      {"object_name_snapshot", provenance.object_name},
      {"original_fields", record.response.fields},
      {"overlay", overlay_value(record)},
      {"overlay_visible", record.overlay_visible},
      {"result_id", record.result_id},
      {"schema_version", record.schema_version},
      {"source_status", std::string{to_string(status)}},
      {"topology_version", scientific.topology.topology_version}});
  if(const auto plot=analysis_plot_projection(record.kind,record.export_table);plot.has_value())
    fields.insert_or_assign("plot",*plot);
  return {"analysis result " + std::to_string(record.result_id),
          std::move(fields), record.response.table};
}

operation::Result<AnalysisResultExportReport> export_analysis_result(
    const PersistentAnalysisResult& record, AnalysisSourceStatus status,
    const std::filesystem::path& path, operation::OutputFormat format,
    bool overwrite) {
  if (format != operation::OutputFormat::json &&
      format != operation::OutputFormat::csv) {
    return operation::Result<AnalysisResultExportReport>::failure(
        invalid("analysis result export format must be json or csv"));
  }
  if (path.empty()) {
    return operation::Result<AnalysisResultExportReport>::failure(
        invalid("analysis result export path must not be empty"));
  }
  std::error_code exists_error;
  if (!overwrite && std::filesystem::exists(path, exists_error)) {
    return operation::Result<AnalysisResultExportReport>::failure(
        invalid("analysis result export target already exists: " +
                    path.string(),
                "set --overwrite true or choose a new path"));
  }
  auto response = analysis_result_response(record, status);
  if (format == operation::OutputFormat::csv)
    response.table = record.export_table;
  const command::ResultEnvelope envelope{
      command::kResultSchemaVersion,
      "invoke \"result export\" --id \"" +
          std::to_string(record.result_id) + "\"",
      std::move(response)};
  const auto serialized = command::render(envelope, format);
  if (!serialized.has_value()) {
    return operation::Result<AnalysisResultExportReport>::failure(
        serialized.error());
  }
  const auto temporary = temporary_path(path);
  if (temporary.empty()) {
    return operation::Result<AnalysisResultExportReport>::failure(
        {operation::ErrorCode::internal,
         "could not allocate a temporary analysis result path", {}});
  }
  std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
  if (!output) {
    return operation::Result<AnalysisResultExportReport>::failure(
        {operation::ErrorCode::internal,
         "could not create temporary analysis result output", {}});
  }
  output.write(serialized.value().data(),
               static_cast<std::streamsize>(serialized.value().size()));
  output.flush();
  if (!output) {
    output.close();
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return operation::Result<AnalysisResultExportReport>::failure(
        {operation::ErrorCode::internal,
         "failed while writing analysis result output", {}});
  }
  output.close();
  if (!replace_file(temporary, path, overwrite)) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return operation::Result<AnalysisResultExportReport>::failure(
        {operation::ErrorCode::internal,
         "could not atomically publish analysis result output: " +
             path.string(),
         {}});
  }
  std::vector<std::string> losses;
  if(format==operation::OutputFormat::csv) {
    losses.push_back("scientific provenance is not represented in CSV columns");
    if(analysis_plot_projection(record.kind,record.export_table).has_value())
      losses.push_back("plot projection metadata is not represented in CSV columns");
  }
  return operation::Result<AnalysisResultExportReport>::success(
      {record.result_id,path,format==operation::OutputFormat::json?"json":"csv",
       static_cast<std::uint64_t>(serialized.value().size()),std::move(losses)});
}

namespace {

// Appends PyMOL-style dashes for one measured segment (RepDistDash layout).
void append_dashes(render::RenderPacket& packet, model::Vec3d first,
                   model::Vec3d second, render::ColorRgba color,
                   const AnalysisOverlayStyle& style) {
  const auto unit = style.units_per_angstrom;
  const auto small = 1.0e-4 * unit;
  const model::Vec3d delta{second.x - first.x, second.y - first.y,
                           second.z - first.z};
  const auto length =
      std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
  if (!(length > small))
    return;
  const model::Vec3d direction{delta.x / length, delta.y / length,
                               delta.z / length};
  const auto dash_length = style.dash_length * unit;
  const auto dash_gap = style.dash_gap * unit;
  auto dash_sum = dash_length + dash_gap;
  if (dash_sum < small)
    dash_sum = 0.5 * unit;
  const auto along = [&](model::Vec3d origin, double distance, double sign) {
    return model::Vec3d{origin.x + sign * direction.x * distance,
                        origin.y + sign * direction.y * distance,
                        origin.z + sign * direction.z * distance};
  };
  if (dash_gap > small) {
    const model::Vec3d middle{(first.x + second.x) * 0.5,
                              (first.y + second.y) * 0.5,
                              (first.z + second.z) * 0.5};
    auto remaining = length / 2.0;
    double used{};
    const auto half_gap = dash_gap * 0.5;
    const auto emit = [&](double from, double to) {
      for (const auto sign : {1.0, -1.0})
        packet.lines.push_back({along(middle, from, sign), along(middle, to, sign),
                                color, color, style.dash_width, 0U});
    };
    while (remaining > dash_sum) {
      emit(used + half_gap, used + dash_length + half_gap);
      remaining -= dash_sum;
      used += dash_sum;
    }
    if (remaining > dash_gap) {
      remaining -= dash_gap;
      emit(used + half_gap, used + remaining + half_gap);
    }
  } else if (dash_length > small) {
    packet.lines.push_back({first, second, color, color, style.dash_width, 0U});
  }
}

}  // namespace

// Dashed chords along an arc around `center` from axis x toward axis y
// (both of length `radius`), phased like PyMOL measurement arcs.
void append_arc_dashes(render::RenderPacket& packet, model::Vec3d center,
                       model::Vec3d x, model::Vec3d y, double angle,
                       double radius, render::ColorRgba color,
                       const AnalysisOverlayStyle& style) {
  const auto unit = style.units_per_angstrom;
  const auto dash_length = style.dash_length * unit;
  const auto dash_gap = style.dash_gap * unit;
  auto dash_sum = dash_length + dash_gap;
  if (dash_sum < 1.0e-4 * unit)
    dash_sum = 0.1 * unit;
  // PyMOL lays dashes out over twice the arc length.
  const auto length = angle * radius * 2.0;
  if (!(length > 1.0e-4 * unit))
    return;
  const auto phase = dash_sum - std::fmod(length / 2.0 + dash_gap / 2.0, dash_sum);
  const auto at = [&](double position) {
    const auto turn = angle * position / length;
    return model::Vec3d{center.x + x.x * std::cos(turn) + y.x * std::sin(turn),
                        center.y + x.y * std::cos(turn) + y.y * std::sin(turn),
                        center.z + x.z * std::cos(turn) + y.z * std::sin(turn)};
  };
  for (auto position = -phase; position < length; position += dash_sum) {
    const auto start = std::max(position, 0.0);
    const auto end = std::min(position + dash_length, length);
    if (start < end) {
      packet.lines.push_back({at(start), at(end), color, color, style.dash_width, 0U});
      render::include(packet.bounds, at(start));
      render::include(packet.bounds, at(end));
    }
  }
}

model::Vec3d minus(model::Vec3d a, model::Vec3d b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
double norm(model::Vec3d a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
model::Vec3d scaled(model::Vec3d a, double s) { return {a.x * s, a.y * s, a.z * s}; }
model::Vec3d normalized_or_zero(model::Vec3d a) {
  const auto l = norm(a);
  return l > 1.0e-8 ? scaled(a, 1.0 / l) : model::Vec3d{};
}
model::Vec3d without_component(model::Vec3d v, model::Vec3d unit) {
  const auto d = v.x * unit.x + v.y * unit.y + v.z * unit.z;
  return {v.x - unit.x * d, v.y - unit.y * d, v.z - unit.z * d};
}
double angle_between(model::Vec3d a, model::Vec3d b) {
  const auto na = norm(a), nb = norm(b);
  if (na <= 1.0e-12 || nb <= 1.0e-12)
    return 0.0;
  const auto c = std::clamp((a.x * b.x + a.y * b.y + a.z * b.z) / (na * nb), -1.0, 1.0);
  return std::acos(c);
}

// PyMOL also draws a solid line along an arm whose atoms are not bonded;
// without topology here, arms up to 2.1 A count as bonds.
void append_arm(render::RenderPacket& packet, model::Vec3d from, model::Vec3d to,
                render::ColorRgba color, const AnalysisOverlayStyle& style) {
  if (norm(minus(from, to)) <= 2.1 * style.units_per_angstrom)
    return;
  packet.lines.push_back({from, to, color, color, style.dash_width, 0U});
  render::include(packet.bounds, from);
  render::include(packet.bounds, to);
}

// PyMOL angle display: a dashed arc at the vertex between the two arms.
void append_angle(render::RenderPacket& packet, model::Vec3d first,
                  model::Vec3d vertex, model::Vec3d third, render::ColorRgba color,
                  const AnalysisOverlayStyle& style) {
  const auto d1 = minus(first, vertex);
  const auto d2 = minus(third, vertex);
  const auto radius = std::min(norm(d1), norm(d2)) * style.angle_size;
  const auto n1 = normalized_or_zero(d1);
  auto d3 = without_component(d2, n1);
  const auto n3 = norm(d3) < 1.0e-8 ? model::Vec3d{1.0, 0.0, 0.0} : normalized_or_zero(d3);
  append_arm(packet, first, vertex, color, style);
  append_arm(packet, third, vertex, color, style);
  append_arc_dashes(packet, vertex, scaled(n1, radius), scaled(n3, radius),
                    angle_between(d1, d2), radius, color, style);
}

// PyMOL dihedral display: solid radial lines from the central bond midpoint
// toward the projected outer arms and a dashed arc between them.
void append_dihedral(render::RenderPacket& packet, model::Vec3d p1, model::Vec3d p2,
                     model::Vec3d p3, model::Vec3d p4, render::ColorRgba color,
                     const AnalysisOverlayStyle& style) {
  const auto axis = normalized_or_zero(minus(p3, p2));
  const auto p12 = without_component(minus(p1, p2), axis);
  const auto p43 = without_component(minus(p4, p3), axis);
  const model::Vec3d middle{(p2.x + p3.x) * 0.5, (p2.y + p3.y) * 0.5, (p2.z + p3.z) * 0.5};
  const auto radius = std::min(norm(p12), norm(p43)) * style.dihedral_size;
  const auto u12 = normalized_or_zero(p12);
  const auto u43 = normalized_or_zero(p43);
  for (const auto& arm : {u12, u43}) {
    const model::Vec3d end{middle.x + arm.x * radius, middle.y + arm.y * radius,
                           middle.z + arm.z * radius};
    packet.lines.push_back({end, middle, color, color, style.dash_width, 0U});
    render::include(packet.bounds, end);
  }
  append_arm(packet, p1, p2, color, style);
  append_arm(packet, p3, p2, color, style);
  append_arm(packet, p3, p4, color, style);
  auto d3 = without_component(p43, u12);
  const auto n3 = norm(d3) < 1.0e-8 ? model::Vec3d{1.0, 0.0, 0.0} : normalized_or_zero(d3);
  append_arc_dashes(packet, middle, scaled(u12, radius), scaled(n3, radius),
                    angle_between(p12, p43), radius, color, style);
}

render::RenderPacket build_analysis_overlay_packet(
    std::span<const PersistentAnalysisResult> records,
    const AnalysisOverlayStyle& style) {
  render::RenderPacket packet;
  packet.provenance.emplace("analysis_overlay", "persistent-result-v2");
  constexpr render::ColorRgba point_color{1.0F, 0.72F, 0.05F, 1.0F};
  // PyMOL measurement objects are yellow unless dash_color is set.
  const auto distance_color =
      style.dash_color.value_or(render::ColorRgba{1.0F, 1.0F, 0.0F, 1.0F});
  const auto geometry_color = distance_color;
  for (const auto& record : records) {
    if (!record.overlay_visible) continue;
    if (const auto* point = std::get_if<PointAnalysisOverlay>(&record.overlay)) {
      packet.spheres.push_back(
          {point->position, 0.25, point_color, 0U});
      packet.labels.push_back(
          {point->position, point->label, point_color, 0U, 0.0, 0, {}, 0U, 0U});
      render::include(packet.bounds, point->position, 0.25);
      continue;
    }
    const auto* distance = std::get_if<DistanceAnalysisOverlay>(&record.overlay);
    if (distance == nullptr) {
      const auto* geometry =
          std::get_if<GeometryAnalysisOverlay>(&record.overlay);
      if (geometry == nullptr) continue;
      const auto& p = geometry->positions;
      if (p.size() == 3U) {
        append_angle(packet, p[0], p[1], p[2], geometry_color, style);
      } else if (p.size() == 4U) {
        append_dihedral(packet, p[0], p[1], p[2], p[3], geometry_color, style);
      } else {
        for (std::size_t index = 1U; index < p.size(); ++index)
          append_dashes(packet, p[index - 1U], p[index], geometry_color, style);
      }
      model::Vec3d anchor{};
      for (const auto position : geometry->positions) {
        anchor.x += position.x;
        anchor.y += position.y;
        anchor.z += position.z;
      }
      const auto divisor = static_cast<double>(geometry->positions.size());
      anchor = {anchor.x / divisor, anchor.y / divisor, anchor.z / divisor};
      packet.labels.push_back(
          {anchor, geometry->label, geometry_color, 0U, 0.0, 0, {}, 0U, 0U});
      continue;
    }
    append_dashes(packet, distance->first_position, distance->second_position,
                  distance_color, style);
    const model::Vec3d middle{
        (distance->first_position.x + distance->second_position.x) * 0.5,
        (distance->first_position.y + distance->second_position.y) * 0.5,
        (distance->first_position.z + distance->second_position.z) * 0.5};
    // PyMOL draws only the dashes and the label (no endpoint markers).
    packet.labels.push_back(
        {middle, distance->label, distance_color, 0U, 0.0, 0, {}, 0U, 0U});
    render::include(packet.bounds, distance->first_position);
    render::include(packet.bounds, distance->second_position);
  }
  return packet;
}

}  // namespace molshredder::application
