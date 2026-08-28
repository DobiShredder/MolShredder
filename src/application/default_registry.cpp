#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/session.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <utility>

#include "molshredder/command/foundation_grammar.hpp"
#include "molshredder/io/format_capabilities.hpp"
#include "molshredder/io/molfile_provider.hpp"
#include "molshredder/model/chemical_perception.hpp"
#include "molshredder/model/molecular_builder.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/scene/pymol_view.hpp"
#include "molshredder/version.hpp"
#include "molshredder_support_configuration.hpp"

namespace molshredder::application {
namespace {

io::StructureFormat structure_format(const command::Arguments &arguments) {
  const auto found = arguments.find("file-format");
  if (found == arguments.end() || found->second == "auto") {
    return io::StructureFormat::auto_detect;
  }
  if (found->second == "pdb")
    return io::StructureFormat::pdb;
  if (found->second == "mmcif" || found->second == "cif") {
    return io::StructureFormat::mmcif;
  }
  if (found->second == "bcif") {
    return io::StructureFormat::bcif;
  }
  if (found->second == "pqr")
    return io::StructureFormat::pqr;
  if (found->second == "mol")
    return io::StructureFormat::mol;
  if (found->second == "mol2")
    return io::StructureFormat::mol2;
  if (found->second == "gro")
    return io::StructureFormat::gro;
  if (found->second == "g96")
    return io::StructureFormat::g96;
  if (found->second == "vtf")
    return io::StructureFormat::vtf;
  if (found->second == "psf")
    return io::StructureFormat::psf;
  if (found->second == "prmtop")
    return io::StructureFormat::prmtop;
  if (found->second == "sdf")
    return io::StructureFormat::sdf;
  if (found->second == "xyz")
    return io::StructureFormat::xyz;
  return io::StructureFormat::auto_detect;
}

render::RepresentationKind representation_kind(std::string_view value) {
  if (value == "sticks")
    return render::RepresentationKind::sticks;
  if (value == "spheres")
    return render::RepresentationKind::spheres;
  if (value == "ribbon")
    return render::RepresentationKind::ribbon;
  if (value == "cartoon")
    return render::RepresentationKind::cartoon;
  return render::RepresentationKind::lines;
}

render::VolumeSliceAxis volume_slice_axis(std::string_view value) {
  if (value == "x")
    return render::VolumeSliceAxis::x;
  if (value == "y")
    return render::VolumeSliceAxis::y;
  return render::VolumeSliceAxis::z;
}

render::MolecularSurfaceKind molecular_surface_kind(std::string_view value) {
  return value == "vdw" ? render::MolecularSurfaceKind::van_der_waals
                        : render::MolecularSurfaceKind::solvent_accessible;
}

std::vector<render::RepresentationKind>
representation_kinds(std::string_view value) {
  if (value == "everything") {
    return {render::RepresentationKind::lines,
            render::RepresentationKind::sticks,
            render::RepresentationKind::spheres,
            render::RepresentationKind::ribbon,
            render::RepresentationKind::cartoon};
  }
  if (value == "wire")
    return {render::RepresentationKind::lines};
  if (value == "licorice")
    return {render::RepresentationKind::sticks};
  return {representation_kind(value)};
}

std::string resolved_representation_names(
    std::span<const render::RepresentationKind> kinds) {
  std::string result;
  for (const auto kind : kinds) {
    if (!result.empty())
      result += ',';
    switch (kind) {
    case render::RepresentationKind::lines:
      result += "lines";
      break;
    case render::RepresentationKind::sticks:
      result += "sticks";
      break;
    case render::RepresentationKind::spheres:
      result += "spheres";
      break;
    case render::RepresentationKind::ribbon:
      result += "ribbon";
      break;
    case render::RepresentationKind::cartoon:
      result += "cartoon";
      break;
    }
  }
  return result;
}

operation::LengthUnit length_unit(std::string_view value) {
  return value == "nanometer" ? operation::LengthUnit::nanometer
                              : operation::LengthUnit::angstrom;
}

std::string_view length_unit_name(operation::LengthUnit unit) {
  return unit == operation::LengthUnit::nanometer ? "nanometer" : "angstrom";
}

double convert_length(double value, operation::LengthUnit from,
                      operation::LengthUnit to) {
  if (from == to)
    return value;
  return from == operation::LengthUnit::angstrom ? value * 0.1 : value * 10.0;
}

double rounded(double value, unsigned int precision) {
  const auto scale = std::pow(10.0, static_cast<double>(precision));
  const auto scaled = value * scale;
  if (!std::isfinite(scaled))
    return value;
  return std::round(scaled) / scale;
}

command::Value precise(double value, unsigned int precision) {
  return command::Number{rounded(value, precision), precision};
}

std::optional<std::string> requested_result_name(
    const command::Arguments &arguments) {
  const auto found = arguments.find("result-name");
  return found == arguments.end() ? std::nullopt
                                  : std::optional<std::string>{found->second};
}

std::optional<operation::Error> validate_result_name(
    const Workspace &workspace, const command::Arguments &arguments) {
  return workspace.validate_analysis_result_name(
      requested_result_name(arguments));
}

operation::Result<command::Response> persist_analysis_response(
    Workspace &workspace, AnalysisResultDraft draft) {
  if(const auto plot=analysis_plot_projection(draft.kind,draft.export_table);plot.has_value())
    draft.response.fields.insert_or_assign("plot",*plot);
  const auto stored = workspace.store_analysis_result(std::move(draft));
  if (!stored.has_value())
    return operation::Result<command::Response>::failure(stored.error());
  auto response = stored.value().response;
  const auto contract_fields =
      scientific_contract_fields(stored.value().provenance.scientific);
  if (!contract_fields.has_value())
    return operation::Result<command::Response>::failure(
        contract_fields.error());
  for (const auto& [name, value] : contract_fields.value())
    response.fields.insert_or_assign(name, value);
  return operation::Result<command::Response>::success(std::move(response));
}

ScientificResultContract scientific_contract(
    std::string algorithm, std::string algorithm_version,
    std::string input_coordinate_unit, std::string output_unit,
    unsigned int presentation_precision, std::string pbc_policy,
    bool pbc_cell_required, std::string missing_data_policy,
    double absolute_tolerance, double relative_tolerance,
    std::string tolerance_unit,
    std::string coordinate_scope = "current_frame") {
  return {kScientificResultContractSchemaVersion,
          {},
          0U,
          0U,
          true,
          std::move(coordinate_scope),
          std::move(algorithm),
          std::move(algorithm_version),
          std::move(input_coordinate_unit),
          std::move(output_unit),
          "float64",
          presentation_precision,
          std::move(pbc_policy),
          pbc_cell_required,
          std::move(missing_data_policy),
          {absolute_tolerance, relative_tolerance,
           std::move(tolerance_unit)},
          true};
}

ScientificResultContract scientific_contract(
    std::string algorithm, std::string algorithm_version,
    operation::LengthUnit input_coordinate_unit,
    operation::LengthUnit output_unit, unsigned int presentation_precision,
    std::string pbc_policy, bool pbc_cell_required,
    std::string missing_data_policy, double absolute_tolerance = 1.0e-10,
    double relative_tolerance = 1.0e-12,
    std::string coordinate_scope = "current_frame") {
  return scientific_contract(
      std::move(algorithm), std::move(algorithm_version),
      std::string{length_unit_name(input_coordinate_unit)},
      std::string{length_unit_name(output_unit)}, presentation_precision,
      std::move(pbc_policy), pbc_cell_required,
      std::move(missing_data_policy), absolute_tolerance,
      relative_tolerance, std::string{length_unit_name(output_unit)},
      std::move(coordinate_scope));
}

operation::Result<command::Response> scientific_response(
    const Workspace& workspace, command::Response response,
    ScientificResultContract contract) {
  const auto bound =
      workspace.bind_scientific_result_contract(std::move(contract));
  if (!bound.has_value())
    return operation::Result<command::Response>::failure(bound.error());
  const auto contract_fields = scientific_contract_fields(bound.value());
  if (!contract_fields.has_value())
    return operation::Result<command::Response>::failure(
        contract_fields.error());
  for (const auto& [name, value] : contract_fields.value())
    response.fields.insert_or_assign(name, value);
  return operation::Result<command::Response>::success(std::move(response));
}

operation::Result<GeometryAnalysisOverlay> geometry_analysis_overlay(
    const Workspace& workspace, std::uint64_t object_id,
    std::span<const model::AtomIndex> indices,
    std::span<const model::Vec3d> positions, std::string label) {
  const auto* object = workspace.active_object();
  if (object == nullptr || object->id != object_id ||
      indices.size() != positions.size() ||
      (indices.size() != 3U && indices.size() != 4U)) {
    return operation::Result<GeometryAnalysisOverlay>::failure(
        {operation::ErrorCode::stale_result,
         "geometry result lost its active source topology", {}});
  }
  const auto& topology = *object->system->topology();
  const model::TopologySnapshotReference snapshot{
      model::kTopologyReferenceSchemaVersion, object_id, topology.version()};
  std::vector<model::AtomReference> atoms;
  atoms.reserve(indices.size());
  for (const auto index : indices) {
    const auto atom_id = topology.atom_id(index);
    if (!atom_id.has_value()) {
      return operation::Result<GeometryAnalysisOverlay>::failure(
          {operation::ErrorCode::stale_result,
           "geometry endpoint is stale for the source topology", {}});
    }
    atoms.push_back({snapshot, *atom_id});
  }
  return operation::Result<GeometryAnalysisOverlay>::success(
      {std::move(atoms), {positions.begin(), positions.end()},
       std::move(label)});
}

operation::Result<std::size_t>
size_argument(const command::Arguments &arguments, std::string_view name,
              bool require_positive = false) {
  const auto found = arguments.find(name);
  if (found == arguments.end()) {
    return operation::Result<std::size_t>::failure(
        operation::Error{operation::ErrorCode::internal,
                         "normalized command is missing --" + std::string{name},
                         {}});
  }
  const auto &text = found->second;
  unsigned long long parsed{};
  const auto result =
      molshredder::core::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      parsed > std::numeric_limits<std::size_t>::max() ||
      (require_positive && parsed == 0U)) {
    return operation::Result<std::size_t>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "invalid non-negative value for --" + std::string{name} + ": " + text,
        require_positive ? "provide a positive integer"
                         : "provide a non-negative integer"});
  }
  return operation::Result<std::size_t>::success(
      static_cast<std::size_t>(parsed));
}

operation::Result<std::vector<std::string>> batch_values(
    std::string_view text, std::string_view parameter) {
  std::vector<std::string> values;
  std::size_t begin{};
  while (begin <= text.size()) {
    const auto end = text.find(';', begin);
    const auto length = end == std::string_view::npos ? text.size() - begin
                                                      : end - begin;
    if (length == 0U) {
      return operation::Result<std::vector<std::string>>::failure(
          operation::Error{operation::ErrorCode::invalid_argument,
                           "--" + std::string{parameter} +
                               " contains an empty batch item",
                           "provide non-empty semicolon-delimited values"});
    }
    values.emplace_back(text.substr(begin, length));
    if (end == std::string_view::npos) break;
    begin = end + 1U;
  }
  return operation::Result<std::vector<std::string>>::success(
      std::move(values));
}

operation::Result<std::vector<std::string_view>> delimited_fields(
    std::string_view text, char delimiter, std::string_view description) {
  std::vector<std::string_view> fields;
  std::size_t begin{};
  while (begin <= text.size()) {
    const auto end = text.find(delimiter, begin);
    const auto field = text.substr(
        begin, end == std::string_view::npos ? text.size() - begin
                                             : end - begin);
    if (field.empty())
      return operation::Result<std::vector<std::string_view>>::failure(
          operation::Error{operation::ErrorCode::invalid_argument,
                           std::string{description} +
                               " contains an empty field",
                           "provide every documented field"});
    fields.push_back(field);
    if (end == std::string_view::npos) break;
    begin = end + 1U;
  }
  return operation::Result<std::vector<std::string_view>>::success(
      std::move(fields));
}

template <typename Integer>
operation::Result<Integer> integer_text(std::string_view text,
                                        std::string_view description) {
  Integer value{};
  const auto converted = molshredder::core::from_chars(
      text.data(), text.data() + text.size(), value);
  if (converted.ec != std::errc{} ||
      converted.ptr != text.data() + text.size())
    return operation::Result<Integer>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "invalid " + std::string{description} + ": " + std::string{text},
        "provide a decimal integer"});
  return operation::Result<Integer>::success(value);
}

operation::Result<double> finite_number_text(
    std::string_view text, std::string_view description) {
  double value{};
  const auto converted = molshredder::core::from_chars(
      text.data(), text.data() + text.size(), value);
  if (converted.ec != std::errc{} ||
      converted.ptr != text.data() + text.size() || !std::isfinite(value))
    return operation::Result<double>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "invalid finite " + std::string{description} + ": " +
            std::string{text},
        "provide a finite number"});
  return operation::Result<double>::success(value);
}

operation::Result<model::BondOrder> builder_bond_order(
    std::string_view text) {
  if (text == "single")
    return operation::Result<model::BondOrder>::success(
        model::BondOrder::single);
  if (text == "double")
    return operation::Result<model::BondOrder>::success(
        model::BondOrder::double_bond);
  if (text == "triple")
    return operation::Result<model::BondOrder>::success(
        model::BondOrder::triple);
  if (text == "aromatic")
    return operation::Result<model::BondOrder>::success(
        model::BondOrder::aromatic);
  if (text == "amide")
    return operation::Result<model::BondOrder>::success(
        model::BondOrder::amide);
  return operation::Result<model::BondOrder>::failure(operation::Error{
      operation::ErrorCode::invalid_argument,
      "unsupported builder bond order: " + std::string{text},
      "use single, double, triple, aromatic, or amide"});
}

std::string_view bond_order_name(model::BondOrder order) {
  switch (order) {
    case model::BondOrder::single:
      return "single";
    case model::BondOrder::double_bond:
      return "double";
    case model::BondOrder::triple:
      return "triple";
    case model::BondOrder::aromatic:
      return "aromatic";
    case model::BondOrder::amide:
      return "amide";
    case model::BondOrder::zero:
      return "zero";
    case model::BondOrder::query:
      return "query";
    case model::BondOrder::unknown:
      return "unknown";
  }
  return "unknown";
}

command::Value::Object topology_edit_fields(
    const TopologyPropertyEditResult &value) {
  return {{"diff_schema_version", kEditTransactionDiffSchemaVersion},
          {"transaction_kind", std::string{to_string(value.transaction_kind)}},
          {"coordinate_revision", value.coordinate_revision},
          {"coordinate_source_revision",
           value.coordinate_source_revision},
          {"evicted_transaction_count", value.evicted_transaction_count},
          {"invalidated_measurement_count",
           value.invalidated_measurement_count},
          {"object_id", value.object_id},
          {"previous_coordinate_source_revision",
           value.previous_coordinate_source_revision},
          {"previous_topology_version", value.previous_topology_version},
          {"topology_version", value.topology_version},
          {"transaction_id", value.transaction_id},
          {"undo_bytes", value.undo_bytes}};
}

operation::Result<std::uint64_t>
uint64_argument(const command::Arguments &arguments, std::string_view name,
                bool require_positive = false) {
  const auto found = arguments.find(name);
  if (found == arguments.end())
    return operation::Result<std::uint64_t>::failure(operation::Error{
        operation::ErrorCode::internal,
        "normalized command is missing --" + std::string{name}, {}});
  std::uint64_t parsed{};
  const auto &text = found->second;
  const auto result =
      molshredder::core::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      (require_positive && parsed == 0U)) {
    return operation::Result<std::uint64_t>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "invalid unsigned 64-bit value for --" + std::string{name} + ": " +
            text,
        require_positive ? "provide a positive integer in the uint64 range"
                         : "provide an integer in the uint64 range"});
  }
  return operation::Result<std::uint64_t>::success(parsed);
}

operation::Result<std::vector<model::AtomId>> atom_id_list_argument(
    const command::Arguments &arguments, std::string_view name) {
  const auto found = arguments.find(name);
  if (found == arguments.end())
    return operation::Result<std::vector<model::AtomId>>::failure(
        operation::Error{operation::ErrorCode::internal,
                         "normalized command is missing --" +
                             std::string{name},
                         {}});
  if (found->second == "none")
    return operation::Result<std::vector<model::AtomId>>::success({});
  std::vector<model::AtomId> ids;
  std::string_view remaining{found->second};
  while (!remaining.empty()) {
    const auto comma = remaining.find(',');
    const auto token = remaining.substr(0U, comma);
    std::uint64_t value{};
    const auto parsed =
        molshredder::core::from_chars(token.data(), token.data() + token.size(), value);
    if (token.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != token.data() + token.size() || value == 0U) {
      return operation::Result<std::vector<model::AtomId>>::failure(
          operation::Error{operation::ErrorCode::invalid_argument,
                           "invalid stable atom ID list: " + found->second,
                           "use comma-separated positive uint64 IDs or none"});
    }
    ids.push_back(model::AtomId{value});
    if (comma == std::string_view::npos)
      break;
    remaining.remove_prefix(comma + 1U);
  }
  return operation::Result<std::vector<model::AtomId>>::success(
      std::move(ids));
}

trajectory::AtomMappingPolicy trajectory_mapping_policy(
    std::string_view value) {
  if (value == "exact") return trajectory::AtomMappingPolicy::exact;
  if (value == "explicit") return trajectory::AtomMappingPolicy::explicit_map;
  return trajectory::AtomMappingPolicy::index_order;
}

command::Value trajectory_mapping_value(
    const trajectory::AtomMappingReport &mapping) {
  command::Value::Array axes;
  for (const auto &axis : mapping.compared_axes) axes.emplace_back(axis);
  return command::Value::Object{
      {"compared_axes", std::move(axes)},
      {"identity_strength", mapping.identity_strength},
      {"policy", std::string{trajectory::to_string(mapping.policy)}},
      {"topology_version", mapping.topology_version}};
}

std::string_view time_unit_name(model::TimeUnit unit) {
  return unit == model::TimeUnit::femtosecond ? "femtosecond" : "picosecond";
}

command::Value trajectory_semantics_value(
    const trajectory::TrajectorySemanticReport &semantics) {
  const auto &channels = semantics.channels;
  return command::Value::Object{
      {"canonical_coordinate_unit", semantics.canonical_coordinate_unit},
      {"canonical_time_unit", semantics.canonical_time_unit},
      {"channels",
       command::Value::Object{
           {"force", channels.has_forces},
           {"physical_time", channels.has_physical_time},
           {"source_step", channels.has_source_step},
           {"unit_cell", channels.has_unit_cell},
           {"velocity", channels.has_velocities}}},
      {"coordinate_conversion_applied",
       semantics.coordinate_conversion_applied},
      {"force_conversion_applied", semantics.force_conversion_applied},
      {"force_unit", channels.force_unit.empty()
                         ? command::Value{nullptr}
                         : command::Value{channels.force_unit}},
      {"schema", semantics.schema},
      {"missing_data_policy", semantics.missing_data_policy},
      {"source_coordinate_unit",
       std::string{length_unit_name(channels.source_coordinate_unit)}},
      {"source_force_unit", channels.source_force_unit.empty()
                                ? command::Value{nullptr}
                                : command::Value{channels.source_force_unit}},
      {"source_physical_time_unit",
       channels.has_physical_time
           ? command::Value{std::string{
                 time_unit_name(channels.physical_time_unit)}}
           : command::Value{nullptr}},
      {"source_velocity_time_unit",
       channels.has_velocities
           ? command::Value{std::string{
                 time_unit_name(channels.velocity_time_unit)}}
           : command::Value{nullptr}},
      {"time_conversion_applied", semantics.time_conversion_applied},
      {"validation_scope", semantics.validation_scope}};
}

operation::Result<CameraStateScope>
camera_state_scope(const command::Arguments &arguments) {
  const auto found = arguments.find("state");
  if (found == arguments.end()) {
    return operation::Result<CameraStateScope>::failure(operation::Error{
        operation::ErrorCode::internal,
        "normalized camera command is missing --state", {}});
  }
  if (found->second == "current" || found->second == "-1") {
    return operation::Result<CameraStateScope>::success(
        CameraStateScope{CameraStateScopeKind::current, 0U});
  }
  if (found->second == "all" || found->second == "0") {
    return operation::Result<CameraStateScope>::success(
        CameraStateScope{CameraStateScopeKind::all, 0U});
  }
  unsigned long long parsed{};
  const auto converted = molshredder::core::from_chars(
      found->second.data(), found->second.data() + found->second.size(), parsed);
  if (converted.ec != std::errc{} ||
      converted.ptr != found->second.data() + found->second.size() ||
      parsed == 0U || parsed - 1U > std::numeric_limits<std::size_t>::max()) {
    return operation::Result<CameraStateScope>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "invalid camera state scope: " + found->second,
        "use current, all, -1, 0, or a positive one-based state"});
  }
  return operation::Result<CameraStateScope>::success(CameraStateScope{
      CameraStateScopeKind::explicit_state,
      static_cast<std::size_t>(parsed - 1U)});
}

command::Value camera_state_scope_value(CameraStateScope scope) {
  switch (scope.kind) {
  case CameraStateScopeKind::current: return std::string{"current"};
  case CameraStateScopeKind::all: return std::string{"all"};
  case CameraStateScopeKind::explicit_state:
    return static_cast<std::uint64_t>(scope.frame_index + 1U);
  }
  return std::string{"current"};
}

operation::Result<double> number_argument(const command::Arguments &arguments,
                                          std::string_view name) {
  const auto found = arguments.find(name);
  if (found == arguments.end()) {
    return operation::Result<double>::failure(
        operation::Error{operation::ErrorCode::internal,
                         "normalized command is missing --" + std::string{name},
                         {}});
  }
  double parsed{};
  const auto result =
      molshredder::core::from_chars(found->second.data(),
                      found->second.data() + found->second.size(), parsed);
  if (result.ec != std::errc{} ||
      result.ptr != found->second.data() + found->second.size() ||
      !std::isfinite(parsed)) {
    return operation::Result<double>::failure(
        operation::Error{operation::ErrorCode::invalid_argument,
                         "invalid finite value for --" + std::string{name} +
                             ": " + found->second,
                         "provide a finite number"});
  }
  return operation::Result<double>::success(parsed);
}

struct RenderSettingTarget {
  render::RenderSettingScope scope;
  render::RenderSettingContext context;
};

std::string_view render_setting_scope_name(
    render::RenderSettingScopeLevel level) {
  using enum render::RenderSettingScopeLevel;
  switch (level) {
  case global: return "global";
  case object: return "object";
  case object_state: return "state";
  case atom: return "atom";
  case bond: return "bond";
  }
  return "global";
}

operation::Result<RenderSettingTarget> render_setting_target(
    const command::Arguments &arguments, const Workspace &workspace) {
  using operation::Error;
  using operation::ErrorCode;
  using operation::Result;
  const auto scope_name = arguments.at("scope");
  if (scope_name == "global")
    return Result<RenderSettingTarget>::success({{}, {}});
  const auto object_reference = arguments.at("object");
  const WorkspaceObject *object = nullptr;
  for (const auto &candidate : workspace.objects()) {
    if ((object_reference == "current" &&
         workspace.active_object() == &candidate) ||
        object_reference == candidate.system->name() ||
        object_reference == std::to_string(candidate.id)) {
      object = &candidate;
      break;
    }
  }
  if (object == nullptr) {
    return Result<RenderSettingTarget>::failure(
        Error{ErrorCode::not_found,
              "render setting object does not exist: " + object_reference,
              "use current, an object name, or object list ID"});
  }
  std::size_t state_index{};
  const auto state = arguments.at("state");
  if (state == "current") {
    if (object->trajectory.has_value())
      state_index = object->trajectory->timeline.snapshot().frame;
  } else {
    unsigned long long parsed{};
    const auto conversion =
        molshredder::core::from_chars(state.data(), state.data() + state.size(), parsed);
    if (conversion.ec != std::errc{} ||
        conversion.ptr != state.data() + state.size() || parsed == 0U ||
        parsed - 1U > std::numeric_limits<std::size_t>::max()) {
      return Result<RenderSettingTarget>::failure(Error{
          ErrorCode::invalid_argument, "invalid render setting state: " + state,
          "use current or a positive one-based state"});
    }
    state_index = static_cast<std::size_t>(parsed - 1U);
  }
  auto level = render::RenderSettingScopeLevel::object;
  std::uint64_t atom_id{};
  std::uint64_t bond_id{};
  if (scope_name == "state") {
    level = render::RenderSettingScopeLevel::object_state;
  } else if (scope_name == "atom" || scope_name == "bond") {
    const auto found = arguments.find("target");
    if (found == arguments.end()) {
      return Result<RenderSettingTarget>::failure(Error{
          ErrorCode::invalid_argument,
          "--target is required for atom and bond render setting scopes",
          "provide the one-based atom or bond ID"});
    }
    unsigned long long parsed{};
    const auto conversion = molshredder::core::from_chars(
        found->second.data(), found->second.data() + found->second.size(), parsed);
    if (conversion.ec != std::errc{} ||
        conversion.ptr != found->second.data() + found->second.size() ||
        parsed == 0U) {
      return Result<RenderSettingTarget>::failure(Error{
          ErrorCode::invalid_argument,
          "invalid positive render setting target: " + found->second,
          "provide a one-based atom or bond ID"});
    }
    if (scope_name == "atom") {
      level = render::RenderSettingScopeLevel::atom;
      atom_id = parsed;
    } else {
      level = render::RenderSettingScopeLevel::bond;
      bond_id = parsed;
    }
  } else if (scope_name != "object") {
    return Result<RenderSettingTarget>::failure(
        Error{ErrorCode::invalid_argument,
              "unknown render setting scope: " + scope_name, {}});
  }
  const auto scope_state =
      level == render::RenderSettingScopeLevel::object ? 0U : state_index;
  return Result<RenderSettingTarget>::success(
      {{level, object->id, scope_state, atom_id, bond_id},
       {object->id, state_index, atom_id, bond_id}});
}

operation::Result<render::RenderSettingValue> parse_render_setting_value(
    const render::RenderSettingDefinition &definition,
    std::string_view text) {
  using operation::Error;
  using operation::ErrorCode;
  using operation::Result;
  switch (definition.value_type) {
  case render::RenderSettingValueType::boolean:
    if (text == "true")
      return Result<render::RenderSettingValue>::success(true);
    if (text == "false")
      return Result<render::RenderSettingValue>::success(false);
    break;
  case render::RenderSettingValueType::integer: {
    std::int64_t parsed{};
    const auto conversion =
        molshredder::core::from_chars(text.data(), text.data() + text.size(), parsed);
    if (conversion.ec == std::errc{} &&
        conversion.ptr == text.data() + text.size())
      return Result<render::RenderSettingValue>::success(parsed);
    break;
  }
  case render::RenderSettingValueType::number: {
    double parsed{};
    const auto conversion =
        molshredder::core::from_chars(text.data(), text.data() + text.size(), parsed);
    if (conversion.ec == std::errc{} &&
        conversion.ptr == text.data() + text.size() && std::isfinite(parsed))
      return Result<render::RenderSettingValue>::success(parsed);
    break;
  }
  case render::RenderSettingValueType::color:
    return Result<render::RenderSettingValue>::success(std::string{text});
  }
  return Result<render::RenderSettingValue>::failure(
      Error{ErrorCode::invalid_argument,
            "render setting value does not match type for " + definition.name,
            "inspect setting list for the declared type and range"});
}

command::Value render_setting_value(const render::RenderSettingValue &value) {
  return std::visit(
      [](const auto &item) -> command::Value { return item; }, value);
}

std::string_view render_setting_type_name(render::RenderSettingValueType type) {
  using enum render::RenderSettingValueType;
  switch (type) {
  case boolean: return "boolean";
  case integer: return "integer";
  case number: return "number";
  case color: return "color";
  }
  return "number";
}

operation::Result<model::Vec3d> vector_argument(
    const command::Arguments &arguments, std::string_view name) {
  const auto found = arguments.find(name);
  if (found == arguments.end()) {
    return operation::Result<model::Vec3d>::failure(operation::Error{
        operation::ErrorCode::internal,
        "normalized command is missing --" + std::string{name}, {}});
  }
  auto normalized = found->second;
  std::replace(normalized.begin(), normalized.end(), ',', ' ');
  std::istringstream stream{normalized};
  model::Vec3d result;
  std::string extra;
  if (!(stream >> result.x >> result.y >> result.z) || (stream >> extra) ||
      !scene::is_finite(result)) {
    return operation::Result<model::Vec3d>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "invalid three-component finite vector for --" + std::string{name} +
            ": " + found->second,
        "provide x,y,z, for example --" + std::string{name} + " 1,2,3"});
  }
  return operation::Result<model::Vec3d>::success(result);
}

render::ColorRgba named_color(std::string_view name, float alpha) {
  if (name == "blue") return {0.2F, 0.35F, 1.0F, alpha};
  if (name == "green") return {0.2F, 0.8F, 0.35F, alpha};
  if (name == "magenta") return {0.9F, 0.25F, 0.8F, alpha};
  if (name == "orange") return {1.0F, 0.5F, 0.15F, alpha};
  if (name == "red") return {0.95F, 0.2F, 0.2F, alpha};
  if (name == "white") return {1.0F, 1.0F, 1.0F, alpha};
  if (name == "yellow") return {1.0F, 0.85F, 0.2F, alpha};
  return {0.2F, 0.65F, 1.0F, alpha};
}

