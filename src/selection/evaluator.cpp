#include "molshredder/selection/evaluator.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <cctype>
#include <cstdint>
#include <cmath>
#include <optional>
#include <map>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

#include "molshredder/operation/error.hpp"
#include "molshredder/analysis/contacts.hpp"

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

std::optional<double> floating(std::string_view text) {
  double value{};
  const auto parsed = molshredder::core::from_chars(
      text.data(), text.data() + text.size(), value);
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

std::optional<std::vector<std::pair<std::int64_t, std::int64_t>>>
integer_ranges(std::string_view text) {
  std::vector<std::pair<std::int64_t, std::int64_t>> result;
  std::size_t begin{};
  while (begin <= text.size()) {
    const auto separator = text.find_first_of("+,", begin);
    const auto item = text.substr(
        begin, separator == std::string_view::npos ? text.size() - begin
                                                   : separator - begin);
    auto range = integer_range(item);
    if (!range.has_value()) {
      const auto dash = item.find('-', 1U);
      if (dash != std::string_view::npos) {
        const auto first = integer(item.substr(0U, dash));
        const auto last = integer(item.substr(dash + 1U));
        if (first.has_value() && last.has_value() &&
            first.value() <= last.value()) {
          range = std::pair{first.value(), last.value()};
        }
      }
    }
    if (!range.has_value()) return std::nullopt;
    result.push_back(range.value());
    if (separator == std::string_view::npos) break;
    begin = separator + 1U;
  }
  return result.empty() ? std::nullopt
                        : std::optional{std::move(result)};
}

bool wildcard_match(std::string_view pattern, std::string_view value) {
  std::size_t pattern_index{};
  std::size_t value_index{};
  std::size_t star = std::string_view::npos;
  std::size_t retry{};
  while (value_index < value.size()) {
    if (pattern_index < pattern.size() &&
        (pattern[pattern_index] == '?' ||
         pattern[pattern_index] == value[value_index])) {
      ++pattern_index;
      ++value_index;
    } else if (pattern_index < pattern.size() &&
               pattern[pattern_index] == '*') {
      star = pattern_index++;
      retry = value_index;
    } else if (star != std::string_view::npos) {
      pattern_index = star + 1U;
      value_index = ++retry;
    } else {
      return false;
    }
  }
  while (pattern_index < pattern.size() && pattern[pattern_index] == '*')
    ++pattern_index;
  return pattern_index == pattern.size();
}

bool string_value_matches(std::string_view expected, std::string_view actual,
                          bool literal, bool case_insensitive = false) {
  if (literal) return expected == actual;
  std::size_t begin{};
  while (begin <= expected.size()) {
    const auto separator = expected.find('+', begin);
    auto pattern = std::string{expected.substr(
        begin, separator == std::string_view::npos ? expected.size() - begin
                                                   : separator - begin)};
    auto candidate = std::string{actual};
    if (case_insensitive) {
      pattern = lowercase(std::move(pattern));
      candidate = lowercase(std::move(candidate));
    }
    const auto dash = pattern.find('-');
    if (!literal && dash != std::string::npos && dash > 0U &&
        dash + 1U < pattern.size() && pattern.find('*') == std::string::npos &&
        pattern.find('?') == std::string::npos) {
      const auto first = pattern.substr(0U, dash);
      const auto last = pattern.substr(dash + 1U);
      if (first <= candidate && candidate <= last) return true;
    } else if (wildcard_match(pattern, candidate)) {
      return true;
    }
    if (separator == std::string_view::npos) break;
    begin = separator + 1U;
  }
  return false;
}

bool residue_value_matches(const Predicate& predicate,
                           const model::ResidueRecord& residue) {
  const auto actual =
      std::to_string(residue.sequence_number) + residue.insertion_code;
  std::size_t begin{};
  while (begin <= predicate.value.size()) {
    const auto separator = predicate.value.find('+', begin);
    const auto item = std::string_view{predicate.value}.substr(
        begin, separator == std::string::npos
                   ? predicate.value.size() - begin
                   : separator - begin);
    const auto dash = item.find('-', 1U);
    if (!predicate.literal && dash != std::string_view::npos) {
      const auto first = integer(item.substr(0U, dash));
      const auto last = integer(item.substr(dash + 1U));
      if (first.has_value() && last.has_value() &&
          first.value() <= residue.sequence_number &&
          residue.sequence_number <= last.value())
        return true;
    } else if (string_value_matches(item, actual, predicate.literal)) {
      return true;
    }
    if (separator == std::string_view::npos) break;
    begin = separator + 1U;
  }
  return false;
}

char peptide_code(std::string_view residue_name) {
  static const std::map<std::string_view, char, std::less<>> codes{
      {"ALA", 'A'}, {"ARG", 'R'}, {"ASN", 'N'}, {"ASP", 'D'},
      {"CYS", 'C'}, {"GLN", 'Q'}, {"GLU", 'E'}, {"GLY", 'G'},
      {"HIS", 'H'}, {"ILE", 'I'}, {"LEU", 'L'}, {"LYS", 'K'},
      {"MET", 'M'}, {"PHE", 'F'}, {"PRO", 'P'}, {"SER", 'S'},
      {"THR", 'T'}, {"TRP", 'W'}, {"TYR", 'Y'}, {"VAL", 'V'},
      {"MSE", 'M'}};
  const auto found = codes.find(residue_name);
  return found == codes.end() ? 'X' : found->second;
}

std::vector<std::uint8_t> peptide_sequence_residues(
    std::string pattern, const model::Topology& topology) {
  pattern = lowercase(std::move(pattern));
  std::transform(pattern.begin(), pattern.end(), pattern.begin(), [](char value) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
  });
  std::vector<std::uint8_t> result(topology.residue_count(), 0U);
  if (pattern.empty()) return result;
  std::size_t chain_begin{};
  while (chain_begin < topology.residue_count()) {
    auto chain_end = chain_begin + 1U;
    while (chain_end < topology.residue_count() &&
           topology.residues()[chain_end].chain_id ==
               topology.residues()[chain_begin].chain_id)
      ++chain_end;
    if (chain_end - chain_begin >= pattern.size()) {
      for (auto begin = chain_begin; begin + pattern.size() <= chain_end;
           ++begin) {
        bool matches = true;
        for (std::size_t offset = 0; offset < pattern.size(); ++offset) {
          const auto expected = pattern[offset];
          const auto actual = peptide_code(topology.residues()[begin + offset].name);
          if (expected != '?' && expected != 'X' && expected != actual) {
            matches = false;
            break;
          }
        }
        if (matches) {
          std::fill(result.begin() + static_cast<std::ptrdiff_t>(begin),
                    result.begin() +
                        static_cast<std::ptrdiff_t>(begin + pattern.size()),
                    1U);
        }
      }
    }
    chain_begin = chain_end;
  }
  return result;
}

