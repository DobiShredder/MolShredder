#include "molshredder/io/structure_reader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
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
  if (content.find("@<TRIPOS>MOLECULE") != std::string_view::npos) {
    return StructureFormat::mol2;
  }
  std::size_t meaningful_position{};
  while (meaningful_position < content.size()) {
    const auto end = content.find('\n', meaningful_position);
    const auto line = content.substr(meaningful_position,
                                     end == std::string_view::npos
                                         ? content.size() - meaningful_position
                                         : end - meaningful_position);
    const auto cleaned = detail::trim(line);
    if (!cleaned.empty() && !cleaned.starts_with('#')) {
      if (cleaned == "PSF" || cleaned.starts_with("PSF ") ||
          cleaned.starts_with("PSF\t")) {
        return StructureFormat::psf;
      }
      if (cleaned.starts_with("ATOM  ") || cleaned.starts_with("HETATM") ||
          cleaned.starts_with("HEADER")) {
        return StructureFormat::pdb;
      }
      if (cleaned == "TITLE")
        return StructureFormat::g96;
      if (cleaned.starts_with("%VERSION"))
        return StructureFormat::prmtop;
      const auto first_space = cleaned.find_first_of(" \t");
      auto keyword = cleaned.substr(0U, first_space);
      std::transform(keyword.begin(), keyword.end(), keyword.begin(),
                     [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                     });
      if (keyword == "atom" || keyword == "a" || keyword == "bond" ||
          keyword == "b" || keyword == "unitcell" || keyword == "u" ||
          keyword == "pbc" || keyword == "p" || keyword == "timestep" ||
          keyword == "t" || keyword == "coordinates" || keyword == "c" ||
          keyword == "indexed" || keyword == "i" || keyword == "ordered" ||
          keyword == "o") {
        return StructureFormat::vtf;
      }
      break;
    }
    if (end == std::string_view::npos)
      break;
    meaningful_position = end + 1U;
  }
  std::size_t header_position{};
  for (std::size_t line_index = 0; line_index < 4U; ++line_index) {
    if (header_position > content.size())
      break;
    const auto end = content.find('\n', header_position);
    auto line =
        content.substr(header_position, end == std::string_view::npos
                                            ? content.size() - header_position
                                            : end - header_position);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1U);
    if (line_index == 3U && (line.find("V2000") != std::string_view::npos ||
                             line.find("V3000") != std::string_view::npos)) {
      return content.find("$$$$") == std::string_view::npos
                 ? StructureFormat::mol
                 : StructureFormat::sdf;
    }
    if (end == std::string_view::npos)
      break;
    header_position = end + 1U;
  }
  std::array<std::string_view, 3> first_lines{};
  std::size_t line_position{};
  std::size_t line_count{};
  while (line_count < first_lines.size() && line_position < content.size()) {
    const auto end = content.find('\n', line_position);
    first_lines[line_count++] =
        content.substr(line_position, end == std::string_view::npos
                                          ? content.size() - line_position
                                          : end - line_position);
    if (end == std::string_view::npos)
      break;
    line_position = end + 1U;
  }
  if (line_count == first_lines.size()) {
    const auto count_text = detail::trim(first_lines[1]);
    std::size_t atom_count{};
    const auto count_result = std::from_chars(
        count_text.data(), count_text.data() + count_text.size(), atom_count);
    const auto atom_line = first_lines[2];
    std::int64_t residue_number{};
    const auto residue_field = detail::trim(
        atom_line.substr(0U, std::min<std::size_t>(5U, atom_line.size())));
    const auto residue_result = std::from_chars(
        residue_field.data(), residue_field.data() + residue_field.size(),
        residue_number);
    if (count_result.ec == std::errc{} &&
        count_result.ptr == count_text.data() + count_text.size() &&
        atom_count > 0U && atom_line.size() >= 44U &&
        residue_result.ec == std::errc{} &&
        residue_result.ptr == residue_field.data() + residue_field.size()) {
      return StructureFormat::gro;
    }
  }
  std::size_t position = 0;
  while (position < content.size()) {
    const auto end = content.find('\n', position);
    const auto line = content.substr(position, end == std::string_view::npos
                                                   ? content.size() - position
                                                   : end - position);
    const auto cleaned = detail::trim(line);
    if (!cleaned.empty() && cleaned.front() != '#') {
      std::size_t atom_count{};
      const auto count_result = std::from_chars(
          cleaned.data(), cleaned.data() + cleaned.size(), atom_count);
      if (count_result.ec == std::errc{} &&
          count_result.ptr == cleaned.data() + cleaned.size()) {
        return StructureFormat::xyz;
      }
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

} // namespace

operation::Result<StructureDocument>
read_structure(std::string_view content, StructureReadOptions options) {
  auto format = options.format;
  if (format == StructureFormat::auto_detect) {
    format = detect_format(content);
  }
  if (format == StructureFormat::auto_detect) {
    return operation::Result<StructureDocument>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        options.source_name + ": input is empty or has no recognizable records",
        "provide PDB, PDBx/mmCIF, MOL/SDF/MOL2, G96/GRO, explicit PQR, or XYZ "
        "content"});
  }
  if (format == StructureFormat::pdb) {
    return detail::read_pdb(content, std::move(options.source_name));
  }
  if (format == StructureFormat::pqr) {
    return detail::read_pqr(content, std::move(options.source_name));
  }
  if (format == StructureFormat::mol || format == StructureFormat::sdf) {
    return detail::read_molfile(content, std::move(options.source_name),
                                format);
  }
  if (format == StructureFormat::mol2) {
    return detail::read_mol2(content, std::move(options.source_name));
  }
  if (format == StructureFormat::psf) {
    return detail::read_psf(content, std::move(options.source_name));
  }
  if (format == StructureFormat::prmtop) {
    return detail::read_prmtop(content, std::move(options.source_name));
  }
  if (format == StructureFormat::gro) {
    return detail::read_gro(content, std::move(options.source_name));
  }
  if (format == StructureFormat::g96) {
    return detail::read_g96(content, std::move(options.source_name));
  }
  if (format == StructureFormat::vtf) {
    return detail::read_vtf(content, std::move(options.source_name));
  }
  if (format == StructureFormat::mmcif) {
    return detail::read_mmcif(content, std::move(options.source_name));
  }
  if (format == StructureFormat::bcif) {
    return detail::read_bcif(content, std::move(options.source_name));
  }
  return detail::read_xyz(content, std::move(options.source_name));
}

