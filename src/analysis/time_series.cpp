#include "molshredder/analysis/time_series.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <utility>

#include "molshredder/operation/error.hpp"
#include "molshredder/selection/evaluator.hpp"

namespace molshredder::analysis {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

std::optional<operation::Error> validate_range(
    const std::shared_ptr<const model::CoordinateSource>& source,
    SeriesRange range) {
  if (source == nullptr) {
    return invalid("time-series analysis requires a coordinate source");
  }
  if (range.stride == 0U) {
    return invalid("time-series stride must be positive");
  }
  if (range.first > range.last) {
    return invalid("time-series first frame must not exceed last frame");
  }
  const auto count = source->frame_count();
  if (!count.has_value()) {
    return invalid("time-series analysis requires a known frame count");
  }
  if (*count == 0U || range.last >= *count) {
    return invalid("time-series frame range exceeds the coordinate source",
                   "choose a last frame smaller than the frame count");
  }
  return std::nullopt;
}

std::size_t row_count(SeriesRange range) {
  return ((range.last - range.first) / range.stride) + 1U;
}

SeriesFrameMetadata metadata(std::size_t frame_index,
                             const model::CoordinateFrame& frame) {
  SeriesFrameMetadata result;
  result.frame_index = frame_index;
  result.source_step = frame.metadata().source_step;
  if (frame.metadata().physical_time.has_value()) {
    result.physical_time = frame.metadata().physical_time->value;
    result.physical_time_unit = frame.metadata().physical_time->unit;
  }
  return result;
}

operation::Error frame_failure(std::size_t frame_index,
                               operation::Error failure) {
  failure.message = "time-series frame " + std::to_string(frame_index) +
                    " failed: " + failure.message;
  return failure;
}

bool next_frame(std::size_t& frame_index, SeriesRange range) {
  if (range.stride > range.last - frame_index) {
    return false;
  }
  frame_index += range.stride;
  return frame_index <= range.last;
}

void progress(operation::TaskContext& context, std::size_t completed,
              std::size_t total, std::string_view stage) {
  if (context.report_progress) {
    context.report_progress(operation::ProgressUpdate{
        static_cast<double>(completed) / static_cast<double>(total), stage});
  }
}

double unit_scale(operation::LengthUnit input,
                  operation::LengthUnit output) noexcept {
  if (input == output) return 1.0;
  return input == operation::LengthUnit::angstrom ? 0.1 : 10.0;
}

model::Vec3d coordinate(const model::CoordinateFrame& frame,
                        std::size_t index) {
  return std::visit(
      [index](const auto& values) {
        return model::Vec3d{static_cast<double>(values[index].x),
                            static_cast<double>(values[index].y),
                            static_cast<double>(values[index].z)};
      },
      frame.positions().values());
}

struct FrameFit {
  RigidTransform transform;
  std::size_t paired_atom_count{};
};

operation::Result<FrameFit> frame_fit(
    const model::CoordinateFrame& reference,
    const model::CoordinateFrame& mobile,
    std::span<const std::uint8_t> selected, std::span<const double> weights,
    FitMode mode, MissingAtomPolicy missing_policy) {
  if (mode == FitMode::none) {
    RigidTransform transform;
    transform.input_scale = unit_scale(mobile.metadata().coordinate_unit,
                                       reference.metadata().coordinate_unit);
    return operation::Result<FrameFit>::success(FrameFit{transform, 0U});
  }
  const auto fitted = fit_rigid(
      FitRequest{&reference, &mobile, selected, weights, missing_policy});
  if (!fitted.has_value()) {
    return operation::Result<FrameFit>::failure(fitted.error());
  }
  return operation::Result<FrameFit>::success(
      FrameFit{fitted.value().transform,
               fitted.value().after.paired_atom_count});
}

operation::Result<std::shared_ptr<const model::CoordinateFrame>>
reference_frame(const std::shared_ptr<const model::CoordinateSource>& source,
                std::size_t index) {
  const auto count = source->frame_count();
  if (!count.has_value() || index >= *count) {
    return operation::Result<
        std::shared_ptr<const model::CoordinateFrame>>::failure(
        invalid("reference frame index exceeds the coordinate source",
                "choose a reference frame smaller than the frame count"));
  }
  return source->read_frame(index);
}

std::optional<operation::Error> validate_alignment_series_inputs(
    std::size_t atom_count, std::span<const std::uint8_t> selected,
    std::span<const std::uint8_t> fit_selected,
    std::span<const double> weights) {
  if ((!selected.empty() &&
       !selection::mask_is_valid(selected, atom_count)) ||
      (!fit_selected.empty() &&
       !selection::mask_is_valid(fit_selected, atom_count))) {
    return operation::Error{operation::ErrorCode::invalid_selection,
                            "alignment time-series selection mask is invalid",
                            {}};
  }
  if ((!weights.empty() && weights.size() != atom_count) ||
      !std::all_of(weights.begin(), weights.end(), [](double weight) {
        return std::isfinite(weight) && weight >= 0.0;
      })) {
    return invalid(
        "alignment time-series weights must be finite, non-negative, and "
        "contain one value per atom");
  }
  return std::nullopt;
}

}  // namespace

