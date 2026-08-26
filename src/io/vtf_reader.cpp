#include "structure_reader_internal.hpp"

#include <algorithm>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
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

struct VtfAtom {
  std::string name{"X"};
  std::string type{"X"};
  std::int64_t residue_id{};
  std::string residue_name{"X"};
  double radius{1.0};
  std::string segment_id;
  std::string chain_id;
  double charge{};
  std::uint8_t atomic_number{};
  std::string alternate_location;
  std::string insertion_code;
  double occupancy{1.0};
  double b_factor{1.0};
  double mass{1.0};
};

enum class CoordinateMode { ordered, indexed };

std::string lower(std::string_view value) {
  std::string result{value};
  std::transform(
      result.begin(), result.end(), result.begin(),
      [](unsigned char item) { return static_cast<char>(std::tolower(item)); });
  return result;
}

std::vector<std::string> tokens(std::string_view text) {
  std::istringstream stream{std::string{text}};
  stream.imbue(std::locale::classic());
  std::vector<std::string> result;
  std::string token;
  while (stream >> token)
    result.push_back(std::move(token));
  return result;
}

template <typename Value>
Result<Value> parse_number(std::string_view text, std::string_view source,
                           std::size_t line, std::string_view field) {
  Value value{};
  const auto parsed =
      molshredder::core::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != text.data() + text.size()) {
    return Result<Value>::failure(parse_error(
        source, line,
        "invalid VTF " + std::string{field} + ": " + std::string{text}));
  }
  if constexpr (std::is_floating_point_v<Value>) {
    if (!std::isfinite(value)) {
      return Result<Value>::failure(parse_error(
          source, line, "VTF " + std::string{field} + " must be finite"));
    }
  }
  return Result<Value>::success(value);
}

Result<std::vector<SourceLine>> logical_lines(std::string_view content,
                                              std::string_view source) {
  std::vector<SourceLine> result;
  std::string current;
  std::size_t current_line{};
  std::size_t position{};
  std::size_t line_number{1U};
  bool continuing = false;
  while (position < content.size()) {
    const auto end = content.find('\n', position);
    auto line = content.substr(position, end == std::string_view::npos
                                             ? content.size() - position
                                             : end - position);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1U);
    auto cleaned = trim(line);
    if (!continuing && (cleaned.empty() || cleaned.front() == '#')) {
      position = end == std::string_view::npos ? content.size() : end + 1U;
      ++line_number;
      continue;
    }
    if (!continuing) {
      current.clear();
      current_line = line_number;
    }
    bool has_continuation = !cleaned.empty() && cleaned.back() == '\\';
    if (has_continuation) {
      cleaned.pop_back();
      cleaned = trim(cleaned);
    }
    if (!current.empty() && !cleaned.empty())
      current.push_back(' ');
    current += cleaned;
    continuing = has_continuation;
    if (!continuing && !current.empty()) {
      result.push_back({current, current_line});
    }
    position = end == std::string_view::npos ? content.size() : end + 1U;
    ++line_number;
  }
  if (continuing) {
    return Result<std::vector<SourceLine>>::failure(parse_error(
        source, current_line, "VTF continuation line reaches end of input"));
  }
  return Result<std::vector<SourceLine>>::success(std::move(result));
}

bool is_keyword(std::string_view value, std::string_view short_form,
                std::string_view long_form) {
  return value == short_form || value == long_form;
}

