#!/usr/bin/env python3
"""SEQ-319 S3: PDBQT, CHARMM COR/CRD, ChemDraw 3D CC1 and VDB (VIPERdb) structures.

The fixtures under tests/io/fixtures/legacy are synthetic; the expected reading
(elements, charges, chain/segment, coordinates, bond count) was cross-checked
with Open-Source PyMOL 3.1.0.
"""

import importlib
import pathlib
import shutil
import sys
import tempfile


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def ok(result: dict, what: str) -> dict:
    require(result["status"] == "ok", f"{what} failed: {result.get('error')}")
    return result["data"]


def fails(result: dict, what: str) -> str:
    require(result["status"] == "error", f"{what} should have failed")
    return result["error"]["message"]


def main() -> int:
    module_path = pathlib.Path(sys.argv[1]).resolve()
    fixtures = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")
    legacy = fixtures / "legacy"

    def rows(name: str, fixture: str, file_format: str = "auto", source: pathlib.Path | None = None):
        ok(m.invoke("load", {"path": str((source or legacy) / fixture), "name": name, "file-format": file_format}),
           f"load {fixture}")
        table = ok(m.invoke("object atoms", {"properties": "name,resn,resi,chain,element,partial_charge,x,y,z,id",
                                             "limit": "1000"}), "atoms")["table"]["rows"]
        return {row[0]: row for row in table}, table

    # PDBQT: AutoDock types set the element (A -> C; OA/NA/HD/SA -> first letter) and carry partial charges
    by_name, table = rows("lig", "lig.pdbqt")
    require(len(table) == 8, "eight atoms")
    expected_elements = {"C": "C", "C1": "C", "O1": "O", "N1": "N", "H1": "H", "S1": "S", "CL1": "Cl", "ZN": "Zn"}
    require({n: r[4] for n, r in by_name.items()} == expected_elements, f"AutoDock types give the elements: {by_name}")
    charges = {n: round(r[5], 3) for n, r in by_name.items()}
    require(charges == {"C": 0.12, "C1": -0.05, "O1": -0.3, "N1": -0.25, "H1": 0.18, "S1": -0.1, "CL1": 0.02, "ZN": 2.0},
            f"signed charges survive (leading '+' included): {charges}")
    require(by_name["ZN"][3] == "B" and by_name["C"][3] == "A", "chains come from the PDB columns")
    require(abs(by_name["O1"][6] - 2.1) < 1e-6 and abs(by_name["N1"][8] - 0.3) < 1e-6, "coordinates")
    count = lambda sel: ok(m.invoke("object count", {"selection": sel}), sel)["atom_count"]
    require(count("hetatm") == 1 and count("partial_charge > 0.1") == 3, "hetero flag and charge selection")
    ok(m.invoke("object delete", {"object": "lig"}), "cleanup")

    # CHARMM COR: two title lines, atom lines, the segment id gives the chain's first letter (as PyMOL does)
    by_name, table = rows("cor", "t.cor")
    require(len(table) == 5 and [r[0] for r in table] == ["N", "CA", "C", "N", "CA"], "names in file order")
    require({r[1] for r in table} == {"ALA", "GLY"} and [r[2] for r in table] == ["1", "1", "1", "2", "2"], "residues")
    require({r[3] for r in table} == {"P"} and abs(table[4][6] - 4.6) < 1e-6, "chain from segment, coordinates")
    # `.crd` with CHARMM titles is the same format; `.map`-style ambiguity is resolved by the file name only
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        shutil.copy(legacy / "t.cor", root / "same.crd")
        _, table2 = rows("crd", "same.crd", source=root)
        require(len(table2) == 5, ".crd with CHARMM title lines loads as COR")
        shutil.copy(fixtures / "1ubq.pdb", root / "capsid.vdb")
        _, vdb = rows("vdb", "capsid.vdb", source=root)
        require(len(vdb) == 660, "a VIPERdb .vdb file is read as PDB")

        # CC1: symbol, id, coordinates, type and a bond list per atom
        by_name, table = rows("cc1", "t.cc1")
        require([r[4] for r in table] == ["C", "C", "O", "H"] and abs(table[2][6] + 0.6) < 1e-6,
                "elements and coordinates")
        require(ok(m.invoke("object bonds"), "bonds")["returned_count"] == 3, "bonds from the neighbour lists")

        # damaged input is rejected, never crashes
        samples = {"pdbqt": "lig.pdbqt", "cor": "t.cor", "cc1": "t.cc1"}
        for fmt, fixture in samples.items():
            text = (legacy / fixture).read_text()
            for label, blob in (("truncated", text[: len(text) // 2]), ("garbage", "\x00\x01 not a structure\n" * 3)):
                bad = root / f"{label}.{fmt}"
                bad.write_text(blob)
                result = m.invoke("load", {"path": str(bad), "name": "bad", "file-format": fmt})
                # A PDBQT cut at a record boundary is still a valid, shorter PDBQT file.
                if fmt == "pdbqt" and label == "truncated":
                    continue
                require(result["status"] == "error", f"{fmt} {label} must fail")
        wrong = root / "count.cor"
        wrong.write_text("* t\n* t\n 9\n    1    1 ALA  N      0.00000   0.00000   0.00000 PROA 1   0.00000\n")
        require("ends before" in fails(m.invoke("load", {"path": str(wrong), "name": "bad", "file-format": "cor"}), "short"),
                "a short COR file reports the missing atoms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
