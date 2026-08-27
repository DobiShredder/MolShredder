#include "structure_reader_internal.hpp"

#include <algorithm>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <span>
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

std::vector<SourceLine> lines(std::string_view content) {
  std::vector<SourceLine> result;
  std::size_t position{};
  std::size_t number{1U};
  while (position < content.size()) {
    const auto end = content.find('\n', position);
    auto line = content.substr(
        position, end == std::string_view::npos ? content.size() - position
                                                : end - position);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
    result.push_back({std::string{line}, number++});
    if (end == std::string_view::npos) break;
    position = end + 1U;
  }
  return result;
}

std::string_view field(std::string_view line, std::size_t first,
                       std::size_t count) {
  return first >= line.size()
             ? std::string_view{}
             : line.substr(first, std::min(count, line.size() - first));
}

std::vector<std::string_view> tokens(std::string_view line) {
  std::vector<std::string_view> result;
  std::size_t position{};
  while (position < line.size()) {
    while (position < line.size() &&
           std::isspace(static_cast<unsigned char>(line[position])) != 0) {
      ++position;
    }
    if (position == line.size()) break;
    const auto start = position;
    while (position < line.size() &&
           std::isspace(static_cast<unsigned char>(line[position])) == 0) {
      ++position;
    }
    result.push_back(line.substr(start, position - start));
  }
  return result;
}

template <typename Value>
Result<Value> number(std::string_view raw, std::string_view source,
                     std::size_t line, std::string_view name) {
  const auto cleaned = trim(raw);
  Value value{};
  const auto parsed =
      molshredder::core::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), value);
  if (cleaned.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != cleaned.data() + cleaned.size()) {
    return Result<Value>::failure(parse_error(
        source, line,
        "invalid MOL V2000 " + std::string{name} + ": " + cleaned));
  }
  return Result<Value>::success(value);
}

struct ParsedAtom {
  std::uint8_t atomic_number{};
  std::string symbol;
  model::Vec3d position;
  std::int64_t mass_difference{};
  model::AtomStereoParity stereo_parity{model::AtomStereoParity::unspecified};
  std::int32_t formal_charge{};
  bool formal_charge_present{};
  std::optional<std::uint16_t> isotope_mass;
  model::RadicalState radical{model::RadicalState::none};
};

struct ParsedBond {
  std::size_t first{};
  std::size_t second{};
  model::BondOrder order{model::BondOrder::unknown};
  model::BondQuery query{model::BondQuery::none};
  model::BondStereo stereo{model::BondStereo::none};
};

Result<ParsedAtom> parse_atom(const SourceLine& line, std::string_view source) {
  if (line.text.size() < 39U) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line.number,
        "MOL V2000 atom line is shorter than the charge-code field"));
  }
  const auto x = number<double>(field(line.text, 0U, 10U), source, line.number,
                                "atom x coordinate");
  const auto y = number<double>(field(line.text, 10U, 10U), source, line.number,
                                "atom y coordinate");
  const auto z = number<double>(field(line.text, 20U, 10U), source, line.number,
                                "atom z coordinate");
  const auto mass = number<std::int64_t>(field(line.text, 34U, 2U), source,
                                         line.number, "mass difference");
  const auto charge_code = number<std::int64_t>(
      field(line.text, 36U, 3U), source, line.number, "charge code");
  Result<std::int64_t> stereo = Result<std::int64_t>::success(0);
  if (!trim(field(line.text, 39U, 3U)).empty()) {
    stereo = number<std::int64_t>(field(line.text, 39U, 3U), source,
                                  line.number, "atom stereo parity");
  }
  for (const auto* parsed : {&x, &y, &z}) {
    if (!parsed->has_value()) {
      return Result<ParsedAtom>::failure(parsed->error());
    }
  }
  if (!mass.has_value()) return Result<ParsedAtom>::failure(mass.error());
  if (!charge_code.has_value()) {
    return Result<ParsedAtom>::failure(charge_code.error());
  }
  if (!stereo.has_value()) return Result<ParsedAtom>::failure(stereo.error());
  if (!std::isfinite(x.value()) || !std::isfinite(y.value()) ||
      !std::isfinite(z.value())) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line.number, "MOL V2000 coordinates must be finite"));
  }
  const auto symbol = trim(field(line.text, 31U, 3U));
  std::uint8_t atomic_number_value{};
  if (symbol != "*") {
    const auto parsed_element = atomic_number(symbol);
    if (!parsed_element.has_value() || parsed_element.value() == 0U) {
      return Result<ParsedAtom>::failure(parse_error(
          source, line.number, "unknown MOL V2000 atom symbol: " + symbol));
    }
    atomic_number_value = parsed_element.value();
  }
  static const std::map<std::int64_t, std::int32_t> charges{
      {0, 0}, {1, 3}, {2, 2}, {3, 1}, {5, -1}, {6, -2}, {7, -3}};
  if (!charges.contains(charge_code.value()) && charge_code.value() != 4) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line.number, "unsupported MOL V2000 atom charge code"));
  }
  if (mass.value() < -3 || mass.value() > 4 || stereo.value() < 0 ||
      stereo.value() > 3) {
    return Result<ParsedAtom>::failure(parse_error(
        source, line.number,
        "MOL V2000 mass difference or atom stereo parity is out of range"));
  }
  ParsedAtom result;
  result.atomic_number = atomic_number_value;
  result.symbol = symbol == "*" ? "X" : symbol;
  result.position = {x.value(), y.value(), z.value()};
  result.mass_difference = mass.value();
  result.stereo_parity = static_cast<model::AtomStereoParity>(stereo.value());
  result.formal_charge = charge_code.value() == 4
                             ? 0
                             : charges.at(charge_code.value());
  result.formal_charge_present = charge_code.value() != 0 &&
                                 charge_code.value() != 4;
  result.radical = charge_code.value() == 4
                       ? model::RadicalState::doublet
                       : model::RadicalState::none;
  return Result<ParsedAtom>::success(std::move(result));
}