operation::Result<bool> property_value_matches(
    const Predicate& predicate, const model::Topology& topology,
    const EvaluationContext& context, std::size_t atom_index) {
  const auto* frame_properties = context.frame == nullptr
                                     ? nullptr
                                     : &context.frame->metadata().atom_properties;
  const model::AtomProperty* property = nullptr;
  bool from_frame{};
  std::string resolved_name = predicate.property;
  if (frame_properties != nullptr) {
    auto found = frame_properties->find(resolved_name);
    if (found == frame_properties->end() && resolved_name == "secondary_structure") {
      found = frame_properties->find("ss");
      resolved_name = "ss";
    }
    if (found != frame_properties->end()) {
      property = &found->second;
      from_frame = true;
    }
  }
  if (property == nullptr) {
    resolved_name = predicate.property;
    property = topology.properties().find(resolved_name);
    if (property == nullptr && resolved_name == "secondary_structure") {
      property = topology.properties().find("ss");
      resolved_name = "ss";
    }
  }
  if (property == nullptr) {
    return operation::Result<bool>::failure(invalid(
        "atom property '" + predicate.property + "' is unavailable",
        "inspect topology/frame properties before using this selector"));
  }
  if (from_frame && !context.frame->atom_present(atom_index))
    return operation::Result<bool>::success(false);
  const model::AtomProperty* presence = nullptr;
  if (from_frame && frame_properties != nullptr) {
    const auto found = frame_properties->find(resolved_name + "_present");
    if (found != frame_properties->end()) presence = &found->second;
  } else {
    presence = topology.properties().find(resolved_name + "_present");
  }
  if (presence != nullptr) {
    const auto* values = std::get_if<model::BooleanColumn>(&presence->values);
    if (values == nullptr) {
      return operation::Result<bool>::failure(
          invalid("presence property for '" + resolved_name + "' is not boolean"));
    }
    if (values->values[atom_index] == 0U)
      return operation::Result<bool>::success(false);
  }
  return std::visit(
      [&](const auto& values) -> operation::Result<bool> {
        using Values = std::decay_t<decltype(values)>;
        if constexpr (std::is_same_v<Values, std::vector<std::string>>) {
          return operation::Result<bool>::success(string_value_matches(
              predicate.value, values[atom_index], predicate.literal));
        } else if constexpr (std::is_same_v<Values, model::BooleanColumn>) {
          const auto expected = lowercase(predicate.value);
          if (expected != "0" && expected != "1" && expected != "false" &&
              expected != "true") {
            return operation::Result<bool>::failure(invalid(
                "boolean property selector requires true, false, 1, or 0"));
          }
          const bool requested = expected == "1" || expected == "true";
          return operation::Result<bool>::success(
              (values.values[atom_index] != 0U) == requested);
        } else {
          const auto expected = floating(predicate.value);
          if (!expected.has_value()) {
            return operation::Result<bool>::failure(invalid(
                "numeric property selector requires a numeric value",
                "use p.<name> with a comparison for ranges and arithmetic"));
          }
          const auto actual = static_cast<double>(values[atom_index]);
          if (!std::isfinite(actual)) {
            return operation::Result<bool>::failure(invalid(
                "atom property '" + resolved_name +
                "' contains a non-finite value"));
          }
          return operation::Result<bool>::success(actual == expected.value());
        }
      },
      property->values);
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
                                       const model::Topology& topology,
                                       const EvaluationContext& context) {
  Mask result(topology.atom_count(), 0U);
  std::optional<std::vector<std::pair<std::int64_t, std::int64_t>>> ranges;
  if (predicate.field == Field::index || predicate.field == Field::rank ||
      predicate.field == Field::source_id || predicate.field == Field::state) {
    ranges = integer_ranges(predicate.value);
    if (!ranges.has_value() ||
        ((predicate.field == Field::index &&
          std::any_of(ranges->begin(), ranges->end(), [](const auto& range) {
            return range.first < 1;
          })) ||
         (predicate.field == Field::rank &&
          std::any_of(ranges->begin(), ranges->end(), [](const auto& range) {
            return range.first < 0;
          })))) {
      return operation::Result<Mask>::failure(invalid(
          "selection index/id requires an integer, list, or inclusive range",
          "atom index is one-based; examples: index 1, index 1:10, index 1+3"));
    }
  }
  const auto peptide_residues =
      predicate.field == Field::peptide_sequence
          ? peptide_sequence_residues(predicate.value, topology)
          : std::vector<std::uint8_t>{};
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    const auto& atom = topology.atoms()[index];
    const auto& residue = topology.residues()[atom.residue.value];
    bool matches{};
    switch (predicate.field) {
      case Field::atom_name:
        matches = string_value_matches(predicate.value, atom.name,
                                       predicate.literal);
        break;
      case Field::element:
        matches = string_value_matches(predicate.value,
                                       element_symbol(atom.atomic_number),
                                       predicate.literal, true);
        break;
      case Field::alternate_location:
        matches = string_value_matches(predicate.value,
                                       atom.alternate_location,
                                       predicate.literal);
        break;
      case Field::residue_name:
        matches = string_value_matches(predicate.value, residue.name,
                                       predicate.literal);
        break;
      case Field::residue_id:
        matches = residue_value_matches(predicate, residue);
        break;
      case Field::chain:
        matches = string_value_matches(predicate.value, residue.chain_id,
                                       predicate.literal);
        break;
      case Field::segment:
        matches = string_value_matches(predicate.value, residue.segment_id,
                                       predicate.literal);
        break;
      case Field::object:
        matches = string_value_matches(predicate.value, context.object_name,
                                       predicate.literal);
        break;
      case Field::rank: {
        const auto zero_based = static_cast<std::int64_t>(index);
        matches = std::any_of(
            ranges->begin(), ranges->end(), [&](const auto& range) {
              return zero_based >= range.first && zero_based <= range.second;
            });
        break;
      }
      case Field::peptide_sequence:
        matches = peptide_residues[atom.residue.value] != 0U;
        break;
      case Field::index: {
        const auto one_based = static_cast<std::int64_t>(index) + 1;
        matches = std::any_of(
            ranges->begin(), ranges->end(), [&](const auto& range) {
              return one_based >= range.first && one_based <= range.second;
            });
        break;
      }
      case Field::source_id:
        matches = atom.source_serial.has_value() &&
                  std::any_of(
                      ranges->begin(), ranges->end(), [&](const auto& range) {
                        return atom.source_serial.value() >= range.first &&
                               atom.source_serial.value() <= range.second;
                      });
        break;
      case Field::state: {
        if (context.coordinate_source == nullptr) {
          return operation::Result<Mask>::failure(invalid(
              "state selection requires a coordinate source",
              "evaluate through a Workspace with molecular states"));
        }
        bool present_in_state{};
        for (const auto& range : ranges.value()) {
          for (auto state = range.first; state <= range.second; ++state) {
            if (state < 1) {
              return operation::Result<Mask>::failure(invalid(
                  "selection state is one-based and must be positive"));
            }
            const auto frame = context.coordinate_source->read_frame(
                static_cast<std::size_t>(state - 1));
            if (!frame.has_value()) {
              return operation::Result<Mask>::failure(frame.error());
            }
            if (frame.value()->atom_count() != topology.atom_count()) {
              return operation::Result<Mask>::failure(invalid(
                  "selection state atom count does not match topology"));
            }
            if (frame.value()->atom_present(index)) {
              present_in_state = true;
              break;
            }
          }
          if (present_in_state) break;
        }
        matches = present_in_state;
        break;
      }
      case Field::property: {
        const auto property_match =
            property_value_matches(predicate, topology, context, index);
        if (!property_match.has_value())
          return operation::Result<Mask>::failure(property_match.error());
        matches = property_match.value();
        break;
      }
    }
    result[index] = matches ? 1U : 0U;
  }
  return operation::Result<Mask>::success(std::move(result));
}

