#include "molshredder/analysis/basic.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
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

model::Vec3d subtract(model::Vec3d left, model::Vec3d right) noexcept {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

model::Vec3d scale(model::Vec3d value, double factor) noexcept {
  return {value.x * factor, value.y * factor, value.z * factor};
}

double dot(model::Vec3d left, model::Vec3d right) noexcept {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

model::Vec3d cross(model::Vec3d left, model::Vec3d right) noexcept {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

double norm(model::Vec3d value) noexcept {
  return std::hypot(value.x, value.y, value.z);
}

operation::Result<model::Vec3d> atom_position(
    const model::CoordinateFrame& frame, model::AtomIndex atom) {
  if (atom.value >= frame.atom_count()) {
    return operation::Result<model::Vec3d>::failure(
        invalid("geometry atom index is out of range"));
  }
  if (!frame.atom_present(atom.value)) {
    return operation::Result<model::Vec3d>::failure(invalid_selection(
        "geometry atom is missing in this coordinate frame"));
  }
  return operation::Result<model::Vec3d>::success(std::visit(
      [&](const auto& positions) {
        const auto& position = positions[atom.value];
        return model::Vec3d{static_cast<double>(position.x),
                            static_cast<double>(position.y),
                            static_cast<double>(position.z)};
      },
      frame.positions().values()));
}

operation::Result<model::Vec3d> atom_displacement(
    const model::CoordinateFrame& frame, model::AtomIndex from,
    model::AtomIndex to, DistanceBoundary boundary) {
  const auto from_position = atom_position(frame, from);
  if (!from_position.has_value())
    return operation::Result<model::Vec3d>::failure(from_position.error());
  const auto to_position = atom_position(frame, to);
  if (!to_position.has_value())
    return operation::Result<model::Vec3d>::failure(to_position.error());
  auto displacement = subtract(to_position.value(), from_position.value());
  if (boundary == DistanceBoundary::minimum_image) {
    if (!frame.metadata().unit_cell.has_value()) {
      return operation::Result<model::Vec3d>::failure(
          invalid("minimum-image geometry requires a periodic unit cell"));
    }
    const auto minimum = trajectory::minimum_image(
        *frame.metadata().unit_cell, displacement);
    if (!minimum.has_value())
      return operation::Result<model::Vec3d>::failure(minimum.error());
    displacement = minimum.value().displacement;
  }
  return operation::Result<model::Vec3d>::success(displacement);
}

constexpr auto kGeometryRelativeTolerance =
    128.0 * std::numeric_limits<double>::epsilon();

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
        } else {
          return false;
        }
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

operation::Result<AtomAngleResult> atom_angle(
    const model::CoordinateFrame& frame, model::AtomIndex first,
    model::AtomIndex vertex, model::AtomIndex third,
    DistanceBoundary boundary) {
  if (first == vertex || first == third || vertex == third) {
    return operation::Result<AtomAngleResult>::failure(
        invalid("angle requires three distinct atoms"));
  }
  const auto first_vector = atom_displacement(frame, vertex, first, boundary);
  if (!first_vector.has_value())
    return operation::Result<AtomAngleResult>::failure(first_vector.error());
  const auto third_vector = atom_displacement(frame, vertex, third, boundary);
  if (!third_vector.has_value())
    return operation::Result<AtomAngleResult>::failure(third_vector.error());
  const auto first_norm = norm(first_vector.value());
  const auto third_norm = norm(third_vector.value());
  if (!(first_norm > 0.0) || !(third_norm > 0.0)) {
    return operation::Result<AtomAngleResult>::failure(invalid(
        "angle is undefined because a vertex bond has zero length"));
  }
  const auto sine = norm(cross(first_vector.value(), third_vector.value()));
  const auto cosine = dot(first_vector.value(), third_vector.value());
  const auto scale_value = first_norm * third_norm;
  if (!std::isfinite(scale_value) ||
      std::hypot(sine, cosine) <= kGeometryRelativeTolerance * scale_value) {
    return operation::Result<AtomAngleResult>::failure(
        invalid("angle geometry is numerically degenerate"));
  }
  const auto degrees =
      std::atan2(sine, cosine) * 180.0 / std::numbers::pi;
  const auto vertex_position = atom_position(frame, vertex).value();
  return operation::Result<AtomAngleResult>::success(
      {first, vertex, third, {vertex_position.x + first_vector.value().x,
                              vertex_position.y + first_vector.value().y,
                              vertex_position.z + first_vector.value().z},
       vertex_position,
       {vertex_position.x + third_vector.value().x,
        vertex_position.y + third_vector.value().y,
        vertex_position.z + third_vector.value().z},
       degrees, frame.metadata().coordinate_unit, boundary});
}

operation::Result<AtomDihedralResult> atom_dihedral(
    const model::CoordinateFrame& frame, model::AtomIndex first,
    model::AtomIndex second, model::AtomIndex third, model::AtomIndex fourth,
    DistanceBoundary boundary) {
  if (first == second || first == third || first == fourth ||
      second == third || second == fourth || third == fourth) {
    return operation::Result<AtomDihedralResult>::failure(
        invalid("dihedral requires four distinct atoms"));
  }
  const auto b0 = atom_displacement(frame, second, first, boundary);
  if (!b0.has_value())
    return operation::Result<AtomDihedralResult>::failure(b0.error());
  const auto b1 = atom_displacement(frame, second, third, boundary);
  if (!b1.has_value())
    return operation::Result<AtomDihedralResult>::failure(b1.error());
  const auto b2 = atom_displacement(frame, third, fourth, boundary);
  if (!b2.has_value())
    return operation::Result<AtomDihedralResult>::failure(b2.error());
  const auto b1_norm = norm(b1.value());
  if (!(b1_norm > 0.0)) {
    return operation::Result<AtomDihedralResult>::failure(
        invalid("dihedral is undefined because the central bond has zero length"));
  }
  const auto axis = scale(b1.value(), 1.0 / b1_norm);
  const auto v = subtract(b0.value(), scale(axis, dot(b0.value(), axis)));
  const auto w = subtract(b2.value(), scale(axis, dot(b2.value(), axis)));
  const auto v_norm = norm(v);
  const auto w_norm = norm(w);
  const auto outer_scale = norm(b0.value()) * norm(b2.value());
  if (!(v_norm > kGeometryRelativeTolerance * norm(b0.value())) ||
      !(w_norm > kGeometryRelativeTolerance * norm(b2.value())) ||
      !std::isfinite(outer_scale)) {
    return operation::Result<AtomDihedralResult>::failure(invalid(
        "dihedral is undefined because three consecutive atoms are collinear"));
  }
  const auto x = dot(v, w);
  const auto y = dot(cross(axis, v), w);
  const auto degrees =
      std::atan2(y, x) * 180.0 / std::numbers::pi;
  const auto second_position = atom_position(frame, second).value();
  const auto third_position = model::Vec3d{
      second_position.x + b1.value().x, second_position.y + b1.value().y,
      second_position.z + b1.value().z};
  return operation::Result<AtomDihedralResult>::success(
      {first,
       second,
       third,
       fourth,
       {second_position.x + b0.value().x, second_position.y + b0.value().y,
        second_position.z + b0.value().z},
       second_position,
       third_position,
       {third_position.x + b2.value().x, third_position.y + b2.value().y,
        third_position.z + b2.value().z},
       degrees,
       frame.metadata().coordinate_unit,
       boundary});
}

}  // namespace molshredder::analysis
