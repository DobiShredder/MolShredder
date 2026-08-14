#include "structure_reader_internal.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
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
  std::size_t count{};
  SourceLine header;
  std::vector<SourceLine> data;
};

struct ParsedAtom {
  std::int64_t source_id{};
  std::string segment;
  std::int64_t residue_id{};
  std::string insertion;
  std::string residue_name;
  std::string atom_name;
  std::string atom_type;
  double charge{};
  double mass{};
  std::int64_t unused{};
  std::size_t line{};
};

std::vector<SourceLine> source_lines(std::string_view content) {
  std::vector<SourceLine> result;
  std::size_t position{};
  std::size_t number{1U};
  while (position < content.size()) {
    const auto end = content.find('\n', position);
    auto line = content.substr(position, end == std::string_view::npos
                                             ? content.size() - position
                                             : end - position);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1U);
    result.push_back({std::string{line}, number++});
    if (end == std::string_view::npos)
      break;
    position = end + 1U;
  }
  return result;
}

std::vector<std::string_view> tokens(std::string_view line) {
  std::vector<std::string_view> result;
  std::size_t position{};
  while (position < line.size()) {
    while (position < line.size() &&
           std::isspace(static_cast<unsigned char>(line[position])) != 0) {
      ++position;
    }
    if (position == line.size())
      break;
    const auto first = position;
    while (position < line.size() &&
           std::isspace(static_cast<unsigned char>(line[position])) == 0) {
      ++position;
    }
    result.push_back(line.substr(first, position - first));
  }
  return result;
}

template <typename Value>
Result<Value> integer(std::string_view raw, std::string_view source,
                      std::size_t line, std::string_view field) {
  Value value{};
  const auto parsed =
      std::from_chars(raw.data(), raw.data() + raw.size(), value);
  if (raw.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != raw.data() + raw.size()) {
    return Result<Value>::failure(parse_error(
        source, line,
        "invalid PSF " + std::string{field} + ": " + std::string{raw}));
  }
  return Result<Value>::success(value);
}

Result<double> real(std::string_view raw, std::string_view source,
                    std::size_t line, std::string_view field) {
  double value{};
  const auto parsed =
      std::from_chars(raw.data(), raw.data() + raw.size(), value);
  if (raw.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != raw.data() + raw.size() || !std::isfinite(value)) {
    return Result<double>::failure(parse_error(
        source, line,
        "invalid PSF " + std::string{field} + ": " + std::string{raw}));
  }
  return Result<double>::success(value);
}

std::string uppercase(std::string_view value) {
  std::string result{value};
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return result;
}

std::optional<Section> section_header(const SourceLine &line,
                                      std::string_view source,
                                      std::optional<operation::Error> &error) {
  const auto bang = line.text.find('!');
  if (bang == std::string::npos)
    return std::nullopt;
  const auto prefix = tokens(std::string_view{line.text}.substr(0U, bang));
  if (prefix.empty())
    return std::nullopt;
  const auto count = integer<std::uint64_t>(prefix.front(), source, line.number,
                                            "section count");
  if (!count.has_value()) {
    error = count.error();
    return std::nullopt;
  }
  if (count.value() > std::numeric_limits<std::size_t>::max()) {
    error = parse_error(source, line.number, "PSF section count is too large");
    return std::nullopt;
  }
  auto name_text = std::string_view{line.text}.substr(bang + 1U);
  const auto name_end = name_text.find_first_of(": \t\r");
  const auto name = uppercase(name_text.substr(0U, name_end));
  if (name.empty()) {
    error = parse_error(source, line.number, "PSF section name is empty");
    return std::nullopt;
  }
  return Section{name, static_cast<std::size_t>(count.value()), line, {}};
}

