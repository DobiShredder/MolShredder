#include "structure_reader_internal.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace molshredder::io::detail {
namespace {

using operation::Result;

struct SourceLine {
  std::string text;
  std::size_t number{};
};

struct Section {
  std::string name;
  char kind{};
  std::size_t width{};
  std::size_t header_line{};
  std::vector<std::string> comments;
  std::vector<SourceLine> data;
};

std::vector<SourceLine> source_lines(std::string_view content) {
  std::vector<SourceLine> result;
  std::size_t position{};
  std::size_t line_number{1U};
  while (position < content.size()) {
    const auto end = content.find('\n', position);
    auto line = content.substr(position, end == std::string_view::npos
                                             ? content.size() - position
                                             : end - position);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1U);
    result.push_back({std::string{line}, line_number++});
    if (end == std::string_view::npos)
      break;
    position = end + 1U;
  }
  return result;
}

std::string uppercase(std::string_view value) {
  std::string result{value};
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return result;
}

Result<std::pair<char, std::size_t>>
parse_format(std::string_view raw, std::string_view source, std::size_t line) {
  const auto open = raw.find('(');
  const auto close = raw.rfind(')');
  if (open == std::string_view::npos || close == std::string_view::npos ||
      close <= open + 1U) {
    return Result<std::pair<char, std::size_t>>::failure(
        parse_error(source, line, "invalid Amber %FORMAT descriptor"));
  }
  auto descriptor = uppercase(trim(raw.substr(open + 1U, close - open - 1U)));
  const auto kind_position = descriptor.find_first_of("AIEFD");
  if (kind_position == std::string::npos) {
    return Result<std::pair<char, std::size_t>>::failure(parse_error(
        source, line, "unsupported Amber %FORMAT field kind: " + descriptor));
  }
  const auto width_begin = kind_position + 1U;
  auto width_end = width_begin;
  while (width_end < descriptor.size() &&
         std::isdigit(static_cast<unsigned char>(descriptor[width_end])) != 0) {
    ++width_end;
  }
  std::size_t width{};
  const auto parsed = molshredder::core::from_chars(descriptor.data() + width_begin,
                                      descriptor.data() + width_end, width);
  if (width_begin == width_end || parsed.ec != std::errc{} || width == 0U ||
      width > 1024U) {
    return Result<std::pair<char, std::size_t>>::failure(
        parse_error(source, line, "invalid Amber %FORMAT field width"));
  }
  auto kind = descriptor[kind_position];
  if (kind == 'D')
    kind = 'E';
  return Result<std::pair<char, std::size_t>>::success({kind, width});
}

Result<std::map<std::string, Section, std::less<>>>
parse_sections(std::string_view content, std::string_view source) {
  const auto lines = source_lines(content);
  const auto first =
      std::find_if(lines.begin(), lines.end(),
                   [](const auto &line) { return !trim(line.text).empty(); });
  if (first == lines.end() ||
      !uppercase(trim(first->text)).starts_with("%VERSION")) {
    return Result<std::map<std::string, Section, std::less<>>>::failure(
        parse_error(source, first == lines.end() ? 1U : first->number,
                    "Amber prmtop requires a %VERSION header"));
  }

  std::map<std::string, Section, std::less<>> sections;
  Section *current{};
  bool awaiting_format{};
  for (auto line = std::next(first); line != lines.end(); ++line) {
    const auto cleaned = trim(line->text);
    if (cleaned.empty())
      continue;
    const auto upper = uppercase(cleaned);
    if (upper.starts_with("%FLAG")) {
      if (awaiting_format) {
        return Result<std::map<std::string, Section, std::less<>>>::failure(
            parse_error(source, line->number,
                        "Amber %FLAG is missing its %FORMAT descriptor"));
      }
      const auto name = uppercase(trim(std::string_view{cleaned}.substr(5U)));
      if (name.empty() || sections.contains(name)) {
        return Result<std::map<std::string, Section, std::less<>>>::failure(
            parse_error(source, line->number,
                        name.empty() ? "empty Amber %FLAG name"
                                     : "duplicate Amber %FLAG: " + name));
      }
      auto inserted =
          sections.emplace(name, Section{name, 0, 0U, line->number, {}, {}});
      current = &inserted.first->second;
      awaiting_format = true;
      continue;
    }
    if (upper.starts_with("%COMMENT")) {
      if (current == nullptr || !awaiting_format) {
        return Result<std::map<std::string, Section, std::less<>>>::failure(
            parse_error(source, line->number, "unexpected Amber %COMMENT"));
      }
      current->comments.push_back(
          trim(std::string_view{cleaned}.substr(8U)));
      continue;
    }
    if (upper.starts_with("%FORMAT")) {
      if (current == nullptr || !awaiting_format) {
        return Result<std::map<std::string, Section, std::less<>>>::failure(
            parse_error(source, line->number, "unexpected Amber %FORMAT"));
      }
      const auto format = parse_format(cleaned, source, line->number);
      if (!format.has_value()) {
        return Result<std::map<std::string, Section, std::less<>>>::failure(
            format.error());
      }
      current->kind = format.value().first;
      current->width = format.value().second;
      awaiting_format = false;
      continue;
    }
    if (current == nullptr || awaiting_format) {
      return Result<std::map<std::string, Section, std::less<>>>::failure(
          parse_error(source, line->number,
                      "unexpected data outside an Amber %FLAG section"));
    }
    current->data.push_back(*line);
  }
  if (awaiting_format) {
    return Result<std::map<std::string, Section, std::less<>>>::failure(
        parse_error(source, current->header_line,
                    "Amber %FLAG is missing its %FORMAT descriptor"));
  }
  return Result<std::map<std::string, Section, std::less<>>>::success(
      std::move(sections));
}