operation::Result<std::optional<double>> numeric_property(
    const NumericNode& node, const model::Topology& topology,
    std::size_t atom_index, const EvaluationContext& context) {
  if (node.kind == NumericNodeKind::literal) {
    return operation::Result<std::optional<double>>::success(node.literal);
  }
  if (node.kind == NumericNodeKind::field) {
    const auto& atom = topology.atoms()[atom_index];
    if (node.field == NumericField::index) {
      return operation::Result<std::optional<double>>::success(
          static_cast<double>(atom_index + 1U));
    }
    if (node.field == NumericField::source_id) {
      if (!atom.source_serial.has_value())
        return operation::Result<std::optional<double>>::success(std::nullopt);
      return operation::Result<std::optional<double>>::success(
          static_cast<double>(atom.source_serial.value()));
    }
    if (node.field == NumericField::formal_charge) {
      return operation::Result<std::optional<double>>::success(
          static_cast<double>(atom.formal_charge));
    }
    if (node.field == NumericField::coordinate_x ||
        node.field == NumericField::coordinate_y ||
        node.field == NumericField::coordinate_z) {
      if (context.frame == nullptr) {
        return operation::Result<std::optional<double>>::failure(invalid(
            "coordinate selection requires a current frame",
            "evaluate through a Workspace with coordinates"));
      }
      if (context.frame->atom_count() != topology.atom_count()) {
        return operation::Result<std::optional<double>>::failure(
            invalid("selection frame atom count does not match topology"));
      }
      if (!context.frame->atom_present(atom_index))
        return operation::Result<std::optional<double>>::success(std::nullopt);
      const auto coordinate = std::visit(
          [atom_index](const auto& values) {
            return model::Vec3d{static_cast<double>(values[atom_index].x),
                                static_cast<double>(values[atom_index].y),
                                static_cast<double>(values[atom_index].z)};
          },
          context.frame->positions().values());
      if (node.field == NumericField::coordinate_x)
        return operation::Result<std::optional<double>>::success(coordinate.x);
      if (node.field == NumericField::coordinate_y)
        return operation::Result<std::optional<double>>::success(coordinate.y);
      return operation::Result<std::optional<double>>::success(coordinate.z);
    }

    std::string resolved_property = node.property;
    const auto* frame_properties = context.frame == nullptr
                                       ? nullptr
                                       : &context.frame->metadata().atom_properties;
    const model::AtomProperty* property = nullptr;
    bool property_from_frame{};
    if (frame_properties != nullptr) {
      auto found = frame_properties->find(node.property);
      if (found == frame_properties->end() && node.property == "b_factor") {
        found = frame_properties->find("b_iso_or_equiv");
        resolved_property = "b_iso_or_equiv";
      }
      if (found != frame_properties->end()) {
        property = &found->second;
        property_from_frame = true;
      }
    }
    if (property == nullptr) {
      resolved_property = node.property;
      property = topology.properties().find(node.property);
      if (property == nullptr && node.property == "b_factor") {
        property = topology.properties().find("b_iso_or_equiv");
        resolved_property = "b_iso_or_equiv";
      }
    }
    if (property_from_frame && !context.frame->atom_present(atom_index)) {
      return operation::Result<std::optional<double>>::success(std::nullopt);
    }
    if (property == nullptr) {
      return operation::Result<std::optional<double>>::failure(invalid(
          "numeric atom property '" + node.property + "' is unavailable",
          "inspect topology properties or choose an available numeric property"));
    }
    const model::AtomProperty* presence = nullptr;
    if (property_from_frame && frame_properties != nullptr) {
      const auto found = frame_properties->find(resolved_property + "_present");
      if (found != frame_properties->end()) presence = &found->second;
    } else {
      presence = topology.properties().find(resolved_property + "_present");
    }
    if (presence != nullptr) {
      const auto* values = std::get_if<model::BooleanColumn>(&presence->values);
      if (values == nullptr) {
        return operation::Result<std::optional<double>>::failure(invalid(
            "presence property for '" + node.property + "' is not boolean"));
      }
      if (values->values[atom_index] == 0U)
        return operation::Result<std::optional<double>>::success(std::nullopt);
    }
    const auto value = std::visit(
        [atom_index](const auto& values) -> std::optional<double> {
          using Values = std::decay_t<decltype(values)>;
          if constexpr (std::is_same_v<Values, model::BooleanColumn>) {
            return static_cast<double>(values.values[atom_index]);
          } else if constexpr (std::is_same_v<Values,
                                              std::vector<std::string>>) {
            return std::nullopt;
          } else {
            return static_cast<double>(values[atom_index]);
          }
        },
        property->values);
    if (!value.has_value()) {
      return operation::Result<std::optional<double>>::failure(invalid(
          "atom property '" + node.property + "' is not numeric"));
    }
    if (!std::isfinite(value.value())) {
      return operation::Result<std::optional<double>>::failure(invalid(
          "atom property '" + node.property + "' contains a non-finite value"));
    }
    return operation::Result<std::optional<double>>::success(value);
  }
  auto left = numeric_property(*node.left, topology, atom_index, context);
  if (!left.has_value() || !left.value().has_value()) return left;
  if (node.kind == NumericNodeKind::negation) {
    return operation::Result<std::optional<double>>::success(-left.value().value());
  }
  auto right = numeric_property(*node.right, topology, atom_index, context);
  if (!right.has_value() || !right.value().has_value()) return right;
  double value{};
  if (node.kind == NumericNodeKind::addition)
    value = left.value().value() + right.value().value();
  else if (node.kind == NumericNodeKind::subtraction)
    value = left.value().value() - right.value().value();
  else if (node.kind == NumericNodeKind::multiplication)
    value = left.value().value() * right.value().value();
  else {
    if (right.value().value() == 0.0) {
      return operation::Result<std::optional<double>>::failure(
          invalid("numeric selection divides by zero"));
    }
    value = left.value().value() / right.value().value();
  }
  if (!std::isfinite(value)) {
    return operation::Result<std::optional<double>>::failure(
        invalid("numeric selection produced a non-finite value"));
  }
  return operation::Result<std::optional<double>>::success(value);
}

