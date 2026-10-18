#include "structure_reader_internal.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/core/parse_number.hpp"

// PDBQT (AutoDock), CHARMM coordinate (COR/CRD) and ChemDraw 3D (CC1) readers.
// Implemented from the public format descriptions and checked against
// Open-Source PyMOL 3.1.0 (SEQ-319 S3).
namespace molshredder::io::detail {
namespace {

using operation::Result;

std::vector<std::string_view> split_lines(std::string_view content) {
  std::vector<std::string_view> lines;
  std::size_t position = 0;
  while (position <= content.size()) {
    const auto end = content.find('\n', position);
    auto line = content.substr(position, end == std::string_view::npos ? content.size() - position : end - position);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
    lines.push_back(line);
    if (end == std::string_view::npos) break;
    position = end + 1U;
  }
  if (!lines.empty() && lines.back().empty()) lines.pop_back();
  return lines;
}

std::vector<std::string_view> words(std::string_view line) {
  std::vector<std::string_view> out;
  std::size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) != 0) ++i;
    const auto start = i;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) == 0) ++i;
    if (i > start) out.push_back(line.substr(start, i - start));
  }
  return out;
}

template <typename T>
std::optional<T> to_number(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1U);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1U);
  T value{};
  const auto result = molshredder::core::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()) return std::nullopt;
  return value;
}

bool equal_ci(std::string_view a, std::string_view b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
         });
}

std::uint8_t infer_element(std::string_view atom_name, std::string_view residue_name) {
  while (!atom_name.empty() && std::isdigit(static_cast<unsigned char>(atom_name.front())) != 0) atom_name.remove_prefix(1U);
  if (atom_name.empty() || std::isalpha(static_cast<unsigned char>(atom_name.front())) == 0) return 0U;
  if (atom_name.size() >= 2U && std::isalpha(static_cast<unsigned char>(atom_name[1])) != 0) {
    const auto candidate = atom_name.substr(0U, 2U);
    const auto parsed = atomic_number(candidate);
    const bool conventional_case = std::islower(static_cast<unsigned char>(atom_name[1])) != 0;
    const bool elemental_residue = atom_name.size() == 2U && equal_ci(atom_name, residue_name);
    const bool unambiguous = equal_ci(candidate, "CL") || equal_ci(candidate, "BR") || equal_ci(candidate, "FE");
    if (parsed.has_value() && (conventional_case || elemental_residue || unambiguous)) return parsed.value();
  }
  return atomic_number(atom_name.substr(0U, 1U)).value_or(0U);
}

std::string structure_name(std::string_view source_name, std::string_view fallback) {
  if (source_name == "<memory>") return std::string{fallback};
  auto name = std::filesystem::path{source_name}.stem().string();
  return name.empty() ? std::string{fallback} : name;
}

Result<StructureDocument> assemble(StructureFormat format, std::string_view label, std::string source_name,
                                   std::shared_ptr<const model::Topology> topology,
                                   std::vector<model::Vec3d> positions) {
  model::FrameMetadata metadata;
  metadata.source_step = 1U;
  metadata.coordinate_unit = operation::LengthUnit::angstrom;
  const auto frame = model::CoordinateFrame::create(model::CoordinateBuffer{std::move(positions)}, std::nullopt, {},
                                                    std::move(metadata));
  if (!frame.has_value()) return Result<StructureDocument>::failure(frame.error());
  const auto coordinates = model::InMemoryCoordinateSource::create(topology->atom_count(), {frame.value()});
  if (!coordinates.has_value()) return Result<StructureDocument>::failure(coordinates.error());
  StructureData structure;
  structure.name = structure_name(source_name, std::string{label} + "_structure");
  structure.topology = std::move(topology);
  structure.coordinates = coordinates.value();
  structure.metadata.emplace("format", std::string{label});
  StructureDocument document;
  document.format = format;
  document.source_name = std::move(source_name);
  document.structures.push_back(std::move(structure));
  return Result<StructureDocument>::success(std::move(document));
}

} // namespace

