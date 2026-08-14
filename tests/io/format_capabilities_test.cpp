#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <string_view>

#include "molshredder/io/format_capabilities.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

} // namespace

int main() {
  using molshredder::io::format_capabilities;
  using molshredder::io::FormatCapability;
  bool passed = true;
  const auto &capabilities = format_capabilities();
  passed &= expect(molshredder::io::kFormatCapabilitySchemaVersion == 2U,
                   "format capability schema must remain versioned");
  passed &= expect(capabilities.size() == 24U,
                   "registry must enumerate every implemented format");
  std::set<std::string> ids;
  for (const auto &capability : capabilities) {
    passed &= expect(ids.insert(capability.id).second,
                     "format capability IDs must be unique");
    passed &= expect(
        !capability.extensions.empty() && !capability.channels.empty() &&
            !capability.implementation.empty(),
        "every format requires extension/channel/implementation evidence");
  }
  const auto find =
      [&capabilities](std::string_view id) -> const FormatCapability * {
    const auto found = std::find_if(
        capabilities.begin(), capabilities.end(),
        [id](const auto &capability) { return capability.id == id; });
    return found == capabilities.end() ? nullptr : &*found;
  };
  const auto *xyz = find("xyz");
  passed &=
      expect(xyz != nullptr && xyz->readable && xyz->writable &&
                 xyz->multi_frame && !xyz->multi_structure && xyz->streaming,
             "XYZ registry row must match native read/write behavior");
  const auto *pdb = find("pdb");
  passed &=
      expect(pdb != nullptr && pdb->readable && pdb->writable &&
                 pdb->multi_frame && !pdb->multi_structure && pdb->streaming &&
                 std::find(pdb->channels.begin(), pdb->channels.end(),
                           "presence") != pdb->channels.end() &&
                 std::find(pdb->channels.begin(), pdb->channels.end(),
                           "unit_cell") != pdb->channels.end(),
             "PDB registry row must expose native multi-model write behavior");
  const auto *mmcif = find("mmcif");
  passed &= expect(
      mmcif != nullptr && mmcif->readable && mmcif->writable &&
          mmcif->multi_frame && mmcif->multi_structure && mmcif->streaming &&
          std::find(mmcif->channels.begin(), mmcif->channels.end(),
                    "presence") != mmcif->channels.end() &&
          std::find(mmcif->limitations.begin(), mmcif->limitations.end(),
                    "semantic_loss_report") != mmcif->limitations.end(),
      "mmCIF registry row must expose native multi-model write behavior");
  const auto *pqr = find("pqr");
  const auto *bcif = find("bcif");
  const auto *opendx = find("opendx");
  const auto *mrc = find("mrc");
  passed &= expect(
      bcif != nullptr && bcif->readable && !bcif->writable &&
          bcif->multi_frame && bcif->multi_structure && bcif->random_access &&
          !bcif->streaming &&
          std::find(bcif->channels.begin(), bcif->channels.end(),
                    "column_mask") != bcif->channels.end(),
      "BCIF registry row must expose BinaryCIF multi-block read semantics");
  passed &= expect(
      opendx != nullptr && opendx->family == "volume" && opendx->readable &&
          !opendx->writable && !opendx->multi_frame &&
          !opendx->multi_structure && opendx->random_access &&
          !opendx->streaming &&
          std::find(opendx->channels.begin(), opendx->channels.end(),
                    "scalar_grid") != opendx->channels.end() &&
          std::find(opendx->limitations.begin(), opendx->limitations.end(),
                    "single_scalar_field") != opendx->limitations.end(),
      "OpenDX registry row must expose regular scalar volume semantics");
  passed &= expect(
      mrc != nullptr && mrc->family == "volume" && mrc->readable &&
          !mrc->writable && !mrc->multi_frame && !mrc->multi_structure &&
          mrc->random_access && !mrc->streaming &&
          std::find(mrc->channels.begin(), mrc->channels.end(),
                    "axis_permutation") != mrc->channels.end() &&
          std::find(mrc->limitations.begin(), mrc->limitations.end(),
                    "handedness_unspecified") != mrc->limitations.end(),
      "MRC registry row must expose scalar mode, geometry and ambiguity truth");
  passed &=
      expect(pqr != nullptr && pqr->readable && pqr->writable &&
                 !pqr->multi_frame && pqr->streaming &&
                 std::find(pqr->channels.begin(), pqr->channels.end(),
                           "partial_charge") != pqr->channels.end() &&
                 std::find(pqr->channels.begin(), pqr->channels.end(),
                           "pqr_radius") != pqr->channels.end(),
             "PQR registry row must expose native electrostatics channels");
  const auto *mol = find("mol");
  passed &= expect(mol != nullptr && mol->readable && mol->writable &&
                       !mol->multi_frame && !mol->multi_structure &&
                       std::find(mol->channels.begin(), mol->channels.end(),
                                 "bond_order") != mol->channels.end(),
                   "MOL registry row must expose V2000 chemistry channels");
  const auto *sdf = find("sdf");
  passed &= expect(sdf != nullptr && sdf->readable && sdf->writable &&
                       !sdf->multi_frame && sdf->multi_structure &&
                       std::find(sdf->channels.begin(), sdf->channels.end(),
                                 "data_fields") != sdf->channels.end(),
                   "SDF registry row must expose multi-record data fields");
  const auto *mol2 = find("mol2");
  passed &= expect(
      mol2 != nullptr && mol2->readable && mol2->writable &&
          !mol2->multi_frame && mol2->multi_structure &&
          std::find(mol2->channels.begin(), mol2->channels.end(),
                    "sybyl_atom_type") != mol2->channels.end() &&
          std::find(mol2->channels.begin(), mol2->channels.end(),
                    "amide_bond") != mol2->channels.end(),
      "MOL2 registry row must expose atom typing and amide bond semantics");
  const auto *gro = find("gro");
  passed &= expect(
      gro != nullptr && gro->readable && gro->writable && gro->multi_frame &&
          !gro->multi_structure &&
          std::find(gro->channels.begin(), gro->channels.end(), "velocity") !=
              gro->channels.end() &&
          std::find(gro->channels.begin(), gro->channels.end(), "unit_cell") !=
              gro->channels.end(),
      "GRO registry row must expose multi-frame velocity and box semantics");
  const auto *g96 = find("g96");
  passed &= expect(g96 != nullptr && g96->readable && g96->writable &&
                       g96->multi_frame && !g96->multi_structure &&
                       std::find(g96->channels.begin(), g96->channels.end(),
                                 "velocity") != g96->channels.end() &&
                       std::find(g96->channels.begin(), g96->channels.end(),
                                 "source_step") != g96->channels.end() &&
                       std::find(g96->channels.begin(), g96->channels.end(),
                                 "unit_cell") != g96->channels.end(),
                   "G96 registry row must expose block trajectory semantics");
  const auto *vtf = find("vtf");
  passed &= expect(vtf != nullptr && vtf->readable && !vtf->writable &&
                       vtf->multi_frame && !vtf->multi_structure &&
                       std::find(vtf->channels.begin(), vtf->channels.end(),
                                 "sparse_coordinate_inheritance") !=
                           vtf->channels.end() &&
                       std::find(vtf->channels.begin(), vtf->channels.end(),
                                 "connectivity") != vtf->channels.end(),
                   "VTF registry row must expose sparse combined "
                   "topology/trajectory semantics");
  const auto *psf = find("psf");
  passed &= expect(
      psf != nullptr && psf->readable && psf->writable && !psf->multi_frame &&
          !psf->multi_structure &&
          std::find(psf->channels.begin(), psf->channels.end(),
                    "force_field_atom_type") != psf->channels.end() &&
          std::find(psf->channels.begin(), psf->channels.end(),
                    "topology_only") != psf->channels.end(),
      "PSF registry row must expose force-field and topology-only semantics");
  const auto *prmtop = find("prmtop");
  passed &= expect(
      prmtop != nullptr && prmtop->readable && !prmtop->writable &&
          std::find(prmtop->channels.begin(), prmtop->channels.end(),
                    "force_field_atom_type") != prmtop->channels.end() &&
          std::find(prmtop->channels.begin(), prmtop->channels.end(),
                    "topology_only") != prmtop->channels.end(),
      "PRMTOP registry row must expose read-only Amber topology semantics");
  for (const auto id : {"dcd", "trr", "xtc"}) {
    const auto *trajectory = find(id);
    passed &=
        expect(trajectory != nullptr && trajectory->readable &&
                   !trajectory->writable && trajectory->random_access &&
                   trajectory->streaming,
               "trajectory registry row must match indexed read-only behavior");
  }
  const auto *mdcrd = find("mdcrd");
  passed &= expect(
      mdcrd != nullptr && mdcrd->readable && !mdcrd->writable &&
          mdcrd->multi_frame && mdcrd->random_access && mdcrd->streaming &&
          std::find(mdcrd->channels.begin(), mdcrd->channels.end(),
                    "unit_cell") != mdcrd->channels.end(),
      "MDCRD registry row must expose indexed ASCII trajectory semantics");
  const auto *rst7 = find("rst7");
  passed &= expect(
      rst7 != nullptr && rst7->readable && !rst7->writable &&
          !rst7->multi_frame && rst7->random_access && rst7->streaming &&
          std::find(rst7->channels.begin(), rst7->channels.end(), "velocity") !=
              rst7->channels.end(),
      "RST7 registry row must expose single-frame Amber restart semantics");
  const auto *lammps = find("lammps");
  passed &= expect(
      lammps != nullptr && lammps->readable && !lammps->writable &&
          lammps->multi_frame && lammps->random_access && lammps->streaming &&
          std::find(lammps->channels.begin(), lammps->channels.end(),
                    "atom_id") != lammps->channels.end() &&
          std::find(lammps->limitations.begin(), lammps->limitations.end(),
                    "coordinate_unit_required") != lammps->limitations.end(),
      "LAMMPS registry row must expose ID mapping and explicit unit contract");
  const auto *binpos = find("binpos");
  passed &= expect(
      binpos != nullptr && binpos->readable && !binpos->writable &&
          binpos->multi_frame && binpos->random_access && binpos->streaming &&
          std::find(binpos->channels.begin(), binpos->channels.end(),
                    "byte_order") != binpos->channels.end() &&
          std::find(binpos->limitations.begin(), binpos->limitations.end(),
                    "no_unit_cell") != binpos->limitations.end(),
      "BINPOS registry row must expose binary coordinate-only semantics");
  const auto *netcdf = find("netcdf");
  passed &= expect(
      netcdf != nullptr && netcdf->readable && !netcdf->writable &&
          netcdf->multi_frame && netcdf->random_access && netcdf->streaming &&
          std::find(netcdf->channels.begin(), netcdf->channels.end(),
                    "force") != netcdf->channels.end() &&
          std::find(netcdf->channels.begin(), netcdf->channels.end(),
                    "integer_compression") != netcdf->channels.end(),
      "Amber NetCDF registry row must expose random-access scientific "
      "channels");
  const auto *h5md = find("h5md");
  passed &= expect(
      h5md != nullptr && h5md->readable && !h5md->writable &&
          h5md->multi_frame && h5md->random_access && h5md->streaming &&
          std::find(h5md->channels.begin(), h5md->channels.end(),
                    "particle_id") != h5md->channels.end() &&
          std::find(h5md->channels.begin(), h5md->channels.end(), "mass") !=
              h5md->channels.end() &&
          std::find(h5md->channels.begin(), h5md->channels.end(), "image") !=
              h5md->channels.end() &&
          std::find(h5md->limitations.begin(), h5md->limitations.end(),
                    "partial_periodic_boundary_unsupported") !=
              h5md->limitations.end(),
      "H5MD registry row must expose identity, per-particle and box semantics");
  return passed ? 0 : 1;
}
