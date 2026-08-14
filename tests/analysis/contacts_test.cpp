#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "molshredder/analysis/contacts.hpp"
#include "molshredder/trajectory/pbc.hpp"

namespace {
using namespace molshredder;
void require(bool condition, const char* message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(EXIT_FAILURE); }
}
std::shared_ptr<const model::CoordinateFrame> frame(
    std::vector<model::Vec3d> xyz, std::optional<model::UnitCell> cell={}) {
  model::FrameMetadata metadata; metadata.unit_cell=cell;
  auto made=model::CoordinateFrame::create(model::CoordinateBuffer{std::move(xyz)},std::nullopt,{},metadata);
  require(made.has_value(),"frame creation failed"); return made.value();
}
std::shared_ptr<const model::Topology> hbond_topology() {
  model::TopologyBuilder builder;
  const auto residue=builder.add_residue({"MOL",1,"","A",""});
  require(residue.has_value(),"residue creation failed");
  require(builder.add_atom({"N",7,residue.value(),"",0,1}).has_value(),"donor atom failed");
  require(builder.add_atom({"H",1,residue.value(),"",0,2}).has_value(),"hydrogen atom failed");
  require(builder.add_atom({"O",8,residue.value(),"",0,3}).has_value(),"acceptor atom failed");
  require(!builder.add_bond({model::AtomIndex{0},model::AtomIndex{1},model::BondOrder::single}),"D-H bond failed");
  auto topology=builder.build(); require(topology.has_value(),"topology build failed"); return topology.value();
}
}
int main() {
  std::vector<std::uint8_t> all(5,1U);
  const auto raw=frame({{0,0,0},{0.5,0,0},{1.01,0,0},{100,0,0},{100.8,0,0}});
  std::vector<model::Bond> bonds{{model::AtomIndex{0},model::AtomIndex{1},model::BondOrder::single}};
  auto contacts=analysis::find_contacts({*raw,all,all,bonds,1.0,analysis::DistanceBoundary::raw,true,true});
  require(contacts.has_value(),"raw contacts failed");
  require(contacts.value().pairs.size()==2,"cell-list cutoff or bonded exclusion failed");
  require(contacts.value().pairs[0].first.value==1 && contacts.value().pairs[0].second.value==2,"deterministic pair order failed");
  require(contacts.value().pairs[1].first.value==3 && contacts.value().pairs[1].second.value==4,"distant cell contact failed");

  model::UnitCell cell{{10,0,0},{2,9,0},{1,1,8}};
  auto a=trajectory::fractional_to_cartesian(cell,{0.98,0.98,0.98});
  auto b=trajectory::fractional_to_cartesian(cell,{0.02,0.02,0.02});
  require(a.has_value()&&b.has_value(),"triclinic coordinates failed");
  const auto periodic=frame({a.value(),b.value(),{5,5,5}},cell);
  std::vector<std::uint8_t> three(3,1U);
  auto pbc=analysis::find_contacts({*periodic,three,three,{},1.0,analysis::DistanceBoundary::minimum_image,true,true});
  require(pbc.has_value(),"triclinic contacts failed");
  require(pbc.value().pairs.size()==1,"triclinic cell-list missed or duplicated pair");
  require(pbc.value().pairs[0].distance<1.0,"minimum-image distance failed");

  auto missing=analysis::find_contacts({*raw,all,all,{},1.0,analysis::DistanceBoundary::minimum_image,true,true});
  require(!missing.has_value(),"missing unit cell accepted");
  auto bad=analysis::find_contacts({*raw,all,all,{},0.0,analysis::DistanceBoundary::raw,true,true});
  require(!bad.has_value(),"zero cutoff accepted");

  const auto topology=hbond_topology();
  const auto fallback_typing=analysis::resolve_hydrogen_bond_typing(*topology);
  require(fallback_typing.has_value() && fallback_typing.value().estimated &&
      fallback_typing.value().donors==std::vector<std::uint8_t>({1,0,0}) &&
      fallback_typing.value().acceptors==std::vector<std::uint8_t>({1,0,1}),
      "fallback H-bond typing failed");
  auto explicit_builder=model::TopologyBuilder::from(*topology);
  require(!explicit_builder.add_property("hbond_donor",model::BooleanColumn{{0,0,0}},{{},"fixture",{}}),"explicit donor property failed");
  require(!explicit_builder.add_property("hbond_acceptor",model::BooleanColumn{{0,0,1}},{{},"fixture",{}}),"explicit acceptor property failed");
  const auto explicit_topology=explicit_builder.build();
  require(explicit_topology.has_value(),"explicit typing topology failed");
  const auto explicit_typing=analysis::resolve_hydrogen_bond_typing(*explicit_topology.value());
  require(explicit_typing.has_value() && !explicit_typing.value().estimated &&
      explicit_typing.value().donor_source=="topology.property:hbond_donor" &&
      explicit_typing.value().donors==std::vector<std::uint8_t>({0,0,0}),
      "explicit H-bond typing did not override fallback");
  auto invalid_builder=model::TopologyBuilder::from(*topology);
  require(!invalid_builder.add_property("hbond_donor",std::vector<double>{1,0,0}),"invalid-kind fixture property failed");
  const auto invalid_topology=invalid_builder.build();
  require(invalid_topology.has_value() &&
      !analysis::resolve_hydrogen_bond_typing(*invalid_topology.value()).has_value(),
      "non-boolean H-bond typing property accepted");
  std::vector<std::uint8_t> roles(3,1U), donor{1,0,0}, acceptor{0,0,1};
  const auto linear=frame({{0,0,0},{1,0,0},{2.8,0,0}});
  auto hb=analysis::find_hydrogen_bonds({*linear,*topology,roles,roles,donor,acceptor,3.5,30.0,analysis::DistanceBoundary::raw,true});
  require(hb.has_value() && hb.value().bonds.size()==1,"linear H-bond not found");
  require(hb.value().bonds[0].donor.value==0 && hb.value().bonds[0].acceptor.value==2 && hb.value().bonds[0].hydrogen.value==1,"H-bond role indices failed");
  require(std::abs(hb.value().bonds[0].angle_deviation_degrees)<1e-12,"linear H-bond angle failed");
  const auto bent=frame({{0,0,0},{1,0,0},{1,2,0}});
  auto rejected=analysis::find_hydrogen_bonds({*bent,*topology,roles,roles,donor,acceptor,3.5,30.0,analysis::DistanceBoundary::raw,true});
  require(rejected.has_value() && rejected.value().bonds.empty(),"bent H-bond accepted");
  model::UnitCell box{{10,0,0},{0,10,0},{0,0,10}};
  const auto across=frame({{9.5,0,0},{9.8,0,0},{0.5,0,0}},box);
  auto periodic_hb=analysis::find_hydrogen_bonds({*across,*topology,roles,roles,donor,acceptor,2.0,30.0,analysis::DistanceBoundary::minimum_image,true});
  require(periodic_hb.has_value() && periodic_hb.value().bonds.size()==1,"periodic H-bond not found");
  require(std::abs(periodic_hb.value().bonds[0].donor_acceptor_distance-1.0)<1e-12,"periodic donor-acceptor distance failed");
  auto bad_angle=analysis::find_hydrogen_bonds({*linear,*topology,roles,roles,donor,acceptor,3.5,181.0,analysis::DistanceBoundary::raw,true});
  require(!bad_angle.has_value(),"invalid H-bond angle accepted");
  return EXIT_SUCCESS;
}
