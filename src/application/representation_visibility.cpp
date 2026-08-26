#include "molshredder/application/representation_visibility.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

namespace molshredder::application {
namespace {

constexpr std::array<std::string_view, kRepresentationKindCount> kKindNames{
    "lines", "sticks", "spheres", "ribbon", "cartoon"};

operation::Error invalid(std::string message) {
  return {operation::ErrorCode::invalid_argument, std::move(message), {}};
}

operation::Result<std::size_t> kind_index(render::RepresentationKind kind) {
  switch (kind) {
  case render::RepresentationKind::lines:
    return operation::Result<std::size_t>::success(0U);
  case render::RepresentationKind::sticks:
    return operation::Result<std::size_t>::success(1U);
  case render::RepresentationKind::spheres:
    return operation::Result<std::size_t>::success(2U);
  case render::RepresentationKind::ribbon:
    return operation::Result<std::size_t>::success(3U);
  case render::RepresentationKind::cartoon:
    return operation::Result<std::size_t>::success(4U);
  }
  return operation::Result<std::size_t>::failure(
      invalid("unknown representation kind"));
}

std::size_t word_count(std::size_t atom_count) {
  return atom_count / 64U + (atom_count % 64U == 0U ? 0U : 1U);
}

std::uint64_t final_word_mask(std::size_t atom_count) {
  const auto remainder = atom_count % 64U;
  return remainder == 0U ? std::numeric_limits<std::uint64_t>::max()
                         : (std::uint64_t{1U} << remainder) - 1U;
}

std::optional<operation::Error>
validate_snapshot(const RepresentationVisibilitySnapshot &snapshot) {
  if (snapshot.schema_version != kRepresentationVisibilitySchemaVersion) {
    return invalid("unsupported representation visibility schema version");
  }
  const auto expected_words = word_count(snapshot.atom_count);
  for (std::size_t kind = 0; kind < snapshot.masks.size(); ++kind) {
    if (snapshot.masks[kind].size() != expected_words) {
      return invalid("representation visibility word count mismatch for " +
                     std::string{kKindNames[kind]});
    }
    if (expected_words != 0U &&
        (snapshot.masks[kind].back() &
         ~final_word_mask(snapshot.atom_count)) != 0U) {
      return invalid("representation visibility has bits beyond atom count");
    }
  }
  return std::nullopt;
}

operation::Result<std::size_t> parse_size(std::string_view token,
                                          std::string_view field) {
  std::size_t value{};
  const auto result =
      molshredder::core::from_chars(token.data(), token.data() + token.size(), value);
  if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
    return operation::Result<std::size_t>::failure(
        invalid("invalid representation visibility " + std::string{field}));
  }
  return operation::Result<std::size_t>::success(value);
}

operation::Result<std::uint64_t> parse_uint64(std::string_view token,
                                              std::string_view field) {
  std::uint64_t value{};
  const auto result =
      molshredder::core::from_chars(token.data(), token.data() + token.size(), value);
  if (result.ec != std::errc{} || result.ptr != token.data() + token.size())
    return operation::Result<std::uint64_t>::failure(
        invalid("invalid representation visibility " + std::string{field}));
  return operation::Result<std::uint64_t>::success(value);
}

}  // namespace

RepresentationVisibilityState::RepresentationVisibilityState(
    std::size_t atom_count)
    : atom_count_(atom_count) {
  const auto words = word_count(atom_count);
  for (auto &mask : masks_)
    mask.assign(words, 0U);
}

operation::Result<RepresentationVisibilityState>
RepresentationVisibilityState::create(std::size_t atom_count) {
  return operation::Result<RepresentationVisibilityState>::success(
      RepresentationVisibilityState{atom_count});
}

operation::Result<RepresentationVisibilityState>
RepresentationVisibilityState::restore(
    const RepresentationVisibilitySnapshot &snapshot) {
  if (const auto error = validate_snapshot(snapshot); error.has_value()) {
    return operation::Result<RepresentationVisibilityState>::failure(*error);
  }
  RepresentationVisibilityState state{snapshot.atom_count};
  state.masks_ = snapshot.masks;
  return operation::Result<RepresentationVisibilityState>::success(
      std::move(state));
}

