#include "molshredder/application/default_registry.hpp"

#include <cmath>
#include <charconv>
#include <cstdint>
#include <limits>
#include <iterator>
#include <string>
#include <utility>

#include "molshredder/command/foundation_grammar.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/version.hpp"

namespace molshredder::application {
namespace {

io::StructureFormat structure_format(const command::Arguments& arguments) {
  const auto found = arguments.find("file-format");
  if (found == arguments.end() || found->second == "auto") {
    return io::StructureFormat::auto_detect;
  }
  if (found->second == "pdb") return io::StructureFormat::pdb;
  if (found->second == "mmcif" || found->second == "cif") {
    return io::StructureFormat::mmcif;
  }
  return io::StructureFormat::auto_detect;
}

render::RepresentationKind representation_kind(std::string_view value) {
  if (value == "sticks") return render::RepresentationKind::sticks;
  if (value == "spheres") return render::RepresentationKind::spheres;
  if (value == "ribbon") return render::RepresentationKind::ribbon;
  if (value == "cartoon") return render::RepresentationKind::cartoon;
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
  if (from == to) return value;
  return from == operation::LengthUnit::angstrom ? value * 0.1 : value * 10.0;
}

double rounded(double value, unsigned int precision) {
  const auto scale = std::pow(10.0, static_cast<double>(precision));
  const auto scaled = value * scale;
  if (!std::isfinite(scaled)) return value;
  return std::round(scaled) / scale;
}

command::Value precise(double value, unsigned int precision) {
  return command::Number{rounded(value, precision), precision};
}

operation::Result<std::size_t> size_argument(
    const command::Arguments& arguments, std::string_view name,
    bool require_positive = false) {
  const auto found = arguments.find(name);
  if (found == arguments.end()) {
    return operation::Result<std::size_t>::failure(operation::Error{
        operation::ErrorCode::internal,
        "normalized command is missing --" + std::string{name}, {}});
  }
  const auto& text = found->second;
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

operation::Result<double> number_argument(
    const command::Arguments& arguments, std::string_view name) {
  const auto found = arguments.find(name);
  if (found == arguments.end()) {
    return operation::Result<double>::failure(operation::Error{
        operation::ErrorCode::internal,
        "normalized command is missing --" + std::string{name}, {}});
  }
  double parsed{};
  const auto result = std::from_chars(found->second.data(),
                                      found->second.data() + found->second.size(),
                                      parsed);
  if (result.ec != std::errc{} ||
      result.ptr != found->second.data() + found->second.size() ||
      !std::isfinite(parsed)) {
    return operation::Result<double>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "invalid finite value for --" + std::string{name} + ": " +
            found->second,
        "provide a finite number"});
  }
  return operation::Result<double>::success(parsed);
}

operation::Result<analysis::SeriesRange> series_range(
    const command::Arguments& arguments, const Workspace& workspace) {
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
    const auto* object = workspace.active_object();
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
                           "trajectory has no known frames", {}});
    }
    last = *count - 1U;
  }
  return operation::Result<analysis::SeriesRange>::success(
      analysis::SeriesRange{first.value(), last, stride.value()});
}

command::Value optional_step(const analysis::SeriesFrameMetadata& frame) {
  return frame.source_step.has_value() ? command::Value{*frame.source_step}
                                       : command::Value{nullptr};
}

command::Value optional_time(const analysis::SeriesFrameMetadata& frame) {
  return frame.physical_time.has_value() ? command::Value{*frame.physical_time}
                                         : command::Value{nullptr};
}

command::Value optional_time_unit(
    const analysis::SeriesFrameMetadata& frame) {
  if (!frame.physical_time_unit.has_value()) return nullptr;
  return *frame.physical_time_unit == model::TimeUnit::picosecond
             ? command::Value{"picosecond"}
             : command::Value{"femtosecond"};
}

analysis::FitMode fit_mode(std::string_view value) {
  return value == "none" ? analysis::FitMode::none
                         : analysis::FitMode::rigid;
}