Result<std::vector<std::size_t>> parse_atom_ids(std::string_view expression,
                                                std::string_view source,
                                                std::size_t line) {
  std::string compact;
  compact.reserve(expression.size());
  for (const auto character : expression) {
    if (std::isspace(static_cast<unsigned char>(character)) == 0) {
      compact.push_back(character);
    }
  }
  std::vector<std::size_t> result;
  std::size_t position{};
  while (position < compact.size()) {
    const auto comma = compact.find(',', position);
    const auto part = std::string_view{compact}.substr(
        position, comma == std::string::npos ? compact.size() - position
                                             : comma - position);
    if (part.empty()) {
      return Result<std::vector<std::size_t>>::failure(
          parse_error(source, line, "empty VTF atom-id specifier"));
    }
    const auto colon = part.find(':');
    const auto first_text = part.substr(0U, colon);
    const auto first =
        parse_number<std::uint64_t>(first_text, source, line, "atom id");
    if (!first.has_value()) {
      return Result<std::vector<std::size_t>>::failure(first.error());
    }
    auto last = first.value();
    if (colon != std::string_view::npos) {
      if (part.find(':', colon + 1U) != std::string_view::npos) {
        return Result<std::vector<std::size_t>>::failure(parse_error(
            source, line,
            "atom ranges use one colon; double colon is only for bonds"));
      }
      const auto parsed_last = parse_number<std::uint64_t>(
          part.substr(colon + 1U), source, line, "atom range endpoint");
      if (!parsed_last.has_value()) {
        return Result<std::vector<std::size_t>>::failure(parsed_last.error());
      }
      last = parsed_last.value();
    }
    if (first.value() > static_cast<std::uint64_t>(
                            std::numeric_limits<std::size_t>::max()) ||
        last > static_cast<std::uint64_t>(
                   std::numeric_limits<std::size_t>::max())) {
      return Result<std::vector<std::size_t>>::failure(
          parse_error(source, line, "VTF atom id exceeds host index range"));
    }
    const auto begin = static_cast<std::size_t>(first.value());
    const auto finish = static_cast<std::size_t>(last);
    const auto span = begin > finish ? begin - finish : finish - begin;
    if (span > 100000000U) {
      return Result<std::vector<std::size_t>>::failure(parse_error(
          source, line, "VTF atom range is too large to materialize"));
    }
    if (begin <= finish) {
      for (auto id = begin;; ++id) {
        result.push_back(id);
        if (id == finish)
          break;
      }
    } else {
      for (auto id = begin;; --id) {
        result.push_back(id);
        if (id == finish)
          break;
      }
    }
    if (comma == std::string::npos)
      break;
    position = comma + 1U;
  }
  return Result<std::vector<std::size_t>>::success(std::move(result));
}

Result<std::pair<std::vector<std::size_t>, std::string_view>>
atom_targets(std::string_view text, std::string_view source, std::size_t line,
             bool &use_default) {
  auto body = std::string_view{text};
  const auto first_end = body.find_first_of(" \t");
  const auto first = lower(body.substr(0U, first_end));
  if (first == "a" || first == "atom") {
    body = first_end == std::string_view::npos ? std::string_view{}
                                               : body.substr(first_end + 1U);
  }
  while (!body.empty() &&
         std::isspace(static_cast<unsigned char>(body.front())) != 0) {
    body.remove_prefix(1U);
  }
  if (body.starts_with("default") &&
      (body.size() == 7U ||
       std::isspace(static_cast<unsigned char>(body[7])) != 0)) {
    use_default = true;
    body.remove_prefix(7U);
    return Result<
        std::pair<std::vector<std::size_t>, std::string_view>>::success({{},
                                                                         body});
  }
  std::size_t end{};
  while (end < body.size()) {
    const auto character = static_cast<unsigned char>(body[end]);
    if (std::isdigit(character) != 0 || std::isspace(character) != 0 ||
        character == ',' || character == ':' || character == '+' ||
        character == '-') {
      ++end;
      continue;
    }
    break;
  }
  const auto id_expression = std::string_view{body}.substr(0U, end);
  const auto ids = parse_atom_ids(id_expression, source, line);
  if (!ids.has_value()) {
    return Result<std::pair<std::vector<std::size_t>,
                            std::string_view>>::failure(ids.error());
  }
  return Result<std::pair<std::vector<std::size_t>, std::string_view>>::success(
      {ids.value(), body.substr(end)});
}