Result<std::vector<std::string>> fields(const Section &section,
                                        std::string_view source) {
  if (section.width == 0U || section.kind == 0) {
    return Result<std::vector<std::string>>::failure(
        parse_error(source, section.header_line,
                    "Amber section has no usable %FORMAT descriptor"));
  }
  std::vector<std::string> result;
  for (const auto &line : section.data) {
    for (std::size_t offset = 0U; offset < line.text.size();
         offset += section.width) {
      const auto field = trim(std::string_view{line.text}.substr(
          offset, std::min(section.width, line.text.size() - offset)));
      if (!field.empty())
        result.push_back(field);
    }
  }
  return Result<std::vector<std::string>>::success(std::move(result));
}

Result<std::vector<std::int64_t>> integers(const Section &section,
                                           std::string_view source) {
  if (section.kind != 'I') {
    return Result<std::vector<std::int64_t>>::failure(parse_error(
        source, section.header_line, "Amber integer section has non-I format"));
  }
  const auto raw = fields(section, source);
  if (!raw.has_value()) {
    return Result<std::vector<std::int64_t>>::failure(raw.error());
  }
  std::vector<std::int64_t> result;
  result.reserve(raw.value().size());
  for (const auto &field : raw.value()) {
    std::int64_t value{};
    const auto parsed =
        molshredder::core::from_chars(field.data(), field.data() + field.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != field.data() + field.size()) {
      return Result<std::vector<std::int64_t>>::failure(parse_error(
          source, section.header_line,
          "invalid integer in Amber section " + section.name + ": " + field));
    }
    result.push_back(value);
  }
  return Result<std::vector<std::int64_t>>::success(std::move(result));
}

Result<std::vector<double>> reals(const Section &section,
                                  std::string_view source) {
  if (section.kind != 'E' && section.kind != 'F') {
    return Result<std::vector<double>>::failure(parse_error(
        source, section.header_line, "Amber real section has non-real format"));
  }
  const auto raw = fields(section, source);
  if (!raw.has_value())
    return Result<std::vector<double>>::failure(raw.error());
  std::vector<double> result;
  result.reserve(raw.value().size());
  for (auto field : raw.value()) {
    std::replace(field.begin(), field.end(), 'D', 'E');
    std::replace(field.begin(), field.end(), 'd', 'e');
    double value{};
    const auto parsed =
        molshredder::core::from_chars(field.data(), field.data() + field.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != field.data() + field.size() ||
        !std::isfinite(value)) {
      return Result<std::vector<double>>::failure(parse_error(
          source, section.header_line,
          "invalid real in Amber section " + section.name + ": " + field));
    }
    result.push_back(value);
  }
  return Result<std::vector<double>>::success(std::move(result));
}