operation::Result<StructureDocument>
read_structure_file(const std::filesystem::path &path, StructureFormat format) {
  if (format == StructureFormat::auto_detect) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                     return static_cast<char>(std::tolower(value));
                   });
    if (extension == ".pqr")
      format = StructureFormat::pqr;
    if (extension == ".mol")
      format = StructureFormat::mol;
    if (extension == ".mol2")
      format = StructureFormat::mol2;
    if (extension == ".psf")
      format = StructureFormat::psf;
    if (extension == ".prmtop" || extension == ".parm7" ||
        extension == ".top") {
      format = StructureFormat::prmtop;
    }
    if (extension == ".gro")
      format = StructureFormat::gro;
    if (extension == ".g96")
      format = StructureFormat::g96;
    if (extension == ".vtf")
      format = StructureFormat::vtf;
    if (extension == ".sdf" || extension == ".sd") {
      format = StructureFormat::sdf;
    }
    if (extension == ".bcif")
      format = StructureFormat::bcif;
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return operation::Result<StructureDocument>::failure(
        operation::Error{operation::ErrorCode::not_found,
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
  return read_structure(content, StructureReadOptions{format, path.string()});
}

std::string_view to_string(StructureFormat format) noexcept {
  switch (format) {
  case StructureFormat::auto_detect:
    return "auto";
  case StructureFormat::pdb:
    return "pdb";
  case StructureFormat::mmcif:
    return "mmcif";
  case StructureFormat::bcif:
    return "bcif";
  case StructureFormat::pqr:
    return "pqr";
  case StructureFormat::mol:
    return "mol";
  case StructureFormat::sdf:
    return "sdf";
  case StructureFormat::mol2:
    return "mol2";
  case StructureFormat::psf:
    return "psf";
  case StructureFormat::prmtop:
    return "prmtop";
  case StructureFormat::gro:
    return "gro";
  case StructureFormat::g96:
    return "g96";
  case StructureFormat::vtf:
    return "vtf";
  case StructureFormat::xyz:
    return "xyz";
  }
  return "auto";
}

namespace detail {

namespace {

constexpr std::array<std::string_view, 119> kElementSymbols{
    "",   "H",  "He", "Li", "Be", "B",  "C",  "N",  "O",  "F",  "Ne", "Na",
    "Mg", "Al", "Si", "P",  "S",  "Cl", "Ar", "K",  "Ca", "Sc", "Ti", "V",
    "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br",
    "Kr", "Rb", "Sr", "Y",  "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag",
    "Cd", "In", "Sn", "Sb", "Te", "I",  "Xe", "Cs", "Ba", "La", "Ce", "Pr",
    "Nd", "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu",
    "Hf", "Ta", "W",  "Re", "Os", "Ir", "Pt", "Au", "Hg", "Tl", "Pb", "Bi",
    "Po", "At", "Rn", "Fr", "Ra", "Ac", "Th", "Pa", "U",  "Np", "Pu", "Am",
    "Cm", "Bk", "Cf", "Es", "Fm", "Md", "No", "Lr", "Rf", "Db", "Sg", "Bh",
    "Hs", "Mt", "Ds", "Rg", "Cn", "Nh", "Fl", "Mc", "Lv", "Ts", "Og"};

} // namespace

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
  auto normalized = trim(symbol);
  if (normalized.empty()) {
    return std::uint8_t{0};
  }
  normalized[0] = static_cast<char>(
      std::toupper(static_cast<unsigned char>(normalized[0])));
  for (std::size_t index = 1; index < normalized.size(); ++index) {
    normalized[index] = static_cast<char>(
        std::tolower(static_cast<unsigned char>(normalized[index])));
  }
  const auto found =
      std::find(kElementSymbols.begin() + 1, kElementSymbols.end(), normalized);
  if (found == kElementSymbols.end()) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>(
      std::distance(kElementSymbols.begin(), found));
}

std::string_view element_symbol(std::uint8_t atomic_number) noexcept {
  return atomic_number < kElementSymbols.size() ? kElementSymbols[atomic_number]
                                                : std::string_view{};
}

operation::Error parse_error(std::string_view source, std::size_t line,
                             std::string message, std::string suggestion) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::string{source} + ":" + std::to_string(line) +
                              ": " + std::move(message),
                          std::move(suggestion)};
}

operation::Result<model::UnitCell>
make_unit_cell(double a, double b, double c, double alpha_degrees,
               double beta_degrees, double gamma_degrees,
               std::string_view source, std::size_t line) {
  constexpr double pi = 3.141592653589793238462643383279502884;
  const auto alpha = alpha_degrees * pi / 180.0;
  const auto beta = beta_degrees * pi / 180.0;
  const auto gamma = gamma_degrees * pi / 180.0;
  if (!(a > 0.0 && b > 0.0 && c > 0.0) || !std::isfinite(alpha) ||
      !std::isfinite(beta) || !std::isfinite(gamma)) {
    return operation::Result<model::UnitCell>::failure(parse_error(
        source, line,
        "unit-cell lengths and angles must be finite and positive"));
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

} // namespace detail
} // namespace molshredder::io