operation::Result<RepresentationVisibilityState>
RepresentationVisibilityState::remap(const model::TopologyRemap &remap) const {
  if (remap.source_atoms.size() != atom_count_) {
    return operation::Result<RepresentationVisibilityState>::failure(
        invalid("representation visibility topology remap size mismatch"));
  }
  RepresentationVisibilityState target{remap.target_atoms.size()};
  for (std::size_t source = 0; source < remap.source_atoms.size(); ++source) {
    const auto mapped = remap.source_atoms[source];
    if (!mapped.has_value())
      continue;
    if (mapped->value >= target.atom_count_) {
      return operation::Result<RepresentationVisibilityState>::failure(
          invalid("representation visibility topology remap is out of range"));
    }
    for (std::size_t kind = 0; kind < masks_.size(); ++kind) {
      const auto source_set =
          (masks_[kind][source / 64U] &
           (std::uint64_t{1U} << (source % 64U))) != 0U;
      if (source_set) {
        target.masks_[kind][mapped->value / 64U] |=
            std::uint64_t{1U} << (mapped->value % 64U);
      }
    }
  }
  return operation::Result<RepresentationVisibilityState>::success(
      std::move(target));
}

operation::Result<std::size_t> RepresentationVisibilityState::visible_count(
    render::RepresentationKind kind) const {
  const auto index = kind_index(kind);
  if (!index.has_value())
    return operation::Result<std::size_t>::failure(index.error());
  std::size_t count{};
  for (const auto word : masks_[index.value()])
    count += static_cast<std::size_t>(std::popcount(word));
  return operation::Result<std::size_t>::success(count);
}

operation::Result<bool> RepresentationVisibilityState::visible(
    render::RepresentationKind kind, std::size_t atom_index) const {
  if (atom_index >= atom_count_) {
    return operation::Result<bool>::failure(
        invalid("representation visibility atom index is out of range"));
  }
  const auto index = kind_index(kind);
  if (!index.has_value())
    return operation::Result<bool>::failure(index.error());
  const auto word = atom_index / 64U;
  const auto bit = atom_index % 64U;
  return operation::Result<bool>::success(
      (masks_[index.value()][word] & (std::uint64_t{1U} << bit)) != 0U);
}

operation::Result<bool> RepresentationVisibilityState::effectively_visible(
    render::RepresentationKind kind, std::size_t atom_index,
    bool object_enabled) const {
  const auto local = visible(kind, atom_index);
  if (!local.has_value())
    return local;
  return operation::Result<bool>::success(object_enabled && local.value());
}

operation::Result<std::vector<std::uint8_t>>
RepresentationVisibilityState::selection_mask(
    render::RepresentationKind kind) const {
  const auto index = kind_index(kind);
  if (!index.has_value()) {
    return operation::Result<std::vector<std::uint8_t>>::failure(index.error());
  }
  std::vector<std::uint8_t> selection(atom_count_, 0U);
  for (std::size_t atom = 0; atom < atom_count_; ++atom) {
    const auto word = atom / 64U;
    const auto bit = atom % 64U;
    selection[atom] = static_cast<std::uint8_t>(
        (masks_[index.value()][word] >> bit) & std::uint64_t{1U});
  }
  return operation::Result<std::vector<std::uint8_t>>::success(
      std::move(selection));
}

std::optional<operation::Error> RepresentationVisibilityState::apply(
    render::RepresentationKind kind,
    std::span<const std::uint8_t> selection,
    RepresentationVisibilityMutation mutation) {
  const auto index = kind_index(kind);
  if (!index.has_value())
    return index.error();
  if (selection.size() != atom_count_)
    return invalid("representation visibility selection size mismatch");
  if (std::any_of(selection.begin(), selection.end(),
                  [](std::uint8_t value) { return value > 1U; })) {
    return invalid("representation visibility selection must contain only 0 or 1");
  }

  auto next = masks_;
  if (mutation == RepresentationVisibilityMutation::exclusive) {
    for (auto &mask : next) {
      for (std::size_t atom = 0; atom < selection.size(); ++atom) {
        if (selection[atom] != 0U)
          mask[atom / 64U] &= ~(std::uint64_t{1U} << (atom % 64U));
      }
    }
  }
  auto &target = next[index.value()];
  bool toggle_off = false;
  if (mutation == RepresentationVisibilityMutation::toggle) {
    for (std::size_t atom = 0; atom < selection.size(); ++atom) {
      if (selection[atom] != 0U &&
          (target[atom / 64U] &
           (std::uint64_t{1U} << (atom % 64U))) != 0U) {
        toggle_off = true;
        break;
      }
    }
  }
  for (std::size_t atom = 0; atom < selection.size(); ++atom) {
    if (selection[atom] == 0U)
      continue;
    const auto bit = std::uint64_t{1U} << (atom % 64U);
    if (mutation == RepresentationVisibilityMutation::hide || toggle_off)
      target[atom / 64U] &= ~bit;
    else
      target[atom / 64U] |= bit;
  }
  masks_ = std::move(next);
  return std::nullopt;
}

RepresentationVisibilitySnapshot RepresentationVisibilityState::snapshot()
    const {
  return {kRepresentationVisibilitySchemaVersion, atom_count_, masks_};
}

