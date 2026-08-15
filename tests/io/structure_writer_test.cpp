#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "molshredder/io/structure_reader.hpp"
#include "molshredder/io/structure_writer.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/operation/task_context.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

std::string read_text(const std::filesystem::path &path) {
  std::ifstream stream{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream},
          std::istreambuf_iterator<char>{}};
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  if (argc != 10) {
    std::cerr
        << "expected PDB, mmCIF, PQR, SDF, MOL2, GRO, G96, PSF fixture and "
           "temporary directory\n";
    return 2;
  }
  bool passed = true;
  const auto pdb = io::read_structure_file(argv[1]);
  passed &= expect(pdb.has_value(), "writer input PDB must load");
  if (!pdb.has_value())
    return 1;
  const auto &structure = pdb.value().structures.front();
  const auto mmcif = io::read_structure_file(argv[2]);
  passed &= expect(mmcif.has_value(), "writer input mmCIF must load");
  if (!mmcif.has_value())
    return 1;
  const auto &mmcif_structure = mmcif.value().structures.front();
  const auto pqr = io::read_structure_file(argv[3]);
  passed &= expect(pqr.has_value(), "writer input PQR must load");
  if (!pqr.has_value())
    return 1;
  const auto &pqr_structure = pqr.value().structures.front();
  const auto sdf = io::read_structure_file(argv[4]);
  passed &= expect(sdf.has_value() && sdf.value().structures.size() == 2U,
                   "writer input SDF must load both records");
  if (!sdf.has_value())
    return 1;
  const auto &sdf_structure = sdf.value().structures.front();
  const auto mol2 = io::read_structure_file(argv[5]);
  passed &= expect(mol2.has_value() && mol2.value().structures.size() == 2U,
                   "writer input MOL2 must load both molecules");
  if (!mol2.has_value())
    return 1;
  const auto &mol2_structure = mol2.value().structures.front();
  const auto &benzene_mol2_structure = mol2.value().structures[1];
  const auto gro = io::read_structure_file(argv[6]);
  passed &= expect(gro.has_value(), "writer input GRO must load");
  if (!gro.has_value())
    return 1;
  const auto &gro_structure = gro.value().structures.front();
  const auto g96 = io::read_structure_file(argv[7]);
  passed &= expect(g96.has_value(), "writer input G96 must load");
  if (!g96.has_value())
    return 1;
  const auto &g96_structure = g96.value().structures.front();
  const auto psf = io::read_structure_file(argv[8]);
  passed &= expect(psf.has_value(), "writer input PSF must load");
  if (!psf.has_value())
    return 1;
  const auto &psf_structure = psf.value().structures.front();

  operation::TaskContext memory_context;
  std::size_t progress_count{};
  memory_context.report_progress =
      [&progress_count](const operation::ProgressUpdate &) {
        ++progress_count;
      };
  io::StructureWriteOptions current_options;
  current_options.format = io::StructureFormat::xyz;
  current_options.frame_indices = {0U};
  current_options.decimal_places = 3U;
  const auto serialized =
      io::serialize_structure(*structure.topology, *structure.coordinates,
                              current_options, memory_context);
  passed &= expect(
      serialized.has_value() && serialized.value().report.frame_count == 1U &&
          serialized.value().report.atom_count == 3U &&
          serialized.value().report.byte_count ==
              serialized.value().content.size() &&
          !serialized.value().report.losses.empty() && progress_count == 1U &&
          serialized.value().content.find("N 1.000 2.000 3.000") !=
              std::string::npos,
      "XYZ serialization must report size/loss/progress and requested "
      "precision");
  if (serialized.has_value()) {
    const auto roundtrip =
        io::read_structure(serialized.value().content,
                           {io::StructureFormat::xyz, "roundtrip.xyz"});
    passed &= expect(
        roundtrip.has_value() &&
            roundtrip.value().structures.front().topology->atom_count() == 3U,
        "serialized XYZ must be readable by the native reader");
  }

  operation::TaskContext pdb_context;
  std::size_t pdb_progress_count{};
  pdb_context.report_progress =
      [&pdb_progress_count](const operation::ProgressUpdate &) {
        ++pdb_progress_count;
      };
  io::StructureWriteOptions pdb_options;
  pdb_options.format = io::StructureFormat::pdb;
  pdb_options.decimal_places = 3U;
  pdb_options.comment = "native PDB round trip";
  const auto serialized_pdb = io::serialize_structure(
      *structure.topology, *structure.coordinates, pdb_options, pdb_context);
  passed &= expect(
      serialized_pdb.has_value() &&
          serialized_pdb.value().report.format == io::StructureFormat::pdb &&
          serialized_pdb.value().report.frame_count == 2U &&
          serialized_pdb.value().report.atom_count == 3U &&
          serialized_pdb.value().report.byte_count ==
              serialized_pdb.value().content.size() &&
          pdb_progress_count == 2U &&
          serialized_pdb.value().content.find(
              "REMARK 999 native PDB round trip") != std::string::npos &&
          serialized_pdb.value().content.find(
              "CRYST1   10.000   11.000   12.000") != std::string::npos &&
          serialized_pdb.value().content.find("MODEL        1") !=
              std::string::npos &&
          serialized_pdb.value().content.find("HETATM    3") !=
              std::string::npos &&
          serialized_pdb.value().content.find("CONECT    1    2") !=
              std::string::npos &&
          serialized_pdb.value().content.ends_with("END\n"),
      "PDB writer must emit fixed-column multi-model coordinates, cell and "
      "connectivity");
  if (serialized_pdb.has_value()) {
    const auto roundtrip =
        io::read_structure(serialized_pdb.value().content,
                           {io::StructureFormat::pdb, "roundtrip.pdb"});
    passed &= expect(
        roundtrip.has_value() &&
            roundtrip.value().structures.front().topology->atom_count() == 3U &&
            roundtrip.value().structures.front().topology->bonds().size() ==
                1U &&
            roundtrip.value().structures.front().coordinates->frame_count() ==
                2U &&
            roundtrip.value()
                    .structures.front()
                    .topology->atoms()
                    .front()
                    .formal_charge == 1 &&
            roundtrip.value()
                    .structures.front()
                    .topology->atoms()
                    .front()
                    .alternate_location == "A" &&
            roundtrip.value()
                    .structures.front()
                    .topology->residues()
                    .front()
                    .segment_id == "SEG1",
        "PDB read-write-read must preserve topology identity and model count");
    if (roundtrip.has_value()) {
      const auto second =
          roundtrip.value().structures.front().coordinates->read_frame(1U);
      passed &= expect(
          second.has_value() &&
              second.value()->presence() ==
                  std::vector<std::uint8_t>{1U, 1U, 0U} &&
              second.value()->metadata().unit_cell.has_value(),
          "PDB round-trip must preserve later-model presence and unit cell");
    }
  }
  pdb_options.frame_indices = {1U};
  const auto incomplete_first_model = io::serialize_structure(
      *structure.topology, *structure.coordinates, pdb_options, pdb_context);
  passed &= expect(!incomplete_first_model.has_value() &&
                       incomplete_first_model.error().message.find(
                           "first output model") != std::string::npos,
                   "PDB writer must reject a first model that cannot define "
                   "the full topology");

  operation::TaskContext mmcif_context;
  std::size_t mmcif_progress_count{};
  mmcif_context.report_progress =
      [&mmcif_progress_count](const operation::ProgressUpdate &) {
        ++mmcif_progress_count;
      };
  io::StructureWriteOptions mmcif_options;
  mmcif_options.format = io::StructureFormat::mmcif;
  mmcif_options.decimal_places = 4U;
  mmcif_options.comment = "native mmCIF round trip";
  const auto serialized_mmcif = io::serialize_structure(
      *mmcif_structure.topology, *mmcif_structure.coordinates, mmcif_options,
      mmcif_context);
  passed &= expect(
      serialized_mmcif.has_value() &&
          serialized_mmcif.value().report.format ==
              io::StructureFormat::mmcif &&
          serialized_mmcif.value().report.frame_count == 2U &&
          serialized_mmcif.value().report.atom_count == 3U &&
          mmcif_progress_count == 2U &&
          serialized_mmcif.value().content.starts_with("data_MS02\n") &&
          serialized_mmcif.value().content.find(
              "_atom_site.pdbx_PDB_model_num") != std::string::npos &&
          serialized_mmcif.value().content.find(
              "_struct_conn.pdbx_value_order") != std::string::npos &&
          serialized_mmcif.value().content.find("conn1 covale") !=
              std::string::npos,
      "mmCIF writer must emit multi-model atom_site and struct_conn loops");
  if (serialized_mmcif.has_value()) {
    const auto roundtrip =
        io::read_structure(serialized_mmcif.value().content,
                           {io::StructureFormat::mmcif, "roundtrip.cif"});
    passed &= expect(
        roundtrip.has_value() &&
            roundtrip.value().structures.front().topology->atom_count() == 3U &&
            roundtrip.value().structures.front().topology->bonds().size() ==
                2U &&
            roundtrip.value().structures.front().coordinates->frame_count() ==
                2U,
        "native mmCIF read-write-read must preserve atoms, bonds and models");
    if (roundtrip.has_value()) {
      const auto second =
          roundtrip.value().structures.front().coordinates->read_frame(1U);
      passed &= expect(
          second.has_value() &&
              second.value()->presence() ==
                  std::vector<std::uint8_t>{1U, 1U, 0U} &&
              second.value()->metadata().unit_cell.has_value(),
          "mmCIF round-trip must preserve later-model presence and cell");
    }
  }
  mmcif_options.frame_indices = {1U};
  const auto incomplete_mmcif = io::serialize_structure(
      *mmcif_structure.topology, *mmcif_structure.coordinates, mmcif_options,
      mmcif_context);
  passed &= expect(!incomplete_mmcif.has_value() &&
                       incomplete_mmcif.error().message.find(
                           "first output model") != std::string::npos,
                   "mmCIF writer must reject an incomplete first model");

  operation::TaskContext pqr_context;
  io::StructureWriteOptions pqr_options;
  pqr_options.format = io::StructureFormat::pqr;
  pqr_options.frame_indices = {0U};
  pqr_options.decimal_places = 4U;
  pqr_options.comment = "electrostatics round trip";
  const auto serialized_pqr = io::serialize_structure(
      *pqr_structure.topology, *pqr_structure.coordinates, pqr_options,
      pqr_context);
  passed &= expect(
      serialized_pqr.has_value() &&
          serialized_pqr.value().report.format == io::StructureFormat::pqr &&
          serialized_pqr.value().report.frame_count == 1U &&
          serialized_pqr.value().content.starts_with(
              "REMARK electrostatics round trip\n") &&
          serialized_pqr.value().content.find(
              "CRYST1   10.000   11.000   12.000") != std::string::npos &&
          serialized_pqr.value().content.find(
              "ATOM 1 N GLY A 1 1.0000 2.0000 3.0000 -0.3000 1.5500") !=
              std::string::npos,
      "PQR writer must preserve charge/radius and requested decimal precision");
  if (serialized_pqr.has_value()) {
    const auto roundtrip =
        io::read_structure(serialized_pqr.value().content,
                           {io::StructureFormat::pqr, "roundtrip.pqr"});
    const auto *charge =
        roundtrip.has_value()
            ? roundtrip.value().structures.front().topology->properties().find(
                  "partial_charge")
            : nullptr;
    const auto *radius =
        roundtrip.has_value()
            ? roundtrip.value().structures.front().topology->properties().find(
                  "pqr.radius")
            : nullptr;
    bool roundtrip_has_cell = false;
    if (roundtrip.has_value()) {
      const auto roundtrip_frame =
          roundtrip.value().structures.front().coordinates->read_frame(0U);
      roundtrip_has_cell =
          roundtrip_frame.has_value() &&
          roundtrip_frame.value()->metadata().unit_cell.has_value();
    }
    passed &= expect(
        roundtrip.has_value() && roundtrip_has_cell && charge != nullptr &&
            radius != nullptr &&
            std::get<std::vector<double>>(charge->values) ==
                std::vector<double>{-0.3, 0.1, -1.0} &&
            std::get<std::vector<double>>(radius->values) ==
                std::vector<double>{1.55, 1.7, 1.8},
        "native PQR read/write must round-trip electrostatics properties");
  }
  const auto missing_pqr_properties = io::serialize_structure(
      *structure.topology, *structure.coordinates, pqr_options, pqr_context);
  passed &= expect(
      !missing_pqr_properties.has_value() &&
          missing_pqr_properties.error().message.find("partial_charge") !=
              std::string::npos,
      "PQR export must fail rather than invent missing charge/radius values");

  operation::TaskContext sdf_context;
  io::StructureWriteOptions sdf_options;
  sdf_options.format = io::StructureFormat::sdf;
  sdf_options.frame_indices = {0U};
  const auto serialized_sdf = io::serialize_structure(
      *sdf_structure.topology, *sdf_structure.coordinates, sdf_options,
      sdf_context);
  passed &= expect(
      serialized_sdf.has_value() &&
          serialized_sdf.value().report.format == io::StructureFormat::sdf &&
          serialized_sdf.value().content.find("V2000") != std::string::npos &&
          serialized_sdf.value().content.find("M  CHG") != std::string::npos &&
          serialized_sdf.value().content.find("M  ISO") != std::string::npos &&
          serialized_sdf.value().content.find("M  RAD") != std::string::npos &&
          serialized_sdf.value().content.find(
              ">  <NOTE>\nline one\nline two") != std::string::npos &&
          serialized_sdf.value().content.ends_with("$$$$\n"),
      "SDF writer must preserve V2000 chemistry and multiline data fields");
  if (serialized_sdf.has_value()) {
    const auto roundtrip =
        io::read_structure(serialized_sdf.value().content,
                           {io::StructureFormat::sdf, "roundtrip.sdf"});
    const auto *isotope =
        roundtrip.has_value()
            ? roundtrip.value().structures.front().topology->properties().find(
                  "sdf.isotope_mass")
            : nullptr;
    passed &= expect(
        roundtrip.has_value() && roundtrip.value().structures.size() == 1U &&
            roundtrip.value().structures.front().topology->bonds()[0].order ==
                model::BondOrder::double_bond &&
            roundtrip.value().structures.front().topology->bonds()[2].order ==
                model::BondOrder::aromatic &&
            roundtrip.value()
                    .structures.front()
                    .topology->atoms()[0]
                    .formal_charge == 1 &&
            isotope != nullptr &&
            std::get<std::vector<std::int64_t>>(isotope->values)[2] == 13 &&
            roundtrip.value().structures.front().metadata.at("sdf.data.NOTE") ==
                "line one\nline two",
        "native SDF read/write must round-trip chemistry semantics");
  }

  auto mol_options = sdf_options;
  mol_options.format = io::StructureFormat::mol;
  const auto serialized_mol = io::serialize_structure(
      *sdf_structure.topology, *sdf_structure.coordinates, mol_options,
      sdf_context);
  passed &= expect(
      serialized_mol.has_value() &&
          serialized_mol.value().content.find("$$$$") == std::string::npos &&
          std::any_of(serialized_mol.value().report.losses.begin(),
                      serialized_mol.value().report.losses.end(),
                      [](const auto &loss) {
                        return loss.channel == "sdf_data_fields";
                      }),
      "MOL writer must omit SDF tags only with an explicit loss report");
  if (serialized_mol.has_value()) {
    const auto roundtrip =
        io::read_structure(serialized_mol.value().content,
                           {io::StructureFormat::mol, "roundtrip.mol"});
    passed &= expect(
        roundtrip.has_value() &&
            roundtrip.value().structures.front().topology->bonds()[2].order ==
                model::BondOrder::aromatic,
        "native MOL V2000 output must be readable without bond loss");
  }

  operation::TaskContext mol2_context;
  io::StructureWriteOptions mol2_options;
  mol2_options.format = io::StructureFormat::mol2;
  mol2_options.frame_indices = {0U};
  mol2_options.decimal_places = 6U;
  const auto serialized_mol2 = io::serialize_structure(
      *mol2_structure.topology, *mol2_structure.coordinates, mol2_options,
      mol2_context);
  passed &= expect(
      serialized_mol2.has_value() &&
          serialized_mol2.value().report.format == io::StructureFormat::mol2 &&
          serialized_mol2.value().content.starts_with(
              "@<TRIPOS>MOLECULE\nAcetamide\n") &&
          serialized_mol2.value().content.find("USER_CHARGES") !=
              std::string::npos &&
          serialized_mol2.value().content.find(
              "N.am 1 ACE -0.200000 BACKBONE") != std::string::npos &&
          serialized_mol2.value().content.find("2 1 3 am AMIDE") !=
              std::string::npos &&
          serialized_mol2.value().content.find(
              "4 2 4 nc PROXIMITY_ONLY") != std::string::npos &&
          serialized_mol2.value().content.find("@<TRIPOS>CRYSIN") !=
              std::string::npos,
      "MOL2 writer must preserve SYBYL types, charges, amide/status and cell");
  if (serialized_mol2.has_value()) {
    const auto roundtrip =
        io::read_structure(serialized_mol2.value().content,
                           {io::StructureFormat::mol2, "roundtrip.mol2"});
    const auto *atom_types =
        roundtrip.has_value()
            ? roundtrip.value().structures.front().topology->properties().find(
                  "mol2.atom_type")
            : nullptr;
    std::shared_ptr<const model::CoordinateFrame> frame;
    if (roundtrip.has_value()) {
      const auto loaded_frame =
          roundtrip.value().structures.front().coordinates->read_frame(0U);
      if (loaded_frame.has_value())
        frame = loaded_frame.value();
    }
    passed &= expect(
        roundtrip.has_value() && atom_types != nullptr && frame != nullptr &&
            roundtrip.value().structures.front().topology->bonds()[1].order ==
                model::BondOrder::amide &&
            roundtrip.value().structures.front().topology->bonds().size() ==
                3U &&
            roundtrip.value()
                    .structures.front()
                    .topology->source_metadata()
                    .at("mol2.not_connected.0.status") == "PROXIMITY_ONLY" &&
            std::get<std::vector<std::string>>(atom_types->values)[2] ==
                "N.am" &&
            frame->metadata().unit_cell.has_value(),
        "native MOL2 read/write must round-trip chemistry and CRYSIN");
  }
  const auto untyped_mol2 = io::serialize_structure(
      *structure.topology, *structure.coordinates, mol2_options, mol2_context);
  passed &= expect(!untyped_mol2.has_value() &&
                       untyped_mol2.error().message.find("mol2.atom_type") !=
                           std::string::npos,
                   "MOL2 writer must not invent unvalidated SYBYL atom types");
  const auto serialized_benzene_mol2 = io::serialize_structure(
      *benzene_mol2_structure.topology, *benzene_mol2_structure.coordinates,
      mol2_options, mol2_context);
  passed &= expect(
      serialized_benzene_mol2.has_value() &&
          serialized_benzene_mol2.value().content.find("10 C1 1.400000") !=
              std::string::npos &&
          serialized_benzene_mol2.value().content.find("60 60 10 ar") !=
              std::string::npos &&
          serialized_benzene_mol2.value().content.find("7 BEN 10 GROUP") !=
              std::string::npos,
      "MOL2 writer must preserve non-contiguous atom/bond and root "
      "identifiers");

  operation::TaskContext gro_context;
  io::StructureWriteOptions gro_options;
  gro_options.format = io::StructureFormat::gro;
  gro_options.decimal_places = 5U;
  const auto serialized_gro = io::serialize_structure(
      *gro_structure.topology, *gro_structure.coordinates, gro_options,
      gro_context);
  passed &= expect(
      serialized_gro.has_value() &&
          serialized_gro.value().report.format == io::StructureFormat::gro &&
          serialized_gro.value().report.frame_count == 2U &&
          serialized_gro.value().content.find("Synthetic water, t= 0.0") !=
              std::string::npos &&
          serialized_gro.value().content.find("  0.010000") !=
              std::string::npos &&
          serialized_gro.value().content.find("Synthetic water, t= 2.5") !=
              std::string::npos,
      "GRO writer must preserve concatenated frames, titles and optional "
      "velocity");
  if (serialized_gro.has_value()) {
    const auto roundtrip =
        io::read_structure(serialized_gro.value().content,
                           {io::StructureFormat::gro, "roundtrip.gro"});
    std::shared_ptr<const model::CoordinateFrame> first;
    std::shared_ptr<const model::CoordinateFrame> second;
    if (roundtrip.has_value()) {
      const auto first_result =
          roundtrip.value().structures.front().coordinates->read_frame(0U);
      const auto second_result =
          roundtrip.value().structures.front().coordinates->read_frame(1U);
      if (first_result.has_value())
        first = first_result.value();
      if (second_result.has_value())
        second = second_result.value();
    }
    passed &= expect(roundtrip.has_value() && first != nullptr &&
                         second != nullptr && first->velocities().has_value() &&
                         !second->velocities().has_value() &&
                         second->metadata().unit_cell.has_value() &&
                         second->metadata().physical_time->value == 2.5,
                     "native GRO read/write must round-trip velocity presence, "
                     "time and cell");
  }
  auto gro_comment_options = gro_options;
  gro_comment_options.comment = "custom GRO export";
  const auto commented_gro = io::serialize_structure(
      *gro_structure.topology, *gro_structure.coordinates,
      gro_comment_options, gro_context);
  passed &= expect(
      commented_gro.has_value() &&
          commented_gro.value().content.starts_with(
              "custom GRO export, t= 0\n2\n") &&
          commented_gro.value().content.find(
              "custom GRO export, t= 2.5\n2\n") != std::string::npos,
      "GRO comment override must not erase typed per-frame physical time");

  const auto first_gro_frame =
      gro_structure.coordinates->read_frame(0U).value();
  auto contradictory_metadata = first_gro_frame->metadata();
  contradictory_metadata.physical_time.reset();
  const auto contradictory_frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{first_gro_frame->positions()},
      first_gro_frame->velocities(), first_gro_frame->presence(),
      std::move(contradictory_metadata));
  const auto contradictory_source = model::InMemoryCoordinateSource::create(
      gro_structure.topology->atom_count(), {contradictory_frame.value()});
  const auto contradictory_gro = io::serialize_structure(
      *gro_structure.topology, *contradictory_source.value(), gro_options,
      gro_context);
  passed &= expect(
      !contradictory_gro.has_value() &&
          contradictory_gro.error().message.find("no typed physical time") !=
              std::string::npos,
      "GRO writer must reject a stale parseable title time without typed "
      "metadata");

  operation::TaskContext g96_context;
  io::StructureWriteOptions g96_options;
  g96_options.format = io::StructureFormat::g96;
  g96_options.decimal_places = 6U;
  const auto serialized_g96 = io::serialize_structure(
      *g96_structure.topology, *g96_structure.coordinates, g96_options,
      g96_context);
  passed &= expect(
      serialized_g96.has_value() &&
          serialized_g96.value().report.format == io::StructureFormat::g96 &&
          serialized_g96.value().report.frame_count == 2U &&
          serialized_g96.value().content.starts_with(
              "TITLE\nSynthetic G96 water trajectory\nEND\n") &&
          serialized_g96.value().content.find("POSITION\n") !=
              std::string::npos &&
          serialized_g96.value().content.find("VELOCITY\n") !=
              std::string::npos &&
          serialized_g96.value().content.find("    0.110000000") !=
              std::string::npos &&
          std::any_of(serialized_g96.value().report.losses.begin(),
                      serialized_g96.value().report.losses.end(),
                      [](const auto &loss) {
                        return loss.channel == "requested_precision";
                      }),
      "G96 writer must emit fixed F15.9 multi-frame blocks and precision loss");
  if (serialized_g96.has_value()) {
    const auto roundtrip =
        io::read_structure(serialized_g96.value().content,
                           {io::StructureFormat::g96, "roundtrip.g96"});
    std::shared_ptr<const model::CoordinateFrame> first;
    std::shared_ptr<const model::CoordinateFrame> second;
    if (roundtrip.has_value()) {
      const auto first_result =
          roundtrip.value().structures.front().coordinates->read_frame(0U);
      const auto second_result =
          roundtrip.value().structures.front().coordinates->read_frame(1U);
      if (first_result.has_value())
        first = first_result.value();
      if (second_result.has_value())
        second = second_result.value();
    }
    passed &= expect(
        roundtrip.has_value() && first != nullptr && second != nullptr &&
            first->velocities().has_value() &&
            !second->velocities().has_value() &&
            second->metadata().source_step == 25U &&
            second->metadata().physical_time->value == 2.5 &&
            second->metadata().unit_cell.has_value(),
        "native G96 read/write must round-trip velocity, step, time and cell");
  }
  operation::TaskContext cancelled_g96_context;
  cancelled_g96_context.cancellation.request_cancel();
  const auto cancelled_g96 = io::serialize_structure(
      *g96_structure.topology, *g96_structure.coordinates, g96_options,
      cancelled_g96_context);
  passed &= expect(
      !cancelled_g96.has_value() &&
          cancelled_g96.error().code == operation::ErrorCode::cancelled,
      "G96 writer must honor cancellation before decoding the first frame");

  operation::TaskContext psf_context;
  io::StructureWriteOptions psf_options;
  psf_options.format = io::StructureFormat::psf;
  const auto serialized_psf = io::serialize_structure(
      *psf_structure.topology, *psf_structure.coordinates, psf_options,
      psf_context);
  passed &= expect(
      serialized_psf.has_value() &&
          serialized_psf.value().report.format == io::StructureFormat::psf &&
          serialized_psf.value().report.frame_count == 0U &&
          serialized_psf.value().content.starts_with("PSF EXT XPLOR CMAP\n") &&
          serialized_psf.value().content.find("!NATOM") != std::string::npos &&
          serialized_psf.value().content.find("!NIMPHI") != std::string::npos &&
          serialized_psf.value().content.find("!NCRTERM") != std::string::npos,
      "PSF writer must export a zero-frame EXT X-PLOR topology");
  if (serialized_psf.has_value()) {
    const auto roundtrip =
        io::read_structure(serialized_psf.value().content,
                           {io::StructureFormat::psf, "roundtrip.psf"});
    passed &= expect(
        roundtrip.has_value() &&
            roundtrip.value().structures.front().coordinates->frame_count() ==
                0U &&
            roundtrip.value().structures.front().topology->bonds().size() ==
                3U &&
            roundtrip.value().structures.front().topology->angles().size() ==
                2U &&
            roundtrip.value().structures.front().topology->impropers().size() ==
                1U &&
            roundtrip.value().structures.front().topology->cmap_terms().size() ==
                1U,
        "native PSF read/write must round-trip topology and remain "
        "coordinate-free");
  }
  const auto untyped_psf = io::serialize_structure(
      *structure.topology, *structure.coordinates, psf_options, psf_context);
  passed &= expect(
      !untyped_psf.has_value() && untyped_psf.error().message.find(
                                      "psf.atom_type") != std::string::npos,
      "PSF writer must not invent force-field atom types, charge or mass");

  operation::TaskContext all_context;
  io::StructureWriteOptions all_options;
  all_options.format = io::StructureFormat::xyz;
  const auto missing_atom = io::serialize_structure(
      *structure.topology, *structure.coordinates, all_options, all_context);
  passed &=
      expect(!missing_atom.has_value() &&
                 missing_atom.error().message.find("missing atom") !=
                     std::string::npos,
             "XYZ all-frame export must reject unrepresentable missing atoms");

  const auto output = std::filesystem::path{argv[9]} / "writer_roundtrip.xyz";
  std::error_code ignored;
  std::filesystem::remove(output, ignored);
  operation::TaskContext file_context;
  const auto written = io::write_structure_file(
      output, *structure.topology, *structure.coordinates, current_options,
      false, file_context);
  passed &= expect(written.has_value() && std::filesystem::exists(output) &&
                       written.value().byte_count == read_text(output).size(),
                   "atomic XYZ file write must publish a complete target");
  const auto collision = io::write_structure_file(
      output, *structure.topology, *structure.coordinates, current_options,
      false, file_context);
  passed &= expect(!collision.has_value() &&
                       collision.error().code ==
                           operation::ErrorCode::invalid_argument,
                   "non-overwrite XYZ write must preserve an existing target");
  const auto replaced = io::write_structure_file(
      output, *structure.topology, *structure.coordinates, current_options,
      true, file_context);
  passed &= expect(replaced.has_value(),
                   "explicit overwrite must atomically replace XYZ output");

  const auto cancelled_output =
      std::filesystem::path{argv[9]} / "writer_cancelled.xyz";
  std::filesystem::remove(cancelled_output, ignored);
  operation::TaskContext cancelled_context;
  cancelled_context.cancellation.request_cancel();
  const auto cancelled = io::write_structure_file(
      cancelled_output, *structure.topology, *structure.coordinates,
      current_options, false, cancelled_context);
  passed &=
      expect(!cancelled.has_value() &&
                 cancelled.error().code == operation::ErrorCode::cancelled &&
                 !std::filesystem::exists(cancelled_output),
             "cancelled export must not publish a partial target");
  return passed ? 0 : 1;
}
