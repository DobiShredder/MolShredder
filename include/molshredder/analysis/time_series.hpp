#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/analysis/basic.hpp"
#include "molshredder/analysis/alignment.hpp"
#include "molshredder/analysis/contacts.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::analysis {

struct SeriesRange {
  std::size_t first{};
  std::size_t last{};
  std::size_t stride{1U};
};

struct SeriesFrameMetadata {
  std::size_t frame_index{};
  std::optional<std::uint64_t> source_step;
  std::optional<double> physical_time;
  std::optional<model::TimeUnit> physical_time_unit;
};

struct CenterSeriesRow {
  SeriesFrameMetadata frame;
  CenterResult center;
};

struct DistanceSeriesRow {
  SeriesFrameMetadata frame;
  AtomDistanceResult distance;
};

enum class FitMode { none, rigid };

struct RmsdSeriesRow {
  SeriesFrameMetadata frame;
  RmsdResult rmsd;
  double rmsd_before_fit{};
  std::size_t fit_paired_atom_count{};
};

struct RmsfAtomRow {
  model::AtomIndex atom;
  std::size_t observation_count{};
  std::optional<double> rmsf;
};

struct RmsfSeriesResult {
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::size_t frame_count{};
  std::size_t selected_atom_count{};
  std::vector<RmsfAtomRow> atoms;
};

struct InteractionCountRow {
  SeriesFrameMetadata frame;
  std::size_t interaction_count{};
};

struct ContactOccupancyRow {
  model::AtomIndex first;
  model::AtomIndex second;
  std::size_t observation_count{};
  double occupancy{};
  double mean_distance{};
};

struct ContactSeriesResult {
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::size_t frame_count{};
  std::vector<InteractionCountRow> frames;
  std::vector<ContactOccupancyRow> occupancy;
};

struct HydrogenBondOccupancyRow {
  model::AtomIndex donor;
  model::AtomIndex acceptor;
  model::AtomIndex hydrogen;
  std::size_t observation_count{};
  double occupancy{};
  double mean_donor_acceptor_distance{};
  double mean_angle_deviation_degrees{};
};

struct HydrogenBondSeriesResult {
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::size_t frame_count{};
  std::vector<InteractionCountRow> frames;
  std::vector<HydrogenBondOccupancyRow> occupancy;
};

struct CenterSeriesRequest {
  std::shared_ptr<const model::CoordinateSource> source;
  SeriesRange range;
  std::span<const std::uint8_t> selected;
  CenterMode mode{CenterMode::centroid};
  std::span<const double> masses;
  std::string_view mass_unit;
  std::string_view mass_source;
  bool masses_estimated{};
  MissingAtomPolicy missing_atom_policy{MissingAtomPolicy::error};
};

struct DistanceSeriesRequest {
  std::shared_ptr<const model::CoordinateSource> source;
  SeriesRange range;
  model::AtomIndex first;
  model::AtomIndex second;
  DistanceBoundary boundary{DistanceBoundary::raw};
};

struct RmsdSeriesRequest {
  std::shared_ptr<const model::CoordinateSource> source;
  std::size_t reference_frame{};
  SeriesRange range;
  std::span<const std::uint8_t> selected;
  std::span<const std::uint8_t> fit_selected;
  std::span<const double> weights;
  FitMode fit{FitMode::rigid};
  MissingAtomPolicy missing_atom_policy{MissingAtomPolicy::error};
};

struct RmsfSeriesRequest {
  std::shared_ptr<const model::CoordinateSource> source;
  std::size_t reference_frame{};
  SeriesRange range;
  std::span<const std::uint8_t> selected;
  std::span<const std::uint8_t> fit_selected;
  std::span<const double> weights;
  FitMode fit{FitMode::rigid};
  MissingAtomPolicy missing_atom_policy{MissingAtomPolicy::error};
};

struct ContactSeriesRequest {
  std::shared_ptr<const model::CoordinateSource> source;
  const model::Topology* topology{};
  SeriesRange range;
  std::span<const std::uint8_t> first;
  std::span<const std::uint8_t> second;
  double cutoff{};
  operation::LengthUnit cutoff_unit{operation::LengthUnit::angstrom};
  DistanceBoundary boundary{DistanceBoundary::raw};
  bool same_selection{};
  bool exclude_bonded{true};
  bool collect_occupancy{true};
};

struct HydrogenBondSeriesRequest {
  std::shared_ptr<const model::CoordinateSource> source;
  const model::Topology* topology{};
  SeriesRange range;
  std::span<const std::uint8_t> donor_selection;
  std::span<const std::uint8_t> acceptor_selection;
  std::span<const std::uint8_t> donor_capable;
  std::span<const std::uint8_t> acceptor_capable;
  double distance_cutoff{};
  operation::LengthUnit cutoff_unit{operation::LengthUnit::angstrom};
  double maximum_angle_deviation_degrees{};
  DistanceBoundary boundary{DistanceBoundary::raw};
  bool same_selection{};
  bool collect_occupancy{true};
};

[[nodiscard]] operation::Result<std::vector<CenterSeriesRow>> center_series(
    const CenterSeriesRequest& request, operation::TaskContext& context);

[[nodiscard]] operation::Result<std::vector<DistanceSeriesRow>>
distance_series(const DistanceSeriesRequest& request,
                operation::TaskContext& context);

[[nodiscard]] operation::Result<std::vector<RmsdSeriesRow>> rmsd_series(
    const RmsdSeriesRequest& request, operation::TaskContext& context);

[[nodiscard]] operation::Result<RmsfSeriesResult> rmsf_series(
    const RmsfSeriesRequest& request, operation::TaskContext& context);

[[nodiscard]] operation::Result<ContactSeriesResult> contact_series(
    const ContactSeriesRequest& request, operation::TaskContext& context);

[[nodiscard]] operation::Result<HydrogenBondSeriesResult>
hydrogen_bond_series(const HydrogenBondSeriesRequest& request,
                     operation::TaskContext& context);

}  // namespace molshredder::analysis
