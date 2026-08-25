#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/render/reference_renderer.hpp"
#include "molshredder/render/representation.hpp"
#include "molshredder/render/setting_store.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

bool close(double first, double second, double tolerance = 1.0e-9) {
  return std::abs(first - second) <= tolerance;
}

struct Fixture {
  std::shared_ptr<const molshredder::model::Topology> topology;
  std::shared_ptr<const molshredder::model::CoordinateFrame> frame;
  std::vector<molshredder::render::AtomVisual> visuals;
};

Fixture fixture() {
  using namespace molshredder;
  model::TopologyBuilder builder;
  const auto residue =
      builder.add_residue({"LIG", 1, "", "A", ""}).value();
  const std::vector<model::Vec3f> positions{
      {-4.F, 0.F, 0.F}, {-2.F, 0.F, 0.F},
      {-1.F, 2.F, 0.F}, {1.F, 2.F, 0.F},
      {-1.F, -2.F, 0.F}, {1.F, -2.F, 0.F},
      {2.F, 0.F, 0.F}, {4.F, 0.F, 0.F},
      {6.F, 0.F, 0.F}, {11.F, 0.F, 0.F}};
  std::vector<model::AtomIndex> atoms;
  for (std::size_t index = 0; index < positions.size(); ++index) {
    atoms.push_back(builder.add_atom(
        {index == 0U ? "H" : "C", index == 0U ? std::uint8_t{1U}
                                                   : std::uint8_t{6U},
         residue, "", 0, static_cast<std::int64_t>(index + 1U)})
                        .value());
  }
  const std::vector<model::BondOrder> orders{
      model::BondOrder::single, model::BondOrder::double_bond,
      model::BondOrder::triple, model::BondOrder::zero,
      model::BondOrder::single};
  for (std::size_t index = 0; index < orders.size(); ++index)
    static_cast<void>(builder.add_bond(
        {atoms[index * 2U], atoms[index * 2U + 1U], orders[index]}));
  auto topology = builder.build().value();
  auto frame = model::CoordinateFrame::create(
                   model::CoordinateBuffer{positions})
                   .value();
  std::vector<render::AtomVisual> visuals;
  for (std::size_t index = 0; index < positions.size(); ++index) {
    visuals.push_back({index == 0U
                           ? render::ColorRgba{0.9F, 0.9F, 0.9F, 1.F}
                           : render::ColorRgba{0.3F, 0.3F, 0.3F, 1.F},
                       1.5});
  }
  return {std::move(topology), std::move(frame), std::move(visuals)};
}

molshredder::render::RenderSettingScope global_scope() { return {}; }

molshredder::render::RenderSettingScope object_scope() {
  using enum molshredder::render::RenderSettingScopeLevel;
  return {object, 42U, 0U, 0U, 0U};
}

molshredder::render::RenderSettingScope state_scope() {
  using enum molshredder::render::RenderSettingScopeLevel;
  return {object_state, 42U, 7U, 0U, 0U};
}

molshredder::render::RenderSettingScope atom_scope(std::uint64_t atom) {
  return {molshredder::render::RenderSettingScopeLevel::atom, 42U, 7U, atom,
          0U};
}

molshredder::render::RenderSettingScope bond_scope(std::uint64_t bond) {
  return {molshredder::render::RenderSettingScopeLevel::bond, 42U, 7U, 0U,
          bond};
}

