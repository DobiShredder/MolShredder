#include <iostream>
#include <limits>
#include <string_view>

#include "molshredder/model/molecular_builder.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

molshredder::model::MoleculeBuildRequest carbonyl_request() {
  using namespace molshredder::model;
  MoleculeBuildRequest request;
  request.residues.push_back(
      ResidueRecord{"LIG", 1, "", "A", "", ResidueKind::ligand,
                    PolymerType::none,
                    ChemicalAnnotationOrigin::user_override});
  request.atoms = {
      {AtomRecord{"C", 6U, ResidueIndex{0U}, "", 0, std::nullopt,
                  std::nullopt, AtomStereoParity::unspecified,
                  RadicalState::none, true,
                  ChemicalAnnotationOrigin::user_override},
       {0.0, 0.0, 0.0}},
      {AtomRecord{"O", 8U, ResidueIndex{0U}, "", 0, std::nullopt,
                  std::nullopt, AtomStereoParity::unspecified,
                  RadicalState::none, true,
                  ChemicalAnnotationOrigin::user_override},
       {1.2, 0.0, 0.0}}};
  request.bonds.push_back({0U, 1U, BondOrder::double_bond});
  return request;
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  const auto built = model::build_molecule(carbonyl_request());
  passed &= expect(
      built.has_value() && built.value().topology->atom_count() == 2U &&
          built.value().topology->residue_count() == 1U &&
          built.value().topology->bonds().size() == 1U &&
          built.value().topology->bonds().front().order ==
              model::BondOrder::double_bond &&
          built.value().atom_ids ==
              std::vector<model::AtomId>{{1U}, {2U}} &&
          built.value().bond_ids == std::vector<model::BondId>{{1U}} &&
          built.value().coordinates->frame_count() == 1U,
      "builder must create stable residue/atom/bond/coordinate snapshots");

  auto duplicate = carbonyl_request();
  duplicate.bonds.push_back({1U, 0U, model::BondOrder::single});
  passed &= expect(!model::build_molecule(duplicate).has_value(),
                   "duplicate reverse bond must fail atomically");
  auto missing_residue = carbonyl_request();
  missing_residue.atoms.front().atom.residue = model::ResidueIndex{9U};
  passed &= expect(!model::build_molecule(missing_residue).has_value(),
                   "unknown residue ordinal must fail atomically");
  auto non_finite = carbonyl_request();
  non_finite.atoms.front().position.x =
      std::numeric_limits<double>::infinity();
  passed &= expect(!model::build_molecule(non_finite).has_value(),
                   "non-finite builder coordinate must fail atomically");
  auto undersized = carbonyl_request();
  undersized.memory_budget_bytes = 1U;
  const auto exhausted = model::build_molecule(undersized);
  passed &= expect(
      !exhausted.has_value() &&
          exhausted.error().code ==
              operation::ErrorCode::resource_exhausted,
      "undersized builder reservation must fail before candidate allocation");
  auto cancelled_request = carbonyl_request();
  cancelled_request.cancellation_requested = [] { return true; };
  const auto cancelled = model::build_molecule(cancelled_request);
  passed &= expect(
      !cancelled.has_value() &&
          cancelled.error().code == operation::ErrorCode::cancelled,
      "cancelled builder must publish no partial topology or coordinates");
  return passed ? 0 : 1;
}