// =================================================================== PDBQT

Result<StructureDocument> read_pdbqt(std::string_view content, std::string source_name) {
  // Rewrite each atom record into plain PDB (element in columns 77-78, trailing PDBQT columns dropped),
  // reuse the PDB reader, then attach the AutoDock partial charges and atom types.
  std::string converted;
  std::vector<double> charges;
  std::vector<std::string> types;
  std::size_t model_atoms = 0;
  std::optional<std::size_t> first_model_atoms;
  for (const auto line : split_lines(content)) {
    const auto record = line.substr(0, std::min<std::size_t>(line.size(), 6U));
    const bool atom_record = record.starts_with("ATOM") || record.starts_with("HETATM");
    if (record.starts_with("ROOT") || record.starts_with("ENDROOT") || record.starts_with("BRANCH") ||
        record.starts_with("ENDBRA") || record.starts_with("TORSDO") || record.starts_with("BEGIN_") ||
        record.starts_with("END_RE"))
      continue;
    if (record.starts_with("ENDMDL")) {
      if (!first_model_atoms.has_value()) first_model_atoms = model_atoms;
      converted += std::string{line} + "\n";
      continue;
    }
    if (!atom_record) {
      converted += std::string{line} + "\n";
      continue;
    }
    if (line.size() < 66U)
      return Result<StructureDocument>::failure(parse_error(source_name, 1U, "PDBQT atom record is shorter than 66 columns"));
    std::string pdb{line.substr(0, 66U)};
    std::string element;
    double charge = 0.0;
    std::string type;
    if (line.size() > 70U) {
      const auto charge_text = line.substr(70U, std::min<std::size_t>(6U, line.size() - 70U));
      auto cleaned = std::string{charge_text};
      const auto first = cleaned.find_first_not_of(" \t");
      if (first != std::string::npos && cleaned[first] == '+') cleaned.erase(first, 1U);
      charge = to_number<double>(cleaned).value_or(0.0);
    }
    if (line.size() > 77U) {
      type = std::string{line.substr(77U, std::min<std::size_t>(2U, line.size() - 77U))};
      while (!type.empty() && std::isspace(static_cast<unsigned char>(type.back())) != 0) type.pop_back();
    }
    if (type == "A") element = "C";                                             // aromatic carbon
    else if (type.size() == 2U && std::isupper(static_cast<unsigned char>(type[1])) != 0)
      element = std::string{type[0]};                                           // OA, NA, HD, SA...
    else element = type;
    if (element.size() > 2U || (!element.empty() && !atomic_number(element).has_value())) element.clear();
    pdb.resize(76U, ' ');
    // columns 77-78: element right-justified
    pdb += element.size() == 1U ? " " + element : (element.empty() ? "  " : element);
    converted += pdb + "\n";
    if (!first_model_atoms.has_value()) {
      charges.push_back(charge);
      types.push_back(type);
    }
    ++model_atoms;
  }
  auto document = read_pdb(converted, source_name);
  if (!document.has_value()) return document;
  auto &structure = document.value().structures.front();
  if (structure.topology->atom_count() != charges.size())
    return Result<StructureDocument>::failure(
        parse_error(source_name, 1U, "PDBQT atom count changed between the records and the PDB reader"));
  auto builder = model::TopologyBuilder::from(*structure.topology);
  if (const auto error = builder.add_property("partial_charge", std::move(charges),
                                              model::PropertyMetadata{"elementary_charge", "PDBQT charge", {}});
      error.has_value())
    return Result<StructureDocument>::failure(*error);
  if (const auto error = builder.add_property("autodock_type", std::move(types),
                                              model::PropertyMetadata{std::nullopt, "PDBQT atom type", {}});
      error.has_value())
    return Result<StructureDocument>::failure(*error);
  builder.set_source_metadata("format", "pdbqt");
  const auto rebuilt = builder.build();
  if (!rebuilt.has_value()) return Result<StructureDocument>::failure(rebuilt.error());
  structure.topology = rebuilt.value();
  structure.metadata["format"] = "pdbqt";
  document.value().format = StructureFormat::pdbqt;
  return document;
}