operation::Result<Mask> numeric_comparison_mask(
    const NumericComparison& comparison, const model::Topology& topology,
    const EvaluationContext& context) {
  Mask result(topology.atom_count(), 0U);
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    const auto left =
        numeric_property(*comparison.left, topology, index, context);
    if (!left.has_value()) return operation::Result<Mask>::failure(left.error());
    if (!left.value().has_value()) continue;
    bool matches{};
    if (comparison.operation == ComparisonOperator::range) {
      matches = std::any_of(
          comparison.intervals.begin(), comparison.intervals.end(),
          [&](const NumericInterval& interval) {
            return left.value().value() >= interval.first &&
                   left.value().value() <= interval.last;
          });
    } else {
      const auto right =
          numeric_property(*comparison.right, topology, index, context);
      if (!right.has_value())
        return operation::Result<Mask>::failure(right.error());
      if (!right.value().has_value()) continue;
      const auto lhs = left.value().value();
      const auto rhs = right.value().value();
      switch (comparison.operation) {
        case ComparisonOperator::equal: matches = lhs == rhs; break;
        case ComparisonOperator::not_equal: matches = lhs != rhs; break;
        case ComparisonOperator::less: matches = lhs < rhs; break;
        case ComparisonOperator::less_equal: matches = lhs <= rhs; break;
        case ComparisonOperator::greater: matches = lhs > rhs; break;
        case ComparisonOperator::greater_equal: matches = lhs >= rhs; break;
        case ComparisonOperator::range: break;
      }
    }
    result[index] = matches ? 1U : 0U;
  }
  return operation::Result<Mask>::success(std::move(result));
}

operation::Result<Mask> expand_mask(const Expansion& expansion, Mask selected,
                                    const model::Topology& topology,
                                    const EvaluationContext& context) {
  const auto atom_count = topology.atom_count();
  if (expansion.operation == ExpansionOperator::object ||
      expansion.operation == ExpansionOperator::unit_cell) {
    if (expansion.operation == ExpansionOperator::unit_cell &&
        (context.frame == nullptr ||
         !context.frame->metadata().unit_cell.has_value())) {
      return operation::Result<Mask>::failure(invalid(
          "bycell selection requires a current periodic unit cell",
          "load a structure or trajectory frame with unit-cell metadata"));
    }
    const auto any = std::any_of(selected.begin(), selected.end(),
                                 [](std::uint8_t value) { return value != 0U; });
    return operation::Result<Mask>::success(Mask(atom_count, any ? 1U : 0U));
  }
  if (expansion.operation == ExpansionOperator::residue ||
      expansion.operation == ExpansionOperator::chain ||
      expansion.operation == ExpansionOperator::segment ||
      expansion.operation == ExpansionOperator::calpha) {
    Mask result(atom_count, 0U);
    for (std::size_t candidate = 0; candidate < atom_count; ++candidate) {
      const auto& candidate_atom = topology.atoms()[candidate];
      const auto& candidate_residue =
          topology.residues()[candidate_atom.residue.value];
      for (std::size_t source = 0; source < atom_count; ++source) {
        if (selected[source] == 0U) continue;
        const auto& source_atom = topology.atoms()[source];
        const auto& source_residue =
            topology.residues()[source_atom.residue.value];
        const bool same_group =
            expansion.operation == ExpansionOperator::residue
                ? candidate_atom.residue == source_atom.residue
            : expansion.operation == ExpansionOperator::chain
                ? candidate_residue.chain_id == source_residue.chain_id
                : candidate_residue.segment_id == source_residue.segment_id;
        const bool matches = expansion.operation == ExpansionOperator::calpha
                                 ? candidate_atom.residue == source_atom.residue &&
                                       candidate_atom.name == "CA"
                                 : same_group;
        if (matches) {
          result[candidate] = 1U;
          break;
        }
      }
    }
    return operation::Result<Mask>::success(std::move(result));
  }

  std::vector<std::vector<std::size_t>> adjacency(atom_count);
  for (const auto& bond : topology.bonds()) {
    adjacency[bond.first.value].push_back(bond.second.value);
    adjacency[bond.second.value].push_back(bond.first.value);
  }
  if (expansion.operation == ExpansionOperator::neighbor ||
      expansion.operation == ExpansionOperator::bound_to) {
    Mask result(atom_count, 0U);
    for (std::size_t atom = 0; atom < atom_count; ++atom) {
      if (selected[atom] == 0U) continue;
      for (const auto neighbor : adjacency[atom]) result[neighbor] = 1U;
    }
    if (expansion.operation == ExpansionOperator::neighbor) {
      for (std::size_t atom = 0; atom < atom_count; ++atom) {
        if (selected[atom] != 0U) result[atom] = 0U;
      }
    }
    return operation::Result<Mask>::success(std::move(result));
  }
  if (expansion.operation == ExpansionOperator::bond_steps) {
    Mask result = selected;
    Mask frontier = selected;
    for (std::size_t step = 0; step < expansion.steps; ++step) {
      Mask next(atom_count, 0U);
      for (std::size_t atom = 0; atom < atom_count; ++atom) {
        if (frontier[atom] == 0U) continue;
        for (const auto neighbor : adjacency[atom]) {
          if (result[neighbor] == 0U) next[neighbor] = 1U;
          result[neighbor] = 1U;
        }
      }
      frontier = std::move(next);
    }
    return operation::Result<Mask>::success(std::move(result));
  }
  if (expansion.operation == ExpansionOperator::ring) {
    constexpr std::size_t kMaximumSmallRingAtoms = 7U;
    Mask result(atom_count, 0U);
    for (std::size_t center = 0; center < atom_count; ++center) {
      if (selected[center] == 0U || adjacency[center].size() < 2U) continue;
      for (std::size_t first = 0; first < adjacency[center].size(); ++first) {
        for (std::size_t second = first + 1U;
             second < adjacency[center].size(); ++second) {
          const auto start = adjacency[center][first];
          const auto goal = adjacency[center][second];
          std::vector<std::size_t> parent(atom_count, atom_count);
          std::vector<std::size_t> depth(atom_count, atom_count);
          std::vector<std::size_t> queue{start};
          depth[start] = 0U;
          for (std::size_t cursor = 0;
               cursor < queue.size() && depth[goal] == atom_count; ++cursor) {
            const auto atom = queue[cursor];
            if (depth[atom] + 2U >= kMaximumSmallRingAtoms) continue;
            for (const auto neighbor : adjacency[atom]) {
              if (neighbor == center || depth[neighbor] != atom_count) continue;
              parent[neighbor] = atom;
              depth[neighbor] = depth[atom] + 1U;
              queue.push_back(neighbor);
              if (neighbor == goal) break;
            }
          }
          if (depth[goal] == atom_count ||
              depth[goal] + 2U > kMaximumSmallRingAtoms)
            continue;
          result[center] = 1U;
          for (auto atom = goal; atom != atom_count; atom = parent[atom]) {
            result[atom] = 1U;
            if (atom == start) break;
          }
        }
      }
    }
    return operation::Result<Mask>::success(std::move(result));
  }

  Mask result(atom_count, 0U);
  std::vector<std::size_t> stack;
  for (std::size_t start = 0; start < atom_count; ++start) {
    if (selected[start] == 0U || result[start] != 0U) continue;
    stack.push_back(start);
    result[start] = 1U;
    while (!stack.empty()) {
      const auto atom = stack.back();
      stack.pop_back();
      for (const auto neighbor : adjacency[atom]) {
        if (result[neighbor] != 0U) continue;
        result[neighbor] = 1U;
        stack.push_back(neighbor);
      }
    }
  }
  return operation::Result<Mask>::success(std::move(result));
}

