#include "structure_reader_internal.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace molshredder::io::detail {
namespace {

struct SourceLine {
  std::string_view text;
  std::size_t number{};
};

class LineCursor {
 public:
  explicit LineCursor(std::string_view content) : content_{content} {}

  [[nodiscard]] std::optional<SourceLine> next() {
    if (position_ >= content_.size()) return std::nullopt;
    const auto start = position_;
    const auto end = content_.find('\n', start);
    position_ = end == std::string_view::npos ? content_.size() : end + 1U;
    auto text = content_.substr(
        start, end == std::string_view::npos ? content_.size() - start
                                             : end - start);
    if (!text.empty() && text.back() == '\r') text.remove_suffix(1U);
    return SourceLine{text, line_++};
  }

 private:
  std::string_view content_;
  std::size_t position_{};
  std::size_t line_{1U};
};

operation::Result<std::size_t> parse_atom_count(const SourceLine& line,
                                                std::string_view source) {
  const auto cleaned = trim(line.text);
  std::size_t count{};
  const auto parsed =
      std::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), count);
  if (cleaned.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != cleaned.data() + cleaned.size() || count == 0U) {
    return operation::Result<std::size_t>::failure(parse_error(
        source, line.number, "XYZ atom count must be a positive integer"));
  }
  return operation::Result<std::size_t>::success(count);
}

struct ParsedAtom {
  std::uint8_t atomic_number{};
  std::string symbol;
  model::Vec3d position;
};

operation::Result<ParsedAtom> parse_atom(const SourceLine& line,
                                         std::string_view source) {
  std::istringstream stream{std::string{line.text}};
  stream.imbue(std::locale::classic());
  std::string symbol;
  model::Vec3d position;
  if (!(stream >> symbol >> position.x >> position.y >> position.z)) {
    return operation::Result<ParsedAtom>::failure(parse_error(
        source, line.number,
        "XYZ atom record requires element and three Cartesian coordinates"));
  }
  std::string extra;
  if (stream >> extra) {
    return operation::Result<ParsedAtom>::failure(parse_error(
        source, line.number,
        "extended XYZ atom columns are not supported by the foundation reader",
        "provide plain four-column XYZ or select a future extended-XYZ mode"));
  }
  if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
      !std::isfinite(position.z)) {
    return operation::Result<ParsedAtom>::failure(parse_error(
        source, line.number, "XYZ coordinates must be finite"));
  }
  std::uint8_t number{};
  if (symbol != "X" && symbol != "x") {
    const auto parsed_number = atomic_number(symbol);
    if (!parsed_number.has_value()) {
      return operation::Result<ParsedAtom>::failure(parse_error(
          source, line.number, "unknown XYZ element symbol: " + symbol,
          "use an IUPAC element symbol or X for an unknown site"));
    }
    number = parsed_number.value();
    symbol = std::string{element_symbol(number)};
  } else {
    symbol = "X";
  }
  return operation::Result<ParsedAtom>::success(
      ParsedAtom{number, std::move(symbol), position});
}

std::string structure_name(std::string_view source_name) {
  if (source_name == "<memory>") return "xyz";
  auto name = std::filesystem::path{source_name}.stem().string();
  return name.empty() ? std::string{"xyz"} : name;
}

}  // namespace

operation::Result<StructureDocument> read_xyz(std::string_view content,
                                              std::string source_name) {
  LineCursor cursor{content};
  std::vector<std::shared_ptr<const model::CoordinateFrame>> frames;
  std::vector<std::uint8_t> element_numbers;
  std::vector<std::string> symbols;
  std::vector<std::string> comments;
  while (true) {
    auto count_line = cursor.next();
    while (count_line.has_value() && trim(count_line->text).empty()) {
      count_line = cursor.next();
    }
    if (!count_line.has_value()) break;
    const auto count = parse_atom_count(*count_line, source_name);
    if (!count.has_value()) {
      return operation::Result<StructureDocument>::failure(count.error());
    }
    const auto comment_line = cursor.next();
    if (!comment_line.has_value()) {
      return operation::Result<StructureDocument>::failure(parse_error(
          source_name, count_line->number,
          "XYZ frame is missing its required comment line"));
    }
    std::vector<model::Vec3d> positions;
    std::vector<std::uint8_t> frame_elements;
    std::vector<std::string> frame_symbols;
    for (std::size_t atom_index = 0; atom_index < count.value();
         ++atom_index) {
      const auto atom_line = cursor.next();
      if (!atom_line.has_value()) {
        return operation::Result<StructureDocument>::failure(parse_error(
            source_name, comment_line->number,
            "XYZ frame ended before all declared atoms were read"));
      }
      const auto atom = parse_atom(*atom_line, source_name);
      if (!atom.has_value()) {
        return operation::Result<StructureDocument>::failure(atom.error());
      }
      frame_elements.push_back(atom.value().atomic_number);
      frame_symbols.push_back(atom.value().symbol);
      positions.push_back(atom.value().position);
    }
    if (frames.empty()) {
      element_numbers = frame_elements;
      symbols = frame_symbols;
    } else if (frame_elements != element_numbers) {
      return operation::Result<StructureDocument>::failure(parse_error(
          source_name, count_line->number,
          "XYZ frame atom count/order/elements differ from the first frame",
          "split changing-topology XYZ blocks or preserve a stable atom order"));
    }
    model::FrameMetadata metadata;
    metadata.source_step = static_cast<std::uint64_t>(frames.size());
    metadata.coordinate_unit = operation::LengthUnit::angstrom;
    metadata.fields.emplace("xyz.comment", std::string{comment_line->text});
    auto frame = model::CoordinateFrame::create(
        model::CoordinateBuffer{std::move(positions)}, std::nullopt, {},
        std::move(metadata));
    if (!frame.has_value()) {
      return operation::Result<StructureDocument>::failure(frame.error());
    }
    frames.push_back(std::move(frame.value()));
    comments.emplace_back(comment_line->text);
  }
  if (frames.empty()) {
    return operation::Result<StructureDocument>::failure(parse_error(
        source_name, 1U, "XYZ input contains no coordinate frames"));
  }

  model::TopologyBuilder builder;
  const auto residue = builder.add_residue({"MOL", 1, "", "", ""});
  if (!residue.has_value()) {
    return operation::Result<StructureDocument>::failure(residue.error());
  }
  for (std::size_t index = 0; index < element_numbers.size(); ++index) {
    const auto atom = builder.add_atom(
        {symbols[index], element_numbers[index], residue.value(), "", 0,
         static_cast<std::int64_t>(index + 1U)});
    if (!atom.has_value()) {
      return operation::Result<StructureDocument>::failure(atom.error());
    }
  }
  builder.set_source_metadata("format", "xyz");
  const auto topology = builder.build();
  if (!topology.has_value()) {
    return operation::Result<StructureDocument>::failure(topology.error());
  }
  const auto coordinates = model::InMemoryCoordinateSource::create(
      element_numbers.size(), std::move(frames));
  if (!coordinates.has_value()) {
    return operation::Result<StructureDocument>::failure(coordinates.error());
  }
  StructureData structure;
  structure.name = structure_name(source_name);
  structure.topology = topology.value();
  structure.coordinates = coordinates.value();
  structure.metadata.emplace("format", "xyz");
  structure.metadata.emplace("xyz.comment", comments.front());
  StructureDocument document;
  document.format = StructureFormat::xyz;
  document.source_name = std::move(source_name);
  document.structures.push_back(std::move(structure));
  return operation::Result<StructureDocument>::success(std::move(document));
}

}  // namespace molshredder::io::detail