operation::Result<std::vector<CenterSeriesRow>> center_series(
    const CenterSeriesRequest& request, operation::TaskContext& context) {
  if (const auto failure = validate_range(request.source, request.range);
      failure.has_value()) {
    return operation::Result<std::vector<CenterSeriesRow>>::failure(*failure);
  }
  const auto total = row_count(request.range);
  std::vector<CenterSeriesRow> rows;
  rows.reserve(total);
  std::size_t frame_index = request.range.first;
  while (true) {
    if (context.cancellation.is_cancelled()) {
      return operation::Result<std::vector<CenterSeriesRow>>::failure(
          operation::Error{operation::ErrorCode::cancelled,
                           "center time-series analysis cancelled before frame " +
                               std::to_string(frame_index),
                           {}});
    }
    const auto frame = request.source->read_frame(frame_index);
    if (!frame.has_value()) {
      return operation::Result<std::vector<CenterSeriesRow>>::failure(
          frame_failure(frame_index, frame.error()));
    }
    CenterRequest center_request;
    center_request.frame = frame.value().get();
    center_request.selected = request.selected;
    center_request.mode = request.mode;
    center_request.masses = request.masses;
    center_request.mass_unit = request.mass_unit;
    center_request.mass_source = request.mass_source;
    center_request.masses_estimated = request.masses_estimated;
    center_request.missing_atom_policy = request.missing_atom_policy;
    const auto center = calculate_center(center_request);
    if (!center.has_value()) {
      return operation::Result<std::vector<CenterSeriesRow>>::failure(
          frame_failure(frame_index, center.error()));
    }
    rows.push_back(
        CenterSeriesRow{metadata(frame_index, *frame.value()), center.value()});
    progress(context, rows.size(), total, "center-time-series");
    if (frame_index == request.range.last ||
        !next_frame(frame_index, request.range)) {
      break;
    }
  }
  return operation::Result<std::vector<CenterSeriesRow>>::success(
      std::move(rows));
}

