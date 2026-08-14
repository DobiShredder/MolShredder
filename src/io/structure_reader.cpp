#include "molshredder/io/structure_reader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

#include "structure_reader_internal.hpp"

namespace molshredder::io {
namespace {

bool starts_with_case_insensitive(std::string_view value,
                                  std::string_view prefix) {
  return value.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), value.begin(),
                    [](char left, char right) {
                      return std::tolower(static_cast<unsigned char>(left)) ==
                             std::tolower(static_cast<unsigned char>(right));
                    });
}

StructureFormat detect_format(std::string_view content) {
  std::size_t position = 0;
  while (position < content.size()) {
    const auto end = content.find('\n', position);
    const auto line = content.substr(
        position, end == std::string_view::npos ? content.size() - position
                                                : end - position);
    const auto cleaned = detail::trim(line);
    if (!cleaned.empty() && cleaned.front() != '#') {
      if (starts_with_case_insensitive(cleaned, "data_") ||
          starts_with_case_insensitive(cleaned, "loop_") ||
          cleaned.front() == '_') {
        return StructureFormat::mmcif;
      }
      return StructureFormat::pdb;
    }
    if (end == std::string_view::npos) {
      break;
    }
    position = end + 1;
  }
  return StructureFormat::auto_detect;
}

}  // namespace

operation::Result<StructureDocument> read_structure(
    std::string_view content, StructureReadOptions options) {
  auto format = options.format;
  if (format == StructureFormat::auto_detect) {
    format = detect_format(content);
  }
  if (format == StructureFormat::auto_detect) {
    return operation::Result<StructureDocument>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        options.source_name + ": input is empty or has no recognizable records",
        "provide PDB fixed-column or PDBx/mmCIF content"});
  }
  if (format == StructureFormat::pdb) {
    return detail::read_pdb(content, std::move(options.source_name));
  }
  return detail::read_mmcif(content, std::move(options.source_name));
}

operation::Result<StructureDocument> read_structure_file(
    const std::filesystem::path& path, StructureFormat format) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return operation::Result<StructureDocument>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "could not open structure file: " + path.string(),
        "check that the path exists and is readable"});
  }
  std::string content{std::istreambuf_iterator<char>{stream},
                      std::istreambuf_iterator<char>{}};
  if (!stream.good() && !stream.eof()) {
    return operation::Result<StructureDocument>::failure(operation::Error{
        operation::ErrorCode::internal,
        "failed while reading structure file: " + path.string(),
        "check the storage device and file permissions"});
  }
  return read_structure(content,
                        StructureReadOptions{format, path.string()});
}

std::string_view to_string(StructureFormat format) noexcept {
  switch (format) {
    case StructureFormat::auto_detect:
      return "auto";
    case StructureFormat::pdb:
      return "pdb";
    case StructureFormat::mmcif:
      return "mmcif";
  }
  return "auto";
}

namespace detail {

std::string trim(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1);
  }
  return std::string{value};
}

std::optional<std::uint8_t> atomic_number(std::string_view symbol) {
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
  auto normalized = trim(symbol);
  if (normalized.empty()) {
    return std::uint8_t{0};
  }
  normalized[0] =
      static_cast<char>(std::toupper(static_cast<unsigned char>(normalized[0])));
  for (std::size_t index = 1; index < normalized.size(); ++index) {
    normalized[index] = static_cast<char>(
        std::tolower(static_cast<unsigned char>(normalized[index])));
  }
  const auto found = std::find(symbols.begin() + 1, symbols.end(), normalized);
  if (found == symbols.end()) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>(std::distance(symbols.begin(), found));
}

operation::Error parse_error(std::string_view source, std::size_t line,
                             std::string message, std::string suggestion) {
  return operation::Error{
      operation::ErrorCode::invalid_argument,
      std::string{source} + ":" + std::to_string(line) + ": " +
          std::move(message),
      std::move(suggestion)};
}

operation::Result<model::UnitCell> make_unit_cell(
    double a, double b, double c, double alpha_degrees, double beta_degrees,
    double gamma_degrees, std::string_view source, std::size_t line) {
  constexpr double pi = 3.141592653589793238462643383279502884;
  const auto alpha = alpha_degrees * pi / 180.0;
  const auto beta = beta_degrees * pi / 180.0;
  const auto gamma = gamma_degrees * pi / 180.0;
  if (!(a > 0.0 && b > 0.0 && c > 0.0) ||
      !std::isfinite(alpha) || !std::isfinite(beta) ||
      !std::isfinite(gamma)) {
    return operation::Result<model::UnitCell>::failure(parse_error(
        source, line, "unit-cell lengths and angles must be finite and positive"));
  }
  const auto sin_gamma = std::sin(gamma);
  if (std::abs(sin_gamma) <= std::numeric_limits<double>::epsilon()) {
    return operation::Result<model::UnitCell>::failure(
        parse_error(source, line, "unit-cell gamma angle is degenerate"));
  }
  const auto cx = c * std::cos(beta);
  const auto cy =
      c * (std::cos(alpha) - std::cos(beta) * std::cos(gamma)) / sin_gamma;
  const auto cz_squared = c * c - cx * cx - cy * cy;
  if (!(cz_squared > 0.0)) {
    return operation::Result<model::UnitCell>::failure(parse_error(
        source, line, "unit-cell angles do not form a right-handed 3D cell"));
  }
  model::UnitCell cell{{a, 0.0, 0.0},
                       {b * std::cos(gamma), b * sin_gamma, 0.0},
                       {cx, cy, std::sqrt(cz_squared)}};
  if (!cell.is_valid()) {
    return operation::Result<model::UnitCell>::failure(
        parse_error(source, line, "unit-cell vectors are invalid"));
  }
  return operation::Result<model::UnitCell>::success(cell);
}

}  // namespace detail
}  // namespace molshredder::io
