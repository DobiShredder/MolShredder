#include "molshredder/trajectory/attachment.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::trajectory {
namespace {

using operation::Error;
using operation::ErrorCode;

Error invalid(std::string message, std::string suggestion = {},
              std::map<std::string, std::string, std::less<>> details = {}) {
  return {ErrorCode::invalid_argument, std::move(message),
          std::move(suggestion), std::move(details)};
}

template <typename Value>
std::string identity_text(const std::optional<Value>& value) {
  if (!value.has_value()) return "<missing>";
  if constexpr (std::is_same_v<Value, std::string>) {
    return *value;
  } else {
    return std::to_string(*value);
  }
}

TrajectoryAtomIdentity topology_identity(const model::Topology& topology,
                                         std::size_t index) {
  const auto& atom = topology.atoms()[index];
  const auto& residue = topology.residues()[atom.residue.value];
  return {atom.source_serial,
          atom.name,
          residue.name,
          residue.sequence_number,
          residue.insertion_code,
          residue.chain_id,
          residue.segment_id,
          atom.atomic_number};
}

template <typename Scalar>
std::vector<model::Vec3<Scalar>> scale_vectors(
    const std::vector<model::Vec3<Scalar>>& values, double factor) {
  std::vector<model::Vec3<Scalar>> result;
  result.reserve(values.size());
  for (const auto& value : values) {
    result.push_back({static_cast<Scalar>(value.x * factor),
                      static_cast<Scalar>(value.y * factor),
                      static_cast<Scalar>(value.z * factor)});
  }
  return result;
}

model::CoordinateBuffer scale_buffer(const model::CoordinateBuffer& buffer,
                                     double factor) {
  return std::visit(
      [factor](const auto& values) -> model::CoordinateBuffer {
        return model::CoordinateBuffer{scale_vectors(values, factor)};
      },
      buffer.values());
}

bool finite_property(const model::AtomProperty& property) {
  return std::visit(
      [](const auto& values) {
        using Values = std::decay_t<decltype(values)>;
        if constexpr (std::is_same_v<Values, std::vector<float>> ||
                      std::is_same_v<Values, std::vector<double>>) {
          return std::all_of(values.begin(), values.end(),
                             [](auto value) { return std::isfinite(value); });
        }
        return true;
      },
      property.values);
}

void scale_property(model::AtomProperty& property, double factor) {
  std::visit(
      [factor](auto& values) {
        using Values = std::decay_t<decltype(values)>;
        if constexpr (std::is_same_v<Values, std::vector<float>> ||
                      std::is_same_v<Values, std::vector<double>>) {
          for (auto& value : values)
            value = static_cast<typename Values::value_type>(value * factor);
        }
      },
      property.values);
}

struct ForceTriplet {
  std::array<std::string, 3U> names;
};

std::optional<ForceTriplet> force_triplet(
    const model::FrameMetadata& metadata) {
  constexpr std::array<std::array<std::string_view, 3U>, 4U> candidates{{
      {"force_x", "force_y", "force_z"},
      {"force.x", "force.y", "force.z"},
      {"h5md.force.x", "h5md.force.y", "h5md.force.z"},
      {"amber.force.x", "amber.force.y", "amber.force.z"}}};
  std::optional<ForceTriplet> found;
  for (const auto& candidate : candidates) {
    const auto count = static_cast<std::size_t>(std::count_if(
        candidate.begin(), candidate.end(), [&](std::string_view name) {
          return metadata.atom_properties.contains(name);
        }));
    if (count != 0U && count != 3U)
      return ForceTriplet{{"__incomplete__", "", ""}};
    if (count == 3U) {
      if (found.has_value())
        return ForceTriplet{{"__ambiguous__", "", ""}};
      found = ForceTriplet{{std::string{candidate[0]},
                            std::string{candidate[1]},
                            std::string{candidate[2]}}};
    }
  }
  return found;
}

operation::Result<std::pair<std::shared_ptr<const model::CoordinateFrame>,
                            TrajectorySemanticReport>>
normalize(const model::CoordinateFrame& frame) {
  auto metadata = frame.metadata();
  TrajectorySemanticReport report;
  report.channels.has_source_step = metadata.source_step.has_value();
  report.channels.has_physical_time = metadata.physical_time.has_value();
  report.channels.has_unit_cell = metadata.unit_cell.has_value();
  report.channels.has_velocities = frame.velocities().has_value();
  report.channels.source_coordinate_unit = metadata.coordinate_unit;
  if (metadata.physical_time.has_value())
    report.channels.physical_time_unit = metadata.physical_time->unit;
  if (frame.velocities().has_value()) {
    if (!metadata.velocity_time_unit.has_value())
      return operation::Result<std::pair<
          std::shared_ptr<const model::CoordinateFrame>,
          TrajectorySemanticReport>>::failure(
          invalid("trajectory velocity channel has no time unit",
                  "supply a reader that records the velocity time unit"));
    report.channels.velocity_time_unit = *metadata.velocity_time_unit;
  } else if (metadata.velocity_time_unit.has_value()) {
    return operation::Result<std::pair<
        std::shared_ptr<const model::CoordinateFrame>,
        TrajectorySemanticReport>>::failure(
        invalid("trajectory declares a velocity unit without velocities"));
  }

  const auto force = force_triplet(metadata);
  if (force.has_value() && force->names[0] == "__incomplete__")
    return operation::Result<std::pair<
        std::shared_ptr<const model::CoordinateFrame>,
        TrajectorySemanticReport>>::failure(
        invalid("trajectory force vector must contain all x/y/z components"));
  if (force.has_value() && force->names[0] == "__ambiguous__")
    return operation::Result<std::pair<
        std::shared_ptr<const model::CoordinateFrame>,
        TrajectorySemanticReport>>::failure(
        invalid("trajectory contains more than one force vector namespace"));
  report.channels.has_forces = force.has_value();
  if (force.has_value()) {
    const auto& first = metadata.atom_properties.at(force->names[0]);
    if (!first.metadata.unit.has_value() || first.metadata.unit->empty())
      return operation::Result<std::pair<
          std::shared_ptr<const model::CoordinateFrame>,
          TrajectorySemanticReport>>::failure(
          invalid("trajectory force channel has no unit"));
    const auto unit = *first.metadata.unit;
    report.channels.source_force_unit = unit;
    for (const auto& name : force->names) {
      const auto& property = metadata.atom_properties.at(name);
      if (!property.metadata.unit.has_value() ||
          *property.metadata.unit != unit)
        return operation::Result<std::pair<
            std::shared_ptr<const model::CoordinateFrame>,
            TrajectorySemanticReport>>::failure(
            invalid("trajectory force component units do not match"));
      if (!finite_property(property))
        return operation::Result<std::pair<
            std::shared_ptr<const model::CoordinateFrame>,
            TrajectorySemanticReport>>::failure(
            invalid("trajectory force channel contains a non-finite value"));
    }
    if (unit == "kJ mol^-1 nm^-1" || unit == "kJ mol-1 nm-1") {
      for (const auto& name : force->names) {
        auto& property = metadata.atom_properties.at(name);
        scale_property(property, 0.1);
        property.metadata.unit = "kJ mol^-1 angstrom^-1";
      }
      report.force_conversion_applied = true;
      report.channels.force_unit = "kJ mol^-1 angstrom^-1";
    } else if (unit == "kJ mol^-1 angstrom^-1" ||
               unit == "kJ mol-1 angstrom-1") {
      for (const auto& name : force->names)
        metadata.atom_properties.at(name).metadata.unit =
            "kJ mol^-1 angstrom^-1";
      report.channels.force_unit = "kJ mol^-1 angstrom^-1";
    } else if (unit == "kilocalorie/mole/angstrom") {
      for (const auto& name : force->names) {
        auto& property = metadata.atom_properties.at(name);
        scale_property(property, 4.184);
        property.metadata.unit = "kJ mol^-1 angstrom^-1";
      }
      report.force_conversion_applied = true;
      report.channels.force_unit = "kJ mol^-1 angstrom^-1";
    } else {
      return operation::Result<std::pair<
          std::shared_ptr<const model::CoordinateFrame>,
          TrajectorySemanticReport>>::failure(
          invalid("unsupported trajectory force unit: " + unit,
                  "use kJ mol^-1 angstrom^-1, kJ mol^-1 nm^-1, or "
                  "kilocalorie/mole/angstrom"));
    }
  }

  const double length_factor =
      metadata.coordinate_unit == operation::LengthUnit::nanometer ? 10.0 : 1.0;
  auto positions = scale_buffer(frame.positions(), length_factor);
  std::optional<model::CoordinateBuffer> velocities;
  if (frame.velocities().has_value())
    velocities = scale_buffer(*frame.velocities(), length_factor);
  if (metadata.unit_cell.has_value() && length_factor != 1.0) {
    metadata.unit_cell->a = scale_vectors(
        std::vector<model::Vec3d>{metadata.unit_cell->a}, length_factor)[0];
    metadata.unit_cell->b = scale_vectors(
        std::vector<model::Vec3d>{metadata.unit_cell->b}, length_factor)[0];
    metadata.unit_cell->c = scale_vectors(
        std::vector<model::Vec3d>{metadata.unit_cell->c}, length_factor)[0];
  }
  report.coordinate_conversion_applied = length_factor != 1.0;
  metadata.coordinate_unit = operation::LengthUnit::angstrom;
  if (metadata.physical_time.has_value() &&
      metadata.physical_time->unit == model::TimeUnit::femtosecond) {
    metadata.physical_time->value /= 1000.0;
    metadata.physical_time->unit = model::TimeUnit::picosecond;
    report.time_conversion_applied = true;
  }
  if (metadata.velocity_time_unit.has_value()) {
    if (*metadata.velocity_time_unit == model::TimeUnit::femtosecond &&
        velocities.has_value()) {
      velocities = scale_buffer(*velocities, 1000.0);
      metadata.velocity_time_unit = model::TimeUnit::picosecond;
      report.time_conversion_applied = true;
    }
  }
  metadata.fields.insert_or_assign("molshredder.semantic_schema",
                                   report.schema);
  metadata.fields.insert_or_assign("molshredder.coordinate_unit",
                                   "angstrom");
  metadata.fields.insert_or_assign("molshredder.time_unit", "picosecond");
  const auto normalized = model::CoordinateFrame::create(
      std::move(positions), std::move(velocities), frame.presence(),
      std::move(metadata));
  if (!normalized.has_value())
    return operation::Result<std::pair<
        std::shared_ptr<const model::CoordinateFrame>,
        TrajectorySemanticReport>>::failure(normalized.error());
  return operation::Result<std::pair<
      std::shared_ptr<const model::CoordinateFrame>,
      TrajectorySemanticReport>>::success(
      {normalized.value(), std::move(report)});
}

std::optional<Error> compare_channels(const TrajectoryChannelContract& expected,
                                      const TrajectoryChannelContract& actual,
                                      std::size_t frame_index) {
  if (expected != actual)
    return invalid("trajectory channel availability or unit changed at frame " +
                       std::to_string(frame_index),
                   "use a trajectory with consistent step/time/cell/velocity/"
                   "force channels",
                   {{"frame", std::to_string(frame_index)}});
  return std::nullopt;
}

std::optional<Error> compare_adjacent(const model::CoordinateFrame& previous,
                                      const model::CoordinateFrame& current,
                                      std::size_t frame_index) {
  if (current.metadata().source_step.has_value() &&
      previous.metadata().source_step.has_value() &&
      *current.metadata().source_step <= *previous.metadata().source_step)
    return invalid("trajectory source step is not strictly increasing at frame " +
                       std::to_string(frame_index),
                   {}, {{"frame", std::to_string(frame_index)}});
  if (current.metadata().physical_time.has_value() &&
      previous.metadata().physical_time.has_value() &&
      current.metadata().physical_time->value <=
          previous.metadata().physical_time->value)
    return invalid("trajectory physical time is not strictly increasing at frame " +
                       std::to_string(frame_index),
                   {}, {{"frame", std::to_string(frame_index)}});
  return std::nullopt;
}

}  // namespace

