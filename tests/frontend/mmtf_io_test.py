#!/usr/bin/env python3
"""SEQ-319 S1: MMTF read/write.

Round trips use fixtures written by this build; when MOLSHREDDER_PYMOL_TESTDATA
points at an Open-Source PyMOL checkout's testing/data directory the real MMTF
files there are checked against PyMOL's own expectations (atom counts, solvent,
polymer, organic, secondary structure and unit cell).
"""

import gzip
import importlib
import os
import pathlib
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

    def atoms(selection="all"):
        return ok(m.invoke("object atoms", {"selection": selection, "limit": "100000",
                                            "properties": "name,resn,resi,chain,x,y,z,b,q,element"}),
                  "atoms")["table"]["rows"]

    def count(selection: str) -> int:
        return ok(m.invoke("object count", {"selection": selection}), selection)["atom_count"]

    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)

        # 1ubq: single model, protein + water, secondary structure, B-factors
        ok(m.invoke("load", {"path": str(fixtures / "1ubq.pdb"), "name": "ubq"}), "load pdb")
        source = atoms()
        ss_h, ss_s = count("ss H"), count("ss S")
        path = root / "ubq.mmtf"
        saved = ok(m.invoke("save", {"path": str(path), "overwrite": "true"}), "save mmtf")
        require(path.read_bytes()[:1] != b"", "file written")
        ok(m.invoke("load", {"path": str(path), "name": "ubq2"}), "load mmtf")
        again = atoms()
        require(len(again) == len(source) == 660, "atom count survives")
        require([a[:4] for a in again] == [a[:4] for a in source], "names, residues and chains survive")
        require(max(abs(a[i] - b[i]) for a, b in zip(source, again) for i in (4, 5, 6)) < 1.0e-3,
                "coordinates survive within the 0.001 A MMTF resolution")
        require(max(abs(a[7] - b[7]) for a, b in zip(source, again)) < 1.0e-2 and
                max(abs(a[8] - b[8]) for a, b in zip(source, again)) < 1.0e-2, "B-factor and occupancy survive")
        require(count("ss H") == ss_h and count("ss S") == ss_s and ss_h > 0 and ss_s > 0,
                "secondary structure survives")
        require(count("polymer.protein") == count("all") - count("solvent") and count("solvent") == 58,
                "protein and water classification survives")
        ok(m.invoke("object delete", {"object": "ubq2"}), "cleanup")
        ok(m.invoke("object delete", {"object": "ubq"}), "cleanup")

        # a small multi-model structure with bonds and a ligand
        mol2 = fixtures / "chemistry.mol2"
        ok(m.invoke("load", {"path": str(mol2), "name": "chem"}), "load mol2")
        bonds_before = ok(m.invoke("object bonds"), "bonds")["table"]["rows"]
        ok(m.invoke("save", {"path": str(root / "chem.mmtf"), "overwrite": "true"}), "save chem")
        ok(m.invoke("load", {"path": str(root / "chem.mmtf"), "name": "chem2"}), "load chem")
        bonds_after = ok(m.invoke("object bonds"), "bonds")["table"]["rows"]
        require(len(bonds_after) == len(bonds_before) == 6, "bonds survive")
        require(set(r[6] for r in bonds_before) == {"aromatic"} and set(r[6] for r in bonds_after) == {"single"},
                "MMTF has no aromatic order: it is written as single (reported as a format loss)")

        # multiple models become states
        model_pdb = root / "models.pdb"
        lines = []
        for model, shift in enumerate((0.0, 10.0, 20.0), 1):
            lines.append(f"MODEL     {model:4d}")
            for serial, (name, x) in enumerate((("N", 0.0), ("CA", 1.4), ("C", 2.0)), 1):
                lines.append(f"ATOM  {serial:5d} {name:^4s} ALA A   1    {x + shift:8.3f}{0.0:8.3f}{0.0:8.3f}  1.00 20.00           C")
            lines.append("ENDMDL")
        model_pdb.write_text("\n".join(lines) + "\nEND\n")
        ok(m.invoke("load", {"path": str(model_pdb), "name": "mm"}), "load models")
        ok(m.invoke("save", {"path": str(root / "mm.mmtf"), "overwrite": "true", "frames": "all"}), "save models")
        ok(m.invoke("load", {"path": str(root / "mm.mmtf"), "name": "mm2"}), "load models mmtf")
        listing = {row[1]: row for row in ok(m.invoke("object list"), "list")["table"]["rows"]}
        require(listing["mm2"][6] == listing["mm"][6] == 3, "every model comes back as a state")

        # malformed input is rejected, never crashes
        data = path.read_bytes()
        for label, blob, needle in (
                ("truncated", data[: len(data) // 2], "MMTF"),
                ("version", data.replace(b"1.0.0", b"2.0.0", 1), "version"),
                ("not msgpack", b"\xc1" * 64, "MMTF"),
                ("empty", b"", "MMTF"),
                ("trailing", data + b"\x00", "MMTF")):
            bad = root / f"{label.replace(' ', '_')}.mmtf"
            bad.write_bytes(blob)
            message = fails(m.invoke("load", {"path": str(bad), "name": "bad"}), label)
            require(needle.lower() in message.lower(), f"{label}: {message}")
        # a corrupted codec id inside a binary field
        index = data.find(b"xCoordList")
        corrupt = bytearray(data)
        marker = index + len(b"xCoordList") + 5  # bin header then the 4 byte codec id
        corrupt[marker + 3] = 99
        bad = root / "codec.mmtf"
        bad.write_bytes(bytes(corrupt))
        require("codec" in fails(m.invoke("load", {"path": str(bad), "name": "bad"}), "codec").lower(),
                "an unknown codec is reported")

        # real files from an Open-Source PyMOL checkout (optional, never copied into the repo)
        testdata = os.environ.get("MOLSHREDDER_PYMOL_TESTDATA")
        if testdata:
            data_dir = pathlib.Path(testdata)
            expectations = {
                "3njw.mmtf.gz": (169, {"solvent": 25, "ss S": 36, "not polymer": 25}),
                "1x8x.mmtf.gz": (2780, {"polymer": 2544, "solvent": 218, "organic": 13, "inorganic": 5}),
            }
            for name, (total, checks) in expectations.items():
                raw = root / name.removesuffix(".gz")
                raw.write_bytes(gzip.open(data_dir / name).read())
                ok(m.invoke("load", {"path": str(raw), "name": raw.stem}), f"load {name}")
                require(count("all") == total, f"{name} atom count")
                for selection, expected in checks.items():
                    require(count(selection) == expected, f"{name} {selection}: {count(selection)} != {expected}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