operation::Result<std::vector<std::optional<model::Vec3d>>> frame_positions(
    const model::Topology& topology, const EvaluationContext& context) {
  if (context.frame == nullptr) {
    return operation::Result<std::vector<std::optional<model::Vec3d>>>::failure(
        invalid("spatial selection requires a current frame",
                "evaluate through a Workspace with coordinates"));
  }
  if (context.frame->atom_count() != topology.atom_count()) {
    return operation::Result<std::vector<std::optional<model::Vec3d>>>::failure(
        invalid("selection frame atom count does not match topology"));
  }
  std::vector<std::optional<model::Vec3d>> result(topology.atom_count());
  std::visit(
      [&](const auto& values) {
        for (std::size_t index = 0; index < values.size(); ++index) {
          if (!context.frame->atom_present(index)) continue;
          result[index] = model::Vec3d{static_cast<double>(values[index].x),
                                       static_cast<double>(values[index].y),
                                       static_cast<double>(values[index].z)};
        }
      },
      context.frame->positions().values());
  return operation::Result<
      std::vector<std::optional<model::Vec3d>>>::success(std::move(result));
}

operation::Result<std::vector<double>> vdw_radii(
    const model::Topology& topology) {
  const auto* property = topology.properties().find("pqr.radius");
  if (property == nullptr) property = topology.properties().find("vdw_radius");
  std::vector<double> result(topology.atom_count(), 1.7);
  if (property != nullptr) {
    const auto scale = property->metadata.unit == std::optional<std::string>{"nanometer"}
                           ? 10.0
                           : 1.0;
    const auto assigned = std::visit(
        [&](const auto& values) {
          using Values = std::decay_t<decltype(values)>;
          if constexpr (std::is_same_v<Values, std::vector<float>> ||
                        std::is_same_v<Values, std::vector<double>>) {
            for (std::size_t index = 0; index < values.size(); ++index)
              result[index] = static_cast<double>(values[index]) * scale;
            return true;
          }
          return false;
        },
        property->values);
    if (!assigned) {
      return operation::Result<std::vector<double>>::failure(
          invalid("VDW radius property must be float32 or float64"));
    }
  } else {
    for (std::size_t index = 0; index < topology.atom_count(); ++index) {
      switch (topology.atoms()[index].atomic_number) {
        case 1U: result[index] = 1.20; break;
        case 6U: result[index] = 1.70; break;
        case 7U: result[index] = 1.55; break;
        case 8U: result[index] = 1.52; break;
        case 9U: result[index] = 1.47; break;
        case 15U: result[index] = 1.80; break;
        case 16U: result[index] = 1.80; break;
        case 17U: result[index] = 1.75; break;
        case 35U: result[index] = 1.85; break;
        case 53U: result[index] = 1.98; break;
        default: break;
      }
    }
  }
  if (std::any_of(result.begin(), result.end(), [](double radius) {
        return !std::isfinite(radius) || radius <= 0.0;
      })) {
    return operation::Result<std::vector<double>>::failure(
        invalid("VDW radii must be finite and positive"));
  }
  return operation::Result<std::vector<double>>::success(std::move(result));
}

operation::Result<Mask> vdw_gap_mask(
    double gap, const Mask& target,
    const std::vector<std::optional<model::Vec3d>>& positions,
    const model::Topology& topology) {
  const auto radii = vdw_radii(topology);
  if (!radii.has_value())
    return operation::Result<Mask>::failure(radii.error());
  Mask result(topology.atom_count(), 0U);
  if (topology.atom_count() == 0U)
    return operation::Result<Mask>::success(std::move(result));
  const auto maximum_radius =
      *std::max_element(radii.value().begin(), radii.value().end());
  const auto maximum_cutoff = 2.0 * maximum_radius + gap;
  if (maximum_cutoff < 0.0)
    return operation::Result<Mask>::success(std::move(result));
  using CellKey = std::array<std::int64_t, 3>;
  std::map<CellKey, std::vector<std::size_t>> cells;
  const auto cell_size = std::max(maximum_cutoff, 1.0e-9);
  const auto cell_key = [cell_size](const model::Vec3d& position) {
    return CellKey{
        static_cast<std::int64_t>(std::floor(position.x / cell_size)),
        static_cast<std::int64_t>(std::floor(position.y / cell_size)),
        static_cast<std::int64_t>(std::floor(position.z / cell_size))};
  };
  for (std::size_t reference = 0; reference < topology.atom_count();
       ++reference) {
    if (target[reference] != 0U && positions[reference].has_value())
      cells[cell_key(positions[reference].value())].push_back(reference);
  }
  for (std::size_t candidate = 0; candidate < topology.atom_count();
       ++candidate) {
    if (target[candidate] != 0U || !positions[candidate].has_value()) continue;
    const auto base = cell_key(positions[candidate].value());
    for (std::int64_t x = -1; x <= 1 && result[candidate] == 0U; ++x) {
      for (std::int64_t y = -1; y <= 1 && result[candidate] == 0U; ++y) {
        for (std::int64_t z = -1; z <= 1 && result[candidate] == 0U; ++z) {
          const auto found =
              cells.find(CellKey{base[0] + x, base[1] + y, base[2] + z});
          if (found == cells.end()) continue;
          for (const auto reference : found->second) {
            const auto threshold =
                radii.value()[candidate] + radii.value()[reference] + gap;
            if (threshold < 0.0) continue;
            const auto dx =
                positions[candidate]->x - positions[reference]->x;
            const auto dy =
                positions[candidate]->y - positions[reference]->y;
            const auto dz =
                positions[candidate]->z - positions[reference]->z;
            if (dx * dx + dy * dy + dz * dz <= threshold * threshold) {
              result[candidate] = 1U;
              break;
            }
          }
        }
      }
    }
  }
  return operation::Result<Mask>::success(std::move(result));
}