std::string_view to_string(AtomMappingPolicy policy) noexcept {
  switch (policy) {
    case AtomMappingPolicy::exact:
      return "exact";
    case AtomMappingPolicy::index_order:
      return "index";
    case AtomMappingPolicy::explicit_map:
      return "explicit";
  }
  return "index";
}

operation::Result<AtomMappingReport> resolve_atom_mapping(
    const AtomMappingRequest& request) {
  if (request.topology == nullptr)
    return operation::Result<AtomMappingReport>::failure(
        invalid("trajectory mapping requires a topology"));
  const auto count = request.topology->atom_count();
  if (count == 0U)
    return operation::Result<AtomMappingReport>::failure(
        invalid("trajectory atom mapping requires at least one atom"));
  if (request.trajectory_atom_count != count)
    return operation::Result<AtomMappingReport>::failure(
        invalid("trajectory atom count " +
                    std::to_string(request.trajectory_atom_count) +
                    " does not match topology atom count " +
                    std::to_string(count),
                "attach a matching topology or provide a complete explicit map"));

  AtomMappingReport report;
  report.policy = request.policy;
  report.topology_version = request.topology->version();
  report.target_to_source.resize(count);
  if (request.policy == AtomMappingPolicy::index_order) {
    for (std::size_t index = 0; index < count; ++index)
      report.target_to_source[index] = index;
    report.identity_strength = "ordinal-only-explicit-opt-in";
    report.compared_axes = {"atom_count", "ordinal"};
    return operation::Result<AtomMappingReport>::success(std::move(report));
  }
  if (request.policy == AtomMappingPolicy::explicit_map) {
    if (request.source_to_target_atom_ids.size() != count)
      return operation::Result<AtomMappingReport>::failure(
          invalid("explicit trajectory atom map must contain one stable atom ID "
                  "per trajectory atom"));
    std::set<std::uint64_t> used;
    for (std::size_t source = 0; source < count; ++source) {
      const auto id = request.source_to_target_atom_ids[source];
      if (id.value == 0U || !used.insert(id.value).second)
        return operation::Result<AtomMappingReport>::failure(
            invalid("explicit trajectory atom map contains a zero or duplicate "
                    "stable atom ID"));
      const auto target = request.topology->atom_index(id);
      if (!target.has_value())
        return operation::Result<AtomMappingReport>::failure(
            invalid("explicit trajectory atom map references an atom absent from "
                    "the current topology",
                    {}, {{"atom_id", std::to_string(id.value)},
                         {"topology_version",
                          std::to_string(request.topology->version())}}));
      report.target_to_source[target->value] = source;
    }
    report.identity_strength = "stable-id-explicit";
    report.compared_axes = {"atom_count", "stable_atom_id", "topology_version"};
    return operation::Result<AtomMappingReport>::success(std::move(report));
  }

  if (request.trajectory_identities.size() != count)
    return operation::Result<AtomMappingReport>::failure(
        Error{ErrorCode::unsupported,
              "trajectory format does not provide complete atom identity for "
              "exact mapping",
              "use --mapping index only after verifying source order, or provide "
              "--mapping explicit with stable atom IDs"});
  const auto axes = [](const TrajectoryAtomIdentity& value) {
    std::vector<std::string> result;
    if (value.source_serial.has_value()) result.emplace_back("source_serial");
    if (value.atom_name.has_value()) result.emplace_back("atom_name");
    if (value.residue_name.has_value()) result.emplace_back("residue_name");
    if (value.residue_sequence.has_value())
      result.emplace_back("residue_sequence");
    if (value.insertion_code.has_value()) result.emplace_back("insertion_code");
    if (value.chain_id.has_value()) result.emplace_back("chain_id");
    if (value.segment_id.has_value()) result.emplace_back("segment_id");
    if (value.atomic_number.has_value()) result.emplace_back("atomic_number");
    return result;
  };
  report.compared_axes = axes(request.trajectory_identities.front());
  if (report.compared_axes.empty())
    return operation::Result<AtomMappingReport>::failure(
        Error{ErrorCode::unsupported,
              "trajectory atom identity has no comparable fields",
              "use explicit stable-ID mapping or verified index order"});
  for (std::size_t index = 0; index < count; ++index) {
    const auto& actual = request.trajectory_identities[index];
    if (axes(actual) != report.compared_axes)
      return operation::Result<AtomMappingReport>::failure(
          Error{ErrorCode::unsupported,
                "trajectory atom identity axes drift at source ordinal " +
                    std::to_string(index),
                "every trajectory atom must expose the same exact identity "
                "axes"});
    const auto expected = topology_identity(*request.topology, index);
    std::optional<std::string> mismatch_axis;
    std::string expected_value;
    std::string actual_value;
    const auto compare_axis = [&](std::string_view name, const auto& observed,
                                  const auto& wanted) {
      if (!mismatch_axis.has_value() && observed.has_value() &&
          observed != wanted) {
        mismatch_axis = name;
        expected_value = identity_text(wanted);
        actual_value = identity_text(observed);
      }
    };
    compare_axis("source_serial", actual.source_serial,
                 expected.source_serial);
    compare_axis("atom_name", actual.atom_name, expected.atom_name);
    compare_axis("residue_name", actual.residue_name, expected.residue_name);
    compare_axis("residue_sequence", actual.residue_sequence,
                 expected.residue_sequence);
    compare_axis("insertion_code", actual.insertion_code,
                 expected.insertion_code);
    compare_axis("chain_id", actual.chain_id, expected.chain_id);
    compare_axis("segment_id", actual.segment_id, expected.segment_id);
    compare_axis("atomic_number", actual.atomic_number,
                 expected.atomic_number);
    if (mismatch_axis.has_value())
      return operation::Result<AtomMappingReport>::failure(
          invalid("trajectory exact atom identity mismatch at source ordinal " +
                      std::to_string(index),
                  "choose a matching topology or provide an explicit stable-ID "
                  "map",
                  {{"source_ordinal", std::to_string(index)},
                   {"identity_axis", *mismatch_axis},
                   {"expected", expected_value},
                   {"actual", actual_value},
                   {"topology_atom_id",
                    std::to_string(request.topology->atom_ids()[index].value)}}));
    report.target_to_source[index] = index;
  }
  report.identity_strength = report.compared_axes.size() == 8U
                                 ? "full-identity-exact"
                                 : "available-identity-exact";
  return operation::Result<AtomMappingReport>::success(std::move(report));
}