operation::Result<std::vector<DistanceSeriesRow>> distance_series(
    const DistanceSeriesRequest& request, operation::TaskContext& context) {
  if (const auto failure = validate_range(request.source, request.range);
      failure.has_value()) {
    return operation::Result<std::vector<DistanceSeriesRow>>::failure(*failure);
  }
  const auto total = row_count(request.range);
  std::vector<DistanceSeriesRow> rows;
  rows.reserve(total);
  std::size_t frame_index = request.range.first;
  while (true) {
    if (context.cancellation.is_cancelled()) {
      return operation::Result<std::vector<DistanceSeriesRow>>::failure(
          operation::Error{
              operation::ErrorCode::cancelled,
              "distance time-series analysis cancelled before frame " +
                  std::to_string(frame_index),
              {}});
    }
    const auto frame = request.source->read_frame(frame_index);
    if (!frame.has_value()) {
      return operation::Result<std::vector<DistanceSeriesRow>>::failure(
          frame_failure(frame_index, frame.error()));
    }
    const auto distance = atom_distance(*frame.value(), request.first,
                                        request.second, request.boundary);
    if (!distance.has_value()) {
      return operation::Result<std::vector<DistanceSeriesRow>>::failure(
          frame_failure(frame_index, distance.error()));
    }
    rows.push_back(DistanceSeriesRow{metadata(frame_index, *frame.value()),
                                     distance.value()});
    progress(context, rows.size(), total, "distance-time-series");
    if (frame_index == request.range.last ||
        !next_frame(frame_index, request.range)) {
      break;
    }
  }
  return operation::Result<std::vector<DistanceSeriesRow>>::success(
      std::move(rows));
}

operation::Result<std::vector<RmsdSeriesRow>> rmsd_series(
    const RmsdSeriesRequest& request, operation::TaskContext& context) {
  if (const auto failure = validate_range(request.source, request.range);
      failure.has_value()) {
    return operation::Result<std::vector<RmsdSeriesRow>>::failure(*failure);
  }
  if (const auto failure = validate_alignment_series_inputs(
          request.source->atom_count(), request.selected,
          request.fit_selected, request.weights);
      failure.has_value()) {
    return operation::Result<std::vector<RmsdSeriesRow>>::failure(*failure);
  }
  const auto reference = reference_frame(request.source,
                                         request.reference_frame);
  if (!reference.has_value()) {
    return operation::Result<std::vector<RmsdSeriesRow>>::failure(
        reference.error());
  }
  const auto total = row_count(request.range);
  std::vector<RmsdSeriesRow> rows;
  rows.reserve(total);
  std::size_t frame_index = request.range.first;
  while (true) {
    if (context.cancellation.is_cancelled()) {
      return operation::Result<std::vector<RmsdSeriesRow>>::failure(
          operation::Error{operation::ErrorCode::cancelled,
                           "RMSD time-series analysis cancelled before frame " +
                               std::to_string(frame_index),
                           {}});
    }
    const auto frame = request.source->read_frame(frame_index);
    if (!frame.has_value()) {
      return operation::Result<std::vector<RmsdSeriesRow>>::failure(
          frame_failure(frame_index, frame.error()));
    }
    const auto fitted = frame_fit(*reference.value(), *frame.value(),
                                  request.fit_selected, request.weights,
                                  request.fit,
                                  request.missing_atom_policy);
    if (!fitted.has_value()) {
      return operation::Result<std::vector<RmsdSeriesRow>>::failure(
          frame_failure(frame_index, fitted.error()));
    }
    RigidTransform identity;
    identity.input_scale = unit_scale(
        frame.value()->metadata().coordinate_unit,
        reference.value()->metadata().coordinate_unit);
    const auto before = calculate_rmsd(RmsdRequest{
        reference.value().get(), frame.value().get(), request.selected,
        request.weights, request.missing_atom_policy, identity});
    const auto after = calculate_rmsd(RmsdRequest{
        reference.value().get(), frame.value().get(), request.selected,
        request.weights, request.missing_atom_policy,
        fitted.value().transform});
    if (!before.has_value()) {
      return operation::Result<std::vector<RmsdSeriesRow>>::failure(
          frame_failure(frame_index, before.error()));
    }
    if (!after.has_value()) {
      return operation::Result<std::vector<RmsdSeriesRow>>::failure(
          frame_failure(frame_index, after.error()));
    }
    rows.push_back(RmsdSeriesRow{metadata(frame_index, *frame.value()),
                                 after.value(), before.value().rmsd,
                                 fitted.value().paired_atom_count});
    progress(context, rows.size(), total, "rmsd-time-series");
    if (frame_index == request.range.last ||
        !next_frame(frame_index, request.range)) {
      break;
    }
  }
  return operation::Result<std::vector<RmsdSeriesRow>>::success(
      std::move(rows));
}

