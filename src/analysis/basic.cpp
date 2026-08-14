#include "molshredder/analysis/basic.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include "molshredder/operation/error.hpp"
#include "molshredder/selection/evaluator.hpp"
#include "molshredder/trajectory/pbc.hpp"

namespace molshredder::analysis {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

operation::Error invalid_selection(std::string message,
                                   std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_selection,
                          std::move(message), std::move(suggestion)};
}

class CompensatedSum {
 public:
  void add(double value) noexcept {
    const auto next = sum_ + value;
    if (std::abs(sum_) >= std::abs(value)) {
      correction_ += (sum_ - next) + value;
    } else {
      correction_ += (value - next) + sum_;
    }
    sum_ = next;
  }

  [[nodiscard]] double value() const noexcept { return sum_ + correction_; }

 private:
  double sum_{};
  double correction_{};
};

bool annotation_is_true(const model::PropertyMetadata& metadata,
                        std::string_view name) {
  const auto found = metadata.annotations.find(name);
  if (found == metadata.annotations.end()) return false;
  auto value = found->second;
  std::transform(value.begin(), value.end(), value.begin(), [](char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
  return value == "true" || value == "1" || value == "yes";
}

}  // namespace

operation::Result<MassValues> masses_from_property(
    const model::Topology& topology, std::string_view property_name) {
  if (property_name.empty()) {
    return operation::Result<MassValues>::failure(
        invalid("mass property name must not be empty"));
  }
  const auto* property = topology.properties().find(property_name);
  if (property == nullptr) {
    return operation::Result<MassValues>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "mass property '" + std::string{property_name} + "' does not exist",
        "load or assign a numeric per-atom mass property"});
  }
  MassValues result;
  const auto converted = std::visit(
      [&](const auto& values) {
        using Values = std::decay_t<decltype(values)>;
        if constexpr (std::is_same_v<Values, std::vector<float>> ||
                      std::is_same_v<Values, std::vector<double>>) {
          result.values.reserve(values.size());
          for (const auto value : values) {
            result.values.push_back(static_cast<double>(value));
          }
          return true;
        }
        return false;
      },
      property->values);
  if (!converted) {
    return operation::Result<MassValues>::failure(
        invalid("mass property must use float32 or float64 values"));
  }
  if (!std::all_of(result.values.begin(), result.values.end(),
                   [](double mass) {
                     return std::isfinite(mass) && mass >= 0.0;
                   })) {
    return operation::Result<MassValues>::failure(
        invalid("mass property contains a non-finite or negative value"));
  }
  result.source = property->metadata.source.empty()
                      ? "topology-property:" + std::string{property_name}
                      : property->metadata.source;
  result.estimated = annotation_is_true(property->metadata, "estimated");
  result.unit = property->metadata.unit;
  return operation::Result<MassValues>::success(std::move(result));
}

operation::Result<MassValues> estimated_element_masses(
    const model::Topology& topology) {
  MassValues result;
  result.values.reserve(topology.atom_count());
  result.unit = "dalton";
  result.source = "CIAAW abridged standard atomic weights 2024";
  result.estimated = true;
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    const auto atomic_number = topology.atoms()[index].atomic_number;
    double mass{};
    switch (atomic_number) {
      case 1U: mass = 1.008; break;
      case 6U: mass = 12.011; break;
      case 7U: mass = 14.007; break;
      case 8U: mass = 15.999; break;
      case 9U: mass = 18.998; break;
      case 11U: mass = 22.990; break;
      case 12U: mass = 24.305; break;
      case 15U: mass = 30.974; break;
      case 16U: mass = 32.06; break;
      case 17U: mass = 35.45; break;
      case 19U: mass = 39.098; break;
      case 20U: mass = 40.078; break;
      case 26U: mass = 55.845; break;
      case 30U: mass = 65.38; break;
      case 35U: mass = 79.904; break;
      case 53U: mass = 126.90; break;
      default:
        return operation::Result<MassValues>::failure(operation::Error{
            operation::ErrorCode::not_found,
            "no estimated mass for atom index " + std::to_string(index + 1U) +
                " with atomic number " + std::to_string(atomic_number),
            "assign an explicit float mass property with source metadata"});
    }
    result.values.push_back(mass);
  }
  return operation::Result<MassValues>::success(std::move(result));
}

