#include "molshredder/selection/evaluator.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::selection {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_selection,
                          std::move(message), std::move(suggestion)};
}

std::string lowercase(std::string value) {
  for (auto& character : value) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  return value;
}

std::optional<std::int64_t> integer(std::string_view text) {
  std::int64_t value{};
  const auto parsed =
      molshredder::core::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::pair<std::int64_t, std::int64_t>> integer_range(
    std::string_view text) {
  const auto separator = text.find(':');
  if (separator == std::string_view::npos) {
    const auto value = integer(text);
    if (!value.has_value()) return std::nullopt;
    return std::pair{value.value(), value.value()};
  }
  const auto first = integer(text.substr(0U, separator));
  const auto last = integer(text.substr(separator + 1U));
  if (!first.has_value() || !last.has_value() || first.value() > last.value()) {
    return std::nullopt;
  }
  return std::pair{first.value(), last.value()};
}

std::string element_symbol(std::uint8_t atomic_number) {
  static constexpr std::array<std::string_view, 119> symbols{
      "",   "H",  "He", "Li", "Be", "B",  "C",  "N",  "O",  "F",
      "Ne", "Na", "Mg", "Al", "Si", "P",  "S",  "Cl", "Ar", "K",
      "Ca", "Sc", "Ti", "V",  "Cr", "Mn", "Fe", "Co", "Ni", "Cu",
      "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr", "Rb", "Sr", "Y",
      "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In",
      "Sn", "Sb", "Te", "I",  "Xe", "Cs", "Ba", "La", "Ce", "Pr",
      "Nd", "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm",
      "Yb", "Lu", "Hf", "Ta", "W",  "Re", "Os", "Ir", "Pt", "Au",
      "Hg", "Tl", "Pb", "Bi", "Po", "At", "Rn", "Fr", "Ra", "Ac",
      "Th", "Pa", "U",  "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es",
      "Fm", "Md", "No", "Lr", "Rf", "Db", "Sg", "Bh", "Hs", "Mt",
      "Ds", "Rg", "Cn", "Nh", "Fl", "Mc", "Lv", "Ts", "Og"};
  return std::string{symbols[atomic_number]};
}

operation::Result<Mask> predicate_mask(const Predicate& predicate,
                                       const model::Topology& topology) {
  Mask result(topology.atom_count(), 0U);
  std::optional<std::pair<std::int64_t, std::int64_t>> range;
  if (predicate.field == Field::index || predicate.field == Field::source_id) {
    range = integer_range(predicate.value);
    if (!range.has_value() ||
        (predicate.field == Field::index && range->first < 1)) {
      return operation::Result<Mask>::failure(invalid(
          "selection index/id requires an integer or inclusive first:last "
          "range",
          "atom index is one-based; examples: index 1, index 1:10, id 42"));
    }
  }
  const auto expected_element = lowercase(predicate.value);
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    const auto& atom = topology.atoms()[index];
    const auto& residue = topology.residues()[atom.residue.value];
    bool matches{};
    switch (predicate.field) {
      case Field::atom_name:
        matches = atom.name == predicate.value;
        break;
      case Field::element:
        matches = lowercase(element_symbol(atom.atomic_number)) ==
                  expected_element;
        break;
      case Field::residue_name:
        matches = residue.name == predicate.value;
        break;
      case Field::residue_id:
        matches = std::to_string(residue.sequence_number) +
                      residue.insertion_code ==
                  predicate.value;
        break;
      case Field::chain:
        matches = residue.chain_id == predicate.value;
        break;
      case Field::segment:
        matches = residue.segment_id == predicate.value;
        break;
      case Field::index: {
        const auto one_based = static_cast<std::int64_t>(index) + 1;
        matches = one_based >= range->first && one_based <= range->second;
        break;
      }
      case Field::source_id:
        matches = atom.source_serial.has_value() &&
                  atom.source_serial.value() >= range->first &&
                  atom.source_serial.value() <= range->second;
        break;
    }
    result[index] = matches ? 1U : 0U;
  }
  return operation::Result<Mask>::success(std::move(result));
}

operation::Result<Mask> evaluate_node(const std::shared_ptr<const Node>& node,
                                      const model::Topology& topology,
                                      const NamedResolver& named_resolver) {
  if (node == nullptr) {
    return operation::Result<Mask>::failure(
        invalid("selection expression has no root"));
  }
  if (node->kind == NodeKind::all || node->kind == NodeKind::none) {
    return operation::Result<Mask>::success(
        Mask(topology.atom_count(), node->kind == NodeKind::all ? 1U : 0U));
  }
  if (node->kind == NodeKind::predicate) {
    return predicate_mask(node->predicate, topology);
  }
  if (node->kind == NodeKind::named) {
    if (!named_resolver) {
      return operation::Result<Mask>::failure(invalid(
          "named selection '@" + node->name + "' has no resolver",
          "evaluate through NamedSelections or provide a named resolver"));
    }
    auto resolved = named_resolver(node->name);
    if (!resolved.has_value()) return resolved;
    if (!mask_is_valid(resolved.value(), topology.atom_count())) {
      return operation::Result<Mask>::failure(
          invalid("named selection returned an invalid mask"));
    }
    return resolved;
  }

  auto left = evaluate_node(node->left, topology, named_resolver);
  if (!left.has_value()) return left;
  if (node->kind == NodeKind::negation) {
    for (auto& value : left.value()) value = value == 0U ? 1U : 0U;
    return left;
  }
  auto right = evaluate_node(node->right, topology, named_resolver);
  if (!right.has_value()) return right;
  for (std::size_t index = 0; index < left.value().size(); ++index) {
    if (node->kind == NodeKind::conjunction) {
      left.value()[index] =
          left.value()[index] != 0U && right.value()[index] != 0U ? 1U : 0U;
    } else {
      left.value()[index] =
          left.value()[index] != 0U || right.value()[index] != 0U ? 1U : 0U;
    }
  }
  return left;
}

}  // namespace

operation::Result<Mask> evaluate(const Expression& expression,
                                 const model::Topology& topology,
                                 const NamedResolver& named_resolver) {
  return evaluate_node(expression.root(), topology, named_resolver);
}

bool mask_is_valid(std::span<const std::uint8_t> mask,
                   std::size_t atom_count) noexcept {
  return mask.size() == atom_count &&
         std::all_of(mask.begin(), mask.end(),
                     [](std::uint8_t value) { return value <= 1U; });
}

}  // namespace molshredder::selection