operation::Result<RmsdMatrixResult> rmsd_matrix(
    const RmsdMatrixRequest& request,operation::TaskContext& context) {
  if(const auto failure=validate_range(request.source,request.range);failure.has_value())
    return operation::Result<RmsdMatrixResult>::failure(*failure);
  if(const auto failure=validate_alignment_series_inputs(
         request.source->atom_count(),request.selected,request.fit_selected,
         request.weights);failure.has_value())
    return operation::Result<RmsdMatrixResult>::failure(*failure);
  if(request.frame_pair_budget==0U)
    return operation::Result<RmsdMatrixResult>::failure(invalid("RMSD matrix frame-pair budget must be positive"));
  RmsdMatrixResult result;
  std::size_t frame_index=request.range.first;
  while(true) {
    result.frame_indices.push_back(frame_index);
    if(frame_index==request.range.last || !next_frame(frame_index,request.range)) break;
  }
  const auto n=result.frame_indices.size();
  if(n==std::numeric_limits<std::size_t>::max() ||
     (n>0U && n>std::numeric_limits<std::size_t>::max()/(n+1U)))
    return operation::Result<RmsdMatrixResult>::failure(invalid("RMSD matrix dimensions overflow"));
  const auto unique_pairs=(n*(n+1U))/2U;
  if(unique_pairs>request.frame_pair_budget)
    return operation::Result<RmsdMatrixResult>::failure(
        operation::Error{operation::ErrorCode::resource_exhausted,
          "RMSD matrix frame-pair budget is smaller than the requested upper triangle",
          "reduce the frame range or increase frame-pair-budget"});
  result.upper_triangle.reserve(unique_pairs);
  for(std::size_t row=0;row<n;++row) {
    if(context.cancellation.is_cancelled())
      return operation::Result<RmsdMatrixResult>::failure(
          operation::Error{operation::ErrorCode::cancelled,"RMSD matrix calculation cancelled",{}});
    const auto reference=request.source->read_frame(result.frame_indices[row]);
    if(!reference.has_value())
      return operation::Result<RmsdMatrixResult>::failure(frame_failure(result.frame_indices[row],reference.error()));
    if(row==0U) result.coordinate_unit=reference.value()->metadata().coordinate_unit;
    for(std::size_t column=row;column<n;++column) {
      if(context.cancellation.is_cancelled())
        return operation::Result<RmsdMatrixResult>::failure(
            operation::Error{operation::ErrorCode::cancelled,"RMSD matrix calculation cancelled",{}});
      const auto mobile=request.source->read_frame(result.frame_indices[column]);
      if(!mobile.has_value())
        return operation::Result<RmsdMatrixResult>::failure(frame_failure(result.frame_indices[column],mobile.error()));
      RigidTransform transform;
      std::size_t fit_pairs{};
      if(row==column) {
        transform.input_scale=unit_scale(mobile.value()->metadata().coordinate_unit,
                                         reference.value()->metadata().coordinate_unit);
      } else {
        const auto fitted=frame_fit(*reference.value(),*mobile.value(),request.fit_selected,
                                    request.weights,request.fit,request.missing_atom_policy);
        if(!fitted.has_value())
          return operation::Result<RmsdMatrixResult>::failure(frame_failure(result.frame_indices[column],fitted.error()));
        transform=fitted.value().transform; fit_pairs=fitted.value().paired_atom_count;
      }
      const auto rmsd=calculate_rmsd({reference.value().get(),mobile.value().get(),
          request.selected,request.weights,request.missing_atom_policy,transform});
      if(!rmsd.has_value())
        return operation::Result<RmsdMatrixResult>::failure(frame_failure(result.frame_indices[column],rmsd.error()));
      result.upper_triangle.push_back({result.frame_indices[row],result.frame_indices[column],rmsd.value(),fit_pairs});
      ++result.evaluated_frame_pair_count;
      progress(context,result.upper_triangle.size(),unique_pairs,"rmsd-matrix");
    }
  }
  return operation::Result<RmsdMatrixResult>::success(std::move(result));
}

