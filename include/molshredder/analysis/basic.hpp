#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::analysis {

enum class CenterMode { centroid, center_of_mass };
enum class MissingAtomPolicy { error, skip };
enum class DistanceBoundary { raw, minimum_image };

struct MassValues {
  std::vector<double> values;
  std::optional<std::string> unit;
  std::string source;
  bool estimated{};
};

struct CenterRequest {
  const model::CoordinateFrame* frame{};
  std::span<const std::uint8_t> selected;
  CenterMode mode{CenterMode::centroid};
  std::span<const double> masses;
  std::string_view mass_unit;
  std::string_view mass_source;
  bool masses_estimated{};
  MissingAtomPolicy missing_atom_policy{MissingAtomPolicy::error};
};

struct CenterResult {
  model::Vec3d position;
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::size_t selected_atom_count{};
  std::size_t used_atom_count{};
  std::size_t skipped_missing_atom_count{};
  std::optional<double> total_mass;
  std::optional<std::string> mass_unit;
  std::string mass_source;
  bool masses_estimated{};
};

struct AtomDistanceResult {
  model::AtomIndex first;
  model::AtomIndex second;
  model::Vec3d displacement;
  double distance{};
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
};

struct AtomAngleResult {
  model::AtomIndex first;
  model::AtomIndex vertex;
  model::AtomIndex third;
  model::Vec3d first_position;
  model::Vec3d vertex_position;
  model::Vec3d third_position;
  double angle_degrees{};
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  DistanceBoundary boundary{DistanceBoundary::raw};
};

struct AtomDihedralResult {
  model::AtomIndex first;
  model::AtomIndex second;
  model::AtomIndex third;
  model::AtomIndex fourth;
  model::Vec3d first_position;
  model::Vec3d second_position;
  model::Vec3d third_position;
  model::Vec3d fourth_position;
  double angle_degrees{};
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  DistanceBoundary boundary{DistanceBoundary::raw};
};

[[nodiscard]] operation::Result<MassValues> masses_from_property(
    const model::Topology& topology, std::string_view property_name = "mass");

[[nodiscard]] operation::Result<MassValues> estimated_element_masses(
    const model::Topology& topology);

[[nodiscard]] operation::Result<CenterResult> calculate_center(
    const CenterRequest& request);

[[nodiscard]] operation::Result<AtomDistanceResult> atom_distance(
    const model::CoordinateFrame& frame, model::AtomIndex first,
    model::AtomIndex second,
    DistanceBoundary boundary = DistanceBoundary::raw);

[[nodiscard]] operation::Result<AtomAngleResult> atom_angle(
    const model::CoordinateFrame& frame, model::AtomIndex first,
    model::AtomIndex vertex, model::AtomIndex third,
    DistanceBoundary boundary = DistanceBoundary::raw);

[[nodiscard]] operation::Result<AtomDihedralResult> atom_dihedral(
    const model::CoordinateFrame& frame, model::AtomIndex first,
    model::AtomIndex second, model::AtomIndex third, model::AtomIndex fourth,
    DistanceBoundary boundary = DistanceBoundary::raw);

}  // namespace molshredder::analysis
