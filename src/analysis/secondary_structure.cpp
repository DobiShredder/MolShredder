#include "molshredder/analysis/secondary_structure.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <limits>
#include <string>
#include <tuple>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::analysis {
namespace {
constexpr double kPi=3.14159265358979323846;
operation::Error invalid(std::string message) {
  return {operation::ErrorCode::invalid_argument,std::move(message),{}};
}
model::Vec3d add(model::Vec3d a,model::Vec3d b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
model::Vec3d sub(model::Vec3d a,model::Vec3d b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
model::Vec3d mul(model::Vec3d a,double s){return {a.x*s,a.y*s,a.z*s};}
double dot(model::Vec3d a,model::Vec3d b){return a.x*b.x+a.y*b.y+a.z*b.z;}
model::Vec3d cross(model::Vec3d a,model::Vec3d b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double norm(model::Vec3d a){return std::sqrt(dot(a,a));}
std::optional<model::Vec3d> normalized(model::Vec3d a){const auto n=norm(a);if(!(n>0.0)||!std::isfinite(n))return{};return mul(a,1.0/n);}
model::Vec3d coordinate(const model::CoordinateFrame& frame,std::size_t index){
  return std::visit([index](const auto& values){return model::Vec3d{
    static_cast<double>(values[index].x),static_cast<double>(values[index].y),
    static_cast<double>(values[index].z)};},frame.positions().values());
}
std::optional<double> dihedral(model::Vec3d a,model::Vec3d b,model::Vec3d c,model::Vec3d d){
  const auto b0=sub(a,b),b1=sub(c,b),b2=sub(d,c); const auto axis=normalized(b1);
  if(!axis.has_value())return{};
  const auto v=sub(b0,mul(*axis,dot(b0,*axis)));
  const auto w=sub(b2,mul(*axis,dot(b2,*axis))); const auto nv=normalized(v),nw=normalized(w);
  if(!nv.has_value()||!nw.has_value())return{};
  return std::atan2(dot(cross(*axis,*nv),*nw),dot(*nv,*nw))*180.0/kPi;
}
double circular_delta(double value,double center){
  auto delta=std::fmod(value-center+180.0,360.0); if(delta<0.0)delta+=360.0; return delta-180.0;
}
double propensity(const std::optional<double>& phi,const std::optional<double>& psi,
                  double phi_center,double psi_center,double sigma){
  if(!phi.has_value()||!psi.has_value())return 0.0;
  const auto x=circular_delta(*phi,phi_center)/sigma;
  const auto y=circular_delta(*psi,psi_center)/sigma;
  return std::exp(-0.5*(x*x+y*y));
}
bool same_chain(const model::ResidueRecord& a,const model::ResidueRecord& b){
  return a.chain_id==b.chain_id && a.segment_id==b.segment_id;
}
using CellKey=std::array<std::int64_t,3>;
double radial_hbond_energy(double distance){
  constexpr double optimum=3.0,minimum=-2.8; const auto ratio=optimum/distance;
  return minimum*(4.0*std::pow(ratio,6)-3.0*std::pow(ratio,8));
}
double outer_hbond_distance(double cutoff){
  constexpr double minimum=-2.8;
  if(cutoff<minimum)return 0.0;
  double lower=3.0,upper=6.0;
  while(radial_hbond_energy(upper)<=cutoff){
    if(upper>=std::numeric_limits<double>::max()*0.5)
      return std::numeric_limits<double>::max();
    upper*=2.0;
  }
  for(std::size_t iteration=0;iteration<80U;++iteration){
    const auto middle=(lower+upper)*0.5;
    if(radial_hbond_energy(middle)<=cutoff)lower=middle;else upper=middle;
  }
  return std::nextafter(upper,std::numeric_limits<double>::infinity());
}
std::optional<CellKey> cell_key(model::Vec3d point,double width){
  CellKey key{}; const std::array<double,3> values{point.x,point.y,point.z};
  for(std::size_t axis=0;axis<3U;++axis){
    const auto value=std::floor(values[axis]/width);
    if(value<static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
       value>static_cast<double>(std::numeric_limits<std::int64_t>::max()))return{};
    key[axis]=static_cast<std::int64_t>(value);
  }
  return key;
}
std::optional<CellKey> offset_key(const CellKey& key,std::int64_t x,
                                  std::int64_t y,std::int64_t z){
  const std::array<std::int64_t,3> offsets{x,y,z};CellKey result{};
  for(std::size_t axis=0;axis<3U;++axis){
    if((offsets[axis]>0&&key[axis]>std::numeric_limits<std::int64_t>::max()-offsets[axis])||
       (offsets[axis]<0&&key[axis]<std::numeric_limits<std::int64_t>::min()-offsets[axis]))return{};
    result[axis]=key[axis]+offsets[axis];
  }
  return result;
}
int priority(SecondaryStructureState state){
  switch(state){case SecondaryStructureState::alpha_helix:return 7;
    case SecondaryStructureState::pi_helix:return 6;
    case SecondaryStructureState::helix_310:return 5;
    case SecondaryStructureState::extended_strand:return 4;
    case SecondaryStructureState::beta_bridge:return 3;
    case SecondaryStructureState::turn:return 2;
    case SecondaryStructureState::coil:return 1;} return 0;
}
void assign(std::vector<SecondaryStructureResidue>& residues,std::size_t index,
            SecondaryStructureState state){
  if(priority(state)>priority(residues[index].state))residues[index].state=state;
}
}  // namespace

std::string_view to_string(SecondaryStructureState state) noexcept {
  switch(state){case SecondaryStructureState::coil:return "coil";
    case SecondaryStructureState::alpha_helix:return "alpha_helix";
    case SecondaryStructureState::helix_310:return "helix_310";
    case SecondaryStructureState::pi_helix:return "pi_helix";
    case SecondaryStructureState::extended_strand:return "extended_strand";
    case SecondaryStructureState::beta_bridge:return "beta_bridge";
    case SecondaryStructureState::turn:return "turn";} return "coil";
}
char stride_code(SecondaryStructureState state) noexcept {
  switch(state){case SecondaryStructureState::alpha_helix:return 'H';
    case SecondaryStructureState::helix_310:return 'G';case SecondaryStructureState::pi_helix:return 'I';
    case SecondaryStructureState::extended_strand:return 'E';case SecondaryStructureState::beta_bridge:return 'B';
    case SecondaryStructureState::turn:return 'T';case SecondaryStructureState::coil:return 'C';} return 'C';
}

operation::Result<std::vector<BackboneGeometry>> calculate_backbone_geometry(
    const model::Topology& topology,const model::CoordinateFrame& frame){
  if(frame.atom_count()!=topology.atom_count()) return operation::Result<std::vector<BackboneGeometry>>::failure(
      invalid("secondary-structure frame atom count does not match topology"));
  std::vector<BackboneGeometry> result(topology.residue_count());
  for(std::size_t r=0;r<result.size();++r)result[r].atoms.residue=model::ResidueIndex{r};
  for(std::size_t i=0;i<topology.atom_count();++i){
    if(!frame.atom_present(i))continue;
    const auto& atom=topology.atoms()[i]; auto& out=result[atom.residue.value].atoms;
    const auto set=[&](std::optional<model::AtomIndex>& target){if(!target.has_value())target=model::AtomIndex{i};};
    if(atom.name=="N")set(out.n);else if(atom.name=="CA")set(out.ca);else if(atom.name=="C")set(out.c);
    else if(atom.name=="O"||atom.name=="OXT")set(out.o);else if(atom.atomic_number==1U &&
      (atom.name=="H"||atom.name=="HN"||atom.name=="H1"))set(out.h);
  }
  for(std::size_t r=0;r<result.size();++r){
    auto& geometry=result[r]; const auto& atoms=geometry.atoms;
    if(r>0 && same_chain(topology.residues()[r-1],topology.residues()[r]) &&
       result[r-1].atoms.c && atoms.n && atoms.ca && atoms.c)
      geometry.phi_degrees=dihedral(coordinate(frame,result[r-1].atoms.c->value),
        coordinate(frame,atoms.n->value),coordinate(frame,atoms.ca->value),coordinate(frame,atoms.c->value));
    if(r+1<result.size() && same_chain(topology.residues()[r],topology.residues()[r+1]) &&
       atoms.n && atoms.ca && atoms.c && result[r+1].atoms.n)
      geometry.psi_degrees=dihedral(coordinate(frame,atoms.n->value),coordinate(frame,atoms.ca->value),
        coordinate(frame,atoms.c->value),coordinate(frame,result[r+1].atoms.n->value));
    geometry.alpha_propensity=propensity(geometry.phi_degrees,geometry.psi_degrees,-60.0,-45.0,35.0);
    geometry.beta_propensity=propensity(geometry.phi_degrees,geometry.psi_degrees,-120.0,130.0,45.0);
  }
  return operation::Result<std::vector<BackboneGeometry>>::success(std::move(result));
}

operation::Result<double> stride_method_hbond_energy_v0(
    model::Vec3d donor_n,model::Vec3d donor_h,model::Vec3d acceptor_o,model::Vec3d acceptor_c){
  const auto r=norm(sub(donor_n,acceptor_o)); if(!(r>0.0)||!std::isfinite(r))
    return operation::Result<double>::failure(invalid("hydrogen-bond geometry is degenerate"));
  const auto hd=normalized(sub(donor_n,donor_h)),ha=normalized(sub(acceptor_o,donor_h));
  const auto oc=normalized(sub(acceptor_o,acceptor_c)),oh=normalized(sub(donor_h,acceptor_o));
  if(!hd||!ha||!oc||!oh) return operation::Result<double>::failure(invalid("hydrogen-bond angle is degenerate"));
  const auto linear=std::clamp(-dot(*hd,*ha),0.0,1.0); // 1 at D-H...A = 180 degrees
  const auto acceptor=std::clamp(dot(*oc,*oh),0.0,1.0);
  const auto radial=radial_hbond_energy(r);
  const auto energy=radial*linear*linear*acceptor*acceptor;
  if(!std::isfinite(energy))return operation::Result<double>::failure(invalid("hydrogen-bond energy is non-finite"));
  return operation::Result<double>::success(energy);
}

operation::Result<SecondaryStructureResult> assign_secondary_structure(
    const model::Topology& topology,const model::CoordinateFrame& frame,
    const SecondaryStructureParameters& parameters){
  if(!std::isfinite(parameters.hydrogen_bond_energy_cutoff) ||
     parameters.hydrogen_bond_energy_cutoff>=0.0 ||
     !(parameters.minimum_helix_propensity>=0.0&&parameters.minimum_helix_propensity<=1.0) ||
     !(parameters.minimum_beta_propensity>=0.0&&parameters.minimum_beta_propensity<=1.0))
    return operation::Result<SecondaryStructureResult>::failure(invalid("secondary-structure parameters are invalid"));
  const auto geometry=calculate_backbone_geometry(topology,frame);
  if(!geometry.has_value())return operation::Result<SecondaryStructureResult>::failure(geometry.error());
  SecondaryStructureResult result; result.residues.reserve(topology.residue_count());
  for(const auto& item:geometry.value())result.residues.push_back({item.atoms.residue,
    SecondaryStructureState::coil,item.phi_degrees,item.psi_degrees,
    item.atoms.n&&item.atoms.ca&&item.atoms.c&&item.atoms.o});
  std::set<std::pair<std::size_t,std::size_t>> accepted;
  const auto coordinate_scale=frame.metadata().coordinate_unit==operation::LengthUnit::nanometer?10.0:1.0;
  const auto point=[&](std::size_t atom){return mul(coordinate(frame,atom),coordinate_scale);};
  const auto search_distance=outer_hbond_distance(parameters.hydrogen_bond_energy_cutoff);
  std::map<CellKey,std::vector<std::size_t>> acceptor_cells;
  std::vector<std::size_t> unindexed_acceptors;
  if(search_distance>0.0){
    for(std::size_t acceptor=0;acceptor<geometry.value().size();++acceptor){
      const auto& atoms=geometry.value()[acceptor].atoms;if(!atoms.o||!atoms.c)continue;
      const auto key=cell_key(point(atoms.o->value),search_distance);
      if(key)acceptor_cells[*key].push_back(acceptor);else unindexed_acceptors.push_back(acceptor);
    }
  }
  for(std::size_t donor=0;donor<geometry.value().size();++donor){
    const auto& d=geometry.value()[donor].atoms; if(!d.n||!d.ca)continue;
    std::optional<model::Vec3d> h; bool inferred=false;
    if(d.h)h=point(d.h->value);
    else if(donor>0 && same_chain(topology.residues()[donor-1],topology.residues()[donor]) &&
            geometry.value()[donor-1].atoms.c){
      const auto n=point(d.n->value),ca=point(d.ca->value);
      const auto previous_c=point(geometry.value()[donor-1].atoms.c->value);
      const auto away_c=normalized(sub(n,previous_c)),away_ca=normalized(sub(n,ca));
      if(away_c&&away_ca){const auto direction=normalized(add(*away_c,*away_ca));if(direction){h=add(n,*direction);inferred=true;}}
    }
    if(!h)continue;
    std::vector<std::size_t> candidates=unindexed_acceptors;
    if(search_distance>0.0){
      const auto donor_key=cell_key(point(d.n->value),search_distance);
      if(donor_key){
        for(std::int64_t x=-1;x<=1;++x)for(std::int64_t y=-1;y<=1;++y)
          for(std::int64_t z=-1;z<=1;++z){
            const auto key=offset_key(*donor_key,x,y,z);if(!key)continue;
            const auto found=acceptor_cells.find(*key);if(found!=acceptor_cells.end())
              candidates.insert(candidates.end(),found->second.begin(),found->second.end());
          }
      }else{
        for(const auto& [key,values]:acceptor_cells){static_cast<void>(key);
          candidates.insert(candidates.end(),values.begin(),values.end());}
      }
    }
    for(const auto acceptor:candidates){
      if(donor==acceptor)continue;
      const auto& a=geometry.value()[acceptor].atoms; if(!a.o||!a.c)continue;
      if(norm(sub(point(d.n->value),point(a.o->value)))>search_distance)continue;
      const auto energy=stride_method_hbond_energy_v0(point(d.n->value),*h,
        point(a.o->value),point(a.c->value));
      if(energy.has_value()&&energy.value()<=parameters.hydrogen_bond_energy_cutoff){
        accepted.insert({donor,acceptor}); result.hydrogen_bonds.push_back({model::ResidueIndex{donor},
          model::ResidueIndex{acceptor},energy.value(),inferred});
      }
    }
  }
  for(const auto span:std::array<std::size_t,3>{4,3,5}){
    const auto state=span==4?SecondaryStructureState::alpha_helix:
      (span==3?SecondaryStructureState::helix_310:SecondaryStructureState::pi_helix);
    for(std::size_t a=0;a+span+1<geometry.value().size();++a){
      bool continuous=true;
      for(std::size_t r=a;r<a+span+1;++r)
        continuous=continuous&&same_chain(topology.residues()[r],topology.residues()[r+1]);
      if(!continuous)continue;
      if(!accepted.contains({a+span,a})||!accepted.contains({a+span+1,a+1}))continue;
      double sum=0.0;std::size_t count=0;
      for(std::size_t r=a+1;r<=a+span;++r){sum+=geometry.value()[r].alpha_propensity;++count;}
      if(sum/static_cast<double>(count)<parameters.minimum_helix_propensity)continue;
      for(std::size_t r=a+1;r<=a+span;++r)assign(result.residues,r,state);
    }
  }
  std::set<std::pair<std::size_t,std::size_t>> bridges;
  const auto hb=[&](std::size_t d,std::size_t a){return accepted.contains({d,a});};
  const auto count=geometry.value().size();
  std::set<std::pair<std::size_t,std::size_t>> bridge_candidates;
  for(const auto& [donor,acceptor]:accepted){
    for(int donor_offset=-1;donor_offset<=1;++donor_offset)
      for(int acceptor_offset=-1;acceptor_offset<=1;++acceptor_offset){
        const auto first=static_cast<long long>(donor)+donor_offset;
        const auto second=static_cast<long long>(acceptor)+acceptor_offset;
        if(first<0||second<0||first>=static_cast<long long>(count)||
           second>=static_cast<long long>(count))continue;
        const auto i=static_cast<std::size_t>(std::min(first,second));
        const auto j=static_cast<std::size_t>(std::max(first,second));
        if(j>=i+3U)bridge_candidates.insert({i,j});
      }
  }
  for(const auto& [i,j]:bridge_candidates){
    if(geometry.value()[i].beta_propensity<parameters.minimum_beta_propensity ||
       geometry.value()[j].beta_propensity<parameters.minimum_beta_propensity)continue;
    const bool anti=(hb(i,j)&&hb(j,i)) ||
      (i+1<count&&j>0&&j+1<count&&hb(i+1,j-1)&&hb(j+1,i));
    const bool parallel=(j>0&&j+1<count&&hb(i,j-1)&&hb(j+1,i)) ||
      (i>0&&i+1<count&&hb(j,i-1)&&hb(i+1,j));
    if(anti||parallel)bridges.insert({i,j});
  }
  for(const auto& bridge:bridges){
    bool extended=false;
    for(int first_offset=-1;first_offset<=1&&!extended;++first_offset)
      for(int second_offset=-1;second_offset<=1;++second_offset){
        if(first_offset==0&&second_offset==0)continue;
        const auto first=static_cast<long long>(bridge.first)+first_offset;
        const auto second=static_cast<long long>(bridge.second)+second_offset;
        if(first>=0&&second>=0&&bridges.contains({static_cast<std::size_t>(first),
          static_cast<std::size_t>(second)})){extended=true;break;}
      }
    assign(result.residues,bridge.first,extended?SecondaryStructureState::extended_strand:SecondaryStructureState::beta_bridge);
    assign(result.residues,bridge.second,extended?SecondaryStructureState::extended_strand:SecondaryStructureState::beta_bridge);
  }
  for(const auto& [donor,acceptor]:accepted){
    if(donor<=acceptor)continue;
    const auto span=donor-acceptor;if(span<3||span>5)continue;
    for(std::size_t r=acceptor+1;r<donor;++r)assign(result.residues,r,SecondaryStructureState::turn);
  }
  std::sort(result.hydrogen_bonds.begin(),result.hydrogen_bonds.end(),[](const auto& a,const auto& b){
    return std::tie(a.donor.value,a.acceptor.value)<std::tie(b.donor.value,b.acceptor.value);});
  return operation::Result<SecondaryStructureResult>::success(std::move(result));
}
}  // namespace molshredder::analysis