std::uint8_t element_from_mass(double mass) {
  struct Candidate {
    std::uint8_t number;
    double mass;
  };
  constexpr Candidate candidates[]{
      {1U, 1.008},   {6U, 12.011},  {7U, 14.007},  {8U, 15.999},
      {9U, 18.998},  {11U, 22.990}, {12U, 24.305}, {15U, 30.974},
      {16U, 32.06},  {17U, 35.45},  {19U, 39.098}, {20U, 40.078},
      {26U, 55.845}, {30U, 65.38},  {35U, 79.904}, {53U, 126.90}};
  const Candidate *best{};
  auto delta = std::numeric_limits<double>::max();
  for (const auto &candidate : candidates) {
    const auto current = std::abs(mass - candidate.mass);
    if (current < delta) {
      best = &candidate;
      delta = current;
    }
  }
  return best != nullptr && delta <= std::max(0.25, best->mass * 0.02)
             ? best->number
             : 0U;
}

template <typename Value>
Result<Value> require_count(Value values, std::size_t expected,
                            const Section &section, std::string_view source) {
  if (values.size() != expected) {
    return Result<Value>::failure(parse_error(
        source, section.header_line,
        "Amber " + section.name + " contains " + std::to_string(values.size()) +
            " fields; expected " + std::to_string(expected)));
  }
  return Result<Value>::success(std::move(values));
}

Result<model::AtomIndex> amber_atom(std::int64_t encoded,
                                    std::size_t atom_count,
                                    std::string_view source,
                                    const Section &section) {
  if (encoded == std::numeric_limits<std::int64_t>::min() ||
      std::abs(encoded) % 3 != 0) {
    return Result<model::AtomIndex>::failure(parse_error(
        source, section.header_line,
        "Amber connectivity atom coordinate index is not divisible by three"));
  }
  const auto index = static_cast<std::uint64_t>(std::abs(encoded) / 3);
  if (index >= atom_count) {
    return Result<model::AtomIndex>::failure(
        parse_error(source, section.header_line,
                    "Amber connectivity references an atom outside NATOM"));
  }
  return Result<model::AtomIndex>::success(
      model::AtomIndex{static_cast<std::size_t>(index)});
}

} // namespace