Result<ParsedBond> parse_bond(const SourceLine& line, std::string_view source,
                              std::size_t atom_count) {
  if (line.text.size() < 12U) {
    return Result<ParsedBond>::failure(parse_error(
        source, line.number,
        "MOL V2000 bond line is shorter than the stereo field"));
  }
  const auto first = number<std::size_t>(field(line.text, 0U, 3U), source,
                                         line.number, "first bond atom");
  const auto second = number<std::size_t>(field(line.text, 3U, 3U), source,
                                          line.number, "second bond atom");
  const auto type = number<std::uint64_t>(field(line.text, 6U, 3U), source,
                                          line.number, "bond type");
  const auto stereo = number<std::uint64_t>(field(line.text, 9U, 3U), source,
                                            line.number, "bond stereo");
  if (!first.has_value()) return Result<ParsedBond>::failure(first.error());
  if (!second.has_value()) return Result<ParsedBond>::failure(second.error());
  if (!type.has_value()) return Result<ParsedBond>::failure(type.error());
  if (!stereo.has_value()) return Result<ParsedBond>::failure(stereo.error());
  if (first.value() == 0U || second.value() == 0U ||
      first.value() > atom_count || second.value() > atom_count ||
      first.value() == second.value()) {
    return Result<ParsedBond>::failure(parse_error(
        source, line.number, "MOL V2000 bond references invalid atom indices"));
  }
  model::BondOrder order;
  model::BondQuery query{model::BondQuery::none};
  switch (type.value()) {
    case 1U:
      order = model::BondOrder::single;
      break;
    case 2U:
      order = model::BondOrder::double_bond;
      break;
    case 3U:
      order = model::BondOrder::triple;
      break;
    case 4U:
      order = model::BondOrder::aromatic;
      break;
    case 5U:
      order = model::BondOrder::query;
      query = model::BondQuery::single_or_double;
      break;
    case 6U:
      order = model::BondOrder::query;
      query = model::BondQuery::single_or_aromatic;
      break;
    case 7U:
      order = model::BondOrder::query;
      query = model::BondQuery::double_or_aromatic;
      break;
    case 8U:
      order = model::BondOrder::query;
      query = model::BondQuery::any;
      break;
    default:
      return Result<ParsedBond>::failure(parse_error(
          source, line.number,
          "unsupported MOL V2000 bond type"));
  }
  model::BondStereo stereo_value;
  switch (stereo.value()) {
    case 0U: stereo_value = model::BondStereo::none; break;
    case 1U: stereo_value = model::BondStereo::up; break;
    case 3U: stereo_value = model::BondStereo::cis_or_trans; break;
    case 4U: stereo_value = model::BondStereo::either; break;
    case 6U: stereo_value = model::BondStereo::down; break;
    default:
      return Result<ParsedBond>::failure(parse_error(
          source, line.number, "unsupported MOL V2000 bond stereo code"));
  }
  return Result<ParsedBond>::success(
      {first.value() - 1U, second.value() - 1U, order, query, stereo_value});
}

