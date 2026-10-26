// Secondary structure from the hydrogen-bond pattern rules published by
// Kabsch and Sander, Biopolymers 22:2577 (1983). Written from the paper; it
// uses no code of the DSSP program and makes no claim of exact DSSP parity.
//
// Rules used (H-bond (i,j) means the CO of residue i is bonded to the NH of j):
//   energy      E = 0.084 * 332 * (1/rON + 1/rCH - 1/rOH - 1/rCN) kcal/mol,
//               a bond when E < -0.5; each NH keeps its two best partners.
//   n-turn      H-bond (i, i+n), n = 3, 4, 5.
//   helix       two consecutive n-turns at i-1 and i give residues i..i+n-1
//               (n=4 H, n=3 G, n=5 I); a lone n-turn marks i+1..i+n-1 as T.
//   bridge      parallel   (i-1,j) & (j,i+1)  or  (j-1,i) & (i,j+1)
//               antiparallel (i,j) & (j,i)    or  (i-1,j+1) & (j-1,i+1)
//   strand E    residues of a ladder of two or more consecutive bridges of one
//               type; an isolated bridge is B.
// Priority when several apply: H, B, E, G, I, T.
#include "molshredder/analysis/secondary_structure.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "molshredder/operation/error.hpp"

namespace molshredder::analysis {
namespace {

using model::Vec3d;

constexpr double kBondEnergyLimit = -0.5;
constexpr double kEnergyFloor = -9.9;
constexpr double kCouplingConstant = 0.084 * 332.0;  // q1*q2*f in kcal*A/mol
constexpr double kMaxChainBreak = 2.5;                // C(i)-N(i+1), angstrom
constexpr double kCandidateCaDistance = 9.0;

Vec3d sub(Vec3d a, Vec3d b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3d add(Vec3d a, Vec3d b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3d mul(Vec3d a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(Vec3d a, Vec3d b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double length(Vec3d a) { return std::sqrt(dot(a, a)); }

Vec3d point_of(const model::CoordinateFrame &frame, std::size_t index,
               double scale) {
  return std::visit(
      [index, scale](const auto &values) {
        return Vec3d{static_cast<double>(values[index].x) * scale,
                     static_cast<double>(values[index].y) * scale,
                     static_cast<double>(values[index].z) * scale};
      },
      frame.positions().values());
}

struct Backbone {
  bool complete{};
  Vec3d n{}, ca{}, c{}, o{}, h{};
  bool has_h{};   // false for proline and for the first residue of a chain
  bool joined_to_previous{};  // covalently continues residue r-1
};

double hbond_energy(const Backbone &donor, const Backbone &acceptor) {
  const auto r_on = length(sub(acceptor.o, donor.n));
  const auto r_ch = length(sub(acceptor.c, donor.h));
  const auto r_oh = length(sub(acceptor.o, donor.h));
  const auto r_cn = length(sub(acceptor.c, donor.n));
  if (r_on < 0.5 || r_ch < 0.5 || r_oh < 0.5 || r_cn < 0.5) return kEnergyFloor;
  const auto energy =
      kCouplingConstant * (1.0 / r_on + 1.0 / r_ch - 1.0 / r_oh - 1.0 / r_cn);
  return std::clamp(energy, kEnergyFloor, 0.0);
}

struct Partner {
  std::size_t acceptor{};
  double energy{0.0};
  bool used{};
};

int rank_of(SecondaryStructureState state) {
  switch (state) {
  case SecondaryStructureState::alpha_helix: return 7;
  case SecondaryStructureState::beta_bridge: return 6;
  case SecondaryStructureState::extended_strand: return 5;
  case SecondaryStructureState::helix_310: return 4;
  case SecondaryStructureState::pi_helix: return 3;
  case SecondaryStructureState::turn: return 2;
  case SecondaryStructureState::coil: return 1;
  }
  return 0;
}

void assign(std::vector<SecondaryStructureResidue> &residues, std::size_t index,
            SecondaryStructureState state) {
  if (rank_of(state) > rank_of(residues[index].state))
    residues[index].state = state;
}

}  // namespace

operation::Result<SecondaryStructureResult> assign_secondary_structure_dssp(
    const model::Topology &topology, const model::CoordinateFrame &frame) {
  using Output = operation::Result<SecondaryStructureResult>;
  const auto geometry = calculate_backbone_geometry(topology, frame);
  if (!geometry.has_value()) return Output::failure(geometry.error());
  const auto count = topology.residue_count();
  const auto scale =
      frame.metadata().coordinate_unit == operation::LengthUnit::nanometer ? 10.0
                                                                          : 1.0;

  SecondaryStructureResult result;
  result.method = "molshredder-dssp-method-v1";
  result.residues.reserve(count);
  std::vector<Backbone> backbone(count);
  for (std::size_t r = 0; r < count; ++r) {
    const auto &atoms = geometry.value()[r].atoms;
    result.residues.push_back({atoms.residue, SecondaryStructureState::coil,
                               geometry.value()[r].phi_degrees,
                               geometry.value()[r].psi_degrees,
                               atoms.n && atoms.ca && atoms.c && atoms.o});
    if (!(atoms.n && atoms.ca && atoms.c && atoms.o)) continue;
    auto &item = backbone[r];
    item.complete = true;
    item.n = point_of(frame, atoms.n->value, scale);
    item.ca = point_of(frame, atoms.ca->value, scale);
    item.c = point_of(frame, atoms.c->value, scale);
    item.o = point_of(frame, atoms.o->value, scale);
  }
  // Chain continuity and the amide hydrogen, 1.0 A from N opposite to C=O of
  // the previous residue (the paper's construction). Proline has no NH.
  for (std::size_t r = 1; r < count; ++r) {
    if (!backbone[r].complete || !backbone[r - 1].complete) continue;
    const auto &previous = topology.residues()[r - 1];
    const auto &current = topology.residues()[r];
    if (previous.chain_id != current.chain_id ||
        previous.segment_id != current.segment_id)
      continue;
    if (length(sub(backbone[r].n, backbone[r - 1].c)) > kMaxChainBreak) continue;
    backbone[r].joined_to_previous = true;
    if (current.name == "PRO") continue;
    const auto direction = sub(backbone[r - 1].c, backbone[r - 1].o);
    const auto norm = length(direction);
    if (!(norm > 0.0)) continue;
    backbone[r].h = add(backbone[r].n, mul(direction, 1.0 / norm));
    backbone[r].has_h = true;
  }

  // Candidate pairs from a cell grid on C-alpha (cells of 9 A).
  using Cell = std::array<std::int64_t, 3>;
  struct CellHash {
    std::size_t operator()(const Cell &cell) const noexcept {
      std::uint64_t hash = 1469598103934665603ULL;
      for (const auto value : cell) {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ULL;
        hash ^= hash >> 29U;
      }
      return static_cast<std::size_t>(hash);
    }
  };
  const auto cell_of = [](Vec3d p) {
    return Cell{static_cast<std::int64_t>(std::floor(p.x / kCandidateCaDistance)),
                static_cast<std::int64_t>(std::floor(p.y / kCandidateCaDistance)),
                static_cast<std::int64_t>(std::floor(p.z / kCandidateCaDistance))};
  };
  std::unordered_map<Cell, std::vector<std::size_t>, CellHash> grid;
  for (std::size_t r = 0; r < count; ++r)
    if (backbone[r].complete) grid[cell_of(backbone[r].ca)].push_back(r);

  // Each NH keeps its two best CO partners.
  std::vector<std::array<Partner, 2>> best(count);
  const auto offer = [&](std::size_t donor, std::size_t acceptor, double energy) {
    auto &slots = best[donor];
    if (!slots[0].used || energy < slots[0].energy) {
      slots[1] = slots[0];
      slots[0] = {acceptor, energy, true};
    } else if (!slots[1].used || energy < slots[1].energy) {
      slots[1] = {acceptor, energy, true};
    }
  };
  for (std::size_t donor = 0; donor < count; ++donor) {
    if (!backbone[donor].has_h) continue;
    const auto home = cell_of(backbone[donor].ca);
    for (std::int64_t dx = -1; dx <= 1; ++dx)
      for (std::int64_t dy = -1; dy <= 1; ++dy)
        for (std::int64_t dz = -1; dz <= 1; ++dz) {
          const auto found = grid.find({home[0] + dx, home[1] + dy, home[2] + dz});
          if (found == grid.end()) continue;
          for (const auto acceptor : found->second) {
            if (acceptor == donor) continue;
            // The neighbors along the chain cannot bond to each other.
            if ((acceptor + 1 == donor && backbone[donor].joined_to_previous) ||
                (donor + 1 == acceptor && backbone[acceptor].joined_to_previous))
              continue;
            if (length(sub(backbone[donor].ca, backbone[acceptor].ca)) >
                kCandidateCaDistance)
              continue;
            offer(donor, acceptor,
                  hbond_energy(backbone[donor], backbone[acceptor]));
          }
        }
  }
  // hbond(a, d): the CO of residue a is bonded to the NH of residue d.
  const auto hbond = [&](std::size_t a, std::size_t d) {
    if (d >= count || a >= count) return false;
    for (const auto &slot : best[d])
      if (slot.used && slot.acceptor == a && slot.energy < kBondEnergyLimit)
        return true;
    return false;
  };
  for (std::size_t d = 0; d < count; ++d)
    for (const auto &slot : best[d])
      if (slot.used && slot.energy < kBondEnergyLimit)
        result.hydrogen_bonds.push_back(
            {model::ResidueIndex{slot.acceptor}, model::ResidueIndex{d},
             slot.energy, true});
  // Same (donor, acceptor) order as the other method.
  std::sort(result.hydrogen_bonds.begin(), result.hydrogen_bonds.end(),
            [](const auto &a, const auto &b) {
              return std::pair{a.donor.value, a.acceptor.value} <
                     std::pair{b.donor.value, b.acceptor.value};
            });

  // Helices and turns. A run of n residues must be chain-continuous.
  const auto continuous = [&](std::size_t first, std::size_t last) {
    if (last >= count) return false;
    for (std::size_t r = first + 1; r <= last; ++r)
      if (!backbone[r].joined_to_previous) return false;
    return backbone[first].complete;
  };
  struct TurnKind {
    std::size_t span;
    SecondaryStructureState helix;
  };
  const std::array<TurnKind, 3> kinds{
      TurnKind{4, SecondaryStructureState::alpha_helix},
      TurnKind{3, SecondaryStructureState::helix_310},
      TurnKind{5, SecondaryStructureState::pi_helix}};
  for (const auto &kind : kinds) {
    std::vector<std::uint8_t> turn(count, 0U);
    for (std::size_t i = 0; i + kind.span < count; ++i)
      if (continuous(i, i + kind.span) && hbond(i, i + kind.span)) turn[i] = 1U;
    for (std::size_t i = 0; i < count; ++i) {
      if (turn[i] == 0U) continue;
      for (std::size_t r = i + 1; r < i + kind.span; ++r)
        assign(result.residues, r, SecondaryStructureState::turn);
      if (i > 0 && turn[i - 1] != 0U)
        for (std::size_t r = i; r < i + kind.span; ++r)
          assign(result.residues, r, kind.helix);
    }
  }

  // Bridges and ladders.
  enum class Bridge : std::uint8_t { parallel, antiparallel };
  std::set<std::pair<std::size_t, std::size_t>> candidates;
  for (const auto &bond : result.hydrogen_bonds) {
    const auto a = bond.acceptor.value, d = bond.donor.value;
    for (int da = -1; da <= 1; ++da)
      for (int dd = -1; dd <= 1; ++dd) {
        const auto x = static_cast<long long>(a) + da;
        const auto y = static_cast<long long>(d) + dd;
        if (x < 1 || y < 1 || x + 1 >= static_cast<long long>(count) ||
            y + 1 >= static_cast<long long>(count))
          continue;
        const auto lo = static_cast<std::size_t>(std::min(x, y));
        const auto hi = static_cast<std::size_t>(std::max(x, y));
        if (hi >= lo + 3U) candidates.insert({lo, hi});
      }
  }
  std::map<std::pair<std::size_t, std::size_t>, Bridge> bridges;
  for (const auto &[i, j] : candidates) {
    if (!continuous(i - 1, i + 1) || !continuous(j - 1, j + 1)) continue;
    const bool parallel = (hbond(i - 1, j) && hbond(j, i + 1)) ||
                          (hbond(j - 1, i) && hbond(i, j + 1));
    const bool anti = (hbond(i, j) && hbond(j, i)) ||
                      (hbond(i - 1, j + 1) && hbond(j - 1, i + 1));
    if (parallel)
      bridges[{i, j}] = Bridge::parallel;
    else if (anti)
      bridges[{i, j}] = Bridge::antiparallel;
  }
  std::set<std::pair<std::size_t, std::size_t>> visited;
  for (const auto &[pair, type] : bridges) {
    if (visited.contains(pair)) continue;
    // Walk back to the start of this ladder, then forward collecting it.
    auto start = pair;
    while (true) {
      const auto before =
          type == Bridge::parallel
              ? std::pair{start.first - 1, start.second - 1}
              : std::pair{start.first - 1, start.second + 1};
      if (start.first == 0 || (type == Bridge::parallel && start.second == 0)) break;
      const auto found = bridges.find(before);
      if (found == bridges.end() || found->second != type) break;
      start = before;
    }
    std::vector<std::pair<std::size_t, std::size_t>> ladder{start};
    visited.insert(start);
    while (true) {
      const auto &last = ladder.back();
      if (type == Bridge::antiparallel && last.second == 0) break;
      const auto next = type == Bridge::parallel
                            ? std::pair{last.first + 1, last.second + 1}
                            : std::pair{last.first + 1, last.second - 1};
      const auto found = bridges.find(next);
      if (found == bridges.end() || found->second != type) break;
      ladder.push_back(next);
      visited.insert(next);
    }
    const auto state = ladder.size() >= 2 ? SecondaryStructureState::extended_strand
                                          : SecondaryStructureState::beta_bridge;
    for (const auto &[x, y] : ladder) {
      assign(result.residues, x, state);
      assign(result.residues, y, state);
    }
  }
  return Output::success(std::move(result));
}

}  // namespace molshredder::analysis