operation::Result<RmsfSeriesResult> rmsf_series(
    const RmsfSeriesRequest& request, operation::TaskContext& context) {
  if (const auto failure = validate_range(request.source, request.range);
      failure.has_value()) {
    return operation::Result<RmsfSeriesResult>::failure(*failure);
  }
  const auto atom_count = request.source->atom_count();
  if (const auto failure = validate_alignment_series_inputs(
          atom_count, request.selected, request.fit_selected,
          request.weights);
      failure.has_value()) {
    return operation::Result<RmsfSeriesResult>::failure(*failure);
  }
  const auto reference = reference_frame(request.source,
                                         request.reference_frame);
  if (!reference.has_value()) {
    return operation::Result<RmsfSeriesResult>::failure(reference.error());
  }
  struct Accumulator {
    std::size_t count{};
    model::Vec3d mean;
    double squared_deviation{};
  };
  std::vector<Accumulator> accumulators(atom_count);
  const auto total = row_count(request.range);
  std::size_t completed{};
  std::size_t frame_index = request.range.first;
  while (true) {
    if (context.cancellation.is_cancelled()) {
      return operation::Result<RmsfSeriesResult>::failure(operation::Error{
          operation::ErrorCode::cancelled,
          "RMSF time-series analysis cancelled before frame " +
              std::to_string(frame_index),
          {}});
    }
    const auto frame = request.source->read_frame(frame_index);
    if (!frame.has_value()) {
      return operation::Result<RmsfSeriesResult>::failure(
          frame_failure(frame_index, frame.error()));
    }
    const auto fitted = frame_fit(*reference.value(), *frame.value(),
                                  request.fit_selected, request.weights,
                                  request.fit,
                                  request.missing_atom_policy);
    if (!fitted.has_value()) {
      return operation::Result<RmsfSeriesResult>::failure(
          frame_failure(frame_index, fitted.error()));
    }
    for (std::size_t atom = 0; atom < atom_count; ++atom) {
      if (!request.selected.empty() && request.selected[atom] == 0U) continue;
      if (!frame.value()->atom_present(atom)) {
        if (request.missing_atom_policy == MissingAtomPolicy::error) {
          return operation::Result<RmsfSeriesResult>::failure(frame_failure(
              frame_index,
              operation::Error{
                  operation::ErrorCode::invalid_selection,
                  "RMSF selection contains a missing atom at index " +
                      std::to_string(atom + 1U),
                  "remove missing atoms or explicitly use the skip policy"}));
        }
        continue;
      }
      const auto value = fitted.value().transform.apply(
          coordinate(*frame.value(), atom));
      auto& accumulator = accumulators[atom];
      ++accumulator.count;
      const auto inverse_count = 1.0 / static_cast<double>(accumulator.count);
      const model::Vec3d delta{value.x - accumulator.mean.x,
                               value.y - accumulator.mean.y,
                               value.z - accumulator.mean.z};
      accumulator.mean.x += delta.x * inverse_count;
      accumulator.mean.y += delta.y * inverse_count;
      accumulator.mean.z += delta.z * inverse_count;
      const model::Vec3d next_delta{value.x - accumulator.mean.x,
                                    value.y - accumulator.mean.y,
                                    value.z - accumulator.mean.z};
      accumulator.squared_deviation +=
          delta.x * next_delta.x + delta.y * next_delta.y +
          delta.z * next_delta.z;
    }
    ++completed;
    progress(context, completed, total, "rmsf-time-series");
    if (frame_index == request.range.last ||
        !next_frame(frame_index, request.range)) {
      break;
    }
  }
  RmsfSeriesResult result;
  result.coordinate_unit = reference.value()->metadata().coordinate_unit;
  result.frame_count = completed;
  for (std::size_t atom = 0; atom < atom_count; ++atom) {
    if (!request.selected.empty() && request.selected[atom] == 0U) continue;
    ++result.selected_atom_count;
    const auto& accumulator = accumulators[atom];
    std::optional<double> rmsf;
    if (accumulator.count != 0U) {
      rmsf = std::sqrt(accumulator.squared_deviation /
                       static_cast<double>(accumulator.count));
    }
    result.atoms.push_back(
        RmsfAtomRow{model::AtomIndex{atom}, accumulator.count, rmsf});
  }
  return operation::Result<RmsfSeriesResult>::success(std::move(result));
}

