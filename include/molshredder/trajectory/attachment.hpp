#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::trajectory {

enum class AtomMappingPolicy { exact, index_order, explicit_map };

[[nodiscard]] std::string_view to_string(AtomMappingPolicy policy) noexcept;

// Identity fields describe the source trajectory order before any mapping.
// An exact mapping requires every field below, preventing atom-count-only
// attachment from being described as identity validation.
struct TrajectoryAtomIdentity {
  std::optional<std::int64_t> source_serial;
  std::optional<std::string> atom_name;
  std::optional<std::string> residue_name;
  std::optional<std::int64_t> residue_sequence;
  std::optional<std::string> insertion_code;
  std::optional<std::string> chain_id;
  std::optional<std::string> segment_id;
  std::optional<std::uint8_t> atomic_number;

  friend bool operator==(const TrajectoryAtomIdentity&,
                         const TrajectoryAtomIdentity&) = default;
};

struct AtomMappingRequest {
  AtomMappingPolicy policy{AtomMappingPolicy::index_order};
  const model::Topology* topology{};
  std::size_t trajectory_atom_count{};
  std::span<const TrajectoryAtomIdentity> trajectory_identities;
  // For explicit_map, entry i is the target stable AtomId for trajectory
  // source ordinal i. Every current topology atom must occur exactly once.
  std::span<const model::AtomId> source_to_target_atom_ids;
};

struct AtomMappingReport {
  AtomMappingPolicy policy{AtomMappingPolicy::index_order};
  std::string identity_strength;
  std::vector<std::optional<std::size_t>> target_to_source;
  std::vector<std::string> compared_axes;
  std::uint64_t topology_version{};
};

[[nodiscard]] operation::Result<AtomMappingReport>
resolve_atom_mapping(const AtomMappingRequest& request);

struct TrajectoryChannelContract {
  bool has_source_step{};
  bool has_physical_time{};
  bool has_unit_cell{};
  bool has_velocities{};
  bool has_forces{};
  operation::LengthUnit source_coordinate_unit{
      operation::LengthUnit::angstrom};
  model::TimeUnit physical_time_unit{model::TimeUnit::picosecond};
  model::TimeUnit velocity_time_unit{model::TimeUnit::picosecond};
  std::string source_force_unit;
  std::string force_unit;

  friend bool operator==(const TrajectoryChannelContract&,
                         const TrajectoryChannelContract&) = default;
};

struct TrajectorySemanticReport {
  std::string schema{"trajectory-semantic-v1"};
  std::string canonical_coordinate_unit{"angstrom"};
  std::string canonical_time_unit{"picosecond"};
  std::string missing_data_policy{"preserve-explicit-presence-mask"};
  TrajectoryChannelContract channels;
  bool coordinate_conversion_applied{};
  bool time_conversion_applied{};
  bool force_conversion_applied{};
  std::string validation_scope{"per-frame-and-adjacent-metadata"};
};

// Decorates a source with canonical Å/ps normalization and deterministic
// channel-shape/unit/adjacent step-time validation. Creation validates frame
// zero; later failures are surfaced from read_frame without partial state.
class SemanticCoordinateSource final : public model::CoordinateSource {
 public:
  [[nodiscard]] static operation::Result<
      std::shared_ptr<const SemanticCoordinateSource>>
  create(std::shared_ptr<const model::CoordinateSource> source);

  [[nodiscard]] std::size_t atom_count() const noexcept override {
    return source_->atom_count();
  }
  [[nodiscard]] std::optional<std::size_t> frame_count() const noexcept override {
    return source_->frame_count();
  }
  [[nodiscard]] model::FrameAccess access() const noexcept override {
    return source_->access();
  }
  [[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override;

  [[nodiscard]] const TrajectorySemanticReport& report() const noexcept {
    return report_;
  }

 private:
  SemanticCoordinateSource(
      std::shared_ptr<const model::CoordinateSource> source,
      std::shared_ptr<const model::CoordinateFrame> first_frame,
      TrajectorySemanticReport report)
      : source_{std::move(source)},
        first_frame_{std::move(first_frame)},
        report_{std::move(report)} {
    normalized_frames_.emplace(0U, first_frame_);
  }

  std::shared_ptr<const model::CoordinateSource> source_;
  std::shared_ptr<const model::CoordinateFrame> first_frame_;
  TrajectorySemanticReport report_;
  mutable std::mutex normalized_mutex_;
  mutable std::unordered_map<
      std::size_t, std::weak_ptr<const model::CoordinateFrame>>
      normalized_frames_;
};

}  // namespace molshredder::trajectory