Result<bool> apply_pairs(const SourceLine& line, std::string_view source,
                         std::string_view keyword,
                         std::vector<ParsedAtom>& atoms) {
  const auto values = tokens(line.text);
  if (values.size() < 3U || values[0] != "M" || values[1] != keyword) {
    return Result<bool>::success(false);
  }
  const auto count =
      number<std::size_t>(values[2], source, line.number, "property count");
  if (!count.has_value()) return Result<bool>::failure(count.error());
  if (values.size() != 3U + count.value() * 2U) {
    return Result<bool>::failure(parse_error(
        source, line.number,
        "MOL V2000 M  " + std::string{keyword} +
            " pair count does not match the line"));
  }
  for (std::size_t index = 0; index < count.value(); ++index) {
    const auto atom_index = number<std::size_t>(
        values[3U + index * 2U], source, line.number, "property atom index");
    const auto value = number<std::int64_t>(
        values[4U + index * 2U], source, line.number, "property value");
    if (!atom_index.has_value()) return Result<bool>::failure(atom_index.error());
    if (!value.has_value()) return Result<bool>::failure(value.error());
    if (atom_index.value() == 0U || atom_index.value() > atoms.size()) {
      return Result<bool>::failure(parse_error(
          source, line.number,
          "MOL V2000 property references an invalid atom index"));
    }
    auto& atom = atoms[atom_index.value() - 1U];
    if (keyword == "CHG") {
      if (value.value() < std::numeric_limits<std::int32_t>::min() ||
          value.value() > std::numeric_limits<std::int32_t>::max()) {
        return Result<bool>::failure(parse_error(
            source, line.number, "MOL V2000 formal charge is out of range"));
      }
      atom.formal_charge = static_cast<std::int32_t>(value.value());
      atom.formal_charge_present = true;
    } else if (keyword == "ISO") {
      if (value.value() <= 0 ||
          value.value() > std::numeric_limits<std::uint16_t>::max()) {
        return Result<bool>::failure(parse_error(
            source, line.number,
            "MOL V2000 isotope mass number is out of range"));
      }
      atom.isotope_mass = static_cast<std::uint16_t>(value.value());
    } else if (keyword == "RAD") {
      if (value.value() < 1 || value.value() > 3) {
        return Result<bool>::failure(parse_error(
            source, line.number,
            "MOL V2000 radical code must be singlet, doublet or triplet"));
      }
      atom.radical = static_cast<model::RadicalState>(value.value());
    }
  }
  return Result<bool>::success(true);
}

std::string fallback_name(std::string_view source_name,
                          std::size_t record_index) {
  auto stem = std::filesystem::path{source_name}.stem().string();
  if (stem.empty() || stem == "<memory>") stem = "molecule";
  return record_index == 0U ? stem
                            : stem + "_" + std::to_string(record_index + 1U);
}

