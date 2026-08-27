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

render::RenderPacket build_analysis_overlay_packet(
    std::span<const PersistentAnalysisResult> records) {
  render::RenderPacket packet;
  packet.provenance.emplace("analysis_overlay", "persistent-result-v1");
  constexpr render::ColorRgba point_color{1.0F, 0.72F, 0.05F, 1.0F};
  constexpr render::ColorRgba distance_color{0.15F, 0.9F, 1.0F, 1.0F};
  constexpr render::ColorRgba geometry_color{1.0F, 0.38F, 0.72F, 1.0F};
  constexpr std::size_t dash_count = 16U;
  for (const auto& record : records) {
    if (!record.overlay_visible) continue;
    if (const auto* point = std::get_if<PointAnalysisOverlay>(&record.overlay)) {
      packet.spheres.push_back(
          {point->position, 0.25, point_color, 0U});
      packet.labels.push_back(
          {point->position, point->label, point_color, 0U});
      render::include(packet.bounds, point->position, 0.25);
      continue;
    }
    const auto* distance = std::get_if<DistanceAnalysisOverlay>(&record.overlay);
    if (distance == nullptr) {
      const auto* geometry =
          std::get_if<GeometryAnalysisOverlay>(&record.overlay);
      if (geometry == nullptr) continue;
      for (std::size_t index = 1U; index < geometry->positions.size(); ++index) {
        packet.lines.push_back({geometry->positions[index - 1U],
                                geometry->positions[index], geometry_color,
                                geometry_color, 2.0F, 0U});
      }
      model::Vec3d anchor{};
      for (const auto position : geometry->positions) {
        anchor.x += position.x;
        anchor.y += position.y;
        anchor.z += position.z;
        packet.spheres.push_back({position, 0.10, geometry_color, 0U});
        render::include(packet.bounds, position, 0.10);
      }
      const auto divisor = static_cast<double>(geometry->positions.size());
      anchor = {anchor.x / divisor, anchor.y / divisor, anchor.z / divisor};
      packet.labels.push_back({anchor, geometry->label, geometry_color, 0U});
      continue;
    }
    const auto delta = model::Vec3d{
        distance->second_position.x - distance->first_position.x,
        distance->second_position.y - distance->first_position.y,
        distance->second_position.z - distance->first_position.z};
    const auto at = [&](double fraction) {
      return model::Vec3d{distance->first_position.x + delta.x * fraction,
                          distance->first_position.y + delta.y * fraction,
                          distance->first_position.z + delta.z * fraction};
    };
    for (std::size_t dash = 0; dash < dash_count; dash += 2U) {
      packet.lines.push_back(
          {at(static_cast<double>(dash) / dash_count),
           at(static_cast<double>(dash + 1U) / dash_count), distance_color,
           distance_color, 2.0F, 0U});
    }
    packet.spheres.push_back(
        {distance->first_position, 0.12, distance_color, 0U});
    packet.spheres.push_back(
        {distance->second_position, 0.12, distance_color, 0U});
    packet.labels.push_back({at(0.5), distance->label, distance_color, 0U});
    render::include(packet.bounds, distance->first_position, 0.12);
    render::include(packet.bounds, distance->second_position, 0.12);
  }
  return packet;
}

}  // namespace molshredder::application