Result<std::optional<model::UnitCell>> parse_cell(const SourceLine &line,
                                                  std::string_view source) {
  const auto values = tokens(line.text);
  if (values.size() != 4U && values.size() != 7U) {
    return Result<std::optional<model::UnitCell>>::failure(parse_error(
        source, line.number,
        "VTF unitcell requires a, b, c and optional alpha, beta, gamma"));
  }
  std::vector<double> parsed;
  for (std::size_t index = 1U; index < values.size(); ++index) {
    const auto value = parse_number<double>(values[index], source, line.number,
                                            "unitcell value");
    if (!value.has_value()) {
      return Result<std::optional<model::UnitCell>>::failure(value.error());
    }
    parsed.push_back(value.value());
  }
  if (parsed[0] == 0.0 && parsed[1] == 0.0 && parsed[2] == 0.0) {
    return Result<std::optional<model::UnitCell>>::success(std::nullopt);
  }
  const auto cell = make_unit_cell(
      parsed[0], parsed[1], parsed[2], parsed.size() == 6U ? parsed[3] : 90.0,
      parsed.size() == 6U ? parsed[4] : 90.0,
      parsed.size() == 6U ? parsed[5] : 90.0, source, line.number);
  if (!cell.has_value()) {
    return Result<std::optional<model::UnitCell>>::failure(cell.error());
  }
  return Result<std::optional<model::UnitCell>>::success(cell.value());
}

std::string structure_name(std::string_view source_name) {
  if (source_name == "<memory>")
    return "vtf_structure";
  auto result = std::filesystem::path{source_name}.stem().string();
  return result.empty() ? "vtf_structure" : result;
}

} // namespace

