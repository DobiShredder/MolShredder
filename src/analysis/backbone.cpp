#include "molshredder/analysis/backbone.hpp"

#include <array>
#include <cmath>
#include <numbers>
#include <string_view>
#include <variant>

#include "molshredder/analysis/basic.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/selection/evaluator.hpp"

namespace molshredder::analysis {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion), {}};
}

struct Backbone {
  std::optional<std::size_t> n;
  std::optional<std::size_t> ca;
  std::optional<std::size_t> c;
  bool complete() const { return n && ca && c; }
};

double angstrom_per_unit(operation::LengthUnit unit) {
  return unit == operation::LengthUnit::nanometer ? 10.0 : 1.0;
}

}  // namespace

operation::Result<std::vector<BackboneAngles>>
calculate_backbone_angles(const BackboneRequest& request) {
  using R = operation::Result<std::vector<BackboneAngles>>;
  if (request.topology == nullptr || request.frame == nullptr)
    return R::failure(invalid("backbone angles need a topology and a frame"));
  const auto& topology = *request.topology;
  const auto& frame = *request.frame;
  if (frame.atom_count() != topology.atom_count())
    return R::failure(invalid("frame atom count does not match the topology"));
  if (!request.selected.empty() &&
      !selection::mask_is_valid(request.selected, topology.atom_count()))
    return R::failure(invalid("backbone selection mask is invalid"));
  if (!std::isfinite(request.link_distance_angstrom) ||
      request.link_distance_angstrom <= 0.0)
    return R::failure(invalid("link distance must be positive"));

  std::vector<Backbone> backbone(topology.residues().size());
  std::vector<bool> chosen(topology.residues().size(), request.selected.empty());
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    const auto& atom = topology.atoms()[index];
    auto& entry = backbone[atom.residue.value];
    if (!request.selected.empty() && request.selected[index] != 0U &&
        (atom.name == "N" || atom.name == "CA" || atom.name == "C"))
      chosen[atom.residue.value] = true;
    if (!frame.atom_present(index)) continue;
    // The first alternate location of a backbone atom is the one that is used.
    if (atom.name == "N" && !entry.n) entry.n = index;
    else if (atom.name == "CA" && !entry.ca) entry.ca = index;
    else if (atom.name == "C" && !entry.c) entry.c = index;
  }

  const auto scale = angstrom_per_unit(frame.metadata().coordinate_unit);
  const auto distance = [&](std::size_t first, std::size_t second) {
    const auto result =
        atom_distance(frame, model::AtomIndex{first}, model::AtomIndex{second});
    return result.has_value() ? result.value().distance * scale
                              : std::numeric_limits<double>::infinity();
  };
  const auto dihedral = [&](std::size_t a, std::size_t b, std::size_t c,
                            std::size_t d) -> std::optional<double> {
    const auto result = atom_dihedral(frame, model::AtomIndex{a},
                                      model::AtomIndex{b}, model::AtomIndex{c},
                                      model::AtomIndex{d});
    if (!result.has_value()) return std::nullopt;
    return result.value().angle_degrees;
  };
  const auto linked = [&](std::size_t previous, std::size_t next) {
    return backbone[previous].complete() && backbone[next].complete() &&
           distance(*backbone[previous].c, *backbone[next].n) <
               request.link_distance_angstrom;
  };

  std::vector<BackboneAngles> result;
  for (std::size_t residue = 0; residue < backbone.size(); ++residue) {
    if (!chosen[residue] || !backbone[residue].complete()) continue;
    const auto& record = topology.residues()[residue];
    BackboneAngles angles;
    angles.residue = model::ResidueIndex{residue};
    angles.chain = record.chain_id;
    angles.residue_name = record.name;
    angles.sequence_number = record.sequence_number;
    angles.insertion_code = record.insertion_code;
    if (residue > 0U && linked(residue - 1U, residue))
      angles.phi_degrees = dihedral(*backbone[residue - 1U].c, *backbone[residue].n,
                                    *backbone[residue].ca, *backbone[residue].c);
    if (residue + 1U < backbone.size() && linked(residue, residue + 1U))
      angles.psi_degrees = dihedral(*backbone[residue].n, *backbone[residue].ca,
                                    *backbone[residue].c, *backbone[residue + 1U].n);
    result.push_back(std::move(angles));
  }
  return R::success(std::move(result));
}

}  // namespace molshredder::analysis