operation::Result<CenterResult> calculate_center(
    const CenterRequest& request) {
  if (request.frame == nullptr) {
    return operation::Result<CenterResult>::failure(
        invalid("center calculation requires a coordinate frame"));
  }
  const auto atom_count = request.frame->atom_count();
  if (!request.selected.empty() &&
      !selection::mask_is_valid(request.selected, atom_count)) {
    return operation::Result<CenterResult>::failure(
        invalid_selection("center selection mask is invalid"));
  }
  if (request.mode == CenterMode::center_of_mass) {
    if (request.masses.size() != atom_count || request.mass_source.empty()) {
      return operation::Result<CenterResult>::failure(invalid(
          "center of mass requires one mass per atom and mass provenance"));
    }
    if (!std::all_of(request.masses.begin(), request.masses.end(),
                     [](double mass) {
                       return std::isfinite(mass) && mass >= 0.0;
                     })) {
      return operation::Result<CenterResult>::failure(
          invalid("center-of-mass weights must be finite and non-negative"));
    }
  } else if (!request.masses.empty() || !request.mass_unit.empty() ||
             !request.mass_source.empty() || request.masses_estimated) {
    return operation::Result<CenterResult>::failure(invalid(
        "centroid does not accept mass values or mass provenance"));
  }

  CompensatedSum x;
  CompensatedSum y;
  CompensatedSum z;
  CompensatedSum denominator;
  std::size_t selected_count{};
  std::size_t used_count{};
  std::size_t skipped_count{};
  std::optional<operation::Error> loop_error;
  std::visit(
      [&](const auto& positions) {
        for (std::size_t index = 0; index < atom_count; ++index) {
          if (!request.selected.empty() && request.selected[index] == 0U) {
            continue;
          }
          ++selected_count;
          if (!request.frame->atom_present(index)) {
            if (request.missing_atom_policy == MissingAtomPolicy::error) {
              loop_error = invalid_selection(
                  "center selection contains a missing atom at index " +
                      std::to_string(index + 1U),
                  "remove missing atoms or explicitly use the skip policy");
              return;
            }
            ++skipped_count;
            continue;
          }
          const auto weight = request.mode == CenterMode::center_of_mass
                                  ? request.masses[index]
                                  : 1.0;
          x.add(static_cast<double>(positions[index].x) * weight);
          y.add(static_cast<double>(positions[index].y) * weight);
          z.add(static_cast<double>(positions[index].z) * weight);
          denominator.add(weight);
          ++used_count;
        }
      },
      request.frame->positions().values());
  if (loop_error.has_value()) {
    return operation::Result<CenterResult>::failure(
        std::move(loop_error.value()));
  }
  if (used_count == 0U) {
    return operation::Result<CenterResult>::failure(
        invalid_selection("center selection contains no usable atoms"));
  }
  const auto divisor = denominator.value();
  if (!std::isfinite(divisor) || divisor <= 0.0) {
    return operation::Result<CenterResult>::failure(
        invalid("center-of-mass selected total mass must be positive"));
  }

  CenterResult result;
  result.position = {x.value() / divisor, y.value() / divisor,
                     z.value() / divisor};
  result.coordinate_unit = request.frame->metadata().coordinate_unit;
  result.selected_atom_count = selected_count;
  result.used_atom_count = used_count;
  result.skipped_missing_atom_count = skipped_count;
  if (request.mode == CenterMode::center_of_mass) {
    result.total_mass = divisor;
    if (!request.mass_unit.empty()) {
      result.mass_unit = std::string{request.mass_unit};
    }
    result.mass_source = std::string{request.mass_source};
    result.masses_estimated = request.masses_estimated;
  }
  return operation::Result<CenterResult>::success(std::move(result));
}

operation::Result<AtomDistanceResult> atom_distance(
    const model::CoordinateFrame& frame, model::AtomIndex first,
    model::AtomIndex second, DistanceBoundary boundary) {
  if (first.value >= frame.atom_count() || second.value >= frame.atom_count()) {
    return operation::Result<AtomDistanceResult>::failure(
        invalid("distance atom index is out of range"));
  }
  if (!frame.atom_present(first.value) || !frame.atom_present(second.value)) {
    return operation::Result<AtomDistanceResult>::failure(invalid_selection(
        "distance endpoint is missing in this coordinate frame"));
  }
  auto displacement = std::visit(
      [&](const auto& positions) {
        return model::Vec3d{
            static_cast<double>(positions[second.value].x) -
                static_cast<double>(positions[first.value].x),
            static_cast<double>(positions[second.value].y) -
                static_cast<double>(positions[first.value].y),
            static_cast<double>(positions[second.value].z) -
                static_cast<double>(positions[first.value].z)};
      },
      frame.positions().values());
  if (boundary == DistanceBoundary::minimum_image) {
    if (!frame.metadata().unit_cell.has_value()) {
      return operation::Result<AtomDistanceResult>::failure(
          invalid("minimum-image distance requires a periodic unit cell"));
    }
    const auto minimum = trajectory::minimum_image(
        *frame.metadata().unit_cell, displacement);
    if (!minimum.has_value()) {
      return operation::Result<AtomDistanceResult>::failure(minimum.error());
    }
    displacement = minimum.value().displacement;
  }
  const auto distance =
      std::hypot(displacement.x, displacement.y, displacement.z);
  return operation::Result<AtomDistanceResult>::success(
      AtomDistanceResult{first, second, displacement, distance,
                         frame.metadata().coordinate_unit});
}

}  // namespace molshredder::analysis