operation::Result<ContactSeriesResult> contact_series(
    const ContactSeriesRequest& request, operation::TaskContext& context) {
  if(const auto failure=validate_range(request.source,request.range); failure.has_value())
    return operation::Result<ContactSeriesResult>::failure(*failure);
  const auto atom_count=request.source->atom_count();
  if(request.topology==nullptr || request.topology->atom_count()!=atom_count ||
     !selection::mask_is_valid(request.first,atom_count) ||
     !selection::mask_is_valid(request.second,atom_count))
    return operation::Result<ContactSeriesResult>::failure(invalid(
        "contact time-series topology and masks must match the coordinate source"));
  struct Aggregate { std::size_t count{}; double mean{}; };
  std::map<std::pair<std::size_t,std::size_t>,Aggregate> aggregates;
  ContactSeriesResult result; result.coordinate_unit=request.cutoff_unit;
  const auto total=row_count(request.range);
  std::size_t frame_index=request.range.first;
  while(true) {
    if(context.cancellation.is_cancelled())
      return operation::Result<ContactSeriesResult>::failure(
          operation::Error{operation::ErrorCode::cancelled,
            "contact time-series analysis cancelled before frame "+std::to_string(frame_index),{}});
    const auto frame=request.source->read_frame(frame_index);
    if(!frame.has_value()) return operation::Result<ContactSeriesResult>::failure(
        frame_failure(frame_index,frame.error()));
    const auto cutoff=request.cutoff*unit_scale(request.cutoff_unit,frame.value()->metadata().coordinate_unit);
    const auto contacts=find_contacts({*frame.value(),request.first,request.second,
        request.topology->bonds(),cutoff,request.boundary,request.same_selection,
        request.exclude_bonded});
    if(!contacts.has_value()) return operation::Result<ContactSeriesResult>::failure(
        frame_failure(frame_index,contacts.error()));
    result.frames.push_back({metadata(frame_index,*frame.value()),contacts.value().pairs.size()});
    const auto scale=unit_scale(contacts.value().coordinate_unit,request.cutoff_unit);
    if(request.collect_occupancy) for(const auto& pair:contacts.value().pairs) {
      auto& aggregate=aggregates[{pair.first.value,pair.second.value}];
      ++aggregate.count;
      const auto value=pair.distance*scale;
      aggregate.mean += (value-aggregate.mean)/static_cast<double>(aggregate.count);
    }
    progress(context,result.frames.size(),total,"contact-time-series");
    if(frame_index==request.range.last || !next_frame(frame_index,request.range)) break;
  }
  result.frame_count=result.frames.size();
  for(const auto& [key,aggregate]:aggregates)
    result.occupancy.push_back({model::AtomIndex{key.first},model::AtomIndex{key.second},
      aggregate.count,static_cast<double>(aggregate.count)/static_cast<double>(result.frame_count),
      aggregate.mean});
  return operation::Result<ContactSeriesResult>::success(std::move(result));
}