analysis::WeightMode weight_mode(std::string_view value) {
  return value == "mass" ? analysis::WeightMode::mass
                         : analysis::WeightMode::uniform;
}

std::string fit_selection(const command::Arguments& arguments) {
  const auto found = arguments.find("fit-selection");
  return found == arguments.end() ? arguments.at("selection") : found->second;
}

void add_weight_provenance(command::Value::Object& fields,
                           const AnalysisWeightProvenance& weights) {
  fields.emplace("weight", weights.mode == analysis::WeightMode::mass
                               ? "mass"
                               : "uniform");
  fields.emplace("weight_source", weights.source);
  fields.emplace("weight_estimated", weights.estimated);
  fields.emplace("weight_unit", weights.unit.has_value()
                                    ? command::Value{*weights.unit}
                                    : command::Value{nullptr});
}

command::Value optional_frame_count(
    const std::optional<std::size_t>& frame_count) {
  return frame_count.has_value() ? command::Value{*frame_count}
                                 : command::Value{nullptr};
}

command::Value::Object object_fields(const WorkspaceObjectInfo& object) {
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
  if (value == "dcd") return io::TrajectoryFormat::dcd;
  if (value == "trr") return io::TrajectoryFormat::trr;
  if (value == "xtc") return io::TrajectoryFormat::xtc;
  return io::TrajectoryFormat::auto_detect;
}

trajectory::PlaybackMode playback_mode(std::string_view value) {
  if (value == "loop") return trajectory::PlaybackMode::loop;
  if (value == "rock") return trajectory::PlaybackMode::rock;
  return trajectory::PlaybackMode::once;
}

trajectory::PlaybackDirection playback_direction(std::string_view value) {
  return value == "reverse" ? trajectory::PlaybackDirection::reverse
                            : trajectory::PlaybackDirection::forward;
}

std::string_view playback_mode_name(trajectory::PlaybackMode mode) {
  if (mode == trajectory::PlaybackMode::loop) return "loop";
  if (mode == trajectory::PlaybackMode::rock) return "rock";
  return "once";
}

std::string_view playback_direction_name(
    trajectory::PlaybackDirection direction) {
  return direction == trajectory::PlaybackDirection::reverse ? "reverse"
                                                              : "forward";
}

