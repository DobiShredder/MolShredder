#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <variant>
#include <vector>

#include "molshredder/io/trajectory_reader.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  if (argc != 3) {
    std::cerr << "expected LAMMPS fixture and build directory\n";
    return 2;
  }
  bool passed = true;
  const std::vector<std::int64_t> ids{10, 20, 30};
  const auto source = io::open_lammps_dump(
      argv[1], ids, operation::LengthUnit::angstrom);
  passed &= expect(source.has_value() && source.value()->atom_count() == 3U &&
                       source.value()->frame_count() == 2U &&
                       source.value()->access() ==
                           model::FrameAccess::random_access &&
                       source.value()->metadata().has_restricted_triclinic_cell &&
                       source.value()->metadata().coordinate_convention ==
                           io::LammpsCoordinateConvention::mixed,
                   "LAMMPS dump must index mixed orthogonal/triclinic frames");
  if (source.has_value()) {
    const auto second = source.value()->read_frame(1U);
    const auto first = source.value()->read_frame(0U);
    passed &= expect(second.has_value() && first.has_value(),
                     "LAMMPS frames must decode out of order");
    if (first.has_value()) {
      const auto &positions = std::get<std::vector<model::Vec3d>>(
          first.value()->positions().values());
      const auto *elements = std::get_if<std::vector<std::string>>(
          &first.value()->metadata().atom_properties.at("lammps.element")
               .values);
      const auto *types = std::get_if<std::vector<std::int64_t>>(
          &first.value()->metadata().atom_properties.at("lammps.type").values);
      passed &= expect(
          positions[0] == model::Vec3d{1.0, 2.0, -1.0} &&
              positions[1] == model::Vec3d{3.0, 5.0, 1.0} &&
              positions[2] == model::Vec3d{5.0, 3.5, 2.0} && elements != nullptr &&
              *elements == std::vector<std::string>{"C", "O", "H"} &&
              types != nullptr && *types == std::vector<std::int64_t>{1, 2, 1} &&
              first.value()->metadata().source_step == 0U &&
              first.value()->metadata().coordinate_unit ==
                  operation::LengthUnit::angstrom,
          "scaled coordinates and custom properties must follow topology IDs");
    }
    if (second.has_value()) {
      const auto &positions = std::get<std::vector<model::Vec3d>>(
          second.value()->positions().values());
      const auto &cell = *second.value()->metadata().unit_cell;
      passed &= expect(
          positions[0] == model::Vec3d{1.0, 2.0, 0.0} &&
              positions[2] == model::Vec3d{3.0, 1.0, 1.5} &&
              cell.a == model::Vec3d{4.0, 0.0, 0.0} &&
              cell.b == model::Vec3d{1.0, 3.0, 0.0} &&
              cell.c == model::Vec3d{-0.5, 0.25, 2.0} &&
              std::abs(cell.signed_volume() - 24.0) < 1.0e-12 &&
              second.value()->metadata().source_step == 25U,
          "restricted-triclinic bounds and shuffled Cartesian rows must decode");
    }
    passed &= expect(!source.value()->read_frame(2U).has_value(),
                     "LAMMPS out-of-range seek must fail");
  }

  const auto build = std::filesystem::path{argv[2]};
  const auto general_path = build / "general-triclinic.lammpstrj";
  {
    std::ofstream output{general_path};
    output << "ITEM: TIMESTEP\n0\nITEM: NUMBER OF ATOMS\n3\n"
              "ITEM: BOX BOUNDS abc origin\n1 0 0 0\n0 1 0 0\n0 0 1 0\n"
              "ITEM: ATOMS id x y z\n10 0 0 0\n20 0 0 0\n30 0 0 0\n";
  }
  const auto general = io::open_lammps_dump(
      general_path, ids, operation::LengthUnit::angstrom);
  passed &= expect(!general.has_value() &&
                       general.error().code == operation::ErrorCode::unsupported,
                   "general triclinic input must fail explicitly");

  const auto missing_id_path = build / "missing-id.lammpstrj";
  {
    std::ofstream output{missing_id_path};
    output << "ITEM: TIMESTEP\n0\nITEM: NUMBER OF ATOMS\n3\n"
              "ITEM: BOX BOUNDS pp pp pp\n0 1\n0 1\n0 1\n"
              "ITEM: ATOMS type x y z\n1 0 0 0\n1 0 0 0\n1 0 0 0\n";
  }
  const auto missing_id = io::open_lammps_dump(
      missing_id_path, ids, operation::LengthUnit::angstrom);
  passed &= expect(!missing_id.has_value() &&
                       missing_id.error().code ==
                           operation::ErrorCode::unsupported,
                   "ID-less rows must never be attached by unstable row order");

  const auto unwrapped_path = build / "unwrapped.lammpstrj";
  {
    std::ofstream output{unwrapped_path};
    output << "ITEM: TIMESTEP\n5\nITEM: NUMBER OF ATOMS\n3\n"
              "ITEM: BOX BOUNDS pp pp pp\n1 3\n2 6\n-1 1\n"
              "ITEM: ATOMS id xu yu zu ix iy iz\n"
              "10 -3 2 0 -2 0 0\n20 5 7 0 2 1 0\n30 1 2 3 0 0 2\n"
              "ITEM: TIMESTEP\n6\nITEM: NUMBER OF ATOMS\n3\n"
              "ITEM: BOX BOUNDS pp pp pp\n1 3\n2 6\n-1 1\n"
              "ITEM: ATOMS id xsu ysu zsu\n"
              "30 1.5 -0.5 2\n10 -1 0 0\n20 2 1 0.5\n";
  }
  const auto unwrapped = io::open_lammps_dump(
      unwrapped_path, ids, operation::LengthUnit::nanometer);
  passed &= expect(unwrapped.has_value() &&
                       unwrapped.value()->metadata().coordinate_convention ==
                           io::LammpsCoordinateConvention::mixed,
                   "Cartesian and scaled unwrapped conventions must index");
  if (unwrapped.has_value()) {
    const auto cartesian = unwrapped.value()->read_frame(0U);
    const auto scaled = unwrapped.value()->read_frame(1U);
    if (cartesian.has_value() && scaled.has_value()) {
      const auto &cartesian_positions =
          std::get<std::vector<model::Vec3d>>(
              cartesian.value()->positions().values());
      const auto &scaled_positions = std::get<std::vector<model::Vec3d>>(
          scaled.value()->positions().values());
      passed &= expect(
          cartesian_positions[0] == model::Vec3d{-3.0, 2.0, 0.0} &&
              scaled_positions[0] == model::Vec3d{-1.0, 2.0, -1.0} &&
              scaled_positions[1] == model::Vec3d{5.0, 6.0, 0.0} &&
              scaled_positions[2] == model::Vec3d{4.0, 0.0, 3.0} &&
              scaled.value()->metadata().coordinate_unit ==
                  operation::LengthUnit::nanometer,
          "unwrapped coordinates must preserve values outside the primary cell");
    } else {
      passed &= expect(false, "unwrapped frames must decode");
    }
  }

  const io::TrajectoryOpenContext missing_unit{3U, ids, std::nullopt,
                                                std::nullopt, std::nullopt};
  const auto unitless = io::open_trajectory(
      argv[1], io::TrajectoryFormat::lammps_dump, missing_unit);
  passed &= expect(!unitless.has_value() &&
                       unitless.error().message.find("does not encode") !=
                           std::string::npos,
                   "generic LAMMPS open must require an explicit unit");
  std::error_code ignored;
  std::filesystem::remove(general_path, ignored);
  std::filesystem::remove(missing_id_path, ignored);
  std::filesystem::remove(unwrapped_path, ignored);
  return passed ? 0 : 1;
}
