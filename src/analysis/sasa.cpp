#include "molshredder/analysis/sasa.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::analysis {
namespace {

operation::Error invalid(std::string message) {
  return {operation::ErrorCode::invalid_argument, std::move(message), {}};
}

operation::Error exhausted(std::size_t budget) {
  return {operation::ErrorCode::resource_exhausted,
          "SASA point/occluder evaluation budget was exhausted",
          "increase --evaluation-budget above " + std::to_string(budget) +
              " or reduce --samples"};
}

operation::Error cancelled() {
  return {operation::ErrorCode::cancelled, "SASA calculation was cancelled",
          "retry the calculation when the molecular object is ready"};
}

std::vector<model::Vec3d>
positions_as_double(const model::CoordinateFrame &frame) {
  return std::visit(
      [](const auto &values) {
        std::vector<model::Vec3d> result;
        result.reserve(values.size());
        for (const auto value : values)
          result.push_back({static_cast<double>(value.x),
                            static_cast<double>(value.y),
                            static_cast<double>(value.z)});
        return result;
      },
      frame.positions().values());
}

double squared_distance(const model::Vec3d &first,
                        const model::Vec3d &second) noexcept {
  const auto x = first.x - second.x;
  const auto y = first.y - second.y;
  const auto z = first.z - second.z;
  return x * x + y * y + z * z;
}

} // namespace

operation::Result<SasaResult>
solvent_accessible_surface_area(const SasaRequest &request) {
  if (request.frame == nullptr)
    return operation::Result<SasaResult>::failure(
        invalid("SASA requires a coordinate frame"));
  const auto atom_count = request.frame->atom_count();
  if (request.vdw_radii_angstrom.size() != atom_count ||
      (!request.selected.empty() && request.selected.size() != atom_count))
    return operation::Result<SasaResult>::failure(
        invalid("SASA coordinate, radius and selection sizes must match"));
  if (!std::isfinite(request.probe_radius_angstrom) ||
      request.probe_radius_angstrom < 0.0 ||
      request.samples_per_atom < 4U || request.evaluation_budget == 0U)
    return operation::Result<SasaResult>::failure(
        invalid("SASA requires a non-negative finite probe, at least four samples and a positive evaluation budget"));
  if (request.context != nullptr &&
      request.context->cancellation.is_cancelled())
    return operation::Result<SasaResult>::failure(cancelled());

  const auto positions = positions_as_double(*request.frame);
  const auto native_per_angstrom =
      request.frame->metadata().coordinate_unit ==
              operation::LengthUnit::nanometer
          ? 0.1
          : 1.0;
  std::vector<double> expanded_native(atom_count);
  for (std::size_t atom = 0; atom < atom_count; ++atom) {
    const auto radius = request.vdw_radii_angstrom[atom];
    if (!std::isfinite(radius) || radius <= 0.0)
      return operation::Result<SasaResult>::failure(
          invalid("SASA van der Waals radii must be finite and positive"));
    expanded_native[atom] =
        (radius + request.probe_radius_angstrom) * native_per_angstrom;
  }

  SasaResult result;
  result.probe_radius_angstrom = request.probe_radius_angstrom;
  result.samples_per_atom = request.samples_per_atom;
  result.coordinate_unit = request.frame->metadata().coordinate_unit;
  for (std::size_t atom = 0; atom < atom_count; ++atom) {
    if (!request.frame->atom_present(atom) &&
        (request.selected.empty() || request.selected[atom] != 0U))
      return operation::Result<SasaResult>::failure(
          invalid("SASA selected atoms must have coordinates"));
    if (!request.frame->atom_present(atom) &&
        !request.selected.empty() && request.selected[atom] == 0U)
      ++result.ignored_missing_occluders;
  }

  const auto golden_angle =
      std::numbers::pi_v<double> * (3.0 - std::sqrt(5.0));
  const auto spend = [&](std::size_t amount) {
    if (amount > request.evaluation_budget -
                     std::min(result.point_occluder_evaluations,
                              request.evaluation_budget))
      return false;
    result.point_occluder_evaluations += amount;
    return true;
  };
  for (std::size_t atom = 0; atom < atom_count; ++atom) {
    if ((!request.selected.empty() && request.selected[atom] == 0U) ||
        !request.frame->atom_present(atom))
      continue;
    if (request.context != nullptr &&
        request.context->cancellation.is_cancelled())
      return operation::Result<SasaResult>::failure(cancelled());
    std::vector<std::size_t> occluders;
    for (std::size_t other = 0; other < atom_count; ++other) {
      if (other == atom || !request.frame->atom_present(other))
        continue;
      if (!spend(1U))
        return operation::Result<SasaResult>::failure(
            exhausted(request.evaluation_budget));
      const auto cutoff = expanded_native[atom] + expanded_native[other];
      if (squared_distance(positions[atom], positions[other]) <
          cutoff * cutoff)
        occluders.push_back(other);
    }
    std::size_t accessible{};
    for (std::size_t sample = 0; sample < request.samples_per_atom; ++sample) {
      if ((sample & 255U) == 0U && request.context != nullptr &&
          request.context->cancellation.is_cancelled())
        return operation::Result<SasaResult>::failure(cancelled());
      const auto z = 1.0 -
                     2.0 * (static_cast<double>(sample) + 0.5) /
                         static_cast<double>(request.samples_per_atom);
      const auto radial = std::sqrt(std::max(0.0, 1.0 - z * z));
      const auto phi = golden_angle * static_cast<double>(sample);
      const model::Vec3d point{
          positions[atom].x + expanded_native[atom] * radial * std::cos(phi),
          positions[atom].y + expanded_native[atom] * radial * std::sin(phi),
          positions[atom].z + expanded_native[atom] * z};
      auto occluded = false;
      for (const auto other : occluders) {
        if (!spend(1U))
          return operation::Result<SasaResult>::failure(
              exhausted(request.evaluation_budget));
        const auto other_radius = expanded_native[other];
        if (squared_distance(point, positions[other]) <
            other_radius * other_radius) {
          occluded = true;
          break;
        }
      }
      if (!occluded)
        ++accessible;
    }
    const auto expanded_angstrom =
        request.vdw_radii_angstrom[atom] + request.probe_radius_angstrom;
    const auto area = 4.0 * std::numbers::pi_v<double> * expanded_angstrom *
                      expanded_angstrom *
                      static_cast<double>(accessible) /
                      static_cast<double>(request.samples_per_atom);
    result.sampling_area_quantum_square_angstrom = std::max(
        result.sampling_area_quantum_square_angstrom,
        4.0 * std::numbers::pi_v<double> * expanded_angstrom *
            expanded_angstrom /
            static_cast<double>(request.samples_per_atom));
    result.atoms.push_back({model::AtomIndex{atom}, expanded_angstrom,
                            accessible, area});
    result.total_area_square_angstrom += area;
    if (request.context != nullptr && request.context->report_progress)
      request.context->report_progress(
          {static_cast<double>(atom + 1U) / static_cast<double>(atom_count),
           "sasa-sampling"});
  }
  if (result.atoms.empty())
    return operation::Result<SasaResult>::failure(
        invalid("SASA selection contains no present atoms"));
  return operation::Result<SasaResult>::success(std::move(result));
}

} // namespace molshredder::analysis
