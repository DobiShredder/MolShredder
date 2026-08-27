#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/model/topology.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/selection/evaluator.hpp"
#include "molshredder/selection/expression.hpp"
#include "molshredder/selection/named_selection.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

std::shared_ptr<const molshredder::model::Topology> make_topology() {
  using namespace molshredder::model;
  TopologyBuilder builder;
  const auto gly =
      builder.add_residue(ResidueRecord{"GLY", 10, "A", "A", "PROT"});
  const auto water =
      builder.add_residue(ResidueRecord{"HOH", 5, "", "W", "WAT"});
  const auto nitrogen = builder.add_atom(
      AtomRecord{"N", 7U, gly.value(), "A", 0, 101});
  const auto alpha = builder.add_atom(
      AtomRecord{"CA", 6U, gly.value(), "", 0, 102});
  const auto oxygen = builder.add_atom(
      AtomRecord{"O", 8U, gly.value(), "", 0, 103});
  static_cast<void>(builder.add_atom(
      AtomRecord{"O", 8U, water.value(), "", 0, 201}));
  static_cast<void>(builder.add_bond(
      Bond{nitrogen.value(), alpha.value(), BondOrder::single}));
  static_cast<void>(builder.add_bond(
      Bond{alpha.value(), oxygen.value(), BondOrder::double_bond}));
  static_cast<void>(builder.add_property(
      "occupancy", std::vector<double>{1.0, 0.5, 0.0, 1.0}));
  static_cast<void>(builder.add_property(
      "b_factor", std::vector<double>{10.0, 20.0, 30.0, 40.0}));
  static_cast<void>(builder.add_property(
      "partial_charge", std::vector<double>{-0.3, 0.1, -0.2, -0.4}));
  static_cast<void>(builder.add_property(
      "score", std::vector<std::int64_t>{1, 2, 3, 4}));
  static_cast<void>(builder.add_property(
      "score_present", BooleanColumn{{1U, 0U, 1U, 1U}}));
  static_cast<void>(builder.add_property(
      "label", std::vector<std::string>{"a", "b", "c", "d"}));
  static_cast<void>(builder.add_property(
      "secondary_structure",
      std::vector<std::string>{"H", "H", "C", "C"}));
  static_cast<void>(builder.add_property(
      "active", BooleanColumn{{1U, 0U, 1U, 0U}}));
  static_cast<void>(builder.add_property(
      "enabled", BooleanColumn{{1U, 1U, 0U, 1U}}));
  static_cast<void>(builder.add_property(
      "visible", BooleanColumn{{1U, 0U, 1U, 1U}}));
  static_cast<void>(builder.add_property(
      "fixed", BooleanColumn{{0U, 1U, 0U, 0U}}));
  static_cast<void>(builder.add_property(
      "masked", BooleanColumn{{0U, 0U, 1U, 0U}}));
  static_cast<void>(builder.add_property(
      "protected", BooleanColumn{{0U, 0U, 0U, 1U}}));
  static_cast<void>(builder.add_property(
      "restrained", BooleanColumn{{1U, 0U, 0U, 0U}}));
  static_cast<void>(builder.add_property(
      "representation",
      std::vector<std::string>{"cartoon", "sticks", "lines", "spheres"}));
  static_cast<void>(builder.add_property(
      "color", std::vector<std::string>{"blue", "green", "red", "red"}));
  static_cast<void>(builder.add_property(
      "cartoon_color",
      std::vector<std::string>{"cyan", "cyan", "white", "white"}));
  static_cast<void>(builder.add_property(
      "ribbon_color",
      std::vector<std::string>{"blue", "blue", "white", "white"}));
  static_cast<void>(builder.add_property(
      "flag", std::vector<std::int64_t>{1, 2, 2, 0}));
  static_cast<void>(builder.add_property(
      "numeric_type", std::vector<std::int64_t>{7, 6, 8, 8}));
  static_cast<void>(builder.add_property(
      "text_type", std::vector<std::string>{"N", "CT", "O", "OW"}));
  static_cast<void>(builder.add_property(
      "stereo", std::vector<std::string>{"", "R", "", ""}));
  static_cast<void>(builder.add_property(
      "pdb.is_hetero", BooleanColumn{{0U, 0U, 0U, 1U}}));
  return builder.build().value();
}