// =================================================================== CHARMM COR / CRD

Result<StructureDocument> read_cor(std::string_view content, std::string source_name) {
  const auto lines = split_lines(content);
  std::size_t index = 0;
  std::string title;
  while (index < lines.size() && !lines[index].empty() && lines[index].front() == '*') {
    title += std::string{lines[index]} + "\n";
    ++index;
  }
  if (index == 0U || index >= lines.size())
    return Result<StructureDocument>::failure(
        parse_error(source_name, 1U, "CHARMM coordinate files start with title lines beginning with '*'"));
  const auto count_words = words(lines[index]);
  const auto declared = count_words.empty() ? std::nullopt : to_number<long>(count_words.front());
  if (!declared.has_value() || *declared <= 0)
    return Result<StructureDocument>::failure(
        parse_error(source_name, index + 1U, "the atom count line is missing or not positive"));
  constexpr long kMaximumAtoms = 50'000'000L;
  if (*declared > kMaximumAtoms)
    return Result<StructureDocument>::failure(parse_error(source_name, index + 1U, "the atom count is too large"));
  ++index;
  model::TopologyBuilder builder;
  std::vector<model::Vec3d> positions;
  std::optional<model::ResidueIndex> residue;
  std::string current_key;
  for (long atom = 0; atom < *declared; ++atom, ++index) {
    if (index >= lines.size())
      return Result<StructureDocument>::failure(
          parse_error(source_name, index + 1U, "the file ends before all declared atoms are read"));
    const auto w = words(lines[index]);
    if (w.size() < 7U)
      return Result<StructureDocument>::failure(
          parse_error(source_name, index + 1U, "an atom line needs serial, residue number, names and x y z"));
    const auto serial = to_number<long>(w[0]);
    const auto resno = to_number<long>(w[1]);
    const auto x = to_number<double>(w[4]);
    const auto y = to_number<double>(w[5]);
    const auto z = to_number<double>(w[6]);
    if (!serial.has_value() || !resno.has_value() || !x.has_value() || !y.has_value() || !z.has_value())
      return Result<StructureDocument>::failure(parse_error(source_name, index + 1U, "an atom line has a non-numeric field"));
    const std::string resname{w[2]}, name{w[3]};
    const std::string segment = w.size() > 7U ? std::string{w[7]} : std::string{};
    std::string label = w.size() > 8U ? std::string{w[8]} : std::to_string(*resno);
    // The residue id label may carry an insertion code (e.g. 12A).
    std::string digits, insertion;
    for (const auto c : label) (std::isdigit(static_cast<unsigned char>(c)) != 0 || (c == '-' && digits.empty()) ? digits : insertion).push_back(c);
    const auto number = to_number<long>(digits).value_or(*resno);
    const auto key = segment + "|" + label + "|" + resname + "|" + std::to_string(*resno);
    if (!residue.has_value() || key != current_key) {
      // PyMOL (via the molfile cor plugin) takes the chain from the first letter of the segment id.
      const auto added = builder.add_residue(
          {resname, number, insertion.substr(0, 1U), segment.substr(0, 1U), segment});
      if (!added.has_value()) return Result<StructureDocument>::failure(added.error());
      residue = added.value();
      current_key = key;
    }
    const auto added = builder.add_atom({name, infer_element(name, resname), *residue, "", 0, *serial});
    if (!added.has_value()) return Result<StructureDocument>::failure(added.error());
    positions.push_back({*x, *y, *z});
  }
  builder.set_source_metadata("format", "cor");
  builder.set_source_metadata("cor.title", title);
  const auto topology = builder.build();
  if (!topology.has_value()) return Result<StructureDocument>::failure(topology.error());
  return assemble(StructureFormat::cor, "cor", std::move(source_name), topology.value(), std::move(positions));
}