Result<StructureData> parse_record(std::span<const SourceLine> record,
                                   std::string_view source,
                                   StructureFormat format,
                                   std::size_t record_index) {
  if (record.size() < 4U) {
    return Result<StructureData>::failure(parse_error(
        source, record.empty() ? 1U : record.front().number,
        "MOL V2000 record requires three header lines and a counts line"));
  }
  const auto& counts = record[3U];
  if (counts.text.find("V3000") != std::string::npos) {
    return Result<StructureData>::failure(operation::Error{
        operation::ErrorCode::unsupported,
        std::string{source} + ":" + std::to_string(counts.number) +
            ": MOL V3000 is not implemented yet",
        "provide MOL V2000 or use a future V3000 reader"});
  }
  if (counts.text.find("V2000") == std::string::npos ||
      counts.text.size() < 6U) {
    return Result<StructureData>::failure(parse_error(
        source, counts.number, "missing MOL V2000 counts/version line"));
  }
  const auto atom_count = number<std::size_t>(
      field(counts.text, 0U, 3U), source, counts.number, "atom count");
  const auto bond_count = number<std::size_t>(
      field(counts.text, 3U, 3U), source, counts.number, "bond count");
  if (!atom_count.has_value()) {
    return Result<StructureData>::failure(atom_count.error());
  }
  if (!bond_count.has_value()) {
    return Result<StructureData>::failure(bond_count.error());
  }
  if (atom_count.value() == 0U || atom_count.value() > 999U ||
      bond_count.value() > 999U ||
      record.size() < 4U + atom_count.value() + bond_count.value() + 1U) {
    return Result<StructureData>::failure(parse_error(
        source, counts.number,
        "MOL V2000 counts are empty, exceed 999, or exceed available lines"));
  }
  std::vector<ParsedAtom> atoms;
  atoms.reserve(atom_count.value());
  for (std::size_t index = 0; index < atom_count.value(); ++index) {
    const auto atom = parse_atom(record[4U + index], source);
    if (!atom.has_value()) return Result<StructureData>::failure(atom.error());
    atoms.push_back(atom.value());
  }
  std::vector<ParsedBond> bonds;
  bonds.reserve(bond_count.value());
  for (std::size_t index = 0; index < bond_count.value(); ++index) {
    const auto bond = parse_bond(record[4U + atom_count.value() + index], source,
                                 atom_count.value());
    if (!bond.has_value()) return Result<StructureData>::failure(bond.error());
    bonds.push_back(bond.value());
  }
  auto position = 4U + atom_count.value() + bond_count.value();
  std::vector<std::string> unparsed_properties;
  bool found_end = false;
  for (; position < record.size(); ++position) {
    const auto cleaned = trim(record[position].text);
    if (cleaned == "M  END") {
      found_end = true;
      ++position;
      break;
    }
    bool recognized = false;
    for (const auto keyword : {"CHG", "ISO", "RAD"}) {
      const auto applied =
          apply_pairs(record[position], source, keyword, atoms);
      if (!applied.has_value()) {
        return Result<StructureData>::failure(applied.error());
      }
      recognized = recognized || applied.value();
    }
    if (!recognized) unparsed_properties.push_back(record[position].text);
  }
  if (!found_end) {
    return Result<StructureData>::failure(parse_error(
        source, record.back().number, "MOL V2000 record is missing M  END"));
  }

  std::map<std::string, std::string, std::less<>> data_fields;
  while (position < record.size()) {
    if (trim(record[position].text).empty()) {
      ++position;
      continue;
    }
    const auto& header = record[position];
    const auto open = header.text.find('<');
    const auto close = open == std::string::npos
                           ? std::string::npos
                           : header.text.find('>', open + 1U);
    if (header.text.empty() || header.text.front() != '>' ||
        open == std::string::npos || close == std::string::npos ||
        close == open + 1U) {
      return Result<StructureData>::failure(parse_error(
          source, header.number, "malformed SDF data header after M  END"));
    }
    const auto name = header.text.substr(open + 1U, close - open - 1U);
    if (data_fields.contains(name)) {
      return Result<StructureData>::failure(parse_error(
          source, header.number, "duplicate SDF data field: " + name));
    }
    ++position;
    std::string value;
    while (position < record.size() &&
           !trim(record[position].text).empty()) {
      if (!value.empty()) value.push_back('\n');
      value += record[position].text;
      ++position;
    }
    data_fields.emplace(name, std::move(value));
  }
  if (format == StructureFormat::mol && !data_fields.empty()) {
    return Result<StructureData>::failure(parse_error(
        source, record.front().number,
        "MOL input contains SDF data fields; select SDF format"));
  }

  model::TopologyBuilder builder;
  const auto residue = builder.add_residue({"MOL", 1, "", "", ""});
  if (!residue.has_value()) {
    return Result<StructureData>::failure(residue.error());
  }
  std::vector<std::int64_t> mass_difference;
  std::vector<std::int64_t> stereo_parity;
  std::vector<std::int64_t> isotope_mass;
  std::vector<std::int64_t> radical;
  std::vector<model::Vec3d> coordinates;
  for (std::size_t index = 0; index < atoms.size(); ++index) {
    const auto& atom = atoms[index];
    const auto added = builder.add_atom(
        {atom.symbol + std::to_string(index + 1U), atom.atomic_number,
         residue.value(), "", atom.formal_charge,
         static_cast<std::int64_t>(index + 1U), atom.isotope_mass,
         atom.stereo_parity, atom.radical, atom.formal_charge_present,
         model::ChemicalAnnotationOrigin::explicit_input});
    if (!added.has_value()) return Result<StructureData>::failure(added.error());
    mass_difference.push_back(atom.mass_difference);
    stereo_parity.push_back(static_cast<std::int64_t>(atom.stereo_parity));
    isotope_mass.push_back(atom.isotope_mass.value_or(0U));
    radical.push_back(static_cast<std::int64_t>(atom.radical));
    coordinates.push_back(atom.position);
  }
  for (const auto& bond : bonds) {
    if (const auto error = builder.add_bond(
            {{bond.first}, {bond.second}, bond.order, bond.query, bond.stereo,
             model::ChemicalAnnotationOrigin::explicit_input});
        error.has_value()) {
      return Result<StructureData>::failure(*error);
    }
  }
  for (auto property :
       {std::tuple<std::string, model::AtomPropertyColumn,
                   model::PropertyMetadata>{
            "sdf.mass_difference", std::move(mass_difference),
            {std::nullopt, "MOL V2000 atom block", {}}},
        {"sdf.atom_stereo_parity", std::move(stereo_parity),
         {std::nullopt, "MOL V2000 atom block", {}}},
        {"sdf.isotope_mass", std::move(isotope_mass),
         {"mass_number", "MOL V2000 M  ISO", {}}},
        {"sdf.radical", std::move(radical),
         {std::nullopt, "MOL V2000 charge code/M  RAD", {}}}}) {
    if (const auto error = builder.add_property(
            std::move(std::get<0>(property)), std::move(std::get<1>(property)),
            std::move(std::get<2>(property)));
        error.has_value()) {
      return Result<StructureData>::failure(*error);
    }
  }
  builder.set_source_metadata("format",
                              std::string{to_string(format)});
  builder.set_source_metadata("format_version", "V2000");
  builder.set_source_metadata("molfile.name", record[0U].text);
  builder.set_source_metadata("molfile.program", record[1U].text);
  builder.set_source_metadata("molfile.comment", record[2U].text);
  if (!unparsed_properties.empty()) {
    std::string raw;
    for (const auto& line : unparsed_properties) {
      if (!raw.empty()) raw.push_back('\n');
      raw += line;
    }
    builder.set_source_metadata("molfile.unparsed_property_lines",
                                std::move(raw));
  }
  for (const auto& [name, value] : data_fields) {
    builder.set_source_metadata("sdf.data." + name, value);
  }
  const auto topology = builder.build();
  if (!topology.has_value()) {
    return Result<StructureData>::failure(topology.error());
  }
  model::FrameMetadata metadata;
  metadata.source_step = 0U;
  metadata.coordinate_unit = operation::LengthUnit::angstrom;
  const auto frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::move(coordinates)}, std::nullopt, {},
      std::move(metadata));
  if (!frame.has_value()) return Result<StructureData>::failure(frame.error());
  const auto source_coordinates = model::InMemoryCoordinateSource::create(
      topology.value()->atom_count(), {frame.value()});
  if (!source_coordinates.has_value()) {
    return Result<StructureData>::failure(source_coordinates.error());
  }
  StructureData structure;
  structure.name = trim(record[0U].text);
  if (structure.name.empty()) {
    structure.name = fallback_name(source, record_index);
  }
  structure.topology = topology.value();
  structure.coordinates = source_coordinates.value();
  structure.metadata.emplace("format", std::string{to_string(format)});
  structure.metadata.emplace("format_version", "V2000");
  for (const auto& [name, value] : data_fields) {
    structure.metadata.emplace("sdf.data." + name, value);
  }
  return Result<StructureData>::success(std::move(structure));
}

}  // namespace

