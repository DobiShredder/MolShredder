#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "molshredder/application/representation_visibility.hpp"

namespace {

bool expect(bool condition, const char *message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

}  // namespace

int main() {
  using molshredder::application::RepresentationVisibilityMutation;
  using molshredder::application::RepresentationVisibilityState;
  using molshredder::render::RepresentationKind;

  bool passed = true;
  auto created = RepresentationVisibilityState::create(70U);
  passed &= expect(created.has_value(), "visibility state must be creatable");
  auto state = std::move(created.value());

  std::array<std::uint8_t, 70U> first{};
  first[0] = 1U;
  first[2] = 1U;
  first[65] = 1U;
  passed &= expect(
      !state.apply(RepresentationKind::lines, first,
                   RepresentationVisibilityMutation::show)
           .has_value() &&
          state.visible_count(RepresentationKind::lines).value() == 3U,
      "selection-local show must set only selected atoms across words");

  std::array<std::uint8_t, 70U> second{};
  second[2] = 1U;
  second[3] = 1U;
  passed &= expect(
      !state.apply(RepresentationKind::spheres, second,
                   RepresentationVisibilityMutation::show)
           .has_value() &&
          state.visible_count(RepresentationKind::lines).value() == 3U &&
          state.visible_count(RepresentationKind::spheres).value() == 2U,
      "additive show must preserve other representation masks");

  passed &= expect(
      !state.apply(RepresentationKind::lines, second,
                   RepresentationVisibilityMutation::hide)
           .has_value() &&
          state.visible_count(RepresentationKind::lines).value() == 2U &&
          !state.visible(RepresentationKind::lines, 2U).value(),
      "selection-local hide must clear only overlapping target bits");

  const auto before_object_disable = state.snapshot();
  passed &= expect(
      state.visible(RepresentationKind::lines, 0U).value() &&
          !state.effectively_visible(RepresentationKind::lines, 0U, false)
               .value() &&
          state.effectively_visible(RepresentationKind::lines, 0U, true)
              .value() &&
          state.snapshot() == before_object_disable,
      "object enabled state must gate effective visibility without mutating representation state");

  std::array<std::uint8_t, 70U> exclusive{};
  exclusive[4] = 1U;
  exclusive[69] = 1U;
  passed &= expect(
      !state.apply(RepresentationKind::sticks, exclusive,
                   RepresentationVisibilityMutation::exclusive)
           .has_value() &&
          state.visible_count(RepresentationKind::sticks).value() == 2U &&
          state.visible_count(RepresentationKind::lines).value() == 2U &&
          state.visible_count(RepresentationKind::spheres).value() == 2U,
      "exclusive mutation must clear competing representations only inside the selection");
  const auto sticks_mask = state.selection_mask(RepresentationKind::sticks);
  passed &= expect(
      sticks_mask.has_value() && sticks_mask.value() ==
                                     std::vector<std::uint8_t>(exclusive.begin(),
                                                               exclusive.end()),
      "visibility state must expose an exact renderable selection mask");

  passed &= expect(
      !state.apply(RepresentationKind::sticks, exclusive,
                   RepresentationVisibilityMutation::toggle)
           .has_value() &&
          state.visible_count(RepresentationKind::sticks).value() == 0U &&
          !state.apply(RepresentationKind::sticks, exclusive,
                       RepresentationVisibilityMutation::toggle)
               .has_value() &&
          state.visible_count(RepresentationKind::sticks).value() == 2U,
      "toggle must turn the full selected mask off when any bit is on and on when all are off");

  const auto stable = state.snapshot();
  std::array<std::uint8_t, 69U> wrong_size{};
  auto invalid_values = exclusive;
  invalid_values[5] = 2U;
  passed &= expect(
      state.apply(RepresentationKind::lines, wrong_size,
                  RepresentationVisibilityMutation::show)
              .has_value() &&
          state.apply(RepresentationKind::lines, invalid_values,
                      RepresentationVisibilityMutation::show)
              .has_value() &&
          state.apply(static_cast<RepresentationKind>(99), exclusive,
                      RepresentationVisibilityMutation::show)
              .has_value() &&
          state.snapshot() == stable,
      "invalid mutation must preserve the previous snapshot");

  const auto serialized =
      molshredder::application::serialize_representation_visibility(stable);
  const auto parsed =
      serialized.has_value()
          ? molshredder::application::parse_representation_visibility(
                serialized.value())
          : molshredder::operation::Result<
                molshredder::application::RepresentationVisibilitySnapshot>::
                failure(serialized.error());
  const auto restored =
      parsed.has_value()
          ? RepresentationVisibilityState::restore(parsed.value())
          : molshredder::operation::Result<RepresentationVisibilityState>::
                failure(parsed.error());
  passed &= expect(
      serialized.has_value() && parsed.has_value() && restored.has_value() &&
          parsed.value() == stable && restored.value().snapshot() == stable &&
          serialized.value().starts_with(
              "molshredder-representation-visibility 1\natoms 70\n"),
      "representation visibility session fragment must round-trip exactly");

  std::vector<std::uint64_t> session_atom_ids(70U);
  for (std::size_t index = 0; index < session_atom_ids.size(); ++index)
    session_atom_ids[index] = static_cast<std::uint64_t>(index + 1U);
  const molshredder::application::RepresentationVisibilitySessionSnapshot
      session_snapshot{2U, 42U, 7U, session_atom_ids, stable};
  const auto session_serialized =
      molshredder::application::serialize_representation_visibility_session(
          session_snapshot);
  const auto session_parsed =
      session_serialized.has_value()
          ? molshredder::application::parse_representation_visibility_session(
                session_serialized.value())
          : molshredder::operation::Result<
                molshredder::application::
                    RepresentationVisibilitySessionSnapshot>::failure(
                session_serialized.error());
  passed &= expect(
      session_serialized.has_value() && session_parsed.has_value() &&
          session_parsed.value() == session_snapshot &&
          session_serialized.value().starts_with(
              "molshredder-representation-visibility-session 2\nobject 42\n"
              "topology 7\natom-ids 70 1 2 3"),
      "session envelope must round-trip object identity and atom-local representation identity");
  auto stale_identity = session_snapshot;
  stale_identity.atom_ids[0] = stale_identity.atom_ids[1];
  auto duplicate_identity_text = session_serialized.value();
  duplicate_identity_text.replace(
      duplicate_identity_text.find("atom-ids 70 1 2"),
      std::string{"atom-ids 70 1 2"}.size(), "atom-ids 70 2 2");
  passed &= expect(
      !molshredder::application::parse_representation_visibility_session(
           duplicate_identity_text)
           .has_value() &&
          !molshredder::application::serialize_representation_visibility_session(
               stale_identity)
               .has_value(),
      "duplicate stable atom identities must be rejected");

  auto invalid_tail = stable;
  invalid_tail.masks[0].back() |= std::uint64_t{1U} << 63U;
  passed &= expect(
      !RepresentationVisibilityState::restore(invalid_tail).has_value() &&
          !molshredder::application::parse_representation_visibility(
               "molshredder-representation-visibility 2\natoms 0\n"
               "lines\nsticks\nspheres\nribbon\ncartoon\n")
               .has_value() &&
          !molshredder::application::parse_representation_visibility(
               serialized.value() + "extra\n")
               .has_value(),
      "schema, tail bits and trailing session data must be rejected");

  molshredder::model::TopologyBuilder topology_builder;
  const auto residue = topology_builder.add_residue(
      {"UNK", 1, "", "A", ""});
  for (std::size_t index = 0; index < 70U; ++index) {
    static_cast<void>(topology_builder.add_atom(
        {"C", 6U, residue.value(), "", 0, std::nullopt}));
  }
  const auto source_topology = topology_builder.build();
  auto target_builder =
      molshredder::model::TopologyBuilder::from(*source_topology.value());
  const std::array retained_ids{molshredder::model::AtomId{70U},
                                molshredder::model::AtomId{5U},
                                molshredder::model::AtomId{1U}};
  static_cast<void>(target_builder.retain_atoms(retained_ids));
  const auto target_topology = target_builder.build();
  const auto topology_remap = molshredder::model::remap_topology(
      *source_topology.value(), *target_topology.value());
  const auto remapped_visibility = state.remap(topology_remap);
  passed &= expect(
      remapped_visibility.has_value() &&
          remapped_visibility.value().atom_count() == 3U &&
          remapped_visibility.value()
              .visible(RepresentationKind::sticks, 0U)
              .value() &&
          remapped_visibility.value()
              .visible(RepresentationKind::sticks, 1U)
              .value() &&
          !remapped_visibility.value()
               .visible(RepresentationKind::lines, 1U)
               .value() &&
          remapped_visibility.value()
              .visible(RepresentationKind::lines, 2U)
              .value(),
      "representation visibility must follow surviving stable IDs across reorder and deletion");

  return passed ? 0 : 1;
}