operation::Result<std::shared_ptr<const SemanticCoordinateSource>>
SemanticCoordinateSource::create(
    std::shared_ptr<const model::CoordinateSource> source) {
  if (source == nullptr)
    return operation::Result<
        std::shared_ptr<const SemanticCoordinateSource>>::failure(
        invalid("semantic trajectory source must not be null"));
  const auto count = source->frame_count();
  if (!count.has_value() || *count == 0U)
    return operation::Result<
        std::shared_ptr<const SemanticCoordinateSource>>::failure(
        invalid("semantic trajectory validation requires a known non-zero frame "
                "count"));
  const auto first = source->read_frame(0U);
  if (!first.has_value())
    return operation::Result<
        std::shared_ptr<const SemanticCoordinateSource>>::failure(first.error());
  const auto normalized = normalize(*first.value());
  if (!normalized.has_value())
    return operation::Result<
        std::shared_ptr<const SemanticCoordinateSource>>::failure(
        normalized.error());
  return operation::Result<
      std::shared_ptr<const SemanticCoordinateSource>>::success(
      std::shared_ptr<const SemanticCoordinateSource>(
          new SemanticCoordinateSource(std::move(source),
                                       normalized.value().first,
                                       normalized.value().second)));
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
SemanticCoordinateSource::read_frame(std::size_t frame_index) const {
  if (frame_index == 0U)
    return operation::Result<
        std::shared_ptr<const model::CoordinateFrame>>::success(first_frame_);
  const auto current_raw = source_->read_frame(frame_index);
  if (!current_raw.has_value())
    return operation::Result<
        std::shared_ptr<const model::CoordinateFrame>>::failure(
        current_raw.error());
  const auto current = normalize(*current_raw.value());
  if (!current.has_value())
    return operation::Result<
        std::shared_ptr<const model::CoordinateFrame>>::failure(current.error());
  if (const auto error = compare_channels(report_.channels,
                                          current.value().second.channels,
                                          frame_index);
      error.has_value())
    return operation::Result<
        std::shared_ptr<const model::CoordinateFrame>>::failure(*error);

  std::shared_ptr<const model::CoordinateFrame> previous_frame;
  {
    std::lock_guard lock{normalized_mutex_};
    if (const auto found = normalized_frames_.find(frame_index - 1U);
        found != normalized_frames_.end())
      previous_frame = found->second.lock();
  }
  if (previous_frame == nullptr) {
    const auto previous_raw = source_->read_frame(frame_index - 1U);
    if (!previous_raw.has_value())
      return operation::Result<
          std::shared_ptr<const model::CoordinateFrame>>::failure(
          previous_raw.error());
    const auto previous = normalize(*previous_raw.value());
    if (!previous.has_value())
      return operation::Result<
          std::shared_ptr<const model::CoordinateFrame>>::failure(
          previous.error());
    if (const auto error = compare_channels(report_.channels,
                                            previous.value().second.channels,
                                            frame_index - 1U);
        error.has_value())
      return operation::Result<
          std::shared_ptr<const model::CoordinateFrame>>::failure(*error);
    previous_frame = previous.value().first;
  }
  if (const auto error = compare_adjacent(*previous_frame,
                                          *current.value().first, frame_index);
      error.has_value())
    return operation::Result<
        std::shared_ptr<const model::CoordinateFrame>>::failure(*error);
  {
    std::lock_guard lock{normalized_mutex_};
    normalized_frames_.insert_or_assign(frame_index, current.value().first);
    normalized_frames_.insert_or_assign(frame_index - 1U, previous_frame);
    constexpr std::size_t normalized_window = 16U;
    while (normalized_frames_.size() > normalized_window)
      normalized_frames_.erase(normalized_frames_.begin());
  }
  return operation::Result<
      std::shared_ptr<const model::CoordinateFrame>>::success(
      current.value().first);
}

}  // namespace molshredder::trajectory
