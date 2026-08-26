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

std::string read_text(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{stream},
          std::istreambuf_iterator<char>{}};
}

const std::vector<molshredder::model::Vec3d> &
positions(const molshredder::model::CoordinateFrame &frame) {
  return std::get<std::vector<molshredder::model::Vec3d>>(
      frame.positions().values());
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  using model::AtomProperty;
  using model::BooleanColumn;
  using operation::ErrorCode;

  if (argc != 11) {
    std::cerr << "expected synthetic PDB, mmCIF, 1UBQ PDB, PQR, SDF, MOL2, "
                 "GRO, G96, PSF and PRMTOP fixture paths\n";
    return 2;
  }
  bool passed = true;

  const auto mol2_text = read_text(argv[6]);
  const auto mol2 = io::read_structure(
      mol2_text, {io::StructureFormat::auto_detect, "chemistry.mol2"});
  passed &= expect(mol2.has_value() &&
                       mol2.value().format == io::StructureFormat::mol2 &&
                       mol2.value().structures.size() == 2U,
                   "MOL2 marker sniffing must load every molecule record");
  if (mol2.has_value()) {
    const auto &amide = mol2.value().structures[0];
    const auto &aromatic = mol2.value().structures[1];
    const auto *atom_types =
        amide.topology->properties().find("mol2.atom_type");
    const auto *charges = amide.topology->properties().find("partial_charge");
    const auto *charge_present =
        aromatic.topology->properties().find("partial_charge_present");
    const auto *status = amide.topology->properties().find("mol2.status_bits");
    const auto frame = amide.coordinates->read_frame(0U);
    passed &= expect(
        amide.name == "Acetamide" && aromatic.name == "Benzene" &&
            amide.topology->atom_count() == 4U &&
            amide.topology->residue_count() == 1U &&
            amide.topology->residues()[0].chain_id == "A" &&
            amide.topology->bonds()[0].order == model::BondOrder::double_bond &&
            amide.topology->bonds()[1].order == model::BondOrder::amide &&
            aromatic.topology->bonds()[0].order == model::BondOrder::aromatic &&
            amide.topology->bonds().size() == 3U &&
            amide.topology->source_metadata().at(
                "mol2.not_connected.0.status") == "PROXIMITY_ONLY",
        "MOL2 residue identity, connected bond kinds and retained nc rows "
        "must load");
    passed &= expect(
        atom_types != nullptr && charges != nullptr && status != nullptr &&
            std::get<std::vector<std::string>>(atom_types->values)[2] ==
                "N.am" &&
            std::get<std::vector<double>>(charges->values)[0] == 0.5 &&
            std::get<std::vector<std::string>>(status->values)[2] ==
                "BACKBONE" &&
            charge_present != nullptr &&
            std::get<model::BooleanColumn>(charge_present->values).values ==
                std::vector<std::uint8_t>(6U, 0U),
        "MOL2 atom type, partial charge presence and status must be typed");
    passed &= expect(
        frame.has_value() && frame.value()->metadata().unit_cell.has_value() &&
            frame.value()->metadata().unit_cell->signed_volume() > 8000.0 &&
            amide.topology->source_metadata().at("mol2.molecule_type") ==
                "SMALL" &&
            amide.topology->source_metadata().at("mol2.charge_type") ==
                "USER_CHARGES" &&
            amide.topology->source_metadata().at("mol2.bond_status.1") ==
                "AMIDE",
        "MOL2 CRYSIN, molecule/charge type and bond status must retain "
        "provenance");
  }

  const auto gro_text = read_text(argv[7]);
  const auto gro = io::read_structure(
      gro_text, {io::StructureFormat::auto_detect, "trajectory.gro"});
  passed &= expect(
      gro.has_value() && gro.value().format == io::StructureFormat::gro &&
          gro.value().structures.size() == 1U &&
          gro.value().structures.front().topology->atom_count() == 2U &&
          gro.value().structures.front().coordinates->frame_count() == 2U,
      "GRO title/count/fixed atom records must auto-detect as one trajectory");
  if (gro.has_value()) {
    const auto &structure = gro.value().structures.front();
    const auto first = structure.coordinates->read_frame(0U);
    const auto second = structure.coordinates->read_frame(1U);
    const auto *inferred =
        structure.topology->properties().find("gro.element_inferred");
    passed &= expect(
        structure.topology->residues()[0].name == "SOL" &&
            structure.topology->atoms()[0].name == "OW" &&
            structure.topology->atoms()[0].atomic_number == 8U &&
            structure.topology->atoms()[1].source_serial == 2 &&
            inferred != nullptr && first.has_value() && second.has_value() &&
            first.value()->metadata().coordinate_unit ==
                operation::LengthUnit::nanometer &&
            first.value()->velocities().has_value() &&
            first.value()->metadata().velocity_time_unit ==
                model::TimeUnit::picosecond &&
            first.value()->metadata().physical_time->value == 0.0 &&
            second.value()->metadata().physical_time->value == 2.5 &&
            !second.value()->velocities().has_value() &&
            positions(*second.value())[0] == model::Vec3d{0.11, 0.21, 0.31} &&
            second.value()->metadata().unit_cell.has_value() &&
            second.value()->metadata().unit_cell->b.x == 0.3 &&
            second.value()->metadata().unit_cell->c.y == 0.1,
        "GRO identity, variable precision, velocity, time and nine-value box "
        "must load");
  }
  auto changed_gro = gro_text;
  const auto changed_position = changed_gro.rfind("HW1");
  if (changed_position != std::string::npos) {
    changed_gro.replace(changed_position, 3U, "HW2");
  }
  const auto changed_gro_result = io::read_structure(
      changed_gro, {io::StructureFormat::gro, "changed.gro"});
  passed &=
      expect(!changed_gro_result.has_value() &&
                 changed_gro_result.error().message.find(
                     "differs from the first") != std::string::npos,
             "concatenated GRO frames must not silently mutate atom identity");
  const auto mixed_velocity_gro = io::read_structure(
      "mixed\n2\n"
      "    1SOL     OW    1   0.100   0.200   0.300  0.0100  0.0200  0.0300\n"
      "    1SOL    HW1    2   0.150   0.250   0.350\n"
      "1 1 1\n",
      {io::StructureFormat::gro, "mixed.gro"});
  passed &= expect(!mixed_velocity_gro.has_value() &&
                       mixed_velocity_gro.error().message.find("mixes") !=
                           std::string::npos,
                   "one GRO frame must not mix velocity presence by atom");
  const auto malformed_velocity_gro = io::read_structure(
      "malformed velocity\n1\n"
      "    1SOL     OW    1   0.100   0.200   0.300   0.0100  0.0200  0.0300\n"
      "1 1 1\n",
      {io::StructureFormat::gro, "malformed-velocity.gro"});
  passed &= expect(
      !malformed_velocity_gro.has_value() &&
          malformed_velocity_gro.error().message.find("velocity field") !=
              std::string::npos,
      "GRO velocity fields must begin at the inferred fixed-width boundary");
  const auto negative_identity_gro =
      io::read_structure("negative identity\n1\n"
                         "   -1SOL     OW   -1   0.100   0.200   0.300\n"
                         "1 1 1\n",
                         {io::StructureFormat::gro, "negative-identity.gro"});
  passed &=
      expect(!negative_identity_gro.has_value() &&
                 negative_identity_gro.error().message.find("non-negative") !=
                     std::string::npos,
             "GRO residue and atom numbers must reject negative identities");
  const auto huge_gro =
      io::read_structure("huge\n1000000000\n"
                         "    1SOL     OW    1   0.100   0.200   0.300\n",
                         {io::StructureFormat::gro, "huge.gro"});
  passed &=
      expect(!huge_gro.has_value() && huge_gro.error().message.find(
                                          "ended before") != std::string::npos,
             "malicious GRO count must fail without count-sized allocation");

  const auto g96_text = read_text(argv[8]);
  const auto g96 = io::read_structure(
      g96_text, {io::StructureFormat::auto_detect, "trajectory.g96"});
  passed &= expect(
      g96.has_value() && g96.value().format == io::StructureFormat::g96 &&
          g96.value().structures.size() == 1U &&
          g96.value().structures.front().topology->atom_count() == 2U &&
          g96.value().structures.front().coordinates->frame_count() == 2U,
      "G96 TITLE and ordered frame blocks must auto-detect as one trajectory");
  if (g96.has_value()) {
    const auto &structure = g96.value().structures.front();
    const auto first = structure.coordinates->read_frame(0U);
    const auto second = structure.coordinates->read_frame(1U);
    const auto *inferred =
        structure.topology->properties().find("g96.element_inferred");
    passed &= expect(
        structure.topology->source_metadata().at("g96.title") ==
                "Synthetic G96 water trajectory" &&
            structure.topology->residues()[0].name == "SOL" &&
            structure.topology->atoms()[0].name == "OW" &&
            structure.topology->atoms()[0].atomic_number == 8U &&
            inferred != nullptr && first.has_value() && second.has_value() &&
            first.value()->metadata().coordinate_unit ==
                operation::LengthUnit::nanometer &&
            first.value()->velocities().has_value() &&
            first.value()->metadata().velocity_time_unit ==
                model::TimeUnit::picosecond &&
            first.value()->metadata().source_step == 0U &&
            second.value()->metadata().source_step == 25U &&
            second.value()->metadata().physical_time->value == 2.5 &&
            !second.value()->velocities().has_value() &&
            positions(*second.value())[0] == model::Vec3d{0.11, 0.21, 0.31} &&
            second.value()->metadata().unit_cell.has_value() &&
            second.value()->metadata().unit_cell->b.x == 0.3 &&
            second.value()->metadata().unit_cell->c.y == 0.1,
        "G96 identity, F15.9 values, velocity, time and box must load");
  }
  const auto reduced_g96 =
      io::read_structure("TITLE\nreduced\nEND\nPOSITIONRED\n"
                         "    0.100000000    0.200000000    0.300000000\nEND\n",
                         {io::StructureFormat::g96, "reduced.g96"});
  passed &= expect(
      reduced_g96.has_value() &&
          reduced_g96.value()
                  .structures.front()
                  .topology->atoms()[0]
                  .atomic_number == 0U &&
          reduced_g96.value().structures.front().coordinates->frame_count() ==
              1U,
      "standalone POSITIONRED must load with explicit unknown synthetic "
      "identity");
  const auto abutting_box_g96 = io::read_structure(
      "TITLE\nabutting box\nEND\nPOSITIONRED\n"
      "    0.100000000    0.200000000    0.300000000\nEND\nBOX\n"
      "12345.12345678912346.12345678912347.123456789\nEND\n",
      {io::StructureFormat::g96, "abutting-box.g96"});
  std::shared_ptr<const model::CoordinateFrame> abutting_box_frame;
  if (abutting_box_g96.has_value()) {
    const auto loaded =
        abutting_box_g96.value().structures.front().coordinates->read_frame(0U);
    if (loaded.has_value())
      abutting_box_frame = loaded.value();
  }
  passed &= expect(
      abutting_box_frame != nullptr &&
          abutting_box_frame->metadata().unit_cell.has_value() &&
          abutting_box_frame->metadata().unit_cell->a.x == 12345.123456789,
      "G96 BOX parser must honor fixed F15.9 boundaries without separators");
  auto changed_g96 = g96_text;
  const auto last_hw1 = changed_g96.rfind("HW1");
  if (last_hw1 != std::string::npos)
    changed_g96.replace(last_hw1, 3U, "HW2");
  const auto changed_g96_result = io::read_structure(
      changed_g96, {io::StructureFormat::g96, "changed.g96"});
  passed &=
      expect(!changed_g96_result.has_value() &&
                 changed_g96_result.error().message.find(
                     "differs from the first") != std::string::npos,
             "G96 concatenated frames must not silently mutate atom identity");
  const auto missing_end_g96 =
      io::read_structure("TITLE\nmissing end\nEND\nPOSITIONRED\n"
                         "    0.100000000    0.200000000    0.300000000\n",
                         {io::StructureFormat::g96, "missing-end.g96"});
  passed &= expect(!missing_end_g96.has_value() &&
                       missing_end_g96.error().message.find("missing END") !=
                           std::string::npos,
                   "G96 block truncation must fail with block provenance");

  const auto psf_text = read_text(argv[9]);
  const auto psf = io::read_structure(
      psf_text, {io::StructureFormat::auto_detect, "topology.psf"});
  passed &= expect(
      psf.has_value() && psf.value().format == io::StructureFormat::psf &&
          psf.value().structures.size() == 1U &&
          psf.value().structures.front().coordinates->frame_count() == 0U,
      "PSF must auto-detect as a topology-only structure with zero frames");
  if (psf.has_value()) {
    const auto &topology = *psf.value().structures.front().topology;
    const auto *atom_types = topology.properties().find("psf.atom_type");
    const auto *charges = topology.properties().find("partial_charge");
    const auto *masses = topology.properties().find("mass");
    passed &= expect(
        topology.atom_count() == 4U && topology.residue_count() == 2U &&
            topology.bonds().size() == 3U && topology.angles().size() == 2U &&
            topology.dihedrals().size() == 1U &&
            topology.impropers().size() == 1U &&
            topology.cmap_terms().size() == 1U &&
            topology.residues()[1].sequence_number == 2 &&
            topology.residues()[1].insertion_code == "A" &&
            topology.residues()[1].segment_id == "PROT" &&
            topology.atoms()[0].atomic_number == 7U && atom_types != nullptr &&
            charges != nullptr && masses != nullptr &&
            std::get<std::vector<std::string>>(atom_types->values)[1] ==
                "CT1" &&
            std::get<std::vector<double>>(charges->values)[2] == 0.5 &&
            std::get<std::vector<double>>(masses->values)[0] == 14.007 &&
            topology.source_metadata().at("psf.unmodeled.NGRP") == "1",
        "PSF atom identity, force-field properties and ordered connectivity "
        "must load");
  }
  const auto bad_psf_reference =
      io::read_structure("PSF NAMD\n\n1 !NTITLE\nREMARKS bad\n1 !NATOM\n"
                         "1 SEG 1 RES C CT 0.0 12.011 0\n1 !NBOND\n1 2\n",
                         {io::StructureFormat::psf, "bad-reference.psf"});
  passed &=
      expect(!bad_psf_reference.has_value() &&
                 bad_psf_reference.error().message.find("unknown atom 2") !=
                     std::string::npos,
             "PSF connectivity must reject unknown source atom identifiers");
  const auto malformed_cmap = io::read_structure(
      "PSF EXT CMAP\n\n1 !NTITLE\nREMARKS cmap\n1 !NATOM\n"
      "1 SEG 1 RES C CT 0.0 12.011 0\n1 !NCRTERM\n1 1 1 1 1 1 1\n",
      {io::StructureFormat::psf, "cmap.psf"});
  passed &= expect(
      !malformed_cmap.has_value() &&
          malformed_cmap.error().message.find("contains 7 integer fields") !=
              std::string::npos,
      "PSF CMAP terms must require exactly two source-order dihedrals");
  const auto duplicate_psf_term = io::read_structure(
      "PSF NAMD\n\n1 !NTITLE\nREMARKS multiplicity\n4 !NATOM\n"
      "1 SEG 1 RES A CT 0 12.011 0\n2 SEG 1 RES B CT 0 12.011 0\n"
      "3 SEG 1 RES C CT 0 12.011 0\n4 SEG 1 RES D CT 0 12.011 0\n"
      "2 !NPHI\n1 2 3 4 1 2 3 4\n",
      {io::StructureFormat::psf, "multiplicity.psf"});
  passed &= expect(
      duplicate_psf_term.has_value() && duplicate_psf_term.value()
                                                .structures.front()
                                                .topology->dihedrals()
                                                .size() == 2U,
      "X-PLOR PSF duplicate dihedrals must preserve parameter multiplicity");
  const auto drude_fields = io::read_structure(
      "PSF EXT\n\n1 !NTITLE\nREMARKS extra atom fields\n1 !NATOM\n"
      "1 SEG 1 RES D DRUD 0 0.4 0 1.0 2.0\n",
      {io::StructureFormat::psf, "drude-fields.psf"});
  passed &= expect(
      !drude_fields.has_value() &&
          drude_fields.error().message.find("requires") != std::string::npos,
      "unmodeled PSF Drude/CHEQ atom fields must not be silently ignored");

  const auto prmtop_text = read_text(argv[10]);
  const auto prmtop = io::read_structure(
      prmtop_text, {io::StructureFormat::auto_detect, "topology.prmtop"});
  passed &= expect(
      prmtop.has_value() &&
          prmtop.value().format == io::StructureFormat::prmtop &&
          prmtop.value().structures.size() == 1U &&
          prmtop.value().structures.front().coordinates->frame_count() == 0U,
      "PRMTOP must auto-detect as a topology-only zero-frame structure");
  if (prmtop.has_value()) {
    const auto &topology = *prmtop.value().structures.front().topology;
    const auto *types = topology.properties().find("amber.atom_type");
    const auto *charges = topology.properties().find("partial_charge");
    const auto *radii = topology.properties().find("amber.gb_radius");
    passed &= expect(
        topology.atom_count() == 4U && topology.residue_count() == 2U &&
            topology.bonds().size() == 3U && topology.angles().size() == 2U &&
            topology.dihedrals().size() == 1U &&
            topology.impropers().size() == 1U &&
            topology.atoms()[0].atomic_number == 7U &&
            topology.residues()[1].name == "GLY" && types != nullptr &&
            charges != nullptr && radii != nullptr &&
            std::get<std::vector<std::string>>(types->values)[1] == "CT" &&
            std::abs(std::get<std::vector<double>>(charges->values)[1] - 1.0) <
                1.0e-12 &&
            std::get<std::vector<double>>(radii->values)[3] == 1.5 &&
            topology.source_metadata().at("amber.box_angle_degrees") ==
                "90.000000" &&
            topology.source_metadata().at("amber.comment.CHARGE") ==
                "Amber charge in internal electrostatic units\nconverted "
                "with the historical scale",
        "PRMTOP identity, Amber charge scale, box template, GB properties and "
        "signed torsions and section comments must load");
  }
  const auto misplaced_prmtop_comment = io::read_structure(
      "%VERSION VERSION_STAMP = V0001.000\n%COMMENT no section\n",
      {io::StructureFormat::prmtop, "misplaced-comment.prmtop"});
  passed &= expect(
      !misplaced_prmtop_comment.has_value() &&
          misplaced_prmtop_comment.error().message.find(
              "unexpected Amber %COMMENT") != std::string::npos,
      "PRMTOP comments outside the FLAG-to-FORMAT interval must fail");
  auto perturbed_prmtop = prmtop_text;
  const auto pointer_row =
      perturbed_prmtop.find("       0       0       0       0       0       0  "
                            "     0       1       2       0");
  if (pointer_row != std::string::npos) {
    perturbed_prmtop.replace(pointer_row, 8U, "       1");
  }
  const auto perturbed = io::read_structure(
      perturbed_prmtop, {io::StructureFormat::prmtop, "perturbed.prmtop"});
  passed &= expect(
      !perturbed.has_value() &&
          perturbed.error().code == ErrorCode::unsupported,
      "PRMTOP perturbation data must fail instead of being silently flattened");
  auto bad_prmtop_count = prmtop_text;
  const auto bond_pointer =
      bad_prmtop_count.find("       4       2       1       2       1       1");
  if (bond_pointer != std::string::npos) {
    bad_prmtop_count.replace(bond_pointer + 24U, 8U, "       3");
  }
  const auto bad_prmtop = io::read_structure(
      bad_prmtop_count, {io::StructureFormat::prmtop, "bad-count.prmtop"});
  passed &= expect(!bad_prmtop.has_value() &&
                       bad_prmtop.error().message.find("POINTERS declares") !=
                           std::string::npos,
                   "PRMTOP connectivity must match its POINTERS record counts");
  auto unsupported_mol2 = mol2_text;
  const auto amide_type = unsupported_mol2.find("2 1 3 am AMIDE");
  unsupported_mol2.replace(amide_type, std::string{"2 1 3 am AMIDE"}.size(),
                           "2 1 3 du AMIDE");
  const auto dummy_bond = io::read_structure(
      unsupported_mol2, {io::StructureFormat::mol2, "dummy.mol2"});
  passed &=
      expect(!dummy_bond.has_value() &&
                 dummy_bond.error().code == ErrorCode::unsupported &&
                 dummy_bond.error().message.find("dummy") != std::string::npos,
             "MOL2 dummy/query bonds must not become covalent bonds");
  auto bad_mol2_count = mol2_text;
  const auto first_count = bad_mol2_count.find("4 4 1 0 0");
  bad_mol2_count.replace(first_count, std::string{"4 4 1 0 0"}.size(),
                         "999999999 4 1 0 0");
  const auto bad_count = io::read_structure(
      bad_mol2_count, {io::StructureFormat::mol2, "count.mol2"});
  passed &= expect(!bad_count.has_value() && bad_count.error().message.find(
                                                 "counts") != std::string::npos,
                   "MOL2 declared counts must fail before large allocation");

  const auto sdf = io::read_structure_file(argv[5]);
  passed &= expect(sdf.has_value() &&
                       sdf.value().format == io::StructureFormat::sdf &&
                       sdf.value().structures.size() == 2U,
                   "SDF extension must load every V2000 record in order");
  const auto sdf_auto = io::read_structure(
      read_text(argv[5]), {io::StructureFormat::auto_detect, "chemistry.sdf"});
  passed &=
      expect(sdf_auto.has_value() &&
                 sdf_auto.value().format == io::StructureFormat::sdf &&
                 sdf_auto.value().structures.size() == 2U,
             "SDF V2000 counts line/delimiter must support content sniffing");
  if (sdf.has_value()) {
    const auto &first = sdf.value().structures[0];
    const auto &second = sdf.value().structures[1];
    const auto *isotope = first.topology->properties().find("sdf.isotope_mass");
    const auto *radical = first.topology->properties().find("sdf.radical");
    passed &= expect(
        first.name == "Charged aromatic" && second.name == "Nitrile" &&
            first.topology->atom_count() == 4U &&
            first.topology->bonds().size() == 3U &&
            first.topology->bonds()[0].order == model::BondOrder::double_bond &&
            first.topology->bonds()[2].order == model::BondOrder::aromatic &&
            second.topology->bonds().front().order == model::BondOrder::triple,
        "SDF atom counts and single/double/triple/aromatic bond order must "
        "load");
    passed &= expect(
        first.topology->atoms()[0].formal_charge == 1 &&
            first.topology->atoms()[1].formal_charge == -1 &&
            isotope != nullptr && radical != nullptr &&
            std::get<std::vector<std::int64_t>>(isotope->values)[2] == 13 &&
            std::get<std::vector<std::int64_t>>(radical->values)[3] == 2,
        "SDF M CHG/ISO/RAD chemistry properties must load");
    passed &= expect(
        first.metadata.at("sdf.data.ID") == "first-001" &&
            first.metadata.at("sdf.data.NOTE") == "line one\nline two" &&
            second.metadata.at("sdf.data.ID") == "second-002" &&
            first.topology->source_metadata().at("format_version") == "V2000",
        "SDF multiline data fields and format provenance must load");
  }

  const auto protein = io::read_structure_file(argv[3]);
  passed &= expect(protein.has_value() &&
                       protein.value().format == io::StructureFormat::pdb &&
                       protein.value().structures.size() == 1U,
                   "RCSB 1UBQ protein fixture must load as one PDB structure");
  if (protein.has_value()) {
    const auto &structure = protein.value().structures.front();
    const auto frame = structure.coordinates->read_frame(0U);
    passed &= expect(
        structure.name == "1UBQ" && structure.topology->atom_count() == 660U &&
            structure.topology->residue_count() == 134U &&
            structure.topology->residues().front().name == "MET" &&
            structure.topology->residues().front().chain_id == "A" &&
            structure.topology->residues().back().name == "HOH" &&
            frame.has_value() && frame.value()->atom_count() == 660U,
        "1UBQ protein, chain, solvent and coordinate identity must be retained");
  }

  const auto pqr = io::read_structure_file(argv[4]);
  passed &= expect(pqr.has_value() &&
                       pqr.value().format == io::StructureFormat::pqr &&
                       pqr.value().structures.size() == 1U,
                   "PQR extension must select the native reader");
  if (pqr.has_value()) {
    const auto &structure = pqr.value().structures.front();
    const auto *charge =
        structure.topology->properties().find("partial_charge");
    const auto *radius = structure.topology->properties().find("pqr.radius");
    const auto *hetero = structure.topology->properties().find("pqr.is_hetero");
    passed &= expect(structure.topology->atom_count() == 3U &&
                         structure.topology->residue_count() == 2U &&
                         structure.topology->atoms()[0].atomic_number == 7U &&
                         structure.topology->atoms()[1].atomic_number == 6U &&
                         structure.topology->atoms()[2].atomic_number == 17U &&
                         structure.topology->residues()[0].chain_id == "A" &&
                         structure.topology->residues()[1].chain_id.empty(),
                     "PQR identity, optional chain and conservative element "
                     "inference must load");
    passed &= expect(
        charge != nullptr && radius != nullptr && hetero != nullptr &&
            charge->metadata.unit == "elementary_charge" &&
            radius->metadata.unit == "angstrom" &&
            std::get<std::vector<double>>(charge->values) ==
                std::vector<double>{-0.3, 0.1, -1.0} &&
            std::get<std::vector<double>>(radius->values) ==
                std::vector<double>{1.55, 1.7, 1.8} &&
            std::get<BooleanColumn>(hetero->values).values ==
                std::vector<std::uint8_t>{0U, 0U, 1U},
        "PQR charge/radius units and record origin must be typed properties");
    const auto frame = structure.coordinates->read_frame(0U);
    passed &=
        expect(frame.has_value() &&
                   frame.value()->metadata().coordinate_unit ==
                       operation::LengthUnit::angstrom &&
                   frame.value()->metadata().unit_cell.has_value() &&
                   frame.value()->metadata().unit_cell->signed_volume() >
                       1000.0 &&
                   positions(*frame.value())[2] == model::Vec3d{3.0, 4.0, 5.0},
               "PQR coordinates and CRYST1 cell must remain float64 angstrom");
  }

  const auto pdb_text = read_text(argv[1]);
  const auto pdb = io::read_structure(
      pdb_text, io::StructureReadOptions{io::StructureFormat::auto_detect,
                                         "synthetic.pdb"});
  passed &= expect(pdb.has_value(), "valid PDB fixture must parse");
  if (pdb.has_value()) {
    passed &= expect(pdb.value().format == io::StructureFormat::pdb &&
                         pdb.value().structures.size() == 1,
                     "PDB auto-detection must produce one structure");
    const auto &structure = pdb.value().structures.front();
    passed &=
        expect(structure.name == "MS01" &&
                   structure.metadata.at("title") ==
                       "SYNTHETIC MULTI MODEL WITH TRICLINIC CELL" &&
                   structure.metadata.at("deposition_date") == "13-AUG-26" &&
                   structure.metadata.at("space_group") == "P 1",
               "PDB entry and crystallographic metadata must be retained");
    passed &= expect(
        structure.metadata.at("molecule_remarks") ==
            "REMARK 200 SYNTHETIC EXPERIMENTAL DETAIL\n"
            "SEQRES   1 A    1  GLY\nCONECT    1    2\n" &&
            structure.topology->source_metadata().at(
                "pdb.molecule_remarks") ==
                structure.metadata.at("molecule_remarks"),
        "PDB REMARK, unused and CONECT records must remain available as "
        "molecule metadata");
    passed &= expect(structure.topology->atom_count() == 3 &&
                         structure.topology->residue_count() == 2 &&
                         structure.topology->bonds().size() == 1,
                     "PDB topology, residues and CONECT bond must load");
    const auto &first_atom = structure.topology->atoms().front();
    passed &= expect(first_atom.name == "N" && first_atom.atomic_number == 7 &&
                         first_atom.formal_charge == 1 &&
                         first_atom.alternate_location == "A" &&
                         first_atom.source_serial == 1,
                     "PDB atom identity, element, charge and altloc must load");
    passed &= expect(
        structure.topology->residues().front().chain_id == "A" &&
            structure.topology->residues().front().sequence_number == 42 &&
            structure.topology->residues().front().insertion_code == "B" &&
            structure.topology->residues().front().segment_id == "SEG1",
        "PDB residue identity and segment must load");
    const auto *hetero = structure.topology->properties().find("pdb.is_hetero");
    passed &= expect(hetero != nullptr &&
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
      passed &= expect(
          first_frame.value()->metadata().source_step == 1 &&
              second_frame.value()->metadata().source_step == 2 &&
              first_frame.value()->metadata().unit_cell.has_value() &&
              first_frame.value()->metadata().unit_cell->signed_volume() >
                  1000.0,
          "PDB model number and triclinic cell must be retained");
      passed &= expect(positions(*first_frame.value())[0] ==
                               model::Vec3d{1.0, 2.0, 3.0} &&
                           positions(*second_frame.value())[1] ==
                               model::Vec3d{2.5, 3.5, 4.5},
                       "PDB coordinates must retain numeric values");
      passed &=
          expect(second_frame.value()->presence() ==
                     std::vector<std::uint8_t>{1, 1, 0},
                 "missing atom in a later PDB model must use presence mask");
      const auto &occupancy =
          first_frame.value()->metadata().atom_properties.at("occupancy");
      const auto &occupancy_present =
          first_frame.value()->metadata().atom_properties.at(
              "occupancy_present");
      passed &= expect(
          std::get<std::vector<double>>(occupancy.values)[0] == 0.5 &&
              std::get<BooleanColumn>(occupancy_present.values).values[2] == 0,
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
  const auto short_pdb =
      io::read_structure("ATOM\n", {io::StructureFormat::pdb, "bad.pdb"});
  passed &= expect(!short_pdb.has_value() &&
                       short_pdb.error().message.starts_with("bad.pdb:1:"),
                   "malformed PDB error must include source and line");
  auto unknown_connection = pdb_text;
  unknown_connection.replace(unknown_connection.find("CONECT    1    2"), 16,
                             "CONECT    1    9");
  const auto bad_connection = io::read_structure(
      unknown_connection, {io::StructureFormat::pdb, "conect.pdb"});
  passed &= expect(
      !bad_connection.has_value() &&
          bad_connection.error().message.starts_with("conect.pdb:15:") &&
          bad_connection.error().message.find("CONECT") != std::string::npos,
      "PDB CONECT must not silently reference an unknown atom");
  auto inferred_element = pdb_text;
  const auto element_position = inferred_element.find("SEG1 N1+") + 5;
  inferred_element[element_position] = ' ';
  const auto inferred = io::read_structure(
      inferred_element, {io::StructureFormat::pdb, "inferred.pdb"});
  passed &=
      expect(inferred.has_value() && inferred.value()
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
    const auto &structure = cif.value().structures.front();
    passed &= expect(
        structure.name == "MS02" &&
            structure.metadata.at("_struct.title") ==
                "Synthetic structure\nwith multiline metadata" &&
            structure.metadata.at("_audit.creation_method") ==
                "MolShredder # synthetic fixture",
        "mmCIF scalar, quote, comment and text field semantics must load");
    passed &=
        expect(structure.topology->atom_count() == 3 &&
                   structure.topology->residue_count() == 2 &&
                   structure.topology->bonds().size() == 2 &&
                   structure.topology->bonds().front().order ==
                       model::BondOrder::single,
               "mmCIF label/auth atom_site and struct_conn topology must load");
    passed &=
        expect(structure.topology->atoms().front().name == "N" &&
                   structure.topology->atoms().front().formal_charge == 1 &&
                   structure.topology->atoms().front().source_serial == 1,
               "mmCIF author atom identity, charge and source id must load");
    const auto *label_asym =
        structure.topology->properties().find("mmcif.label_asym_id");
    const auto *charge_present =
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
      passed &=
          expect(first_frame.value()->metadata().source_step == 1 &&
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
  passed &= expect(
      two_blocks.has_value() && two_blocks.value().structures.size() == 2 &&
          two_blocks.value().structures[0].name == "one" &&
          two_blocks.value().structures[1].name == "two",
      "independent mmCIF data blocks must become structures in order");

  const auto long_identity = io::read_structure(
      "data_long\nloop_\n_atom_site.group_PDB\n_atom_site.id\n"
      "_atom_site.type_symbol\n_atom_site.label_atom_id\n"
      "_atom_site.label_alt_id\n_atom_site.label_comp_id\n"
      "_atom_site.label_asym_id\n_atom_site.label_entity_id\n"
      "_atom_site.label_seq_id\n_atom_site.pdbx_PDB_ins_code\n"
      "_atom_site.Cartn_x\n_atom_site.Cartn_y\n_atom_site.Cartn_z\n"
      "_atom_site.auth_seq_id\n_atom_site.auth_comp_id\n"
      "_atom_site.auth_asym_id\n_atom_site.auth_atom_id\n"
      "ATOM 1 C 'C alpha' . GLY LABEL_LONG 1 1 ? 0 0 0 10 GLY "
      "AUTH_LONG 'C alpha'\n",
      {io::StructureFormat::mmcif, "long-identity.cif"});
  passed &= expect(
      long_identity.has_value() &&
          long_identity.value().structures.front().topology->atoms().front()
                  .name == "C alpha" &&
          long_identity.value().structures.front().topology->residues().front()
                  .chain_id == "AUTH_LONG" &&
          std::get<std::vector<std::string>>(
              long_identity.value()
                  .structures.front()
                  .topology->properties()
                  .find("mmcif.label_asym_id")
                  ->values)[0] == "LABEL_LONG",
      "native mmCIF must preserve quoted atom names and chain identifiers "
      "beyond the molfile ABI 18 width");

  const auto malformed_loop = io::read_structure(
      "data_bad\nloop_\n_atom_site.id\n_atom_site.type_symbol\n1\n",
      {io::StructureFormat::mmcif, "bad.cif"});
  passed &= expect(!malformed_loop.has_value() &&
                       malformed_loop.error().message.starts_with("bad.cif:2:"),
                   "non-rectangular mmCIF loop must report its source line");
  const auto unterminated_quote =
      io::read_structure("data_bad\n_entry.id 'never closes\n",
                         {io::StructureFormat::mmcif, "quote.cif"});
  passed &=
      expect(!unterminated_quote.has_value() &&
                 unterminated_quote.error().message.starts_with("quote.cif:2:"),
             "unterminated mmCIF quote must report its source line");
  auto unknown_element = cif_text;
  unknown_element.replace(unknown_element.find("ATOM   1 N N"),
                          std::string{"ATOM   1 N N"}.size(), "ATOM   1 Xx N");
  const auto bad_element = io::read_structure(
      unknown_element, {io::StructureFormat::mmcif, "element.cif"});
  passed &= expect(
      !bad_element.has_value() && bad_element.error().message.find(
                                      "element symbol") != std::string::npos,
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

  const std::string xyz_text =
      "3\nfirst frame\nC 0 0 0\nH 1 0 0\nX 0 1 0\n"
      "3\nsecond frame\nC 0.5 0 0\nH 1.5 0 0\nX 0 1.5 0\n";
  const auto xyz = io::read_structure(
      xyz_text, {io::StructureFormat::auto_detect, "motion.xyz"});
  passed &= expect(
      xyz.has_value() && xyz.value().format == io::StructureFormat::xyz &&
          xyz.value().structures.size() == 1U &&
          xyz.value().structures.front().name == "motion" &&
          xyz.value().structures.front().topology->atom_count() == 3U &&
          xyz.value().structures.front().topology->atoms()[2].atomic_number ==
              0U &&
          xyz.value().structures.front().coordinates->frame_count() == 2U,
      "plain multi-frame XYZ must auto-detect and preserve stable element "
      "order");
  if (xyz.has_value()) {
    const auto xyz_second =
        xyz.value().structures.front().coordinates->read_frame(1U);
    passed &= expect(
        xyz_second.has_value() &&
            positions(*xyz_second.value())[0] == model::Vec3d{0.5, 0.0, 0.0} &&
            xyz_second.value()->metadata().source_step == 1U &&
            xyz_second.value()->metadata().fields.at("xyz.comment") ==
                "second frame",
        "XYZ frames must preserve float64 coordinates, zero-based step and "
        "comment");
  }
  const auto changed_xyz =
      io::read_structure("1\none\nC 0 0 0\n1\ntwo\nN 0 0 0\n",
                         {io::StructureFormat::xyz, "changed.xyz"});
  passed &= expect(!changed_xyz.has_value() &&
                       changed_xyz.error().message.find(
                           "atom count/order/elements") != std::string::npos,
                   "XYZ later frames must not silently mutate topology");
  const auto numbered_xyz = io::read_structure(
      "3\nperiodic-table labels\n6 0 0 0\n8 1 0 0\n0 0 1 0\n",
      {io::StructureFormat::xyz, "numbered.xmol"});
  passed &= expect(
      numbered_xyz.has_value() &&
          numbered_xyz.value().structures.front().topology->atoms()[0].name ==
              "C" &&
          numbered_xyz.value()
                  .structures.front()
                  .topology->atoms()[1]
                  .atomic_number == 8U &&
          numbered_xyz.value()
                  .structures.front()
                  .topology->atoms()[2]
                  .atomic_number == 0U,
      "XYZ must canonicalize periodic-table ordinal labels and numeric zero");
  const auto invalid_ordinal = io::read_structure(
      "1\ninvalid ordinal\n119 0 0 0\n",
      {io::StructureFormat::xyz, "ordinal.xyz"});
  passed &= expect(
      !invalid_ordinal.has_value() &&
          invalid_ordinal.error().message.find("between 0 and 118") !=
              std::string::npos,
      "XYZ must reject atomic-number labels outside the periodic table");
  const auto extended_xyz = io::read_structure(
      "1\nextended\nC 0 0 0 1.0\n", {io::StructureFormat::xyz, "extended.xyz"});
  passed &=
      expect(!extended_xyz.has_value() &&
                 extended_xyz.error().message.starts_with("extended.xyz:3:"),
             "unsupported extended XYZ columns must fail with a source line");
  const auto truncated_xyz =
      io::read_structure("1000000000\ntruncated\nC 0 0 0\n",
                         {io::StructureFormat::xyz, "truncated.xyz"});
  passed &=
      expect(!truncated_xyz.has_value() &&
                 truncated_xyz.error().message.find("ended before") !=
                     std::string::npos,
             "malicious XYZ atom count must fail without count-sized reserve");

  const auto bad_pqr_radius =
      io::read_structure("ATOM 1 C MOL 1 0 0 0 0.0 -1.0\n",
                         {io::StructureFormat::pqr, "radius.pqr"});
  passed &= expect(
      !bad_pqr_radius.has_value() &&
          bad_pqr_radius.error().message.starts_with("radius.pqr:1:") &&
          bad_pqr_radius.error().message.find("positive") != std::string::npos,
      "PQR non-positive radius must fail with a source line");
  const auto bad_pqr_columns =
      io::read_structure("ATOM 1 C MOL A 1 0 0 0 0.0\n",
                         {io::StructureFormat::pqr, "columns.pqr"});
  passed &=
      expect(!bad_pqr_columns.has_value() &&
                 bad_pqr_columns.error().message.starts_with("columns.pqr:1:"),
             "truncated PQR atom record must fail with a source line");
  const auto multimodel_pqr =
      io::read_structure("MODEL 1\nATOM 1 C MOL 1 0 0 0 0.0 1.7\nENDMDL\n",
                         {io::StructureFormat::pqr, "models.pqr"});
  passed &=
      expect(!multimodel_pqr.has_value() &&
                 multimodel_pqr.error().code == ErrorCode::invalid_argument,
             "multi-model PQR must not be silently flattened");
  const auto duplicate_pqr_cell = io::read_structure(
      "CRYST1 10 10 10 90 90 90\nCRYST1 10 10 10 90 90 90\n"
      "ATOM 1 C MOL 1 0 0 0 0.0 1.7\n",
      {io::StructureFormat::pqr, "duplicate-cell.pqr"});
  passed &= expect(!duplicate_pqr_cell.has_value() &&
                       duplicate_pqr_cell.error().message.find(
                           "duplicate PQR CRYST1") != std::string::npos,
                   "duplicate PQR CRYST1 must fail explicitly");
  const auto invalid_pqr_cell = io::read_structure(
      "CRYST1 10 10 10 90 90 0\nATOM 1 C MOL 1 0 0 0 0.0 1.7\n",
      {io::StructureFormat::pqr, "invalid-cell.pqr"});
  passed &= expect(!invalid_pqr_cell.has_value() &&
                       invalid_pqr_cell.error().message.find("degenerate") !=
                           std::string::npos,
                   "degenerate PQR CRYST1 must fail explicitly");

  const auto truncated_mol = io::read_structure(
      "bad\nprogram\ncomment\n  2  0  0  0  0  0  0  0  0  0999 V2000\n",
      {io::StructureFormat::mol, "truncated.mol"});
  passed &= expect(!truncated_mol.has_value() &&
                       truncated_mol.error().message.find("available lines") !=
                           std::string::npos,
                   "MOL counts exceeding available lines must fail explicitly");
  const auto query_bond = io::read_structure(
      "query\nprogram\ncomment\n  2  1  0  0  0  0  0  0  0  0999 V2000\n"
      "    0.0000    0.0000    0.0000 C   0  0  0\n"
      "    1.0000    0.0000    0.0000 N   0  0  0\n"
      "  1  2  5  0\nM  END\n",
      {io::StructureFormat::mol, "query.mol"});
  passed &= expect(!query_bond.has_value() && query_bond.error().message.find(
                                                  "query") != std::string::npos,
                   "unsupported MOL query bonds must not become unknown bonds");
  const auto stereo_bond = io::read_structure(
      "stereo\nprogram\ncomment\n  2  1  0  0  0  0  0  0  0  0999 V2000\n"
      "    0.0000    0.0000    0.0000 C   0  0  0\n"
      "    1.0000    0.0000    0.0000 N   0  0  0\n"
      "  1  2  1  1\nM  END\n",
      {io::StructureFormat::mol, "stereo.mol"});
  passed &= expect(!stereo_bond.has_value() &&
                       stereo_bond.error().message.find("stereo") !=
                           std::string::npos,
                   "bond stereo must not be silently discarded");

  return passed ? 0 : 1;
}
