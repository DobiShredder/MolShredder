#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "molshredder/io/structure_reader.hpp"

namespace molshredder::io::detail {

[[nodiscard]] operation::Result<StructureDocument> read_pdb(
    std::string_view content, std::string source_name);

[[nodiscard]] operation::Result<StructureDocument> read_mmcif(
    std::string_view content, std::string source_name);

[[nodiscard]] std::string trim(std::string_view value);

[[nodiscard]] std::optional<std::uint8_t> atomic_number(
    std::string_view symbol);

[[nodiscard]] operation::Error parse_error(std::string_view source,
                                           std::size_t line,
                                           std::string message,
                                           std::string suggestion = {});

[[nodiscard]] operation::Result<model::UnitCell> make_unit_cell(
    double a, double b, double c, double alpha_degrees, double beta_degrees,
    double gamma_degrees, std::string_view source, std::size_t line);

}  // namespace molshredder::io::detail