operation::Result<Mask> spatial_mask(
    const SpatialRelation& relation, Mask left, std::optional<Mask> right,
    const model::Topology& topology, const EvaluationContext& context) {
  const auto positions = frame_positions(topology, context);
  if (!positions.has_value())
    return operation::Result<Mask>::failure(positions.error());
  Mask target = right.has_value() ? std::move(right.value()) : left;
  Mask candidates = right.has_value() ? std::move(left)
                                      : Mask(topology.atom_count(), 1U);
  if (relation.operation == SpatialOperator::vdw_gap) {
    return vdw_gap_mask(relation.distance, target, positions.value(), topology);
  }
  const auto has_target = std::any_of(target.begin(), target.end(),
                                      [](std::uint8_t value) {
                                        return value != 0U;
                                      });
  Mask result(topology.atom_count(), 0U);
  if (!has_target) return operation::Result<Mask>::success(std::move(result));
  const auto cutoff_squared = relation.distance * relation.distance;
  using CellKey = std::array<std::int64_t, 3>;
  std::map<CellKey, std::vector<std::size_t>> cells;
  const auto cell_key = [&](const model::Vec3d& position) {
    return CellKey{
        static_cast<std::int64_t>(std::floor(position.x / relation.distance)),
        static_cast<std::int64_t>(std::floor(position.y / relation.distance)),
        static_cast<std::int64_t>(std::floor(position.z / relation.distance))};
  };
  if (relation.distance > 0.0) {
    for (std::size_t reference = 0; reference < topology.atom_count();
         ++reference) {
      if (target[reference] != 0U &&
          positions.value()[reference].has_value()) {
        cells[cell_key(positions.value()[reference].value())].push_back(
            reference);
      }
    }
  }
  for (std::size_t candidate = 0; candidate < topology.atom_count();
       ++candidate) {
    if (candidates[candidate] == 0U ||
        !positions.value()[candidate].has_value())
      continue;
    bool within{};
    const auto test_reference = [&](std::size_t reference) {
      const auto delta_x = positions.value()[candidate]->x -
                           positions.value()[reference]->x;
      const auto delta_y = positions.value()[candidate]->y -
                           positions.value()[reference]->y;
      const auto delta_z = positions.value()[candidate]->z -
                           positions.value()[reference]->z;
      const auto distance_squared =
          delta_x * delta_x + delta_y * delta_y + delta_z * delta_z;
      if (distance_squared <= cutoff_squared) {
        within = true;
      }
    };
    if (relation.distance == 0.0) {
      for (std::size_t reference = 0; reference < topology.atom_count();
           ++reference) {
        if (target[reference] == 0U ||
            !positions.value()[reference].has_value())
          continue;
        test_reference(reference);
        if (within) break;
      }
    } else {
      const auto base = cell_key(positions.value()[candidate].value());
      for (std::int64_t x = -1; x <= 1 && !within; ++x) {
        for (std::int64_t y = -1; y <= 1 && !within; ++y) {
          for (std::int64_t z = -1; z <= 1 && !within; ++z) {
            const auto found =
                cells.find(CellKey{base[0] + x, base[1] + y, base[2] + z});
            if (found == cells.end()) continue;
            for (const auto reference : found->second) {
              test_reference(reference);
              if (within) break;
            }
          }
        }
      }
    }
    bool matches = relation.operation == SpatialOperator::beyond ? !within
                                                                  : within;
    if (relation.operation == SpatialOperator::around ||
        relation.operation == SpatialOperator::near_to) {
      if (target[candidate] != 0U) matches = false;
    }
    result[candidate] = matches ? 1U : 0U;
  }
  return operation::Result<Mask>::success(std::move(result));
}

Mask match_mask(MatchOperator operation, Mask candidates, const Mask& reference,
                const model::Topology& topology) {
  Mask result(topology.atom_count(), 0U);
  for (std::size_t candidate = 0; candidate < topology.atom_count();
       ++candidate) {
    if (candidates[candidate] == 0U) continue;
    const auto& atom = topology.atoms()[candidate];
    const auto& residue = topology.residues()[atom.residue.value];
    for (std::size_t target = 0; target < topology.atom_count(); ++target) {
      if (reference[target] == 0U) continue;
      const auto& target_atom = topology.atoms()[target];
      const auto& target_residue =
          topology.residues()[target_atom.residue.value];
      bool matches = atom.name == target_atom.name &&
                     residue.sequence_number == target_residue.sequence_number &&
                     residue.insertion_code == target_residue.insertion_code;
      if (operation == MatchOperator::identifiers) {
        matches = matches && residue.name == target_residue.name &&
                  residue.chain_id == target_residue.chain_id &&
                  residue.segment_id == target_residue.segment_id;
      }
      if (matches) {
        result[candidate] = 1U;
        break;
      }
    }
  }
  return result;
}

operation::Result<model::Vec3d> pseudo_point(
    NodeKind kind, const model::Topology& topology,
    const EvaluationContext& context) {
  if (kind == NodeKind::rotation_origin && context.rotation_origin.has_value())
    return operation::Result<model::Vec3d>::success(
        context.rotation_origin.value());
  if (kind == NodeKind::scene_center && context.scene_center.has_value())
    return operation::Result<model::Vec3d>::success(context.scene_center.value());
  if (kind == NodeKind::rotation_origin)
    return operation::Result<model::Vec3d>::success({0.0, 0.0, 0.0});
  const auto positions = frame_positions(topology, context);
  if (!positions.has_value())
    return operation::Result<model::Vec3d>::failure(positions.error());
  model::Vec3d center{};
  std::size_t count{};
  for (const auto& position : positions.value()) {
    if (!position.has_value()) continue;
    center.x += position->x;
    center.y += position->y;
    center.z += position->z;
    ++count;
  }
  if (count == 0U) {
    return operation::Result<model::Vec3d>::failure(
        invalid("scene-center selection has no present coordinates"));
  }
  const auto divisor = static_cast<double>(count);
  return operation::Result<model::Vec3d>::success(
      {center.x / divisor, center.y / divisor, center.z / divisor});
}

