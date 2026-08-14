#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/io/structure_reader.hpp"

namespace molshredder::io::detail {

struct CifToken {
  std::string text;
  std::size_t line{};
  bool quoted{};
};

struct CifLoop {
  std::vector<std::string> columns;
  std::vector<CifToken> values;
  std::size_t line{};
};

struct CifBlock {
  std::string name;
  std::size_t line{};
  std::map<std::string, CifToken, std::less<>> scalars;
  std::vector<CifLoop> loops;
};

[[nodiscard]] operation::Result<StructureDocument>
read_pdb(std::string_view content, std::string source_name);

[[nodiscard]] operation::Result<StructureDocument>
read_pqr(std::string_view content, std::string source_name);

[[nodiscard]] operation::Result<StructureDocument>
read_molfile(std::string_view content, std::string source_name,
             StructureFormat format);

[[nodiscard]] operation::Result<StructureDocument>
read_mol2(std::string_view content, std::string source_name);

[[nodiscard]] operation::Result<StructureDocument>
read_psf(std::string_view content, std::string source_name);

[[nodiscard]] operation::Result<StructureDocument>
read_prmtop(std::string_view content, std::string source_name);

[[nodiscard]] operation::Result<StructureDocument>
read_gro(std::string_view content, std::string source_name);

[[nodiscard]] operation::Result<StructureDocument>
read_g96(std::string_view content, std::string source_name);

[[nodiscard]] operation::Result<StructureDocument>
read_vtf(std::string_view content, std::string source_name);

[[nodiscard]] operation::Result<StructureDocument>
read_mmcif(std::string_view content, std::string source_name);

[[nodiscard]] operation::Result<StructureDocument>
read_bcif(std::string_view content, std::string source_name);

[[nodiscard]] operation::Result<StructureDocument>
build_cif_document(const std::vector<CifBlock> &blocks, std::string source_name,
                   StructureFormat format, std::string syntax);

[[nodiscard]] operation::Result<StructureDocument>
read_xyz(std::string_view content, std::string source_name);

[[nodiscard]] std::string trim(std::string_view value);

[[nodiscard]] std::optional<std::uint8_t>
atomic_number(std::string_view symbol);

[[nodiscard]] std::string_view
element_symbol(std::uint8_t atomic_number) noexcept;

[[nodiscard]] operation::Error parse_error(std::string_view source,
                                           std::size_t line,
                                           std::string message,
                                           std::string suggestion = {});

[[nodiscard]] operation::Result<model::UnitCell>
make_unit_cell(double a, double b, double c, double alpha_degrees,
               double beta_degrees, double gamma_degrees,
               std::string_view source, std::size_t line);

} // namespace molshredder::io::detail