Result<StructureDocument> read_molfile(std::string_view content,
                                       std::string source_name,
                                       StructureFormat format) {
  if (format != StructureFormat::mol && format != StructureFormat::sdf) {
    return Result<StructureDocument>::failure(operation::Error{
        operation::ErrorCode::internal,
        "MOL/SDF reader received a different structure format", {}});
  }
  const auto source_lines = lines(content);
  if (source_lines.empty()) {
    return Result<StructureDocument>::failure(parse_error(
        source_name, 1U, "MOL/SDF input is empty"));
  }
  std::vector<std::span<const SourceLine>> records;
  std::size_t start{};
  for (std::size_t index = 0; index < source_lines.size(); ++index) {
    if (trim(source_lines[index].text) != "$$$$") continue;
    if (format == StructureFormat::mol) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, source_lines[index].number,
          "MOL input contains an SDF record delimiter; select SDF format"));
    }
    if (index == start) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, source_lines[index].number, "empty SDF record"));
    }
    records.emplace_back(source_lines.data() + start, index - start);
    start = index + 1U;
  }
  if (start < source_lines.size()) {
    records.emplace_back(source_lines.data() + start,
                         source_lines.size() - start);
  }
  if (records.empty()) {
    records.emplace_back(source_lines.data(), source_lines.size());
  }
  if (format == StructureFormat::mol && records.size() != 1U) {
    return Result<StructureDocument>::failure(parse_error(
        source_name, 1U, "MOL input must contain exactly one record"));
  }
  StructureDocument document;
  document.format = format;
  document.source_name = source_name;
  document.structures.reserve(records.size());
  for (std::size_t index = 0; index < records.size(); ++index) {
    const auto parsed =
        parse_record(records[index], source_name, format, index);
    if (!parsed.has_value()) {
      return Result<StructureDocument>::failure(parsed.error());
    }
    document.structures.push_back(parsed.value());
  }
  return Result<StructureDocument>::success(std::move(document));
}

}  // namespace molshredder::io::detail