Result<std::pair<std::int64_t, std::string>>
residue_identity(std::string_view raw, std::string_view source,
                 std::size_t line) {
  if (raw.empty()) {
    return Result<std::pair<std::int64_t, std::string>>::failure(
        parse_error(source, line, "PSF residue identifier is empty"));
  }
  std::size_t digits{};
  if (raw[0] == '+' || raw[0] == '-')
    digits = 1U;
  const auto first_digit = digits;
  while (digits < raw.size() && raw[digits] >= '0' && raw[digits] <= '9') {
    ++digits;
  }
  if (digits == first_digit) {
    return Result<std::pair<std::int64_t, std::string>>::failure(parse_error(
        source, line, "PSF residue identifier must begin with an integer"));
  }
  const auto number = integer<std::int64_t>(raw.substr(0U, digits), source,
                                            line, "residue identifier");
  if (!number.has_value()) {
    return Result<std::pair<std::int64_t, std::string>>::failure(
        number.error());
  }
  return Result<std::pair<std::int64_t, std::string>>::success(
      {number.value(), std::string{raw.substr(digits)}});
}

Result<ParsedAtom> parse_atom(const SourceLine &line, std::string_view source) {
  const auto fields = tokens(line.text);
  if (fields.size() != 9U) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line.number,
        "PSF atom record requires id, segment, residue, residue name, atom "
        "name, atom type, charge, mass and unused fields"));
  }
  const auto id =
      integer<std::int64_t>(fields[0], source, line.number, "atom identifier");
  const auto residue = residue_identity(fields[2], source, line.number);
  const auto charge = real(fields[6], source, line.number, "partial charge");
  const auto mass = real(fields[7], source, line.number, "atomic mass");
  const auto unused = integer<std::int64_t>(fields[8], source, line.number,
                                            "unused atom field");
  if (!id.has_value())
    return Result<ParsedAtom>::failure(id.error());
  if (!residue.has_value()) {
    return Result<ParsedAtom>::failure(residue.error());
  }
  if (!charge.has_value())
    return Result<ParsedAtom>::failure(charge.error());
  if (!mass.has_value())
    return Result<ParsedAtom>::failure(mass.error());
  if (!unused.has_value())
    return Result<ParsedAtom>::failure(unused.error());
  if (id.value() <= 0 || fields[1].empty() || fields[3].empty() ||
      fields[4].empty() || fields[5].empty() || mass.value() < 0.0) {
    return Result<ParsedAtom>::failure(
        parse_error(source, line.number,
                    "PSF atom identifiers/names/types must be valid and mass "
                    "must be non-negative"));
  }
  return Result<ParsedAtom>::success(
      {id.value(), std::string{fields[1]}, residue.value().first,
       std::move(residue.value().second), std::string{fields[3]},
       std::string{fields[4]}, std::string{fields[5]}, charge.value(),
       mass.value(), unused.value(), line.number});
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
  double best_delta = std::numeric_limits<double>::max();
  for (const auto &candidate : candidates) {
    const auto delta = std::abs(mass - candidate.mass);
    if (delta < best_delta) {
      best = &candidate;
      best_delta = delta;
    }
  }
  if (best == nullptr ||
      best_delta > std::max(0.25, std::abs(best->mass) * 0.02)) {
    return 0U;
  }
  return best->number;
}

Result<std::vector<std::int64_t>> section_integers(const Section &section,
                                                   std::size_t arity,
                                                   std::string_view source) {
  if (arity != 0U &&
      section.count > std::numeric_limits<std::size_t>::max() / arity) {
    return Result<std::vector<std::int64_t>>::failure(parse_error(
        source, section.header.number, "PSF connectivity count overflows"));
  }
  const auto expected = section.count * arity;
  std::vector<std::int64_t> values;
  values.reserve(expected);
  for (const auto &line : section.data) {
    for (const auto token : tokens(line.text)) {
      const auto value =
          integer<std::int64_t>(token, source, line.number, "atom reference");
      if (!value.has_value()) {
        return Result<std::vector<std::int64_t>>::failure(value.error());
      }
      values.push_back(value.value());
    }
  }
  if (values.size() != expected) {
    return Result<std::vector<std::int64_t>>::failure(parse_error(
        source, section.header.number,
        "PSF " + section.name + " declares " + std::to_string(section.count) +
            " records but contains " + std::to_string(values.size()) +
            " integer fields",
        "provide exactly " + std::to_string(expected) + " integer fields"));
  }
  return Result<std::vector<std::int64_t>>::success(std::move(values));
}

