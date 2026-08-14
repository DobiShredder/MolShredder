#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/render/representation.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

struct Fixture {
  std::shared_ptr<const molshredder::model::Topology> topology;
  std::shared_ptr<const molshredder::model::CoordinateFrame> frame;
  std::vector<molshredder::render::AtomVisual> visuals;
};

Fixture make_fixture() {
  using namespace molshredder;
  model::TopologyBuilder builder;
  const auto residue =
      builder.add_residue(model::ResidueRecord{"GLY", 1, "", "A", ""});
  const auto first = builder.add_atom(
      model::AtomRecord{"N", 7, residue.value(), "", 0, 1});
  const auto second = builder.add_atom(
      model::AtomRecord{"CA", 6, residue.value(), "", 0, 2});
  const auto third = builder.add_atom(
      model::AtomRecord{"O", 8, residue.value(), "", 0, 3});
  static_cast<void>(builder.add_bond(
      model::Bond{first.value(), second.value(), model::BondOrder::single}));
  static_cast<void>(builder.add_bond(
      model::Bond{second.value(), third.value(), model::BondOrder::double_bond}));
  auto topology = builder.build().value();
  auto frame = model::CoordinateFrame::create(
                   model::CoordinateBuffer{std::vector<model::Vec3f>{
                       {-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F},
                       {3.0F, 0.0F, 0.0F}}},
                   std::nullopt, {1U, 1U, 0U})
                   .value();
  return Fixture{std::move(topology), std::move(frame),
                 {{{0.2F, 0.4F, 1.0F, 1.0F}, 1.4},
                  {{0.5F, 0.5F, 0.5F, 1.0F}, 1.7},
                  {{1.0F, 0.1F, 0.1F, 1.0F}, 1.5}}};
}

Fixture make_backbone_fixture(bool split_chain = false,
                              bool nanometer = false) {
  using namespace molshredder;
  model::TopologyBuilder builder;
  std::vector<model::Vec3f> positions;
  std::vector<render::AtomVisual> visuals;
  for (std::size_t index = 0; index < 4U; ++index) {
    const auto chain = split_chain && index >= 2U ? "B" : "A";
    const auto residue = builder.add_residue(model::ResidueRecord{
        "ALA", static_cast<std::int64_t>(index + 1U), "", chain, ""});
    const auto ca = builder.add_atom(
        model::AtomRecord{"CA", 6, residue.value(), "", 0,
                          static_cast<std::int64_t>(index * 2U + 1U)});
    static_cast<void>(builder.add_atom(
        model::AtomRecord{"O", 8, residue.value(), "", 0,
                          static_cast<std::int64_t>(index * 2U + 2U)}));
    const auto scale = nanometer ? 0.1F : 1.0F;
    positions.push_back({static_cast<float>(index) * 3.7F * scale,
                         static_cast<float>(index % 2U) * 0.4F * scale,
                         0.0F});
    positions.push_back({static_cast<float>(index) * 3.7F * scale,
                         static_cast<float>(index % 2U) * 0.4F * scale,
                         1.0F * scale});
    visuals.push_back({{0.2F, 0.6F, 0.9F, 1.0F}, 1.7});
    visuals.push_back({{0.9F, 0.2F, 0.2F, 1.0F}, 1.5});
    static_cast<void>(ca);
  }
  auto metadata = model::FrameMetadata{};
  if (nanometer) metadata.coordinate_unit = operation::LengthUnit::nanometer;
  auto topology = builder.build().value();
  auto frame = model::CoordinateFrame::create(
                   model::CoordinateBuffer{std::move(positions)}, std::nullopt,
                   {}, std::move(metadata))
                   .value();
  return Fixture{std::move(topology), std::move(frame), std::move(visuals)};
}