operation::Result<std::string> serialize_representation_visibility(
    const RepresentationVisibilitySnapshot &snapshot) {
  if (const auto error = validate_snapshot(snapshot); error.has_value())
    return operation::Result<std::string>::failure(*error);

  std::ostringstream output;
  output << "molshredder-representation-visibility "
         << snapshot.schema_version << '\n';
  output << "atoms " << snapshot.atom_count << '\n';
  output << std::hex << std::setfill('0');
  for (std::size_t kind = 0; kind < snapshot.masks.size(); ++kind) {
    output << kKindNames[kind];
    for (const auto word : snapshot.masks[kind])
      output << ' ' << std::setw(16) << word;
    output << '\n';
  }
  return operation::Result<std::string>::success(output.str());
}

operation::Result<RepresentationVisibilitySnapshot>
parse_representation_visibility(std::string_view text) {
  std::istringstream input{std::string{text}};
  std::string label;
  std::string token;
  if (!(input >> label >> token) ||
      label != "molshredder-representation-visibility") {
    return operation::Result<RepresentationVisibilitySnapshot>::failure(
        invalid("invalid representation visibility header"));
  }
  const auto schema = parse_size(token, "schema version");
  if (!schema.has_value())
    return operation::Result<RepresentationVisibilitySnapshot>::failure(
        schema.error());
  if (schema.value() > std::numeric_limits<unsigned int>::max()) {
    return operation::Result<RepresentationVisibilitySnapshot>::failure(
        invalid("invalid representation visibility schema version"));
  }
  if (!(input >> label >> token) || label != "atoms") {
    return operation::Result<RepresentationVisibilitySnapshot>::failure(
        invalid("missing representation visibility atom count"));
  }
  const auto atoms = parse_size(token, "atom count");
  if (!atoms.has_value())
    return operation::Result<RepresentationVisibilitySnapshot>::failure(
        atoms.error());

  RepresentationVisibilitySnapshot snapshot;
  snapshot.schema_version = static_cast<unsigned int>(schema.value());
  snapshot.atom_count = atoms.value();
  const auto words = word_count(snapshot.atom_count);
  for (std::size_t kind = 0; kind < snapshot.masks.size(); ++kind) {
    if (!(input >> label) || label != kKindNames[kind]) {
      return operation::Result<RepresentationVisibilitySnapshot>::failure(
          invalid("missing or out-of-order representation visibility kind"));
    }
    snapshot.masks[kind].reserve(words);
    for (std::size_t word = 0; word < words; ++word) {
      if (!(input >> token)) {
        return operation::Result<RepresentationVisibilitySnapshot>::failure(
            invalid("truncated representation visibility mask"));
      }
      std::uint64_t value{};
      const auto parsed = molshredder::core::from_chars(token.data(),
                                          token.data() + token.size(), value,
                                          16);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != token.data() + token.size() || token.size() != 16U) {
        return operation::Result<RepresentationVisibilitySnapshot>::failure(
            invalid("invalid representation visibility mask word"));
      }
      snapshot.masks[kind].push_back(value);
    }
  }
  if (input >> token) {
    return operation::Result<RepresentationVisibilitySnapshot>::failure(
        invalid("unexpected trailing representation visibility data"));
  }
  if (const auto error = validate_snapshot(snapshot); error.has_value())
    return operation::Result<RepresentationVisibilitySnapshot>::failure(*error);
  return operation::Result<RepresentationVisibilitySnapshot>::success(
      std::move(snapshot));
}

operation::Result<std::string> serialize_representation_visibility_session(
    const RepresentationVisibilitySessionSnapshot &snapshot) {
  auto unique_ids = snapshot.atom_ids;
  std::sort(unique_ids.begin(), unique_ids.end());
  if (snapshot.schema_version !=
          kRepresentationVisibilitySessionSchemaVersion ||
      snapshot.object_id == 0U || snapshot.topology_version == 0U ||
      snapshot.atom_ids.size() != snapshot.visibility.atom_count ||
      std::any_of(snapshot.atom_ids.begin(), snapshot.atom_ids.end(),
                  [](std::uint64_t id) { return id == 0U; }) ||
      std::adjacent_find(unique_ids.begin(), unique_ids.end()) !=
          unique_ids.end()) {
    return operation::Result<std::string>::failure(
        invalid("invalid representation visibility session identity"));
  }
  const auto visibility =
      serialize_representation_visibility(snapshot.visibility);
  if (!visibility.has_value())
    return operation::Result<std::string>::failure(visibility.error());
  std::ostringstream output;
  output << "molshredder-representation-visibility-session "
         << snapshot.schema_version << '\n';
  output << "object " << snapshot.object_id << '\n';
  output << "topology " << snapshot.topology_version << '\n';
  output << "atom-ids " << snapshot.atom_ids.size();
  for (const auto id : snapshot.atom_ids)
    output << ' ' << id;
  output << '\n';
  output << visibility.value();
  return operation::Result<std::string>::success(output.str());
}