operation::Result<HydrogenBondSeriesResult> hydrogen_bond_series(
    const HydrogenBondSeriesRequest& request, operation::TaskContext& context) {
  if(const auto failure=validate_range(request.source,request.range); failure.has_value())
    return operation::Result<HydrogenBondSeriesResult>::failure(*failure);
  const auto atom_count=request.source->atom_count();
  if(request.topology==nullptr || request.topology->atom_count()!=atom_count ||
     !selection::mask_is_valid(request.donor_selection,atom_count) ||
     !selection::mask_is_valid(request.acceptor_selection,atom_count) ||
     !selection::mask_is_valid(request.donor_capable,atom_count) ||
     !selection::mask_is_valid(request.acceptor_capable,atom_count))
    return operation::Result<HydrogenBondSeriesResult>::failure(invalid(
        "hydrogen-bond time-series topology and masks must match the coordinate source"));
  struct Aggregate { std::size_t count{}; double distance_mean{}; double angle_mean{}; };
  using Key=std::tuple<std::size_t,std::size_t,std::size_t>;
  std::map<Key,Aggregate> aggregates;
  HydrogenBondSeriesResult result; result.coordinate_unit=request.cutoff_unit;
  const auto total=row_count(request.range);
  std::size_t frame_index=request.range.first;
  while(true) {
    if(context.cancellation.is_cancelled())
      return operation::Result<HydrogenBondSeriesResult>::failure(
          operation::Error{operation::ErrorCode::cancelled,
            "hydrogen-bond time-series analysis cancelled before frame "+std::to_string(frame_index),{}});
    const auto frame=request.source->read_frame(frame_index);
    if(!frame.has_value()) return operation::Result<HydrogenBondSeriesResult>::failure(
        frame_failure(frame_index,frame.error()));
    const auto cutoff=request.distance_cutoff*unit_scale(request.cutoff_unit,frame.value()->metadata().coordinate_unit);
    const auto bonds=find_hydrogen_bonds({*frame.value(),*request.topology,
        request.donor_selection,request.acceptor_selection,request.donor_capable,
        request.acceptor_capable,cutoff,request.maximum_angle_deviation_degrees,
        request.boundary,request.same_selection});
    if(!bonds.has_value()) return operation::Result<HydrogenBondSeriesResult>::failure(
        frame_failure(frame_index,bonds.error()));
    result.frames.push_back({metadata(frame_index,*frame.value()),bonds.value().bonds.size()});
    const auto scale=unit_scale(bonds.value().coordinate_unit,request.cutoff_unit);
    if(request.collect_occupancy) for(const auto& bond:bonds.value().bonds) {
      auto& aggregate=aggregates[{bond.donor.value,bond.acceptor.value,bond.hydrogen.value}];
      ++aggregate.count;
      const auto inverse=1.0/static_cast<double>(aggregate.count);
      aggregate.distance_mean += (bond.donor_acceptor_distance*scale-aggregate.distance_mean)*inverse;
      aggregate.angle_mean += (bond.angle_deviation_degrees-aggregate.angle_mean)*inverse;
    }
    progress(context,result.frames.size(),total,"hbond-time-series");
    if(frame_index==request.range.last || !next_frame(frame_index,request.range)) break;
  }
  result.frame_count=result.frames.size();
  for(const auto& [key,aggregate]:aggregates)
    result.occupancy.push_back({model::AtomIndex{std::get<0>(key)},
      model::AtomIndex{std::get<1>(key)},model::AtomIndex{std::get<2>(key)},
      aggregate.count,static_cast<double>(aggregate.count)/static_cast<double>(result.frame_count),
      aggregate.distance_mean,aggregate.angle_mean});
  return operation::Result<HydrogenBondSeriesResult>::success(std::move(result));
}

}  // namespace molshredder::analysis