command::Value color_value(render::ColorRgba color) {
  return command::Value::Array{static_cast<double>(color.red),
                               static_cast<double>(color.green),
                               static_cast<double>(color.blue),
                               static_cast<double>(color.alpha)};
}

command::Value transfer_points_value(
    std::span<const render::TransferPoint> points) {
  command::Value::Array values;
  values.reserve(points.size());
  for (const auto &point : points) {
    values.emplace_back(command::Value::Object{{"color", color_value(point.color)},
                                                {"value", point.value}});
  }
  return values;
}

render::TransferPreset transfer_preset(std::string_view value) {
  if (value == "fire") return render::TransferPreset::fire;
  if (value == "grayscale") return render::TransferPreset::grayscale;
  if (value == "ice") return render::TransferPreset::ice;
  if (value == "spectrum") return render::TransferPreset::spectrum;
  return render::TransferPreset::density;
}

operation::Result<render::TransferFunction>
parse_transfer_function(std::string_view encoded) {
  std::vector<render::TransferPoint> points;
  std::istringstream entries{std::string{encoded}};
  std::string entry;
  while (std::getline(entries, entry, ';')) {
    std::replace(entry.begin(), entry.end(), ',', ' ');
    std::istringstream fields{entry};
    double value{};
    double red{};
    double green{};
    double blue{};
    double alpha{};
    std::string extra;
    if (!(fields >> value >> red >> green >> blue >> alpha) ||
        (fields >> extra)) {
      return operation::Result<render::TransferFunction>::failure(
          operation::Error{
              operation::ErrorCode::invalid_argument,
              "invalid --points transfer-function tuple: " + entry,
              "use value,r,g,b,a tuples separated by semicolons"});
    }
    points.push_back(
        {value,
         {static_cast<float>(red), static_cast<float>(green),
          static_cast<float>(blue), static_cast<float>(alpha)}});
  }
  return render::TransferFunction::create(std::move(points));
}

command::Value provider_value(const io::FormatProvider &provider) {
  return command::Value::Object{
      {"available", provider.available},
      {"id", provider.id},
      {"license_expression", provider.license_expression},
      {"license_status", std::string{io::to_string(provider.license_status)}},
      {"origin", std::string{io::to_string(provider.origin)}},
      {"trust", std::string{io::to_string(provider.trust)}},
      {"unavailable_reason",
       provider.unavailable_reason.empty()
           ? command::Value{nullptr}
           : command::Value{provider.unavailable_reason}},
      {"version", provider.version}};
}

command::Value optional_text_value(const std::optional<std::string> &value) {
  return value.has_value() ? command::Value{*value} : command::Value{nullptr};
}

command::Value optional_unsigned_value(
    const std::optional<std::uint64_t> &value) {
  return value.has_value() ? command::Value{*value} : command::Value{nullptr};
}

command::Value graphics_runtime_value(const GraphicsRuntimeInfo &graphics) {
  return command::Value::Object{
      {"api", graphics.api},
      {"backend", graphics.backend},
      {"device_id", optional_unsigned_value(graphics.device_id)},
      {"device_name", optional_text_value(graphics.device_name)},
      {"device_type", optional_text_value(graphics.device_type)},
      {"driver_version", optional_text_value(graphics.driver_version)},
      {"failure_reason", optional_text_value(graphics.failure_reason)},
      {"rhi_based", graphics.rhi_based},
      {"status", std::string{to_string(graphics.status)}},
      {"vendor_id", optional_unsigned_value(graphics.vendor_id)}};
}

io::FormatProvider molfile_provider_value(
    const io::MolfileProviderDescriptor &descriptor) {
  const auto trust = descriptor.trust == io::MolfileProviderTrust::explicit_path
                         ? io::FormatProviderTrust::untrusted
                         : io::FormatProviderTrust::trusted_configured;
  return io::FormatProvider{
      descriptor.provider_id,
      std::to_string(descriptor.major_version) + "." +
          std::to_string(descriptor.minor_version),
      io::FormatProviderOrigin::dynamic_plugin,
      trust,
      io::FormatProviderLicenseStatus::pending,
      "NOASSERTION",
      true,
      {}};
}

command::Value direction_value(
    const io::FormatDirectionCapability &capability) {
  const auto array = [](const std::vector<std::string> &values) {
    command::Value::Array result;
    result.reserve(values.size());
    for (const auto &value : values) result.emplace_back(value);
    return command::Value{std::move(result)};
  };
  return command::Value::Object{
      {"available", capability.available},
      {"channels", array(capability.channels)},
      {"limitations", array(capability.limitations)},
      {"typed_loss_reporting", capability.typed_loss_reporting},
      {"unavailable_reason",
       capability.unavailable_reason.empty()
           ? command::Value{nullptr}
           : command::Value{capability.unavailable_reason}}};
}

operation::Result<io::FormatProvider>
requested_provider(const command::Arguments &arguments,
                   io::FormatDirection direction) {
  const auto found = arguments.find("provider");
  return io::resolve_format_provider(
      {}, direction, found == arguments.end() ? "auto" : found->second);
}

operation::Result<analysis::SeriesRange>
series_range(const command::Arguments &arguments, const Workspace &workspace) {
  const auto first = size_argument(arguments, "first");
  const auto stride = size_argument(arguments, "stride", true);
  if (!first.has_value()) {
    return operation::Result<analysis::SeriesRange>::failure(first.error());
  }
  if (!stride.has_value()) {
    return operation::Result<analysis::SeriesRange>::failure(stride.error());
  }
  std::size_t last{};
  if (arguments.contains("last")) {
    const auto parsed = size_argument(arguments, "last");
    if (!parsed.has_value()) {
      return operation::Result<analysis::SeriesRange>::failure(parsed.error());
    }
    last = parsed.value();
  } else {
    const auto *object = workspace.active_object();
    if (object == nullptr || !object->trajectory.has_value()) {
      return operation::Result<analysis::SeriesRange>::failure(
          operation::Error{operation::ErrorCode::not_found,
                           "active object has no attached trajectory",
                           "attach one with traj load first"});
    }
    const auto count = object->trajectory->cache->frame_count();
    if (!count.has_value() || *count == 0U) {
      return operation::Result<analysis::SeriesRange>::failure(
          operation::Error{operation::ErrorCode::invalid_argument,
                           "trajectory has no known frames",
                           {}});
    }
    last = *count - 1U;
  }
  return operation::Result<analysis::SeriesRange>::success(
      analysis::SeriesRange{first.value(), last, stride.value()});
}

command::Value optional_step(const analysis::SeriesFrameMetadata &frame) {
  return frame.source_step.has_value() ? command::Value{*frame.source_step}
                                       : command::Value{nullptr};
}

command::Value optional_time(const analysis::SeriesFrameMetadata &frame) {
  return frame.physical_time.has_value() ? command::Value{*frame.physical_time}
                                         : command::Value{nullptr};
}

command::Value optional_time_unit(const analysis::SeriesFrameMetadata &frame) {
  if (!frame.physical_time_unit.has_value())
    return nullptr;
  return *frame.physical_time_unit == model::TimeUnit::picosecond
             ? command::Value{"picosecond"}
             : command::Value{"femtosecond"};
}

analysis::FitMode fit_mode(std::string_view value) {
  return value == "none" ? analysis::FitMode::none : analysis::FitMode::rigid;
}

analysis::WeightMode weight_mode(std::string_view value) {
  return value == "mass" ? analysis::WeightMode::mass
                         : analysis::WeightMode::uniform;
}

std::string fit_selection(const command::Arguments &arguments) {
  const auto found = arguments.find("fit-selection");
  return found == arguments.end() ? arguments.at("selection") : found->second;
}

void add_weight_provenance(command::Value::Object &fields,
                           const AnalysisWeightProvenance &weights) {
  fields.emplace("weight", weights.mode == analysis::WeightMode::mass
                               ? "mass"
                               : "uniform");
  fields.emplace("weight_source", weights.source);
  fields.emplace("weight_estimated", weights.estimated);
  fields.emplace("weight_unit", weights.unit.has_value()
                                    ? command::Value{*weights.unit}
                                    : command::Value{nullptr});
}

command::Value
optional_frame_count(const std::optional<std::size_t> &frame_count) {
  return frame_count.has_value() ? command::Value{*frame_count}
                                 : command::Value{nullptr};
}

command::Value::Object object_fields(const WorkspaceObjectInfo &object) {
  return {{"active", object.active},
          {"atom_count", object.atom_count},
          {"effectively_visible", object.effectively_visible},
          {"frame_count", optional_frame_count(object.frame_count)},
          {"has_trajectory", object.has_trajectory},
          {"name", object.name},
          {"object_id", object.id},
          {"representation_count", object.representation_count},
          {"scene_node_id", object.scene_node_id},
          {"visible", object.visible}};
}

command::Value::Object
object_lifecycle_fields(const ObjectLifecycleResult &result) {
  command::Value::Array order;
  order.reserve(result.ordered_object_ids.size());
  for (const auto id : result.ordered_object_ids)
    order.emplace_back(id);
  return {{"active_object_id",
           result.active_object_id.has_value()
               ? command::Value{*result.active_object_id}
               : command::Value{nullptr}},
          {"deleted", result.deleted},
          {"name", result.name},
          {"new_position", result.new_position + 1U},
          {"object_count", result.object_count},
          {"object_id", result.object_id},
          {"old_position", result.old_position + 1U},
          {"ordered_object_ids", std::move(order)},
          {"removed_measurement_count", result.removed_measurement_count},
          {"removed_setting_override_count",
           result.removed_setting_override_count},
          {"scene_node_id", result.scene_node_id}};
}

io::TrajectoryFormat trajectory_format(std::string_view value) {
  if (value == "dcd")
    return io::TrajectoryFormat::dcd;
  if (value == "trr")
    return io::TrajectoryFormat::trr;
  if (value == "xtc")
    return io::TrajectoryFormat::xtc;
  if (value == "rst7")
    return io::TrajectoryFormat::rst7;
  if (value == "mdcrd" || value == "crd")
    return io::TrajectoryFormat::mdcrd;
  if (value == "crdbox")
    return io::TrajectoryFormat::crdbox;
  if (value == "netcdf" || value == "nc" || value == "ncdf" ||
      value == "ncrst")
    return io::TrajectoryFormat::amber_netcdf;
  if (value == "h5md")
    return io::TrajectoryFormat::h5md;
  if (value == "lammps" || value == "lammpstrj" || value == "dump")
    return io::TrajectoryFormat::lammps_dump;
  if (value == "binpos")
    return io::TrajectoryFormat::binpos;
  return io::TrajectoryFormat::auto_detect;
}

io::VolumeFormat volume_format(std::string_view value) {
  if (value == "dx" || value == "opendx")
    return io::VolumeFormat::opendx;
  if (value == "mrc" || value == "map" || value == "ccp4" || value == "mrcs")
    return io::VolumeFormat::mrc;
  return io::VolumeFormat::auto_detect;
}

std::string_view volume_precision_name(model::VolumePrecision precision) {
  return precision == model::VolumePrecision::float32 ? "float32" : "float64";
}

command::Value vector_value(model::Vec3d value) {
  return command::Value::Array{value.x, value.y, value.z};
}

command::Value transform_value(const scene::Transform &transform) {
  return command::Value::Object{
      {"pivot", vector_value(transform.pivot)},
      {"rotation",
       command::Value::Array{transform.rotation.w, transform.rotation.x,
                             transform.rotation.y, transform.rotation.z}},
      {"scale", vector_value(transform.scale)},
      {"translation", vector_value(transform.translation)}};
}

std::string_view projection_name(scene::ProjectionMode projection) {
  return projection == scene::ProjectionMode::orthographic ? "orthographic"
                                                            : "perspective";
}

command::Value camera_value(const scene::CameraParameters &camera) {
  return command::Value::Object{
      {"aspect_ratio", camera.aspect_ratio},
      {"distance", camera.distance},
      {"far_clip", camera.far_clip},
      {"field_of_view_radians", camera.vertical_field_of_view_radians},
      {"near_clip", camera.near_clip},
      {"model_origin", vector_value(camera.model_origin)},
      {"orientation",
       command::Value::Array{camera.orientation.w, camera.orientation.x,
                             camera.orientation.y, camera.orientation.z}},
      {"orthographic_height", camera.orthographic_height},
      {"projection", std::string{projection_name(camera.projection)}},
      {"target", vector_value(camera.target)}};
}

bool implemented_stereo_mode(scene::StereoMode mode) {
  return mode == scene::StereoMode::side_by_side ||
         mode == scene::StereoMode::crosseye ||
         mode == scene::StereoMode::walleye ||
         mode == scene::StereoMode::anaglyph ||
         mode == scene::StereoMode::row_interleaved ||
         mode == scene::StereoMode::column_interleaved ||
         mode == scene::StereoMode::checkerboard;
}

command::Value stereo_value(const scene::StereoParameters &stereo) {
  return command::Value::Object{
      {"angle_scale", stereo.angle_scale},
      {"anaglyph_mode", std::string{scene::to_string(stereo.anaglyph_mode)}},
      {"enabled", stereo.enabled},
      {"mode", std::string{scene::to_string(stereo.mode)}},
      {"shift_percent", stereo.shift_percent},
      {"swap_eyes", stereo.swap_eyes}};
}

command::Value stereo_modes_value(const GraphicsRuntimeInfo &graphics) {
  command::Value::Array modes;
  constexpr std::array all_modes{
      scene::StereoMode::side_by_side, scene::StereoMode::crosseye,
      scene::StereoMode::walleye, scene::StereoMode::anaglyph,
      scene::StereoMode::quad_buffer, scene::StereoMode::row_interleaved,
      scene::StereoMode::column_interleaved,
      scene::StereoMode::checkerboard, scene::StereoMode::openvr};
  for (const auto mode : all_modes) {
    const auto implemented = implemented_stereo_mode(mode);
    const auto runtime_available =
        implemented && graphics.status == RuntimeStatus::ready &&
        graphics.rhi_based;
    std::string reason;
    if (!implemented) {
      reason = "presentation compositor is not implemented";
    } else if (graphics.status != RuntimeStatus::ready) {
      reason = "interactive graphics runtime is not ready";
    } else if (!graphics.rhi_based) {
      reason = "the active renderer is not QRhi-based";
    }
    modes.emplace_back(command::Value::Object{
        {"implemented", implemented},
        {"mode", std::string{scene::to_string(mode)}},
        {"reason", reason.empty() ? command::Value{} : command::Value{reason}},
        {"runtime_available", runtime_available}});
  }
  return modes;
}

command::Value extent_value(const SpatialExtent &extent) {
  return command::Value::Object{
      {"center", vector_value(extent.center)},
      {"evaluated_frame_count",
       static_cast<std::uint64_t>(extent.evaluated_frame_count)},
      {"maximum", vector_value(extent.maximum)},
      {"maximum_radius", extent.maximum_radius},
      {"minimum", vector_value(extent.minimum)},
      {"selected_atom_count",
       static_cast<std::uint64_t>(extent.selected_atom_count)},
      {"skipped_missing_atom_count",
       static_cast<std::uint64_t>(extent.skipped_missing_atom_count)},
      {"used_atom_count", static_cast<std::uint64_t>(extent.used_atom_count)}};
}

command::Value principal_axes_value(
    const analysis::PrincipalAxesResult &principal) {
  command::Value::Array axes;
  axes.reserve(principal.axes.size());
  for (const auto axis : principal.axes)
    axes.emplace_back(vector_value(axis));
  return command::Value::Object{
      {"axes", std::move(axes)},
      {"centroid", vector_value(principal.centroid)},
      {"primary_secondary_degenerate",
       principal.primary_secondary_degenerate},
      {"sample_count",
       static_cast<std::uint64_t>(principal.sample_count)},
      {"secondary_tertiary_degenerate",
       principal.secondary_tertiary_degenerate},
      {"variances",
       command::Value::Array{principal.variances[0], principal.variances[1],
                             principal.variances[2]}}};
}

CameraClipMode clip_mode(std::string_view mode) {
  if (mode == "far") return CameraClipMode::far_relative;
  if (mode == "move") return CameraClipMode::move;
  if (mode == "slab") return CameraClipMode::slab;
  if (mode == "atoms") return CameraClipMode::atoms;
  if (mode == "near-set") return CameraClipMode::near_absolute;
  if (mode == "far-set") return CameraClipMode::far_absolute;
  return CameraClipMode::near_relative;
}

std::string_view clip_mode_name(CameraClipMode mode) {
  switch (mode) {
  case CameraClipMode::near_relative: return "near";
  case CameraClipMode::far_relative: return "far";
  case CameraClipMode::move: return "move";
  case CameraClipMode::slab: return "slab";
  case CameraClipMode::atoms: return "atoms";
  case CameraClipMode::near_absolute: return "near-set";
  case CameraClipMode::far_absolute: return "far-set";
  }
  return "near";
}

scene::CameraAxis camera_axis(std::string_view axis) {
  if (axis == "y") return scene::CameraAxis::y;
  if (axis == "z") return scene::CameraAxis::z;
  return scene::CameraAxis::x;
}

std::string_view camera_axis_name(scene::CameraAxis axis) {
  switch (axis) {
  case scene::CameraAxis::x: return "x";
  case scene::CameraAxis::y: return "y";
  case scene::CameraAxis::z: return "z";
  }
  return "x";
}

command::Value named_view_value(const NamedViewRecord &view) {
  return command::Value::Object{{"camera", camera_value(view.camera)},
                                {"name", view.name}};
}

command::Value named_scene_value(const NamedSceneRecord &scene) {
  return command::Value::Object{
      {"current", scene.current},
      {"molecular_object_count",
       static_cast<std::uint64_t>(scene.molecular_object_count)},
      {"name", scene.name},
      {"volume_object_count",
       static_cast<std::uint64_t>(scene.volume_object_count)}};
}

command::Value movie_timeline_value(const MovieTimelineState &movie) {
  command::Value::Array keyframes;
  keyframes.reserve(movie.keyframes.size());
  for (const auto &[frame, keyframe] : movie.keyframes) {
    command::Value::Object value{{"frame", static_cast<std::uint64_t>(frame)}};
    value.emplace("scene",
                  keyframe.scene_name.has_value()
                      ? command::Value{*keyframe.scene_name}
                      : command::Value{nullptr});
    value.emplace("trajectory_frame",
                  keyframe.trajectory_frame.has_value()
                      ? command::Value{static_cast<std::uint64_t>(
                            *keyframe.trajectory_frame)}
                      : command::Value{nullptr});
    keyframes.emplace_back(std::move(value));
  }
  return command::Value::Object{
      {"current_frame", static_cast<std::uint64_t>(movie.current_frame)},
      {"fps", movie.frames_per_second},
      {"frame_count", static_cast<std::uint64_t>(movie.frame_count)},
      {"keyframes", std::move(keyframes)},
      {"loop", movie.loop},
      {"playing", movie.playing},
      {"security", "typed-scene-trajectory-keys-only"}};
}

command::Response movie_response(std::string message,
                                 const MovieTimelineResult &result) {
  return {std::move(message),
          {{"applied_keyframe_count",
            static_cast<std::uint64_t>(result.applied_keyframe_count)},
           {"movie", movie_timeline_value(result.timeline)}}};
}

operation::Result<double> animation_duration(
    const command::Arguments &arguments) {
  const auto duration = number_argument(arguments, "duration");
  if (!duration.has_value())
    return duration;
  if (duration.value() < 0.0 || duration.value() > 3600.0) {
    return operation::Result<double>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "camera animation duration must be between 0 and 3600 seconds",
        "Use duration 0 for an immediate camera update."});
  }
  return duration;
}

int animation_hand(const command::Arguments &arguments) {
  return arguments.at("hand") == "-1" ? -1
         : arguments.at("hand") == "0" ? 0
                                        : 1;
}

command::Value animation_value(double duration, int hand,
                               const scene::CameraParameters &start,
                               const scene::CameraParameters &end) {
  const auto &effective_start = duration > 0.0 ? start : end;
  return command::Value::Object{
      {"active", duration > 0.0},
      {"committed_endpoint", true},
      {"duration_seconds", duration},
      {"end", camera_value(end)},
      {"hand", hand},
      {"start", camera_value(effective_start)}};
}

operation::Result<scene::CameraParameters> camera_parameters(
    const command::Arguments &arguments,
    scene::CameraParameters parameters) {
  const auto previous_target = parameters.target;
  const auto assign = [&arguments](std::string_view name, double &target)
      -> std::optional<operation::Error> {
    if (!arguments.contains(name))
      return std::nullopt;
    const auto parsed = number_argument(arguments, name);
    if (!parsed.has_value())
      return parsed.error();
    target = parsed.value();
    return std::nullopt;
  };
  for (const auto &[name, target] :
       std::initializer_list<std::pair<std::string_view, double *>>{
           {"target-x", &parameters.target.x},
           {"target-y", &parameters.target.y},
           {"target-z", &parameters.target.z},
           {"model-origin-x", &parameters.model_origin.x},
           {"model-origin-y", &parameters.model_origin.y},
           {"model-origin-z", &parameters.model_origin.z},
           {"orientation-w", &parameters.orientation.w},
           {"orientation-x", &parameters.orientation.x},
           {"orientation-y", &parameters.orientation.y},
           {"orientation-z", &parameters.orientation.z},
           {"distance", &parameters.distance},
           {"field-of-view", &parameters.vertical_field_of_view_radians},
           {"orthographic-height", &parameters.orthographic_height},
           {"aspect-ratio", &parameters.aspect_ratio},
           {"near-clip", &parameters.near_clip},
           {"far-clip", &parameters.far_clip}}) {
    if (const auto error = assign(name, *target); error.has_value())
      return operation::Result<scene::CameraParameters>::failure(*error);
  }
  if (arguments.contains("target-x") &&
      !arguments.contains("model-origin-x")) {
    parameters.model_origin.x += parameters.target.x - previous_target.x;
  }
  if (arguments.contains("target-y") &&
      !arguments.contains("model-origin-y")) {
    parameters.model_origin.y += parameters.target.y - previous_target.y;
  }
  if (arguments.contains("target-z") &&
      !arguments.contains("model-origin-z")) {
    parameters.model_origin.z += parameters.target.z - previous_target.z;
  }
  if (const auto found = arguments.find("projection");
      found != arguments.end()) {
    parameters.projection = found->second == "orthographic"
                                ? scene::ProjectionMode::orthographic
                                : scene::ProjectionMode::perspective;
  }
  return operation::Result<scene::CameraParameters>::success(parameters);
}

command::Value shape_value(model::VolumeShape shape) {
  return command::Value::Array{shape.x, shape.y, shape.z};
}

std::string shape_text(model::VolumeShape shape) {
  return std::to_string(shape.x) + "x" + std::to_string(shape.y) + "x" +
         std::to_string(shape.z);
}

trajectory::PlaybackMode playback_mode(std::string_view value) {
  if (value == "loop")
    return trajectory::PlaybackMode::loop;
  if (value == "rock")
    return trajectory::PlaybackMode::rock;
  return trajectory::PlaybackMode::once;
}

trajectory::PlaybackDirection playback_direction(std::string_view value) {
  return value == "reverse" ? trajectory::PlaybackDirection::reverse
                            : trajectory::PlaybackDirection::forward;
}

std::string_view playback_mode_name(trajectory::PlaybackMode mode) {
  if (mode == trajectory::PlaybackMode::loop)
    return "loop";
  if (mode == trajectory::PlaybackMode::rock)
    return "rock";
  return "once";
}

std::string_view
playback_direction_name(trajectory::PlaybackDirection direction) {
  return direction == trajectory::PlaybackDirection::reverse ? "reverse"
                                                             : "forward";
}

command::Response
trajectory_frame_response(std::string summary,
                          const TrajectoryFrameResult &result) {
  command::Value::Object fields{
      {"direction",
       std::string{playback_direction_name(result.playback.direction)}},
      {"frame", static_cast<std::uint64_t>(result.playback.frame)},
      {"boundary_crossings",
       static_cast<std::uint64_t>(result.boundary_crossings)},
      {"catch_up_limited", result.catch_up_limited},
      {"fps", result.frames_per_second},
      {"mode", std::string{playback_mode_name(result.playback.mode)}},
      {"object_id", result.object_id},
      {"playing", result.playback.playing},
      {"pending_transitions", result.pending_transitions},
      {"prefetch_completed_count",
       static_cast<std::uint64_t>(result.prefetch.completed_count)},
      {"prefetch_generation", result.prefetch.generation},
      {"prefetch_requested_count",
       static_cast<std::uint64_t>(result.prefetch.frame_indices.size())},
      {"prefetch_state",
       std::string{trajectory::to_string(result.prefetch.state)}},
      {"rebuilt_representation_count",
       static_cast<std::uint64_t>(result.rebuilt_representation_count)},
      {"sequence_position",
       static_cast<std::uint64_t>(result.playback.sequence_position)},
      {"sequence_size",
       static_cast<std::uint64_t>(result.playback.sequence_size)},
      {"transitions", static_cast<std::uint64_t>(result.transitions)}};
  if (result.source_step.has_value()) {
    fields.emplace("source_step", *result.source_step);
  }
  if (result.physical_time.has_value()) {
    fields.emplace("physical_time", *result.physical_time);
    fields.emplace("physical_time_unit",
                   result.physical_time_unit.value_or("unspecified"));
  }
  if (result.prefetch.error.has_value()) {
    fields.emplace("prefetch_error", result.prefetch.error->message);
  }
  return {std::move(summary), std::move(fields)};
}

} // namespace

operation::Result<command::Response> persist_sasa_analysis(
    Workspace &workspace, const command::Arguments &arguments,
    SasaAnalysisResult measured) {
  const auto precision =
      static_cast<unsigned int>(std::stoul(arguments.at("precision")));
  const auto probe = number_argument(arguments, "probe-radius").value();
  const auto budget = size_argument(arguments, "evaluation-budget", true).value();
  const auto square_nanometer = arguments.at("unit") == "square-nanometer";
  const auto area_factor = square_nanometer ? 0.01 : 1.0;
  const auto radius_factor = square_nanometer ? 0.1 : 1.0;
  const auto radius_unit = square_nanometer ? "nanometer" : "angstrom";
  const auto &sasa = measured.sasa;
  const auto total =
      rounded(sasa.total_area_square_angstrom * area_factor, precision);
  command::Response response{
      "solvent-accessible surface area calculated",
      {{"evaluation_budget", static_cast<std::uint64_t>(budget)},
       {"ignored_missing_occluders",
        static_cast<std::uint64_t>(sasa.ignored_missing_occluders)},
       {"object_id", measured.object_id},
       {"point_occluder_evaluations",
        static_cast<std::uint64_t>(sasa.point_occluder_evaluations)},
       {"probe_radius_angstrom", probe},
       {"radius_source", measured.radius_source},
       {"samples_per_atom", static_cast<std::uint64_t>(sasa.samples_per_atom)},
       {"selected_atom_count", static_cast<std::uint64_t>(sasa.atoms.size())},
       {"selection", measured.selection_expression},
       {"total_area", total},
       {"unit", arguments.at("unit")}}};
  command::Table table;
  table.columns = {"atom_index", "area", "area_unit", "expanded_radius",
                   "radius_unit", "accessible_samples"};
  table.rows.reserve(sasa.atoms.size());
  for (const auto &atom : sasa.atoms) {
    table.rows.push_back(
        {atom.atom.value + 1U,
         precise(atom.area_square_angstrom * area_factor, precision),
         arguments.at("unit"),
         precise(atom.expanded_radius_angstrom * radius_factor, precision),
         radius_unit,
         static_cast<std::uint64_t>(atom.accessible_sample_count)});
  }
  AnalysisResultDraft draft;
  draft.name = requested_result_name(arguments);
  draft.kind = AnalysisResultKind::sasa;
  draft.provenance.canonical_command = "analyze sasa";
  draft.provenance.canonical_arguments = arguments;
  draft.provenance.scientific = scientific_contract(
      "Shrake-Rupley deterministic Fibonacci sphere sampling",
      analysis::kSasaAlgorithmVersion,
      std::string{length_unit_name(sasa.coordinate_unit)}, arguments.at("unit"),
      precision, "raw", false, "skip",
      sasa.sampling_area_quantum_square_angstrom * area_factor,
      1.0 / static_cast<double>(sasa.samples_per_atom), arguments.at("unit"));
  draft.response = std::move(response);
  draft.export_table = std::move(table);
  return persist_analysis_response(workspace, std::move(draft));
}

