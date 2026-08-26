#include "molshredder/analysis/contacts.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>

#include "molshredder/operation/error.hpp"
#include "molshredder/trajectory/pbc.hpp"

namespace molshredder::analysis {
namespace {
using Key = std::array<std::int64_t, 3>;

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return {operation::ErrorCode::invalid_argument, std::move(message),
          std::move(suggestion)};
}

std::vector<model::Vec3d> positions(const model::CoordinateFrame& frame) {
  return std::visit([](const auto& values) {
    std::vector<model::Vec3d> result;
    result.reserve(values.size());
    for (const auto& p : values) result.push_back(
        {static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z)});
    return result;
  }, frame.positions().values());
}

double norm(model::Vec3d p) { return std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z); }
model::Vec3d subtract(model::Vec3d a, model::Vec3d b) {
  return {a.x-b.x, a.y-b.y, a.z-b.z};
}
std::int64_t modulo(std::int64_t value, std::int64_t divisor) {
  const auto result = value % divisor;
  return result < 0 ? result + divisor : result;
}
double wrapped(double value) { return value - std::floor(value); }

bool bonded(std::span<const model::Bond> bonds, std::size_t a, std::size_t b) {
  return std::any_of(bonds.begin(), bonds.end(), [a,b](const auto& bond) {
    return (bond.first.value == a && bond.second.value == b) ||
           (bond.first.value == b && bond.second.value == a);
  });
}
}  // namespace

