#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "molshredder/io/structure_reader.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/error.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{stream},
          std::istreambuf_iterator<char>{}};
}

const std::vector<molshredder::model::Vec3d>& positions(
    const molshredder::model::CoordinateFrame& frame) {
  return std::get<std::vector<molshredder::model::Vec3d>>(
      frame.positions().values());
}

}  // namespace

int main(int argc, char** argv) {
  using namespace molshredder;
  using model::AtomProperty;
  using model::BooleanColumn;
  using operation::ErrorCode;

  if (argc != 3) {
    std::cerr << "expected PDB and mmCIF fixture paths\n";
    return 2;
  }
  bool passed = true;

  const auto pdb_text = read_text(argv[1]);
  const auto pdb = io::read_structure(
      pdb_text, io::StructureReadOptions{io::StructureFormat::auto_detect,
                                         "synthetic.pdb"});
  passed &= expect(pdb.has_value(), "valid PDB fixture must parse");
  if (pdb.has_value()) {
    passed &= expect(pdb.value().format == io::StructureFormat::pdb &&
                         pdb.value().structures.size() == 1,
                     "PDB auto-detection must produce one structure");
    const auto& structure = pdb.value().structures.front();
    passed &= expect(structure.name == "MS01" &&
                         structure.metadata.at("title") ==
                             "SYNTHETIC MULTI MODEL WITH TRICLINIC CELL" &&
                         structure.metadata.at("space_group") == "P 1",
                     "PDB entry and crystallographic metadata must be retained");
    passed &= expect(structure.topology->atom_count() == 3 &&
                         structure.topology->residue_count() == 2 &&
                         structure.topology->bonds().size() == 1,
                     "PDB topology, residues and CONECT bond must load");
    const auto& first_atom = structure.topology->atoms().front();
    passed &= expect(first_atom.name == "N" && first_atom.atomic_number == 7 &&
                         first_atom.formal_charge == 1 &&
                         first_atom.alternate_location == "A" &&
                         first_atom.source_serial == 1,
                     "PDB atom identity, element, charge and altloc must load");
    passed &= expect(structure.topology->residues().front().chain_id == "A" &&
                         structure.topology->residues().front().sequence_number ==
                             42 &&
                         structure.topology->residues().front().insertion_code ==
                             "B" &&
                         structure.topology->residues().front().segment_id ==
                             "SEG1",
                     "PDB residue identity and segment must load");
    const auto* hetero =
        structure.topology->properties().find("pdb.is_hetero");
    passed &= expect(
        hetero != nullptr &&
            std::get<BooleanColumn>(hetero->values).values ==
                std::vector<std::uint8_t>{0, 0, 1},
        "PDB ATOM/HETATM origin must be a typed static property");
    passed &= expect(structure.coordinates->frame_count() == 2,
                     "PDB MODEL records must become coordinate frames");
    const auto first_frame = structure.coordinates->read_frame(0);
    const auto second_frame = structure.coordinates->read_frame(1);
    passed &= expect(first_frame.has_value() && second_frame.has_value(),
                     "both PDB frames must be readable");
    if (first_frame.has_value() && second_frame.has_value()) {
      passed &= expect(first_frame.value()->metadata().source_step == 1 &&
                           second_frame.value()->metadata().source_step == 2 &&
                           first_frame.value()->metadata().unit_cell.has_value() &&
                           first_frame.value()
                                   ->metadata()
                                   .unit_cell->signed_volume() > 1000.0,
                       "PDB model number and triclinic cell must be retained");
      passed &= expect(positions(*first_frame.value())[0] ==
                           model::Vec3d{1.0, 2.0, 3.0} &&
                           positions(*second_frame.value())[1] ==
                               model::Vec3d{2.5, 3.5, 4.5},
                       "PDB coordinates must retain numeric values");
      passed &= expect(second_frame.value()->presence() ==
                           std::vector<std::uint8_t>{1, 1, 0},
                       "missing atom in a later PDB model must use presence mask");
      const auto& occupancy = first_frame.value()
                                  ->metadata()
                                  .atom_properties.at("occupancy");
      const auto& occupancy_present =
          first_frame.value()
              ->metadata()
              .atom_properties.at("occupancy_present");
      passed &= expect(std::get<std::vector<double>>(occupancy.values)[0] ==
                           0.5 &&
                           std::get<BooleanColumn>(occupancy_present.values)
                                   .values[2] == 0,
                       "PDB optional occupancy must retain value and missingness");
    }
  }

  const auto pdb_file = io::read_structure_file(argv[1]);
  passed &= expect(pdb_file.has_value() &&
                       pdb_file.value().format == io::StructureFormat::pdb,
                   "file reader must auto-detect a PDB path");
  const auto missing_file = io::read_structure_file(
      std::filesystem::path{argv[1]}.parent_path() / "does-not-exist.pdb");
  passed &= expect(!missing_file.has_value() &&
                       missing_file.error().code == ErrorCode::not_found,
                   "missing structure path must return stable not-found error");
  const auto short_pdb = io::read_structure(
      "ATOM\n", {io::StructureFormat::pdb, "bad.pdb"});
  passed &= expect(!short_pdb.has_value() &&
                       short_pdb.error().message.starts_with("bad.pdb:1:"),
                   "malformed PDB error must include source and line");
  auto unknown_connection = pdb_text;
  unknown_connection.replace(unknown_connection.find("CONECT    1    2"), 16,
                             "CONECT    1    9");
  const auto bad_connection = io::read_structure(
      unknown_connection, {io::StructureFormat::pdb, "conect.pdb"});
  passed &= expect(!bad_connection.has_value() &&
                       bad_connection.error().message.starts_with(
                           "conect.pdb:13:") &&
                       bad_connection.error().message.find("CONECT") !=
                           std::string::npos,
                   "PDB CONECT must not silently reference an unknown atom");
  auto inferred_element = pdb_text;
  const auto element_position = inferred_element.find("SEG1 N1+") + 5;
  inferred_element[element_position] = ' ';
  const auto inferred = io::read_structure(
      inferred_element, {io::StructureFormat::pdb, "inferred.pdb"});
  passed &= expect(inferred.has_value() &&
                       inferred.value()
                               .structures.front()
                               .topology->atoms()
                               .front()
                               .atomic_number == 7,
                   "blank PDB element field must infer an aligned atom-name element");

  const auto cif_text = read_text(argv[2]);
  const auto cif = io::read_structure(
      cif_text, {io::StructureFormat::auto_detect, "synthetic.cif"});
  if (!cif.has_value()) {
    std::cerr << cif.error().message << '\n';
  }
  passed &= expect(cif.has_value(), "valid mmCIF fixture must parse");
  if (cif.has_value()) {
    passed &= expect(cif.value().format == io::StructureFormat::mmcif &&
                         cif.value().structures.size() == 1,
                     "mmCIF auto-detection must produce one structure");
    const auto& structure = cif.value().structures.front();
    passed &= expect(structure.name == "MS02" &&
                         structure.metadata.at("_struct.title") ==
                             "Synthetic structure\nwith multiline metadata" &&
                         structure.metadata.at("_audit.creation_method") ==
                             "MolShredder # synthetic fixture",
                     "mmCIF scalar, quote, comment and text field semantics must load");
    passed &= expect(structure.topology->atom_count() == 3 &&
                         structure.topology->residue_count() == 2 &&
                         structure.topology->bonds().size() == 2 &&
                         structure.topology->bonds().front().order ==
                             model::BondOrder::single,
                     "mmCIF label/auth atom_site and struct_conn topology must load");
    passed &= expect(structure.topology->atoms().front().name == "N" &&
                         structure.topology->atoms().front().formal_charge == 1 &&
                         structure.topology->atoms().front().source_serial == 1,
                     "mmCIF author atom identity, charge and source id must load");
    const auto* label_asym =
        structure.topology->properties().find("mmcif.label_asym_id");
    const auto* charge_present =
        structure.topology->properties().find("formal_charge_present");
    passed &= expect(
        label_asym != nullptr &&
            std::get<std::vector<std::string>>(label_asym->values)[0] == "A" &&
            charge_present != nullptr &&
            std::get<BooleanColumn>(charge_present->values).values ==
                std::vector<std::uint8_t>{1, 0, 0},
        "mmCIF label identifiers and nullable charge must retain provenance");
    const auto first_frame = structure.coordinates->read_frame(0);
    const auto second_frame = structure.coordinates->read_frame(1);
    passed &= expect(first_frame.has_value() && second_frame.has_value() &&
                         structure.coordinates->frame_count() == 2,
                     "mmCIF model numbers must become two frames");
    if (first_frame.has_value() && second_frame.has_value()) {
      passed &= expect(first_frame.value()->metadata().source_step == 1 &&
                           second_frame.value()->metadata().source_step == 2 &&
                           first_frame.value()->metadata().unit_cell.has_value(),
                       "mmCIF model and cell metadata must load");
      passed &= expect(second_frame.value()->presence() ==
                           std::vector<std::uint8_t>{1, 1, 0} &&
                           positions(*second_frame.value())[0] ==
                               model::Vec3d{1.5, 2.5, 3.5},
                       "mmCIF missing model atom and coordinates must load");
    }
  }

  if (pdb.has_value() && cif.has_value()) {
    const auto pdb_frame =
        pdb.value().structures.front().coordinates->read_frame(0);
    const auto cif_frame =
        cif.value().structures.front().coordinates->read_frame(0);
    passed &= expect(
        pdb_frame.has_value() && cif_frame.has_value() &&
            positions(*pdb_frame.value()) == positions(*cif_frame.value()) &&
            std::abs(pdb_frame.value()->metadata().unit_cell->signed_volume() -
                     cif_frame.value()->metadata().unit_cell->signed_volume()) <
                1.0e-9,
        "equivalent PDB/mmCIF coordinates and cell must map identically");
  }

  const auto two_blocks = io::read_structure(
      "data_one\n_entry.id one\nloop_\n_atom_site.group_PDB\n"
      "_atom_site.id\n_atom_site.type_symbol\n_atom_site.label_atom_id\n"
      "_atom_site.label_alt_id\n_atom_site.label_comp_id\n"
      "_atom_site.label_asym_id\n_atom_site.label_entity_id\n"
      "_atom_site.label_seq_id\n_atom_site.pdbx_PDB_ins_code\n"
      "_atom_site.Cartn_x\n_atom_site.Cartn_y\n_atom_site.Cartn_z\n"
      "ATOM 1 C CA . GLY A 1 1 ? 0 0 0\n"
      "data_two\n_entry.id two\nloop_\n_atom_site.group_PDB\n"
      "_atom_site.id\n_atom_site.type_symbol\n_atom_site.label_atom_id\n"
      "_atom_site.label_alt_id\n_atom_site.label_comp_id\n"
      "_atom_site.label_asym_id\n_atom_site.label_entity_id\n"
      "_atom_site.label_seq_id\n_atom_site.pdbx_PDB_ins_code\n"
      "_atom_site.Cartn_x\n_atom_site.Cartn_y\n_atom_site.Cartn_z\n"
      "ATOM 1 N N . ALA B 2 1 ? 1 2 3\n",
      {io::StructureFormat::mmcif, "blocks.cif"});
  passed &= expect(two_blocks.has_value() &&
                       two_blocks.value().structures.size() == 2 &&
                       two_blocks.value().structures[0].name == "one" &&
                       two_blocks.value().structures[1].name == "two",
                   "independent mmCIF data blocks must become structures in order");

  const auto malformed_loop = io::read_structure(
      "data_bad\nloop_\n_atom_site.id\n_atom_site.type_symbol\n1\n",
      {io::StructureFormat::mmcif, "bad.cif"});
  passed &= expect(!malformed_loop.has_value() &&
                       malformed_loop.error().message.starts_with("bad.cif:2:"),
                   "non-rectangular mmCIF loop must report its source line");
  const auto unterminated_quote = io::read_structure(
      "data_bad\n_entry.id 'never closes\n",
      {io::StructureFormat::mmcif, "quote.cif"});
  passed &= expect(!unterminated_quote.has_value() &&
                       unterminated_quote.error().message.starts_with(
                           "quote.cif:2:"),
                   "unterminated mmCIF quote must report its source line");
  auto unknown_element = cif_text;
  unknown_element.replace(unknown_element.find("ATOM   1 N N"),
                          std::string{"ATOM   1 N N"}.size(),
                          "ATOM   1 Xx N");
  const auto bad_element = io::read_structure(
      unknown_element, {io::StructureFormat::mmcif, "element.cif"});
  passed &= expect(!bad_element.has_value() &&
                       bad_element.error().message.find("element symbol") !=
                           std::string::npos,
                   "unknown mmCIF element must fail instead of becoming element zero");
  auto partial_cell = cif_text;
  partial_cell.replace(partial_cell.find("_cell.length_b 11.0"),
                       std::string{"_cell.length_b 11.0"}.size(),
                       "_cell.length_b ?");
  const auto bad_cell = io::read_structure(
      partial_cell, {io::StructureFormat::mmcif, "cell.cif"});
  passed &= expect(!bad_cell.has_value() &&
                       bad_cell.error().message.find("unit cell") !=
                           std::string::npos,
                   "partial mmCIF cell must fail instead of inventing values");
  auto changed_identity = cif_text;
  changed_identity.replace(changed_identity.find("ATOM   5 C CA A"),
                           std::string{"ATOM   5 C CA A"}.size(),
                           "ATOM   5 C CB A");
  const auto bad_model = io::read_structure(
      changed_identity, {io::StructureFormat::mmcif, "model.cif"});
  passed &= expect(!bad_model.has_value() &&
                       bad_model.error().message.find("absent from model 1") !=
                           std::string::npos,
                   "later mmCIF model must not mutate topology identity");
  const auto empty = io::read_structure(" \n# only comment\n");
  passed &= expect(!empty.has_value() &&
                       empty.error().code == ErrorCode::invalid_argument,
                   "empty auto-detected input must fail explicitly");

  return passed ? 0 : 1;
}