operation::Result<StructureDocument> read_prmtop(std::string_view content,
                                                 std::string source_name) {
  auto parsed = parse_sections(content, source_name);
  if (!parsed.has_value())
    return Result<StructureDocument>::failure(parsed.error());
  const auto &sections = parsed.value();
  for (const auto &[name, section] : sections) {
    if (name.starts_with("CHARMM_")) {
      return Result<StructureDocument>::failure(operation::Error{
          operation::ErrorCode::unsupported,
          source_name + ":" + std::to_string(section.header_line) +
              ": Chamber CHARMM-specific prmtop sections are not supported",
          "use a standard Amber prmtop or retain this file for a future "
          "Chamber reader"});
    }
  }
  const auto get = [&](std::string_view name) -> const Section * {
    const auto found = sections.find(name);
    return found == sections.end() ? nullptr : &found->second;
  };
  const auto *pointer_section = get("POINTERS");
  const auto *atom_name_section = get("ATOM_NAME");
  const auto *charge_section = get("CHARGE");
  const auto *mass_section = get("MASS");
  const auto *type_section = get("AMBER_ATOM_TYPE");
  const auto *residue_label_section = get("RESIDUE_LABEL");
  const auto *residue_pointer_section = get("RESIDUE_POINTER");
  if (pointer_section == nullptr || atom_name_section == nullptr ||
      charge_section == nullptr || mass_section == nullptr ||
      type_section == nullptr || residue_label_section == nullptr ||
      residue_pointer_section == nullptr) {
    return Result<StructureDocument>::failure(parse_error(
        source_name, 1U,
        "Amber prmtop is missing a required identity/topology section"));
  }
  const auto pointer_values = integers(*pointer_section, source_name);
  if (!pointer_values.has_value()) {
    return Result<StructureDocument>::failure(pointer_values.error());
  }
  if (pointer_values.value().size() < 31U || pointer_values.value()[0] <= 0 ||
      pointer_values.value()[11] <= 0) {
    return Result<StructureDocument>::failure(parse_error(
        source_name, pointer_section->header_line,
        "Amber POINTERS requires at least 31 values and positive NATOM/NRES"));
  }
  for (const auto index : {1U,  2U,  3U,  4U,  5U,  6U,  7U,  10U, 12U, 13U,
                           14U, 15U, 16U, 17U, 18U, 19U, 27U, 28U, 29U, 30U}) {
    if (pointer_values.value()[index] < 0) {
      return Result<StructureDocument>::failure(
          parse_error(source_name, pointer_section->header_line,
                      "Amber POINTERS count fields must be non-negative"));
    }
  }
  if (pointer_values.value()[20] != 0) {
    return Result<StructureDocument>::failure(operation::Error{
        operation::ErrorCode::unsupported,
        source_name + ": perturbation topology (IFPERT) is not supported",
        "use a non-perturbed Amber topology"});
  }
  const auto atom_count = static_cast<std::size_t>(pointer_values.value()[0]);
  const auto residue_count =
      static_cast<std::size_t>(pointer_values.value()[11]);
  std::optional<std::vector<double>> box_dimensions;
  if (pointer_values.value()[27] != 0) {
    const auto *box_section = get("BOX_DIMENSIONS");
    if (box_section == nullptr) {
      return Result<StructureDocument>::failure(
          parse_error(source_name, pointer_section->header_line,
                      "Amber IFBOX is non-zero but BOX_DIMENSIONS is missing"));
    }
    auto values = reals(*box_section, source_name);
    if (!values.has_value()) {
      return Result<StructureDocument>::failure(values.error());
    }
    if (values.value().size() != 4U || values.value()[0] <= 0.0 ||
        values.value()[0] >= 180.0 || values.value()[1] <= 0.0 ||
        values.value()[2] <= 0.0 || values.value()[3] <= 0.0) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, box_section->header_line,
          "Amber BOX_DIMENSIONS requires angle and three positive lengths"));
    }
    box_dimensions = std::move(values.value());
  }
  const auto atom_names_raw = fields(*atom_name_section, source_name);
  const auto charges_raw = reals(*charge_section, source_name);
  const auto masses_raw = reals(*mass_section, source_name);
  const auto atom_types_raw = fields(*type_section, source_name);
  const auto residue_names_raw = fields(*residue_label_section, source_name);
  const auto residue_pointers_raw =
      integers(*residue_pointer_section, source_name);
  if (!atom_names_raw.has_value())
    return Result<StructureDocument>::failure(atom_names_raw.error());
  if (!charges_raw.has_value())
    return Result<StructureDocument>::failure(charges_raw.error());
  if (!masses_raw.has_value())
    return Result<StructureDocument>::failure(masses_raw.error());
  if (!atom_types_raw.has_value())
    return Result<StructureDocument>::failure(atom_types_raw.error());
  if (!residue_names_raw.has_value())
    return Result<StructureDocument>::failure(residue_names_raw.error());
  if (!residue_pointers_raw.has_value())
    return Result<StructureDocument>::failure(residue_pointers_raw.error());
  auto atom_names = require_count(std::move(atom_names_raw.value()), atom_count,
                                  *atom_name_section, source_name);
  auto charges = require_count(std::move(charges_raw.value()), atom_count,
                               *charge_section, source_name);
  auto masses = require_count(std::move(masses_raw.value()), atom_count,
                              *mass_section, source_name);
  auto atom_types = require_count(std::move(atom_types_raw.value()), atom_count,
                                  *type_section, source_name);
  auto residue_names =
      require_count(std::move(residue_names_raw.value()), residue_count,
                    *residue_label_section, source_name);
  auto residue_pointers =
      require_count(std::move(residue_pointers_raw.value()), residue_count,
                    *residue_pointer_section, source_name);
  if (!atom_names.has_value())
    return Result<StructureDocument>::failure(atom_names.error());
  if (!charges.has_value())
    return Result<StructureDocument>::failure(charges.error());
  if (!masses.has_value())
    return Result<StructureDocument>::failure(masses.error());
  if (!atom_types.has_value())
    return Result<StructureDocument>::failure(atom_types.error());
  if (!residue_names.has_value())
    return Result<StructureDocument>::failure(residue_names.error());
  if (!residue_pointers.has_value())
    return Result<StructureDocument>::failure(residue_pointers.error());

  if (residue_pointers.value().front() != 1) {
    return Result<StructureDocument>::failure(
        parse_error(source_name, residue_pointer_section->header_line,
                    "Amber RESIDUE_POINTER must begin with atom 1"));
  }
  for (std::size_t index = 0; index < residue_count; ++index) {
    const auto pointer = residue_pointers.value()[index];
    const auto next = index + 1U < residue_count
                          ? residue_pointers.value()[index + 1U]
                          : static_cast<std::int64_t>(atom_count + 1U);
    if (pointer <= 0 || next <= pointer ||
        next > static_cast<std::int64_t>(atom_count + 1U)) {
      return Result<StructureDocument>::failure(
          parse_error(source_name, residue_pointer_section->header_line,
                      "Amber RESIDUE_POINTER values must be increasing valid "
                      "atom indices"));
    }
  }

  std::vector<std::int64_t> atomic_numbers(atom_count, 0);
  if (const auto *atomic_number_section = get("ATOMIC_NUMBER");
      atomic_number_section != nullptr) {
    auto values = integers(*atomic_number_section, source_name);
    if (!values.has_value())
      return Result<StructureDocument>::failure(values.error());
    auto checked = require_count(std::move(values.value()), atom_count,
                                 *atomic_number_section, source_name);
    if (!checked.has_value())
      return Result<StructureDocument>::failure(checked.error());
    atomic_numbers = std::move(checked.value());
  }

  std::vector<std::int64_t> atom_type_indices(atom_count, 0);
  if (const auto *atom_type_index_section = get("ATOM_TYPE_INDEX");
      atom_type_index_section != nullptr) {
    auto values = integers(*atom_type_index_section, source_name);
    if (!values.has_value())
      return Result<StructureDocument>::failure(values.error());
    auto checked = require_count(std::move(values.value()), atom_count,
                                 *atom_type_index_section, source_name);
    if (!checked.has_value())
      return Result<StructureDocument>::failure(checked.error());
    atom_type_indices = std::move(checked.value());
  }

  model::TopologyBuilder builder;
  std::vector<model::ResidueIndex> residue_indices;
  residue_indices.reserve(residue_count);
  for (std::size_t index = 0; index < residue_count; ++index) {
    auto residue = builder.add_residue({residue_names.value()[index],
                                        static_cast<std::int64_t>(index + 1U),
                                        {},
                                        {},
                                        {}});
    if (!residue.has_value())
      return Result<StructureDocument>::failure(residue.error());
    residue_indices.push_back(residue.value());
  }
  std::size_t residue_index{};
  for (std::size_t index = 0; index < atom_count; ++index) {
    while (residue_index + 1U < residue_count &&
           index + 1U >= static_cast<std::size_t>(
                             residue_pointers.value()[residue_index + 1U])) {
      ++residue_index;
    }
    if (atomic_numbers[index] < 0 || atomic_numbers[index] > 118 ||
        masses.value()[index] < 0.0) {
      return Result<StructureDocument>::failure(
          parse_error(source_name, atom_name_section->header_line,
                      "Amber atom has invalid atomic number or negative mass"));
    }
    const auto number = atomic_numbers[index] == 0
                            ? element_from_mass(masses.value()[index])
                            : static_cast<std::uint8_t>(atomic_numbers[index]);
    auto atom = builder.add_atom({atom_names.value()[index],
                                  number,
                                  residue_indices[residue_index],
                                  {},
                                  0,
                                  static_cast<std::int64_t>(index + 1U)});
    if (!atom.has_value())
      return Result<StructureDocument>::failure(atom.error());
  }

  constexpr double amber_charge_scale = 18.2223;
  std::vector<double> elementary_charges;
  elementary_charges.reserve(atom_count);
  for (const auto charge : charges.value()) {
    elementary_charges.push_back(charge / amber_charge_scale);
  }
  auto property_error = builder.add_property(
      "partial_charge", std::move(elementary_charges),
      {std::string{"elementary_charge"}, "Amber CHARGE / 18.2223", {}});
  if (property_error.has_value())
    return Result<StructureDocument>::failure(*property_error);
  property_error =
      builder.add_property("mass", std::move(masses.value()),
                           {std::string{"dalton"}, "Amber MASS", {}});
  if (property_error.has_value())
    return Result<StructureDocument>::failure(*property_error);
  property_error =
      builder.add_property("amber.atom_type", std::move(atom_types.value()),
                           {std::nullopt, "Amber AMBER_ATOM_TYPE", {}});
  if (property_error.has_value())
    return Result<StructureDocument>::failure(*property_error);
  property_error = builder.add_property(
      "amber.atom_type_index", std::move(atom_type_indices),
      {std::nullopt, "Amber ATOM_TYPE_INDEX", {}});
  if (property_error.has_value())
    return Result<StructureDocument>::failure(*property_error);

  const auto add_connectivity =
      [&](std::string_view section_name, std::size_t arity,
          std::size_t expected_count,
          auto add_term) -> std::optional<operation::Error> {
    const auto *section = get(section_name);
    if (section == nullptr) {
      if (expected_count == 0U)
        return std::nullopt;
      return parse_error(
          source_name, pointer_section->header_line,
          "Amber POINTERS declares records for missing section " +
              std::string{section_name});
    }
    const auto values = integers(*section, source_name);
    if (!values.has_value())
      return values.error();
    if (expected_count > std::numeric_limits<std::size_t>::max() / arity ||
        values.value().size() != expected_count * arity) {
      return parse_error(source_name, section->header_line,
                         "Amber " + std::string{section_name} + " contains " +
                             std::to_string(values.value().size() / arity) +
                             " records; POINTERS declares " +
                             std::to_string(expected_count));
    }
    for (std::size_t offset = 0U; offset < values.value().size();
         offset += arity) {
      std::vector<model::AtomIndex> atoms;
      atoms.reserve(arity - 1U);
      for (std::size_t field = 0U; field + 1U < arity; ++field) {
        auto atom = amber_atom(values.value()[offset + field], atom_count,
                               source_name, *section);
        if (!atom.has_value())
          return atom.error();
        atoms.push_back(atom.value());
      }
      if (values.value()[offset + arity - 1U] <= 0) {
        return parse_error(
            source_name, section->header_line,
            "Amber connectivity parameter index must be positive");
      }
      if (const auto error = add_term(atoms, values.value(), offset);
          error.has_value()) {
        return error;
      }
    }
    return std::nullopt;
  };
  const std::pair<std::string_view, std::size_t> bond_sections[]{
      {"BONDS_INC_HYDROGEN",
       static_cast<std::size_t>(pointer_values.value()[2])},
      {"BONDS_WITHOUT_HYDROGEN",
       static_cast<std::size_t>(pointer_values.value()[3])}};
  for (const auto &[name, count] : bond_sections) {
    if (const auto error = add_connectivity(
            name, 3U, count,
            [&](const auto &atoms, const auto &, std::size_t) {
              return builder.add_bond(
                  {atoms[0], atoms[1], model::BondOrder::unknown});
            });
        error.has_value()) {
      return Result<StructureDocument>::failure(*error);
    }
  }
  const std::pair<std::string_view, std::size_t> angle_sections[]{
      {"ANGLES_INC_HYDROGEN",
       static_cast<std::size_t>(pointer_values.value()[4])},
      {"ANGLES_WITHOUT_HYDROGEN",
       static_cast<std::size_t>(pointer_values.value()[5])}};
  for (const auto &[name, count] : angle_sections) {
    if (const auto error = add_connectivity(
            name, 4U, count,
            [&](const auto &atoms, const auto &, std::size_t) {
              return builder.add_angle({atoms[0], atoms[1], atoms[2]});
            });
        error.has_value()) {
      return Result<StructureDocument>::failure(*error);
    }
  }
  const std::pair<std::string_view, std::size_t> dihedral_sections[]{
      {"DIHEDRALS_INC_HYDROGEN",
       static_cast<std::size_t>(pointer_values.value()[6])},
      {"DIHEDRALS_WITHOUT_HYDROGEN",
       static_cast<std::size_t>(pointer_values.value()[7])}};
  for (const auto &[name, count] : dihedral_sections) {
    if (const auto error = add_connectivity(
            name, 5U, count,
            [&](const auto &atoms, const auto &raw, std::size_t offset) {
              if (raw[offset + 3U] < 0) {
                return builder.add_improper(
                    {atoms[2], atoms[0], atoms[1], atoms[3]}, true);
              }
              return builder.add_dihedral(
                  {atoms[0], atoms[1], atoms[2], atoms[3]}, true);
            });
        error.has_value()) {
      return Result<StructureDocument>::failure(*error);
    }
  }

  if (const auto *radii_section = get("RADII"); radii_section != nullptr) {
    auto values = reals(*radii_section, source_name);
    if (!values.has_value())
      return Result<StructureDocument>::failure(values.error());
    auto checked = require_count(std::move(values.value()), atom_count,
                                 *radii_section, source_name);
    if (!checked.has_value())
      return Result<StructureDocument>::failure(checked.error());
    property_error =
        builder.add_property("amber.gb_radius", std::move(checked.value()),
                             {std::string{"angstrom"}, "Amber RADII", {}});
    if (property_error.has_value())
      return Result<StructureDocument>::failure(*property_error);
  }
  if (const auto *screen_section = get("SCREEN"); screen_section != nullptr) {
    auto values = reals(*screen_section, source_name);
    if (!values.has_value())
      return Result<StructureDocument>::failure(values.error());
    auto checked = require_count(std::move(values.value()), atom_count,
                                 *screen_section, source_name);
    if (!checked.has_value())
      return Result<StructureDocument>::failure(checked.error());
    property_error =
        builder.add_property("amber.gb_screen", std::move(checked.value()),
                             {std::nullopt, "Amber SCREEN", {}});
    if (property_error.has_value())
      return Result<StructureDocument>::failure(*property_error);
  }

  builder.set_source_metadata("format", "amber-prmtop");
  builder.set_source_metadata("coordinates", "absent");
  builder.set_source_metadata("amber.section_count",
                              std::to_string(sections.size()));
  builder.set_source_metadata("amber.charge_scale", "18.2223");
  builder.set_source_metadata("amber.force_parameters", "not-modeled");
  for (const auto &[name, section] : sections) {
    if (section.comments.empty())
      continue;
    std::string joined;
    for (const auto &comment : section.comments) {
      if (!joined.empty())
        joined.push_back('\n');
      joined += comment;
    }
    builder.set_source_metadata("amber.comment." + name, std::move(joined));
  }
  if (box_dimensions.has_value()) {
    builder.set_source_metadata("amber.box_angle_degrees",
                                std::to_string((*box_dimensions)[0]));
    builder.set_source_metadata("amber.box_length_a_angstrom",
                                std::to_string((*box_dimensions)[1]));
    builder.set_source_metadata("amber.box_length_b_angstrom",
                                std::to_string((*box_dimensions)[2]));
    builder.set_source_metadata("amber.box_length_c_angstrom",
                                std::to_string((*box_dimensions)[3]));
  }
  if (const auto *title_section = get("TITLE"); title_section != nullptr) {
    const auto title = fields(*title_section, source_name);
    if (title.has_value() && !title.value().empty()) {
      std::string joined;
      for (const auto &field : title.value())
        joined += field;
      builder.set_source_metadata("title", trim(joined));
    }
  }
  const auto topology = builder.build();
  if (!topology.has_value())
    return Result<StructureDocument>::failure(topology.error());
  const auto coordinates =
      model::InMemoryCoordinateSource::create(atom_count, {});
  if (!coordinates.has_value())
    return Result<StructureDocument>::failure(coordinates.error());
  const auto title = topology.value()->source_metadata().contains("title")
                         ? topology.value()->source_metadata().at("title")
                         : std::string{"Amber topology"};
  return Result<StructureDocument>::success(
      {StructureFormat::prmtop,
       source_name,
       {{title,
         topology.value(),
         coordinates.value(),
         {{"format", "amber-prmtop"}, {"coordinate_frames", "0"}}}}});
}

} // namespace molshredder::io::detail