operation::Result<Mask> spatial_pseudo_mask(
    const SpatialRelation& relation, Mask candidates, model::Vec3d target,
    const model::Topology& topology, const EvaluationContext& context) {
  if (relation.operation == SpatialOperator::vdw_gap) {
    return operation::Result<Mask>::failure(invalid(
        "VDW gap requires an atom selection, not a virtual center/origin"));
  }
  const auto positions = frame_positions(topology, context);
  if (!positions.has_value())
    return operation::Result<Mask>::failure(positions.error());
  const auto cutoff_squared = relation.distance * relation.distance;
  Mask result(topology.atom_count(), 0U);
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    if (candidates[index] == 0U || !positions.value()[index].has_value())
      continue;
    const auto dx = positions.value()[index]->x - target.x;
    const auto dy = positions.value()[index]->y - target.y;
    const auto dz = positions.value()[index]->z - target.z;
    const auto within = dx * dx + dy * dy + dz * dz <= cutoff_squared;
    result[index] =
        (relation.operation == SpatialOperator::beyond ? !within : within)
            ? 1U
            : 0U;
  }
  return operation::Result<Mask>::success(std::move(result));
}

bool protein_residue(std::string_view name) {
  static constexpr std::array<std::string_view, 23> names{
      "ALA", "ARG", "ASN", "ASP", "CYS", "GLN", "GLU", "GLY",
      "HIS", "ILE", "LEU", "LYS", "MET", "PHE", "PRO", "SER",
      "THR", "TRP", "TYR", "VAL", "MSE", "SEC", "PYL"};
  return std::find(names.begin(), names.end(), name) != names.end();
}

bool nucleic_residue(std::string_view name) {
  static constexpr std::array<std::string_view, 13> names{
      "A", "C", "G", "U", "I", "DA", "DC", "DG", "DT", "DU",
      "DI", "ADE", "THY"};
  return std::find(names.begin(), names.end(), name) != names.end();
}

bool solvent_residue(std::string_view name) {
  static constexpr std::array<std::string_view, 8> names{
      "HOH", "WAT", "H2O", "DOD", "SOL", "TIP3", "TIP4", "SPC"};
  return std::find(names.begin(), names.end(), name) != names.end();
}

bool metal_element(std::uint8_t atomic_number) {
  switch (atomic_number) {
    case 3:
    case 4:
    case 11:
    case 12:
    case 13:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 37:
    case 38:
    case 39:
    case 40:
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
    case 48:
    case 49:
    case 50:
    case 55:
    case 56:
    case 57:
    case 72:
    case 73:
    case 74:
    case 75:
    case 76:
    case 77:
    case 78:
    case 79:
    case 80:
    case 81:
    case 82: return true;
    default: return false;
  }
}

bool protein_backbone_name(std::string_view name) {
  return name == "N" || name == "CA" || name == "C" || name == "O" ||
         name == "OXT";
}

bool nucleic_backbone_name(std::string_view name) {
  return name == "P" || name == "OP1" || name == "OP2" || name == "O5'" ||
         name == "C5'" || name == "C4'" || name == "C3'" || name == "O3'";
}

operation::Result<Mask> chemical_mask(ChemicalClass chemical,
                                      const model::Topology& topology) {
  if (chemical == ChemicalClass::donor || chemical == ChemicalClass::acceptor) {
    const auto typing = analysis::resolve_hydrogen_bond_typing(topology);
    if (!typing.has_value())
      return operation::Result<Mask>::failure(typing.error());
    return operation::Result<Mask>::success(
        chemical == ChemicalClass::donor ? typing.value().donors
                                         : typing.value().acceptors);
  }
  std::vector<std::uint8_t> residue_has_carbon(topology.residue_count(), 0U);
  for (const auto& atom : topology.atoms()) {
    if (atom.atomic_number == 6U) residue_has_carbon[atom.residue.value] = 1U;
  }
  const model::BooleanColumn* hetero = nullptr;
  for (const auto name : {"pdb.is_hetero", "mmcif.is_hetero",
                          "pqr.is_hetero"}) {
    const auto* property = topology.properties().find(name);
    if (property == nullptr) continue;
    hetero = std::get_if<model::BooleanColumn>(&property->values);
    if (hetero == nullptr) {
      return operation::Result<Mask>::failure(
          invalid(std::string{name} + " atom property must be boolean"));
    }
    break;
  }
  Mask result(topology.atom_count(), 0U);
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    const auto& atom = topology.atoms()[index];
    const auto& residue = topology.residues()[atom.residue.value];
    const bool typed = residue.chemical_origin !=
                       model::ChemicalAnnotationOrigin::unspecified;
    const bool protein = typed
                             ? residue.polymer_type == model::PolymerType::protein
                             : protein_residue(residue.name);
    const bool nucleic = typed
                             ? residue.polymer_type == model::PolymerType::dna ||
                                   residue.polymer_type == model::PolymerType::rna
                             : nucleic_residue(residue.name);
    const bool polymer = typed
                             ? residue.polymer_type != model::PolymerType::none
                             : protein || nucleic;
    const bool solvent = typed
                             ? residue.kind == model::ResidueKind::solvent
                             : solvent_residue(residue.name);
    const bool backbone = protein ? protein_backbone_name(atom.name)
                                  : nucleic && nucleic_backbone_name(atom.name);
    bool matches{};
    switch (chemical) {
      case ChemicalClass::hetero:
        matches = hetero != nullptr && hetero->values[index] != 0U;
        break;
      case ChemicalClass::hydrogen: matches = atom.atomic_number == 1U; break;
      case ChemicalClass::polymer: matches = polymer; break;
      case ChemicalClass::protein: matches = protein; break;
      case ChemicalClass::nucleic: matches = nucleic; break;
      case ChemicalClass::organic:
        matches = !polymer && !solvent &&
                  residue_has_carbon[atom.residue.value] != 0U;
        break;
      case ChemicalClass::inorganic:
        matches = !polymer && !solvent &&
                  residue_has_carbon[atom.residue.value] == 0U;
        break;
      case ChemicalClass::solvent: matches = solvent; break;
      case ChemicalClass::metal: matches = metal_element(atom.atomic_number); break;
      case ChemicalClass::backbone: matches = backbone; break;
      case ChemicalClass::sidechain: matches = polymer && !backbone; break;
      case ChemicalClass::guide:
        matches = (protein && atom.name == "CA") ||
                  (nucleic && atom.name == "C4'");
        break;
      case ChemicalClass::donor:
      case ChemicalClass::acceptor: break;
    }
    result[index] = matches ? 1U : 0U;
  }
  return operation::Result<Mask>::success(std::move(result));
}

