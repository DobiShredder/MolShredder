#!/usr/bin/env python3
"""SEQ-319 S4: CML and PDBML (PDBx/XML) structures and the safe XML parser.

The fixtures under tests/io/fixtures/xml are synthetic; atom/bond/chain/residue mapping was cross-checked
with Open-Source PyMOL 3.1.0 (lazyio.load_cml / load_pdbml). CML is a superset: namespaces, the
atomRefs2 spelling, array form and aromatic orders are accepted.
"""

import importlib
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
    fixtures = pathlib.Path(sys.argv[2]).resolve() / "xml"
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")

    def load(name: str, path: pathlib.Path, file_format: str = "auto") -> dict:
        return m.invoke("load", {"path": str(path), "name": name, "file-format": file_format})

    atoms = lambda: ok(m.invoke("object atoms", {"properties": "name,resn,resi,chain,element,formal_charge,b,q,x,y,z",
                                                 "limit": "1000"}), "atoms")["table"]["rows"]

    # CML: two molecules become two objects, bonds from atomRefs2, planar x2/y2 gets z = 0
    ok(load("cml", fixtures / "two.cml"), "load two.cml")
    objects = ok(m.invoke("object list"), "list")["table"]["rows"]
    names = [row[1] for row in objects]
    require(len(names) == 2, f"two molecules become two objects: {names}")
    ok(m.invoke("object delete", {"object": names[1]}), "drop the array-form molecule")
    rows = atoms()
    require([r[0] for r in rows] == ["a1", "a2", "a3", "a4"] and [r[4] for r in rows] == ["C", "C", "O", "N"], f"atoms: {rows}")
    require(rows[2][5] == -1 and abs(rows[3][8] - 3.0) < 1e-6 and abs(rows[3][9] + 1.0) < 1e-6 and rows[3][10] == 0.0,
            "formal charge and planar coordinates")
    bonds = ok(m.invoke("object bonds"), "bonds")
    require(bonds["returned_count"] == 3, "three bonds")
    ok(m.invoke("object delete", {"object": names[0]}), "cleanup")

    # array form of the second molecule: atomID/elementType/x3.. lists and atomRef1/atomRef2 bonds
    ok(load("cml", fixtures / "two.cml"), "reload")
    ok(m.invoke("object delete", {"object": names[0]}), "drop first")
    rows = atoms()
    require([r[0] for r in rows] == ["x1", "x2", "x3"] and [r[4] for r in rows] == ["Cl", "Na", "H"], f"array atoms: {rows}")
    require(abs(rows[2][10] + 1.0) < 1e-6, "array coordinates")
    require(ok(m.invoke("object bonds"), "bonds")["returned_count"] == 2, "array bonds")
    ok(m.invoke("object delete", {"object": names[1]}), "cleanup")

    # PDBML: auth identifiers win, occupancy/B and the unit cell survive, HETATM sets the hetero flag
    ok(load("pdbml", fixtures / "mini.pdbml"), "load mini.pdbml")
    rows = atoms()
    require([r[0] for r in rows] == ["N", "CA", "O"], f"names: {rows}")
    require([r[1] for r in rows] == ["GLY", "GLY", "HOH"] and [r[2] for r in rows] == ["5", "5", "101"], "residues use auth ids")
    require({r[3] for r in rows} == {"B"}, "chain is auth_asym_id")
    require(abs(rows[1][6] - 12.5) < 1e-6 and abs(rows[1][7] - 0.5) < 1e-6, "B-factor and occupancy")
    require(abs(rows[2][8] - 5.0) < 1e-6, "coordinates")
    count = lambda sel: ok(m.invoke("object count", {"selection": sel}), sel)["atom_count"]
    require(count("hetatm") == 1, "HETATM flag")
    ok(m.invoke("object delete", {"object": "pdbml"}), "cleanup")

    # auto detection from content, regardless of the file name
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        (root / "guess.xml").write_text((fixtures / "mini.pdbml").read_text())
        ok(load("guess", root / "guess.xml"), "auto-detect PDBML in .xml")
        require(len(atoms()) == 3, "auto-detected PDBML")
        ok(m.invoke("object delete", {"object": "guess"}), "cleanup")
        (root / "guess2.xml").write_text((fixtures / "two.cml").read_text())
        ok(load("guess2", root / "guess2.xml"), "auto-detect CML in .xml")
        for name in [row[1] for row in ok(m.invoke("object list"), "list")["table"]["rows"]]:
            ok(m.invoke("object delete", {"object": name}), "cleanup")

        # hostile and damaged XML is rejected with a message, never crashes or expands entities
        cases = {
            "doctype_subset": ('<?xml version="1.0"?><!DOCTYPE cml [<!ENTITY x "boom">]><cml><molecule/></cml>', "cml", "internal subset"),
            "entity_use": ('<cml><molecule id="&x;"><atomArray/></molecule></cml>', "cml", "undefined entity"),
            "mismatch": ('<cml><molecule><atomArray></molecule></cml>', "cml", "mismatched"),
            "no_root": ("just text", "cml", "root"),
            "wrong_root": ('<html><body/></html>', "cml", "not <cml>"),
            "no_atoms": ('<cml><molecule id="e"/></cml>', "cml", "no <molecule>"),
            "bad_coordinate": ('<cml><molecule><atomArray><atom id="a" elementType="C" x3="1" y3="q" z3="0"/></atomArray></molecule></cml>',
                               "cml", "invalid"),
            "unknown_bond_atom": ('<cml><molecule><atomArray><atom id="a" elementType="C" x3="1" y3="0" z3="0"/></atomArray>'
                                  '<bondArray><bond atomRefs2="a b"/></bondArray></molecule></cml>', "cml", "unknown atom"),
            "pdbml_wrong_root": ('<cml/>', "pdbml", "datablock"),
            "pdbml_no_atoms": ('<PDBx:datablock xmlns:PDBx="x"><PDBx:cellCategory/></PDBx:datablock>', "pdbml", "atom_site"),
        }
        for label, (text, fmt, fragment) in cases.items():
            bad = root / f"{label}.xml"
            bad.write_text(text)
            message = fails(load("bad", bad, fmt), label)
            require(fragment.lower() in message.lower(), f"{label}: expected '{fragment}' in '{message}'")
        # invalid UTF-8 inside file content must come back as text with replacement characters, never as an exception
        binary = root / "binary.cml"
        binary.write_bytes(b'<cml><molecule id="m\xff\xfe"><atomArray><atom id="a\xff" elementType="C" x3="0" y3="0" z3="0"/></atomArray>'
                           b'<bondArray><bond atomRefs2="a\xff zz\xfe"/></bondArray></molecule></cml>')
        message = fails(load("bin", binary, "cml"), "invalid UTF-8 diagnostic")
        require("unknown atom" in message, f"diagnostic survives invalid bytes: {message!r}")
        binary.write_bytes(b'<cml><molecule id="m"><atomArray><atom id="a\xff" elementType="C" x3="0" y3="0" z3="0"/></atomArray></molecule></cml>')
        ok(load("bin", binary, "cml"), "atom id with invalid UTF-8 loads")
        require(len(atoms()) == 1, "one atom loaded")
        ok(m.invoke("object delete", {"object": "bin"}), "cleanup")
        deep = root / "deep.xml"
        deep.write_text("<cml>" + "<a>" * 500 + "</a>" * 500 + "</cml>")
        require("nesting" in fails(load("bad", deep, "cml"), "deep").lower(), "deep nesting is bounded")
        half = (fixtures / "mini.pdbml").read_text()
        cut = root / "cut.pdbml"
        cut.write_text(half[: len(half) // 2])
        fails(load("bad", cut, "pdbml"), "truncated PDBML")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