std::string title_from(const Section &title) {
  for (const auto &line : title.data) {
    auto value = trim(line.text);
    if (value.empty())
      continue;
    constexpr std::string_view remarks{"REMARKS"};
    if (uppercase(value).starts_with(remarks)) {
      value = trim(std::string_view{value}.substr(remarks.size()));
    }
    if (!value.empty())
      return value;
  }
  return {};
}

operation::Error unsupported(std::string_view source, const Section &section,
                             std::string message) {
  return operation::Error{
      operation::ErrorCode::unsupported,
      std::string{source} + ":" + std::to_string(section.header.number) + ": " +
          std::move(message),
      "convert to an X-PLOR/NAMD PSF without unsupported force-field sections"};
}

} // namespace

operation::Result<StructureDocument> read_psf(std::string_view content,
                                              std::string source_name) {
  const auto lines = source_lines(content);
  const auto header =
      std::find_if(lines.begin(), lines.end(),
                   [](const auto &line) { return !trim(line.text).empty(); });
  if (header == lines.end()) {
    return Result<StructureDocument>::failure(
        parse_error(source_name, 1U, "PSF input is empty"));
  }
  const auto header_text = trim(header->text);
  const auto header_fields = tokens(header_text);
  if (header_fields.empty() || uppercase(header_fields.front()) != "PSF") {
    return Result<StructureDocument>::failure(
        parse_error(source_name, header->number, "PSF header is missing"));
  }
  std::set<std::string, std::less<>> flags;
  for (std::size_t index = 1U; index < header_fields.size(); ++index) {
    const auto flag = uppercase(header_fields[index]);
    if (flag != "NAMD" && flag != "EXT" && flag != "XPLOR" && flag != "CMAP") {
      return Result<StructureDocument>::failure(operation::Error{
          operation::ErrorCode::unsupported,
          source_name + ":" + std::to_string(header->number) +
              ": unsupported PSF header flag: " + flag,
          "use standard, NAMD whitespace-delimited or EXT X-PLOR PSF"});
    }
    flags.insert(flag);
  }

  std::vector<Section> sections;
  for (auto line = std::next(header); line != lines.end(); ++line) {
    std::optional<operation::Error> error;
    auto next = section_header(*line, source_name, error);
    if (error.has_value())
      return Result<StructureDocument>::failure(*error);
    if (next.has_value()) {
      if (std::any_of(sections.begin(), sections.end(), [&](const auto &item) {
            return item.name == next->name;
          })) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, line->number, "duplicate PSF section: " + next->name));
      }
      sections.push_back(std::move(*next));
    } else if (!sections.empty()) {
      sections.back().data.push_back(*line);
    } else if (!trim(line->text).empty()) {
      return Result<StructureDocument>::failure(
          parse_error(source_name, line->number,
                      "unexpected data before the first PSF section"));
    }
  }
  const auto find_section = [&](std::string_view name) -> const Section * {
    const auto found = std::find_if(
        sections.begin(), sections.end(),
        [name](const auto &section) { return section.name == name; });
    return found == sections.end() ? nullptr : &*found;
  };
  const auto *title = find_section("NTITLE");
  const auto *atom_section = find_section("NATOM");
  if (title == nullptr || atom_section == nullptr) {
    return Result<StructureDocument>::failure(parse_error(
        source_name, header->number, "PSF requires NTITLE and NATOM sections"));
  }
  const auto title_record_count = static_cast<std::size_t>(
      std::count_if(title->data.begin(), title->data.end(),
                    [](const auto &line) { return !trim(line.text).empty(); }));
  if (title_record_count != title->count) {
    return Result<StructureDocument>::failure(parse_error(
        source_name, title->header.number,
        "PSF NTITLE count does not match the number of title records"));
  }
  std::vector<SourceLine> atom_lines;
  for (const auto &line : atom_section->data) {
    if (!trim(line.text).empty())
      atom_lines.push_back(line);
  }
  if (atom_lines.size() != atom_section->count || atom_lines.empty()) {
    return Result<StructureDocument>::failure(parse_error(
        source_name, atom_section->header.number,
        "PSF NATOM count does not match the number of atom records"));
  }
  std::vector<ParsedAtom> atoms;
  atoms.reserve(atom_lines.size());
  std::map<std::int64_t, model::AtomIndex> source_indices;
  for (const auto &line : atom_lines) {
    auto atom = parse_atom(line, source_name);
    if (!atom.has_value())
      return Result<StructureDocument>::failure(atom.error());
    if (source_indices.contains(atom.value().source_id)) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, line.number, "duplicate PSF atom identifier"));
    }
    source_indices.emplace(atom.value().source_id,
                           model::AtomIndex{atoms.size()});
    atoms.push_back(std::move(atom.value()));
  }

  model::TopologyBuilder builder;
  using ResidueKey = std::tuple<std::string, std::int64_t, std::string>;
  struct ResidueValue {
    model::ResidueIndex index;
    std::string name;
  };
  std::map<ResidueKey, ResidueValue> residues;
  std::vector<std::string> atom_types;
  std::vector<double> charges;
  std::vector<double> masses;
  std::vector<std::int64_t> unused_values;
  model::BooleanColumn inferred_elements;
  for (const auto &atom : atoms) {
    const ResidueKey key{atom.segment, atom.residue_id, atom.insertion};
    auto residue = residues.find(key);
    if (residue == residues.end()) {
      auto added = builder.add_residue({atom.residue_name, atom.residue_id,
                                        atom.insertion, "", atom.segment});
      if (!added.has_value())
        return Result<StructureDocument>::failure(added.error());
      residue =
          residues.emplace(key, ResidueValue{added.value(), atom.residue_name})
              .first;
    } else if (residue->second.name != atom.residue_name) {
      return Result<StructureDocument>::failure(
          parse_error(source_name, atom.line,
                      "PSF atoms with the same segment/residue identity "
                      "disagree on residue name"));
    }
    const auto element = element_from_mass(atom.mass);
    auto added =
        builder.add_atom({atom.atom_name, element, residue->second.index, "", 0,
                          atom.source_id});
    if (!added.has_value())
      return Result<StructureDocument>::failure(added.error());
    atom_types.push_back(atom.atom_type);
    charges.push_back(atom.charge);
    masses.push_back(atom.mass);
    unused_values.push_back(atom.unused);
    inferred_elements.values.push_back(element == 0U ? 0U : 1U);
  }

  const auto atom_index =
      [&](std::int64_t source_id,
          const Section &section) -> Result<model::AtomIndex> {
    const auto found = source_indices.find(source_id);
    if (found == source_indices.end()) {
      return Result<model::AtomIndex>::failure(
          parse_error(source_name, section.header.number,
                      "PSF " + section.name + " references unknown atom " +
                          std::to_string(source_id)));
    }
    return Result<model::AtomIndex>::success(found->second);
  };
  for (const auto &[name, arity] :
       std::vector<std::pair<std::string_view, std::size_t>>{
           {"NBOND", 2U}, {"NTHETA", 3U}, {"NPHI", 4U}, {"NIMPHI", 4U}}) {
    const auto *section = find_section(name);
    if (section == nullptr)
      continue;
    const auto values = section_integers(*section, arity, source_name);
    if (!values.has_value())
      return Result<StructureDocument>::failure(values.error());
    for (std::size_t offset = 0U; offset < values.value().size();
         offset += arity) {
      std::vector<model::AtomIndex> indices;
      indices.reserve(arity);
      for (std::size_t field = 0U; field < arity; ++field) {
        auto index = atom_index(values.value()[offset + field], *section);
        if (!index.has_value())
          return Result<StructureDocument>::failure(index.error());
        indices.push_back(index.value());
      }
      std::optional<operation::Error> error;
      if (name == "NBOND") {
        error = builder.add_bond(
            {indices[0], indices[1], model::BondOrder::unknown});
      } else if (name == "NTHETA") {
        error = builder.add_angle({indices[0], indices[1], indices[2]});
      } else if (name == "NPHI") {
        error = builder.add_dihedral(
            {indices[0], indices[1], indices[2], indices[3]}, true);
      } else {
        error = builder.add_improper(
            {indices[0], indices[1], indices[2], indices[3]}, true);
      }
      if (error.has_value())
        return Result<StructureDocument>::failure(*error);
    }
  }

  const std::set<std::string, std::less<>> auxiliary{
      "NTITLE", "NATOM", "NBOND", "NTHETA", "NPHI", "NIMPHI",
      "NDON",   "NACC",  "NNB",   "NGRP",   "MOLNT"};
  const std::set<std::string, std::less<>> unsupported_sections{
      "NCRTERM", "NCMAP", "NUMLP", "NUMLPH", "NUMANISO"};
  for (const auto &section : sections) {
    if (unsupported_sections.contains(section.name) && section.count != 0U) {
      return Result<StructureDocument>::failure(
          unsupported(source_name, section,
                      "PSF section " + section.name +
                          " cannot yet be represented without data loss"));
    }
    if (!auxiliary.contains(section.name) &&
        !unsupported_sections.contains(section.name) && section.count != 0U) {
      return Result<StructureDocument>::failure(
          unsupported(source_name, section,
                      "unknown non-empty PSF section: " + section.name));
    }
    if ((section.name == "NDON" || section.name == "NACC" ||
         section.name == "NNB" || section.name == "NGRP" ||
         section.name == "MOLNT") &&
        section.count != 0U) {
      builder.set_source_metadata("psf.unmodeled." + section.name,
                                  std::to_string(section.count));
    }
  }
  for (auto property :
       {std::tuple<std::string, model::AtomPropertyColumn,
                   model::PropertyMetadata>{
            "psf.atom_type",
            std::move(atom_types),
            {std::nullopt, "PSF NATOM atom type", {}}},
        {"partial_charge",
         std::move(charges),
         {"elementary_charge", "PSF NATOM charge", {}}},
        {"mass", std::move(masses), {"dalton", "PSF NATOM mass", {}}},
        {"psf.unused",
         std::move(unused_values),
         {std::nullopt, "PSF NATOM unused field", {}}},
        {"psf.element_inferred",
         std::move(inferred_elements),
         {std::nullopt, "PSF NATOM mass inference", {}}}}) {
    if (const auto error = builder.add_property(
            std::move(std::get<0>(property)), std::move(std::get<1>(property)),
            std::move(std::get<2>(property)));
        error.has_value()) {
      return Result<StructureDocument>::failure(*error);
    }
  }
  builder.set_source_metadata("format", "psf");
  builder.set_source_metadata("psf.variant", flags.contains("NAMD") ? "namd"
                                             : flags.contains("EXT")
                                                 ? "ext"
                                                 : "standard");
  const auto title_text = title_from(*title);
  if (!title_text.empty())
    builder.set_source_metadata("psf.title", title_text);
  builder.set_source_metadata("psf.coordinates", "absent");
  builder.set_source_metadata("psf.improper_order", "source_quartet");
  const auto topology = builder.build();
  if (!topology.has_value())
    return Result<StructureDocument>::failure(topology.error());
  const auto coordinates =
      model::InMemoryCoordinateSource::create(atoms.size(), {});
  if (!coordinates.has_value()) {
    return Result<StructureDocument>::failure(coordinates.error());
  }
  auto name = title_text.empty() ? source_name : title_text;
  return Result<StructureDocument>::success(
      {StructureFormat::psf,
       source_name,
       {{std::move(name),
         topology.value(),
         coordinates.value(),
         {{"coordinate_state", "absent"}, {"topology_format", "psf"}}}}});
}

} // namespace molshredder::io::detail