molshredder::render::RepresentationRequest request_for(
    const Fixture& data, const molshredder::render::RenderSettingStore& store,
    molshredder::render::RepresentationKind kind,
    std::span<const std::uint8_t> selected = {}) {
  molshredder::render::RepresentationRequest request{
      data.topology.get(), data.frame.get(), 7U, 99U, data.visuals, selected,
      {}, {}, &store, 42U, {}};
  request.style.kind = kind;
  return request;
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  const auto data = fixture();
  auto store = render::RenderSettingStore::create().value();

  passed &= expect(!store.set("line_width", global_scope(), 2.0) &&
                       !store.set("line_width", object_scope(), 3.0) &&
                       !store.set("line_width", state_scope(), 4.0) &&
                       !store.set("line_width", atom_scope(3U), 5.0) &&
                       !store.set("line_width", bond_scope(1U), 6.0) &&
                       !store.set("line_color", atom_scope(3U),
                                  std::string{"red"}),
                   "line precedence fixture settings must be accepted");
  const auto lines = render::build_representation(
      request_for(data, store, render::RepresentationKind::lines));
  passed &= expect(lines.has_value() && lines.value().lines.size() == 22U,
                   "single/double/triple/zero/long bonds must emit 2/4/6/8/2 line segments");
  if (lines.has_value()) {
    passed &= expect(lines.value().lines[0].width_pixels == 6.F &&
                         lines.value().lines[1].width_pixels == 6.F,
                     "bond override must beat atom/state/object/global width");
    passed &= expect(lines.value().lines[2].width_pixels == 5.F &&
                         lines.value().lines[3].width_pixels == 4.F &&
                         lines.value().lines[2].start_color.red > 0.9F,
                     "atom override must affect only its bond half before state fallback");
    passed &= expect(lines.value().pick_targets.size() == 5U,
                     "all line segments for a bond must retain one bond pick target");
    passed &= expect(
        lines.value().provenance.at("valence_fancy_fallback_count") == "2",
        "isolated multiple bonds must record deterministic fancy-to-simple layout fallback");
  }

  auto hidden_store = store;
  passed &= expect(!hidden_store.set("hide_long_bonds", state_scope(), true),
                   "long-bond setting must be accepted");
  const auto hidden = render::build_representation(
      request_for(data, hidden_store, render::RepresentationKind::lines));
  passed &= expect(hidden.has_value() && hidden.value().lines.size() == 20U &&
                       hidden.value().pick_targets.size() == 4U,
                   "hide_long_bonds must use 0.9 times summed atom radii as oracle cutoff");

  auto half_store = render::RenderSettingStore::create().value();
  passed &= expect(!half_store.set("half_bonds", state_scope(), true),
                   "half-bond setting must be accepted");
  std::vector<std::uint8_t> one_atom(data.visuals.size(), 0U);
  one_atom[0] = 1U;
  const auto half = render::build_representation(request_for(
      data, half_store, render::RepresentationKind::lines, one_atom));
  passed &= expect(half.has_value() && half.value().lines.size() == 1U &&
                       half.value().lines[0].start ==
                           model::Vec3d{-4.0, 0.0, 0.0} &&
                       half.value().lines[0].end ==
                           model::Vec3d{-3.0, 0.0, 0.0},
                   "half_bonds must emit only the selected endpoint half");

  auto stick_store = render::RenderSettingStore::create().value();
  passed &= expect(!stick_store.set("stick_radius", state_scope(), 0.3) &&
                       !stick_store.set("stick_radius", bond_scope(1U), 0.5) &&
                       !stick_store.set("stick_h_scale", state_scope(), 1.0) &&
                       !stick_store.set("stick_transparency", bond_scope(1U),
                                        0.25) &&
                       !stick_store.set("stick_color", bond_scope(1U),
                                        std::string{"#00ff00"}) &&
                       !stick_store.set("stick_ball", atom_scope(1U), true) &&
                       !stick_store.set("stick_ball_ratio", state_scope(), 2.0),
                   "stick style and ball settings must be accepted");
  std::vector<std::uint8_t> first_bond(data.visuals.size(), 0U);
  first_bond[0] = first_bond[1] = 1U;
  const auto sticks = render::build_representation(request_for(
      data, stick_store, render::RepresentationKind::sticks, first_bond));
  passed &= expect(sticks.has_value() && sticks.value().cylinders.size() == 2U &&
                       sticks.value().spheres.size() == 1U,
                   "ball-and-stick must emit two colored halves and one selected atom ball");
  if (sticks.has_value()) {
    passed &= expect(close(sticks.value().cylinders[0].radius, 0.5) &&
                         close(sticks.value().cylinders[1].radius, 0.5) &&
                         close(sticks.value().spheres[0].radius, 1.0) &&
                         close(sticks.value().cylinders[0].color.alpha, 0.75) &&
                         sticks.value().cylinders[0].color.green == 1.F,
                     "stick radius, transparency, color and ball ratio must resolve exactly");
  }

  auto sphere_store = render::RenderSettingStore::create().value();
  passed &= expect(!sphere_store.set("sphere_scale", state_scope(), 2.0) &&
                       !sphere_store.set("sphere_scale", atom_scope(1U), 3.0) &&
                       !sphere_store.set("sphere_transparency", atom_scope(1U),
                                         0.5) &&
                       !sphere_store.set("sphere_color", atom_scope(1U),
                                         std::string{"oxygen"}) &&
                       !sphere_store.set("sphere_mode", state_scope(),
                                         std::int64_t{5}),
                   "sphere appearance and legacy mode settings must be accepted");
  std::vector<std::uint8_t> first_two(data.visuals.size(), 0U);
  first_two[0] = first_two[1] = 1U;
  const auto spheres = render::build_representation(request_for(
      data, sphere_store, render::RepresentationKind::spheres, first_two));
  passed &= expect(spheres.has_value() && spheres.value().spheres.size() == 2U &&
                       close(spheres.value().spheres[0].radius, 4.5) &&
                       close(spheres.value().spheres[1].radius, 3.0) &&
                       close(spheres.value().spheres[0].color.alpha, 0.5) &&
                       spheres.value().spheres[0].color.red > 0.9F &&
                       spheres.value().provenance.at("sphere_mode_requested") ==
                           "5" &&
                       spheres.value().provenance.at("sphere_mode_effective") ==
                           "9" &&
                       spheres.value().provenance.at("sphere_mode_fallback") ==
                           "true",
                   "sphere scope, transparency/color and legacy fallback must be explicit");

  if (spheres.has_value()) {
    auto parameters = scene::Camera::create().value().parameters();
    parameters.projection = scene::ProjectionMode::orthographic;
    parameters.orthographic_height = 14.0;
    parameters.aspect_ratio = 1.0;
    parameters.distance = 16.0;
    const auto camera = scene::Camera::create(parameters).value();
    render::ReferenceRenderSettings image_settings;
    image_settings.width = 64U;
    image_settings.height = 64U;
    const auto first_image =
        render::render_reference(spheres.value(), camera, image_settings);
    const auto second_image =
        render::render_reference(spheres.value(), camera, image_settings);
    passed &= expect(first_image.has_value() && second_image.has_value() &&
                         render::image_checksum(first_image.value()) ==
                             render::image_checksum(second_image.value()) &&
                         render::image_checksum(first_image.value()) != 0U &&
                         first_image.value().pick_ids ==
                             second_image.value().pick_ids,
                     "fallback sphere image and picking must be deterministic");
  }

  const auto point_supported = render::resolve_sphere_mode(
      3, true, {true, false, true, false});
  const auto point_fallback = render::resolve_sphere_mode(3, true, {});
  passed &= expect(point_supported.effective == 3 && !point_supported.fallback &&
                       point_fallback.effective == 9 &&
                       point_fallback.fallback,
                   "sphere mode negotiation must distinguish native and fallback paths");
  return passed ? 0 : 1;
}
