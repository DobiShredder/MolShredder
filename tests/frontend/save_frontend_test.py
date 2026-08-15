#!/usr/bin/env python3

import importlib
import json
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require(len(sys.argv) == 15,
            "expected module, CLI, PDB, mmCIF, XYZ, PQR, SDF, MOL2, GRO, G96, PSF, PRMTOP, RST7 fixtures and output directory")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    pdb_fixture = pathlib.Path(sys.argv[3]).resolve()
    mmcif_fixture = pathlib.Path(sys.argv[4]).resolve()
    fixture = pathlib.Path(sys.argv[5]).resolve()
    pqr_fixture = pathlib.Path(sys.argv[6]).resolve()
    sdf_fixture = pathlib.Path(sys.argv[7]).resolve()
    mol2_fixture = pathlib.Path(sys.argv[8]).resolve()
    gro_fixture = pathlib.Path(sys.argv[9]).resolve()
    g96_fixture = pathlib.Path(sys.argv[10]).resolve()
    psf_fixture = pathlib.Path(sys.argv[11]).resolve()
    prmtop_fixture = pathlib.Path(sys.argv[12]).resolve()
    rst7_fixture = pathlib.Path(sys.argv[13]).resolve()
    output_directory = pathlib.Path(sys.argv[14]).resolve()
    output = output_directory / "frontend-roundtrip.xyz"
    pqr_output = output_directory / "frontend-roundtrip.pqr"
    sdf_output = output_directory / "frontend-roundtrip.sdf"
    mol2_output = output_directory / "frontend-roundtrip.mol2"
    gro_output = output_directory / "frontend-roundtrip.gro"
    g96_output = output_directory / "frontend-roundtrip.g96"
    psf_output = output_directory / "frontend-roundtrip.psf"
    pdb_output = output_directory / "frontend-roundtrip.pdb"
    mmcif_output = output_directory / "frontend-roundtrip.cif"
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    writable = molshredder.invoke(
        "format list", {"family": "structure", "direction": "write"}
    )
    require(writable["status"] == "ok" and
            writable["data"]["capability_schema_version"] == 2 and
            writable["data"]["format_count"] == 10 and
            [item["id"] for item in writable["data"]["formats"]] ==
            ["pdb", "mmcif", "pqr", "mol", "sdf", "mol2", "psf", "gro", "g96", "xyz"] and
            "presence" in writable["data"]["formats"][0]["channels"] and
            "partial_charge" in writable["data"]["formats"][2]["channels"] and
            "pqr_radius" in writable["data"]["formats"][2]["channels"] and
            "unit_cell" in writable["data"]["formats"][2]["channels"] and
            not writable["data"]["formats"][3]["multi_structure"] and
            writable["data"]["formats"][4]["multi_structure"] and
            writable["data"]["formats"][5]["multi_structure"] and
            "sybyl_atom_type" in
            writable["data"]["formats"][5]["channels"] and
            "topology_only" in writable["data"]["formats"][6]["channels"] and
            "velocity" in writable["data"]["formats"][7]["channels"] and
            "source_step" in writable["data"]["formats"][8]["channels"] and
            writable["data"]["formats"][9]["channels"] ==
            ["element", "coordinates", "frame_comment"],
            "native capability registry does not expose chemistry write truth")
    readable = molshredder.invoke("format list", {"direction": "read"})
    require(readable["status"] == "ok" and
            readable["data"]["format_count"] == 24 and
            [item["id"] for item in readable["data"]["formats"]][-5:] ==
            ["lammps", "netcdf", "h5md", "binpos", "rst7"] and
            next(item for item in readable["data"]["formats"]
                 if item["id"] == "prmtop")["family"] == "structure" and
            "topology_only" in next(
                item for item in readable["data"]["formats"]
                if item["id"] == "prmtop")["channels"] and
            "velocity" in readable["data"]["formats"][-1]["channels"],
            "native capability registry does not expose Amber read truth")

    loaded = molshredder.invoke(
        "load", {"path": str(fixture), "name": "motion"}
    )
    require(loaded["status"] == "ok" and loaded["data"]["frame_count"] == 2,
            "Python XYZ load did not retain both frames")
    arguments = {
        "path": str(output), "file-format": "xyz", "frames": "all",
        "precision": "4", "overwrite": "true"
    }
    python_save = molshredder.invoke("save", arguments)
    require(python_save["status"] == "ok", "Python XYZ save failed")

    script = (
        "format json\n"
        f'invoke "load" --file-format "xyz" --name "motion" --path "{fixture}"\n'
        f'invoke "save" --file-format "xyz" --frames "all" '
        f'--overwrite "true" --path "{output}" --precision "4"\n'
        "exit\n"
    )
    completed = subprocess.run(
        [str(cli_path), "console"], input=script, text=True,
        capture_output=True, check=True
    )
    envelopes = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(len(envelopes) == 2, "CLI did not emit load/save envelopes")
    require(python_save == envelopes[-1],
            "CLI and Python save result/loss envelopes diverged")
    text = output.read_text(encoding="utf-8")
    require(text.startswith("3\n") and text.count("\n3\n") == 1,
            "multi-frame XYZ output does not contain two frame blocks")
    reloaded = molshredder.invoke(
        "load", {"path": str(output), "name": "roundtrip"}
    )
    require(reloaded["status"] == "ok" and
            reloaded["data"]["frame_count"] == 2,
            "saved XYZ did not round-trip through the native reader")

    pqr_loaded = molshredder.invoke(
        "load", {"path": str(pqr_fixture), "name": "electrostatics"}
    )
    require(pqr_loaded["status"] == "ok" and
            pqr_loaded["data"]["format"] == "pqr" and
            pqr_loaded["data"]["frame_count"] == 1,
            "Python PQR load did not preserve its single frame")
    pqr_arguments = {
        "path": str(pqr_output), "file-format": "pqr",
        "frames": "current", "precision": "4", "overwrite": "true"
    }
    python_pqr_save = molshredder.invoke("save", pqr_arguments)
    require(python_pqr_save["status"] == "ok" and
            python_pqr_save["data"]["format"] == "pqr",
            "Python PQR save failed")
    pqr_script = (
        "format json\n"
        f'invoke "load" --file-format "xyz" --name "motion" '
        f'--path "{fixture}"\n'
        f'invoke "load" --file-format "xyz" --name "roundtrip" '
        f'--path "{output}"\n'
        f'invoke "load" --file-format "pqr" --name "electrostatics" '
        f'--path "{pqr_fixture}"\n'
        f'invoke "save" --file-format "pqr" --frames "current" '
        f'--overwrite "true" --path "{pqr_output}" --precision "4"\n'
        "exit\n"
    )
    pqr_completed = subprocess.run(
        [str(cli_path), "console"], input=pqr_script, text=True,
        capture_output=True, check=True
    )
    pqr_envelopes = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in pqr_completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(len(pqr_envelopes) == 4 and
            python_pqr_save == pqr_envelopes[-1],
            "CLI and Python PQR save envelopes diverged")
    pqr_text = pqr_output.read_text(encoding="utf-8")
    require("-0.3000 1.5500" in pqr_text and
            "-1.0000 1.8000" in pqr_text,
            "PQR output did not preserve charge/radius columns")

    sdf_loaded = molshredder.invoke(
        "load", {"path": str(sdf_fixture), "name": "chemistry",
                 "file-format": "sdf"}
    )
    require(sdf_loaded["status"] == "ok" and
            sdf_loaded["data"]["structure_count"] == 2 and
            sdf_loaded["data"]["object_ids"] == [4, 5] and
            [item["object_name"] for item in sdf_loaded["data"]["objects"]] ==
            ["chemistry_1", "chemistry_2"],
            "Python SDF load did not expose every created object")
    activated = molshredder.invoke("object activate", {"id": "4"})
    require(activated["status"] == "ok", "first SDF record activation failed")
    sdf_arguments = {
        "path": str(sdf_output), "file-format": "sdf",
        "frames": "current", "overwrite": "true"
    }
    python_sdf_save = molshredder.invoke("save", sdf_arguments)
    require(python_sdf_save["status"] == "ok" and
            python_sdf_save["data"]["format"] == "sdf",
            "Python SDF save failed")
    sdf_script = (
        "format json\n"
        f'invoke "load" --file-format "xyz" --name "motion" '
        f'--path "{fixture}"\n'
        f'invoke "load" --file-format "xyz" --name "roundtrip" '
        f'--path "{output}"\n'
        f'invoke "load" --file-format "pqr" --name "electrostatics" '
        f'--path "{pqr_fixture}"\n'
        f'invoke "load" --file-format "sdf" --name "chemistry" '
        f'--path "{sdf_fixture}"\n'
        'invoke "object activate" --id "4"\n'
        f'invoke "save" --file-format "sdf" --frames "current" '
        f'--overwrite "true" --path "{sdf_output}"\n'
        "exit\n"
    )
    sdf_completed = subprocess.run(
        [str(cli_path), "console"], input=sdf_script, text=True,
        capture_output=True, check=True
    )
    sdf_envelopes = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in sdf_completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(len(sdf_envelopes) == 6 and
            sdf_loaded == sdf_envelopes[3] and
            python_sdf_save == sdf_envelopes[-1],
            "CLI and Python SDF load/save envelopes diverged")
    sdf_text = sdf_output.read_text(encoding="utf-8")
    require("M  CHG" in sdf_text and "M  ISO" in sdf_text and
            ">  <NOTE>\nline one\nline two" in sdf_text and
            sdf_text.endswith("$$$$\n"),
            "SDF frontend output lost chemistry or data fields")

    mol2_loaded = molshredder.invoke(
        "load", {"path": str(mol2_fixture), "name": "typed",
                 "file-format": "mol2"}
    )
    require(mol2_loaded["status"] == "ok" and
            mol2_loaded["data"]["structure_count"] == 2 and
            mol2_loaded["data"]["object_ids"] == [6, 7] and
            [item["object_name"] for item in mol2_loaded["data"]["objects"]] ==
            ["typed_1", "typed_2"],
            "Python MOL2 load did not expose both typed molecules")
    activated_mol2 = molshredder.invoke("object activate", {"id": "6"})
    require(activated_mol2["status"] == "ok",
            "first MOL2 molecule activation failed")
    mol2_arguments = {
        "path": str(mol2_output), "file-format": "mol2",
        "frames": "current", "precision": "6", "overwrite": "true"
    }
    python_mol2_save = molshredder.invoke("save", mol2_arguments)
    require(python_mol2_save["status"] == "ok" and
            python_mol2_save["data"]["format"] == "mol2",
            "Python MOL2 save failed")
    mol2_script = (
        "format json\n"
        f'invoke "load" --file-format "xyz" --name "motion" '
        f'--path "{fixture}"\n'
        f'invoke "load" --file-format "xyz" --name "roundtrip" '
        f'--path "{output}"\n'
        f'invoke "load" --file-format "pqr" --name "electrostatics" '
        f'--path "{pqr_fixture}"\n'
        f'invoke "load" --file-format "sdf" --name "chemistry" '
        f'--path "{sdf_fixture}"\n'
        f'invoke "load" --file-format "mol2" --name "typed" '
        f'--path "{mol2_fixture}"\n'
        'invoke "object activate" --id "6"\n'
        f'invoke "save" --file-format "mol2" --frames "current" '
        f'--overwrite "true" --path "{mol2_output}" --precision "6"\n'
        "exit\n"
    )
    mol2_completed = subprocess.run(
        [str(cli_path), "console"], input=mol2_script, text=True,
        capture_output=True, check=True
    )
    mol2_envelopes = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in mol2_completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(len(mol2_envelopes) == 7 and
            mol2_loaded == mol2_envelopes[4] and
            python_mol2_save == mol2_envelopes[-1],
            "CLI and Python MOL2 load/save envelopes diverged")
    mol2_text = mol2_output.read_text(encoding="utf-8")
    require("N.am 1 ACE -0.200000 BACKBONE" in mol2_text and
            "2 1 3 am AMIDE" in mol2_text and
            "@<TRIPOS>CRYSIN" in mol2_text,
            "MOL2 frontend output lost atom/bond typing or cell")

    gro_loaded = molshredder.invoke(
        "load", {"path": str(gro_fixture), "name": "gromacs",
                 "file-format": "gro"}
    )
    require(gro_loaded["status"] == "ok" and
            gro_loaded["data"]["object_id"] == 8 and
            gro_loaded["data"]["frame_count"] == 2,
            "Python GRO load did not retain both frames")
    gro_arguments = {
        "path": str(gro_output), "file-format": "gro",
        "frames": "all", "precision": "5", "overwrite": "true",
        "comment": "frontend GRO"
    }
    python_gro_save = molshredder.invoke("save", gro_arguments)
    require(python_gro_save["status"] == "ok" and
            python_gro_save["data"]["format"] == "gro" and
            python_gro_save["data"]["frame_count"] == 2,
            "Python GRO all-frame save failed")
    gro_script = (
        "format json\n"
        f'invoke "load" --file-format "xyz" --name "motion" '
        f'--path "{fixture}"\n'
        f'invoke "load" --file-format "xyz" --name "roundtrip" '
        f'--path "{output}"\n'
        f'invoke "load" --file-format "pqr" --name "electrostatics" '
        f'--path "{pqr_fixture}"\n'
        f'invoke "load" --file-format "sdf" --name "chemistry" '
        f'--path "{sdf_fixture}"\n'
        f'invoke "load" --file-format "mol2" --name "typed" '
        f'--path "{mol2_fixture}"\n'
        f'invoke "load" --file-format "gro" --name "gromacs" '
        f'--path "{gro_fixture}"\n'
        f'invoke "save" --file-format "gro" --frames "all" '
        f'--comment "frontend GRO" --overwrite "true" '
        f'--path "{gro_output}" --precision "5"\n'
        "exit\n"
    )
    gro_completed = subprocess.run(
        [str(cli_path), "console"], input=gro_script, text=True,
        capture_output=True, check=True
    )
    gro_envelopes = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in gro_completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(len(gro_envelopes) == 7 and
            gro_loaded == gro_envelopes[5] and
            python_gro_save == gro_envelopes[-1],
            "CLI and Python GRO load/save envelopes diverged")
    gro_text = gro_output.read_text(encoding="utf-8")
    require(gro_text.count("frontend GRO, t=") == 2 and
            "frontend GRO, t= 2.5" in gro_text and
            "  0.010000" in gro_text,
            "GRO frontend output lost frames, title or velocity")

    g96_loaded = molshredder.invoke(
        "load", {"path": str(g96_fixture), "name": "gromos",
                 "file-format": "g96"}
    )
    require(g96_loaded["status"] == "ok" and
            g96_loaded["data"]["object_id"] == 9 and
            g96_loaded["data"]["frame_count"] == 2,
            "Python G96 load did not retain both frames")
    g96_arguments = {
        "path": str(g96_output), "file-format": "g96",
        "frames": "all", "precision": "6", "overwrite": "true"
    }
    python_g96_save = molshredder.invoke("save", g96_arguments)
    require(python_g96_save["status"] == "ok" and
            python_g96_save["data"]["format"] == "g96" and
            python_g96_save["data"]["frame_count"] == 2,
            "Python G96 all-frame save failed")
    g96_script = (
        "format json\n"
        f'invoke "load" --file-format "xyz" --name "motion" '
        f'--path "{fixture}"\n'
        f'invoke "load" --file-format "xyz" --name "roundtrip" '
        f'--path "{output}"\n'
        f'invoke "load" --file-format "pqr" --name "electrostatics" '
        f'--path "{pqr_fixture}"\n'
        f'invoke "load" --file-format "sdf" --name "chemistry" '
        f'--path "{sdf_fixture}"\n'
        f'invoke "load" --file-format "mol2" --name "typed" '
        f'--path "{mol2_fixture}"\n'
        f'invoke "load" --file-format "gro" --name "gromacs" '
        f'--path "{gro_fixture}"\n'
        f'invoke "load" --file-format "g96" --name "gromos" '
        f'--path "{g96_fixture}"\n'
        f'invoke "save" --file-format "g96" --frames "all" '
        f'--overwrite "true" --path "{g96_output}" --precision "6"\n'
        "exit\n"
    )
    g96_completed = subprocess.run(
        [str(cli_path), "console"], input=g96_script, text=True,
        capture_output=True, check=True
    )
    g96_envelopes = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in g96_completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(len(g96_envelopes) == 8 and
            g96_loaded == g96_envelopes[6] and
            python_g96_save == g96_envelopes[-1],
            "CLI and Python G96 load/save envelopes diverged")
    g96_text = g96_output.read_text(encoding="utf-8")
    require(g96_text.startswith(
                "TITLE\nSynthetic G96 water trajectory\nEND\n") and
            g96_text.count("POSITION\n") == 2 and
            g96_text.count("VELOCITY\n") == 1 and
            "    0.110000000" in g96_text,
            "G96 frontend output lost fixed blocks, frames or velocity")

    psf_loaded = molshredder.invoke(
        "load", {"path": str(psf_fixture), "name": "topology",
                 "file-format": "psf"}
    )
    require(psf_loaded["status"] == "ok" and
            psf_loaded["data"]["object_id"] == 10 and
            psf_loaded["data"]["frame_count"] == 0,
            "Python PSF load did not expose topology-only zero-frame state")
    psf_arguments = {
        "path": str(psf_output), "file-format": "psf",
        "frames": "current", "precision": "6", "overwrite": "true"
    }
    python_psf_save = molshredder.invoke("save", psf_arguments)
    require(python_psf_save["status"] == "ok" and
            python_psf_save["data"]["format"] == "psf" and
            python_psf_save["data"]["frame_count"] == 0,
            "Python topology-only PSF save failed")
    psf_script = (
        "format json\n"
        f'invoke "load" --file-format "xyz" --name "motion" --path "{fixture}"\n'
        f'invoke "load" --file-format "xyz" --name "roundtrip" --path "{output}"\n'
        f'invoke "load" --file-format "pqr" --name "electrostatics" --path "{pqr_fixture}"\n'
        f'invoke "load" --file-format "sdf" --name "chemistry" --path "{sdf_fixture}"\n'
        f'invoke "load" --file-format "mol2" --name "typed" --path "{mol2_fixture}"\n'
        f'invoke "load" --file-format "gro" --name "gromacs" --path "{gro_fixture}"\n'
        f'invoke "load" --file-format "g96" --name "gromos" --path "{g96_fixture}"\n'
        f'invoke "load" --file-format "psf" --name "topology" --path "{psf_fixture}"\n'
        f'invoke "save" --file-format "psf" --frames "current" '
        f'--overwrite "true" --path "{psf_output}" --precision "6"\n'
        "exit\n"
    )
    psf_completed = subprocess.run(
        [str(cli_path), "console"], input=psf_script, text=True,
        capture_output=True, check=True
    )
    psf_envelopes = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in psf_completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(len(psf_envelopes) == 9 and
            psf_loaded == psf_envelopes[7] and
            python_psf_save == psf_envelopes[-1],
            "CLI and Python PSF topology-only load/save envelopes diverged")
    psf_text = psf_output.read_text(encoding="utf-8")
    require(psf_text.startswith("PSF EXT XPLOR CMAP\n") and
            "!NATOM" in psf_text and "!NPHI" in psf_text and
            "!NCRTERM" in psf_text,
            "PSF frontend output lost force-field topology sections")

    prmtop_loaded = molshredder.invoke(
        "load", {"path": str(prmtop_fixture), "name": "amber",
                 "file-format": "prmtop"}
    )
    require(prmtop_loaded["status"] == "ok" and
            prmtop_loaded["data"]["object_id"] == 11 and
            prmtop_loaded["data"]["frame_count"] == 0 and
            prmtop_loaded["data"]["format"] == "prmtop",
            "Python PRMTOP load did not expose topology-only state")
    rst7_attached = molshredder.invoke(
        "traj load", {"path": str(rst7_fixture), "file-format": "rst7",
                      "cache-mib": "1", "prefetch-frames": "0"}
    )
    require(rst7_attached["status"] == "ok" and
            rst7_attached["data"]["format"] == "rst7" and
            rst7_attached["data"]["frame_count"] == 1,
            "Python RST7 attachment failed")
    amber_script = psf_script.replace(
        "exit\n",
        f'invoke "load" --file-format "prmtop" --name "amber" '
        f'--path "{prmtop_fixture}"\n'
        f'invoke "traj load" --cache-mib "1" --file-format "rst7" '
        f'--path "{rst7_fixture}" --prefetch-frames "0"\nexit\n'
    )
    amber_completed = subprocess.run(
        [str(cli_path), "console"], input=amber_script, text=True,
        capture_output=True, check=True
    )
    amber_envelopes = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in amber_completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(len(amber_envelopes) == 11 and
            prmtop_loaded == amber_envelopes[9] and
            rst7_attached == amber_envelopes[10],
            "CLI and Python PRMTOP/RST7 operation envelopes diverged")

    pdb_loaded = molshredder.invoke(
        "load", {"path": str(pdb_fixture), "name": "pdb_models",
                 "file-format": "pdb"}
    )
    require(pdb_loaded["status"] == "ok" and
            pdb_loaded["data"]["object_id"] == 12 and
            pdb_loaded["data"]["frame_count"] == 2,
            "Python PDB load did not retain both models")
    pdb_arguments = {
        "path": str(pdb_output), "file-format": "pdb",
        "frames": "all", "precision": "3", "overwrite": "true",
        "comment": "frontend parity"
    }
    python_pdb_save = molshredder.invoke("save", pdb_arguments)
    require(python_pdb_save["status"] == "ok" and
            python_pdb_save["data"]["format"] == "pdb" and
            python_pdb_save["data"]["frame_count"] == 2,
            "Python PDB multi-model save failed")
    pdb_script = amber_script.replace(
        "exit\n",
        f'invoke "load" --file-format "pdb" --name "pdb_models" '
        f'--path "{pdb_fixture}"\n'
        f'invoke "save" --comment "frontend parity" --file-format "pdb" '
        f'--frames "all" --overwrite "true" --path "{pdb_output}" '
        f'--precision "3"\nexit\n'
    )
    pdb_completed = subprocess.run(
        [str(cli_path), "console"], input=pdb_script, text=True,
        capture_output=True, check=True
    )
    pdb_envelopes = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in pdb_completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(len(pdb_envelopes) == 13 and
            pdb_loaded == pdb_envelopes[11] and
            python_pdb_save == pdb_envelopes[12],
            "CLI and Python PDB load/save envelopes diverged")
    pdb_text = pdb_output.read_text(encoding="utf-8")
    require(pdb_text.count("MODEL ") == 2 and
            "CRYST1   10.000   11.000   12.000" in pdb_text and
            "CONECT    1    2" in pdb_text and pdb_text.endswith("END\n"),
            "PDB frontend output lost models, cell or connectivity")

    mmcif_loaded = molshredder.invoke(
        "load", {"path": str(mmcif_fixture), "name": "cif_models",
                 "file-format": "mmcif"}
    )
    require(mmcif_loaded["status"] == "ok" and
            mmcif_loaded["data"]["object_id"] == 13 and
            mmcif_loaded["data"]["frame_count"] == 2,
            "Python mmCIF load did not retain both models")
    mmcif_arguments = {
        "path": str(mmcif_output), "file-format": "mmcif",
        "frames": "all", "precision": "4", "overwrite": "true",
        "comment": "frontend parity"
    }
    python_mmcif_save = molshredder.invoke("save", mmcif_arguments)
    require(python_mmcif_save["status"] == "ok" and
            python_mmcif_save["data"]["format"] == "mmcif" and
            python_mmcif_save["data"]["frame_count"] == 2,
            "Python mmCIF multi-model save failed")
    mmcif_script = pdb_script.replace(
        "exit\n",
        f'invoke "load" --file-format "mmcif" --name "cif_models" '
        f'--path "{mmcif_fixture}"\n'
        f'invoke "save" --comment "frontend parity" --file-format "mmcif" '
        f'--frames "all" --overwrite "true" --path "{mmcif_output}" '
        f'--precision "4"\nexit\n'
    )
    mmcif_completed = subprocess.run(
        [str(cli_path), "console"], input=mmcif_script, text=True,
        capture_output=True, check=True
    )
    mmcif_envelopes = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in mmcif_completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(len(mmcif_envelopes) == 15 and
            mmcif_loaded == mmcif_envelopes[13] and
            python_mmcif_save == mmcif_envelopes[14],
            "CLI and Python mmCIF load/save envelopes diverged")
    mmcif_text = mmcif_output.read_text(encoding="utf-8")
    require(mmcif_text.startswith("data_MS02\n") and
            mmcif_text.count("conn1 covale") == 1 and
            "_atom_site.pdbx_PDB_model_num" in mmcif_text,
            "mmCIF frontend output lost block, models or connectivity")
    print("save-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
