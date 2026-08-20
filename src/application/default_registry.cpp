#include "molshredder/application/default_registry.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

#include "molshredder/command/foundation_grammar.hpp"
#include "molshredder/io/format_capabilities.hpp"
#include "molshredder/io/molfile_provider.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/version.hpp"

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
      std::from_chars(text.data(), text.data() + text.size(), parsed);
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
      std::from_chars(found->second.data(),
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

command::Registry make_default_registry() {
  return make_default_registry(std::make_shared<Workspace>());
}

command::Registry make_default_registry(std::shared_ptr<Workspace> workspace) {
  using command::Arguments;
  using command::Descriptor;
  using command::Response;
  using operation::Result;
  using operation::TaskContext;

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

  auto descriptors = command::foundation_command_descriptors();
  auto object_descriptors = command::object_command_descriptors();
  auto file_descriptors = command::file_command_descriptors();
  auto trajectory_descriptors = command::trajectory_command_descriptors();
  descriptors.insert(descriptors.end(),
                     std::make_move_iterator(object_descriptors.begin()),
                     std::make_move_iterator(object_descriptors.end()));
  descriptors.insert(descriptors.end(),
                     std::make_move_iterator(file_descriptors.begin()),
                     std::make_move_iterator(file_descriptors.end()));
  descriptors.insert(descriptors.end(),
                     std::make_move_iterator(trajectory_descriptors.begin()),
                     std::make_move_iterator(trajectory_descriptors.end()));
  for (auto descriptor : std::move(descriptors)) {
    const auto canonical_name = descriptor.canonical_name;
    command::Handler handler;
    if (canonical_name == "format list") {
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
        return Result<Response>::success(
            {"listed " + std::to_string(table.rows.size()) + " formats",
             {{"capability_schema_version", io::kFormatCapabilitySchemaVersion},
             {"direction", arguments.at("direction")},
             {"family", arguments.at("family")},
             {"format_count", static_cast<std::uint64_t>(table.rows.size())},
              {"formats", std::move(formats)},
              {"provider", provider_value(selected_provider.value())}},
             std::move(table)});
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
    } else if (canonical_name == "object activate") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto id = size_argument(arguments, "id", true);
        if (!id.has_value())
          return Result<Response>::failure(id.error());
        const auto activated =
            workspace->activate_object(static_cast<std::uint64_t>(id.value()));
        if (!activated.has_value()) {
          return Result<Response>::failure(activated.error());
        }
        return Result<Response>::success(
            {"activated object " + activated.value().name,
             object_fields(activated.value())});
      };
    } else if (canonical_name == "object visibility") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto id = size_argument(arguments, "id", true);
        if (!id.has_value())
          return Result<Response>::failure(id.error());
        const auto changed = workspace->set_object_visibility(
            static_cast<std::uint64_t>(id.value()),
            arguments.at("visible") == "true");
        if (!changed.has_value()) {
          return Result<Response>::failure(changed.error());
        }
        return Result<Response>::success(
            {changed.value().visible ? "object shown" : "object hidden",
             object_fields(changed.value())});
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
    } else if (canonical_name == "show") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
        const auto replace_argument = arguments.find("replace");
        const auto replace = replace_argument != arguments.end() &&
                             replace_argument->second == "true";
        const auto shown =
            workspace->show(representation_kind(arguments.at("representation")),
                            arguments.at("selection"), replace);
        if (!shown.has_value())
          return Result<Response>::failure(shown.error());
        return Result<Response>::success(
            {"representation created",
             {{"object_id", shown.value().object_id},
              {"primitive_count",
               static_cast<std::uint64_t>(shown.value().primitive_count)},
              {"representation", arguments.at("representation")},
              {"replace", replace},
              {"representation_index",
               static_cast<std::uint64_t>(shown.value().representation_index)},
              {"selected_atom_count",
               static_cast<std::uint64_t>(shown.value().selected_atom_count)},
              {"selection", arguments.at("selection")}}});
      };
    } else if (canonical_name == "analyze center") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
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
        return Result<Response>::success(
            {arguments.at("mode") + " calculated", std::move(fields)});
      };
    } else if (canonical_name == "measure distance") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
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
        return Result<Response>::success(
            {"distance measured",
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
              {"unit", std::string{length_unit_name(target_unit)}}}});
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
        return Result<Response>::success(
            {"secondary structure assigned",
             {{"exact_stride_parity", false},
              {"hydrogen_bond_count",
               analyzed.value().assignment.hydrogen_bonds.size()},
              {"method", std::string{analyzed.value().assignment.method}},
              {"object_id", analyzed.value().object_id},
              {"precision", precision},
              {"selected_residue_count", selected_count},
              {"selection", analyzed.value().selection_expression},
              {"state_counts", counts}},
             std::move(table)});
      };
    } else if (canonical_name == "analyze contacts") {
      handler = [workspace](const Arguments &arguments, TaskContext &) {
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
        return Result<Response>::success(
            {"contacts calculated",
             {{"cutoff", precise(parsed_cutoff.value(), precision)},
              {"exclude_bonded", arguments.at("exclude-bonded") == "true"},
              {"first", analyzed.value().first_expression},
              {"object_id", analyzed.value().object_id},
              {"pbc", arguments.at("pbc")},
              {"pair_count", analyzed.value().contacts.pairs.size()},
              {"precision", precision},
              {"second", analyzed.value().second_expression},
              {"unit", std::string{length_unit_name(target_unit)}}},
             std::move(table)});
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
        return Result<Response>::success(
            {"hydrogen bonds calculated",
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
             std::move(table)});
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
        return Result<Response>::success(
            {"trajectory " + arguments.at("mode") + " calculated",
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
             std::move(table)});
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
        return Result<Response>::success(
            {"trajectory distance calculated",
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
             std::move(table)});
      };
    } else if (canonical_name == "analyze trajectory rmsd") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
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
        return Result<Response>::success({"trajectory RMSD calculated",
                                          std::move(fields), std::move(table)});
      };
    } else if (canonical_name == "analyze trajectory rmsf") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
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
        return Result<Response>::success({"trajectory RMSF calculated",
                                          std::move(fields), std::move(table)});
      };
    } else if (canonical_name == "analyze trajectory contacts") {
      handler = [workspace](const Arguments &arguments, TaskContext &context) {
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
        return Result<Response>::success(
            {"trajectory contacts calculated",
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
             std::move(table)});
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
        return Result<Response>::success(
            {"trajectory hydrogen bonds calculated",
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
             std::move(table)});
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
            std::move(h5md_particle_group));
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
  return registry;
}

} // namespace molshredder::application