command::Response trajectory_frame_response(
    std::string summary, const TrajectoryFrameResult& result) {
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

}  // namespace

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
  const auto version_error = registry.add(
      Descriptor{"system version", "Print the MolShredder version", {}, {}},
      [](const Arguments&, TaskContext&) {
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
  auto trajectory_descriptors = command::trajectory_command_descriptors();
  descriptors.insert(descriptors.end(),
                     std::make_move_iterator(object_descriptors.begin()),
                     std::make_move_iterator(object_descriptors.end()));
  descriptors.insert(descriptors.end(),
                     std::make_move_iterator(trajectory_descriptors.begin()),
                     std::make_move_iterator(trajectory_descriptors.end()));
  for (auto descriptor : std::move(descriptors)) {
    const auto canonical_name = descriptor.canonical_name;
    command::Handler handler;
    if (canonical_name == "object list") {
      handler = [workspace](const Arguments&, TaskContext&) {
        const auto objects = workspace->list_objects();
        command::Table table;
        table.columns = {"object_id", "name", "active", "visible",
                         "effectively_visible", "atom_count", "frame_count",
                         "representation_count", "has_trajectory",
                         "scene_node_id"};
        table.rows.reserve(objects.size());
        command::Value active_object_id{nullptr};
        for (const auto& object : objects) {
          table.rows.push_back(
              {object.id, object.name, object.active, object.visible,
               object.effectively_visible, object.atom_count,
               optional_frame_count(object.frame_count),
               object.representation_count, object.has_trajectory,
               object.scene_node_id});
          if (object.active) active_object_id = object.id;
        }
        return Result<Response>::success(
            {"listed " + std::to_string(objects.size()) + " objects",
             {{"active_object_id", std::move(active_object_id)},
              {"object_count", objects.size()}},
             std::move(table)});
      };
    } else if (canonical_name == "object activate") {
      handler = [workspace](const Arguments& arguments, TaskContext&) {
        const auto id = size_argument(arguments, "id", true);
        if (!id.has_value()) return Result<Response>::failure(id.error());
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
      handler = [workspace](const Arguments& arguments, TaskContext&) {
        const auto id = size_argument(arguments, "id", true);
        if (!id.has_value()) return Result<Response>::failure(id.error());
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
      handler = [workspace](const Arguments& arguments, TaskContext&) {
        std::optional<std::string> name;
        if (const auto found = arguments.find("name");
            found != arguments.end()) {
          name = found->second;
        }
        const auto loaded = workspace->load_structure(
            arguments.at("path"), std::move(name), structure_format(arguments));
        if (!loaded.has_value()) return Result<Response>::failure(loaded.error());
        return Result<Response>::success(
            {"loaded " + loaded.value().object_name,
             {{"atom_count", static_cast<std::uint64_t>(loaded.value().atom_count)},
              {"format", std::string{io::to_string(loaded.value().format)}},
              {"frame_count", static_cast<std::uint64_t>(loaded.value().frame_count)},
              {"object_id", loaded.value().object_id},
              {"object_name", loaded.value().object_name}}});
      };
    } else if (canonical_name == "select") {
      handler = [workspace](const Arguments& arguments, TaskContext&) {
        const auto error = workspace->set_named_selection(
            arguments.at("name"), arguments.at("expression"),
            arguments.at("update") == "true");
        if (error.has_value()) return Result<Response>::failure(error.value());
        return Result<Response>::success(
            {"selection " + arguments.at("name") + " defined",
             {{"dynamic", arguments.at("update") == "true"},
              {"expression", arguments.at("expression")},
              {"name", arguments.at("name")}}});
      };
    } else if (canonical_name == "show") {
      handler = [workspace](const Arguments& arguments, TaskContext&) {
        const auto replace_argument = arguments.find("replace");
        const auto replace = replace_argument != arguments.end() &&
                             replace_argument->second == "true";
        const auto shown = workspace->show(
            representation_kind(arguments.at("representation")),
            arguments.at("selection"), replace);
        if (!shown.has_value()) return Result<Response>::failure(shown.error());
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
      handler = [workspace](const Arguments& arguments, TaskContext&) {
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
        const auto& center = analyzed.value().center;
        const auto component = [&](double value) {
          return rounded(convert_length(value, center.coordinate_unit,
                                        target_unit),
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
          fields.emplace("mass_unit",
                         center.mass_unit.value_or("unspecified"));
          fields.emplace("total_mass", center.total_mass.value());
        }
        return Result<Response>::success(
            {arguments.at("mode") + " calculated", std::move(fields)});
      };
    } else if (canonical_name == "measure distance") {
      handler = [workspace](const Arguments& arguments, TaskContext&) {
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
        const auto& distance = measured.value().distance;
        const auto component = [&](double value) {
          return rounded(convert_length(value, distance.coordinate_unit,
                                        target_unit),
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
      handler = [workspace](const Arguments& arguments, TaskContext&) {
        const auto energy=number_argument(arguments,"energy-cutoff");
        const auto helix=number_argument(arguments,"helix-propensity");
        const auto beta=number_argument(arguments,"beta-propensity");
        if(!energy.has_value()) return Result<Response>::failure(energy.error());
        if(!helix.has_value()) return Result<Response>::failure(helix.error());
        if(!beta.has_value()) return Result<Response>::failure(beta.error());
        const auto analyzed=workspace->analyze_secondary_structure(
            arguments.at("selection"),{energy.value(),helix.value(),beta.value()});
        if(!analyzed.has_value()) return Result<Response>::failure(analyzed.error());
        const auto* object=workspace->active_object();
        if(object==nullptr) return Result<Response>::failure(operation::Error{
            operation::ErrorCode::internal,"secondary-structure result lost its topology",{}});
        const auto precision=static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        table.columns={"residue_index","residue_name","residue_number","insertion_code",
          "chain","segment","code","state","phi_degrees","psi_degrees","backbone_complete"};
        command::Value::Object counts;
        std::size_t selected_count{};
        for(const auto& row:analyzed.value().assignment.residues){
          if(!analyzed.value().selected_residues[row.residue.value])continue;
          ++selected_count; const auto& residue=object->system->topology()->residues()[row.residue.value];
          const auto state=std::string{analysis::to_string(row.state)};
          const auto found=counts.find(state);
          const auto old=found==counts.end()?0U:static_cast<std::size_t>(std::get<std::uint64_t>(found->second.data));
          counts[state]=old+1U;
          table.rows.push_back({row.residue.value+1U,residue.name,residue.sequence_number,
            residue.insertion_code,residue.chain_id,residue.segment_id,
            std::string(1,analysis::stride_code(row.state)),state,
            row.phi_degrees?command::Value{precise(*row.phi_degrees,precision)}:command::Value{nullptr},
            row.psi_degrees?command::Value{precise(*row.psi_degrees,precision)}:command::Value{nullptr},
            row.backbone_complete});
        }
        return Result<Response>::success({"secondary structure assigned",
          {{"exact_stride_parity",false},{"hydrogen_bond_count",analyzed.value().assignment.hydrogen_bonds.size()},
           {"method",std::string{analyzed.value().assignment.method}},{"object_id",analyzed.value().object_id},
           {"precision",precision},{"selected_residue_count",selected_count},
           {"selection",analyzed.value().selection_expression},{"state_counts",counts}},std::move(table)});
      };
    } else if (canonical_name == "analyze contacts") {
      handler = [workspace](const Arguments& arguments, TaskContext&) {
        const auto parsed_cutoff = number_argument(arguments, "cutoff");
        if (!parsed_cutoff.has_value())
          return Result<Response>::failure(parsed_cutoff.error());
        const auto same_selection = !arguments.contains("second");
        const auto second = same_selection ? arguments.at("first")
                                           : arguments.at("second");
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
        table.columns = {"first_atom_index", "second_atom_index", "dx", "dy",
                         "dz", "distance", "unit", "pbc"};
        table.rows.reserve(analyzed.value().contacts.pairs.size());
        for (const auto& pair : analyzed.value().contacts.pairs) {
          const auto component = [&](double value) {
            return precise(convert_length(value,
                analyzed.value().contacts.coordinate_unit, target_unit), precision);
          };
          table.rows.push_back({pair.first.value + 1U, pair.second.value + 1U,
                                component(pair.displacement.x),
                                component(pair.displacement.y),
                                component(pair.displacement.z),
                                component(pair.distance),
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
      handler = [workspace](const Arguments& arguments, TaskContext&) {
        const auto cutoff=number_argument(arguments,"cutoff");
        const auto angle=number_argument(arguments,"angle");
        if(!cutoff.has_value()) return Result<Response>::failure(cutoff.error());
        if(!angle.has_value()) return Result<Response>::failure(angle.error());
        const auto same_selection=!arguments.contains("acceptors");
        const auto acceptors=same_selection ? arguments.at("donors") : arguments.at("acceptors");
        const auto boundary=arguments.at("pbc")=="minimum-image"
            ? analysis::DistanceBoundary::minimum_image : analysis::DistanceBoundary::raw;
        const auto target_unit=length_unit(arguments.at("unit"));
        const auto analyzed=workspace->analyze_hydrogen_bonds(
            arguments.at("donors"),acceptors,cutoff.value(),angle.value(),
            boundary,same_selection,target_unit);
        if(!analyzed.has_value()) return Result<Response>::failure(analyzed.error());
        const auto precision=static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        table.columns={"donor_atom_index","acceptor_atom_index","hydrogen_atom_index",
                       "donor_acceptor_distance","unit","angle_deviation_degrees"};
        table.rows.reserve(analyzed.value().hydrogen_bonds.bonds.size());
        for(const auto& bond:analyzed.value().hydrogen_bonds.bonds) {
          table.rows.push_back({bond.donor.value+1U,bond.acceptor.value+1U,
                                bond.hydrogen.value+1U,
                                precise(convert_length(bond.donor_acceptor_distance,
                                  analyzed.value().hydrogen_bonds.coordinate_unit,target_unit),precision),
                                std::string{length_unit_name(target_unit)},
                                precise(bond.angle_deviation_degrees,precision)});
        }
        return Result<Response>::success(
            {"hydrogen bonds calculated",
             {{"acceptors",analyzed.value().acceptor_expression},
              {"angle",precise(angle.value(),precision)},
              {"bond_count",analyzed.value().hydrogen_bonds.bonds.size()},
              {"cutoff",precise(cutoff.value(),precision)},
              {"donor_typing_source",analyzed.value().typing.donor_source},
              {"donors",analyzed.value().donor_expression},
              {"object_id",analyzed.value().object_id},
              {"pbc",arguments.at("pbc")},
              {"acceptor_typing_source",analyzed.value().typing.acceptor_source},
              {"typing_estimated",analyzed.value().typing.estimated},
              {"unit",std::string{length_unit_name(target_unit)}}},
             std::move(table)});
      };
    } else if (canonical_name == "analyze trajectory center") {
      handler = [workspace](const Arguments& arguments, TaskContext& context) {
        const auto range = series_range(arguments, *workspace);
        if (!range.has_value()) return Result<Response>::failure(range.error());
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
        table.columns = {"frame", "source_step", "physical_time",
                         "physical_time_unit", "x", "y", "z", "unit",
                         "selected_atom_count", "used_atom_count",
                         "skipped_missing_atom_count", "total_mass",
                         "mass_unit", "mass_source", "mass_estimated"};
        table.rows.reserve(analyzed.value().rows.size());
        for (const auto& row : analyzed.value().rows) {
          const auto component = [&](double value) {
            return rounded(convert_length(value, row.center.coordinate_unit,
                                          target_unit),
                           precision);
          };
          table.rows.push_back(
              {row.frame.frame_index,
               optional_step(row.frame),
               optional_time(row.frame),
               optional_time_unit(row.frame),
               precise(component(row.center.position.x), precision),
               precise(component(row.center.position.y), precision),
               precise(component(row.center.position.z), precision),
               std::string{length_unit_name(target_unit)},
               row.center.selected_atom_count,
               row.center.used_atom_count,
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
      handler = [workspace](const Arguments& arguments, TaskContext& context) {
        const auto range = series_range(arguments, *workspace);
        if (!range.has_value()) return Result<Response>::failure(range.error());
        const auto boundary = arguments.at("pbc") == "minimum-image"
                                  ? analysis::DistanceBoundary::minimum_image
                                  : analysis::DistanceBoundary::raw;
        const auto analyzed = workspace->analyze_distance_time_series(
            arguments.at("from"), arguments.at("to"), boundary,
            range.value(), context);
        if (!analyzed.has_value()) {
          return Result<Response>::failure(analyzed.error());
        }
        const auto target_unit = length_unit(arguments.at("unit"));
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        table.columns = {"frame", "source_step", "physical_time",
                         "physical_time_unit", "first_atom_index",
                         "second_atom_index", "dx", "dy", "dz",
                         "distance", "unit", "pbc"};
        table.rows.reserve(analyzed.value().rows.size());
        for (const auto& row : analyzed.value().rows) {
          const auto component = [&](double value) {
            return rounded(convert_length(value, row.distance.coordinate_unit,
                                          target_unit),
                           precision);
          };
          table.rows.push_back(
              {row.frame.frame_index,
               optional_step(row.frame),
               optional_time(row.frame),
               optional_time_unit(row.frame),
               row.distance.first.value + 1U,
               row.distance.second.value + 1U,
               precise(component(row.distance.displacement.x), precision),
               precise(component(row.distance.displacement.y), precision),
               precise(component(row.distance.displacement.z), precision),
               precise(component(row.distance.distance), precision),
               std::string{length_unit_name(target_unit)}, arguments.at("pbc")});
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
      handler = [workspace](const Arguments& arguments, TaskContext& context) {
        const auto range = series_range(arguments, *workspace);
        const auto reference = size_argument(arguments, "reference");
        if (!range.has_value()) return Result<Response>::failure(range.error());
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
        table.columns = {"frame", "source_step", "physical_time",
                         "physical_time_unit", "reference_frame", "rmsd",
                         "rmsd_before_fit", "unit", "selected_atom_count",
                         "paired_atom_count", "skipped_missing_atom_count",
                         "effective_atom_count", "weight_sum",
                         "fit_paired_atom_count", "fit", "weight"};
        table.rows.reserve(analyzed.value().rows.size());
        for (const auto& row : analyzed.value().rows) {
          const auto converted = [&](double value) {
            return rounded(convert_length(value, row.rmsd.coordinate_unit,
                                          target_unit),
                           precision);
          };
          table.rows.push_back(
              {row.frame.frame_index,
               optional_step(row.frame),
               optional_time(row.frame),
               optional_time_unit(row.frame),
               analyzed.value().reference_frame,
               precise(converted(row.rmsd.rmsd), precision),
               precise(converted(row.rmsd_before_fit), precision),
               std::string{length_unit_name(target_unit)},
               row.rmsd.selected_atom_count,
               row.rmsd.paired_atom_count,
               row.rmsd.skipped_missing_atom_count,
               row.rmsd.effective_atom_count,
               row.rmsd.weight_sum,
               row.fit_paired_atom_count,
               arguments.at("fit"),
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
        return Result<Response>::success(
            {"trajectory RMSD calculated", std::move(fields),
             std::move(table)});
      };
    } else if (canonical_name == "analyze trajectory rmsf") {
      handler = [workspace](const Arguments& arguments, TaskContext& context) {
        const auto range = series_range(arguments, *workspace);
        const auto reference = size_argument(arguments, "reference");
        if (!range.has_value()) return Result<Response>::failure(range.error());
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
        const auto* object = workspace->active_object();
        if (object == nullptr) {
          return Result<Response>::failure(operation::Error{
              operation::ErrorCode::internal,
              "RMSF result lost its active topology", {}});
        }
        const auto target_unit = length_unit(arguments.at("unit"));
        const auto precision =
            static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        table.columns = {"atom_index", "source_serial", "atom_name",
                         "residue_name", "residue_number", "insertion_code",
                         "chain", "segment", "observation_count", "rmsf",
                         "unit"};
        table.rows.reserve(analyzed.value().series.atoms.size());
        for (const auto& row : analyzed.value().series.atoms) {
          const auto& atom = object->system->topology()->atoms()[row.atom.value];
          const auto& residue =
              object->system->topology()->residues()[atom.residue.value];
          const auto rmsf = row.rmsf.has_value()
                                ? precise(convert_length(
                                              *row.rmsf,
                                              analyzed.value()
                                                  .series.coordinate_unit,
                                              target_unit),
                                          precision)
                                : command::Value{nullptr};
          table.rows.push_back(
              {row.atom.value + 1U,
               atom.source_serial.has_value()
                   ? command::Value{*atom.source_serial}
                   : command::Value{nullptr},
               atom.name,
               residue.name,
               residue.sequence_number,
               residue.insertion_code,
               residue.chain_id,
               residue.segment_id,
               row.observation_count,
               rmsf,
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
        return Result<Response>::success(
            {"trajectory RMSF calculated", std::move(fields),
             std::move(table)});
      };
    } else if (canonical_name == "analyze trajectory contacts") {
      handler = [workspace](const Arguments& arguments, TaskContext& context) {
        const auto range=series_range(arguments,*workspace);
        const auto cutoff=number_argument(arguments,"cutoff");
        if(!range.has_value()) return Result<Response>::failure(range.error());
        if(!cutoff.has_value()) return Result<Response>::failure(cutoff.error());
        const auto same=!arguments.contains("selection2");
        const auto second=same ? arguments.at("selection1") : arguments.at("selection2");
        const auto boundary=arguments.at("pbc")=="minimum-image"
            ? analysis::DistanceBoundary::minimum_image : analysis::DistanceBoundary::raw;
        const auto target_unit=length_unit(arguments.at("unit"));
        const auto analyzed=workspace->analyze_contact_time_series(
            arguments.at("selection1"),second,cutoff.value(),target_unit,boundary,
            same,arguments.at("exclude-bonded")=="true",
            arguments.at("report")=="occupancy",range.value(),context);
        if(!analyzed.has_value()) return Result<Response>::failure(analyzed.error());
        const auto precision=static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        if(arguments.at("report")=="frames") {
          table.columns={"frame","source_step","physical_time","physical_time_unit","contact_count"};
          for(const auto& row:analyzed.value().series.frames)
            table.rows.push_back({row.frame.frame_index,optional_step(row.frame),optional_time(row.frame),
                                  optional_time_unit(row.frame),row.interaction_count});
        } else {
          table.columns={"first_atom_index","second_atom_index","observation_count",
                         "occupancy","mean_distance","unit"};
          for(const auto& row:analyzed.value().series.occupancy)
            table.rows.push_back({row.first.value+1U,row.second.value+1U,row.observation_count,
                                  precise(row.occupancy,precision),precise(row.mean_distance,precision),
                                  std::string{length_unit_name(target_unit)}});
        }
        return Result<Response>::success({"trajectory contacts calculated",
          {{"cutoff",precise(cutoff.value(),precision)},
           {"exclude_bonded",arguments.at("exclude-bonded")=="true"},
           {"first_frame",range.value().first},{"frame_count",analyzed.value().series.frame_count},
           {"last_frame",range.value().last},{"object_id",analyzed.value().object_id},
           {"pair_count",analyzed.value().series.occupancy.size()},
           {"pbc",arguments.at("pbc")},{"report",arguments.at("report")},
           {"precision",precision},
           {"selection1",analyzed.value().first_expression},
           {"selection2",analyzed.value().second_expression},{"stride",range.value().stride},
           {"unit",std::string{length_unit_name(target_unit)}}},std::move(table)});
      };
    } else if (canonical_name == "analyze trajectory hbonds") {
      handler = [workspace](const Arguments& arguments, TaskContext& context) {
        const auto range=series_range(arguments,*workspace);
        const auto cutoff=number_argument(arguments,"cutoff");
        const auto angle=number_argument(arguments,"angle");
        if(!range.has_value()) return Result<Response>::failure(range.error());
        if(!cutoff.has_value()) return Result<Response>::failure(cutoff.error());
        if(!angle.has_value()) return Result<Response>::failure(angle.error());
        const auto same=!arguments.contains("acceptors");
        const auto acceptors=same ? arguments.at("donors") : arguments.at("acceptors");
        const auto boundary=arguments.at("pbc")=="minimum-image"
            ? analysis::DistanceBoundary::minimum_image : analysis::DistanceBoundary::raw;
        const auto target_unit=length_unit(arguments.at("unit"));
        const auto analyzed=workspace->analyze_hydrogen_bond_time_series(
            arguments.at("donors"),acceptors,cutoff.value(),target_unit,angle.value(),
            boundary,same,arguments.at("report")=="occupancy",range.value(),context);
        if(!analyzed.has_value()) return Result<Response>::failure(analyzed.error());
        const auto precision=static_cast<unsigned int>(std::stoul(arguments.at("precision")));
        command::Table table;
        if(arguments.at("report")=="frames") {
          table.columns={"frame","source_step","physical_time","physical_time_unit","hbond_count"};
          for(const auto& row:analyzed.value().series.frames)
            table.rows.push_back({row.frame.frame_index,optional_step(row.frame),optional_time(row.frame),
                                  optional_time_unit(row.frame),row.interaction_count});
        } else {
          table.columns={"donor_atom_index","acceptor_atom_index","hydrogen_atom_index",
                         "observation_count","occupancy","mean_donor_acceptor_distance",
                         "unit","mean_angle_deviation_degrees"};
          for(const auto& row:analyzed.value().series.occupancy)
            table.rows.push_back({row.donor.value+1U,row.acceptor.value+1U,row.hydrogen.value+1U,
                                  row.observation_count,precise(row.occupancy,precision),
                                  precise(row.mean_donor_acceptor_distance,precision),
                                  std::string{length_unit_name(target_unit)},
                                  precise(row.mean_angle_deviation_degrees,precision)});
        }
        return Result<Response>::success({"trajectory hydrogen bonds calculated",
          {{"acceptor_typing_source",analyzed.value().typing.acceptor_source},
           {"acceptors",analyzed.value().acceptor_expression},
           {"angle",precise(angle.value(),precision)},{"bond_count",analyzed.value().series.occupancy.size()},
           {"cutoff",precise(cutoff.value(),precision)},
           {"donor_typing_source",analyzed.value().typing.donor_source},
           {"donors",analyzed.value().donor_expression},{"first_frame",range.value().first},
           {"frame_count",analyzed.value().series.frame_count},{"last_frame",range.value().last},
           {"object_id",analyzed.value().object_id},{"pbc",arguments.at("pbc")},
           {"report",arguments.at("report")},{"stride",range.value().stride},
           {"precision",precision},
           {"typing_estimated",analyzed.value().typing.estimated},
           {"unit",std::string{length_unit_name(target_unit)}}},std::move(table)});
      };
    } else if (canonical_name == "traj load") {
      handler = [workspace](const Arguments& arguments, TaskContext&) {
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
        const auto loaded = workspace->load_trajectory(
            arguments.at("path"), trajectory_format(arguments.at("file-format")),
            cache_mib.value() * bytes_per_mib, prefetch_frames.value());
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
        auto& fields = response.fields;
        fields.emplace("prefetch_frame_count",
                       static_cast<std::uint64_t>(
                           loaded.value().prefetch_frame_count));
        fields.emplace("prefetch_generation",
                       loaded.value().prefetch.generation);
        fields.emplace("prefetch_requested_count",
                       static_cast<std::uint64_t>(
                           loaded.value().prefetch.frame_indices.size()));
        fields.emplace("prefetch_state",
                       std::string{trajectory::to_string(
                           loaded.value().prefetch.state)});
        return Result<Response>::success(std::move(response));
      };
    } else if (canonical_name == "traj frame") {
      handler = [workspace](const Arguments& arguments, TaskContext&) {
        const auto frame = size_argument(arguments, "frame");
        if (!frame.has_value()) return Result<Response>::failure(frame.error());
        const auto selected = workspace->set_trajectory_frame(frame.value());
        if (!selected.has_value()) {
          return Result<Response>::failure(selected.error());
        }
        return Result<Response>::success(trajectory_frame_response(
            "trajectory frame selected", selected.value()));
      };
    } else if (canonical_name == "traj play") {
      handler = [workspace](const Arguments& arguments, TaskContext&) {
        const auto steps = size_argument(arguments, "steps");
        if (!steps.has_value()) return Result<Response>::failure(steps.error());
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
      handler = [workspace](const Arguments& arguments, TaskContext&) {
        const auto first = size_argument(arguments, "first");
        const auto stride = size_argument(arguments, "stride", true);
        if (!first.has_value()) return Result<Response>::failure(first.error());
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
      handler = [workspace](const Arguments&, TaskContext&) {
        const auto paused = workspace->pause_trajectory();
        if (!paused.has_value()) {
          return Result<Response>::failure(paused.error());
        }
        return Result<Response>::success(
            trajectory_frame_response("trajectory paused", paused.value()));
      };
    } else if (canonical_name == "traj speed") {
      handler = [workspace](const Arguments& arguments, TaskContext&) {
        const auto fps = number_argument(arguments, "fps");
        if (!fps.has_value()) return Result<Response>::failure(fps.error());
        const auto configured = workspace->set_trajectory_speed(fps.value());
        if (!configured.has_value()) {
          return Result<Response>::failure(configured.error());
        }
        return Result<Response>::success(trajectory_frame_response(
            "trajectory speed configured", configured.value()));
      };
    } else if (canonical_name == "traj tick") {
      handler = [workspace](const Arguments& arguments, TaskContext&) {
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
      handler = [canonical_name](const Arguments&, TaskContext&) {
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

}  // namespace molshredder::application