bool evaluates_to(const molshredder::model::Topology& topology,
                  std::string_view text,
                  const molshredder::selection::Mask& expected) {
  const auto expression = molshredder::selection::Expression::parse(text);
  if (!expression.has_value()) return false;
  const auto mask = molshredder::selection::evaluate(expression.value(),
                                                      topology);
  return mask.has_value() && mask.value() == expected;
}

std::shared_ptr<const molshredder::model::CoordinateFrame> make_frame(
    float offset, std::vector<double> occupancy,
    std::vector<std::uint8_t> presence = {1U, 1U, 1U, 1U}) {
  using namespace molshredder::model;
  FrameMetadata metadata;
  metadata.atom_properties.emplace(
      "occupancy", AtomProperty{std::move(occupancy), {}});
  return CoordinateFrame::create(
             CoordinateBuffer{std::vector<Vec3f>{{offset + 0.0F, 0.0F, 0.0F},
                                                  {offset + 1.0F, 1.0F, 0.0F},
                                                  {offset + 2.0F, 2.0F, 0.0F},
                                                  {offset + 3.0F, 3.0F, 0.0F}}},
             std::nullopt, std::move(presence), std::move(metadata))
      .value();
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  const auto topology = make_topology();
  const auto first_frame = make_frame(0.0F, {1.0, 0.5, 0.0, 1.0});
  const auto second_frame =
      make_frame(10.0F, {0.0, 1.0, 1.0, 0.0}, {1U, 1U, 0U, 1U});
  const auto coordinate_source = model::InMemoryCoordinateSource::create(
      topology->atom_count(), {first_frame, second_frame});

  passed &= expect(evaluates_to(*topology, "all", {1U, 1U, 1U, 1U}) &&
                       evaluates_to(*topology, "none", {0U, 0U, 0U, 0U}),
                   "all and none must produce complete masks");
  const auto aliases_parse = [](std::string_view tokens,
                                const auto& expression_for) {
    std::istringstream input{std::string{tokens}};
    std::string token;
    while (input >> token) {
      const auto parsed = selection::Expression::parse(expression_for(token));
      if (!parsed.has_value()) {
        std::cerr << "alias did not parse: " << token << '\n';
        return false;
      }
    }
    return true;
  };
  passed &= expect(
      aliases_parse(
          "all * none present pr. bonded enabled visible v. v; fixed fxd. "
          "masked msk. protected restrained rst. hetatm het hydrogens hydro "
          "h. h; donors don. hbd. acceptors acc. hba. polymer pol. "
          "polymer.protein polymer.nucleic organic org. inorganic ino. "
          "solvent sol. metals backbone bb. sidechain sc. guide center origin",
          [](const std::string& token) { return token; }) &&
          aliases_parse(
              "byresidue byresi byres br. br; b; bychain bc. bysegment byseg "
              "bysegi bs. byobject byobj bo. bo; bymolecule bymol bm. "
              "byfragment byfrag bf. bycalpha bca. neighbor nbr. nbr; "
              "bound_to bto. byring bycell first last not",
              [](const std::string& token) { return token + " all"; }) &&
          aliases_parse(
              "name n. n; element elem symbol e. e; altloc alt resname resn "
              "r. r; resid resi residue resident i. i; chain c. c; segment "
              "segid segi s. s; object model o. m. m; rank pepseq ps. label "
              "flag f. f; numeric_type nt. nt; text_type tt. tt; stereo ss "
              "custom color cartoon_color ribbon_color rep state index idx. id ID",
              [](const std::string& token) {
                if (token == "rank" || token == "state" || token == "flag" ||
                    token == "f." || token == "f;" ||
                    token == "numeric_type" || token == "nt." || token == "nt;")
                  return token + " 1";
                if (token == "index" || token == "idx." || token == "id" ||
                    token == "ID")
                  return token + " 1";
                return token + " X";
              }) &&
          aliases_parse(
              "b formal_charge fc. fc; partial_charge pc. pc; q x y z",
              [](const std::string& token) { return token + " = 1"; }) &&
          aliases_parse(
              "around a. a; expand x. x; extend xt. gap",
              [](const std::string& token) { return "all " + token + " 1"; }) &&
          aliases_parse(
              "within w. near_to nto. beyond be.",
              [](const std::string& token) {
                return "all " + token + " 1 of all";
              }) &&
          aliases_parse(
              "in like l. l;",
              [](const std::string& token) { return "all " + token + " all"; }) &&
          aliases_parse(
              "= < >",
              [](const std::string& token) { return "b " + token + " 1"; }) &&
          selection::Expression::parse("! all").has_value() &&
          selection::Expression::parse("all & all").has_value() &&
          selection::Expression::parse("all + none").has_value() &&
          selection::Expression::parse("all - none").has_value() &&
          selection::Expression::parse("all | none").has_value() &&
          selection::Expression::parse("all and all").has_value() &&
          selection::Expression::parse("all or none").has_value() &&
          selection::Expression::parse("%named").has_value() &&
          selection::Expression::parse("p.score = 1").has_value() &&
          selection::Expression::parse("b in 1:2").has_value(),
      "all pinned selection keywords, aliases and comparison tokens must parse");
  passed &= expect(
      evaluates_to(*topology, "name CA or name O and chain A",
                   {0U, 1U, 1U, 0U}) &&
          evaluates_to(*topology, "(name CA or name O) and not resn HOH",
                       {0U, 1U, 1U, 0U}),
      "not/and/or precedence and parentheses must be deterministic");
  passed &= expect(
      evaluates_to(*topology, "chain A name CA", {0U, 1U, 0U, 0U}) &&
          evaluates_to(*topology, "chain A-W", {1U, 1U, 1U, 1U}),
      "implicit conjunction and alpha ranges must be deterministic");
  passed &= expect(evaluates_to(*topology, "name \"and\"", {0U, 0U, 0U, 0U}),
                   "quoted logical keywords must remain predicate values");
  passed &= expect(evaluates_to(*topology, "chain \"\"", {0U, 0U, 0U, 0U}),
                   "quoted empty identifier must remain a predicate value");
  passed &= expect(evaluates_to(*topology, "elem o", {0U, 0U, 1U, 1U}) &&
                       evaluates_to(*topology, "resi 10A", {1U, 1U, 1U, 0U}) &&
                       evaluates_to(*topology, "segi WAT", {0U, 0U, 0U, 1U}) &&
                       evaluates_to(*topology, "index 2:4", {0U, 1U, 1U, 1U}) &&
                       evaluates_to(*topology, "idx. 1+3-4",
                                    {1U, 0U, 1U, 1U}) &&
                       evaluates_to(*topology, "id 201", {0U, 0U, 0U, 1U}),
                   "field aliases, residue IDs and numeric ranges must work");
  passed &= expect(
      evaluates_to(*topology, "name N+O", {1U, 0U, 1U, 1U}) &&
          evaluates_to(*topology, "n. C*", {0U, 1U, 0U, 0U}) &&
          evaluates_to(*topology, "symbol o", {0U, 0U, 1U, 1U}) &&
          evaluates_to(*topology, "alt A", {1U, 0U, 0U, 0U}) &&
          evaluates_to(*topology, "residue 5-10", {1U, 1U, 1U, 1U}) &&
          evaluates_to(*topology, "c. A and s. PROT",
                       {1U, 1U, 1U, 0U}),
      "identifier aliases, wildcard/list, altloc and residue ranges must work");
  passed &= expect(
      evaluates_to(*topology, "name \"C*\"", {0U, 0U, 0U, 0U}),
      "quoted wildcard characters must remain literal values");
  passed &= expect(
      evaluates_to(*topology, "*", {1U, 1U, 1U, 1U}) &&
          evaluates_to(*topology, "name N | name CA",
                       {1U, 1U, 0U, 0U}) &&
          evaluates_to(*topology, "name N + name CA",
                       {1U, 1U, 0U, 0U}) &&
          evaluates_to(*topology, "all - name O", {1U, 1U, 0U, 0U}) &&
          evaluates_to(*topology, "! name O", {1U, 1U, 0U, 0U}) &&
          evaluates_to(*topology, "first index 2:4", {0U, 1U, 0U, 0U}) &&
          evaluates_to(*topology, "last index 1:3", {0U, 0U, 1U, 0U}),
      "symbolic logic, subtraction, all alias and first/last must work");
  passed &= expect(
      evaluates_to(*topology, "name O in index 3", {0U, 0U, 1U, 0U}) &&
          evaluates_to(*topology, "name O like index 3",
                       {0U, 0U, 1U, 0U}) &&
          evaluates_to(*topology, "name O l. index 4",
                       {0U, 0U, 0U, 1U}),
      "identifier-set and name/residue match operators must filter the left selection");
  passed &= expect(
      evaluates_to(*topology, "ss H", {1U, 1U, 0U, 0U}) &&
          evaluates_to(*topology, "p.label a+c", {1U, 0U, 1U, 0U}) &&
          evaluates_to(*topology, "p.active true", {1U, 0U, 1U, 0U}) &&
          evaluates_to(*topology, "p.score 3", {0U, 0U, 1U, 0U}),
      "text, boolean and numeric custom property matching must work");
  passed &= expect(
      evaluates_to(*topology, "enabled", {1U, 1U, 0U, 1U}) &&
          evaluates_to(*topology, "v.", {1U, 0U, 1U, 1U}) &&
          evaluates_to(*topology, "fixed or msk.", {0U, 1U, 1U, 0U}) &&
          evaluates_to(*topology, "protected or rst.",
                       {1U, 0U, 0U, 1U}) &&
          evaluates_to(*topology, "rep cartoon", {1U, 0U, 0U, 0U}) &&
          evaluates_to(*topology, "color red", {0U, 0U, 1U, 1U}) &&
          evaluates_to(*topology, "cartoon_color cyan",
                       {1U, 1U, 0U, 0U}) &&
          evaluates_to(*topology, "ribbon_color blue",
                       {1U, 1U, 0U, 0U}),
      "appearance and edit-status selectors must use typed atom properties");
  passed &= expect(
      evaluates_to(*topology, "flag 2", {0U, 1U, 1U, 0U}) &&
          evaluates_to(*topology, "f. 1", {1U, 0U, 0U, 0U}) &&
          evaluates_to(*topology, "numeric_type 8", {0U, 0U, 1U, 1U}) &&
          evaluates_to(*topology, "tt. O*", {0U, 0U, 1U, 1U}) &&
          evaluates_to(*topology, "stereo R", {0U, 1U, 0U, 0U}) &&
          evaluates_to(*topology, "rank 0+3", {1U, 0U, 0U, 1U}) &&
          evaluates_to(*topology, "pepseq G", {1U, 1U, 1U, 0U}),
      "property, zero-based rank and peptide sequence selectors must work");
  const auto object_expression = selection::Expression::parse("model fixture");
  const auto object_mask = selection::evaluate(
      object_expression.value(), *topology, {},
      selection::EvaluationContext{nullptr, 0U, nullptr, "fixture"});
  passed &= expect(object_mask.has_value() &&
                       object_mask.value() ==
                           selection::Mask({1U, 1U, 1U, 1U}),
                   "object/model selectors must use the explicit object context");
  passed &= expect(
      evaluates_to(*topology, "polymer.protein", {1U, 1U, 1U, 0U}) &&
          evaluates_to(*topology, "polymer", {1U, 1U, 1U, 0U}) &&
          evaluates_to(*topology, "solvent", {0U, 0U, 0U, 1U}) &&
          evaluates_to(*topology, "hetatm", {0U, 0U, 0U, 1U}) &&
          evaluates_to(*topology, "backbone", {1U, 1U, 1U, 0U}) &&
          evaluates_to(*topology, "guide", {0U, 1U, 0U, 0U}) &&
          evaluates_to(*topology, "acceptors", {1U, 0U, 1U, 1U}) &&
          evaluates_to(*topology, "donors", {0U, 0U, 0U, 0U}) &&
          evaluates_to(*topology, "hydrogens", {0U, 0U, 0U, 0U}) &&
          evaluates_to(*topology, "metals", {0U, 0U, 0U, 0U}),
      "versioned chemical class selectors must share topology typing");
  passed &= expect(
      evaluates_to(*topology, "byresidue index 1", {1U, 1U, 1U, 0U}) &&
          evaluates_to(*topology, "bc. index 1", {1U, 1U, 1U, 0U}) &&
          evaluates_to(*topology, "bycalpha index 1", {0U, 1U, 0U, 0U}) &&
          evaluates_to(*topology, "neighbor index 2", {1U, 0U, 1U, 0U}) &&
          evaluates_to(*topology, "bound_to index 1:2",
                       {1U, 1U, 1U, 0U}) &&
          evaluates_to(*topology, "bymolecule index 1",
                       {1U, 1U, 1U, 0U}) &&
          evaluates_to(*topology, "index 1 extend 2",
                       {1U, 1U, 1U, 0U}),
      "residue/chain/calpha/bond/component expansion must be deterministic");
  model::TopologyBuilder ring_builder;
  const auto ring_residue = ring_builder.add_residue(
      model::ResidueRecord{"BEN", 1, "", "L", "LIG"});
  std::array<model::AtomIndex, 6U> ring_atoms;
  for (std::size_t index = 0; index < ring_atoms.size(); ++index) {
    ring_atoms[index] = ring_builder
                            .add_atom(model::AtomRecord{
                                "C" + std::to_string(index + 1U), 6U,
                                ring_residue.value(), "", 0,
                                static_cast<std::int64_t>(index + 1U)})
                            .value();
  }
  for (std::size_t index = 0; index < ring_atoms.size(); ++index) {
    static_cast<void>(ring_builder.add_bond(model::Bond{
        ring_atoms[index], ring_atoms[(index + 1U) % ring_atoms.size()],
        model::BondOrder::aromatic}));
  }
  const auto ring_topology = ring_builder.build().value();
  model::FrameMetadata ring_metadata;
  ring_metadata.unit_cell = model::UnitCell{{10.0, 0.0, 0.0},
                                             {0.0, 10.0, 0.0},
                                             {0.0, 0.0, 10.0}};
  const auto ring_frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::vector<model::Vec3d>(6U)}, std::nullopt, {},
      std::move(ring_metadata));
  const auto ring_context =
      selection::EvaluationContext{ring_frame.value().get(), 0U};
  const auto evaluates_ring = [&](std::string_view text,
                                  const selection::Mask& expected) {
    const auto parsed = selection::Expression::parse(text);
    if (!parsed.has_value()) return false;
    const auto evaluated = selection::evaluate(parsed.value(), *ring_topology,
                                               {}, ring_context);
    return evaluated.has_value() && evaluated.value() == expected;
  };
  passed &= expect(
      evaluates_ring("byring index 1", {1U, 1U, 1U, 1U, 1U, 1U}) &&
          evaluates_ring("bycell index 1", {1U, 1U, 1U, 1U, 1U, 1U}),
      "small-ring and current unit-cell expansion must be deterministic");
  const auto missing_cell = selection::Expression::parse("bycell index 1");
  passed &= expect(
      missing_cell.has_value() &&
          !selection::evaluate(missing_cell.value(), *topology).has_value(),
      "unit-cell expansion must fail explicitly without cell metadata");
  passed &= expect(
      evaluates_to(*topology, "b > 20", {0U, 0U, 1U, 1U}) &&
          evaluates_to(*topology, "q = 1", {1U, 0U, 0U, 1U}) &&
          evaluates_to(*topology, "partial_charge < -0.25",
                       {1U, 0U, 0U, 1U}) &&
          evaluates_to(*topology, "partial_charge in -0.4:-0.3,0.1",
                       {1U, 1U, 0U, 1U}) &&
          evaluates_to(*topology, "id in 101:103,201",
                       {1U, 1U, 1U, 1U}),
      "source-backed numeric fields and =/< />/in comparison must work");
  passed &= expect(
      evaluates_to(*topology, "b / 10 + q * 2 >= 3",
                   {1U, 1U, 1U, 1U}) &&
          evaluates_to(*topology, "p.score - index = 0",
                       {1U, 0U, 1U, 1U}) &&
          evaluates_to(*topology, "p.score != 2", {1U, 0U, 1U, 1U}),
      "numeric multiplication/division must bind before addition/subtraction");
  passed &= expect(
      evaluates_to(*topology, "(b + q * 10) / 2 >= 10",
                   {1U, 1U, 1U, 1U}),
      "numeric parentheses must override arithmetic precedence");
  passed &= expect(
      evaluates_to(*topology, "p.score >= 2", {0U, 0U, 1U, 1U}),
      "missing numeric property rows must be excluded from membership");
  const auto coordinate_expression =
      selection::Expression::parse("x >= 2 and q > 0");
  const auto first_coordinate_mask = selection::evaluate(
      coordinate_expression.value(), *topology, {},
      selection::EvaluationContext{first_frame.get(), 0U});
  const auto second_coordinate_mask = selection::evaluate(
      coordinate_expression.value(), *topology, {},
      selection::EvaluationContext{second_frame.get(), 1U});
  passed &= expect(
      first_coordinate_mask.has_value() && second_coordinate_mask.has_value() &&
          first_coordinate_mask.value() == selection::Mask({0U, 0U, 0U, 1U}) &&
          second_coordinate_mask.value() == selection::Mask({0U, 1U, 0U, 0U}),
      "coordinate and frame-property selections must use current-frame presence");
  const auto state_context = selection::EvaluationContext{
      second_frame.get(), 1U, coordinate_source.value().get()};
  const auto evaluates_state = [&](std::string_view text,
                                   const selection::Mask& expected) {
    const auto parsed = selection::Expression::parse(text);
    if (!parsed.has_value()) return false;
    const auto evaluated =
        selection::evaluate(parsed.value(), *topology, {}, state_context);
    return evaluated.has_value() && evaluated.value() == expected;
  };
  passed &= expect(
      evaluates_state("present", {1U, 1U, 0U, 1U}) &&
          evaluates_state("pr.", {1U, 1U, 0U, 1U}) &&
          evaluates_state("state 1", {1U, 1U, 1U, 1U}) &&
          evaluates_state("state 2", {1U, 1U, 0U, 1U}) &&
          evaluates_state("bonded", {1U, 1U, 1U, 0U}),
      "present/state and bonded status must use explicit frame/topology state");
  const auto spatial_context =
      selection::EvaluationContext{first_frame.get(), 0U};
  const auto evaluates_spatial = [&](std::string_view text,
                                     const selection::Mask& expected) {
    const auto parsed = selection::Expression::parse(text);
    if (!parsed.has_value()) return false;
    const auto evaluated =
        selection::evaluate(parsed.value(), *topology, {}, spatial_context);
    return evaluated.has_value() && evaluated.value() == expected;
  };
  passed &= expect(
      evaluates_spatial("index 1 around 1.5", {0U, 1U, 0U, 0U}) &&
          evaluates_spatial("index 1 expand 0", {1U, 0U, 0U, 0U}) &&
          evaluates_spatial("index 1 expand 1.5", {1U, 1U, 0U, 0U}) &&
          evaluates_spatial("index 1:3 within 1.5 of index 4",
                            {0U, 0U, 1U, 0U}) &&
          evaluates_spatial("all near_to 1.5 of index 4",
                            {0U, 0U, 1U, 0U}) &&
          evaluates_spatial("all beyond 1.5 of index 1",
                            {0U, 0U, 1U, 1U}) &&
          evaluates_spatial("index 1 gap -1.8", {0U, 1U, 0U, 0U}) &&
          evaluates_spatial("all within 0.8 of center",
                            {0U, 1U, 1U, 0U}) &&
          evaluates_spatial("origin around 0.1", {1U, 0U, 0U, 0U}),
      "spatial inclusion, exclusion and exact cutoff boundaries must work");

  const auto references =
      selection::Expression::parse("@first or (@second-item and @first)");
  passed &= expect(references.has_value() &&
                       references.value().named_references() ==
                           std::set<std::string, std::less<>>{"first",
                                                             "second-item"},
                   "named references must be explicit and deduplicated");
  const auto resolved = selection::evaluate(
      references.value(), *topology,
      [](std::string_view name) {
        if (name == "first") {
          return operation::Result<selection::Mask>::success(
              {1U, 0U, 0U, 0U});
        }
        if (name == "second-item") {
          return operation::Result<selection::Mask>::success(
            {0U, 1U, 0U, 0U});
        }
        return operation::Result<selection::Mask>::failure(operation::Error{
            operation::ErrorCode::not_found, "unexpected selection", {}});
      });
  passed &= expect(resolved.has_value() &&
                       resolved.value() == selection::Mask({1U, 0U, 0U, 0U}),
                   "external named resolver must compose masks");

  for (const auto invalid : {"", "name", "name CA extra", "(name CA",
                             "name CA and", "unknown CA", "@", "not",
                             "b >", "b in 2:1", "b in 1,,2",
                             "all in", "all like",
                             "all within -1 of all", "index 1 around"}) {
    const auto parsed = selection::Expression::parse(invalid);
    passed &= expect(!parsed.has_value() &&
                         parsed.error().code ==
                             operation::ErrorCode::invalid_selection,
                     "malformed expression must return invalid_selection");
  }
  const auto invalid_range = selection::Expression::parse("index 0");
  passed &= expect(invalid_range.has_value() &&
                       !selection::evaluate(invalid_range.value(), *topology)
                            .has_value(),
                   "one-based index domain must be enforced at evaluation");
  const auto unresolved = selection::Expression::parse("@missing");
  passed &= expect(unresolved.has_value() &&
                       !selection::evaluate(unresolved.value(), *topology)
                            .has_value(),
                   "named term without a resolver must fail");
  for (const auto invalid_numeric : {"b / 0 > 1", "p.label > 1",
                                     "p.missing > 1", "p.active maybe"}) {
    const auto parsed = selection::Expression::parse(invalid_numeric);
    passed &= expect(parsed.has_value() &&
                         !selection::evaluate(parsed.value(), *topology)
                              .has_value(),
                     "invalid numeric evaluation must fail explicitly");
  }

  selection::NamedSelections named;
  passed &= expect(!named.set("oxygen",
                              selection::Expression::parse("element O").value(),
                              true, *topology)
                        .has_value(),
                   "dynamic named selection must be created");
  passed &= expect(!named.set(
                        "protein_oxygen",
                        selection::Expression::parse(
                            "@oxygen and not resname HOH")
                            .value(),
                        true, *topology)
                        .has_value(),
                   "named selections must compose");
  passed &= expect(!named.set("first",
                              selection::Expression::parse("index 1").value(),
                              false, *topology)
                        .has_value(),
                   "static named selection must cache a mask");
  passed &= expect(
      !named.set("moving", coordinate_expression.value(), true, *topology,
                 selection::EvaluationContext{first_frame.get(), 0U})
           .has_value() &&
          !named.set("frozen", coordinate_expression.value(), false, *topology,
                     selection::EvaluationContext{first_frame.get(), 0U})
               .has_value(),
      "dynamic and static coordinate selections must be definable");
  const auto moving_second = named.evaluate(
      "moving", *topology,
      selection::EvaluationContext{second_frame.get(), 1U});
  const auto frozen_second = named.evaluate(
      "frozen", *topology,
      selection::EvaluationContext{second_frame.get(), 1U});
  passed &= expect(
      moving_second.has_value() && frozen_second.has_value() &&
          moving_second.value() == selection::Mask({0U, 1U, 0U, 0U}) &&
          frozen_second.value() == selection::Mask({0U, 0U, 0U, 1U}),
      "dynamic selection must re-evaluate while static selection preserves its defining frame mask");
  const auto protein_oxygen = named.evaluate("protein_oxygen", *topology);
  passed &= expect(protein_oxygen.has_value() &&
                       protein_oxygen.value() ==
                           selection::Mask({0U, 0U, 1U, 0U}),
                   "dynamic named selection must evaluate dependencies");

  const auto other_topology = make_topology();
  passed &= expect(named.evaluate("oxygen", *other_topology).has_value() &&
                       !named.evaluate("first", *other_topology).has_value(),
                   "dynamic selection may re-evaluate but static mask is snapshot-bound");
  auto target_builder = model::TopologyBuilder::from(*topology);
  const std::array retained{model::AtomId{4U}, model::AtomId{1U}};
  passed &= expect(!target_builder.retain_atoms(retained).has_value(),
                   "selection remap target must build");
  const auto target_topology = target_builder.build();
  const auto selection_remap = model::remap_topology(*topology,
                                                      *target_topology.value());
  const auto remapped_named = named.remap(*topology, *target_topology.value(),
                                         selection_remap);
  passed &= expect(
      remapped_named.has_value() &&
          remapped_named.value().evaluate("first", *target_topology.value())
                  .value() == selection::Mask({0U, 1U}) &&
          remapped_named.value().evaluate("oxygen", *target_topology.value())
                  .value() == selection::Mask({1U, 0U}),
      "static selections must follow stable identity and dynamic selections must re-evaluate on the target snapshot");
  passed &= expect(named.set(
                       "oxygen",
                       selection::Expression::parse("@protein_oxygen").value(),
                       true, *topology)
                       .has_value() &&
                       named.evaluate("oxygen", *topology).has_value(),
                   "cycle-producing replacement must roll back atomically");
  passed &= expect(named.erase("oxygen").has_value(),
                   "referenced named selection must not be erased");
  passed &= expect(named.erase("protein_oxygen") == std::nullopt &&
                       named.erase("oxygen") == std::nullopt &&
                       named.list().size() == 3U &&
                       named.list()[1].name == "frozen" &&
                       named.list()[2].name == "moving",
                   "dependency-order erase and deterministic list must work");
  passed &= expect(named.set("1bad", selection::Expression::parse("all").value(),
                             true, *topology)
                       .has_value() &&
                       named.erase("missing").has_value(),
                   "invalid names and missing erase must fail");

  return passed ? 0 : 1;
}