operation::Result<RepresentationVisibilitySessionSnapshot>
parse_representation_visibility_session(std::string_view text) {
  std::array<std::size_t, 4U> newline{};
  std::size_t offset{};
  for (auto &position : newline) {
    position = text.find('\n', offset);
    if (position == std::string_view::npos) {
      return operation::Result<
          RepresentationVisibilitySessionSnapshot>::failure(
          invalid("truncated representation visibility session"));
    }
    offset = position + 1U;
  }
  std::istringstream header{std::string{text.substr(0U, newline[3])}};
  std::string label;
  std::string token;
  if (!(header >> label >> token) ||
      label != "molshredder-representation-visibility-session") {
    return operation::Result<RepresentationVisibilitySessionSnapshot>::failure(
        invalid("invalid representation visibility session header"));
  }
  const auto schema = parse_size(token, "session schema version");
  if (!schema.has_value() ||
      schema.value() != kRepresentationVisibilitySessionSchemaVersion) {
    return operation::Result<RepresentationVisibilitySessionSnapshot>::failure(
        schema.has_value()
            ? invalid("unsupported representation visibility session schema")
            : schema.error());
  }
  if (!(header >> label >> token) || label != "object") {
    return operation::Result<RepresentationVisibilitySessionSnapshot>::failure(
        invalid("missing representation visibility session object"));
  }
  const auto object = parse_uint64(token, "session object identity");
  if (!object.has_value() || object.value() == 0U) {
    return operation::Result<RepresentationVisibilitySessionSnapshot>::failure(
        object.has_value()
            ? invalid("invalid representation visibility session object")
            : object.error());
  }
  if (!(header >> label >> token) || label != "topology") {
    return operation::Result<RepresentationVisibilitySessionSnapshot>::failure(
        invalid("missing representation visibility topology revision"));
  }
  const auto topology = parse_uint64(token, "topology revision");
  if (!topology.has_value() || topology.value() == 0U) {
    return operation::Result<RepresentationVisibilitySessionSnapshot>::failure(
        topology.has_value() ? invalid("invalid topology revision")
                             : topology.error());
  }
  if (!(header >> label >> token) || label != "atom-ids") {
    return operation::Result<RepresentationVisibilitySessionSnapshot>::failure(
        invalid("missing representation visibility stable atom IDs"));
  }
  const auto atom_count = parse_size(token, "stable atom ID count");
  if (!atom_count.has_value()) {
    return operation::Result<RepresentationVisibilitySessionSnapshot>::failure(
        atom_count.error());
  }
  std::vector<std::uint64_t> atom_ids;
  atom_ids.reserve(atom_count.value());
  for (std::size_t index = 0; index < atom_count.value(); ++index) {
    if (!(header >> token)) {
      return operation::Result<
          RepresentationVisibilitySessionSnapshot>::failure(
          invalid("truncated representation visibility stable atom IDs"));
    }
    const auto id = parse_uint64(token, "stable atom ID");
    if (!id.has_value() || id.value() == 0U) {
      return operation::Result<
          RepresentationVisibilitySessionSnapshot>::failure(
          id.has_value() ? invalid("invalid stable atom ID") : id.error());
    }
    atom_ids.push_back(id.value());
  }
  if (header >> token) {
    return operation::Result<RepresentationVisibilitySessionSnapshot>::failure(
        invalid("unexpected representation visibility session header data"));
  }
  const auto visibility =
      parse_representation_visibility(text.substr(newline[3] + 1U));
  if (!visibility.has_value()) {
    return operation::Result<RepresentationVisibilitySessionSnapshot>::failure(
        visibility.error());
  }
  auto unique_ids = atom_ids;
  std::sort(unique_ids.begin(), unique_ids.end());
  if (atom_ids.size() != visibility.value().atom_count ||
      std::adjacent_find(unique_ids.begin(), unique_ids.end()) !=
          unique_ids.end()) {
    return operation::Result<RepresentationVisibilitySessionSnapshot>::failure(
        invalid("representation visibility stable atom identity table is inconsistent"));
  }
  return operation::Result<RepresentationVisibilitySessionSnapshot>::success(
      {kRepresentationVisibilitySessionSchemaVersion, object.value(),
       topology.value(), std::move(atom_ids), visibility.value()});
}

}  // namespace molshredder::application