operation::Result<command::Response> persist_rdf_analysis(
    Workspace &workspace, const command::Arguments &arguments,
    RdfAnalysisResult analyzed) {
  const auto maximum = number_argument(arguments, "maximum-radius").value();
  const auto width = number_argument(arguments, "bin-width").value();
  const auto budget = size_argument(arguments, "evaluation-budget", true).value();
  const auto boundary = arguments.at("pbc") == "minimum-image"
                            ? analysis::DistanceBoundary::minimum_image
                            : analysis::DistanceBoundary::raw;
  const auto normalization = arguments.at("normalization") == "g-r"
                                 ? analysis::RdfNormalization::radial_distribution
                                 : analysis::RdfNormalization::pair_count;
  const auto target_unit = length_unit(arguments.at("unit"));
  const auto precision =
      static_cast<unsigned int>(std::stoul(arguments.at("precision")));
  const auto &rdf = analyzed.rdf;
  command::Table table;
  table.columns = {"bin_index", "lower", "upper", "center", "pair_count",
                   "value", "distance_unit", "normalization"};
  table.rows.reserve(rdf.bins.size());
  for (std::size_t index = 0; index < rdf.bins.size(); ++index) {
    const auto distance = [&](double value) {
      return precise(convert_length(value, rdf.coordinate_unit, target_unit),
                     precision);
    };
    const auto &bin = rdf.bins[index];
    table.rows.push_back({index, distance(bin.lower), distance(bin.upper),
                          distance(bin.center), bin.pair_count,
                          precise(bin.value, precision),
                          std::string{length_unit_name(target_unit)},
                          arguments.at("normalization")});
  }
  command::Response response{
      "radial distribution calculated",
      {{"bin_count", rdf.bins.size()},
       {"bin_width", precise(width, precision)},
       {"eligible_pair_count", rdf.eligible_pair_count},
       {"evaluated_pair_count", rdf.evaluated_pair_count},
       {"evaluation_budget", static_cast<std::uint64_t>(budget)},
       {"first", analyzed.first_expression},
       {"ignored_missing_atoms", rdf.ignored_missing_atoms},
       {"maximum_radius", precise(maximum, precision)},
       {"normalization", arguments.at("normalization")},
       {"object_id", analyzed.object_id},
       {"pbc", arguments.at("pbc")},
       {"precision", precision},
       {"second", analyzed.second_expression},
       {"unit", std::string{length_unit_name(target_unit)}}},
      table};
  AnalysisResultDraft draft;
  draft.name = requested_result_name(arguments);
  draft.kind = AnalysisResultKind::rdf;
  draft.provenance.canonical_command = "analyze rdf";
  draft.provenance.canonical_arguments = arguments;
  draft.provenance.scientific = scientific_contract(
      normalization == analysis::RdfNormalization::radial_distribution
          ? "eligible-pair shell-volume normalized radial distribution"
          : "half-open radial pair-count histogram",
      analysis::kRdfAlgorithmVersion,
      std::string{length_unit_name(rdf.coordinate_unit)},
      std::string{length_unit_name(target_unit)}, precision,
      arguments.at("pbc"),
      boundary == analysis::DistanceBoundary::minimum_image, "skip",
      normalization == analysis::RdfNormalization::pair_count ? 0.0 : 1.0e-12,
      0.0,
      normalization == analysis::RdfNormalization::pair_count ? "count"
                                                               : "dimensionless");
  draft.response = std::move(response);
  draft.export_table = std::move(table);
  return persist_analysis_response(workspace, std::move(draft));
}

operation::Result<command::Response> persist_rmsd_matrix_analysis(
    Workspace &workspace, const command::Arguments &arguments,
    RmsdMatrixAnalysisResult analyzed) {
  const auto budget = size_argument(arguments, "frame-pair-budget", true).value();
  const auto target_unit = length_unit(arguments.at("unit"));
  const auto precision =
      static_cast<unsigned int>(std::stoul(arguments.at("precision")));
  command::Table table;
  table.columns = {"first_frame", "second_frame", "rmsd", "unit",
                   "selected_atom_count", "paired_atom_count",
                   "skipped_missing_atom_count", "effective_atom_count",
                   "weight_sum", "fit_paired_atom_count", "fit", "weight"};
  table.rows.reserve(analyzed.matrix.upper_triangle.size());
  for (const auto &cell : analyzed.matrix.upper_triangle) {
    const auto value = convert_length(cell.rmsd.rmsd,
                                      cell.rmsd.coordinate_unit, target_unit);
    table.rows.push_back(
        {cell.first_frame, cell.second_frame, precise(value, precision),
         std::string{length_unit_name(target_unit)},
         cell.rmsd.selected_atom_count, cell.rmsd.paired_atom_count,
         cell.rmsd.skipped_missing_atom_count, cell.rmsd.effective_atom_count,
         cell.rmsd.weight_sum, cell.fit_paired_atom_count, arguments.at("fit"),
         arguments.at("weight")});
  }
  command::Value::Object fields{
      {"evaluated_frame_pair_count", analyzed.matrix.evaluated_frame_pair_count},
      {"first_frame", analyzed.range.first},
      {"fit", arguments.at("fit")},
      {"fit_selection", analyzed.fit_selection_expression},
      {"frame_count", analyzed.matrix.frame_indices.size()},
      {"frame_pair_budget", static_cast<std::uint64_t>(budget)},
      {"last_frame", analyzed.range.last},
      {"missing", arguments.at("missing")},
      {"object_id", analyzed.object_id},
      {"precision", precision},
      {"selection", analyzed.selection_expression},
      {"stride", analyzed.range.stride},
      {"triangle", "upper-including-diagonal"},
      {"unit", std::string{length_unit_name(target_unit)}}};
  add_weight_provenance(fields, analyzed.weights);
  command::Response response{"trajectory RMSD matrix calculated",
                             std::move(fields), table};
  AnalysisResultDraft draft;
  draft.name = requested_result_name(arguments);
  draft.kind = AnalysisResultKind::rmsd_matrix;
  draft.provenance.canonical_command = "analyze trajectory rmsd-matrix";
  draft.provenance.canonical_arguments = arguments;
  draft.provenance.scientific = scientific_contract(
      arguments.at("fit") == "rigid"
          ? "all-pairs Horn quaternion rigid-fit RMSD upper triangle"
          : "all-pairs weighted RMSD upper triangle without fit",
      "molshredder-rmsd-matrix-v1", analyzed.matrix.coordinate_unit,
      target_unit, precision, "raw", false, arguments.at("missing"), 1.0e-10,
      1.0e-12, "trajectory_range");
  draft.provenance.frame_first = analyzed.range.first;
  draft.provenance.frame_last = analyzed.range.last;
  draft.provenance.frame_stride = analyzed.range.stride;
  draft.response = std::move(response);
  draft.export_table = std::move(table);
  return persist_analysis_response(workspace, std::move(draft));
}

command::Registry make_default_registry() {
  return make_default_registry(std::make_shared<Workspace>(),
                               std::make_shared<RuntimeDiagnostics>());
}

command::Registry make_default_registry(std::shared_ptr<Workspace> workspace) {
  return make_default_registry(std::move(workspace),
                               std::make_shared<RuntimeDiagnostics>());
}