molshredder::render::RepresentationRequest request_for(
    const Fixture& fixture) {
  return molshredder::render::RepresentationRequest{
      fixture.topology.get(), fixture.frame.get(), 7U, 42U,
      fixture.visuals, {}, {}, {}};
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  const auto fixture = make_fixture();

  auto request = request_for(fixture);
  request.style.kind = render::RepresentationKind::spheres;
  request.style.sphere_scale = 2.0;
  const auto spheres = render::build_representation(request);
  passed &= expect(spheres.has_value() && spheres.value().spheres.size() == 2U &&
                       spheres.value().lines.empty() &&
                       spheres.value().cylinders.empty(),
                   "sphere representation must emit present atoms only");
  if (spheres.has_value()) {
    passed &= expect(spheres.value().topology_version == 1U &&
                         spheres.value().frame_index == 7U &&
                         spheres.value().scene_node_id == 42U &&
                         spheres.value().spheres[0].radius == 2.8 &&
                         spheres.value().pick_targets.at(1U).atom ==
                             model::AtomIndex{0U} &&
                         spheres.value().pick_targets.at(2U).atom ==
                             model::AtomIndex{1U},
                     "sphere packet must preserve identity, scale, and picks");
    passed &= expect(!spheres.value().bounds.empty &&
                         spheres.value().bounds.minimum.x == -3.8 &&
                         spheres.value().bounds.maximum.x == 4.4,
                     "sphere bounds must include scaled radii");
  }

  request = request_for(fixture);
  request.style.kind = render::RepresentationKind::lines;
  request.style.line_width_pixels = 3.0F;
  const auto lines = render::build_representation(request);
  passed &= expect(lines.has_value() && lines.value().lines.size() == 2U &&
                       lines.value().pick_targets.size() == 1U &&
                       lines.value().lines[0].end == model::Vec3d{} &&
                       lines.value().lines[1].start == model::Vec3d{} &&
                       lines.value().lines[0].pick_id ==
                           lines.value().lines[1].pick_id &&
                       lines.value().pick_targets.at(1U).bond_index == 0U,
                   "bond lines must split at midpoint and share one pick");

  request.style.kind = render::RepresentationKind::sticks;
  request.style.stick_radius = 0.25;
  const auto sticks = render::build_representation(request);
  passed &= expect(sticks.has_value() && sticks.value().cylinders.size() == 2U &&
                       sticks.value().cylinders[0].radius == 0.25 &&
                       sticks.value().cylinders[0].color ==
                           fixture.visuals[0].color &&
                       sticks.value().cylinders[1].color ==
                           fixture.visuals[1].color,
                   "sticks must emit atom-colored half cylinders");

  const std::vector<std::uint8_t> selected{1U, 0U, 1U};
  request = request_for(fixture);
  request.style.kind = render::RepresentationKind::lines;
  request.selected = selected;
  const auto filtered = render::build_representation(request);
  passed &= expect(filtered.has_value() && filtered.value().lines.empty() &&
                       filtered.value().pick_targets.empty() &&
                       filtered.value().bounds.empty,
                   "bond representations must require both endpoints selected");

  auto invalid_request = request_for(fixture);
  invalid_request.topology = nullptr;
  passed &= expect(!render::build_representation(invalid_request).has_value(),
                   "null topology must fail");
  invalid_request = request_for(fixture);
  invalid_request.atom_visuals =
      std::span<const render::AtomVisual>{fixture.visuals.data(), 2U};
  passed &= expect(!render::build_representation(invalid_request).has_value(),
                   "visual count mismatch must fail");
  auto invalid_visuals = fixture.visuals;
  invalid_visuals[0].color.red =
      std::numeric_limits<float>::quiet_NaN();
  invalid_request = request_for(fixture);
  invalid_request.atom_visuals = invalid_visuals;
  passed &= expect(!render::build_representation(invalid_request).has_value(),
                   "non-finite color must fail");
  invalid_request = request_for(fixture);
  invalid_request.style.stick_radius = 0.0;
  passed &= expect(!render::build_representation(invalid_request).has_value(),
                   "non-positive dimensions must fail");

  const auto backbone = make_backbone_fixture();
  auto cartoon_request = request_for(backbone);
  cartoon_request.style.kind = render::RepresentationKind::cartoon;
  cartoon_request.style.backbone_samples_per_residue = 2U;
  const std::vector<analysis::SecondaryStructureState> helix_states(
      4U, analysis::SecondaryStructureState::alpha_helix);
  cartoon_request.secondary_structure = helix_states;
  const auto cartoon = render::build_representation(cartoon_request);
  passed &= expect(cartoon.has_value() &&
                       cartoon.value().mesh_vertices.size() == 28U &&
                       cartoon.value().mesh_triangles.size() == 48U &&
                       cartoon.value().pick_targets.size() == 4U &&
                       cartoon.value().pick_targets.at(1U).kind ==
                           render::PickKind::residue &&
                       cartoon.value().pick_targets.at(1U).residue ==
                           model::ResidueIndex{0U} &&
                       cartoon.value().provenance.at(
                           "secondary_structure_method") ==
                           "caller-supplied" &&
                       !cartoon.value().bounds.empty,
                   "cartoon must emit smooth residue-pickable backbone mesh");
  if (cartoon.has_value()) {
    for (std::size_t ring = 1U; ring < 7U; ++ring) {
      passed &= expect(
          scene::dot(cartoon.value().mesh_vertices[(ring - 1U) * 4U].normal,
                     cartoon.value().mesh_vertices[ring * 4U].normal) > 0.0,
          "transported ribbon frame must not flip between adjacent rings");
    }
  }
  cartoon_request.secondary_structure = {};
  const auto assigned_cartoon = render::build_representation(cartoon_request);
  passed &= expect(
      assigned_cartoon.has_value() &&
          assigned_cartoon.value().provenance.at(
              "secondary_structure_method") ==
              "molshredder-stride-method-v0" &&
          assigned_cartoon.value().provenance.at("exact_stride_parity") ==
              "false",
      "automatic cartoon assignment must preserve method provenance");

  const auto split = make_backbone_fixture(true);
  auto split_request = request_for(split);
  split_request.style.kind = render::RepresentationKind::ribbon;
  split_request.style.backbone_samples_per_residue = 2U;
  split_request.secondary_structure = helix_states;
  const auto split_ribbon = render::build_representation(split_request);
  passed &= expect(split_ribbon.has_value() &&
                       split_ribbon.value().mesh_vertices.size() == 24U &&
                       split_ribbon.value().mesh_triangles.size() == 32U,
                   "ribbon must not bridge distinct chains");

  const auto backbone_nm = make_backbone_fixture(false, true);
  auto nanometer_request = request_for(backbone_nm);
  nanometer_request.style.kind = render::RepresentationKind::cartoon;
  nanometer_request.style.backbone_samples_per_residue = 2U;
  nanometer_request.secondary_structure = helix_states;
  const auto cartoon_nm = render::build_representation(nanometer_request);
  passed &= expect(cartoon.has_value() && cartoon_nm.has_value() &&
                       std::abs(cartoon_nm.value().mesh_vertices[0].position.z -
                                cartoon.value().mesh_vertices[0].position.z *
                                    0.1) < 1.0e-6,
                   "cartoon geometry must preserve physical scale for nm frames");

  return passed ? 0 : 1;
}
