#include <cmath>
#include <iostream>
#include <string_view>
#include <variant>
#include <vector>

#include "molshredder/io/structure_reader.hpp"
#include "molshredder/model/coordinates.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

const std::vector<molshredder::model::Vec3d> &
positions(const molshredder::model::CoordinateFrame &frame) {
  return std::get<std::vector<molshredder::model::Vec3d>>(
      frame.positions().values());
}

} // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  constexpr std::string_view fixture = R"VTF(
# public VTF syntax fixture
atom default name X type CG resname UNK radius 1.2 mass 2.0
atom 0:2 name B chain A segid SYS resid 7 resname BEAD \
  charge -0.25 occupancy 0.75 bfactor 4.0
atom 0 name C1 atomicnumber 6 altloc A insertion I
atom 1, 2 name O atomicnumber 8
bond 0::2,2:0
unitcell 10 11 12 80 90 100
timestep ordered
0 0 0 ignored userdata
1 0 0
2 0 0
timestep indexed
1 1 2 3 trailing userdata
pbc 20 21 22
ordered
4 5 6
)VTF";
  const auto parsed = io::read_structure(
      fixture, {io::StructureFormat::auto_detect, "coarse.vtf"});
  passed &= expect(parsed.has_value(), "valid VTF fixture must parse");
  if (parsed.has_value()) {
    const auto &structure = parsed.value().structures.front();
    const auto first = structure.coordinates->read_frame(0U);
    const auto second = structure.coordinates->read_frame(1U);
    const auto third = structure.coordinates->read_frame(2U);
    const auto *types = structure.topology->properties().find("vtf.atom_type");
    const auto *radii = structure.topology->properties().find("vdw_radius");
    const auto *charges =
        structure.topology->properties().find("partial_charge");
    passed &= expect(
        parsed.value().format == io::StructureFormat::vtf &&
            structure.topology->atom_count() == 3U &&
            structure.topology->residue_count() == 2U &&
            structure.topology->bonds().size() == 3U &&
            structure.coordinates->frame_count() == 3U,
        "VTF topology, chain bonds and timestep count must be preserved");
    passed &= expect(
        structure.topology->atoms()[0].name == "C1" &&
            structure.topology->atoms()[0].atomic_number == 6U &&
            structure.topology->atoms()[0].alternate_location == "A" &&
            structure.topology->atoms()[0].source_serial == 0 &&
            structure.topology
                    ->residues()[structure.topology->atoms()[0].residue.value]
                    .name == "BEAD" &&
            structure.topology
                    ->residues()[structure.topology->atoms()[0].residue.value]
                    .sequence_number == 7 &&
            structure.topology
                    ->residues()[structure.topology->atoms()[0].residue.value]
                    .chain_id == "A" &&
            structure.topology
                    ->residues()[structure.topology->atoms()[0].residue.value]
                    .segment_id == "SYS" &&
            structure.topology
                    ->residues()[structure.topology->atoms()[0].residue.value]
                    .insertion_code == "I",
        "VTF atom and residue identity must survive cumulative definitions");
    passed &= expect(
        types != nullptr && radii != nullptr && charges != nullptr &&
            std::get<std::vector<std::string>>(types->values)[2] == "CG" &&
            std::get<std::vector<double>>(radii->values)[1] == 1.2 &&
            std::get<std::vector<double>>(charges->values)[0] == -0.25,
        "VTF type, radius and charge must remain typed properties");
    passed &= expect(
        first.has_value() && second.has_value() && third.has_value() &&
            positions(*first.value())[2] == model::Vec3d{2.0, 0.0, 0.0} &&
            positions(*second.value())[0] == model::Vec3d{0.0, 0.0, 0.0} &&
            positions(*second.value())[1] == model::Vec3d{1.0, 2.0, 3.0} &&
            positions(*third.value())[0] == model::Vec3d{4.0, 5.0, 6.0} &&
            positions(*third.value())[1] == model::Vec3d{1.0, 2.0, 3.0},
        "indexed and partial ordered VTF frames must inherit prior "
        "coordinates");
    passed &= expect(
        first.value()->metadata().coordinate_unit ==
                operation::LengthUnit::angstrom &&
            first.value()->metadata().unit_cell.has_value() &&
            std::abs(first.value()->metadata().unit_cell->signed_volume() -
                     1279.5782) < 0.01 &&
            third.value()->metadata().unit_cell.has_value() &&
            std::abs(third.value()->metadata().unit_cell->signed_volume() -
                     9240.0) < 0.01,
        "VTF triclinic and inherited orthogonal unit cells must be preserved");
  }

  const auto structure_only = io::read_structure(
      "atom 2 name Q\nbond 0::2\n", {io::StructureFormat::vtf, "only.vtf"});
  passed &= expect(
      structure_only.has_value() &&
          structure_only.value().structures[0].topology->atom_count() == 3U &&
          structure_only.value().structures[0].coordinates->frame_count() == 0U,
      "VTF gaps must materialize default atoms and structure-only input must "
      "load");

  const auto incomplete_first =
      io::read_structure("atom 0:1\ntimestep indexed\n0 0 0 0\n",
                         {io::StructureFormat::vtf, "incomplete.vtf"});
  passed &= expect(!incomplete_first.has_value() &&
                       incomplete_first.error().message.find(
                           "first VTF timestep") != std::string::npos,
                   "first VTF frame must not invent missing coordinates");
  const auto duplicate_index = io::read_structure(
      "atom 0\ntimestep\n0 0 0\ntimestep indexed\n0 1 1 1\n0 2 2 2\n",
      {io::StructureFormat::vtf, "duplicate.vtf"});
  passed &= expect(!duplicate_index.has_value() &&
                       duplicate_index.error().message.find("more than once") !=
                           std::string::npos,
                   "indexed VTF frames must reject duplicate atom updates");
  const auto malformed_cell = io::read_structure(
      "atom 0\nunitcell 1 2 -3\n", {io::StructureFormat::vtf, "cell.vtf"});
  passed &= expect(!malformed_cell.has_value(),
                   "invalid VTF cell geometry must be rejected");
  const auto unknown_option = io::read_structure(
      "atom 0 color blue\n", {io::StructureFormat::vtf, "option.vtf"});
  passed &= expect(!unknown_option.has_value() &&
                       unknown_option.error().message.find(
                           "unknown VTF atom option") != std::string::npos,
                   "unknown VTF atom options must not be discarded silently");

  return passed ? 0 : 1;
}