operation::Result<Mask> evaluate_node(const std::shared_ptr<const Node>& node,
                                      const model::Topology& topology,
                                      const NamedResolver& named_resolver,
                                      const EvaluationContext& context) {
  if (node == nullptr) {
    return operation::Result<Mask>::failure(
        invalid("selection expression has no root"));
  }
  if (node->kind == NodeKind::all || node->kind == NodeKind::none) {
    return operation::Result<Mask>::success(
        Mask(topology.atom_count(), node->kind == NodeKind::all ? 1U : 0U));
  }
  if (node->kind == NodeKind::scene_center ||
      node->kind == NodeKind::rotation_origin) {
    return operation::Result<Mask>::success(Mask(topology.atom_count(), 0U));
  }
  if (node->kind == NodeKind::present) {
    if (context.frame == nullptr) {
      return operation::Result<Mask>::failure(invalid(
          "present selection requires a current frame"));
    }
    return operation::Result<Mask>::success(context.frame->presence());
  }
  if (node->kind == NodeKind::bonded) {
    Mask bonded(topology.atom_count(), 0U);
    for (const auto& bond : topology.bonds()) {
      bonded[bond.first.value] = 1U;
      bonded[bond.second.value] = 1U;
    }
    return operation::Result<Mask>::success(std::move(bonded));
  }
  if (node->kind == NodeKind::boolean_property) {
    Predicate predicate{Field::property, "true", false, node->name};
    return predicate_mask(predicate, topology, context);
  }
  if (node->kind == NodeKind::chemical) {
    return chemical_mask(node->chemical, topology);
  }
  if (node->kind == NodeKind::predicate) {
    return predicate_mask(node->predicate, topology, context);
  }
  if (node->kind == NodeKind::numeric_comparison) {
    return numeric_comparison_mask(node->numeric_comparison, topology, context);
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
  if (node->kind == NodeKind::expansion) {
    auto selected = evaluate_node(node->left, topology, named_resolver, context);
    if (!selected.has_value()) return selected;
    return expand_mask(node->expansion, std::move(selected.value()), topology,
                       context);
  }
  if (node->kind == NodeKind::spatial) {
    const auto left_is_pseudo =
        node->left->kind == NodeKind::scene_center ||
        node->left->kind == NodeKind::rotation_origin;
    const auto right_is_pseudo =
        node->right != nullptr &&
        (node->right->kind == NodeKind::scene_center ||
         node->right->kind == NodeKind::rotation_origin);
    if (left_is_pseudo || right_is_pseudo) {
      const auto pseudo = pseudo_point(
          left_is_pseudo ? node->left->kind : node->right->kind, topology,
          context);
      if (!pseudo.has_value())
        return operation::Result<Mask>::failure(pseudo.error());
      Mask candidates(topology.atom_count(), 1U);
      if (right_is_pseudo) {
        auto evaluated =
            evaluate_node(node->left, topology, named_resolver, context);
        if (!evaluated.has_value()) return evaluated;
        candidates = std::move(evaluated.value());
      }
      return spatial_pseudo_mask(node->spatial, std::move(candidates),
                                 pseudo.value(), topology, context);
    }
    auto left = evaluate_node(node->left, topology, named_resolver, context);
    if (!left.has_value()) return left;
    std::optional<Mask> right;
    if (node->right != nullptr) {
      auto evaluated_right =
          evaluate_node(node->right, topology, named_resolver, context);
      if (!evaluated_right.has_value()) return evaluated_right;
      right = std::move(evaluated_right.value());
    }
    return spatial_mask(node->spatial, std::move(left.value()),
                        std::move(right), topology, context);
  }
  if (node->kind == NodeKind::match) {
    auto left = evaluate_node(node->left, topology, named_resolver, context);
    if (!left.has_value()) return left;
    auto right = evaluate_node(node->right, topology, named_resolver, context);
    if (!right.has_value()) return right;
    return operation::Result<Mask>::success(match_mask(
        node->match, std::move(left.value()), right.value(), topology));
  }

  auto left = evaluate_node(node->left, topology, named_resolver, context);
  if (!left.has_value()) return left;
  if (node->kind == NodeKind::negation) {
    for (auto& value : left.value()) value = value == 0U ? 1U : 0U;
    return left;
  }
  if (node->kind == NodeKind::first || node->kind == NodeKind::last) {
    Mask positioned(left.value().size(), 0U);
    if (node->kind == NodeKind::first) {
      const auto found = std::find(left.value().begin(), left.value().end(), 1U);
      if (found != left.value().end())
        positioned[static_cast<std::size_t>(found - left.value().begin())] = 1U;
    } else {
      const auto found = std::find(left.value().rbegin(), left.value().rend(), 1U);
      if (found != left.value().rend()) {
        const auto offset = static_cast<std::size_t>(
            std::distance(left.value().begin(), found.base()) - 1);
        positioned[offset] = 1U;
      }
    }
    return operation::Result<Mask>::success(std::move(positioned));
  }
  auto right = evaluate_node(node->right, topology, named_resolver, context);
  if (!right.has_value()) return right;
  for (std::size_t index = 0; index < left.value().size(); ++index) {
    if (node->kind == NodeKind::conjunction) {
      left.value()[index] =
          left.value()[index] != 0U && right.value()[index] != 0U ? 1U : 0U;
    } else if (node->kind == NodeKind::subtraction) {
      left.value()[index] =
          left.value()[index] != 0U && right.value()[index] == 0U ? 1U : 0U;
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
                                 const NamedResolver& named_resolver,
                                 const EvaluationContext& context) {
  if (context.frame != nullptr &&
      context.frame->atom_count() != topology.atom_count()) {
    return operation::Result<Mask>::failure(
        invalid("selection frame atom count does not match topology"));
  }
  return evaluate_node(expression.root(), topology, named_resolver, context);
}

bool mask_is_valid(std::span<const std::uint8_t> mask,
                   std::size_t atom_count) noexcept {
  return mask.size() == atom_count &&
         std::all_of(mask.begin(), mask.end(),
                     [](std::uint8_t value) { return value <= 1U; });
}

}  // namespace molshredder::selection