Result<StructureDocument> read_vtf(std::string_view content,
                                   std::string source_name) {
  const auto parsed_lines = logical_lines(content, source_name);
  if (!parsed_lines.has_value()) {
    return Result<StructureDocument>::failure(parsed_lines.error());
  }

  VtfAtom default_atom;
  std::vector<VtfAtom> atoms;
  std::vector<std::pair<std::size_t, std::size_t>> bonds;
  std::optional<model::UnitCell> inherited_cell;
  std::vector<model::Vec3d> inherited_positions;
  std::vector<std::shared_ptr<const model::CoordinateFrame>> frames;
  bool in_coordinates = false;
  CoordinateMode mode = CoordinateMode::ordered;
  std::size_t ordered_index{};
  std::optional<std::size_t> active_frame_line;
  std::set<std::size_t> indexed_seen;

  auto finish_frame = [&]() -> std::optional<operation::Error> {
    if (!active_frame_line.has_value())
      return std::nullopt;
    if (frames.empty() &&
        (mode != CoordinateMode::ordered || ordered_index != atoms.size())) {
      return parse_error(
          source_name, *active_frame_line,
          "the first VTF timestep must define every atom in ordered form",
          "write one ordered x y z row per atom before sparse indexed "
          "timesteps");
    }
    model::FrameMetadata metadata;
    metadata.source_step = static_cast<std::uint64_t>(frames.size());
    metadata.coordinate_unit = operation::LengthUnit::angstrom;
    metadata.unit_cell = inherited_cell;
    metadata.fields.emplace("vtf.coordinate_mode",
                            mode == CoordinateMode::ordered ? "ordered"
                                                            : "indexed");
    const auto frame = model::CoordinateFrame::create(
        model::CoordinateBuffer{inherited_positions}, std::nullopt, {},
        std::move(metadata));
    if (!frame.has_value())
      return frame.error();
    frames.push_back(frame.value());
    active_frame_line.reset();
    return std::nullopt;
  };

  for (const auto &line : parsed_lines.value()) {
    const auto fields = tokens(line.text);
    if (fields.empty())
      continue;
    const auto keyword = lower(fields.front());
    const bool timestep = is_keyword(keyword, "t", "timestep") ||
                          is_keyword(keyword, "c", "coordinates") ||
                          is_keyword(keyword, "i", "indexed") ||
                          is_keyword(keyword, "o", "ordered");
    if (timestep) {
      if (const auto error = finish_frame(); error.has_value()) {
        return Result<StructureDocument>::failure(*error);
      }
      if (atoms.empty()) {
        return Result<StructureDocument>::failure(
            parse_error(source_name, line.number,
                        "VTF coordinate block appears before any atoms"));
      }
      mode = CoordinateMode::ordered;
      if (is_keyword(keyword, "i", "indexed") ||
          (fields.size() >= 2U &&
           is_keyword(lower(fields[1]), "i", "indexed"))) {
        mode = CoordinateMode::indexed;
      } else if (fields.size() >= 2U &&
                 !is_keyword(lower(fields[1]), "o", "ordered")) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, line.number, "unknown VTF timestep coordinate mode"));
      }
      if (fields.size() > 2U) {
        return Result<StructureDocument>::failure(
            parse_error(source_name, line.number,
                        "VTF timestep header contains unexpected fields"));
      }
      if (frames.empty())
        inherited_positions.assign(atoms.size(), {});
      ordered_index = 0U;
      indexed_seen.clear();
      active_frame_line = line.number;
      in_coordinates = true;
      continue;
    }

    const bool unitcell =
        is_keyword(keyword, "u", "unitcell") || is_keyword(keyword, "p", "pbc");
    if (unitcell) {
      const auto cell = parse_cell(line, source_name);
      if (!cell.has_value()) {
        return Result<StructureDocument>::failure(cell.error());
      }
      inherited_cell = cell.value();
      continue;
    }

    if (in_coordinates) {
      if (!active_frame_line.has_value()) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, line.number, "VTF coordinate row has no timestep"));
      }
      if (mode == CoordinateMode::ordered) {
        if (fields.size() < 3U || ordered_index >= atoms.size()) {
          return Result<StructureDocument>::failure(parse_error(
              source_name, line.number,
              "ordered VTF timestep has an invalid or extra coordinate row"));
        }
        std::vector<double> xyz;
        for (std::size_t index = 0U; index < 3U; ++index) {
          const auto value = parse_number<double>(fields[index], source_name,
                                                  line.number, "coordinate");
          if (!value.has_value()) {
            return Result<StructureDocument>::failure(value.error());
          }
          xyz.push_back(value.value());
        }
        inherited_positions[ordered_index++] = {xyz[0], xyz[1], xyz[2]};
      } else {
        if (fields.size() < 4U) {
          return Result<StructureDocument>::failure(parse_error(
              source_name, line.number,
              "indexed VTF coordinate row requires atom id and x y z"));
        }
        const auto atom_id = parse_number<std::uint64_t>(
            fields[0], source_name, line.number, "coordinate atom id");
        if (!atom_id.has_value()) {
          return Result<StructureDocument>::failure(atom_id.error());
        }
        if (atom_id.value() >= atoms.size()) {
          return Result<StructureDocument>::failure(
              parse_error(source_name, line.number,
                          "indexed VTF coordinate references an unknown atom"));
        }
        const auto dense_id = static_cast<std::size_t>(atom_id.value());
        if (!indexed_seen.insert(dense_id).second) {
          return Result<StructureDocument>::failure(parse_error(
              source_name, line.number,
              "indexed VTF timestep defines one atom more than once"));
        }
        std::vector<double> xyz;
        for (std::size_t index = 1U; index < 4U; ++index) {
          const auto value = parse_number<double>(fields[index], source_name,
                                                  line.number, "coordinate");
          if (!value.has_value()) {
            return Result<StructureDocument>::failure(value.error());
          }
          xyz.push_back(value.value());
        }
        inherited_positions[dense_id] = {xyz[0], xyz[1], xyz[2]};
      }
      continue;
    }

    if (is_keyword(keyword, "b", "bond")) {
      const auto first_space = line.text.find_first_of(" \t");
      if (first_space == std::string::npos) {
        return Result<StructureDocument>::failure(
            parse_error(source_name, line.number, "VTF bond line is empty"));
      }
      auto expression = std::string_view{line.text}.substr(first_space + 1U);
      std::string compact;
      for (const auto character : expression) {
        if (std::isspace(static_cast<unsigned char>(character)) == 0) {
          compact.push_back(character);
        }
      }
      std::size_t position{};
      while (position < compact.size()) {
        const auto comma = compact.find(',', position);
        const auto part = std::string_view{compact}.substr(
            position, comma == std::string::npos ? compact.size() - position
                                                 : comma - position);
        const auto double_colon = part.find("::");
        const auto colon = part.find(':');
        if (colon == std::string_view::npos) {
          return Result<StructureDocument>::failure(parse_error(
              source_name, line.number, "invalid VTF bond specifier"));
        }
        const auto endpoint_offset = double_colon == std::string_view::npos
                                         ? colon + 1U
                                         : double_colon + 2U;
        const auto from = parse_number<std::uint64_t>(
            part.substr(0U, colon), source_name, line.number, "bond atom id");
        const auto to = parse_number<std::uint64_t>(
            part.substr(endpoint_offset), source_name, line.number,
            "bond atom id");
        if (!from.has_value()) {
          return Result<StructureDocument>::failure(from.error());
        }
        if (!to.has_value()) {
          return Result<StructureDocument>::failure(to.error());
        }
        if (from.value() >= atoms.size() || to.value() >= atoms.size()) {
          return Result<StructureDocument>::failure(
              parse_error(source_name, line.number,
                          "VTF bond references an atom not yet defined"));
        }
        auto first = static_cast<std::size_t>(from.value());
        const auto last = static_cast<std::size_t>(to.value());
        if (double_colon == std::string_view::npos) {
          bonds.emplace_back(first, last);
        } else {
          while (first != last) {
            const auto next = first < last ? first + 1U : first - 1U;
            bonds.emplace_back(first, next);
            first = next;
          }
        }
        if (comma == std::string::npos)
          break;
        position = comma + 1U;
      }
      continue;
    }

    bool use_default = false;
    const auto targets =
        atom_targets(line.text, source_name, line.number, use_default);
    if (!targets.has_value()) {
      return Result<StructureDocument>::failure(targets.error());
    }
    auto option_tokens = tokens(targets.value().second);
    if (option_tokens.size() % 2U != 0U) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, line.number,
          "VTF atom options require a value after every option name"));
    }
    const auto ids = targets.value().first;
    if (!use_default) {
      if (ids.empty()) {
        return Result<StructureDocument>::failure(parse_error(
            source_name, line.number, "VTF atom line has no atom ids"));
      }
      const auto maximum = *std::max_element(ids.begin(), ids.end());
      if (maximum > 100000000U) {
        return Result<StructureDocument>::failure(
            parse_error(source_name, line.number,
                        "VTF atom id is too large to materialize"));
      }
      if (maximum >= atoms.size())
        atoms.resize(maximum + 1U, default_atom);
    }
    auto apply = [&](auto &&change) {
      if (use_default) {
        change(default_atom);
      } else {
        for (const auto id : ids)
          change(atoms[id]);
      }
    };
    for (std::size_t index = 0U; index < option_tokens.size(); index += 2U) {
      const auto option = lower(option_tokens[index]);
      const auto &value = option_tokens[index + 1U];
      if (is_keyword(option, "n", "name")) {
        apply([&](VtfAtom &atom) { atom.name = value; });
      } else if (is_keyword(option, "t", "type")) {
        apply([&](VtfAtom &atom) { atom.type = value; });
      } else if (option == "resid") {
        const auto parsed = parse_number<std::int64_t>(
            value, source_name, line.number, "residue id");
        if (!parsed.has_value())
          return Result<StructureDocument>::failure(parsed.error());
        apply([&](VtfAtom &atom) { atom.residue_id = parsed.value(); });
      } else if (option == "res" || option == "resn" || option == "resna" ||
                 option == "resnam" || option == "resname") {
        apply([&](VtfAtom &atom) { atom.residue_name = value; });
      } else if (is_keyword(option, "r", "radius")) {
        const auto parsed =
            parse_number<double>(value, source_name, line.number, "radius");
        if (!parsed.has_value() || parsed.value() < 0.0) {
          return Result<StructureDocument>::failure(
              parsed.has_value()
                  ? parse_error(source_name, line.number,
                                "VTF radius must be non-negative")
                  : parsed.error());
        }
        apply([&](VtfAtom &atom) { atom.radius = parsed.value(); });
      } else if (is_keyword(option, "s", "segid")) {
        apply([&](VtfAtom &atom) { atom.segment_id = value; });
      } else if (is_keyword(option, "c", "chain")) {
        apply([&](VtfAtom &atom) { atom.chain_id = value; });
      } else if (option == "charge" || option == "q") {
        const auto parsed =
            parse_number<double>(value, source_name, line.number, "charge");
        if (!parsed.has_value())
          return Result<StructureDocument>::failure(parsed.error());
        apply([&](VtfAtom &atom) { atom.charge = parsed.value(); });
      } else if (is_keyword(option, "a", "atomicnumber")) {
        const auto parsed = parse_number<std::uint64_t>(
            value, source_name, line.number, "atomic number");
        if (!parsed.has_value() || parsed.value() > 118U) {
          return Result<StructureDocument>::failure(
              parsed.has_value()
                  ? parse_error(source_name, line.number,
                                "VTF atomic number must be 0-118")
                  : parsed.error());
        }
        apply([&](VtfAtom &atom) {
          atom.atomic_number = static_cast<std::uint8_t>(parsed.value());
        });
      } else if (option == "altloc") {
        apply([&](VtfAtom &atom) { atom.alternate_location = value; });
      } else if (is_keyword(option, "i", "insertion")) {
        apply([&](VtfAtom &atom) { atom.insertion_code = value; });
      } else if (is_keyword(option, "o", "occupancy")) {
        const auto parsed =
            parse_number<double>(value, source_name, line.number, "occupancy");
        if (!parsed.has_value())
          return Result<StructureDocument>::failure(parsed.error());
        apply([&](VtfAtom &atom) { atom.occupancy = parsed.value(); });
      } else if (is_keyword(option, "b", "bfactor")) {
        const auto parsed =
            parse_number<double>(value, source_name, line.number, "B-factor");
        if (!parsed.has_value())
          return Result<StructureDocument>::failure(parsed.error());
        apply([&](VtfAtom &atom) { atom.b_factor = parsed.value(); });
      } else if (is_keyword(option, "m", "mass")) {
        const auto parsed =
            parse_number<double>(value, source_name, line.number, "mass");
        if (!parsed.has_value() || parsed.value() < 0.0) {
          return Result<StructureDocument>::failure(
              parsed.has_value() ? parse_error(source_name, line.number,
                                               "VTF mass must be non-negative")
                                 : parsed.error());
        }
        apply([&](VtfAtom &atom) { atom.mass = parsed.value(); });
      } else {
        return Result<StructureDocument>::failure(parse_error(
            source_name, line.number, "unknown VTF atom option: " + option));
      }
    }
  }
  if (const auto error = finish_frame(); error.has_value()) {
    return Result<StructureDocument>::failure(*error);
  }
  if (atoms.empty()) {
    return Result<StructureDocument>::failure(
        parse_error(source_name, 1U, "VTF input contains no atoms"));
  }

  model::TopologyBuilder builder;
  using ResidueKey = std::tuple<std::string, std::string, std::int64_t,
                                std::string, std::string>;
  std::map<ResidueKey, model::ResidueIndex> residue_indices;
  std::vector<std::string> atom_types;
  std::vector<double> radii;
  std::vector<double> charges;
  std::vector<double> occupancies;
  std::vector<double> b_factors;
  std::vector<double> masses;
  for (std::size_t id = 0U; id < atoms.size(); ++id) {
    const auto &atom = atoms[id];
    const ResidueKey key{atom.segment_id, atom.chain_id, atom.residue_id,
                         atom.insertion_code, atom.residue_name};
    auto found = residue_indices.find(key);
    if (found == residue_indices.end()) {
      const auto residue = builder.add_residue(
          {atom.residue_name, atom.residue_id, atom.insertion_code,
           atom.chain_id, atom.segment_id});
      if (!residue.has_value()) {
        return Result<StructureDocument>::failure(residue.error());
      }
      found = residue_indices.emplace(key, residue.value()).first;
    }
    const auto added = builder.add_atom({atom.name, atom.atomic_number,
                                         found->second, atom.alternate_location,
                                         0, static_cast<std::int64_t>(id)});
    if (!added.has_value()) {
      return Result<StructureDocument>::failure(added.error());
    }
    atom_types.push_back(atom.type);
    radii.push_back(atom.radius);
    charges.push_back(atom.charge);
    occupancies.push_back(atom.occupancy);
    b_factors.push_back(atom.b_factor);
    masses.push_back(atom.mass);
  }
  for (const auto& [first, second] : bonds) {
    if (const auto error =
            builder.add_bond({{first}, {second}, model::BondOrder::unknown});
        error.has_value()) {
      return Result<StructureDocument>::failure(parse_error(
          source_name, 1U, "invalid VTF bond topology: " + error->message));
    }
  }
  const auto add_property =
      [&](std::string name, model::AtomPropertyColumn values,
          std::optional<std::string> unit =
              std::nullopt) -> std::optional<operation::Error> {
    return builder.add_property(std::move(name), std::move(values),
                                {std::move(unit), "VTF atom property", {}});
  };
  if (const auto error = add_property("vtf.atom_type", std::move(atom_types));
      error.has_value())
    return Result<StructureDocument>::failure(*error);
  if (const auto error =
          add_property("vdw_radius", std::move(radii), "angstrom");
      error.has_value())
    return Result<StructureDocument>::failure(*error);
  if (const auto error =
          add_property("partial_charge", std::move(charges), "e");
      error.has_value())
    return Result<StructureDocument>::failure(*error);
  if (const auto error = add_property("occupancy", std::move(occupancies));
      error.has_value())
    return Result<StructureDocument>::failure(*error);
  if (const auto error = add_property("b_factor", std::move(b_factors));
      error.has_value())
    return Result<StructureDocument>::failure(*error);
  if (const auto error = add_property("mass", std::move(masses), "dalton");
      error.has_value())
    return Result<StructureDocument>::failure(*error);
  builder.set_source_metadata("format", "vtf");
  builder.set_source_metadata("vtf.coordinate_inheritance", "previous_frame");
  const auto topology = builder.build();
  if (!topology.has_value()) {
    return Result<StructureDocument>::failure(topology.error());
  }
  const auto coordinates =
      model::InMemoryCoordinateSource::create(atoms.size(), std::move(frames));
  if (!coordinates.has_value()) {
    return Result<StructureDocument>::failure(coordinates.error());
  }
  StructureData structure;
  structure.name = structure_name(source_name);
  structure.topology = topology.value();
  structure.coordinates = coordinates.value();
  structure.metadata.emplace("format", "vtf");
  StructureDocument document;
  document.format = StructureFormat::vtf;
  document.source_name = std::move(source_name);
  document.structures.push_back(std::move(structure));
  return Result<StructureDocument>::success(std::move(document));
}

} // namespace molshredder::io::detail
