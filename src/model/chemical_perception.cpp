#include "molshredder/model/chemical_perception.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <utility>

namespace molshredder::model {
namespace {

std::string uppercase(std::string_view value) {
  std::string result{value};
  std::ranges::transform(result, result.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return result;
}

std::pair<ResidueKind, PolymerType> classify_residue(std::string_view name) {
  static const std::set<std::string, std::less<>> amino{
      "ALA", "ARG", "ASN", "ASP", "CYS", "GLN", "GLU", "GLY", "HIS",
      "ILE", "LEU", "LYS", "MET", "PHE", "PRO", "SER", "THR", "TRP",
      "TYR", "VAL", "SEC", "PYL", "ASX", "GLX"};
  static const std::set<std::string, std::less<>> dna{
      "DA", "DC", "DG", "DT", "DI", "ADE", "CYT", "GUA", "THY"};
  static const std::set<std::string, std::less<>> rna{
      "A", "C", "G", "U", "I", "URA"};
  static const std::set<std::string, std::less<>> solvent{
      "HOH", "WAT", "H2O", "DOD", "SOL", "TIP3", "TIP4", "SPC"};
  static const std::set<std::string, std::less<>> ions{
      "NA", "K", "CL", "CA", "MG", "ZN", "FE", "MN", "CU", "CO",
      "NI", "BR", "IOD"};
  static const std::set<std::string, std::less<>> carbohydrates{
      "NAG", "NDG", "BMA", "MAN", "GAL", "GLC", "FUC", "SIA"};
  const auto normalized = uppercase(name);
  if (amino.contains(normalized))
    return {ResidueKind::amino_acid, PolymerType::protein};
  if (dna.contains(normalized))
    return {ResidueKind::nucleic_acid, PolymerType::dna};
  if (rna.contains(normalized))
    return {ResidueKind::nucleic_acid, PolymerType::rna};
  if (solvent.contains(normalized))
    return {ResidueKind::solvent, PolymerType::none};
  if (ions.contains(normalized))
    return {ResidueKind::ion, PolymerType::none};
  if (carbohydrates.contains(normalized))
    return {ResidueKind::carbohydrate, PolymerType::carbohydrate};
  return {ResidueKind::ligand, PolymerType::none};
}

double covalent_radius(std::uint8_t atomic_number) {
  switch (atomic_number) {
  case 1: return 0.31;
  case 5: return 0.84;
  case 6: return 0.76;
  case 7: return 0.71;
  case 8: return 0.66;
  case 9: return 0.57;
  case 14: return 1.11;
  case 15: return 1.07;
  case 16: return 1.05;
  case 17: return 1.02;
  case 35: return 1.20;
  case 53: return 1.39;
  default: return 0.0;
  }
}

double target_valence(std::uint8_t atomic_number) {
  switch (atomic_number) {
  case 1:
  case 9:
  case 17:
  case 35:
  case 53: return 1.0;
  case 5: return 3.0;
  case 6:
  case 14: return 4.0;
  case 7:
  case 15: return 3.0;
  case 8:
  case 16: return 2.0;
  default: return 0.0;
  }
}

double order_value(BondOrder order) {
  switch (order) {
  case BondOrder::single: return 1.0;
  case BondOrder::double_bond: return 2.0;
  case BondOrder::triple: return 3.0;
  case BondOrder::aromatic: return 1.5;
  case BondOrder::amide: return 1.0;
  case BondOrder::zero: return 0.0;
  case BondOrder::unknown:
  case BondOrder::query: return 0.0;
  }
  return 0.0;
}

std::vector<Vec3d> positions_angstrom(const CoordinateFrame &frame) {
  const auto scale = frame.metadata().coordinate_unit ==
                             operation::LengthUnit::nanometer
                         ? 10.0
                         : 1.0;
  return std::visit(
      [scale](const auto &values) {
        std::vector<Vec3d> result;
        result.reserve(values.size());
        for (const auto &value : values) {
          result.push_back({static_cast<double>(value.x) * scale,
                            static_cast<double>(value.y) * scale,
                            static_cast<double>(value.z) * scale});
        }
        return result;
      },
      frame.positions().values());
}

bool path_exists_without_edge(const std::vector<std::vector<std::size_t>> &graph,
                              std::size_t start, std::size_t goal,
                              std::pair<std::size_t, std::size_t> removed) {
  std::vector<std::uint8_t> visited(graph.size(), 0U);
  std::queue<std::size_t> pending;
  pending.push(start);
  visited[start] = 1U;
  while (!pending.empty()) {
    const auto current = pending.front();
    pending.pop();
    for (const auto next : graph[current]) {
      if ((current == removed.first && next == removed.second) ||
          (current == removed.second && next == removed.first))
        continue;
      if (next == goal)
        return true;
      if (visited[next] == 0U) {
        visited[next] = 1U;
        pending.push(next);
      }
    }
  }
  return false;
}

void collect_cycles(const std::vector<std::vector<std::size_t>> &graph,
                    std::size_t start, std::size_t current,
                    std::vector<std::size_t> &path,
                    std::vector<std::uint8_t> &visited,
                    std::vector<std::vector<std::size_t>> &cycles) {
  if (path.size() > 6U)
    return;
  for (const auto next : graph[current]) {
    if (next == start && path.size() >= 5U) {
      if (path[1] < path.back())
        cycles.push_back(path);
      continue;
    }
    if (next < start || visited[next] != 0U || path.size() == 6U)
      continue;
    visited[next] = 1U;
    path.push_back(next);
    collect_cycles(graph, start, next, path, visited, cycles);
    path.pop_back();
    visited[next] = 0U;
  }
}

std::vector<std::vector<std::size_t>> five_and_six_member_cycles(
    const std::vector<std::vector<std::size_t>> &graph) {
  std::vector<std::vector<std::size_t>> cycles;
  std::vector<std::uint8_t> visited(graph.size(), 0U);
  for (std::size_t start = 0; start < graph.size(); ++start) {
    std::vector<std::size_t> path{start};
    visited[start] = 1U;
    collect_cycles(graph, start, start, path, visited, cycles);
    visited[start] = 0U;
  }
  return cycles;
}

double distance(const Vec3d &first, const Vec3d &second) {
  const auto dx = first.x - second.x;
  const auto dy = first.y - second.y;
  const auto dz = first.z - second.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool planar_cycle(const std::vector<std::size_t> &cycle,
                  const std::vector<Vec3d> &coordinates) {
  const auto &origin = coordinates[cycle[0]];
  const auto first = Vec3d{coordinates[cycle[1]].x - origin.x,
                           coordinates[cycle[1]].y - origin.y,
                           coordinates[cycle[1]].z - origin.z};
  const auto second = Vec3d{coordinates[cycle[2]].x - origin.x,
                            coordinates[cycle[2]].y - origin.y,
                            coordinates[cycle[2]].z - origin.z};
  const auto normal = Vec3d{first.y * second.z - first.z * second.y,
                            first.z * second.x - first.x * second.z,
                            first.x * second.y - first.y * second.x};
  const auto norm =
      std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
  if (norm < 1.0e-9)
    return false;
  return std::ranges::all_of(cycle, [&](std::size_t index) {
    const auto offset = Vec3d{coordinates[index].x - origin.x,
                              coordinates[index].y - origin.y,
                              coordinates[index].z - origin.z};
    return std::abs(offset.x * normal.x + offset.y * normal.y +
                    offset.z * normal.z) /
               norm <=
           0.15;
  });
}

}  // namespace

operation::Result<ChemicalPerceptionReport> perceive_chemistry(
    const Topology &topology, const CoordinateFrame &frame,
    ChemicalPerceptionOptions options, operation::TaskContext *context) {
  if (frame.atom_count() != topology.atom_count()) {
    return operation::Result<ChemicalPerceptionReport>::failure(
        {operation::ErrorCode::invalid_argument,
         "chemical perception topology/frame atom counts differ", {}});
  }
  if (!std::isfinite(options.covalent_radius_scale) ||
      options.covalent_radius_scale < 1.0 ||
      options.covalent_radius_scale > 1.5) {
    return operation::Result<ChemicalPerceptionReport>::failure(
        {operation::ErrorCode::invalid_argument,
         "covalent radius scale must be finite and in [1.0, 1.5]", {}});
  }
  ChemicalPerceptionReport report;
  report.topology_version = topology.version();
  report.assumptions = {
      "coordinates are converted to angstrom before distance tests",
      "only supported main-group covalent radii participate in inference",
      "new connectivity is proposed as inferred single bonds",
      "polymer membership requires two same-family residues in one chain",
      "explicit and user annotations are immutable inputs"};
  report.residues.reserve(topology.residue_count());
  for (std::size_t index = 0; index < topology.residue_count(); ++index) {
    const auto &residue = topology.residues()[index];
    if (residue.chemical_origin != ChemicalAnnotationOrigin::unspecified) {
      report.residues.push_back(
          {{index}, residue.kind, residue.polymer_type, false});
      continue;
    }
    const auto classified = classify_residue(residue.name);
    report.residues.push_back(
        {{index}, classified.first, classified.second, true});
  }
  // Component names identify residue kind, but polymer membership additionally
  // requires at least two same-family residues in one chain.
  for (auto &assessment : report.residues) {
    if (!assessment.proposed ||
        assessment.polymer_type == PolymerType::none)
      continue;
    const auto &source = topology.residues()[assessment.residue.value];
    const auto same_family = static_cast<std::size_t>(std::ranges::count_if(
        report.residues, [&](const ResidueChemicalAssessment &candidate) {
          return candidate.polymer_type == assessment.polymer_type &&
                 topology.residues()[candidate.residue.value].chain_id ==
                     source.chain_id;
        }));
    if (same_family < 2U)
      assessment.polymer_type = PolymerType::none;
  }
  const auto coordinates = positions_angstrom(frame);
  std::set<std::pair<std::size_t, std::size_t>> edges;
  for (const auto &bond : topology.bonds())
    edges.emplace(bond.first.value, bond.second.value);

  if (options.infer_connectivity) {
    for (std::size_t first = 0; first < topology.atom_count(); ++first) {
      if (context != nullptr && context->cancellation.is_cancelled()) {
        return operation::Result<ChemicalPerceptionReport>::failure(
            {operation::ErrorCode::cancelled,
             "chemical perception was cancelled", {}});
      }
      if (context != nullptr && context->report_progress &&
          topology.atom_count() > 0U) {
        context->report_progress(
            {static_cast<double>(first) /
                 static_cast<double>(topology.atom_count()),
             "chemical-connectivity-perception"});
      }
      if (!frame.atom_present(first))
        continue;
      const auto first_radius =
          covalent_radius(topology.atoms()[first].atomic_number);
      if (first_radius == 0.0)
        continue;
      for (std::size_t second = first + 1U; second < topology.atom_count();
           ++second) {
        if (!frame.atom_present(second) || edges.contains({first, second}))
          continue;
        if (report.evaluated_pair_count == options.pair_budget) {
          return operation::Result<ChemicalPerceptionReport>::failure(
              {operation::ErrorCode::resource_exhausted,
               "chemical perception pair budget exceeded",
               "increase the explicit pair budget or reduce the input",
               {{"pair_budget", std::to_string(options.pair_budget)}}});
        }
        ++report.evaluated_pair_count;
        const auto second_radius =
            covalent_radius(topology.atoms()[second].atomic_number);
        if (second_radius == 0.0)
          continue;
        const auto dx = coordinates[first].x - coordinates[second].x;
        const auto dy = coordinates[first].y - coordinates[second].y;
        const auto dz = coordinates[first].z - coordinates[second].z;
        const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const auto cutoff =
            options.covalent_radius_scale * (first_radius + second_radius);
        if (distance >= 0.4 && distance <= cutoff) {
          report.proposed_bonds.push_back(
              {{first}, {second}, BondOrder::single, distance});
          edges.emplace(first, second);
        }
      }
    }
  }

  std::vector<std::vector<std::size_t>> graph(topology.atom_count());
  for (const auto edge : edges) {
    graph[edge.first].push_back(edge.second);
    graph[edge.second].push_back(edge.first);
  }

  std::map<std::pair<std::size_t, std::size_t>, std::size_t>
      existing_bond_indices;
  for (std::size_t index = 0; index < topology.bonds().size(); ++index) {
    const auto &bond = topology.bonds()[index];
    existing_bond_indices.emplace(
        std::pair{bond.first.value, bond.second.value}, index);
  }
  std::map<std::pair<std::size_t, std::size_t>, std::size_t>
      proposed_bond_indices;
  for (std::size_t index = 0; index < report.proposed_bonds.size(); ++index) {
    const auto &bond = report.proposed_bonds[index];
    proposed_bond_indices.emplace(
        std::pair{bond.first.value, bond.second.value}, index);
  }
  std::set<std::size_t> changed_bond_indices;
  for (const auto &cycle : five_and_six_member_cycles(graph)) {
    const auto supported_atoms = std::ranges::all_of(cycle, [&](std::size_t atom) {
      const auto element = topology.atoms()[atom].atomic_number;
      return element == 6U || element == 7U;
    });
    if (!supported_atoms || !planar_cycle(cycle, coordinates))
      continue;
    bool supported_edges = true;
    for (std::size_t index = 0; index < cycle.size(); ++index) {
      const auto first = cycle[index];
      const auto second = cycle[(index + 1U) % cycle.size()];
      if (!frame.atom_present(first) || !frame.atom_present(second) ||
          distance(coordinates[first], coordinates[second]) < 1.25 ||
          distance(coordinates[first], coordinates[second]) > 1.50) {
        supported_edges = false;
        break;
      }
      const auto edge = std::minmax(first, second);
      const auto existing = existing_bond_indices.find(edge);
      if (existing == existing_bond_indices.end())
        continue;
      const auto &bond = topology.bonds()[existing->second];
      if (bond.order != BondOrder::aromatic &&
          (bond.order != BondOrder::unknown ||
           bond.order_origin == ChemicalAnnotationOrigin::explicit_input ||
           bond.order_origin == ChemicalAnnotationOrigin::user_override)) {
        supported_edges = false;
        break;
      }
    }
    if (!supported_edges)
      continue;
    ++report.aromatic_ring_count;
    for (std::size_t index = 0; index < cycle.size(); ++index) {
      const auto edge = std::minmax(cycle[index],
                                    cycle[(index + 1U) % cycle.size()]);
      if (const auto proposed = proposed_bond_indices.find(edge);
          proposed != proposed_bond_indices.end()) {
        report.proposed_bonds[proposed->second].order = BondOrder::aromatic;
      } else if (const auto existing = existing_bond_indices.find(edge);
                 existing != existing_bond_indices.end() &&
                 topology.bonds()[existing->second].order ==
                     BondOrder::unknown &&
                 changed_bond_indices.insert(existing->second).second) {
        report.proposed_bond_order_changes.push_back(
            {existing->second, BondOrder::unknown, BondOrder::aromatic});
      }
    }
  }
  std::vector<std::uint8_t> in_ring(topology.atom_count(), 0U);
  for (const auto edge : edges) {
    if (path_exists_without_edge(graph, edge.first, edge.second, edge)) {
      ++report.ring_bond_count;
      in_ring[edge.first] = 1U;
      in_ring[edge.second] = 1U;
    }
  }

  std::vector<double> valences(topology.atom_count(), 0.0);
  for (const auto &bond : topology.bonds()) {
    valences[bond.first.value] += order_value(bond.order);
    valences[bond.second.value] += order_value(bond.order);
  }
  for (const auto &bond : report.proposed_bonds) {
    valences[bond.first.value] += order_value(bond.order);
    valences[bond.second.value] += order_value(bond.order);
  }
  for (const auto &change : report.proposed_bond_order_changes) {
    const auto &bond = topology.bonds()[change.bond_index];
    const auto delta = order_value(change.proposed_order) -
                       order_value(change.source_order);
    valences[bond.first.value] += delta;
    valences[bond.second.value] += delta;
  }
  report.atoms.reserve(topology.atom_count());
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    const auto target = target_valence(topology.atoms()[index].atomic_number);
    const auto supported = target > 0.0;
    const auto remaining = supported ? target - valences[index] : 0.0;
    const auto hydrogens = remaining > 0.0
                               ? static_cast<std::size_t>(
                                     std::floor(remaining + 1e-9))
                               : 0U;
    report.atoms.push_back(
        {{index}, valences[index], hydrogens, in_ring[index] != 0U, supported});
    if (supported && valences[index] > target + 1e-9) {
      report.warnings.push_back("atom " + std::to_string(index + 1U) +
                                " exceeds conservative target valence");
    }
  }

  if (context != nullptr && context->report_progress)
    context->report_progress({1.0, "chemical-perception-complete"});
  return operation::Result<ChemicalPerceptionReport>::success(
      std::move(report));
}

operation::Result<std::shared_ptr<const Topology>> apply_chemical_perception(
    const Topology &source, const ChemicalPerceptionReport &report) {
  if (report.rule_set != kChemicalPerceptionRuleSet ||
      report.rule_version != kChemicalPerceptionRuleVersion ||
      report.topology_version != source.version()) {
    return operation::Result<std::shared_ptr<const Topology>>::failure(
        {operation::ErrorCode::stale_result,
         "chemical perception report does not match the source topology",
         {}});
  }
  auto builder = TopologyBuilder::from(source);
  for (const auto &residue : report.residues) {
    if (!residue.proposed)
      continue;
    if (const auto error = builder.set_residue_semantics(
            residue.residue, residue.kind, residue.polymer_type,
            ChemicalAnnotationOrigin::inferred);
        error.has_value()) {
      return operation::Result<std::shared_ptr<const Topology>>::failure(*error);
    }
  }
  for (const auto &proposal : report.proposed_bonds) {
    if ((proposal.order != BondOrder::single &&
         proposal.order != BondOrder::aromatic) ||
        !std::isfinite(proposal.distance_angstrom) ||
        proposal.distance_angstrom <= 0.0) {
      return operation::Result<std::shared_ptr<const Topology>>::failure(
          {operation::ErrorCode::invalid_argument,
           "chemical perception proposal is malformed", {}});
    }
    if (const auto error = builder.add_bond(
            {proposal.first, proposal.second, proposal.order, BondQuery::none,
             BondStereo::none, ChemicalAnnotationOrigin::inferred});
        error.has_value()) {
      return operation::Result<std::shared_ptr<const Topology>>::failure(*error);
    }
  }
  for (const auto &change : report.proposed_bond_order_changes) {
    if (change.bond_index >= source.bonds().size() ||
        source.bonds()[change.bond_index].order != change.source_order ||
        change.source_order != BondOrder::unknown ||
        change.proposed_order != BondOrder::aromatic ||
        source.bonds()[change.bond_index].order_origin ==
            ChemicalAnnotationOrigin::explicit_input ||
        source.bonds()[change.bond_index].order_origin ==
            ChemicalAnnotationOrigin::user_override) {
      return operation::Result<std::shared_ptr<const Topology>>::failure(
          {operation::ErrorCode::invalid_argument,
           "chemical perception bond-order proposal is malformed", {}});
    }
    if (const auto error = builder.set_bond_semantics(
            change.bond_index, BondOrder::aromatic, BondQuery::none,
            ChemicalAnnotationOrigin::inferred);
        error.has_value()) {
      return operation::Result<std::shared_ptr<const Topology>>::failure(*error);
    }
  }
  return builder.build();
}

}  // namespace molshredder::model
