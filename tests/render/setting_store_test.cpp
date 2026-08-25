#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "molshredder/render/setting_store.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

double number(const molshredder::render::ResolvedRenderSetting &resolved) {
  return std::get<double>(resolved.value);
}

}  // namespace

int main() {
  using namespace molshredder::render;
  bool passed = true;
  auto created = RenderSettingStore::create();
  passed &= expect(created.has_value(), "P0 setting store must be creatable");
  auto store = std::move(created.value());
  const auto &definitions = p0_render_setting_definitions();
  constexpr std::array expected_catalog{
      std::pair{43U, "line_smooth"},
      std::pair{44U, "line_width"},
      std::pair{110U, "line_radius"},
      std::pair{391U, "line_stick_helper"},
      std::pair{526U, "line_color"},
      std::pair{645U, "line_use_shader"},
      std::pair{679U, "line_as_cylinders"},
      std::pair{21U, "stick_radius"},
      std::pair{46U, "stick_quality"},
      std::pair{47U, "stick_overlap"},
      std::pair{48U, "stick_nub"},
      std::pair{198U, "stick_transparency"},
      std::pair{276U, "stick_ball"},
      std::pair{277U, "stick_ball_ratio"},
      std::pair{278U, "stick_fixed_radius"},
      std::pair{376U, "stick_color"},
      std::pair{512U, "stick_valence_scale"},
      std::pair{604U, "stick_ball_color"},
      std::pair{605U, "stick_h_scale"},
      std::pair{644U, "stick_use_shader"},
      std::pair{673U, "stick_debug"},
      std::pair{675U, "stick_round_nub"},
      std::pair{676U, "stick_good_geometry"},
      std::pair{677U, "stick_as_cylinders"},
      std::pair{87U, "sphere_quality"},
      std::pair{155U, "sphere_scale"},
      std::pair{172U, "sphere_transparency"},
      std::pair{173U, "sphere_color"},
      std::pair{203U, "sphere_solvent"},
      std::pair{421U, "sphere_mode"},
      std::pair{422U, "sphere_point_max_size"},
      std::pair{423U, "sphere_point_size"},
      std::pair{646U, "sphere_use_shader"},
      std::pair{45U, "half_bonds"},
      std::pair{560U, "hide_long_bonds"},
      std::pair{64U, "valence"},
      std::pair{135U, "valence_size"},
      std::pair{616U, "valence_mode"},
      std::pair{752U, "valence_zero_scale"},
      std::pair{753U, "valence_zero_mode"},
      std::pair{51U, "auto_show_lines"},
      std::pair{72U, "auto_show_nonbonded"},
      std::pair{420U, "auto_show_spheres"}};
  bool catalog_exact = definitions.size() == expected_catalog.size();
  for (std::size_t index = 0; index < expected_catalog.size(); ++index) {
    catalog_exact &= definitions[index].stable_id == expected_catalog[index].first &&
                     definitions[index].name == expected_catalog[index].second;
  }
  passed &= expect(
      catalog_exact && store.definition("line_width") != nullptr &&
          store.definition("line_width")->stable_id == 44U &&
          store.definition("sphere_mode")->stable_id == 421U &&
          store.definition("valence_zero_mode")->maximum == 2.0,
      "catalog must expose all 43 pinned setting definitions and stable IDs");

  const RenderSettingScope global{};
  const RenderSettingScope object{RenderSettingScopeLevel::object, 7U};
  const RenderSettingScope object_state{RenderSettingScopeLevel::object_state,
                                        7U, 2U};
  const RenderSettingScope atom{RenderSettingScopeLevel::atom, 7U, 2U, 11U};
  const RenderSettingScope bond{RenderSettingScopeLevel::bond, 7U, 2U, 0U,
                                19U};
  const RenderSettingContext context{7U, 2U, 11U, 19U};
  passed &= expect(
      !store.set("line_width", global, 2.0).has_value() &&
          !store.set("line_width", object, 3.0).has_value() &&
          !store.set("line_width", object_state, 4.0).has_value() &&
          !store.set("line_width", atom, 5.0).has_value() &&
          !store.set("line_width", bond, 6.0).has_value() &&
          number(store.resolve("line_width", context).value()) == 6.0,
      "resolution must prefer bond over atom, state, object and global");
  passed &= expect(
      store.unset("line_width", bond).value() &&
          number(store.resolve("line_width", context).value()) == 5.0 &&
          store.unset("line_width", atom).value() &&
          number(store.resolve("line_width", context).value()) == 4.0 &&
          store.unset("line_width", object_state).value() &&
          number(store.resolve("line_width", context).value()) == 3.0 &&
          store.unset("line_width", object).value() &&
          number(store.resolve("line_width", context).value()) == 2.0 &&
          store.unset("line_width", global).value() &&
          std::abs(number(store.resolve("line_width", context).value()) -
                   1.49) < 1.0e-15,
      "unset must expose the next inherited value and finally the default");

  passed &= expect(
      !store.set("sphere_scale", atom, 1.5).has_value() &&
          !store.set("sphere_transparency", atom, 0.25).has_value() &&
          store.reset_scope(atom).value() == 2U && store.override_count() == 0U,
      "scope reset must remove every override at exactly that identity");

  const auto stable = store.snapshot();
  const RenderSettingScope malformed_atom{RenderSettingScopeLevel::atom, 7U,
                                           2U, 0U};
  passed &= expect(
      store.set("unknown", global, 1.0).has_value() &&
          store.set("line_width", global, true).has_value() &&
          store.set("line_width", global, 0.0).has_value() &&
          store.set("line_width", global,
                    std::numeric_limits<double>::infinity())
              .has_value() &&
          store.set("line_smooth", object, true).has_value() &&
          store.set("sphere_scale", malformed_atom, 1.0).has_value() &&
          store.snapshot() == stable,
      "unknown, type, range, non-finite and invalid-scope updates must be failure-atomic");

  passed &= expect(
      !store.set("line_width", bond, 2.25).has_value() &&
          !store.set("sphere_mode", object_state, std::int64_t{9})
               .has_value() &&
          !store.set("sphere_color", atom, std::string{"oxygen"})
               .has_value(),
      "number, integer and color values must accept their declared types");
  const auto snapshot = store.snapshot();
  const auto serialized = serialize_render_settings(snapshot);
  const auto parsed = serialized.has_value()
                          ? parse_render_settings(serialized.value())
                          : molshredder::operation::Result<
                                RenderSettingSnapshot>::failure(
                                serialized.error());
  const auto restored = parsed.has_value()
                            ? RenderSettingStore::restore(parsed.value())
                            : molshredder::operation::Result<
                                  RenderSettingStore>::failure(parsed.error());
  passed &= expect(
      serialized.has_value() && parsed.has_value() && restored.has_value() &&
          parsed.value() == snapshot &&
          restored.value().snapshot() == snapshot &&
          serialized.value().starts_with(
              "molshredder-render-settings 2\ncatalog \"pymol-oss-3.1.0-p0-v1\"\ncount 3\n"),
      "setting snapshot must serialize, parse and restore exactly");
  passed &= expect(
      !parse_render_settings(serialized.value() + "extra\n").has_value() &&
          !parse_render_settings(
               "molshredder-render-settings 3\ncatalog \"bad\"\ncount 0\n")
               .has_value(),
      "trailing data and unknown schema/catalog must be rejected");

  return passed ? 0 : 1;
}