// =================================================================== ChemDraw 3D CC1

Result<StructureDocument> read_cc1(std::string_view content, std::string source_name) {
  const auto lines = split_lines(content);
  if (lines.empty() || lines[0].size() < 3U)
    return Result<StructureDocument>::failure(parse_error(source_name, 1U, "CC1 input is empty"));
  const auto declared = to_number<long>(lines[0].substr(0, 3U));
  if (!declared.has_value() || *declared <= 0 || static_cast<std::size_t>(*declared) + 1U > lines.size() + 0U)
    return Result<StructureDocument>::failure(
        parse_error(source_name, 1U, "the atom count (columns 1-3) is missing or exceeds the file"));
  const auto count = static_cast<std::size_t>(*declared);
  model::TopologyBuilder builder;
  const auto residue = builder.add_residue({"UNK", 0, "", "", ""});
  if (!residue.has_value()) return Result<StructureDocument>::failure(residue.error());
  std::vector<model::Vec3d> positions;
  std::map<std::string, std::size_t> ids;
  std::vector<std::vector<std::string>> neighbours;
  for (std::size_t atom = 0; atom < count; ++atom) {
    const auto line = lines[1U + atom];
    if (line.size() < 49U)
      return Result<StructureDocument>::failure(parse_error(source_name, atom + 2U, "an atom line is shorter than 49 columns"));
    std::string symbol{line.substr(0, 3U)};
    symbol.erase(std::remove_if(symbol.begin(), symbol.end(), [](unsigned char c) { return std::isspace(c) != 0; }), symbol.end());
    std::string id{line.substr(3, 5)};
    id.erase(std::remove_if(id.begin(), id.end(), [](unsigned char c) { return std::isspace(c) != 0; }), id.end());
    const auto x = to_number<double>(line.substr(8, 12));
    const auto y = to_number<double>(line.substr(20, 12));
    const auto z = to_number<double>(line.substr(32, 12));
    if (!x.has_value() || !y.has_value() || !z.has_value())
      return Result<StructureDocument>::failure(parse_error(source_name, atom + 2U, "an atom line has an invalid coordinate"));
    const auto element = atomic_number(symbol);
    const auto added = builder.add_atom({symbol.empty() ? std::string{"X"} : symbol, element.value_or(0U), residue.value(), "", 0});
    if (!added.has_value()) return Result<StructureDocument>::failure(added.error());
    ids[id] = atom;
    positions.push_back({*x, *y, *z});
    std::vector<std::string> bonded;
    for (const auto w : words(line.substr(49))) bonded.emplace_back(w);
    neighbours.push_back(std::move(bonded));
  }
  std::set<std::pair<std::size_t, std::size_t>> seen;
  for (std::size_t atom = 0; atom < count; ++atom)
    for (const auto &other : neighbours[atom]) {
      const auto found = ids.find(other);
      if (found == ids.end())
        return Result<StructureDocument>::failure(parse_error(source_name, atom + 2U, "a bond references an unknown atom id"));
      const auto lo = std::min(atom, found->second), hi = std::max(atom, found->second);
      if (lo == hi || !seen.insert({lo, hi}).second) continue;
      const auto error = builder.add_bond({model::AtomIndex{lo}, model::AtomIndex{hi}, model::BondOrder::unknown,
                                           model::BondQuery::none, model::BondStereo::none,
                                           model::ChemicalAnnotationOrigin::explicit_input});
      if (error.has_value()) return Result<StructureDocument>::failure(*error);
    }
  builder.set_source_metadata("format", "cc1");
  const auto topology = builder.build();
  if (!topology.has_value()) return Result<StructureDocument>::failure(topology.error());
  return assemble(StructureFormat::cc1, "cc1", std::move(source_name), topology.value(), std::move(positions));
}

} // namespace molshredder::io::detail
