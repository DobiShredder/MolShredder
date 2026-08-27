#include "molshredder/analysis/rdf.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "molshredder/operation/error.hpp"
#include "molshredder/trajectory/pbc.hpp"

namespace molshredder::analysis {
namespace {
operation::Error invalid(std::string message) {
  return {operation::ErrorCode::invalid_argument, std::move(message), {}};
}
operation::Error exhausted() {
  return {operation::ErrorCode::resource_exhausted,
          "RDF pair evaluation budget was exhausted",
          "reduce the selection or radius, or increase evaluation-budget"};
}
operation::Error cancelled() {
  return {operation::ErrorCode::cancelled, "RDF calculation was cancelled", {}};
}
std::vector<model::Vec3d> positions(const model::CoordinateFrame &frame) {
  return std::visit([](const auto &values) {
    std::vector<model::Vec3d> result;
    result.reserve(values.size());
    for (const auto &p : values)
      result.push_back({static_cast<double>(p.x), static_cast<double>(p.y),
                        static_cast<double>(p.z)});
    return result;
  }, frame.positions().values());
}
model::Vec3d subtract(model::Vec3d a, model::Vec3d b) {
  return {a.x-b.x, a.y-b.y, a.z-b.z};
}
double norm(model::Vec3d v) { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); }
model::Vec3d cross(model::Vec3d a, model::Vec3d b) {
  return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
} // namespace

operation::Result<RdfResult> radial_distribution_function(const RdfRequest &request) {
  if (request.frame == nullptr)
    return operation::Result<RdfResult>::failure(invalid("RDF requires a coordinate frame"));
  const auto atom_count=request.frame->atom_count();
  if(request.first.size()!=atom_count || request.second.size()!=atom_count)
    return operation::Result<RdfResult>::failure(invalid("RDF selection masks must match the coordinate atom count"));
  if(!(request.maximum_radius>0.0) || !std::isfinite(request.maximum_radius) ||
     !(request.bin_width>0.0) || !std::isfinite(request.bin_width) ||
     request.bin_width>request.maximum_radius)
    return operation::Result<RdfResult>::failure(invalid("RDF radius and bin width must be finite and positive, with bin width not greater than radius"));
  if(request.evaluation_budget==0U)
    return operation::Result<RdfResult>::failure(invalid("RDF evaluation budget must be positive"));
  if(request.context!=nullptr && request.context->cancellation.is_cancelled())
    return operation::Result<RdfResult>::failure(cancelled());

  const model::UnitCell *cell=nullptr;
  double cell_volume=0.0;
  if(request.boundary==DistanceBoundary::minimum_image) {
    if(!request.frame->metadata().unit_cell.has_value())
      return operation::Result<RdfResult>::failure(invalid("minimum-image RDF requires unit-cell metadata"));
    cell=&*request.frame->metadata().unit_cell;
    cell_volume=cell->signed_volume();
    const auto ab=norm(cross(cell->a,cell->b));
    const auto bc=norm(cross(cell->b,cell->c));
    const auto ca=norm(cross(cell->c,cell->a));
    if(!(cell_volume>0.0) || !std::isfinite(cell_volume) || !(ab>0.0) || !(bc>0.0) || !(ca>0.0))
      return operation::Result<RdfResult>::failure(invalid("RDF requires a finite positive unit-cell volume"));
    const auto minimum_height=std::min({cell_volume/bc,cell_volume/ca,cell_volume/ab});
    if(!std::isfinite(minimum_height) || request.maximum_radius>0.5*minimum_height)
      return operation::Result<RdfResult>::failure(invalid("minimum-image RDF radius must not exceed half the minimum cell height"));
  } else if(request.normalization==RdfNormalization::radial_distribution) {
    return operation::Result<RdfResult>::failure(invalid("radial-distribution normalization requires minimum-image PBC and a unit-cell volume"));
  }

  const auto bin_count_value=std::ceil(request.maximum_radius/request.bin_width);
  if(!std::isfinite(bin_count_value) || bin_count_value>10'000'000.0)
    return operation::Result<RdfResult>::failure(invalid("RDF bin count exceeds the supported bound"));
  const auto bin_count=static_cast<std::size_t>(bin_count_value);
  RdfResult result;
  result.maximum_radius=request.maximum_radius; result.bin_width=request.bin_width;
  result.boundary=request.boundary; result.normalization=request.normalization;
  result.coordinate_unit=request.frame->metadata().coordinate_unit;
  result.bins.resize(bin_count);
  for(std::size_t i=0;i<bin_count;++i) {
    const auto lower=static_cast<double>(i)*request.bin_width;
    const auto upper=std::min(request.maximum_radius,lower+request.bin_width);
    result.bins[i]={lower,upper,0.5*(lower+upper),0U,0.0};
  }

  const auto xyz=positions(*request.frame);
  std::vector<std::size_t> first_atoms,second_atoms;
  for(std::size_t atom=0;atom<atom_count;++atom) {
    const auto present=request.frame->atom_present(atom);
    if((request.first[atom]||request.second[atom])&&!present) ++result.ignored_missing_atoms;
    if(request.first[atom]&&present) first_atoms.push_back(atom);
    if(request.second[atom]&&present) second_atoms.push_back(atom);
  }
  if(first_atoms.empty()||second_atoms.empty())
    return operation::Result<RdfResult>::failure(invalid("RDF selections must each contain a present atom"));

  const auto maximum_squared=request.maximum_radius*request.maximum_radius;
  for(std::size_t outer=0;outer<first_atoms.size();++outer) {
    const auto first=first_atoms[outer];
    for(const auto second:second_atoms) {
      if(first==second || (request.same_selection&&first>second)) continue;
      if(result.evaluated_pair_count>=request.evaluation_budget)
        return operation::Result<RdfResult>::failure(exhausted());
      ++result.evaluated_pair_count; ++result.eligible_pair_count;
      auto displacement=subtract(xyz[second],xyz[first]);
      if(cell!=nullptr) {
        const auto image=trajectory::minimum_image(*cell,displacement);
        if(!image.has_value()) return operation::Result<RdfResult>::failure(image.error());
        displacement=image.value().displacement;
      }
      const auto d2=displacement.x*displacement.x+displacement.y*displacement.y+displacement.z*displacement.z;
      if(d2<maximum_squared) {
        const auto index=std::min(bin_count-1U,static_cast<std::size_t>(std::sqrt(d2)/request.bin_width));
        ++result.bins[index].pair_count;
      }
      if(request.context!=nullptr && (result.evaluated_pair_count&0x3ffU)==0U && request.context->cancellation.is_cancelled())
        return operation::Result<RdfResult>::failure(cancelled());
    }
    if(request.context!=nullptr && request.context->report_progress)
      request.context->report_progress({static_cast<double>(outer+1U)/static_cast<double>(first_atoms.size()),"rdf-pairs"});
  }
  if(result.eligible_pair_count==0U)
    return operation::Result<RdfResult>::failure(invalid("RDF selections do not define any distinct atom pairs"));

  constexpr double pi=3.141592653589793238462643383279502884;
  for(auto &bin:result.bins) {
    if(request.normalization==RdfNormalization::pair_count) {
      bin.value=static_cast<double>(bin.pair_count);
    } else {
      const auto shell=(4.0*pi/3.0)*(bin.upper*bin.upper*bin.upper-bin.lower*bin.lower*bin.lower);
      const auto expected=static_cast<double>(result.eligible_pair_count)*shell/cell_volume;
      bin.value=expected>0.0?static_cast<double>(bin.pair_count)/expected:0.0;
    }
  }
  return operation::Result<RdfResult>::success(std::move(result));
}
} // namespace molshredder::analysis