command::Registry make_default_registry(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<RuntimeDiagnostics> diagnostics) {
  using command::Arguments;
  using command::Descriptor;
  using command::Response;
  using operation::Result;
  using operation::TaskContext;

  if (!diagnostics)
    diagnostics = std::make_shared<RuntimeDiagnostics>();

  command::Registry registry;
  auto molfile_registry = std::make_shared<io::MolfileProviderRegistry>();
  auto molfile_action_mutex = std::make_shared<std::timed_mutex>();
  const auto version_error = registry.add(
      Descriptor{"system version", "Print the MolShredder version", {}, {}},
      [](const Arguments &, TaskContext &) {
        const std::string current_version{molshredder::version()};
        return Result<Response>::success(
            {"MolShredder " + current_version,
             {{"result_schema_version", command::kResultSchemaVersion},
              {"version", current_version}}});
      });
  if (version_error.has_value()) {
    std::terminate();
  }
  const auto version_alias_error =
      registry.add_alias(command::AliasSpec{"version", "system version", {}});
  if (version_alias_error.has_value()) {
    std::terminate();
  }
  const auto info_error = registry.add(
      Descriptor{"system info",
                 "Report the compiled support configuration",
                 {},
                 command::UndoPolicy::not_applicable},
      [diagnostics](const Arguments &, TaskContext &) {
        return Result<Response>::success(
            {"MolShredder support configuration",
             {{"build_configuration",
               std::string{build_support::build_configuration()}},
              {"configuration_schema_version", build_support::schema_version},
              {"dependencies",
               command::Value::Object{
                   {"hdf5", std::string{build_support::hdf5_version}},
                   {"netcdf", std::string{build_support::netcdf_version}},
                   {"python", std::string{build_support::python_version}}}},
              {"features",
               command::Value::Object{
                   {"desktop", build_support::desktop},
                   {"embedded_python", build_support::embedded_python},
                   {"hdf5", build_support::hdf5},
                   {"netcdf", build_support::netcdf},
                   {"thread_sanitizer", build_support::thread_sanitizer}}},
              {"platform",
               command::Value::Object{
                   {"architecture", std::string{build_support::architecture}},
                   {"operating_system",
                    std::string{build_support::operating_system}}}},
              {"project_version",
               std::string{build_support::project_version}},
              {"runtime",
               command::Value::Object{
                   {"graphics",
                    graphics_runtime_value(diagnostics->graphics())}}},
              {"toolchain",
               command::Value::Object{
                   {"compiler_id", std::string{build_support::compiler_id}},
                   {"compiler_version",
                    std::string{build_support::compiler_version}},
                   {"cxx_standard", 20U}}}}});
      });
  if (info_error.has_value()) {
    std::terminate();
  }

  auto descriptors = command::foundation_command_descriptors();
  auto object_descriptors = command::object_command_descriptors();
  auto view_descriptors = command::view_command_descriptors();
  auto file_descriptors = command::file_command_descriptors();
  auto trajectory_descriptors = command::trajectory_command_descriptors();
  auto analysis_result_descriptors =
      command::analysis_result_command_descriptors();
  descriptors.insert(descriptors.end(),
                     std::make_move_iterator(object_descriptors.begin()),
                     std::make_move_iterator(object_descriptors.end()));
  descriptors.insert(descriptors.end(),
                     std::make_move_iterator(view_descriptors.begin()),
                     std::make_move_iterator(view_descriptors.end()));
  descriptors.insert(descriptors.end(),
                     std::make_move_iterator(file_descriptors.begin()),
                     std::make_move_iterator(file_descriptors.end()));
  descriptors.insert(descriptors.end(),
                     std::make_move_iterator(trajectory_descriptors.begin()),
                     std::make_move_iterator(trajectory_descriptors.end()));
  descriptors.insert(
      descriptors.end(),
      std::make_move_iterator(analysis_result_descriptors.begin()),
      std::make_move_iterator(analysis_result_descriptors.end()));
  for (auto descriptor : std::move(descriptors)) {
    const auto canonical_name = descriptor.canonical_name;
    const auto persist_in_session =
        is_session_persistent_descriptor(descriptor);
    command::Handler handler;
    if (canonical_name == "result list") {
      handler = [workspace](const Arguments &, TaskContext &) {
        command::Table table;
        table.columns = {"result_id", "name", "kind", "source_status",
                         "object_id", "topology_version",
                         "coordinate_revision", "created_at_utc",
                         "overlay_visible"};
        for (const auto &record : workspace->analysis_results()) {
          table.rows.push_back(
              {record.result_id, record.name,
               std::string{to_string(record.kind)},
               std::string{to_string(workspace->analysis_source_status(record))},
               record.provenance.scientific.topology.object_id,
               record.provenance.scientific.topology.topology_version,
               record.provenance.scientific.coordinate_revision,
               record.provenance.created_at_utc, record.overlay_visible});
        }
        return Result<Response>::success(
            {"persistent analysis results",
             {{"result_count", workspace->analysis_results().size()}},
             std::move(table)});
      };
    } else if (canonical_name == "result get") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto id = size_argument(arguments, "id", true);
        if (!id.has_value()) return Result<Response>::failure(id.error());
        const auto record = workspace->analysis_result(id.value());
        if (!record.has_value())
          return Result<Response>::failure(record.error());
        return Result<Response>::success(analysis_result_response(
            record.value(), workspace->analysis_source_status(record.value())));
      };
    } else if (canonical_name == "result delete") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto id = size_argument(arguments, "id", true);
        if (!id.has_value()) return Result<Response>::failure(id.error());
        const auto removed = workspace->delete_analysis_result(id.value());
        if (!removed.has_value())
          return Result<Response>::failure(removed.error());
        return Result<Response>::success(
            {"analysis result deleted",
             {{"name", removed.value().name},
              {"result_id", removed.value().result_id},
              {"result_count", workspace->analysis_results().size()}}});
      };
    } else if (canonical_name == "result show" ||
               canonical_name == "result hide") {
      handler = [workspace, canonical_name](const Arguments &arguments,
                                            TaskContext &) {
        const auto id = size_argument(arguments, "id", true);
        if (!id.has_value()) return Result<Response>::failure(id.error());
        const auto changed = workspace->set_analysis_overlay_visible(
            id.value(), canonical_name == "result show");
        if (!changed.has_value())
          return Result<Response>::failure(changed.error());
        return Result<Response>::success(analysis_result_response(
            changed.value(),
            workspace->analysis_source_status(changed.value())));
      };
    } else if (canonical_name == "result export") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if (context.cancellation.is_cancelled())
          return Result<Response>::failure(
              operation::Error{operation::ErrorCode::cancelled,
                               "analysis result export cancelled", {}});
        const auto id = size_argument(arguments, "id", true);
        if (!id.has_value()) return Result<Response>::failure(id.error());
        const auto record = workspace->analysis_result(id.value());
        if (!record.has_value())
          return Result<Response>::failure(record.error());
        const auto format = arguments.at("output-format") == "csv"
                                ? operation::OutputFormat::csv
                                : operation::OutputFormat::json;
        const auto exported = export_analysis_result(
            record.value(), workspace->analysis_source_status(record.value()),
            arguments.at("path"), format,
            arguments.at("overwrite") == "true");
        if (!exported.has_value())
          return Result<Response>::failure(exported.error());
        if (context.report_progress)
          context.report_progress({1.0, "analysis result exported"});
        return Result<Response>::success(
            {"analysis result exported",
             {{"byte_count", exported.value().byte_count},
              {"format", exported.value().format},
              {"loss_item_count",exported.value().losses.size()},
              {"losses",[&]{command::Value::Array values; for(const auto& loss:exported.value().losses) values.emplace_back(loss); return values;}()},
              {"path", exported.value().path.string()},
              {"result_id", exported.value().result_id}}});
      };
    } else if (canonical_name == "view get") {
      handler = [workspace](const Arguments &, TaskContext &) {
        return Result<Response>::success(
            {"Current camera view",
             {{"camera", camera_value(workspace->camera().parameters())}}});
      };
    } else if (canonical_name == "stereo get") {
      handler = [workspace](const Arguments &, TaskContext &) {
        return Result<Response>::success(
            {"Current stereo configuration",
             {{"stereo", stereo_value(workspace->stereo())}}});
      };
    } else if (canonical_name == "stereo modes") {
      handler = [diagnostics](const Arguments &, TaskContext &) {
        const auto graphics = diagnostics->graphics();
        return Result<Response>::success(
            {"Stereo renderer capabilities",
             {{"graphics_status", std::string{to_string(graphics.status)}},
              {"modes", stereo_modes_value(graphics)}}});
      };
    } else if (canonical_name == "stereo set") {
      handler = [workspace, diagnostics](const Arguments &arguments,
                                         TaskContext &) {
        const auto mode = scene::stereo_mode_from_string(arguments.at("mode"));
        if (!mode.has_value()) return Result<Response>::failure(mode.error());
        if (!implemented_stereo_mode(mode.value())) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::unsupported,
              "stereo mode is not implemented by the current renderer: " +
                  std::string{scene::to_string(mode.value())},
              "Use stereo modes to inspect implemented presentation modes."});
        }
        const auto shift = number_argument(arguments, "shift-percent");
        if (!shift.has_value()) return Result<Response>::failure(shift.error());
        const auto angle = number_argument(arguments, "angle-scale");
        if (!angle.has_value()) return Result<Response>::failure(angle.error());
        const auto anaglyph_mode = scene::anaglyph_mode_from_string(
            arguments.at("anaglyph-mode"));
        if (!anaglyph_mode.has_value())
          return Result<Response>::failure(anaglyph_mode.error());
        scene::StereoParameters parameters{
            arguments.at("enabled") == "true", mode.value(),
            arguments.at("swap-eyes") == "true", shift.value(), angle.value(),
            anaglyph_mode.value()};
        const auto configured = workspace->set_stereo(parameters);
        if (!configured.has_value())
          return Result<Response>::failure(configured.error());
        const auto graphics = diagnostics->graphics();
        const auto active = parameters.enabled &&
                            graphics.status == RuntimeStatus::ready &&
                            graphics.rhi_based;
        return Result<Response>::success(
            {"Stereo configuration updated",
             {{"previous", stereo_value(configured.value().previous)},
              {"render_active", active},
              {"stereo", stereo_value(configured.value().current)}}});
      };
    } else if (canonical_name == "view center") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto duration = animation_duration(arguments);
        if (!duration.has_value())
          return Result<Response>::failure(duration.error());
        const auto state_scope = camera_state_scope(arguments);
        if (!state_scope.has_value())
          return Result<Response>::failure(state_scope.error());
        const auto hand = animation_hand(arguments);
        const auto start = workspace->camera().parameters();
        const auto centered = workspace->center_camera(
            arguments.at("selection"), arguments.at("move-origin") == "true",
            state_scope.value(), &context);
        if (!centered.has_value())
          return Result<Response>::failure(centered.error());
        return Result<Response>::success(
            {"Camera centered on selection",
             {{"animation",
               animation_value(duration.value(), hand, start,
                               centered.value().camera.parameters())},
              {"camera", camera_value(centered.value().camera.parameters())},
              {"extent", extent_value(centered.value().extent)},
              {"object_id", centered.value().object_id},
              {"selection", centered.value().selection_expression},
              {"state", camera_state_scope_value(centered.value().state_scope)}}});
      };
    } else if (canonical_name == "view zoom") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto duration = animation_duration(arguments);
        if (!duration.has_value())
          return Result<Response>::failure(duration.error());
        const auto state_scope = camera_state_scope(arguments);
        if (!state_scope.has_value())
          return Result<Response>::failure(state_scope.error());
        const auto buffer = number_argument(arguments, "buffer");
        if (!buffer.has_value())
          return Result<Response>::failure(buffer.error());
        const auto hand = animation_hand(arguments);
        const auto start = workspace->camera().parameters();
        const auto zoomed = workspace->zoom_camera(
            arguments.at("selection"), buffer.value(),
            arguments.at("complete") == "true", state_scope.value(),
            &context);
        if (!zoomed.has_value())
          return Result<Response>::failure(zoomed.error());
        return Result<Response>::success(
            {"Camera framed selection",
             {{"animation",
               animation_value(duration.value(), hand, start,
                               zoomed.value().camera.parameters())},
              {"buffer", buffer.value()},
              {"camera", camera_value(zoomed.value().camera.parameters())},
              {"complete", arguments.at("complete") == "true"},
              {"extent", extent_value(zoomed.value().extent)},
              {"object_id", zoomed.value().object_id},
              {"selection", zoomed.value().selection_expression},
              {"state", camera_state_scope_value(zoomed.value().state_scope)}}});
      };
    } else if (canonical_name == "view orient") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto duration = animation_duration(arguments);
        if (!duration.has_value())
          return Result<Response>::failure(duration.error());
        const auto state_scope = camera_state_scope(arguments);
        if (!state_scope.has_value())
          return Result<Response>::failure(state_scope.error());
        const auto hand = animation_hand(arguments);
        const auto start = workspace->camera().parameters();
        const auto oriented = workspace->orient_camera(
            arguments.at("selection"), state_scope.value(), &context);
        if (!oriented.has_value())
          return Result<Response>::failure(oriented.error());
        return Result<Response>::success(
            {"Camera aligned to selection principal axes",
             {{"animation",
               animation_value(duration.value(), hand, start,
                               oriented.value().camera.parameters())},
              {"camera", camera_value(oriented.value().camera.parameters())},
              {"extent", extent_value(oriented.value().extent)},
              {"object_id", oriented.value().object_id},
              {"oriented_center",
               vector_value(oriented.value().oriented_center)},
              {"oriented_half_extents",
               vector_value(oriented.value().oriented_half_extents)},
              {"principal_axes",
               principal_axes_value(oriented.value().principal_axes)},
              {"selection", oriented.value().selection_expression},
              {"state",
               camera_state_scope_value(oriented.value().state_scope)}}});
      };
    } else if (canonical_name == "view origin") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto object = arguments.find("object");
        const auto position = arguments.find("position");
        if (object != arguments.end()) {
          operation::Result<ObjectOriginResult> updated =
              operation::Result<ObjectOriginResult>::failure(
                  operation::Error{operation::ErrorCode::internal,
                                   "object origin dispatch failed", {}});
          std::string source;
          if (position != arguments.end()) {
            const auto parsed = vector_argument(arguments, "position");
            if (!parsed.has_value())
              return Result<Response>::failure(parsed.error());
            updated = workspace->set_object_origin(object->second,
                                                   parsed.value());
            source = "position";
          } else {
            const auto state_scope = camera_state_scope(arguments);
            if (!state_scope.has_value())
              return Result<Response>::failure(state_scope.error());
            updated = workspace->set_object_origin_from_selection(
                object->second, arguments.at("selection"),
                state_scope.value(), &context);
            source = "selection";
          }
          if (!updated.has_value())
            return Result<Response>::failure(updated.error());
          command::Value::Object fields{
              {"camera", camera_value(workspace->camera().parameters())},
              {"object_id", updated.value().object_id},
              {"object_name", updated.value().object_name},
              {"position", vector_value(updated.value().position)},
              {"scene_version", updated.value().scene_version},
              {"source", source},
              {"target", "object"},
              {"transform", transform_value(updated.value().transform)}};
          if (updated.value().extent.has_value())
            fields.emplace("extent", extent_value(*updated.value().extent));
          if (updated.value().selection_expression.has_value()) {
            fields.emplace("selection",
                           *updated.value().selection_expression);
          }
          if (updated.value().state_scope.has_value()) {
            fields.emplace(
                "state",
                camera_state_scope_value(*updated.value().state_scope));
          }
          return Result<Response>::success(
              {"Object transform origin updated", std::move(fields)});
        }

        if (position != arguments.end()) {
          const auto parsed = vector_argument(arguments, "position");
          if (!parsed.has_value())
            return Result<Response>::failure(parsed.error());
          const auto updated = workspace->set_camera_origin(parsed.value());
          if (!updated.has_value())
            return Result<Response>::failure(updated.error());
          return Result<Response>::success(
              {"Camera model origin set from coordinates",
               {{"camera", camera_value(updated.value().parameters())},
                {"position", vector_value(parsed.value())},
                {"source", "position"},
                {"target", "camera"}}});
        }

        const auto state_scope = camera_state_scope(arguments);
        if (!state_scope.has_value())
          return Result<Response>::failure(state_scope.error());
        const auto updated = workspace->set_camera_origin(
            arguments.at("selection"), state_scope.value(), &context);
        if (!updated.has_value())
          return Result<Response>::failure(updated.error());
        return Result<Response>::success(
            {"Camera model origin set from selection",
             {{"camera", camera_value(updated.value().camera.parameters())},
              {"extent", extent_value(updated.value().extent)},
              {"object_id", updated.value().object_id},
              {"position", vector_value(updated.value().extent.center)},
              {"selection", updated.value().selection_expression},
              {"source", "selection"},
              {"state", camera_state_scope_value(updated.value().state_scope)},
              {"target", "camera"}}});
      };
    } else if (canonical_name == "view reset") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        if (const auto object = arguments.find("object");
            object != arguments.end()) {
          const auto reset = workspace->reset_object_transforms(object->second);
          if (!reset.has_value())
            return Result<Response>::failure(reset.error());
          command::Value::Array object_ids;
          object_ids.reserve(reset.value().object_ids.size());
          for (const auto object_id : reset.value().object_ids)
            object_ids.emplace_back(object_id);
          return Result<Response>::success(
              {"Object transforms reset",
               {{"camera", camera_value(workspace->camera().parameters())},
                {"object_count", static_cast<std::uint64_t>(
                                     reset.value().object_ids.size())},
                {"object_ids", std::move(object_ids)},
                {"object_reference", reset.value().object_reference},
                {"scene_version", reset.value().scene_version},
                {"target", "object"}}});
        }
        const auto duration = animation_duration(arguments);
        if (!duration.has_value())
          return Result<Response>::failure(duration.error());
        const auto hand = animation_hand(arguments);
        const auto start = workspace->camera().parameters();
        const auto reset = workspace->reset_camera();
        if (!reset.has_value())
          return Result<Response>::failure(reset.error());
        command::Value::Object fields{
            {"animation",
             animation_value(duration.value(), hand, start,
                             reset.value().camera.parameters())},
            {"camera", camera_value(reset.value().camera.parameters())},
            {"molecular_object_count",
             static_cast<std::uint64_t>(reset.value().molecular_object_count)},
            {"volume_object_count",
             static_cast<std::uint64_t>(reset.value().volume_object_count)},
            {"target", "camera"}};
        if (reset.value().extent.has_value())
          fields.emplace("extent", extent_value(*reset.value().extent));
        return Result<Response>::success(
            {"Camera reset to visible scene", std::move(fields)});
      };
    } else if (canonical_name == "view clip") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto distance = number_argument(arguments, "distance");
        if (!distance.has_value())
          return Result<Response>::failure(distance.error());
        const auto state_scope = camera_state_scope(arguments);
        if (!state_scope.has_value())
          return Result<Response>::failure(state_scope.error());
        std::optional<std::string> selection;
        if (const auto found = arguments.find("selection");
            found != arguments.end()) {
          selection = found->second;
        }
        const auto clipped = workspace->clip_camera(
            clip_mode(arguments.at("mode")), distance.value(),
            std::move(selection), state_scope.value(), &context);
        if (!clipped.has_value())
          return Result<Response>::failure(clipped.error());
        const auto &parameters = clipped.value().camera.parameters();
        command::Value::Object fields{
            {"camera", camera_value(parameters)},
            {"distance", distance.value()},
            {"far_clip", parameters.far_clip},
            {"mode", std::string{clip_mode_name(clipped.value().mode)}},
            {"near_clip", parameters.near_clip},
            {"state", camera_state_scope_value(clipped.value().state_scope)}};
        if (clipped.value().selection_expression.has_value()) {
          fields.emplace("selection",
                         *clipped.value().selection_expression);
        }
        if (clipped.value().extent.has_value()) {
          fields.emplace(
              "depth_range",
              command::Value::Array{
                  clipped.value().extent->minimum_depth,
                  clipped.value().extent->maximum_depth});
          fields.emplace("extent",
                         extent_value(clipped.value().extent->spatial));
        }
        return Result<Response>::success(
            {"Camera clipping planes updated", std::move(fields)});
      };
    } else if (canonical_name == "view get-clip") {
      handler = [workspace](const Arguments &, TaskContext &) {
        const auto &parameters = workspace->camera().parameters();
        return Result<Response>::success(
            {"Current camera clipping planes",
             {{"camera", camera_value(parameters)},
              {"far_clip", parameters.far_clip},
              {"near_clip", parameters.near_clip},
              {"thickness", parameters.far_clip - parameters.near_clip}}});
      };
    } else if (canonical_name == "view move") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto distance = number_argument(arguments, "distance");
        if (!distance.has_value())
          return Result<Response>::failure(distance.error());
        const auto moved = workspace->move_camera(
            camera_axis(arguments.at("axis")), distance.value());
        if (!moved.has_value())
          return Result<Response>::failure(moved.error());
        return Result<Response>::success(
            {"Camera translated on local axis",
             {{"axis", std::string{camera_axis_name(moved.value().axis)}},
              {"camera", camera_value(moved.value().camera.parameters())},
              {"distance", moved.value().amount}}});
      };
    } else if (canonical_name == "view turn") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto angle = number_argument(arguments, "angle");
        if (!angle.has_value())
          return Result<Response>::failure(angle.error());
        const auto turned = workspace->turn_camera(
            camera_axis(arguments.at("axis")), angle.value());
        if (!turned.has_value())
          return Result<Response>::failure(turned.error());
        return Result<Response>::success(
            {"Camera rotated around model origin",
             {{"angle_degrees", turned.value().amount},
              {"axis", std::string{camera_axis_name(turned.value().axis)}},
              {"camera", camera_value(turned.value().camera.parameters())}}});
      };
    } else if (canonical_name == "view projection") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        std::optional<double> field_of_view_degrees;
        if (arguments.contains("field-of-view-degrees")) {
          const auto parsed =
              number_argument(arguments, "field-of-view-degrees");
          if (!parsed.has_value())
            return Result<Response>::failure(parsed.error());
          field_of_view_degrees = parsed.value();
        }
        const auto mode = arguments.at("mode") == "orthographic"
                              ? scene::ProjectionMode::orthographic
                              : scene::ProjectionMode::perspective;
        const auto projected = workspace->set_camera_projection(
            mode, field_of_view_degrees,
            arguments.at("preserve-scale") == "true");
        if (!projected.has_value())
          return Result<Response>::failure(projected.error());
        return Result<Response>::success(
            {"Camera projection updated",
             {{"camera", camera_value(projected.value().camera.parameters())},
              {"field_of_view_degrees",
               projected.value().field_of_view_degrees},
              {"mode", std::string{projection_name(projected.value().mode)}},
              {"preserve_scale", projected.value().preserve_scale},
              {"previous_mode",
               std::string{projection_name(projected.value().previous_mode)}},
              {"previous_vertical_span",
               projected.value().previous_vertical_span},
              {"vertical_span", projected.value().vertical_span}}});
      };
    } else if (canonical_name == "view set") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto parameters = camera_parameters(
            arguments, workspace->camera().parameters());
        if (!parameters.has_value())
          return Result<Response>::failure(parameters.error());
        const auto updated = workspace->set_camera(parameters.value());
        if (!updated.has_value())
          return Result<Response>::failure(updated.error());
        return Result<Response>::success(
            {"Camera view updated",
             {{"camera", camera_value(updated.value().parameters())}}});
      };
    } else if (canonical_name == "view export-pymol") {
      handler = [workspace](const Arguments &, TaskContext &) {
        const auto exported = scene::to_pymol_view(workspace->camera());
        if (!exported.has_value())
          return Result<Response>::failure(exported.error());
        command::Value::Array values;
        values.reserve(exported.value().values.size());
        for (const auto value : exported.value().values)
          values.emplace_back(value);
        return Result<Response>::success(
            {"PyMOL 18-value view exported",
             {{"camera", camera_value(workspace->camera().parameters())},
              {"layout", "pymol-get-view-18"},
              {"text", scene::format_pymol_view(exported.value())},
              {"values", std::move(values)}}});
      };
    } else if (canonical_name == "view import-pymol") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto duration = animation_duration(arguments);
        if (!duration.has_value())
          return Result<Response>::failure(duration.error());
        const auto hand = animation_hand(arguments);
        const auto start = workspace->camera().parameters();
        const auto parsed = scene::parse_pymol_view(arguments.at("values"));
        if (!parsed.has_value())
          return Result<Response>::failure(parsed.error());
        const auto converted = scene::from_pymol_view(
            parsed.value(), workspace->camera().parameters());
        if (!converted.has_value())
          return Result<Response>::failure(converted.error());
        const auto updated = workspace->set_camera(converted.value().parameters());
        if (!updated.has_value())
          return Result<Response>::failure(updated.error());
        command::Value::Array values;
        values.reserve(parsed.value().values.size());
        for (const auto value : parsed.value().values)
          values.emplace_back(value);
        return Result<Response>::success(
            {"PyMOL 18-value view imported",
             {{"camera", camera_value(updated.value().parameters())},
              {"animation",
               animation_value(duration.value(), hand, start,
                               updated.value().parameters())},
              {"layout", "pymol-get-view-18"},
              {"text", scene::format_pymol_view(parsed.value())},
              {"values", std::move(values)}}});
      };
    } else if (canonical_name == "view list") {
      handler = [workspace](const Arguments &, TaskContext &) {
        command::Value::Array views;
        const auto stored = workspace->list_named_views();
        views.reserve(stored.size());
        for (const auto &view : stored)
          views.emplace_back(named_view_value(view));
        return Result<Response>::success(
            {"Stored camera views",
             {{"count", static_cast<std::uint64_t>(stored.size())},
              {"current", camera_value(workspace->camera().parameters())},
              {"views", std::move(views)}}});
      };
    } else if (canonical_name == "view store") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto stored = workspace->store_named_view(arguments.at("name"));
        if (!stored.has_value())
          return Result<Response>::failure(stored.error());
        return Result<Response>::success(
            {stored.value().replaced ? "Named view replaced"
                                     : "Named view stored",
             {{"count", static_cast<std::uint64_t>(stored.value().view_count)},
              {"replaced", stored.value().replaced},
              {"view", named_view_value(stored.value().view)}}});
      };
    } else if (canonical_name == "view recall") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto duration = animation_duration(arguments);
        if (!duration.has_value())
          return Result<Response>::failure(duration.error());
        const auto hand = animation_hand(arguments);
        const auto start = workspace->camera().parameters();
        const auto recalled =
            workspace->recall_named_view(arguments.at("name"));
        if (!recalled.has_value())
          return Result<Response>::failure(recalled.error());
        return Result<Response>::success(
            {"Named view recalled",
             {{"animation",
               animation_value(duration.value(), hand, start,
                               recalled.value().camera)},
              {"view", named_view_value(recalled.value())}}});
      };
    } else if (canonical_name == "view delete") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto deleted =
            workspace->delete_named_view(arguments.at("name"));
        if (!deleted.has_value())
          return Result<Response>::failure(deleted.error());
        return Result<Response>::success(
            {"Named view deleted",
             {{"count", static_cast<std::uint64_t>(deleted.value().view_count)},
              {"name", deleted.value().name}}});
      };
    } else if (canonical_name == "view clear") {
      handler = [workspace](const Arguments &, TaskContext &) {
        const auto cleared = workspace->clear_named_views();
        return Result<Response>::success(
            {"Named views cleared",
             {{"cleared_all", cleared.cleared_all},
              {"count", static_cast<std::uint64_t>(cleared.view_count)}}});
      };
    } else if (canonical_name == "scene list") {
      handler = [workspace](const Arguments &, TaskContext &) {
        command::Value::Array scenes;
        const auto stored = workspace->list_named_scenes();
        scenes.reserve(stored.size());
        for (const auto &scene : stored)
          scenes.emplace_back(named_scene_value(scene));
        return Result<Response>::success(
            {"Stored named scenes",
             {{"count", static_cast<std::uint64_t>(stored.size())},
              {"scenes", std::move(scenes)}}});
      };
    } else if (canonical_name == "scene store") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto stored = workspace->store_named_scene(arguments.at("name"));
        if (!stored.has_value())
          return Result<Response>::failure(stored.error());
        return Result<Response>::success(
            {stored.value().replaced ? "Named scene replaced"
                                     : "Named scene stored",
             {{"count", static_cast<std::uint64_t>(stored.value().scene_count)},
              {"replaced", stored.value().replaced},
              {"scene", named_scene_value(stored.value().scene)}}});
      };
    } else if (canonical_name == "scene recall") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto recalled =
            workspace->recall_named_scene(arguments.at("name"));
        if (!recalled.has_value())
          return Result<Response>::failure(recalled.error());
        return Result<Response>::success(
            {"Named scene recalled",
             {{"scene", named_scene_value(recalled.value())}}});
      };
    } else if (canonical_name == "scene delete") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto deleted =
            workspace->delete_named_scene(arguments.at("name"));
        if (!deleted.has_value())
          return Result<Response>::failure(deleted.error());
        return Result<Response>::success(
            {"Named scene deleted",
             {{"count", static_cast<std::uint64_t>(deleted.value().scene_count)},
              {"name", deleted.value().name}}});
      };
    } else if (canonical_name == "scene clear") {
      handler = [workspace](const Arguments &, TaskContext &) {
        const auto cleared = workspace->clear_named_scenes();
        return Result<Response>::success(
            {"Named scenes cleared",
             {{"cleared_all", cleared.cleared_all},
              {"count", static_cast<std::uint64_t>(cleared.scene_count)}}});
      };
    } else if (canonical_name == "movie status") {
      handler = [workspace](const Arguments &, TaskContext &) {
        if (!workspace->movie().has_value()) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::not_found,
              "movie timeline is not configured",
              "run movie configure before requesting movie status"});
        }
        return Result<Response>::success(
            {"Movie timeline status",
             {{"movie", movie_timeline_value(*workspace->movie())}}});
      };
    } else if (canonical_name == "movie configure") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto frames = size_argument(arguments, "frames", true);
        const auto fps = number_argument(arguments, "fps");
        if (!frames.has_value()) return Result<Response>::failure(frames.error());
        if (!fps.has_value()) return Result<Response>::failure(fps.error());
        const auto configured = workspace->configure_movie(
            frames.value(), fps.value(), arguments.at("loop") == "true");
        if (!configured.has_value())
          return Result<Response>::failure(configured.error());
        return Result<Response>::success(
            movie_response("Movie timeline configured", configured.value()));
      };
    } else if (canonical_name == "movie keyframe") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto frame = size_argument(arguments, "frame", true);
        if (!frame.has_value()) return Result<Response>::failure(frame.error());
        MovieFrameRecord keyframe;
        keyframe.frame = frame.value();
        if (arguments.contains("scene"))
          keyframe.scene_name = arguments.at("scene");
        if (arguments.contains("trajectory-frame")) {
          const auto trajectory_frame =
              size_argument(arguments, "trajectory-frame");
          if (!trajectory_frame.has_value())
            return Result<Response>::failure(trajectory_frame.error());
          keyframe.trajectory_frame = trajectory_frame.value();
        }
        const auto stored =
            workspace->set_movie_keyframe(std::move(keyframe));
        if (!stored.has_value())
          return Result<Response>::failure(stored.error());
        return Result<Response>::success(
            movie_response("Movie keyframe stored", stored.value()));
      };
    } else if (canonical_name == "movie seek") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto frame = size_argument(arguments, "frame", true);
        if (!frame.has_value()) return Result<Response>::failure(frame.error());
        const auto selected = workspace->seek_movie(frame.value());
        if (!selected.has_value())
          return Result<Response>::failure(selected.error());
        return Result<Response>::success(
            movie_response("Movie frame selected", selected.value()));
      };
    } else if (canonical_name == "movie play" ||
               canonical_name == "movie pause") {
      handler = [workspace, canonical_name](const Arguments &, TaskContext &) {
        const auto changed =
            workspace->play_movie(canonical_name == "movie play");
        if (!changed.has_value())
          return Result<Response>::failure(changed.error());
        return Result<Response>::success(movie_response(
            canonical_name == "movie play" ? "Movie playback started"
                                            : "Movie playback paused",
            changed.value()));
      };
    } else if (canonical_name == "movie step") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto steps = size_argument(arguments, "steps", true);
        if (!steps.has_value()) return Result<Response>::failure(steps.error());
        const auto stepped = workspace->step_movie(steps.value());
        if (!stepped.has_value())
          return Result<Response>::failure(stepped.error());
        return Result<Response>::success(
            movie_response("Movie timeline advanced", stepped.value()));
      };
    } else if (canonical_name == "movie clear") {
      handler = [workspace](const Arguments &, TaskContext &) {
        const auto cleared = workspace->clear_movie();
        if (!cleared.has_value())
          return Result<Response>::failure(cleared.error());
        return Result<Response>::success(
            movie_response("Movie timeline cleared", cleared.value()));
      };
    } else if (canonical_name == "format list") {
      handler = [](const Arguments &arguments, TaskContext &) {
        const auto requested_direction = arguments.at("direction") == "write"
                                             ? io::FormatDirection::write
                                             : io::FormatDirection::read;
        auto selected_provider =
            requested_provider(arguments, requested_direction);
        if (!selected_provider.has_value() &&
            arguments.at("direction") == "all") {
          selected_provider =
              requested_provider(arguments, io::FormatDirection::write);
        }
        if (!selected_provider.has_value()) {
          return Result<Response>::failure(selected_provider.error());
        }
        command::Table table;
        table.columns = {
            "id",        "family",      "extensions",      "read",
            "write",     "multi_frame", "multi_structure", "random_access",
            "streaming", "channels",    "limitations",     "implementation",
            "provider",  "provider_version", "provider_origin",
            "provider_trust", "license_status", "read_unavailable_reason",
            "write_unavailable_reason"};
        const auto array = [](const std::vector<std::string> &values) {
          command::Value::Array result;
          result.reserve(values.size());
          for (const auto &value : values)
            result.emplace_back(value);
          return command::Value{std::move(result)};
        };
        const auto joined = [](const std::vector<std::string> &values) {
          std::string result;
          for (const auto &value : values) {
            if (!result.empty())
              result += ',';
            result += value;
          }
          return result;
        };
        command::Value::Array formats;
        for (const auto &capability : io::format_capabilities()) {
          if (capability.provider.id != selected_provider.value().id) continue;
          if (arguments.at("family") != "all" &&
              arguments.at("family") != capability.family) {
            continue;
          }
          if ((arguments.at("direction") == "read" && !capability.readable) ||
              (arguments.at("direction") == "write" && !capability.writable)) {
            continue;
          }
          table.rows.push_back(
              {capability.id, capability.family, joined(capability.extensions),
               capability.readable, capability.writable, capability.multi_frame,
               capability.multi_structure, capability.random_access,
               capability.streaming, joined(capability.channels),
               joined(capability.limitations), capability.implementation,
               capability.provider.id, capability.provider.version,
               std::string{io::to_string(capability.provider.origin)},
               std::string{io::to_string(capability.provider.trust)},
               std::string{io::to_string(capability.provider.license_status)},
               capability.readable
                   ? command::Value{nullptr}
                   : command::Value{io::direction_capability(
                                        capability, io::FormatDirection::read)
                                        ->unavailable_reason},
               capability.writable
                   ? command::Value{nullptr}
                   : command::Value{io::direction_capability(
                                        capability, io::FormatDirection::write)
                                        ->unavailable_reason}});
          command::Value::Object directions;
          for (const auto &direction : capability.directions) {
            directions.emplace(std::string{io::to_string(direction.direction)},
                               direction_value(direction));
          }
          command::Value::Object extension_fields;
          for (const auto &[name, value] : capability.extension_fields) {
            extension_fields.emplace(name, value);
          }
          formats.emplace_back(command::Value::Object{
              {"channels", array(capability.channels)},
              {"directions", std::move(directions)},
              {"extensions", array(capability.extensions)},
              {"extension_fields", std::move(extension_fields)},
              {"family", capability.family},
              {"id", capability.id},
              {"implementation", capability.implementation},
              {"limitations", array(capability.limitations)},
              {"multi_frame", capability.multi_frame},
              {"multi_structure", capability.multi_structure},
              {"provider", provider_value(capability.provider)},
              {"random_access", capability.random_access},
              {"read", capability.readable},
              {"streaming", capability.streaming},
              {"write", capability.writable}});
        }
        // Capture this before moving the table into the response. Function
        // argument evaluation order is not portable; MSVC may evaluate the
        // moved table argument before the summary/field initializers.
        const auto format_count = table.rows.size();
        return Result<Response>::success(
            {"listed " + std::to_string(format_count) + " formats",
             {{"capability_schema_version", io::kFormatCapabilitySchemaVersion},
             {"direction", arguments.at("direction")},
             {"family", arguments.at("family")},
             {"format_count", static_cast<std::uint64_t>(format_count)},
              {"formats", std::move(formats)},
              {"provider", provider_value(selected_provider.value())}},
             std::move(table)});
      };
    } else if (canonical_name == "session save") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if (context.cancellation.is_cancelled()) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::cancelled,
              "session save cancelled before capture", {}});
        }
        const auto maximum_mib = size_argument(arguments, "maximum-mib", true);
        if (!maximum_mib.has_value())
          return Result<Response>::failure(maximum_mib.error());
        if (maximum_mib.value() == 0U || maximum_mib.value() > 1024U) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "session maximum-mib must be between 1 and 1024", {}});
        }
        std::map<std::string, std::string, std::less<>> extensions;
        if (const auto found = arguments.find("ui-visible-panels");
            found != arguments.end() && !found->second.empty()) {
          extensions.emplace("ui.visible-panels", found->second);
        }
        const auto document = capture_session_document(
            *workspace, std::string{molshredder::version()},
            std::move(extensions));
        if (!document.has_value())
          return Result<Response>::failure(document.error());
        constexpr std::size_t bytes_per_mib = 1024U * 1024U;
        const auto written = write_session_file_atomic(
            arguments.at("path"), document.value(),
            maximum_mib.value() * bytes_per_mib);
        if (!written.has_value())
          return Result<Response>::failure(written.error());
        return Result<Response>::success(
            {"Session saved",
             {{"bytes", static_cast<std::uint64_t>(written.value())},
              {"command_count", static_cast<std::uint64_t>(
                                    document.value().invocations.size())},
              {"path", arguments.at("path")},
              {"schema_version", document.value().schema_version}}});
      };
    } else if (canonical_name == "session load") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto maximum_mib = size_argument(arguments, "maximum-mib", true);
        if (!maximum_mib.has_value())
          return Result<Response>::failure(maximum_mib.error());
        if (maximum_mib.value() == 0U || maximum_mib.value() > 1024U) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "session maximum-mib must be between 1 and 1024", {}});
        }
        const auto has_relink_original = arguments.contains("relink-original");
        const auto has_relink_target = arguments.contains("relink-target");
        if (has_relink_original != has_relink_target) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "session relink-original and relink-target must be provided together",
              "provide one exact old-path/new-path pair or omit both"});
        }
        std::optional<std::filesystem::path> recovery;
        if (arguments.contains("recovery"))
          recovery = arguments.at("recovery");
        constexpr std::size_t bytes_per_mib = 1024U * 1024U;
        const auto loaded = read_session_file_with_recovery(
            arguments.at("path"), recovery,
            maximum_mib.value() * bytes_per_mib);
        if (!loaded.has_value())
          return Result<Response>::failure(loaded.error());
        std::map<std::string, std::filesystem::path, std::less<>> mappings;
        if (has_relink_original) {
          mappings.emplace(arguments.at("relink-original"),
                           arguments.at("relink-target"));
        }
        const auto relinked = relink_session_paths(
            loaded.value().document, mappings,
            loaded.value().source_path.parent_path());
        if (!relinked.has_value())
          return Result<Response>::failure(relinked.error());
        if (!relinked.value().unresolved_paths.empty()) {
          std::string unresolved;
          for (const auto &path : relinked.value().unresolved_paths) {
            if (!unresolved.empty()) unresolved += "; ";
            unresolved += path;
          }
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::not_found,
              "session contains unresolved external paths: " + unresolved,
              "provide an exact relink-original/relink-target mapping"});
        }
        const auto replayed = replay_session_atomically(
            relinked.value().document, workspace, context);
        if (!replayed.has_value())
          return Result<Response>::failure(replayed.error());
        command::Value::Array migration_notes;
        for (const auto &note : loaded.value().document.migration_notes)
          migration_notes.emplace_back(note);
        command::Value::Object extensions;
        for (const auto &[key, value] :
             relinked.value().document.extensions) {
          extensions.emplace(key, value);
        }
        return Result<Response>::success(
            {loaded.value().recovered ? "Session recovered and loaded"
                                      : "Session loaded",
             {{"applied_command_count",
               static_cast<std::uint64_t>(replayed.value().applied_count)},
              {"migration_notes", std::move(migration_notes)},
              {"extensions", std::move(extensions)},
              {"primary_error", loaded.value().primary_error},
              {"recovered", loaded.value().recovered},
              {"relinked_count", static_cast<std::uint64_t>(
                                     relinked.value().relinked_count)},
              {"schema_version", relinked.value().document.schema_version},
              {"source_path", loaded.value().source_path.string()},
              {"source_schema_version",
               relinked.value().document.source_schema_version}}});
      };
    } else if (canonical_name == "session autosave") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if (context.cancellation.is_cancelled()) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::cancelled,
              "session autosave cancelled before capture", {}});
        }
        const auto maximum_mib = size_argument(arguments, "maximum-mib", true);
        if (!maximum_mib.has_value())
          return Result<Response>::failure(maximum_mib.error());
        if (maximum_mib.value() == 0U || maximum_mib.value() > 1024U) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "session maximum-mib must be between 1 and 1024", {}});
        }
        std::map<std::string, std::string, std::less<>> extensions;
        if (const auto found = arguments.find("ui-visible-panels");
            found != arguments.end() && !found->second.empty()) {
          extensions.emplace("ui.visible-panels", found->second);
        }
        const auto document = capture_session_document(
            *workspace, std::string{molshredder::version()},
            std::move(extensions));
        if (!document.has_value())
          return Result<Response>::failure(document.error());
        constexpr std::size_t bytes_per_mib = 1024U * 1024U;
        const auto saved = write_session_autosave_atomic(
            arguments.at("path"), arguments.at("recovery"), document.value(),
            maximum_mib.value() * bytes_per_mib);
        if (!saved.has_value())
          return Result<Response>::failure(saved.error());
        return Result<Response>::success(
            {"Session autosaved",
             {{"bytes", static_cast<std::uint64_t>(saved.value().bytes)},
              {"command_count", static_cast<std::uint64_t>(
                                    document.value().invocations.size())},
              {"path", saved.value().primary_path.string()},
              {"recovery", saved.value().recovery_path.string()},
              {"recovery_updated", saved.value().recovery_updated},
              {"schema_version", document.value().schema_version}}});
      };
    } else if (canonical_name == "volume load") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto provider =
            requested_provider(arguments, io::FormatDirection::read);
        if (!provider.has_value()) return Result<Response>::failure(provider.error());
        std::optional<std::string> name;
        if (const auto found = arguments.find("name");
            found != arguments.end()) {
          name = found->second;
        }
        const auto loaded = workspace->load_volume(
            arguments.at("path"), std::move(name),
            volume_format(arguments.at("file-format")),
            length_unit(arguments.at("coordinate-unit")));
        if (!loaded.has_value())
          return Result<Response>::failure(loaded.error());
        command::Value::Array deltas;
        deltas.reserve(loaded.value().deltas.size());
        for (const auto delta : loaded.value().deltas)
          deltas.emplace_back(vector_value(delta));
        return Result<Response>::success(
            {"loaded volume " + loaded.value().object_name,
             {{"coordinate_unit",
               std::string{length_unit_name(loaded.value().coordinate_unit)}},
              {"deltas", std::move(deltas)},
              {"dimensions", shape_value(loaded.value().shape)},
              {"format", std::string{io::to_string(loaded.value().format)}},
              {"maximum", loaded.value().maximum},
              {"minimum", loaded.value().minimum},
              {"object_id", loaded.value().object_id},
              {"object_name", loaded.value().object_name},
              {"origin", vector_value(loaded.value().origin)},
              {"provider", provider_value(provider.value())},
              {"precision",
               std::string{volume_precision_name(loaded.value().precision)}},
              {"value_count", loaded.value().value_count}}});
      };
    } else if (canonical_name == "volume list") {
      handler = [workspace](const Arguments &, TaskContext &) {
        const auto volumes = workspace->list_volumes();
        command::Table table;
        table.columns = {"object_id",       "name",         "active",
                         "visible",         "dimensions",   "value_count",
                         "precision",       "minimum",      "maximum",
                         "coordinate_unit", "representations",
                         "scene_node_id"};
        command::Value::Array items;
        items.reserve(volumes.size());
        command::Value active_object_id{nullptr};
        for (const auto &volume : volumes) {
          const auto dimensions = shape_value(volume.shape);
          const auto precision =
              std::string{volume_precision_name(volume.precision)};
          const auto unit =
              std::string{length_unit_name(volume.coordinate_unit)};
          table.rows.push_back({volume.id, volume.name, volume.active,
                                volume.visible, shape_text(volume.shape),
                                volume.value_count, precision, volume.minimum,
                                volume.maximum, unit,
                                volume.representation_count,
                                volume.scene_node_id});
          items.emplace_back(command::Value::Object{
              {"active", volume.active},
              {"coordinate_unit", unit},
              {"dimensions", dimensions},
              {"effectively_visible", volume.effectively_visible},
              {"maximum", volume.maximum},
              {"minimum", volume.minimum},
              {"name", volume.name},
              {"object_id", volume.id},
              {"precision", precision},
              {"representation_count", volume.representation_count},
              {"scene_node_id", volume.scene_node_id},
              {"value_count", volume.value_count},
              {"visible", volume.visible}});
          if (volume.active)
            active_object_id = volume.id;
        }
        return Result<Response>::success(
            {"listed " + std::to_string(volumes.size()) + " volumes",
             {{"active_volume_id", std::move(active_object_id)},
              {"volume_count", volumes.size()},
              {"volumes", std::move(items)}},
             std::move(table)});
      };
    } else if (canonical_name == "volume save") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto provider =
            requested_provider(arguments, io::FormatDirection::write);
        if (!provider.has_value()) return Result<Response>::failure(provider.error());
        const auto saved = workspace->save_active_volume(
            arguments.at("path"), volume_format(arguments.at("file-format")),
            arguments.at("overwrite") == "true", context);
        if (!saved.has_value())
          return Result<Response>::failure(saved.error());
        command::Table table;
        table.columns = {"channel", "count", "message"};
        std::uint64_t lost_item_count{};
        for (const auto &loss : saved.value().report.losses) {
          table.rows.push_back({loss.channel, loss.count, loss.message});
          lost_item_count += loss.count;
        }
        return Result<Response>::success(
            {"saved volume " + saved.value().path.string(),
             {{"byte_count", saved.value().report.byte_count},
              {"dimensions", shape_value(saved.value().report.shape)},
              {"format",
               std::string{io::to_string(saved.value().report.format)}},
              {"loss_channel_count",
               static_cast<std::uint64_t>(saved.value().report.losses.size())},
              {"loss_item_count", lost_item_count},
              {"object_id", saved.value().object_id},
              {"path", saved.value().path.string()},
              {"provider", provider_value(provider.value())},
              {"precision", std::string{volume_precision_name(
                                saved.value().report.precision)}},
              {"value_count", saved.value().report.value_count}},
             std::move(table)});
      };
    } else if (canonical_name == "volume isosurface") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto level = number_argument(arguments, "level");
        const auto opacity = number_argument(arguments, "opacity");
        if (!level.has_value()) return Result<Response>::failure(level.error());
        if (!opacity.has_value())
          return Result<Response>::failure(opacity.error());
        if (opacity.value() < 0.0 || opacity.value() > 1.0) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "--opacity must be between 0 and 1",
              "provide an opacity in the inclusive range [0, 1]"});
        }
        const auto color = named_color(arguments.at("color"),
                                       static_cast<float>(opacity.value()));
        const auto shown = workspace->show_volume_isosurface(
            level.value(), color, arguments.at("replace") == "true", context);
        if (!shown.has_value())
          return Result<Response>::failure(shown.error());
        command::Value bounds{nullptr};
        if (!shown.value().bounds.empty) {
          bounds = command::Value::Object{
              {"maximum", vector_value(shown.value().bounds.maximum)},
              {"minimum", vector_value(shown.value().bounds.minimum)}};
        }
        return Result<Response>::success(
            {"created volume isosurface",
             {{"algorithm", "marching-tetrahedra"},
              {"bounds", std::move(bounds)},
              {"color", color_value(shown.value().color)},
              {"level", shown.value().level},
              {"object_id", shown.value().object_id},
              {"representation_index", shown.value().representation_index},
              {"triangle_count", shown.value().triangle_count},
              {"vertex_count", shown.value().vertex_count}}});
      };
    } else if (canonical_name == "volume slice") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto index = size_argument(arguments, "index");
        const auto opacity = number_argument(arguments, "opacity");
        const auto memory_budget =
            size_argument(arguments, "memory-budget-bytes", true);
        if (!index.has_value())
          return Result<Response>::failure(index.error());
        if (!opacity.has_value())
          return Result<Response>::failure(opacity.error());
        if (!memory_budget.has_value())
          return Result<Response>::failure(memory_budget.error());
        if (opacity.value() < 0.0 || opacity.value() > 1.0) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "--opacity must be between 0 and 1",
              "provide an opacity in the inclusive range [0, 1]"});
        }
        const auto alpha = static_cast<float>(opacity.value());
        render::VolumeSliceStyle style;
        style.axis = volume_slice_axis(arguments.at("axis"));
        style.index = index.value();
        style.minimum_color = named_color(arguments.at("minimum-color"), alpha);
        style.maximum_color = named_color(arguments.at("maximum-color"), alpha);
        style.memory_budget_bytes = memory_budget.value();
        const auto shown = workspace->show_volume_slice(
            style, arguments.at("replace") == "true", context);
        if (!shown.has_value())
          return Result<Response>::failure(shown.error());
        command::Value bounds{nullptr};
        if (!shown.value().bounds.empty) {
          bounds = command::Value::Object{
              {"maximum", vector_value(shown.value().bounds.maximum)},
              {"minimum", vector_value(shown.value().bounds.minimum)}};
        }
        return Result<Response>::success(
            {"created volume slice",
             {{"algorithm", "orthogonal-grid-slice"},
              {"algorithm_version", static_cast<std::uint64_t>(1U)},
              {"axis", std::string{render::to_string(shown.value().axis)}},
              {"bounds", std::move(bounds)},
              {"index", static_cast<std::uint64_t>(shown.value().index)},
              {"maximum_color", color_value(shown.value().maximum_color)},
              {"memory_budget_bytes",
               static_cast<std::uint64_t>(shown.value().memory_budget_bytes)},
              {"minimum_color", color_value(shown.value().minimum_color)},
              {"object_id", shown.value().object_id},
              {"pick_target_count",
               static_cast<std::uint64_t>(shown.value().pick_target_count)},
              {"representation_index",
               static_cast<std::uint64_t>(shown.value().representation_index)},
              {"required_bytes",
               static_cast<std::uint64_t>(shown.value().required_bytes)},
              {"triangle_count",
               static_cast<std::uint64_t>(shown.value().triangle_count)},
              {"vertex_count",
               static_cast<std::uint64_t>(shown.value().vertex_count)}}});
      };
    } else if (canonical_name == "volume ramp set") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto updated = workspace->set_volume_ramp(
            transfer_preset(arguments.at("preset")));
        if (!updated.has_value())
          return Result<Response>::failure(updated.error());
        return Result<Response>::success(
            {"updated volume transfer function",
             {{"algorithm", updated.value().algorithm},
              {"algorithm_version",
               static_cast<std::uint64_t>(updated.value().algorithm_version)},
              {"object_id", updated.value().object_id},
              {"points", transfer_points_value(updated.value().points)},
              {"name", updated.value().name}}});
      };
    } else if (canonical_name == "volume ramp define") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        auto transfer = parse_transfer_function(arguments.at("points"));
        if (!transfer.has_value())
          return Result<Response>::failure(transfer.error());
        const auto updated = workspace->define_volume_ramp(
            arguments.at("name"), std::move(transfer.value()));
        if (!updated.has_value())
          return Result<Response>::failure(updated.error());
        return Result<Response>::success(
            {"defined volume transfer function",
             {{"algorithm", updated.value().algorithm},
              {"algorithm_version",
               static_cast<std::uint64_t>(updated.value().algorithm_version)},
              {"object_id", updated.value().object_id},
              {"points", transfer_points_value(updated.value().points)},
              {"name", updated.value().name}}});
      };
    } else if (canonical_name == "volume ramp get") {
      handler = [workspace](const Arguments &, TaskContext &) {
        const auto current = workspace->volume_ramp();
        if (!current.has_value())
          return Result<Response>::failure(current.error());
        return Result<Response>::success(
            {"active volume transfer function",
             {{"algorithm", current.value().algorithm},
              {"algorithm_version",
               static_cast<std::uint64_t>(current.value().algorithm_version)},
              {"object_id", current.value().object_id},
              {"points", transfer_points_value(current.value().points)},
              {"name", current.value().name}}});
      };
    } else if (canonical_name == "volume render") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto sampling_step = number_argument(arguments, "sampling-step");
        const auto maximum_steps = size_argument(arguments, "maximum-steps", true);
        const auto lookup_samples =
            size_argument(arguments, "lookup-table-samples", true);
        const auto texture_budget =
            size_argument(arguments, "texture-budget-bytes", true);
        if (!sampling_step.has_value())
          return Result<Response>::failure(sampling_step.error());
        if (!maximum_steps.has_value())
          return Result<Response>::failure(maximum_steps.error());
        if (!lookup_samples.has_value())
          return Result<Response>::failure(lookup_samples.error());
        if (!texture_budget.has_value())
          return Result<Response>::failure(texture_budget.error());
        render::DirectVolumeStyle style;
        style.mode = arguments.at("mode") == "pre-integrated"
                         ? render::VolumeClassificationMode::pre_integrated
                         : render::VolumeClassificationMode::post_classified;
        style.sampling_step = sampling_step.value();
        style.maximum_steps = maximum_steps.value();
        style.lookup_table_samples = lookup_samples.value();
        style.texture_budget_bytes = texture_budget.value();
        const auto ramp = arguments.at("ramp") == "current"
                              ? std::optional<render::TransferPreset>{}
                              : std::optional<render::TransferPreset>{
                                    transfer_preset(arguments.at("ramp"))};
        const auto shown = workspace->show_direct_volume(
            style, ramp, arguments.at("replace") == "true", context);
        if (!shown.has_value())
          return Result<Response>::failure(shown.error());
        return Result<Response>::success(
            {"prepared direct volume",
             {{"algorithm", "front-to-back-volume-ray-march"},
              {"algorithm_version", static_cast<std::uint64_t>(1U)},
              {"lookup_table_samples",
               static_cast<std::uint64_t>(shown.value().lookup_table_samples)},
              {"maximum_steps",
               static_cast<std::uint64_t>(shown.value().maximum_steps)},
              {"mode", "post-classified"},
              {"object_id", shown.value().object_id},
              {"required_texture_bytes",
               static_cast<std::uint64_t>(shown.value().required_texture_bytes)},
              {"sampling_step", shown.value().sampling_step},
              {"texture_budget_bytes",
               static_cast<std::uint64_t>(shown.value().texture_budget_bytes)},
              {"transfer_function", shown.value().transfer_function_name}}});
      };
    } else if (canonical_name == "volume hide") {
      handler = [workspace](const Arguments &, TaskContext &) {
        const auto hidden = workspace->hide_direct_volume();
        if (!hidden.has_value())
          return Result<Response>::failure(hidden.error());
        return Result<Response>::success(
            {hidden.value() ? "hid direct volume" : "direct volume already hidden",
             {{"removed", hidden.value()}}});
      };
    } else if (canonical_name == "object list") {
      handler = [workspace](const Arguments &, TaskContext &) {
        const auto objects = workspace->list_objects();
        command::Table table;
        table.columns = {"object_id",
                         "name",
                         "active",
                         "visible",
                         "effectively_visible",
                         "atom_count",
                         "frame_count",
                         "representation_count",
                         "has_trajectory",
                         "scene_node_id"};
        table.rows.reserve(objects.size());
        command::Value active_object_id{nullptr};
        for (const auto &object : objects) {
          table.rows.push_back({object.id, object.name, object.active,
                                object.visible, object.effectively_visible,
                                object.atom_count,
                                optional_frame_count(object.frame_count),
                                object.representation_count,
                                object.has_trajectory, object.scene_node_id});
          if (object.active)
            active_object_id = object.id;
        }
        return Result<Response>::success(
            {"listed " + std::to_string(objects.size()) + " objects",
             {{"active_object_id", std::move(active_object_id)},
              {"object_count", objects.size()}},
             std::move(table)});
      };
    } else if (canonical_name == "object chemistry") {
      handler = [workspace](const Arguments &, TaskContext &) {
        const auto *object = workspace->active_object();
        if (object == nullptr) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::not_found,
              "no active molecular object",
              "load or activate a molecular structure first"});
        }
        const auto &topology = *object->system->topology();
        std::size_t formal_charge_present_count{};
        std::size_t isotope_atom_count{};
        std::size_t radical_atom_count{};
        std::size_t atom_stereo_count{};
        std::size_t explicit_atom_annotation_count{};
        std::size_t inferred_atom_annotation_count{};
        std::size_t user_atom_annotation_count{};
        for (const auto &atom : topology.atoms()) {
          formal_charge_present_count +=
              atom.formal_charge_present || atom.formal_charge != 0 ? 1U : 0U;
          isotope_atom_count += atom.isotope_mass_number.has_value() ? 1U : 0U;
          radical_atom_count +=
              atom.radical != model::RadicalState::none ? 1U : 0U;
          atom_stereo_count +=
              atom.stereo_parity != model::AtomStereoParity::unspecified ? 1U
                                                                         : 0U;
          explicit_atom_annotation_count +=
              atom.chemical_origin ==
                      model::ChemicalAnnotationOrigin::explicit_input
                  ? 1U
                  : 0U;
          inferred_atom_annotation_count +=
              atom.chemical_origin == model::ChemicalAnnotationOrigin::inferred
                  ? 1U
                  : 0U;
          user_atom_annotation_count +=
              atom.chemical_origin ==
                      model::ChemicalAnnotationOrigin::user_override
                  ? 1U
                  : 0U;
        }

        std::array<std::size_t, 7U> residue_kinds{};
        std::array<std::size_t, 6U> polymer_types{};
        std::size_t explicit_residue_annotation_count{};
        std::size_t inferred_residue_annotation_count{};
        std::size_t user_residue_annotation_count{};
        for (const auto &residue : topology.residues()) {
          ++residue_kinds[static_cast<std::size_t>(residue.kind)];
          ++polymer_types[static_cast<std::size_t>(residue.polymer_type)];
          explicit_residue_annotation_count +=
              residue.chemical_origin ==
                      model::ChemicalAnnotationOrigin::explicit_input
                  ? 1U
                  : 0U;
          inferred_residue_annotation_count +=
              residue.chemical_origin ==
                      model::ChemicalAnnotationOrigin::inferred
                  ? 1U
                  : 0U;
          user_residue_annotation_count +=
              residue.chemical_origin ==
                      model::ChemicalAnnotationOrigin::user_override
                  ? 1U
                  : 0U;
        }

        constexpr std::size_t bond_order_count = 8U;
        std::array<std::size_t, bond_order_count> bond_orders{};
        std::size_t bond_stereo_count{};
        std::size_t explicit_bond_annotation_count{};
        std::size_t inferred_bond_annotation_count{};
        std::size_t user_bond_annotation_count{};
        for (const auto &bond : topology.bonds()) {
          ++bond_orders[static_cast<std::size_t>(bond.order)];
          bond_stereo_count +=
              bond.stereo != model::BondStereo::none ? 1U : 0U;
          explicit_bond_annotation_count +=
              bond.order_origin ==
                      model::ChemicalAnnotationOrigin::explicit_input
                  ? 1U
                  : 0U;
          inferred_bond_annotation_count +=
              bond.order_origin == model::ChemicalAnnotationOrigin::inferred
                  ? 1U
                  : 0U;
          user_bond_annotation_count +=
              bond.order_origin ==
                      model::ChemicalAnnotationOrigin::user_override
                  ? 1U
                  : 0U;
        }

        const auto *partial_charge = topology.properties().find("partial_charge");
        const auto *partial_charge_present =
            topology.properties().find("partial_charge_present");
        std::size_t partial_charge_present_count{};
        bool partial_charge_presence_known = true;
        if (partial_charge != nullptr) {
          partial_charge_present_count = topology.atom_count();
          if (partial_charge_present != nullptr) {
            if (const auto *mask = std::get_if<model::BooleanColumn>(
                    &partial_charge_present->values)) {
              partial_charge_present_count = static_cast<std::size_t>(
                  std::ranges::count_if(mask->values,
                                        [](std::uint8_t value) {
                                          return value != 0U;
                                        }));
            } else {
              partial_charge_presence_known = false;
              partial_charge_present_count = 0U;
            }
          }
        } else if (partial_charge_present != nullptr) {
          partial_charge_presence_known = false;
        }

        command::Table table;
        table.columns = {"semantic", "count"};
        table.rows = {
            {"formal_charge_present", formal_charge_present_count},
            {"partial_charge_present", partial_charge_present_count},
            {"isotope_atom", isotope_atom_count},
            {"radical_atom", radical_atom_count},
            {"atom_stereo", atom_stereo_count},
            {"bond_unknown", bond_orders[0]},
            {"bond_single", bond_orders[1]},
            {"bond_double", bond_orders[2]},
            {"bond_triple", bond_orders[3]},
            {"bond_aromatic", bond_orders[4]},
            {"bond_amide", bond_orders[5]},
            {"bond_zero", bond_orders[6]},
            {"bond_query", bond_orders[7]},
            {"bond_stereo", bond_stereo_count},
            {"explicit_atom_annotation", explicit_atom_annotation_count},
            {"inferred_atom_annotation", inferred_atom_annotation_count},
            {"user_atom_annotation", user_atom_annotation_count},
            {"explicit_bond_annotation", explicit_bond_annotation_count},
            {"inferred_bond_annotation", inferred_bond_annotation_count},
            {"user_bond_annotation", user_bond_annotation_count}};
        table.rows.insert(
            table.rows.end(),
            {{"residue_unknown", residue_kinds[0]},
             {"residue_amino_acid", residue_kinds[1]},
             {"residue_nucleic_acid", residue_kinds[2]},
             {"residue_carbohydrate", residue_kinds[3]},
             {"residue_solvent", residue_kinds[4]},
             {"residue_ion", residue_kinds[5]},
             {"residue_ligand", residue_kinds[6]},
             {"polymer_protein", polymer_types[1]},
             {"polymer_dna", polymer_types[2]},
             {"polymer_rna", polymer_types[3]},
             {"polymer_carbohydrate", polymer_types[4]},
             {"polymer_other", polymer_types[5]},
             {"explicit_residue_annotation",
              explicit_residue_annotation_count},
             {"inferred_residue_annotation",
              inferred_residue_annotation_count},
             {"user_residue_annotation", user_residue_annotation_count}});
        return Result<Response>::success(
            {"inspected chemical semantics",
             {{"atom_count", topology.atom_count()},
              {"bond_count", topology.bonds().size()},
              {"chemical_semantics_schema_version", 1U},
              {"formal_charge_present_count", formal_charge_present_count},
              {"isotope_atom_count", isotope_atom_count},
              {"object_id", object->id},
              {"object_name", object->system->name()},
              {"partial_charge_presence_known",
               partial_charge_presence_known},
              {"partial_charge_present_count",
               partial_charge_present_count},
              {"radical_atom_count", radical_atom_count},
              {"residue_count", topology.residue_count()},
              {"topology_version", topology.version()}},
             std::move(table)});
      };
    } else if (canonical_name == "object perceive-chemistry") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto radius_scale = number_argument(arguments, "radius-scale");
        const auto pair_budget = size_argument(arguments, "pair-budget", true);
        if (!radius_scale.has_value())
          return Result<Response>::failure(radius_scale.error());
        if (!pair_budget.has_value())
          return Result<Response>::failure(pair_budget.error());
        const auto *object = workspace->active_object();
        if (object == nullptr) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::not_found,
              "no active molecular object",
              "load or activate a molecular structure first"});
        }
        const auto frame_index = object->trajectory.has_value()
                                     ? object->trajectory->timeline.snapshot().frame
                                     : 0U;
        const auto frame = object->system->coordinates()->read_frame(frame_index);
        if (!frame.has_value())
          return Result<Response>::failure(frame.error());
        const auto perceived = model::perceive_chemistry(
            *object->system->topology(), *frame.value(),
            {arguments.at("infer-connectivity") == "true",
             radius_scale.value(), pair_budget.value()},
            &context);
        if (!perceived.has_value())
          return Result<Response>::failure(perceived.error());

        bool applied = false;
        std::uint64_t result_topology_version =
            perceived.value().topology_version;
        std::size_t result_bond_count =
            object->system->topology()->bonds().size();
        const auto proposed_residue_count = static_cast<std::size_t>(
            std::ranges::count_if(perceived.value().residues,
                                  [](const auto &residue) {
                                    return residue.proposed;
                                  }));
        if (arguments.at("apply") == "true" &&
            (!perceived.value().proposed_bonds.empty() ||
             !perceived.value().proposed_bond_order_changes.empty() ||
             proposed_residue_count > 0U)) {
          const auto changed =
              workspace->apply_active_chemical_perception(perceived.value());
          if (!changed.has_value())
            return Result<Response>::failure(changed.error());
          applied = true;
          result_topology_version = changed.value().topology_version;
          result_bond_count = changed.value().bond_count;
        }

        command::Value::Array assumptions;
        for (const auto &entry : perceived.value().assumptions)
          assumptions.emplace_back(entry);
        command::Value::Array warnings;
        for (const auto &entry : perceived.value().warnings)
          warnings.emplace_back(entry);
        std::size_t implicit_hydrogen_count{};
        std::size_t ring_atom_count{};
        std::size_t unsupported_valence_atom_count{};
        for (const auto &atom : perceived.value().atoms) {
          implicit_hydrogen_count += atom.implicit_hydrogen_count;
          ring_atom_count += atom.in_ring ? 1U : 0U;
          unsupported_valence_atom_count += atom.valence_supported ? 0U : 1U;
        }
        command::Table table;
        table.columns = {"first_atom", "second_atom", "order",
                         "distance_angstrom", "origin"};
        table.rows.reserve(perceived.value().proposed_bonds.size());
        for (const auto &bond : perceived.value().proposed_bonds) {
          table.rows.push_back({bond.first.value + 1U, bond.second.value + 1U,
                                bond.order == model::BondOrder::aromatic
                                    ? "aromatic"
                                    : "single",
                                bond.distance_angstrom, "inferred"});
        }
        for (const auto &change : perceived.value().proposed_bond_order_changes) {
          const auto &bond = object->system->topology()->bonds()[change.bond_index];
          table.rows.push_back({bond.first.value + 1U, bond.second.value + 1U,
                                "aromatic", command::Value{nullptr},
                                "inferred-order-change"});
        }
        return Result<Response>::success(
            {"proposed conservative chemical semantics",
             {{"aromatic_ring_edge_count",
               perceived.value().aromatic_ring_count},
              {"applied", applied},
              {"assumptions", std::move(assumptions)},
              {"chemical_perception_schema_version", 1U},
              {"evaluated_pair_count",
               perceived.value().evaluated_pair_count},
              {"frame", frame_index},
              {"implicit_hydrogen_count", implicit_hydrogen_count},
              {"object_id", object->id},
              {"proposed_bond_count",
               perceived.value().proposed_bonds.size()},
              {"proposed_bond_order_change_count",
               perceived.value().proposed_bond_order_changes.size()},
              {"proposed_residue_count", proposed_residue_count},
              {"ring_atom_count", ring_atom_count},
              {"ring_bond_count", perceived.value().ring_bond_count},
              {"result_bond_count", result_bond_count},
              {"result_topology_version", result_topology_version},
              {"rule_set", perceived.value().rule_set},
              {"rule_version", perceived.value().rule_version},
              {"topology_version", perceived.value().topology_version},
              {"unsupported_valence_atom_count",
               unsupported_valence_atom_count},
              {"warnings", std::move(warnings)}},
             std::move(table)});
      };
    } else if (canonical_name == "object activate") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto id = uint64_argument(arguments, "id", true);
        if (!id.has_value())
          return Result<Response>::failure(id.error());
        const auto activated =
            workspace->activate_object(id.value());
        if (!activated.has_value()) {
          return Result<Response>::failure(activated.error());
        }
        return Result<Response>::success(
            {"activated object " + activated.value().name,
             object_fields(activated.value())});
      };
    } else if (canonical_name == "object visibility") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto id = uint64_argument(arguments, "id", true);
        if (!id.has_value())
          return Result<Response>::failure(id.error());
        const auto changed = workspace->set_object_visibility(
            id.value(), arguments.at("visible") == "true");
        if (!changed.has_value()) {
          return Result<Response>::failure(changed.error());
        }
        return Result<Response>::success(
            {changed.value().visible ? "object shown" : "object hidden",
             object_fields(changed.value())});
      };
    } else if (canonical_name == "object rename") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto changed = workspace->rename_object(arguments.at("object"),
                                                      arguments.at("name"));
        if (!changed.has_value())
          return Result<Response>::failure(changed.error());
        return Result<Response>::success(
            {"renamed object " + changed.value().name,
             object_lifecycle_fields(changed.value())});
      };
    } else if (canonical_name == "object delete") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto changed = workspace->delete_object(arguments.at("object"));
        if (!changed.has_value())
          return Result<Response>::failure(changed.error());
        return Result<Response>::success(
            {"deleted object " + changed.value().name,
             object_lifecycle_fields(changed.value())});
      };
    } else if (canonical_name == "object reorder") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto position = size_argument(arguments, "position", true);
        if (!position.has_value())
          return Result<Response>::failure(position.error());
        const auto changed = workspace->reorder_object(
            arguments.at("object"), position.value() - 1U);
        if (!changed.has_value())
          return Result<Response>::failure(changed.error());
        return Result<Response>::success(
            {"reordered object " + changed.value().name,
             object_lifecycle_fields(changed.value())});
      };
    } else if (canonical_name == "object topology-retain") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto ids = atom_id_list_argument(arguments, "atom-ids");
        if (!ids.has_value())
          return Result<Response>::failure(ids.error());
        const auto version =
            uint64_argument(arguments, "expected-version", true);
        if (!version.has_value())
          return Result<Response>::failure(version.error());
        const auto changed =
            workspace->retain_active_atoms(ids.value(), version.value());
        if (!changed.has_value())
          return Result<Response>::failure(changed.error());
        command::Value::Array ordered;
        ordered.reserve(changed.value().ordered_atom_ids.size());
        for (const auto id : changed.value().ordered_atom_ids)
          ordered.emplace_back(id);
        return Result<Response>::success(
            {"updated topology snapshot",
             {{"atom_count", changed.value().atom_count},
              {"invalidated_measurement_count",
               changed.value().invalidated_measurement_count},
              {"object_id", changed.value().object_id},
              {"ordered_atom_ids", std::move(ordered)},
              {"previous_atom_count", changed.value().previous_atom_count},
              {"previous_version", changed.value().previous_version},
              {"removed_atom_count", changed.value().removed_atom_count},
              {"removed_bond_count", changed.value().removed_bond_count},
              {"removed_setting_override_count",
               changed.value().removed_setting_override_count},
              {"topology_version", changed.value().topology_version}}});
      };
    } else if (canonical_name == "edit atom-position") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if (context.cancellation.is_cancelled())
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::cancelled,
              "atom position edit cancelled before commit", {}});
        const auto atom_id = uint64_argument(arguments, "atom-id", true);
        const auto x = number_argument(arguments, "x");
        const auto y = number_argument(arguments, "y");
        const auto z = number_argument(arguments, "z");
        const auto topology =
            uint64_argument(arguments, "expected-topology-version", true);
        const auto source = uint64_argument(
            arguments, "expected-coordinate-source-revision", true);
        if (!atom_id.has_value()) return Result<Response>::failure(atom_id.error());
        if (!x.has_value()) return Result<Response>::failure(x.error());
        if (!y.has_value()) return Result<Response>::failure(y.error());
        if (!z.has_value()) return Result<Response>::failure(z.error());
        if (!topology.has_value()) return Result<Response>::failure(topology.error());
        if (!source.has_value()) return Result<Response>::failure(source.error());
        const auto *object = workspace->active_object();
        if (object == nullptr)
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::not_found,
              "workspace has no active molecular object", {}});
        const auto frame = object->system->coordinates()->read_frame(0U);
        if (!frame.has_value()) return Result<Response>::failure(frame.error());
        const auto requested_unit = length_unit(arguments.at("unit"));
        const auto coordinate_unit = frame.value()->metadata().coordinate_unit;
        const model::Vec3d requested{x.value(), y.value(), z.value()};
        const model::Vec3d canonical{
            convert_length(requested.x, requested_unit, coordinate_unit),
            convert_length(requested.y, requested_unit, coordinate_unit),
            convert_length(requested.z, requested_unit, coordinate_unit)};
        const auto edited = workspace->set_active_atom_position(
            model::AtomId{atom_id.value()}, canonical, topology.value(),
            source.value());
        if (!edited.has_value()) return Result<Response>::failure(edited.error());
        const auto &value = edited.value();
        return Result<Response>::success(
            {"atom position edited",
             {{"diff_schema_version", kEditTransactionDiffSchemaVersion},
              {"transaction_kind",
               std::string{to_string(value.transaction_kind)}},
              {"atom_id", value.atom_id.value},
              {"affected_state_count", value.affected_state_count},
              {"coordinate_revision", value.coordinate_revision},
              {"coordinate_source_revision",
               value.coordinate_source_revision},
              {"evicted_transaction_count", value.evicted_transaction_count},
              {"invalidated_measurement_count",
               value.invalidated_measurement_count},
              {"object_id", value.object_id},
              {"position", command::Value::Array{x.value(), y.value(), z.value()}},
              {"previous_coordinate_revision",
               value.previous_coordinate_revision},
              {"previous_coordinate_source_revision",
               value.previous_coordinate_source_revision},
              {"previous_position",
               command::Value::Array{
                   convert_length(value.previous_position.x, coordinate_unit,
                                  requested_unit),
                   convert_length(value.previous_position.y, coordinate_unit,
                                  requested_unit),
                   convert_length(value.previous_position.z, coordinate_unit,
                                  requested_unit)}},
              {"state_scope", "all"},
              {"transaction_id", value.transaction_id},
              {"undo_bytes", value.undo_bytes},
              {"unit", arguments.at("unit")}}});
      };
    } else if (canonical_name == "edit atom-properties") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if (context.cancellation.is_cancelled())
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::cancelled,
              "atom property edit cancelled before commit", {}});
        const auto atom_id = uint64_argument(arguments, "atom-id", true);
        const auto topology_version =
            uint64_argument(arguments, "expected-topology-version", true);
        const auto source_revision = uint64_argument(
            arguments, "expected-coordinate-source-revision", true);
        if (!atom_id.has_value()) return Result<Response>::failure(atom_id.error());
        if (!topology_version.has_value())
          return Result<Response>::failure(topology_version.error());
        if (!source_revision.has_value())
          return Result<Response>::failure(source_revision.error());
        const auto *object = workspace->active_object();
        if (object == nullptr)
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::not_found,
              "workspace has no active molecular object", {}});
        const auto &topology = *object->system->topology();
        const auto index = topology.atom_index(model::AtomId{atom_id.value()});
        if (!index.has_value())
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "atom property edit stable ID does not exist", {}});
        auto atom = topology.atoms()[index->value];
        const auto previous_atom = atom;
        bool changed{};
        if (const auto found = arguments.find("name"); found != arguments.end()) {
          atom.name = found->second;
          changed = true;
        }
        if (const auto found = arguments.find("atomic-number");
            found != arguments.end()) {
          const auto value = integer_text<unsigned int>(
              found->second, "atomic number");
          if (!value.has_value()) return Result<Response>::failure(value.error());
          if (value.value() > 118U)
            return Result<Response>::failure(operation::Error{
                operation::ErrorCode::invalid_argument,
                "atomic number must be in the range 0-118", {}});
          atom.atomic_number = static_cast<std::uint8_t>(value.value());
          changed = true;
        }
        if (const auto found = arguments.find("formal-charge");
            found != arguments.end()) {
          const auto value = integer_text<std::int32_t>(
              found->second, "formal charge");
          if (!value.has_value()) return Result<Response>::failure(value.error());
          atom.formal_charge = value.value();
          atom.formal_charge_present = true;
          changed = true;
        }
        if (!changed)
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "atom property edit requires at least one changed field", {}});
        atom.chemical_origin = model::ChemicalAnnotationOrigin::user_override;
        auto builder = model::TopologyBuilder::from(topology);
        if (const auto error = builder.set_atom(*index, atom); error.has_value())
          return Result<Response>::failure(*error);
        const auto candidate = builder.build();
        if (!candidate.has_value()) return Result<Response>::failure(candidate.error());
        const auto committed = workspace->commit_active_topology_edit(
            candidate.value(), topology_version.value(),
            source_revision.value(), EditTransactionKind::atom_properties);
        if (!committed.has_value())
          return Result<Response>::failure(committed.error());
        auto fields = topology_edit_fields(committed.value());
        fields.emplace("atom_id", atom_id.value());
        fields.emplace("atomic_number", static_cast<std::uint64_t>(atom.atomic_number));
        fields.emplace("formal_charge", static_cast<std::int64_t>(atom.formal_charge));
        fields.emplace("name", atom.name);
        fields.emplace("previous_atomic_number",
                       static_cast<std::uint64_t>(previous_atom.atomic_number));
        fields.emplace("previous_formal_charge",
                       static_cast<std::int64_t>(previous_atom.formal_charge));
        fields.emplace("previous_formal_charge_present",
                       previous_atom.formal_charge_present);
        fields.emplace("previous_name", previous_atom.name);
        return Result<Response>::success(
            {"atom properties edited", std::move(fields)});
      };
    } else if (canonical_name == "edit residue-properties") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if (context.cancellation.is_cancelled())
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::cancelled,
              "residue property edit cancelled before commit", {}});
        const auto atom_id = uint64_argument(arguments, "atom-id", true);
        const auto topology_version =
            uint64_argument(arguments, "expected-topology-version", true);
        const auto source_revision = uint64_argument(
            arguments, "expected-coordinate-source-revision", true);
        if (!atom_id.has_value()) return Result<Response>::failure(atom_id.error());
        if (!topology_version.has_value())
          return Result<Response>::failure(topology_version.error());
        if (!source_revision.has_value())
          return Result<Response>::failure(source_revision.error());
        const auto *object = workspace->active_object();
        if (object == nullptr)
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::not_found,
              "workspace has no active molecular object", {}});
        const auto &topology = *object->system->topology();
        const auto atom_index =
            topology.atom_index(model::AtomId{atom_id.value()});
        if (!atom_index.has_value())
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "residue property edit atom ID does not exist", {}});
        const auto residue_index =
            topology.atoms()[atom_index->value].residue;
        auto residue = topology.residues()[residue_index.value];
        const auto previous_residue = residue;
        bool changed{};
        if (const auto found = arguments.find("name"); found != arguments.end()) {
          residue.name = found->second;
          changed = true;
        }
        if (const auto found = arguments.find("chain"); found != arguments.end()) {
          residue.chain_id = found->second;
          changed = true;
        }
        if (const auto found = arguments.find("residue-number");
            found != arguments.end()) {
          const auto value = integer_text<std::int64_t>(
              found->second, "residue number");
          if (!value.has_value()) return Result<Response>::failure(value.error());
          residue.sequence_number = value.value();
          changed = true;
        }
        if (!changed)
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "residue property edit requires at least one changed field", {}});
        residue.chemical_origin =
            model::ChemicalAnnotationOrigin::user_override;
        auto builder = model::TopologyBuilder::from(topology);
        if (const auto error = builder.set_residue(residue_index, residue);
            error.has_value())
          return Result<Response>::failure(*error);
        const auto candidate = builder.build();
        if (!candidate.has_value()) return Result<Response>::failure(candidate.error());
        const auto committed = workspace->commit_active_topology_edit(
            candidate.value(), topology_version.value(),
            source_revision.value(), EditTransactionKind::residue_properties);
        if (!committed.has_value())
          return Result<Response>::failure(committed.error());
        auto fields = topology_edit_fields(committed.value());
        fields.emplace("atom_id", atom_id.value());
        fields.emplace("chain", residue.chain_id);
        fields.emplace("name", residue.name);
        fields.emplace("residue_index",
                       static_cast<std::uint64_t>(residue_index.value + 1U));
        fields.emplace("residue_number", residue.sequence_number);
        fields.emplace("previous_chain", previous_residue.chain_id);
        fields.emplace("previous_name", previous_residue.name);
        fields.emplace("previous_residue_number",
                       previous_residue.sequence_number);
        return Result<Response>::success(
            {"residue properties edited", std::move(fields)});
      };
    } else if (canonical_name == "edit bond-order") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if (context.cancellation.is_cancelled())
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::cancelled,
              "bond order edit cancelled before commit", {}});
        const auto bond_id = uint64_argument(arguments, "bond-id", true);
        const auto topology_version =
            uint64_argument(arguments, "expected-topology-version", true);
        const auto source_revision = uint64_argument(
            arguments, "expected-coordinate-source-revision", true);
        if (!bond_id.has_value()) return Result<Response>::failure(bond_id.error());
        if (!topology_version.has_value())
          return Result<Response>::failure(topology_version.error());
        if (!source_revision.has_value())
          return Result<Response>::failure(source_revision.error());
        const auto order = builder_bond_order(arguments.at("order"));
        if (!order.has_value()) return Result<Response>::failure(order.error());
        const auto *object = workspace->active_object();
        if (object == nullptr)
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::not_found,
              "workspace has no active molecular object", {}});
        const auto &topology = *object->system->topology();
        const auto index = topology.bond_index(model::BondId{bond_id.value()});
        if (!index.has_value())
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "bond property edit stable ID does not exist", {}});
        const auto previous_bond = topology.bonds()[*index];
        auto builder = model::TopologyBuilder::from(topology);
        if (const auto error = builder.set_bond_semantics(
                *index, order.value(), model::BondQuery::none,
                model::ChemicalAnnotationOrigin::user_override);
            error.has_value())
          return Result<Response>::failure(*error);
        const auto candidate = builder.build();
        if (!candidate.has_value()) return Result<Response>::failure(candidate.error());
        const auto committed = workspace->commit_active_topology_edit(
            candidate.value(), topology_version.value(),
            source_revision.value(), EditTransactionKind::bond_order);
        if (!committed.has_value())
          return Result<Response>::failure(committed.error());
        auto fields = topology_edit_fields(committed.value());
        fields.emplace("bond_id", bond_id.value());
        fields.emplace("order", arguments.at("order"));
        fields.emplace("previous_order",
                       std::string{bond_order_name(previous_bond.order)});
        return Result<Response>::success(
            {"bond order edited", std::move(fields)});
      };
    } else if (canonical_name == "edit undo" ||
               canonical_name == "edit redo") {
      handler = [workspace, canonical_name](const Arguments &,
                                            TaskContext &context) {
        if (context.cancellation.is_cancelled())
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::cancelled,
              "edit history traversal cancelled before commit", {}});
        const auto changed = canonical_name == "edit undo"
                                 ? workspace->undo_edit()
                                 : workspace->redo_edit();
        if (!changed.has_value()) return Result<Response>::failure(changed.error());
        const auto &value = changed.value();
        command::Value::Object fields{
            {"diff_schema_version", kEditTransactionDiffSchemaVersion},
            {"transaction_kind",
             std::string{to_string(value.transaction_kind)}},
            {"coordinate_revision", value.coordinate_revision},
            {"coordinate_source_revision", value.coordinate_source_revision},
            {"direction", value.direction},
            {"invalidated_measurement_count",
             value.invalidated_measurement_count},
            {"memory_budget_bytes", value.history.memory_budget_bytes},
            {"memory_used_bytes", value.history.memory_used_bytes},
            {"object_id", value.object_id},
            {"redo_count", value.history.redo_count},
            {"transaction_id", value.transaction_id},
            {"undo_count", value.history.undo_count}};
        if (value.history.next_undo_transaction.has_value())
          fields.emplace("next_undo_transaction",
                         *value.history.next_undo_transaction);
        if (value.history.next_redo_transaction.has_value())
          fields.emplace("next_redo_transaction",
                         *value.history.next_redo_transaction);
        return Result<Response>::success(
            {"edit " + value.direction + " committed", std::move(fields)});
      };
    } else if (canonical_name == "edit history") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if (context.cancellation.is_cancelled())
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::cancelled,
              "edit history update cancelled before commit", {}});
        auto status = operation::Result<EditHistoryStatus>::success(
            workspace->edit_history_status());
        if (const auto found = arguments.find("memory-budget-bytes");
            found != arguments.end()) {
          const auto budget =
              size_argument(arguments, "memory-budget-bytes", true);
          if (!budget.has_value()) return Result<Response>::failure(budget.error());
          status = workspace->set_edit_history_budget(budget.value());
        }
        if (!status.has_value()) return Result<Response>::failure(status.error());
        command::Value::Object fields{
            {"memory_budget_bytes", status.value().memory_budget_bytes},
            {"memory_used_bytes", status.value().memory_used_bytes},
            {"redo_count", status.value().redo_count},
            {"undo_count", status.value().undo_count}};
        if (status.value().next_undo_transaction.has_value())
          fields.emplace("next_undo_transaction",
                         *status.value().next_undo_transaction);
        if (status.value().next_redo_transaction.has_value())
          fields.emplace("next_redo_transaction",
                         *status.value().next_redo_transaction);
        return Result<Response>::success(
            {"edit history inspected", std::move(fields)});
      };
    } else if (canonical_name == "build molecule") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if (context.cancellation.is_cancelled())
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::cancelled,
              "molecule builder cancelled before validation", {}});
        const auto residue_number = integer_text<std::int64_t>(
            arguments.at("residue-number"), "residue number");
        const auto budget =
            size_argument(arguments, "memory-budget-bytes", true);
        const auto atom_rows = batch_values(arguments.at("atoms"), "atoms");
        if (!residue_number.has_value())
          return Result<Response>::failure(residue_number.error());
        if (!budget.has_value()) return Result<Response>::failure(budget.error());
        if (!atom_rows.has_value())
          return Result<Response>::failure(atom_rows.error());

        model::MoleculeBuildRequest request;
        request.cancellation_requested = [&context] {
          return context.cancellation.is_cancelled();
        };
        request.coordinate_unit = length_unit(arguments.at("unit"));
        request.memory_budget_bytes = budget.value();
        request.residues.push_back(model::ResidueRecord{
            arguments.at("residue-name"), residue_number.value(), "",
            arguments.at("chain"), "", model::ResidueKind::ligand,
            model::PolymerType::none,
            model::ChemicalAnnotationOrigin::user_override});
        request.atoms.reserve(atom_rows.value().size());
        for (const auto &row : atom_rows.value()) {
          if (context.cancellation.is_cancelled())
            return Result<Response>::failure(operation::Error{
                operation::ErrorCode::cancelled,
                "molecule builder cancelled while parsing atoms", {}});
          const auto fields = delimited_fields(row, ',', "builder atom row");
          if (!fields.has_value())
            return Result<Response>::failure(fields.error());
          if (fields.value().size() != 6U)
            return Result<Response>::failure(operation::Error{
                operation::ErrorCode::invalid_argument,
                "builder atom row must contain 6 comma-separated fields",
                "use name,atomic-number,x,y,z,formal-charge"});
          const auto atomic_number =
              integer_text<unsigned int>(fields.value()[1], "atomic number");
          const auto x = finite_number_text(fields.value()[2], "atom x");
          const auto y = finite_number_text(fields.value()[3], "atom y");
          const auto z = finite_number_text(fields.value()[4], "atom z");
          const auto charge =
              integer_text<std::int32_t>(fields.value()[5], "formal charge");
          if (!atomic_number.has_value())
            return Result<Response>::failure(atomic_number.error());
          if (!x.has_value()) return Result<Response>::failure(x.error());
          if (!y.has_value()) return Result<Response>::failure(y.error());
          if (!z.has_value()) return Result<Response>::failure(z.error());
          if (!charge.has_value()) return Result<Response>::failure(charge.error());
          if (atomic_number.value() > 118U)
            return Result<Response>::failure(operation::Error{
                operation::ErrorCode::invalid_argument,
                "builder atomic number must be in the range 0-118", {}});
          request.atoms.push_back(model::BuilderAtom{
              model::AtomRecord{
                  std::string{fields.value()[0]},
                  static_cast<std::uint8_t>(atomic_number.value()),
                  model::ResidueIndex{0U}, "", charge.value(), std::nullopt,
                  std::nullopt, model::AtomStereoParity::unspecified,
                  model::RadicalState::none, true,
                  model::ChemicalAnnotationOrigin::user_override},
              {x.value(), y.value(), z.value()}});
        }
        if (arguments.at("bonds") != "none") {
          const auto bond_rows =
              batch_values(arguments.at("bonds"), "bonds");
          if (!bond_rows.has_value())
            return Result<Response>::failure(bond_rows.error());
          request.bonds.reserve(bond_rows.value().size());
          for (const auto &row : bond_rows.value()) {
            if (context.cancellation.is_cancelled())
              return Result<Response>::failure(operation::Error{
                  operation::ErrorCode::cancelled,
                  "molecule builder cancelled while parsing bonds", {}});
            const auto fields =
                delimited_fields(row, ',', "builder bond row");
            if (!fields.has_value())
              return Result<Response>::failure(fields.error());
            if (fields.value().size() != 3U)
              return Result<Response>::failure(operation::Error{
                  operation::ErrorCode::invalid_argument,
                  "builder bond row must contain 3 comma-separated fields",
                  "use first-atom,second-atom,order with one-based atoms"});
            const auto first =
                integer_text<std::size_t>(fields.value()[0], "first atom");
            const auto second =
                integer_text<std::size_t>(fields.value()[1], "second atom");
            const auto order = builder_bond_order(fields.value()[2]);
            if (!first.has_value()) return Result<Response>::failure(first.error());
            if (!second.has_value()) return Result<Response>::failure(second.error());
            if (!order.has_value()) return Result<Response>::failure(order.error());
            if (first.value() == 0U || second.value() == 0U)
              return Result<Response>::failure(operation::Error{
                  operation::ErrorCode::invalid_argument,
                  "builder bond atom ordinals are one-based", {}});
            request.bonds.push_back(
                {first.value() - 1U, second.value() - 1U, order.value()});
          }
        }
        const auto built = model::build_molecule(request);
        if (!built.has_value()) return Result<Response>::failure(built.error());
        if (context.cancellation.is_cancelled())
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::cancelled,
              "molecule builder cancelled before commit", {}});
        const auto committed = workspace->commit_built_molecule(
            arguments.at("name"), built.value());
        if (!committed.has_value())
          return Result<Response>::failure(committed.error());
        const auto &loaded = committed.value().loaded;
        if (context.report_progress)
          context.report_progress({1.0, "molecule-builder-commit"});
        command::Value::Array atom_ids;
        for (const auto id : built.value().atom_ids)
          atom_ids.emplace_back(id.value);
        command::Value::Array bond_ids;
        for (const auto id : built.value().bond_ids)
          bond_ids.emplace_back(id.value);
        return Result<Response>::success(
            {"molecular fragment built",
             {{"diff_schema_version", kEditTransactionDiffSchemaVersion},
              {"transaction_kind",
               std::string{to_string(committed.value().transaction_kind)}},
              {"algorithm", "molshredder-molecule-builder"},
              {"algorithm_version", "1"},
              {"atom_count", loaded.atom_count},
              {"atom_ids", std::move(atom_ids)},
              {"bond_count", built.value().bond_ids.size()},
              {"bond_ids", std::move(bond_ids)},
              {"coordinate_source_revision", 1U},
              {"evicted_transaction_count",
               committed.value().evicted_transaction_count},
              {"frame_count", loaded.frame_count},
              {"memory_reserved_bytes", built.value().reserved_bytes},
              {"object_id", loaded.object_id},
              {"object_name", loaded.object_name},
              {"residue_count", built.value().topology->residue_count()},
              {"topology_version", built.value().topology->version()},
              {"transaction_id", committed.value().transaction_id},
              {"undo_bytes", committed.value().undo_bytes},
              {"unit", arguments.at("unit")}}});
      };
    } else if (canonical_name == "load batch") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto provider =
            requested_provider(arguments, io::FormatDirection::read);
        if (!provider.has_value())
          return Result<Response>::failure(provider.error());
        if (provider.value().origin != io::FormatProviderOrigin::native_builtin) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::unsupported,
              "atomic batch load currently requires the native provider",
              "set --provider native"});
        }
        const auto paths = batch_values(arguments.at("paths"), "paths");
        if (!paths.has_value())
          return Result<Response>::failure(paths.error());
        std::vector<std::string> names;
        if (const auto found = arguments.find("names");
            found != arguments.end()) {
          auto parsed = batch_values(found->second, "names");
          if (!parsed.has_value())
            return Result<Response>::failure(parsed.error());
          names = std::move(parsed.value());
          if (names.size() != paths.value().size()) {
            return Result<Response>::failure(operation::Error{
                operation::ErrorCode::invalid_argument,
                "--names count must equal --paths count",
                "omit --names or provide one name for every input"});
          }
        }
        std::vector<StructureLoadRequest> requests;
        requests.reserve(paths.value().size());
        for (std::size_t index = 0; index < paths.value().size(); ++index) {
          requests.push_back(
              {paths.value()[index],
               names.empty() ? std::nullopt
                             : std::optional<std::string>{names[index]},
               structure_format(arguments)});
        }
        auto loaded = workspace->load_structure_batch(requests, context);
        if (!loaded.has_value())
          return Result<Response>::failure(loaded.error());
        command::Value::Array formats;
        for (const auto format : loaded.value().formats)
          formats.emplace_back(std::string{io::to_string(format)});
        command::Value::Array object_ids;
        command::Value::Array loaded_objects;
        for (const auto &object : loaded.value().objects) {
          object_ids.emplace_back(object.object_id);
          loaded_objects.emplace_back(command::Value::Object{
              {"atom_count", static_cast<std::uint64_t>(object.atom_count)},
              {"frame_count", static_cast<std::uint64_t>(object.frame_count)},
              {"object_id", object.object_id},
              {"object_name", object.object_name},
              {"source_record_index",
               static_cast<std::uint64_t>(object.source_record_index)}});
        }
        return Result<Response>::success(
            {"atomically loaded " +
                 std::to_string(loaded.value().objects.size()) +
                 " structure(s)",
             {{"active_object_id", loaded.value().object_id},
              {"active_object_name", loaded.value().object_name},
              {"formats", std::move(formats)},
              {"input_count",
               static_cast<std::uint64_t>(loaded.value().input_count)},
              {"object_ids", std::move(object_ids)},
              {"objects", std::move(loaded_objects)},
              {"provider", provider_value(provider.value())},
              {"structure_count",
               static_cast<std::uint64_t>(loaded.value().objects.size())}}});
      };
    } else if (canonical_name == "load") {
      handler = [workspace, molfile_registry,
                 molfile_action_mutex](const Arguments &arguments,
                                       TaskContext &context) {
        const auto requested = arguments.at("provider");
        const auto is_molfile = requested.starts_with("molfile:");
        operation::Result<io::FormatProvider> provider =
            is_molfile
                ? Result<io::FormatProvider>::failure(operation::Error{
                      operation::ErrorCode::unsupported,
                      "molfile provider is not registered: " + requested, {}})
                : requested_provider(arguments, io::FormatDirection::read);
        std::optional<std::string> name;
        if (const auto found = arguments.find("name");
            found != arguments.end()) {
          name = found->second;
        }
        operation::Result<LoadResult> loaded =
            Result<LoadResult>::failure(operation::Error{
                operation::ErrorCode::internal, "structure load not started", {}});
        if (is_molfile) {
          if (requested != "molfile:pqr") {
            return Result<Response>::failure(operation::Error{
                operation::ErrorCode::unsupported,
                "only molfile:pqr structure staging is currently supported",
                "select --provider molfile:pqr or use the native provider"});
          }
          const auto selected_format = arguments.at("file-format");
          if (selected_format != "auto" && selected_format != "pqr") {
            return Result<Response>::failure(operation::Error{
                operation::ErrorCode::invalid_argument,
                "molfile:pqr requires --file-format pqr or auto",
                "set --file-format pqr"});
          }
          if (context.cancellation.is_cancelled()) {
            return Result<Response>::failure(operation::Error{
                operation::ErrorCode::cancelled,
                "molfile load cancelled during action scheduling", {}});
          }
          std::unique_lock action_lock{*molfile_action_mutex,
                                       std::defer_lock};
          if (context.report_progress) {
            context.report_progress({0.0, "molfile-wait-action"});
          }
          while (!action_lock.try_lock_for(std::chrono::milliseconds{10})) {
            if (context.cancellation.is_cancelled()) {
              return Result<Response>::failure(operation::Error{
                  operation::ErrorCode::cancelled,
                  "molfile load cancelled during action scheduling", {}});
            }
          }
          if (context.cancellation.is_cancelled()) {
            return Result<Response>::failure(operation::Error{
                operation::ErrorCode::cancelled,
                "molfile load cancelled during action scheduling", {}});
          }
          auto registered = molfile_registry->descriptors();
          auto selected = std::find_if(
              registered.begin(), registered.end(), [&](const auto &entry) {
                return entry.provider_id == requested;
              });
          if (selected == registered.end()) {
            const auto plugin_path = arguments.find("plugin-path");
            if (plugin_path == arguments.end()) {
              return Result<Response>::failure(operation::Error{
                  operation::ErrorCode::unsupported,
                  "molfile:pqr requires an explicitly approved plugin path",
                  "provide --plugin-path with the molfile shared library"});
            }
            io::MolfileDiscoveryRequest discovery;
            discovery.explicit_files.emplace_back(plugin_path->second);
            const auto discovered = molfile_registry->discover(discovery);
            if (!discovered.has_value()) {
              return Result<Response>::failure(discovered.error());
            }
            registered = molfile_registry->descriptors();
            selected = std::find_if(
                registered.begin(), registered.end(), [&](const auto &entry) {
                  return entry.provider_id == requested;
                });
          }
          if (selected == registered.end()) {
            return Result<Response>::failure(operation::Error{
                operation::ErrorCode::unsupported,
                "plugin did not register requested provider " + requested,
                "inspect the plugin's registered molfile format name"});
          }
          provider = Result<io::FormatProvider>::success(
              molfile_provider_value(*selected));
          auto document = molfile_registry->read_structure(
              arguments.at("path"), requested, context);
          if (!document.has_value()) {
            return Result<Response>::failure(document.error());
          }
          loaded = workspace->load_structure_document(
              std::move(document.value()), arguments.at("path"),
              std::move(name));
        } else {
          if (!provider.has_value()) {
            return Result<Response>::failure(provider.error());
          }
          loaded = workspace->load_structure(arguments.at("path"),
                                             std::move(name),
                                             structure_format(arguments));
        }
        if (!loaded.has_value())
          return Result<Response>::failure(loaded.error());
        command::Value::Array object_ids;
        command::Value::Array loaded_objects;
        object_ids.reserve(loaded.value().objects.size());
        loaded_objects.reserve(loaded.value().objects.size());
        for (const auto &object : loaded.value().objects) {
          object_ids.emplace_back(object.object_id);
          loaded_objects.emplace_back(command::Value::Object{
              {"atom_count", static_cast<std::uint64_t>(object.atom_count)},
              {"frame_count", static_cast<std::uint64_t>(object.frame_count)},
              {"object_id", object.object_id},
              {"object_name", object.object_name},
              {"source_record_index",
               static_cast<std::uint64_t>(object.source_record_index)}});
        }
        return Result<Response>::success(
            {"loaded " + std::to_string(loaded.value().objects.size()) +
                 " structure(s); active object " + loaded.value().object_name,
             {{"atom_count",
               static_cast<std::uint64_t>(loaded.value().atom_count)},
              {"format", std::string{io::to_string(loaded.value().format)}},
              {"frame_count",
               static_cast<std::uint64_t>(loaded.value().frame_count)},
              {"object_id", loaded.value().object_id},
              {"object_ids", std::move(object_ids)},
              {"object_name", loaded.value().object_name},
              {"objects", std::move(loaded_objects)},
              {"provider", provider_value(provider.value())},
              {"structure_count",
               static_cast<std::uint64_t>(loaded.value().objects.size())}}});
      };
    } else if (canonical_name == "save") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto provider =
            requested_provider(arguments, io::FormatDirection::write);
        if (!provider.has_value()) return Result<Response>::failure(provider.error());
        const auto precision = size_argument(arguments, "precision");
        if (!precision.has_value()) {
          return Result<Response>::failure(precision.error());
        }
        std::string comment;
        if (const auto found = arguments.find("comment");
            found != arguments.end()) {
          comment = found->second;
        }
        const auto saved = workspace->save_active_structure(
            arguments.at("path"), structure_format(arguments),
            arguments.at("frames") == "all",
            static_cast<unsigned int>(precision.value()), std::move(comment),
            arguments.at("overwrite") == "true", context);
        if (!saved.has_value()) {
          return Result<Response>::failure(saved.error());
        }
        command::Table table;
        table.columns = {"channel", "count", "message"};
        std::uint64_t lost_item_count{};
        for (const auto &loss : saved.value().report.losses) {
          table.rows.push_back({loss.channel, loss.count, loss.message});
          lost_item_count += loss.count;
        }
        return Result<Response>::success(
            {"saved " + saved.value().path.string(),
             {{"atom_count",
               static_cast<std::uint64_t>(saved.value().report.atom_count)},
              {"byte_count", saved.value().report.byte_count},
              {"format",
               std::string{io::to_string(saved.value().report.format)}},
              {"frame_count",
               static_cast<std::uint64_t>(saved.value().report.frame_count)},
              {"loss_channel_count",
               static_cast<std::uint64_t>(saved.value().report.losses.size())},
              {"loss_item_count", lost_item_count},
              {"object_id", saved.value().object_id},
              {"path", saved.value().path.string()},
              {"provider", provider_value(provider.value())}},
             std::move(table)});
      };
    } else if (canonical_name == "select") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto error = workspace->set_named_selection(
            arguments.at("name"), arguments.at("expression"),
            arguments.at("update") == "true");
        if (error.has_value())
          return Result<Response>::failure(error.value());
        return Result<Response>::success(
            {"selection " + arguments.at("name") + " defined",
             {{"dynamic", arguments.at("update") == "true"},
              {"expression", arguments.at("expression")},
              {"name", arguments.at("name")}}});
      };
    } else if (canonical_name == "setting list") {
      handler = [workspace](const Arguments &, TaskContext &) {
        command::Table table;
        table.columns = {"id", "name", "type", "default", "minimum",
                         "maximum", "unit", "maximum_scope"};
        for (const auto &definition :
             render::p0_render_setting_definitions()) {
          table.rows.push_back(
              {definition.stable_id, definition.name,
               std::string{render_setting_type_name(definition.value_type)},
               render_setting_value(definition.default_value),
               definition.minimum.has_value()
                   ? command::Value{*definition.minimum}
                   : command::Value{nullptr},
               definition.maximum.has_value()
                   ? command::Value{*definition.maximum}
                   : command::Value{nullptr},
               definition.unit,
               std::string{render_setting_scope_name(
                   definition.maximum_scope)}});
        }
        return Result<Response>::success(
            {"listed " + std::to_string(table.rows.size()) +
                 " P0 render settings",
             {{"catalog_revision",
               std::string{render::kRenderSettingCatalogRevision}},
              {"definition_count",
               static_cast<std::uint64_t>(table.rows.size())},
              {"override_count", static_cast<std::uint64_t>(
                                     workspace->render_settings()
                                         .override_count())}},
             std::move(table)});
      };
    } else if (canonical_name == "setting set" ||
               canonical_name == "setting get" ||
               canonical_name == "setting unset" ||
               canonical_name == "setting reset") {
      handler = [workspace, canonical_name](const Arguments &arguments,
                                             TaskContext &) {
        const auto target = render_setting_target(arguments, *workspace);
        if (!target.has_value())
          return Result<Response>::failure(target.error());
        const auto requested_scope =
            std::string{render_setting_scope_name(target.value().scope.level)};
        if (canonical_name == "setting reset") {
          const auto removed =
              workspace->reset_render_setting_scope(target.value().scope);
          if (!removed.has_value())
            return Result<Response>::failure(removed.error());
          return Result<Response>::success(
              {"render setting scope reset",
               {{"catalog_revision",
                 std::string{render::kRenderSettingCatalogRevision}},
                {"override_count", static_cast<std::uint64_t>(
                                       workspace->render_settings()
                                           .override_count())},
                {"removed_count",
                 static_cast<std::uint64_t>(removed.value())},
                {"scope", requested_scope}}});
        }
        const auto &name = arguments.at("name");
        const auto *definition = workspace->render_settings().definition(name);
        if (definition == nullptr) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "unknown render setting: " + name,
              "use setting list to inspect available names"});
        }
        bool removed{};
        if (canonical_name == "setting set") {
          const auto value =
              parse_render_setting_value(*definition, arguments.at("value"));
          if (!value.has_value())
            return Result<Response>::failure(value.error());
          if (const auto error = workspace->set_render_setting(
                  name, target.value().scope, value.value());
              error.has_value())
            return Result<Response>::failure(*error);
        } else if (canonical_name == "setting unset") {
          const auto result =
              workspace->unset_render_setting(name, target.value().scope);
          if (!result.has_value())
            return Result<Response>::failure(result.error());
          removed = result.value();
        }
        const auto resolved = workspace->resolve_render_setting(
            name, target.value().context);
        if (!resolved.has_value())
          return Result<Response>::failure(resolved.error());
        command::Value source_scope{nullptr};
        if (resolved.value().source_scope.has_value()) {
          source_scope = std::string{render_setting_scope_name(
              resolved.value().source_scope->level)};
        }
        return Result<Response>::success(
            {canonical_name == "setting get"
                 ? "render setting resolved"
             : canonical_name == "setting unset"
                 ? "render setting override removed"
                 : "render setting override applied",
             {{"catalog_revision",
               std::string{render::kRenderSettingCatalogRevision}},
              {"name", name},
              {"override_count", static_cast<std::uint64_t>(
                                     workspace->render_settings()
                                         .override_count())},
              {"removed", removed},
              {"requested_scope", requested_scope},
              {"source_scope", std::move(source_scope)},
              {"type",
               std::string{render_setting_type_name(definition->value_type)}},
              {"value", render_setting_value(resolved.value().value)}}});
      };
    } else if (canonical_name == "surface show") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto probe_radius = number_argument(arguments, "probe-radius");
        const auto grid_spacing = number_argument(arguments, "grid-spacing");
        const auto opacity = number_argument(arguments, "opacity");
        const auto voxel_budget =
            size_argument(arguments, "voxel-budget", true);
        const auto memory_budget =
            size_argument(arguments, "memory-budget-bytes", true);
        if (!probe_radius.has_value())
          return Result<Response>::failure(probe_radius.error());
        if (!grid_spacing.has_value())
          return Result<Response>::failure(grid_spacing.error());
        if (!opacity.has_value())
          return Result<Response>::failure(opacity.error());
        if (!voxel_budget.has_value())
          return Result<Response>::failure(voxel_budget.error());
        if (!memory_budget.has_value())
          return Result<Response>::failure(memory_budget.error());
        if (opacity.value() < 0.0 || opacity.value() > 1.0) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "--opacity must be between 0 and 1",
              "provide an opacity in the inclusive range [0, 1]"});
        }
        render::MolecularSurfaceStyle style;
        style.kind = molecular_surface_kind(arguments.at("kind"));
        style.probe_radius_angstrom = probe_radius.value();
        style.grid_spacing_angstrom = grid_spacing.value();
        style.color = named_color(arguments.at("color"),
                                  static_cast<float>(opacity.value()));
        style.voxel_budget = voxel_budget.value();
        style.memory_budget_bytes = memory_budget.value();
        const auto shown = workspace->show_molecular_surface(
            arguments.at("selection"), style, context);
        if (!shown.has_value())
          return Result<Response>::failure(shown.error());
        command::Value bounds{nullptr};
        if (!shown.value().bounds.empty) {
          bounds = command::Value::Object{
              {"maximum", vector_value(shown.value().bounds.maximum)},
              {"minimum", vector_value(shown.value().bounds.minimum)}};
        }
        return Result<Response>::success(
            {"created molecular surface",
             {{"algorithm", "union-sphere-signed-distance"},
              {"algorithm_version", "1"},
              {"bounds", std::move(bounds)},
              {"grid_spacing_angstrom", shown.value().grid_spacing_angstrom},
              {"kind", std::string{render::to_string(shown.value().kind)}},
              {"memory_budget_bytes",
               static_cast<std::uint64_t>(shown.value().memory_budget_bytes)},
              {"object_id", shown.value().object_id},
              {"pick_target_count",
               static_cast<std::uint64_t>(shown.value().pick_target_count)},
              {"probe_radius_angstrom", shown.value().probe_radius_angstrom},
              {"selection", shown.value().selection_expression},
              {"triangle_count",
               static_cast<std::uint64_t>(shown.value().triangle_count)},
              {"vertex_count",
               static_cast<std::uint64_t>(shown.value().vertex_count)},
              {"voxel_budget",
               static_cast<std::uint64_t>(shown.value().voxel_budget)},
              {"voxel_count",
               static_cast<std::uint64_t>(shown.value().voxel_count)}}});
      };
    } else if (canonical_name == "surface hide") {
      handler = [workspace](const Arguments &, TaskContext &) {
        const auto hidden = workspace->hide_molecular_surface();
        if (!hidden.has_value())
          return Result<Response>::failure(hidden.error());
        return Result<Response>::success(
            {hidden.value() ? "hid molecular surface"
                            : "molecular surface already hidden",
             {{"hidden", hidden.value()}}});
      };
    } else if (canonical_name == "show" || canonical_name == "hide" ||
               canonical_name == "as" || canonical_name == "toggle") {
      handler = [workspace, canonical_name](const Arguments &arguments,
                                             TaskContext &) {
        const auto replace_argument = arguments.find("replace");
        const auto replace = replace_argument != arguments.end() &&
                             replace_argument->second == "true";
        auto mutation = RepresentationVisibilityMutation::show;
        if (canonical_name == "hide")
          mutation = RepresentationVisibilityMutation::hide;
        else if (canonical_name == "as" || replace)
          mutation = RepresentationVisibilityMutation::exclusive;
        else if (canonical_name == "toggle")
          mutation = RepresentationVisibilityMutation::toggle;
        const auto kinds =
            representation_kinds(arguments.at("representation"));
        const auto changed = workspace->mutate_representation_visibility(
            kinds, arguments.at("selection"), mutation);
        if (!changed.has_value())
          return Result<Response>::failure(changed.error());
        std::size_t primitive_count{};
        for (const auto &representation :
             workspace->active_object()->representations) {
          if (std::find(kinds.begin(), kinds.end(), representation.kind) ==
              kinds.end())
            continue;
          primitive_count += representation.packet.lines.size() +
                             representation.packet.cylinders.size() +
                             representation.packet.spheres.size() +
                             representation.packet.mesh_triangles.size();
        }
        return Result<Response>::success(
            {"representation visibility updated",
             {{"affected_atom_count",
               static_cast<std::uint64_t>(
                   changed.value().affected_atom_count)},
              {"object_id", changed.value().object_id},
              {"operation", canonical_name == "show" && replace
                                ? std::string{"as"}
                                : canonical_name},
              {"primitive_count",
               static_cast<std::uint64_t>(primitive_count)},
              {"representation", arguments.at("representation")},
              {"representation_count",
               static_cast<std::uint64_t>(
                   changed.value().representation_count)},
              {"resolved_representations",
               resolved_representation_names(kinds)},
              {"selection", arguments.at("selection")},
              {"visible_membership_count",
               static_cast<std::uint64_t>(
                   changed.value().visible_atom_count)}}});
      };
    } else if (canonical_name == "analyze center") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        if (const auto error = validate_result_name(*workspace, arguments);
            error.has_value())
          return Result<Response>::failure(*error);
        const auto mode = arguments.at("mode") == "com"
                              ? analysis::CenterMode::center_of_mass
                              : analysis::CenterMode::centroid;
        const auto analyzed =
            workspace->analyze_center(arguments.at("selection"), mode);
        if (!analyzed.has_value()) {
          return Result<Response>::failure(analyzed.error());
        }
        const auto target_unit = length_unit(arguments.at("unit"));
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        const auto &center = analyzed.value().center;
        const auto component = [&](double value) {
          return rounded(
              convert_length(value, center.coordinate_unit, target_unit),
              precision);
        };
        command::Value::Object fields{
            {"mode", arguments.at("mode")},
            {"object_id", analyzed.value().object_id},
            {"position", command::Value::Array{component(center.position.x),
                                               component(center.position.y),
                                               component(center.position.z)}},
            {"precision", precision},
            {"selected_atom_count",
             static_cast<std::uint64_t>(center.selected_atom_count)},
            {"selection", analyzed.value().selection_expression},
            {"skipped_missing_atom_count",
             static_cast<std::uint64_t>(center.skipped_missing_atom_count)},
            {"unit", std::string{length_unit_name(target_unit)}},
            {"used_atom_count",
             static_cast<std::uint64_t>(center.used_atom_count)}};
        if (center.total_mass.has_value()) {
          fields.emplace("mass_estimated", center.masses_estimated);
          fields.emplace("mass_source", center.mass_source);
          fields.emplace("mass_unit", center.mass_unit.value_or("unspecified"));
          fields.emplace("total_mass", center.total_mass.value());
        }
        Response response{arguments.at("mode") + " calculated",
                          std::move(fields)};
        command::Table export_table;
        export_table.columns = {"component", "value", "unit"};
        export_table.rows = {
            {"x", precise(component(center.position.x), precision),
             std::string{length_unit_name(target_unit)}},
            {"y", precise(component(center.position.y), precision),
             std::string{length_unit_name(target_unit)}},
            {"z", precise(component(center.position.z), precision),
             std::string{length_unit_name(target_unit)}}};
        AnalysisResultDraft draft;
        draft.name = requested_result_name(arguments);
        draft.kind = AnalysisResultKind::center;
        draft.provenance.canonical_command = "analyze center";
        draft.provenance.canonical_arguments = arguments;
        draft.provenance.scientific = scientific_contract(
            mode == analysis::CenterMode::center_of_mass
                ? "compensated weighted center"
                : "compensated arithmetic center",
            "molshredder-center-v1", center.coordinate_unit, target_unit,
            precision, "not_applicable", false, "error");
        draft.response = response;
        draft.export_table = std::move(export_table);
        draft.overlay = PointAnalysisOverlay{
            center.position,
            arguments.at("mode") + " result in " +
                std::string{length_unit_name(target_unit)}};
        return persist_analysis_response(*workspace, std::move(draft));
      };
    } else if (canonical_name == "measure distance") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        if (const auto error = validate_result_name(*workspace, arguments);
            error.has_value())
          return Result<Response>::failure(*error);
        if (arguments.at("mode") != "atom") {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::unsupported,
              "distance reduction mode is not implemented yet: " +
                  arguments.at("mode"),
              "use --mode atom with one atom per endpoint"});
        }
        const auto boundary = arguments.at("pbc") == "minimum-image"
                                  ? analysis::DistanceBoundary::minimum_image
                                  : analysis::DistanceBoundary::raw;
        const auto measured = workspace->measure_distance(
            arguments.at("from"), arguments.at("to"), boundary);
        if (!measured.has_value()) {
          return Result<Response>::failure(measured.error());
        }
        const auto target_unit = length_unit(arguments.at("unit"));
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        const auto &distance = measured.value().distance;
        const auto component = [&](double value) {
          return rounded(
              convert_length(value, distance.coordinate_unit, target_unit),
              precision);
        };
        Response response{
            "distance measured",
            {{"displacement",
               command::Value::Array{component(distance.displacement.x),
                                     component(distance.displacement.y),
                                     component(distance.displacement.z)}},
              {"distance", component(distance.distance)},
              {"first_atom_index",
               static_cast<std::uint64_t>(distance.first.value + 1U)},
              {"from", measured.value().from_expression},
              {"measurement_id", measured.value().measurement_id},
              {"object_id", measured.value().object_id},
              {"pbc", arguments.at("pbc")},
              {"precision", precision},
              {"second_atom_index",
               static_cast<std::uint64_t>(distance.second.value + 1U)},
              {"to", measured.value().to_expression},
              {"unit", std::string{length_unit_name(target_unit)}}}};
        const auto label = std::to_string(component(distance.distance)) + " " +
                           std::string{length_unit_name(target_unit)};
        const auto overlay =
            workspace->distance_analysis_overlay(measured.value(), label);
        if (!overlay.has_value())
          return Result<Response>::failure(overlay.error());
        command::Table export_table;
        export_table.columns = {"first_atom_index", "second_atom_index",
                                "distance", "unit", "pbc"};
        export_table.rows = {{distance.first.value + 1U,
                              distance.second.value + 1U,
                              precise(component(distance.distance), precision),
                              std::string{length_unit_name(target_unit)},
                              arguments.at("pbc")}};
        AnalysisResultDraft draft;
        draft.name = requested_result_name(arguments);
        draft.kind = AnalysisResultKind::distance;
        draft.provenance.canonical_command = "measure distance";
        draft.provenance.canonical_arguments = arguments;
        draft.provenance.scientific = scientific_contract(
            boundary == analysis::DistanceBoundary::minimum_image
                ? "exact triclinic closest-lattice distance"
                : "Euclidean atom distance",
            "molshredder-distance-v1", distance.coordinate_unit, target_unit,
            precision, arguments.at("pbc"),
            boundary == analysis::DistanceBoundary::minimum_image, "error");
        draft.response = response;
        draft.export_table = std::move(export_table);
        draft.overlay = overlay.value();
        return persist_analysis_response(*workspace, std::move(draft));
      };
    } else if (canonical_name == "measure angle") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        if (const auto error = validate_result_name(*workspace, arguments);
            error.has_value())
          return Result<Response>::failure(*error);
        const auto boundary = arguments.at("pbc") == "minimum-image"
                                  ? analysis::DistanceBoundary::minimum_image
                                  : analysis::DistanceBoundary::raw;
        const auto measured = workspace->analyze_angle(
            arguments.at("first"), arguments.at("vertex"),
            arguments.at("third"), boundary);
        if (!measured.has_value())
          return Result<Response>::failure(measured.error());
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        const auto &angle = measured.value().angle;
        const auto value = rounded(angle.angle_degrees, precision);
        Response response{
            "angle measured",
            {{"angle_degrees", value},
             {"first", measured.value().first_expression},
             {"first_atom_index",
              static_cast<std::uint64_t>(angle.first.value + 1U)},
             {"object_id", measured.value().object_id},
             {"pbc", arguments.at("pbc")},
             {"precision", precision},
             {"third", measured.value().third_expression},
             {"third_atom_index",
              static_cast<std::uint64_t>(angle.third.value + 1U)},
             {"unit", "degree"},
             {"vertex", measured.value().vertex_expression},
             {"vertex_atom_index",
              static_cast<std::uint64_t>(angle.vertex.value + 1U)}}};
        command::Table table;
        table.columns = {"first_atom_index", "vertex_atom_index",
                         "third_atom_index", "angle_degrees", "pbc"};
        table.rows = {{angle.first.value + 1U, angle.vertex.value + 1U,
                       angle.third.value + 1U, precise(value, precision),
                       arguments.at("pbc")}};
        const std::array indices{angle.first, angle.vertex, angle.third};
        const std::array positions{angle.first_position, angle.vertex_position,
                                   angle.third_position};
        const auto overlay = geometry_analysis_overlay(
            *workspace, measured.value().object_id, indices, positions,
            std::to_string(value) + " deg");
        if (!overlay.has_value())
          return Result<Response>::failure(overlay.error());
        AnalysisResultDraft draft;
        draft.name = requested_result_name(arguments);
        draft.kind = AnalysisResultKind::angle;
        draft.provenance.canonical_command = "measure angle";
        draft.provenance.canonical_arguments = arguments;
        draft.provenance.scientific = scientific_contract(
            "atan2 bond-vector angle", "molshredder-angle-v1",
            std::string{length_unit_name(angle.coordinate_unit)}, "degree",
            precision, arguments.at("pbc"),
            boundary == analysis::DistanceBoundary::minimum_image, "error",
            1.0e-10, 1.0e-12, "degree");
        draft.response = std::move(response);
        draft.export_table = std::move(table);
        draft.overlay = overlay.value();
        return persist_analysis_response(*workspace, std::move(draft));
      };
    } else if (canonical_name == "measure dihedral") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        if (const auto error = validate_result_name(*workspace, arguments);
            error.has_value())
          return Result<Response>::failure(*error);
        const auto boundary = arguments.at("pbc") == "minimum-image"
                                  ? analysis::DistanceBoundary::minimum_image
                                  : analysis::DistanceBoundary::raw;
        const auto measured = workspace->analyze_dihedral(
            arguments.at("first"), arguments.at("second"),
            arguments.at("third"), arguments.at("fourth"), boundary);
        if (!measured.has_value())
          return Result<Response>::failure(measured.error());
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        const auto &dihedral = measured.value().dihedral;
        const auto value = rounded(dihedral.angle_degrees, precision);
        Response response{
            "dihedral measured",
            {{"angle_degrees", value},
             {"first", measured.value().first_expression},
             {"first_atom_index",
              static_cast<std::uint64_t>(dihedral.first.value + 1U)},
             {"fourth", measured.value().fourth_expression},
             {"fourth_atom_index",
              static_cast<std::uint64_t>(dihedral.fourth.value + 1U)},
             {"object_id", measured.value().object_id},
             {"pbc", arguments.at("pbc")},
             {"precision", precision},
             {"second", measured.value().second_expression},
             {"second_atom_index",
              static_cast<std::uint64_t>(dihedral.second.value + 1U)},
             {"third", measured.value().third_expression},
             {"third_atom_index",
              static_cast<std::uint64_t>(dihedral.third.value + 1U)},
             {"unit", "degree"}}};
        command::Table table;
        table.columns = {"first_atom_index", "second_atom_index",
                         "third_atom_index", "fourth_atom_index",
                         "angle_degrees", "pbc"};
        table.rows = {{dihedral.first.value + 1U,
                       dihedral.second.value + 1U,
                       dihedral.third.value + 1U,
                       dihedral.fourth.value + 1U, precise(value, precision),
                       arguments.at("pbc")}};
        const std::array indices{dihedral.first, dihedral.second,
                                 dihedral.third, dihedral.fourth};
        const std::array positions{
            dihedral.first_position, dihedral.second_position,
            dihedral.third_position, dihedral.fourth_position};
        const auto overlay = geometry_analysis_overlay(
            *workspace, measured.value().object_id, indices, positions,
            std::to_string(value) + " deg");
        if (!overlay.has_value())
          return Result<Response>::failure(overlay.error());
        AnalysisResultDraft draft;
        draft.name = requested_result_name(arguments);
        draft.kind = AnalysisResultKind::dihedral;
        draft.provenance.canonical_command = "measure dihedral";
        draft.provenance.canonical_arguments = arguments;
        draft.provenance.scientific = scientific_contract(
            "projected-vector signed atan2 torsion",
            "molshredder-dihedral-v1",
            std::string{length_unit_name(dihedral.coordinate_unit)}, "degree",
            precision, arguments.at("pbc"),
            boundary == analysis::DistanceBoundary::minimum_image, "error",
            1.0e-10, 1.0e-12, "degree");
        draft.response = std::move(response);
        draft.export_table = std::move(table);
        draft.overlay = overlay.value();
        return persist_analysis_response(*workspace, std::move(draft));
      };
    } else if (canonical_name == "analyze sasa") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if (const auto error = validate_result_name(*workspace, arguments);
            error.has_value())
          return Result<Response>::failure(*error);
        const auto probe = number_argument(arguments, "probe-radius");
        const auto samples = size_argument(arguments, "samples", true);
        const auto budget =
            size_argument(arguments, "evaluation-budget", true);
        if (!probe.has_value()) return Result<Response>::failure(probe.error());
        if (!samples.has_value())
          return Result<Response>::failure(samples.error());
        if (!budget.has_value()) return Result<Response>::failure(budget.error());
        auto measured = workspace->analyze_sasa(
            arguments.at("selection"), probe.value(), samples.value(),
            budget.value(), &context);
        if (!measured.has_value())
          return Result<Response>::failure(measured.error());
        return persist_sasa_analysis(*workspace, arguments,
                                     std::move(measured.value()));
      };
    } else if (canonical_name == "analyze rdf") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if(const auto error=validate_result_name(*workspace,arguments);error.has_value())
          return Result<Response>::failure(*error);
        const auto maximum=number_argument(arguments,"maximum-radius");
        const auto width=number_argument(arguments,"bin-width");
        const auto budget=size_argument(arguments,"evaluation-budget",true);
        if(!maximum.has_value()) return Result<Response>::failure(maximum.error());
        if(!width.has_value()) return Result<Response>::failure(width.error());
        if(!budget.has_value()) return Result<Response>::failure(budget.error());
        const auto same_selection=!arguments.contains("second");
        const auto second=same_selection?arguments.at("first"):arguments.at("second");
        const auto boundary=arguments.at("pbc")=="minimum-image"
            ?analysis::DistanceBoundary::minimum_image:analysis::DistanceBoundary::raw;
        const auto normalization=arguments.at("normalization")=="g-r"
            ?analysis::RdfNormalization::radial_distribution:analysis::RdfNormalization::pair_count;
        const auto target_unit=length_unit(arguments.at("unit"));
        auto analyzed=workspace->analyze_rdf(
            arguments.at("first"),second,maximum.value(),width.value(),boundary,
            normalization,same_selection,static_cast<std::uint64_t>(budget.value()),
            target_unit,&context);
        if(!analyzed.has_value()) return Result<Response>::failure(analyzed.error());
        return persist_rdf_analysis(*workspace,arguments,
                                    std::move(analyzed.value()));
      };
    } else if (canonical_name == "analyze secondary-structure") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto energy = number_argument(arguments, "energy-cutoff");
        const auto helix = number_argument(arguments, "helix-propensity");
        const auto beta = number_argument(arguments, "beta-propensity");
        if (!energy.has_value())
          return Result<Response>::failure(energy.error());
        if (!helix.has_value())
          return Result<Response>::failure(helix.error());
        if (!beta.has_value())
          return Result<Response>::failure(beta.error());
        const auto analyzed = workspace->analyze_secondary_structure(
            arguments.at("selection"),
            {energy.value(), helix.value(), beta.value()});
        if (!analyzed.has_value())
          return Result<Response>::failure(analyzed.error());
        const auto *object = workspace->active_object();
        if (object == nullptr)
          return Result<Response>::failure(
              operation::Error{operation::ErrorCode::internal,
                               "secondary-structure result lost its topology",
                               {}});
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        table.columns = {"residue_index",
                         "residue_name",
                         "residue_number",
                         "insertion_code",
                         "chain",
                         "segment",
                         "code",
                         "state",
                         "phi_degrees",
                         "psi_degrees",
                         "backbone_complete"};
        command::Value::Object counts;
        std::size_t selected_count{};
        for (const auto &row : analyzed.value().assignment.residues) {
          if (!analyzed.value().selected_residues[row.residue.value])
            continue;
          ++selected_count;
          const auto &residue =
              object->system->topology()->residues()[row.residue.value];
          const auto state = std::string{analysis::to_string(row.state)};
          const auto found = counts.find(state);
          const auto old =
              found == counts.end()
                  ? 0U
                  : static_cast<std::size_t>(
                        std::get<std::uint64_t>(found->second.data));
          counts[state] = old + 1U;
          table.rows.push_back(
              {row.residue.value + 1U, residue.name, residue.sequence_number,
               residue.insertion_code, residue.chain_id, residue.segment_id,
               std::string(1, analysis::stride_code(row.state)), state,
               row.phi_degrees
                   ? command::Value{precise(*row.phi_degrees, precision)}
                   : command::Value{nullptr},
               row.psi_degrees
                   ? command::Value{precise(*row.psi_degrees, precision)}
                   : command::Value{nullptr},
               row.backbone_complete});
        }
        Response response{
            "secondary structure assigned",
            {{"exact_stride_parity", false},
             {"hydrogen_bond_count",
              analyzed.value().assignment.hydrogen_bonds.size()},
             {"method", std::string{analyzed.value().assignment.method}},
             {"object_id", analyzed.value().object_id},
             {"precision", precision},
             {"selected_residue_count", selected_count},
             {"selection", analyzed.value().selection_expression},
             {"state_counts", counts}},
            std::move(table)};
        return scientific_response(
            *workspace, std::move(response),
            scientific_contract(
                "paper-derived STRIDE-like hydrogen-bond and propensity assignment",
                std::string{analyzed.value().assignment.method},
                std::string{length_unit_name(analyzed.value().coordinate_unit)},
                "mixed", precision, "not_applicable", false, "error",
                1.0e-6, 1.0e-12, "degree"));
      };
    } else if (canonical_name == "analyze contacts") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        if (const auto error = validate_result_name(*workspace, arguments);
            error.has_value())
          return Result<Response>::failure(*error);
        const auto parsed_cutoff = number_argument(arguments, "cutoff");
        if (!parsed_cutoff.has_value())
          return Result<Response>::failure(parsed_cutoff.error());
        const auto same_selection = !arguments.contains("second");
        const auto second =
            same_selection ? arguments.at("first") : arguments.at("second");
        const auto boundary = arguments.at("pbc") == "minimum-image"
                                  ? analysis::DistanceBoundary::minimum_image
                                  : analysis::DistanceBoundary::raw;
        const auto target_unit = length_unit(arguments.at("unit"));
        const auto analyzed = workspace->analyze_contacts(
            arguments.at("first"), second, parsed_cutoff.value(), boundary,
            same_selection, arguments.at("exclude-bonded") == "true",
            target_unit);
        if (!analyzed.has_value())
          return Result<Response>::failure(analyzed.error());
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        table.columns = {"first_atom_index",
                         "second_atom_index",
                         "dx",
                         "dy",
                         "dz",
                         "distance",
                         "unit",
                         "pbc"};
        table.rows.reserve(analyzed.value().contacts.pairs.size());
        for (const auto &pair : analyzed.value().contacts.pairs) {
          const auto component = [&](double value) {
            return precise(
                convert_length(value, analyzed.value().contacts.coordinate_unit,
                               target_unit),
                precision);
          };
          table.rows.push_back(
              {pair.first.value + 1U, pair.second.value + 1U,
               component(pair.displacement.x), component(pair.displacement.y),
               component(pair.displacement.z), component(pair.distance),
               std::string{length_unit_name(target_unit)},
               arguments.at("pbc")});
        }
        Response response{
            "contacts calculated",
            {{"cutoff", precise(parsed_cutoff.value(), precision)},
              {"exclude_bonded", arguments.at("exclude-bonded") == "true"},
              {"first", analyzed.value().first_expression},
              {"object_id", analyzed.value().object_id},
              {"pbc", arguments.at("pbc")},
              {"pair_count", analyzed.value().contacts.pairs.size()},
              {"precision", precision},
              {"second", analyzed.value().second_expression},
              {"unit", std::string{length_unit_name(target_unit)}}},
            table};
        AnalysisResultDraft draft;
        draft.name = requested_result_name(arguments);
        draft.kind = AnalysisResultKind::contacts;
        draft.provenance.canonical_command = "analyze contacts";
        draft.provenance.canonical_arguments = arguments;
        draft.provenance.scientific = scientific_contract(
            boundary == analysis::DistanceBoundary::minimum_image
                ? "wrapped fractional cell list with exact closest lattice"
                : "Cartesian cell list",
            "molshredder-contacts-v1",
            analyzed.value().contacts.coordinate_unit, target_unit, precision,
            arguments.at("pbc"),
            boundary == analysis::DistanceBoundary::minimum_image, "error",
            1.0e-9, 1.0e-12);
        draft.response = std::move(response);
        draft.export_table = std::move(table);
        return persist_analysis_response(*workspace, std::move(draft));
      };
    } else if (canonical_name == "analyze hbonds") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto cutoff = number_argument(arguments, "cutoff");
        const auto angle = number_argument(arguments, "angle");
        if (!cutoff.has_value())
          return Result<Response>::failure(cutoff.error());
        if (!angle.has_value())
          return Result<Response>::failure(angle.error());
        const auto same_selection = !arguments.contains("acceptors");
        const auto acceptors =
            same_selection ? arguments.at("donors") : arguments.at("acceptors");
        const auto boundary = arguments.at("pbc") == "minimum-image"
                                  ? analysis::DistanceBoundary::minimum_image
                                  : analysis::DistanceBoundary::raw;
        const auto target_unit = length_unit(arguments.at("unit"));
        const auto analyzed = workspace->analyze_hydrogen_bonds(
            arguments.at("donors"), acceptors, cutoff.value(), angle.value(),
            boundary, same_selection, target_unit);
        if (!analyzed.has_value())
          return Result<Response>::failure(analyzed.error());
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        table.columns = {"donor_atom_index",
                         "acceptor_atom_index",
                         "hydrogen_atom_index",
                         "donor_acceptor_distance",
                         "unit",
                         "angle_deviation_degrees"};
        table.rows.reserve(analyzed.value().hydrogen_bonds.bonds.size());
        for (const auto &bond : analyzed.value().hydrogen_bonds.bonds) {
          table.rows.push_back(
              {bond.donor.value + 1U, bond.acceptor.value + 1U,
               bond.hydrogen.value + 1U,
               precise(convert_length(
                           bond.donor_acceptor_distance,
                           analyzed.value().hydrogen_bonds.coordinate_unit,
                           target_unit),
                       precision),
               std::string{length_unit_name(target_unit)},
               precise(bond.angle_deviation_degrees, precision)});
        }
        Response response{
            "hydrogen bonds calculated",
            {{"acceptors", analyzed.value().acceptor_expression},
             {"angle", precise(angle.value(), precision)},
             {"bond_count", analyzed.value().hydrogen_bonds.bonds.size()},
             {"cutoff", precise(cutoff.value(), precision)},
             {"donor_typing_source", analyzed.value().typing.donor_source},
             {"donors", analyzed.value().donor_expression},
             {"object_id", analyzed.value().object_id},
             {"pbc", arguments.at("pbc")},
             {"acceptor_typing_source",
              analyzed.value().typing.acceptor_source},
             {"typing_estimated", analyzed.value().typing.estimated},
             {"unit", std::string{length_unit_name(target_unit)}}},
            std::move(table)};
        return scientific_response(
            *workspace, std::move(response),
            scientific_contract(
                "typed donor-acceptor geometric hydrogen-bond search",
                "molshredder-hbond-v1",
                analyzed.value().hydrogen_bonds.coordinate_unit, target_unit,
                precision, arguments.at("pbc"),
                boundary == analysis::DistanceBoundary::minimum_image,
                "error", 1.0e-9, 1.0e-12));
      };
    } else if (canonical_name == "analyze trajectory center") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto range = series_range(arguments, *workspace);
        if (!range.has_value())
          return Result<Response>::failure(range.error());
        const auto mode = arguments.at("mode") == "com"
                              ? analysis::CenterMode::center_of_mass
                              : analysis::CenterMode::centroid;
        const auto analyzed = workspace->analyze_center_time_series(
            arguments.at("selection"), mode, range.value(),
            arguments.at("missing") == "skip"
                ? analysis::MissingAtomPolicy::skip
                : analysis::MissingAtomPolicy::error,
            context);
        if (!analyzed.has_value()) {
          return Result<Response>::failure(analyzed.error());
        }
        const auto target_unit = length_unit(arguments.at("unit"));
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        table.columns = {"frame",
                         "source_step",
                         "physical_time",
                         "physical_time_unit",
                         "x",
                         "y",
                         "z",
                         "unit",
                         "selected_atom_count",
                         "used_atom_count",
                         "skipped_missing_atom_count",
                         "total_mass",
                         "mass_unit",
                         "mass_source",
                         "mass_estimated"};
        table.rows.reserve(analyzed.value().rows.size());
        for (const auto &row : analyzed.value().rows) {
          const auto component = [&](double value) {
            return rounded(
                convert_length(value, row.center.coordinate_unit, target_unit),
                precision);
          };
          table.rows.push_back(
              {row.frame.frame_index, optional_step(row.frame),
               optional_time(row.frame), optional_time_unit(row.frame),
               precise(component(row.center.position.x), precision),
               precise(component(row.center.position.y), precision),
               precise(component(row.center.position.z), precision),
               std::string{length_unit_name(target_unit)},
               row.center.selected_atom_count, row.center.used_atom_count,
               row.center.skipped_missing_atom_count,
               row.center.total_mass.has_value()
                   ? command::Value{*row.center.total_mass}
                   : command::Value{nullptr},
               row.center.mass_unit.has_value()
                   ? command::Value{*row.center.mass_unit}
                   : command::Value{nullptr},
               row.center.mass_source.empty()
                   ? command::Value{nullptr}
                   : command::Value{row.center.mass_source},
               row.center.total_mass.has_value()
                   ? command::Value{row.center.masses_estimated}
                   : command::Value{nullptr}});
        }
        Response response{
            "trajectory " + arguments.at("mode") + " calculated",
            {{"first_frame", range.value().first},
             {"last_frame", range.value().last},
             {"missing", arguments.at("missing")},
             {"mode", arguments.at("mode")},
             {"object_id", analyzed.value().object_id},
             {"precision", precision},
             {"row_count", analyzed.value().rows.size()},
             {"selection", analyzed.value().selection_expression},
             {"stride", range.value().stride},
             {"unit", std::string{length_unit_name(target_unit)}}},
            std::move(table)};
        const auto input_unit = analyzed.value().rows.empty()
                                    ? target_unit
                                    : analyzed.value().rows.front()
                                          .center.coordinate_unit;
        return scientific_response(
            *workspace, std::move(response),
            scientific_contract(
                mode == analysis::CenterMode::center_of_mass
                    ? "per-frame compensated weighted center"
                    : "per-frame compensated arithmetic center",
                "molshredder-center-series-v1", input_unit, target_unit,
                precision, "not_applicable", false, arguments.at("missing"),
                1.0e-10, 1.0e-12, "trajectory_range"));
      };
    } else if (canonical_name == "analyze trajectory distance") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto range = series_range(arguments, *workspace);
        if (!range.has_value())
          return Result<Response>::failure(range.error());
        const auto boundary = arguments.at("pbc") == "minimum-image"
                                  ? analysis::DistanceBoundary::minimum_image
                                  : analysis::DistanceBoundary::raw;
        const auto analyzed = workspace->analyze_distance_time_series(
            arguments.at("from"), arguments.at("to"), boundary, range.value(),
            context);
        if (!analyzed.has_value()) {
          return Result<Response>::failure(analyzed.error());
        }
        const auto target_unit = length_unit(arguments.at("unit"));
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        table.columns = {"frame",
                         "source_step",
                         "physical_time",
                         "physical_time_unit",
                         "first_atom_index",
                         "second_atom_index",
                         "dx",
                         "dy",
                         "dz",
                         "distance",
                         "unit",
                         "pbc"};
        table.rows.reserve(analyzed.value().rows.size());
        for (const auto &row : analyzed.value().rows) {
          const auto component = [&](double value) {
            return rounded(convert_length(value, row.distance.coordinate_unit,
                                          target_unit),
                           precision);
          };
          table.rows.push_back(
              {row.frame.frame_index, optional_step(row.frame),
               optional_time(row.frame), optional_time_unit(row.frame),
               row.distance.first.value + 1U, row.distance.second.value + 1U,
               precise(component(row.distance.displacement.x), precision),
               precise(component(row.distance.displacement.y), precision),
               precise(component(row.distance.displacement.z), precision),
               precise(component(row.distance.distance), precision),
               std::string{length_unit_name(target_unit)},
               arguments.at("pbc")});
        }
        Response response{
            "trajectory distance calculated",
            {{"first_frame", range.value().first},
             {"from", analyzed.value().from_expression},
             {"last_frame", range.value().last},
             {"object_id", analyzed.value().object_id},
             {"pbc", arguments.at("pbc")},
             {"precision", precision},
             {"row_count", analyzed.value().rows.size()},
             {"stride", range.value().stride},
             {"to", analyzed.value().to_expression},
             {"unit", std::string{length_unit_name(target_unit)}}},
            std::move(table)};
        const auto input_unit = analyzed.value().rows.empty()
                                    ? target_unit
                                    : analyzed.value().rows.front()
                                          .distance.coordinate_unit;
        return scientific_response(
            *workspace, std::move(response),
            scientific_contract(
                boundary == analysis::DistanceBoundary::minimum_image
                    ? "per-frame exact triclinic closest-lattice distance"
                    : "per-frame Euclidean atom distance",
                "molshredder-distance-series-v1", input_unit, target_unit,
                precision, arguments.at("pbc"),
                boundary == analysis::DistanceBoundary::minimum_image,
                "error", 1.0e-10, 1.0e-12, "trajectory_range"));
      };
    } else if (canonical_name == "analyze trajectory rmsd") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if (const auto error = validate_result_name(*workspace, arguments);
            error.has_value())
          return Result<Response>::failure(*error);
        const auto range = series_range(arguments, *workspace);
        const auto reference = size_argument(arguments, "reference");
        if (!range.has_value())
          return Result<Response>::failure(range.error());
        if (!reference.has_value()) {
          return Result<Response>::failure(reference.error());
        }
        const auto fit_selection_expression = fit_selection(arguments);
        const auto analyzed = workspace->analyze_rmsd_time_series(
            arguments.at("selection"), fit_selection_expression,
            reference.value(), range.value(), fit_mode(arguments.at("fit")),
            weight_mode(arguments.at("weight")),
            arguments.at("missing") == "skip"
                ? analysis::MissingAtomPolicy::skip
                : analysis::MissingAtomPolicy::error,
            context);
        if (!analyzed.has_value()) {
          return Result<Response>::failure(analyzed.error());
        }
        const auto target_unit = length_unit(arguments.at("unit"));
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        table.columns = {"frame",
                         "source_step",
                         "physical_time",
                         "physical_time_unit",
                         "reference_frame",
                         "rmsd",
                         "rmsd_before_fit",
                         "unit",
                         "selected_atom_count",
                         "paired_atom_count",
                         "skipped_missing_atom_count",
                         "effective_atom_count",
                         "weight_sum",
                         "fit_paired_atom_count",
                         "fit",
                         "weight"};
        table.rows.reserve(analyzed.value().rows.size());
        for (const auto &row : analyzed.value().rows) {
          const auto converted = [&](double value) {
            return rounded(
                convert_length(value, row.rmsd.coordinate_unit, target_unit),
                precision);
          };
          table.rows.push_back(
              {row.frame.frame_index, optional_step(row.frame),
               optional_time(row.frame), optional_time_unit(row.frame),
               analyzed.value().reference_frame,
               precise(converted(row.rmsd.rmsd), precision),
               precise(converted(row.rmsd_before_fit), precision),
               std::string{length_unit_name(target_unit)},
               row.rmsd.selected_atom_count, row.rmsd.paired_atom_count,
               row.rmsd.skipped_missing_atom_count,
               row.rmsd.effective_atom_count, row.rmsd.weight_sum,
               row.fit_paired_atom_count, arguments.at("fit"),
               arguments.at("weight")});
        }
        command::Value::Object fields{
            {"first_frame", range.value().first},
            {"fit", arguments.at("fit")},
            {"fit_selection", analyzed.value().fit_selection_expression},
            {"last_frame", range.value().last},
            {"missing", arguments.at("missing")},
            {"object_id", analyzed.value().object_id},
            {"precision", precision},
            {"reference_frame", analyzed.value().reference_frame},
            {"row_count", analyzed.value().rows.size()},
            {"selection", analyzed.value().selection_expression},
            {"stride", range.value().stride},
            {"unit", std::string{length_unit_name(target_unit)}}};
        add_weight_provenance(fields, analyzed.value().weights);
        Response response{"trajectory RMSD calculated", std::move(fields),
                          table};
        AnalysisResultDraft draft;
        draft.name = requested_result_name(arguments);
        draft.kind = AnalysisResultKind::rmsd_series;
        draft.provenance.canonical_command = "analyze trajectory rmsd";
        draft.provenance.canonical_arguments = arguments;
        const auto input_unit = analyzed.value().rows.empty()
                                    ? target_unit
                                    : analyzed.value().rows.front()
                                          .rmsd.coordinate_unit;
        draft.provenance.scientific = scientific_contract(
            arguments.at("fit") == "rigid"
                ? "Horn quaternion rigid fit and weighted RMSD"
                : "weighted RMSD without fit",
            "molshredder-rmsd-series-v1", input_unit, target_unit, precision,
            "raw", false, arguments.at("missing"), 1.0e-10, 1.0e-12,
            "trajectory_range");
        draft.provenance.frame_first = range.value().first;
        draft.provenance.frame_last = range.value().last;
        draft.provenance.frame_stride = range.value().stride;
        draft.response = std::move(response);
        draft.export_table = std::move(table);
        return persist_analysis_response(*workspace, std::move(draft));
      };
    } else if (canonical_name == "analyze trajectory rmsd-matrix") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if(const auto error=validate_result_name(*workspace,arguments);error.has_value())
          return Result<Response>::failure(*error);
        const auto range=series_range(arguments,*workspace);
        const auto budget=size_argument(arguments,"frame-pair-budget",true);
        if(!range.has_value()) return Result<Response>::failure(range.error());
        if(!budget.has_value()) return Result<Response>::failure(budget.error());
        auto analyzed=workspace->analyze_rmsd_matrix(
            arguments.at("selection"),fit_selection(arguments),range.value(),
            fit_mode(arguments.at("fit")),weight_mode(arguments.at("weight")),
            arguments.at("missing")=="skip"?analysis::MissingAtomPolicy::skip:
                                             analysis::MissingAtomPolicy::error,
            static_cast<std::uint64_t>(budget.value()),context);
        if(!analyzed.has_value()) return Result<Response>::failure(analyzed.error());
        return persist_rmsd_matrix_analysis(*workspace,arguments,
                                            std::move(analyzed.value()));
      };
    } else if (canonical_name == "analyze trajectory rmsf") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if(const auto error=validate_result_name(*workspace,arguments);error.has_value())
          return Result<Response>::failure(*error);
        const auto range = series_range(arguments, *workspace);
        const auto reference = size_argument(arguments, "reference");
        if (!range.has_value())
          return Result<Response>::failure(range.error());
        if (!reference.has_value()) {
          return Result<Response>::failure(reference.error());
        }
        const auto fit_selection_expression = fit_selection(arguments);
        const auto analyzed = workspace->analyze_rmsf_time_series(
            arguments.at("selection"), fit_selection_expression,
            reference.value(), range.value(), fit_mode(arguments.at("fit")),
            weight_mode(arguments.at("weight")),
            arguments.at("missing") == "skip"
                ? analysis::MissingAtomPolicy::skip
                : analysis::MissingAtomPolicy::error,
            context);
        if (!analyzed.has_value()) {
          return Result<Response>::failure(analyzed.error());
        }
        const auto *object = workspace->active_object();
        if (object == nullptr) {
          return Result<Response>::failure(
              operation::Error{operation::ErrorCode::internal,
                               "RMSF result lost its active topology",
                               {}});
        }
        const auto target_unit = length_unit(arguments.at("unit"));
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        table.columns = {
            "atom_index",        "source_serial",  "atom_name", "residue_name",
            "residue_number",    "insertion_code", "chain",     "segment",
            "observation_count", "rmsf",           "unit"};
        table.rows.reserve(analyzed.value().series.atoms.size());
        for (const auto &row : analyzed.value().series.atoms) {
          const auto &atom =
              object->system->topology()->atoms()[row.atom.value];
          const auto &residue =
              object->system->topology()->residues()[atom.residue.value];
          const auto rmsf =
              row.rmsf.has_value()
                  ? precise(
                        convert_length(*row.rmsf,
                                       analyzed.value().series.coordinate_unit,
                                       target_unit),
                        precision)
                  : command::Value{nullptr};
          table.rows.push_back({row.atom.value + 1U,
                                atom.source_serial.has_value()
                                    ? command::Value{*atom.source_serial}
                                    : command::Value{nullptr},
                                atom.name, residue.name,
                                residue.sequence_number, residue.insertion_code,
                                residue.chain_id, residue.segment_id,
                                row.observation_count, rmsf,
                                std::string{length_unit_name(target_unit)}});
        }
        command::Value::Object fields{
            {"first_frame", range.value().first},
            {"fit", arguments.at("fit")},
            {"fit_selection", analyzed.value().fit_selection_expression},
            {"frame_count", analyzed.value().series.frame_count},
            {"last_frame", range.value().last},
            {"missing", arguments.at("missing")},
            {"object_id", analyzed.value().object_id},
            {"precision", precision},
            {"reference_frame", analyzed.value().reference_frame},
            {"row_count", analyzed.value().series.atoms.size()},
            {"selected_atom_count",
             analyzed.value().series.selected_atom_count},
            {"selection", analyzed.value().selection_expression},
            {"stride", range.value().stride},
            {"unit", std::string{length_unit_name(target_unit)}}};
        add_weight_provenance(fields, analyzed.value().weights);
        Response response{"trajectory RMSF calculated",std::move(fields),table};
        AnalysisResultDraft draft;
        draft.name=requested_result_name(arguments);
        draft.kind=AnalysisResultKind::rmsf_series;
        draft.provenance.canonical_command="analyze trajectory rmsf";
        draft.provenance.canonical_arguments=arguments;
        draft.provenance.scientific=scientific_contract(
                arguments.at("fit") == "rigid"
                    ? "Horn quaternion rigid fit and per-atom RMSF"
                    : "per-atom RMSF without fit",
                "molshredder-rmsf-series-v1",
                analyzed.value().series.coordinate_unit, target_unit,
                precision, "raw", false, arguments.at("missing"), 1.0e-10,
                1.0e-12, "trajectory_range");
        draft.provenance.frame_first=range.value().first;
        draft.provenance.frame_last=range.value().last;
        draft.provenance.frame_stride=range.value().stride;
        draft.response=std::move(response); draft.export_table=std::move(table);
        return persist_analysis_response(*workspace,std::move(draft));
      };
    } else if (canonical_name == "analyze trajectory contacts") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        if (const auto error = validate_result_name(*workspace, arguments);
            error.has_value())
          return Result<Response>::failure(*error);
        const auto range = series_range(arguments, *workspace);
        const auto cutoff = number_argument(arguments, "cutoff");
        if (!range.has_value())
          return Result<Response>::failure(range.error());
        if (!cutoff.has_value())
          return Result<Response>::failure(cutoff.error());
        const auto same = !arguments.contains("selection2");
        const auto second =
            same ? arguments.at("selection1") : arguments.at("selection2");
        const auto boundary = arguments.at("pbc") == "minimum-image"
                                  ? analysis::DistanceBoundary::minimum_image
                                  : analysis::DistanceBoundary::raw;
        const auto target_unit = length_unit(arguments.at("unit"));
        const auto analyzed = workspace->analyze_contact_time_series(
            arguments.at("selection1"), second, cutoff.value(), target_unit,
            boundary, same, arguments.at("exclude-bonded") == "true",
            arguments.at("report") == "occupancy", range.value(), context);
        if (!analyzed.has_value())
          return Result<Response>::failure(analyzed.error());
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        if (arguments.at("report") == "frames") {
          table.columns = {"frame", "source_step", "physical_time",
                           "physical_time_unit", "contact_count"};
          for (const auto &row : analyzed.value().series.frames)
            table.rows.push_back(
                {row.frame.frame_index, optional_step(row.frame),
                 optional_time(row.frame), optional_time_unit(row.frame),
                 row.interaction_count});
        } else {
          table.columns = {"first_atom_index",  "second_atom_index",
                           "observation_count", "occupancy",
                           "mean_distance",     "unit"};
          for (const auto &row : analyzed.value().series.occupancy)
            table.rows.push_back({row.first.value + 1U, row.second.value + 1U,
                                  row.observation_count,
                                  precise(row.occupancy, precision),
                                  precise(row.mean_distance, precision),
                                  std::string{length_unit_name(target_unit)}});
        }
        Response response{
            "trajectory contacts calculated",
            {{"cutoff", precise(cutoff.value(), precision)},
              {"exclude_bonded", arguments.at("exclude-bonded") == "true"},
              {"first_frame", range.value().first},
              {"frame_count", analyzed.value().series.frame_count},
              {"last_frame", range.value().last},
              {"object_id", analyzed.value().object_id},
              {"pair_count", analyzed.value().series.occupancy.size()},
              {"pbc", arguments.at("pbc")},
              {"report", arguments.at("report")},
              {"precision", precision},
              {"selection1", analyzed.value().first_expression},
              {"selection2", analyzed.value().second_expression},
              {"stride", range.value().stride},
              {"unit", std::string{length_unit_name(target_unit)}}},
            table};
        AnalysisResultDraft draft;
        draft.name = requested_result_name(arguments);
        draft.kind = AnalysisResultKind::contact_series;
        draft.provenance.canonical_command = "analyze trajectory contacts";
        draft.provenance.canonical_arguments = arguments;
        draft.provenance.scientific = scientific_contract(
            boundary == analysis::DistanceBoundary::minimum_image
                ? "per-frame wrapped fractional cell list with exact closest lattice"
                : "per-frame Cartesian cell list",
            "molshredder-contact-series-v1",
            analyzed.value().series.coordinate_unit, target_unit, precision,
            arguments.at("pbc"),
            boundary == analysis::DistanceBoundary::minimum_image, "error",
            1.0e-9, 1.0e-12, "trajectory_range");
        draft.provenance.frame_first = range.value().first;
        draft.provenance.frame_last = range.value().last;
        draft.provenance.frame_stride = range.value().stride;
        draft.response = std::move(response);
        draft.export_table = std::move(table);
        return persist_analysis_response(*workspace, std::move(draft));
      };
    } else if (canonical_name == "analyze trajectory hbonds") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto range = series_range(arguments, *workspace);
        const auto cutoff = number_argument(arguments, "cutoff");
        const auto angle = number_argument(arguments, "angle");
        if (!range.has_value())
          return Result<Response>::failure(range.error());
        if (!cutoff.has_value())
          return Result<Response>::failure(cutoff.error());
        if (!angle.has_value())
          return Result<Response>::failure(angle.error());
        const auto same = !arguments.contains("acceptors");
        const auto acceptors =
            same ? arguments.at("donors") : arguments.at("acceptors");
        const auto boundary = arguments.at("pbc") == "minimum-image"
                                  ? analysis::DistanceBoundary::minimum_image
                                  : analysis::DistanceBoundary::raw;
        const auto target_unit = length_unit(arguments.at("unit"));
        const auto analyzed = workspace->analyze_hydrogen_bond_time_series(
            arguments.at("donors"), acceptors, cutoff.value(), target_unit,
            angle.value(), boundary, same,
            arguments.at("report") == "occupancy", range.value(), context);
        if (!analyzed.has_value())
          return Result<Response>::failure(analyzed.error());
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        if (arguments.at("report") == "frames") {
          table.columns = {"frame", "source_step", "physical_time",
                           "physical_time_unit", "hbond_count"};
          for (const auto &row : analyzed.value().series.frames)
            table.rows.push_back(
                {row.frame.frame_index, optional_step(row.frame),
                 optional_time(row.frame), optional_time_unit(row.frame),
                 row.interaction_count});
        } else {
          table.columns = {"donor_atom_index",
                           "acceptor_atom_index",
                           "hydrogen_atom_index",
                           "observation_count",
                           "occupancy",
                           "mean_donor_acceptor_distance",
                           "unit",
                           "mean_angle_deviation_degrees"};
          for (const auto &row : analyzed.value().series.occupancy)
            table.rows.push_back(
                {row.donor.value + 1U, row.acceptor.value + 1U,
                 row.hydrogen.value + 1U, row.observation_count,
                 precise(row.occupancy, precision),
                 precise(row.mean_donor_acceptor_distance, precision),
                 std::string{length_unit_name(target_unit)},
                 precise(row.mean_angle_deviation_degrees, precision)});
        }
        Response response{
            "trajectory hydrogen bonds calculated",
            {{"acceptor_typing_source",
              analyzed.value().typing.acceptor_source},
             {"acceptors", analyzed.value().acceptor_expression},
             {"angle", precise(angle.value(), precision)},
             {"bond_count", analyzed.value().series.occupancy.size()},
             {"cutoff", precise(cutoff.value(), precision)},
             {"donor_typing_source", analyzed.value().typing.donor_source},
             {"donors", analyzed.value().donor_expression},
             {"first_frame", range.value().first},
             {"frame_count", analyzed.value().series.frame_count},
             {"last_frame", range.value().last},
             {"object_id", analyzed.value().object_id},
             {"pbc", arguments.at("pbc")},
             {"report", arguments.at("report")},
             {"stride", range.value().stride},
             {"precision", precision},
             {"typing_estimated", analyzed.value().typing.estimated},
             {"unit", std::string{length_unit_name(target_unit)}}},
            std::move(table)};
        return scientific_response(
            *workspace, std::move(response),
            scientific_contract(
                "per-frame typed donor-acceptor geometric hydrogen-bond search",
                "molshredder-hbond-series-v1",
                analyzed.value().series.coordinate_unit, target_unit,
                precision, arguments.at("pbc"),
                boundary == analysis::DistanceBoundary::minimum_image,
                "error", 1.0e-9, 1.0e-12, "trajectory_range"));
      };
    } else if (canonical_name == "traj load") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto provider =
            requested_provider(arguments, io::FormatDirection::read);
        if (!provider.has_value()) return Result<Response>::failure(provider.error());
        const auto cache_mib = size_argument(arguments, "cache-mib", true);
        const auto prefetch_frames =
            size_argument(arguments, "prefetch-frames");
        if (!cache_mib.has_value()) {
          return Result<Response>::failure(cache_mib.error());
        }
        if (!prefetch_frames.has_value()) {
          return Result<Response>::failure(prefetch_frames.error());
        }
        const auto mapping_policy =
            trajectory_mapping_policy(arguments.at("mapping"));
        const auto atom_map = atom_id_list_argument(arguments, "atom-map");
        if (!atom_map.has_value())
          return Result<Response>::failure(atom_map.error());
        std::optional<std::uint64_t> expected_topology_version;
        if (const auto found = arguments.find("expected-topology-version");
            found != arguments.end()) {
          const auto parsed =
              uint64_argument(arguments, "expected-topology-version", true);
          if (!parsed.has_value())
            return Result<Response>::failure(parsed.error());
          expected_topology_version = parsed.value();
        }
        if (mapping_policy == trajectory::AtomMappingPolicy::explicit_map) {
          if (atom_map.value().empty() ||
              !expected_topology_version.has_value()) {
            return Result<Response>::failure(operation::Error{
                operation::ErrorCode::invalid_argument,
                "explicit trajectory mapping requires --atom-map and "
                "--expected-topology-version",
                "provide comma-separated target stable atom IDs in trajectory "
                "source order and the current topology version"});
          }
        } else if (!atom_map.value().empty() ||
                   expected_topology_version.has_value()) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "--atom-map and --expected-topology-version require "
              "--mapping explicit",
              "remove explicit-map parameters or select explicit mapping"});
        }
        constexpr std::size_t bytes_per_mib = 1024U * 1024U;
        if (cache_mib.value() >
            std::numeric_limits<std::size_t>::max() / bytes_per_mib) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::invalid_argument,
              "trajectory cache size overflows addressable memory",
              "choose a smaller --cache-mib value"});
        }
        std::optional<std::string> h5md_particle_group;
        if (const auto found = arguments.find("particle-group");
            found != arguments.end() && !found->second.empty()) {
          h5md_particle_group = found->second;
        }
        const auto loaded = workspace->load_trajectory(
            arguments.at("path"),
            trajectory_format(arguments.at("file-format")),
            cache_mib.value() * bytes_per_mib, prefetch_frames.value(),
            arguments.at("coordinate-unit") == "auto"
                ? std::nullopt
                : std::optional<operation::LengthUnit>{length_unit(
                      arguments.at("coordinate-unit"))},
            std::move(h5md_particle_group), mapping_policy, atom_map.value(),
            expected_topology_version);
        if (!loaded.has_value()) {
          return Result<Response>::failure(loaded.error());
        }
        Response response{
            "trajectory attached",
            {{"atom_count",
              static_cast<std::uint64_t>(loaded.value().atom_count)},
             {"cache_budget_bytes",
              static_cast<std::uint64_t>(loaded.value().cache_budget_bytes)},
             {"current_frame",
              static_cast<std::uint64_t>(loaded.value().current_frame)},
             {"format", std::string{io::to_string(loaded.value().format)}},
             {"frame_count",
              static_cast<std::uint64_t>(loaded.value().frame_count)},
             {"object_id", loaded.value().object_id}}};
        auto &fields = response.fields;
        fields.emplace("provider", provider_value(provider.value()));
        fields.emplace(
            "prefetch_frame_count",
            static_cast<std::uint64_t>(loaded.value().prefetch_frame_count));
        fields.emplace("prefetch_generation",
                       loaded.value().prefetch.generation);
        fields.emplace("prefetch_requested_count",
                       static_cast<std::uint64_t>(
                           loaded.value().prefetch.frame_indices.size()));
        fields.emplace("prefetch_state", std::string{trajectory::to_string(
                                             loaded.value().prefetch.state)});
        fields.emplace("atom_mapping",
                       trajectory_mapping_value(loaded.value().mapping));
        fields.emplace("semantics",
                       trajectory_semantics_value(loaded.value().semantics));
        return Result<Response>::success(std::move(response));
      };
    } else if (canonical_name == "traj save") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
        const auto provider =
            requested_provider(arguments, io::FormatDirection::write);
        if (!provider.has_value()) return Result<Response>::failure(provider.error());
        std::string title;
        if (const auto found = arguments.find("title");
            found != arguments.end()) {
          title = found->second;
        }
        const auto saved = workspace->save_active_trajectory_frame(
            arguments.at("path"),
            trajectory_format(arguments.at("file-format")), std::move(title),
            arguments.at("overwrite") == "true", context);
        if (!saved.has_value())
          return Result<Response>::failure(saved.error());
        command::Table table;
        table.columns = {"channel", "count", "message"};
        std::uint64_t lost_item_count{};
        for (const auto &loss : saved.value().report.losses) {
          table.rows.push_back({loss.channel, loss.count, loss.message});
          lost_item_count += loss.count;
        }
        return Result<Response>::success(
            {"saved trajectory frame " + saved.value().path.string(),
             {{"atom_count",
               static_cast<std::uint64_t>(saved.value().report.atom_count)},
              {"byte_count", saved.value().report.byte_count},
              {"format",
               std::string{io::to_string(saved.value().report.format)}},
              {"frame_index", saved.value().frame_index},
              {"has_temperature", saved.value().report.has_temperature},
              {"has_forces", saved.value().report.has_forces},
              {"has_time", saved.value().report.has_time},
              {"has_unit_cell", saved.value().report.has_unit_cell},
              {"has_velocities", saved.value().report.has_velocities},
              {"precision",
               saved.value().report.precision.has_value()
                   ? command::Value{*saved.value().report.precision ==
                                            io::TrrPrecision::float32
                                        ? "float32"
                                        : "float64"}
                   : command::Value{nullptr}},
              {"loss_channel_count",
               static_cast<std::uint64_t>(saved.value().report.losses.size())},
              {"loss_item_count", lost_item_count},
              {"object_id", saved.value().object_id},
              {"path", saved.value().path.string()},
              {"provider", provider_value(provider.value())}},
             std::move(table)});
      };
    } else if (canonical_name == "traj frame") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto frame = size_argument(arguments, "frame");
        if (!frame.has_value())
          return Result<Response>::failure(frame.error());
        const auto selected = workspace->set_trajectory_frame(frame.value());
        if (!selected.has_value()) {
          return Result<Response>::failure(selected.error());
        }
        return Result<Response>::success(trajectory_frame_response(
            "trajectory frame selected", selected.value()));
      };
    } else if (canonical_name == "traj play") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto steps = size_argument(arguments, "steps");
        if (!steps.has_value())
          return Result<Response>::failure(steps.error());
        const auto played = workspace->play_trajectory(
            playback_mode(arguments.at("mode")),
            playback_direction(arguments.at("direction")), steps.value());
        if (!played.has_value()) {
          return Result<Response>::failure(played.error());
        }
        return Result<Response>::success(
            trajectory_frame_response("trajectory advanced", played.value()));
      };
    } else if (canonical_name == "traj range") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto first = size_argument(arguments, "first");
        const auto stride = size_argument(arguments, "stride", true);
        if (!first.has_value())
          return Result<Response>::failure(first.error());
        if (!stride.has_value()) {
          return Result<Response>::failure(stride.error());
        }
        std::optional<std::size_t> last;
        if (arguments.contains("last")) {
          const auto parsed = size_argument(arguments, "last");
          if (!parsed.has_value()) {
            return Result<Response>::failure(parsed.error());
          }
          last = parsed.value();
        }
        const auto configured = workspace->configure_trajectory_range(
            {first.value(), last, stride.value()},
            playback_mode(arguments.at("mode")),
            playback_direction(arguments.at("direction")));
        if (!configured.has_value()) {
          return Result<Response>::failure(configured.error());
        }
        return Result<Response>::success(trajectory_frame_response(
            "trajectory range configured", configured.value()));
      };
    } else if (canonical_name == "traj pause") {
      handler = [workspace](const Arguments &, TaskContext &) {
        const auto paused = workspace->pause_trajectory();
        if (!paused.has_value()) {
          return Result<Response>::failure(paused.error());
        }
        return Result<Response>::success(
            trajectory_frame_response("trajectory paused", paused.value()));
      };
    } else if (canonical_name == "traj speed") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto fps = number_argument(arguments, "fps");
        if (!fps.has_value())
          return Result<Response>::failure(fps.error());
        const auto configured = workspace->set_trajectory_speed(fps.value());
        if (!configured.has_value()) {
          return Result<Response>::failure(configured.error());
        }
        return Result<Response>::success(trajectory_frame_response(
            "trajectory speed configured", configured.value()));
      };
    } else if (canonical_name == "traj tick") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto milliseconds = number_argument(arguments, "elapsed-ms");
        if (!milliseconds.has_value()) {
          return Result<Response>::failure(milliseconds.error());
        }
        const auto ticked =
            workspace->tick_trajectory(milliseconds.value() / 1000.0);
        if (!ticked.has_value()) {
          return Result<Response>::failure(ticked.error());
        }
        return Result<Response>::success(
            trajectory_frame_response("trajectory ticked", ticked.value()));
      };
    } else {
      handler = [canonical_name](const Arguments &, TaskContext &) {
        return Result<Response>::failure(operation::Error{
            operation::ErrorCode::unsupported,
            "command kernel is not implemented yet: " + canonical_name,
            "use command help to inspect the provisional grammar"});
      };
    }
    if (persist_in_session) {
      handler = [workspace, canonical_name,
                 state_handler = std::move(handler)](
                    const Arguments &arguments,
                    TaskContext &context) -> Result<Response> {
        auto result = state_handler(arguments, context);
        if (result.has_value()) {
          workspace->record_session_invocation(
              command::Invocation{canonical_name, arguments});
        }
        return result;
      };
    }
    const auto registration_error =
        registry.add(std::move(descriptor), std::move(handler));
    if (registration_error.has_value()) {
      std::terminate();
    }
  }
  for (auto alias : command::foundation_command_aliases()) {
    const auto registration_error = registry.add_alias(std::move(alias));
    if (registration_error.has_value()) {
      std::terminate();
    }
  }
  for (auto alias : command::view_command_aliases()) {
    const auto registration_error = registry.add_alias(std::move(alias));
    if (registration_error.has_value()) {
      std::terminate();
    }
  }
  return registry;
}

} // namespace molshredder::application
