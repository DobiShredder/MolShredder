#include <cmath>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <set>
#include <vector>

#include "molshredder/analysis/secondary_structure.hpp"

namespace {
using namespace molshredder;
void require(bool condition,const char* message){if(!condition){std::cerr<<message<<'\n';std::exit(EXIT_FAILURE);}}
struct Fixture {
  std::shared_ptr<const model::Topology> topology;
  std::shared_ptr<const model::CoordinateFrame> frame;
};
Fixture helix_fixture(){
  model::TopologyBuilder builder; std::vector<model::Vec3d> xyz;
  for(std::size_t r=0;r<6;++r){
    const auto residue=builder.add_residue({"ALA",static_cast<std::int64_t>(r+1),"","A",""}).value();
    const auto base=100.0+static_cast<double>(r)*20.0;
    std::vector<std::pair<std::string,model::Vec3d>> atoms{
      {"N",{base,0,0}},{"CA",{base,1,0}},{"C",{base,2,0}},
      {"O",{base,3,0}},{"H",{base,0,0}}};
    if(r==0){atoms[2].second={-1,0,0};atoms[3].second={0,0,0};}
    if(r==1){atoms[2].second={-1,10,0};atoms[3].second={0,10,0};}
    if(r==4){atoms[0].second={3,0,0};atoms[4].second={2,0,0};}
    if(r==5){atoms[0].second={3,10,0};atoms[4].second={2,10,0};}
    for(const auto& [name,position]:atoms){
      const auto z=name=="H"?1U:(name=="O"?8U:(name=="N"?7U:6U));
      require(builder.add_atom({name,static_cast<std::uint8_t>(z),residue,"",0,{}}).has_value(),"atom build failed");
      xyz.push_back(position);
    }
  }
  auto topology=builder.build(); require(topology.has_value(),"topology build failed");
  auto frame=model::CoordinateFrame::create(model::CoordinateBuffer{std::move(xyz)});
  require(frame.has_value(),"frame build failed"); return {topology.value(),frame.value()};
}
Fixture scaling_fixture(std::size_t residue_count){
  model::TopologyBuilder builder;std::vector<model::Vec3f> xyz;
  xyz.reserve(residue_count*5U);
  for(std::size_t r=0;r<residue_count;++r){
    const auto residue=builder.add_residue({"ALA",static_cast<std::int64_t>(r+1),"","A",""}).value();
    const auto base=static_cast<float>(r)*3.8F;
    const std::vector<std::pair<std::string,model::Vec3f>> atoms{
      {"N",{base,0.0F,0.0F}},{"CA",{base+1.0F,0.4F,0.0F}},
      {"C",{base+2.0F,0.0F,0.0F}},{"O",{base+2.8F,0.0F,0.0F}},
      {"H",{base-0.8F,0.0F,0.0F}}};
    for(const auto& [name,position]:atoms){
      const auto z=name=="H"?1U:(name=="O"?8U:(name=="N"?7U:6U));
      require(builder.add_atom({name,static_cast<std::uint8_t>(z),residue,"",0,{}}).has_value(),
              "scaling atom build failed");xyz.push_back(position);
    }
  }
  auto topology=builder.build();require(topology.has_value(),"scaling topology build failed");
  auto frame=model::CoordinateFrame::create(model::CoordinateBuffer{std::move(xyz)});
  require(frame.has_value(),"scaling frame build failed");return {topology.value(),frame.value()};
}
}
int main(){
  using namespace molshredder;
  const auto energy=analysis::stride_method_hbond_energy_v0(
      {3,0,0},{2,0,0},{0,0,0},{-1,0,0});
  require(energy.has_value()&&std::abs(energy.value()+2.8)<1e-12,
          "ideal v0 H-bond energy must reach radial minimum");
  require(!analysis::stride_method_hbond_energy_v0({0,0,0},{0,0,0},{0,0,0},{1,0,0}).has_value(),
          "degenerate H-bond geometry accepted");
  const auto fixture=helix_fixture();
  const auto geometry=analysis::calculate_backbone_geometry(*fixture.topology,*fixture.frame);
  require(geometry.has_value()&&geometry.value().size()==6,"backbone extraction failed");
  const auto assigned=analysis::assign_secondary_structure(*fixture.topology,*fixture.frame,
      analysis::SecondaryStructureParameters{-0.5,0.0,0.0});
  require(assigned.has_value(),"secondary-structure assignment failed");
  require(assigned.value().method=="molshredder-stride-method-v0","method provenance failed");
  require(assigned.value().residues[1].state==analysis::SecondaryStructureState::alpha_helix &&
          assigned.value().residues[4].state==analysis::SecondaryStructureState::alpha_helix,
          "two consecutive i-to-i+4 H-bonds did not assign alpha helix");
  require(analysis::stride_code(assigned.value().residues[2].state)=='H',"STRIDE state code failed");
  require(assigned.value().hydrogen_bonds.size()>=2,"accepted H-bonds not preserved");
  std::set<std::pair<std::size_t,std::size_t>> exhaustive;
  const auto& coordinates=std::get<std::vector<model::Vec3d>>(
      fixture.frame->positions().values());
  for(std::size_t donor=0;donor<6U;++donor)for(std::size_t acceptor=0;acceptor<6U;++acceptor){
    if(donor==acceptor)continue;
    const auto candidate=analysis::stride_method_hbond_energy_v0(
        coordinates[donor*5U],coordinates[donor*5U+4U],
        coordinates[acceptor*5U+3U],coordinates[acceptor*5U+2U]);
    if(candidate.has_value()&&candidate.value()<=-0.5)exhaustive.insert({donor,acceptor});
  }
  std::set<std::pair<std::size_t,std::size_t>> indexed;
  for(const auto& bond:assigned.value().hydrogen_bonds)
    indexed.insert({bond.donor.value,bond.acceptor.value});
  require(indexed==exhaustive,
          "spatial H-bond search differs from exhaustive assignment");
  std::vector<model::Vec3d> nanometer_positions;
  std::visit([&](const auto& values){for(const auto& p:values)nanometer_positions.push_back(
      {static_cast<double>(p.x)*0.1,static_cast<double>(p.y)*0.1,static_cast<double>(p.z)*0.1});},
      fixture.frame->positions().values());
  model::FrameMetadata nanometer_metadata;
  nanometer_metadata.coordinate_unit=operation::LengthUnit::nanometer;
  const auto nanometer_frame=model::CoordinateFrame::create(
      model::CoordinateBuffer{std::move(nanometer_positions)},std::nullopt,{},nanometer_metadata);
  const auto nanometer_assigned=analysis::assign_secondary_structure(
      *fixture.topology,*nanometer_frame.value(),analysis::SecondaryStructureParameters{-0.5,0.0,0.0});
  require(nanometer_assigned.has_value() &&
      nanometer_assigned.value().residues[2].state==analysis::SecondaryStructureState::alpha_helix,
      "nanometer coordinates changed secondary-structure assignment");
  auto invalid=analysis::assign_secondary_structure(*fixture.topology,*fixture.frame,
      analysis::SecondaryStructureParameters{0.0,0.05,0.02});
  require(!invalid.has_value(),"invalid energy cutoff accepted");
  constexpr std::size_t scaling_residues=5000U;
  const auto scaling=scaling_fixture(scaling_residues);
  const auto started=std::chrono::steady_clock::now();
  const auto scaled=analysis::assign_secondary_structure(*scaling.topology,*scaling.frame);
  const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now()-started);
  require(scaled.has_value()&&scaled.value().residues.size()==scaling_residues,
          "large secondary-structure assignment failed");
  require(elapsed<std::chrono::seconds{5},
          "secondary-structure spatial scaling regression exceeded five seconds");
  std::cout<<"secondary_residues="<<scaling_residues
           <<" assignment_ms="<<elapsed.count()<<'\n';
  return EXIT_SUCCESS;
}
