#include "molshredder/analysis/rdf.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {
int failures{};
void check(bool condition,const std::string &message){if(!condition){std::cerr<<"FAIL: "<<message<<'\n';++failures;}}
std::shared_ptr<const molshredder::model::CoordinateFrame> make_frame(
    std::vector<molshredder::model::Vec3d> positions,
    std::optional<molshredder::model::UnitCell> cell=std::nullopt,
    std::vector<std::uint8_t> presence={}){
  molshredder::model::FrameMetadata metadata;
  metadata.coordinate_unit=molshredder::operation::LengthUnit::angstrom;
  metadata.unit_cell=cell;
  return molshredder::model::CoordinateFrame::create(
      molshredder::model::CoordinateBuffer{std::move(positions)},std::nullopt,
      std::move(presence),metadata).value();
}
}

int main(){
  using namespace molshredder;
  const std::vector<std::uint8_t> all{1U,1U,1U,1U};
  const auto raw=make_frame({{0,0,0},{1,0,0},{2,0,0},{4,0,0}});
  const auto counted=analysis::radial_distribution_function(
      {raw.get(),all,all,3.0,1.0,analysis::DistanceBoundary::raw,
       analysis::RdfNormalization::pair_count,true,100U,nullptr});
  check(counted.has_value(),"raw pair-count RDF succeeds");
  if(counted.has_value()){
    check(counted.value().eligible_pair_count==6U,"same selection uses unordered pairs");
    check(counted.value().bins.size()==3U,"three bins are emitted");
    check(counted.value().bins[0].pair_count==0U && counted.value().bins[1].pair_count==2U && counted.value().bins[2].pair_count==2U,
          "half-open bins contain analytic counts");
  }

  const model::UnitCell box{{10,0,0},{0,10,0},{0,0,10}};
  const auto periodic=make_frame({{0,0,0},{9,0,0},{5,5,5},{6,5,5}},box);
  const auto normalized=analysis::radial_distribution_function(
      {periodic.get(),all,all,2.0,1.0,analysis::DistanceBoundary::minimum_image,
       analysis::RdfNormalization::radial_distribution,true,100U,nullptr});
  check(normalized.has_value(),"periodic normalized RDF succeeds");
  if(normalized.has_value()){
    constexpr double pi=3.14159265358979323846;
    const auto expected=2.0/(6.0*(4.0*pi/3.0)*7.0/1000.0);
    check(normalized.value().bins[1].pair_count==2U,"minimum image finds analytic pairs");
    check(std::abs(normalized.value().bins[1].value-expected)<1e-12,"g(r) normalization is analytic");
  }

  check(!analysis::radial_distribution_function(
      {raw.get(),all,all,2.0,1.0,analysis::DistanceBoundary::raw,
       analysis::RdfNormalization::radial_distribution,true,100U,nullptr}).has_value(),
       "g(r) rejects raw coordinates without volume");
  check(!analysis::radial_distribution_function(
      {periodic.get(),all,all,5.1,1.0,analysis::DistanceBoundary::minimum_image,
       analysis::RdfNormalization::pair_count,true,100U,nullptr}).has_value(),
       "radius above half minimum cell height fails");
  check(!analysis::radial_distribution_function(
      {raw.get(),all,all,3.0,1.0,analysis::DistanceBoundary::raw,
       analysis::RdfNormalization::pair_count,true,1U,nullptr}).has_value(),
       "budget exhaustion is failure-atomic");
  const auto missing=make_frame({{0,0,0},{1,0,0},{2,0,0},{4,0,0}},std::nullopt,
                                {1U,0U,1U,1U});
  const auto skipped=analysis::radial_distribution_function(
      {missing.get(),all,all,3.0,1.0,analysis::DistanceBoundary::raw,
       analysis::RdfNormalization::pair_count,true,100U,nullptr});
  check(skipped.has_value() && skipped.value().ignored_missing_atoms==1U &&
            skipped.value().eligible_pair_count==3U,
        "missing selected atoms are counted and skipped consistently");
  operation::TaskContext context; context.cancellation.request_cancel();
  check(!analysis::radial_distribution_function(
      {raw.get(),all,all,3.0,1.0,analysis::DistanceBoundary::raw,
       analysis::RdfNormalization::pair_count,true,100U,&context}).has_value(),
       "pre-cancelled RDF returns no result");
  return failures==0?0:1;
}