operation::Result<ContactResult> find_contacts(const ContactRequest& request) {
  const auto atom_count = request.frame.atom_count();
  if (request.first.size() != atom_count || request.second.size() != atom_count)
    return operation::Result<ContactResult>::failure(invalid(
        "contact selection masks must match the coordinate atom count"));
  if (!(request.cutoff > 0.0) || !std::isfinite(request.cutoff))
    return operation::Result<ContactResult>::failure(invalid(
        "contact cutoff must be finite and positive"));
  const auto xyz = positions(request.frame);
  std::vector<model::Vec3d> indexed = xyz;
  Key counts{1,1,1};
  Key radius{1,1,1};
  const model::UnitCell* cell = nullptr;
  if (request.boundary == DistanceBoundary::minimum_image) {
    if (!request.frame.metadata().unit_cell.has_value())
      return operation::Result<ContactResult>::failure(invalid(
          "minimum-image contacts require unit-cell metadata"));
    cell = &*request.frame.metadata().unit_cell;
    for (std::size_t i=0; i<atom_count; ++i) {
      const auto fractional = trajectory::cartesian_to_fractional(*cell, xyz[i]);
      if (!fractional.has_value())
        return operation::Result<ContactResult>::failure(fractional.error());
      indexed[i] = {wrapped(fractional.value().x), wrapped(fractional.value().y),
                    wrapped(fractional.value().z)};
    }
    const auto volume = cell->signed_volume();
    const auto cross = [](model::Vec3d a, model::Vec3d b) {
      return model::Vec3d{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
    };
    const std::array<double,3> reciprocal_norms{
      norm(cross(cell->b,cell->c))/volume,
      norm(cross(cell->c,cell->a))/volume,
      norm(cross(cell->a,cell->b))/volume};
    for (std::size_t axis=0; axis<3; ++axis) {
      const auto extent = request.cutoff * reciprocal_norms[axis];
      constexpr std::int64_t maximum_bins_per_axis = 1'000'000;
      counts[axis] = extent >= 1.0 ? 1 :
          (extent <= 1.0 / static_cast<double>(maximum_bins_per_axis)
               ? maximum_bins_per_axis
               : std::max<std::int64_t>(1,
                     static_cast<std::int64_t>(std::floor(1.0 / extent))));
      radius[axis] = std::min(counts[axis],
          static_cast<std::int64_t>(std::ceil(extent * static_cast<double>(counts[axis]))) + 1);
    }
  }

  const auto key_for = [&](model::Vec3d p) {
    if (cell != nullptr) return Key{
      std::min(counts[0]-1, static_cast<std::int64_t>(std::floor(
                               p.x * static_cast<double>(counts[0])))),
      std::min(counts[1]-1, static_cast<std::int64_t>(std::floor(
                               p.y * static_cast<double>(counts[1])))),
      std::min(counts[2]-1, static_cast<std::int64_t>(std::floor(
                               p.z * static_cast<double>(counts[2]))))};
    return Key{static_cast<std::int64_t>(std::floor(p.x/request.cutoff)),
               static_cast<std::int64_t>(std::floor(p.y/request.cutoff)),
               static_cast<std::int64_t>(std::floor(p.z/request.cutoff))};
  };
  std::map<Key,std::vector<std::size_t>> bins;
  for (std::size_t i=0; i<atom_count; ++i)
    if (request.second[i] && request.frame.atom_present(i)) bins[key_for(indexed[i])].push_back(i);

  ContactResult result{request.frame.metadata().coordinate_unit,{}};
  std::set<std::pair<std::size_t,std::size_t>> emitted;
  const auto cutoff2 = request.cutoff * request.cutoff;
  for (std::size_t i=0; i<atom_count; ++i) {
    if (!request.first[i] || !request.frame.atom_present(i)) continue;
    const auto base = key_for(indexed[i]);
    std::set<Key> visited_bins;
    for (auto dx=-radius[0]; dx<=radius[0]; ++dx)
      for (auto dy=-radius[1]; dy<=radius[1]; ++dy)
        for (auto dz=-radius[2]; dz<=radius[2]; ++dz) {
          Key key{base[0]+dx,base[1]+dy,base[2]+dz};
          if (cell != nullptr) for (std::size_t a=0;a<3;++a) key[a]=modulo(key[a],counts[a]);
          if (!visited_bins.insert(key).second) continue;
          const auto found=bins.find(key); if(found==bins.end()) continue;
          for (const auto j:found->second) {
            if (i==j) continue;
            auto pair=std::minmax(i,j);
            if (request.same_selection && i>j) continue;
            if (!emitted.insert(pair).second && request.same_selection) continue;
            if (request.exclude_bonded && bonded(request.bonds,i,j)) continue;
            auto displacement=subtract(xyz[j],xyz[i]);
            if(cell!=nullptr) {
              const auto image=trajectory::minimum_image(*cell,displacement);
              if(!image.has_value()) return operation::Result<ContactResult>::failure(image.error());
              displacement=image.value().displacement;
            }
            const auto d2=displacement.x*displacement.x+displacement.y*displacement.y+displacement.z*displacement.z;
            if(d2<=cutoff2) result.pairs.push_back({model::AtomIndex{i},model::AtomIndex{j},displacement,std::sqrt(d2)});
          }
        }
  }
  std::sort(result.pairs.begin(),result.pairs.end(),[](const auto& a,const auto& b){
    return std::tie(a.first.value,a.second.value)<std::tie(b.first.value,b.second.value);
  });
  return operation::Result<ContactResult>::success(std::move(result));
}

operation::Result<HydrogenBondTyping> resolve_hydrogen_bond_typing(
    const model::Topology& topology) {
  HydrogenBondTyping result;
  const auto count=topology.atom_count();
  result.donors.assign(count,0U); result.acceptors.assign(count,0U);
  const auto explicit_column = [&](std::string_view name,
                                   std::vector<std::uint8_t>& output)
      -> operation::Result<bool> {
    const auto* property=topology.properties().find(name);
    if(property==nullptr) return operation::Result<bool>::success(false);
    const auto* boolean=std::get_if<model::BooleanColumn>(&property->values);
    if(boolean==nullptr) return operation::Result<bool>::failure(invalid(
        std::string{name}+" atom property must be boolean"));
    output=boolean->values;
    return operation::Result<bool>::success(true);
  };
  const auto explicit_donors=explicit_column("hbond_donor",result.donors);
  if(!explicit_donors.has_value())
    return operation::Result<HydrogenBondTyping>::failure(explicit_donors.error());
  const auto explicit_acceptors=explicit_column("hbond_acceptor",result.acceptors);
  if(!explicit_acceptors.has_value())
    return operation::Result<HydrogenBondTyping>::failure(explicit_acceptors.error());
  std::vector<std::uint8_t> bonded_hydrogen(count,0U);
  for(const auto& bond:topology.bonds()) {
    if(topology.atoms()[bond.first.value].atomic_number==1U) bonded_hydrogen[bond.second.value]=1U;
    if(topology.atoms()[bond.second.value].atomic_number==1U) bonded_hydrogen[bond.first.value]=1U;
  }
  const auto polar=[](std::uint8_t z){ return z==7U || z==8U || z==16U; };
  if(!explicit_donors.value())
    for(std::size_t i=0;i<count;++i)
      result.donors[i]=static_cast<std::uint8_t>(polar(topology.atoms()[i].atomic_number) && bonded_hydrogen[i]);
  if(!explicit_acceptors.value())
    for(std::size_t i=0;i<count;++i)
      result.acceptors[i]=static_cast<std::uint8_t>(polar(topology.atoms()[i].atomic_number) && topology.atoms()[i].formal_charge<=0);
  result.donor_source=explicit_donors.value() ? "topology.property:hbond_donor" : "element-bond-v1";
  result.acceptor_source=explicit_acceptors.value() ? "topology.property:hbond_acceptor" : "element-charge-v1";
  result.estimated=!explicit_donors.value() || !explicit_acceptors.value();
  return operation::Result<HydrogenBondTyping>::success(std::move(result));
}

operation::Result<HydrogenBondResult> find_hydrogen_bonds(
    const HydrogenBondRequest& request) {
  const auto atom_count = request.frame.atom_count();
  const auto valid_mask = [atom_count](std::span<const std::uint8_t> mask) {
    return mask.size() == atom_count;
  };
  if (request.topology.atom_count() != atom_count ||
      !valid_mask(request.donor_selection) ||
      !valid_mask(request.acceptor_selection) ||
      !valid_mask(request.donor_capable) ||
      !valid_mask(request.acceptor_capable))
    return operation::Result<HydrogenBondResult>::failure(invalid(
        "hydrogen-bond topology and masks must match the coordinate atom count"));
  if (!(request.maximum_angle_deviation_degrees >= 0.0) ||
      request.maximum_angle_deviation_degrees > 180.0 ||
      !std::isfinite(request.maximum_angle_deviation_degrees))
    return operation::Result<HydrogenBondResult>::failure(invalid(
        "hydrogen-bond angle deviation must be finite and within [0, 180] degrees"));

  std::vector<std::uint8_t> donors(atom_count, 0U), acceptors(atom_count, 0U);
  for (std::size_t i=0; i<atom_count; ++i) {
    const auto non_hydrogen = request.topology.atoms()[i].atomic_number != 1U;
    donors[i] = static_cast<std::uint8_t>(non_hydrogen && request.donor_selection[i] && request.donor_capable[i]);
    acceptors[i] = static_cast<std::uint8_t>(non_hydrogen && request.acceptor_selection[i] && request.acceptor_capable[i]);
  }
  std::vector<std::uint8_t> candidate_first=donors, candidate_second=acceptors;
  if (request.same_selection) {
    for (std::size_t i=0; i<atom_count; ++i)
      candidate_first[i]=candidate_second[i]=static_cast<std::uint8_t>(donors[i] || acceptors[i]);
  }
  const auto candidates = find_contacts(
      {request.frame, candidate_first, candidate_second, {}, request.distance_cutoff,
       request.boundary, request.same_selection, false});
  if (!candidates.has_value())
    return operation::Result<HydrogenBondResult>::failure(candidates.error());

  const auto xyz = positions(request.frame);
  const model::UnitCell* cell = request.boundary == DistanceBoundary::minimum_image
      ? &*request.frame.metadata().unit_cell : nullptr;
  const auto displacement = [&](std::size_t from, std::size_t to)
      -> operation::Result<model::Vec3d> {
    auto value = subtract(xyz[to], xyz[from]);
    if (cell == nullptr) return operation::Result<model::Vec3d>::success(value);
    const auto image = trajectory::minimum_image(*cell, value);
    if (!image.has_value()) return operation::Result<model::Vec3d>::failure(image.error());
    return operation::Result<model::Vec3d>::success(image.value().displacement);
  };
  std::vector<std::vector<std::size_t>> hydrogens(atom_count);
  for (const auto& bond : request.topology.bonds()) {
    const auto first = bond.first.value, second = bond.second.value;
    if (request.topology.atoms()[first].atomic_number == 1U)
      hydrogens[second].push_back(first);
    if (request.topology.atoms()[second].atomic_number == 1U)
      hydrogens[first].push_back(second);
  }
  HydrogenBondResult result{request.frame.metadata().coordinate_unit,{}};
  const auto evaluate = [&](std::size_t donor, std::size_t acceptor)
      -> std::optional<operation::Error> {
    if (!donors[donor] || !acceptors[acceptor]) return std::nullopt;
    for (const auto hydrogen : hydrogens[donor]) {
      if (!request.frame.atom_present(hydrogen)) continue;
      const auto h_to_d = displacement(hydrogen, donor);
      const auto h_to_a = displacement(hydrogen, acceptor);
      if (!h_to_d.has_value()) return h_to_d.error();
      if (!h_to_a.has_value()) return h_to_a.error();
      const auto nd=norm(h_to_d.value()), na=norm(h_to_a.value());
      if (!(nd>0.0) || !(na>0.0)) continue;
      auto cosine=(h_to_d.value().x*h_to_a.value().x+
                   h_to_d.value().y*h_to_a.value().y+
                   h_to_d.value().z*h_to_a.value().z)/(nd*na);
      cosine=std::clamp(cosine,-1.0,1.0);
      constexpr double radians_to_degrees=57.2957795130823208768;
      const auto angle=std::acos(cosine)*radians_to_degrees;
      const auto deviation=180.0-angle;
      if (deviation <= request.maximum_angle_deviation_degrees)
        result.bonds.push_back({model::AtomIndex{donor},model::AtomIndex{acceptor},
                                model::AtomIndex{hydrogen},
                                norm(subtract(xyz[acceptor],xyz[donor])),deviation});
    }
    return std::nullopt;
  };
  for (const auto& pair : candidates.value().pairs) {
    if (const auto failure=evaluate(pair.first.value,pair.second.value); failure.has_value())
      return operation::Result<HydrogenBondResult>::failure(*failure);
    if (request.same_selection)
      if (const auto failure=evaluate(pair.second.value,pair.first.value); failure.has_value())
        return operation::Result<HydrogenBondResult>::failure(*failure);
  }
  for (auto& bond : result.bonds) {
    const auto da=displacement(bond.donor.value,bond.acceptor.value);
    if (!da.has_value()) return operation::Result<HydrogenBondResult>::failure(da.error());
    bond.donor_acceptor_distance=norm(da.value());
  }
  std::sort(result.bonds.begin(),result.bonds.end(),[](const auto& a,const auto& b){
    return std::tie(a.donor.value,a.acceptor.value,a.hydrogen.value) <
           std::tie(b.donor.value,b.acceptor.value,b.hydrogen.value);
  });
  return operation::Result<HydrogenBondResult>::success(std::move(result));
}
}  // namespace molshredder::analysis
