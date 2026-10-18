#!/usr/bin/env python3
"""SEQ-318 S4/S5: VMD selection keywords (residuetype, backbonetype, structure,
helix/sheet words, vx/vy/vz), the keyword catalog and `object bonds`."""

import importlib
import pathlib
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def ok(result: dict, what: str) -> dict:
    require(result["status"] == "ok", f"{what} failed: {result.get('error')}")
    return result["data"]


def main() -> int:
    module_path = pathlib.Path(sys.argv[1]).resolve()
    fixtures = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")

    ok(m.invoke("load", {"path": str(fixtures / "1ubq.pdb"), "name": "ubq"}), "load 1ubq")

    def count(selection: str) -> int:
        return ok(m.invoke("object count", {"selection": selection}), selection)["atom_count"]

    def error_of(selection: str) -> str:
        result = m.invoke("object count", {"selection": selection})
        require(result["status"] == "error", f"{selection!r} should fail")
        return result["error"]["message"]

    total = count("all")
    # residuetype: protein / nucleic / waters / nothing partition the atoms
    protein, nucleic = count("residuetype protein"), count("residuetype nucleic")
    waters, nothing = count("residuetype waters"), count("residuetype nothing")
    require(protein == count("polymer.protein") and waters == count("solvent"), "residuetype classes")
    require(protein + nucleic + waters + nothing == total, "residuetype values partition the atoms")
    require(count("residuetype protein waters") == protein + waters, "value lists are unions")
    require(count("residuetype protein and name CA") == count("polymer.protein and name CA") == 76,
            "a value keyword combines with and")

    # backbonetype: proteinback / nucleicback / hydrogen / normal partition the atoms
    protein_back = count("backbonetype proteinback")
    require(protein_back == count("polymer.protein and backbone") > 0, "proteinback")
    require(count("backbonetype nucleicback") == 0 and count("backbonetype hydrogen") == count("hydrogens"),
            "nucleicback and hydrogen")
    require(protein_back + count("backbonetype nucleicback") + count("backbonetype hydrogen") +
            count("backbonetype normal") == total, "backbonetype values partition the atoms")

    # structure classes answered from the file-provided assignment
    require(count("structure H") == count("ss H") == count("helix") == count("alpha_helix"), "helix")
    for word in ("sheet", "betasheet", "beta_sheet", "extended_beta"):
        require(count(word) == count("structure E") == count("ss S") > 0, f"{word} selects the sheet atoms")
    require(count("structure H E") == count("ss H") + count("ss S"), "structure value lists")
    require("computed" in error_of("pi_helix") and "computed" in error_of("structure G"),
            "classes that need a computed assignment are rejected, not silently empty")
    require("requires a value" in error_of("residuetype"), "a value keyword needs a value")

    # velocities are absent in a PDB; the error says so
    require("velocities" in error_of("vx > 0"), "vx needs a frame with velocities")

    # keyword catalog: every example parses (velocity examples fail only for missing data)
    table = ok(m.invoke("select keywords"), "select keywords")["table"]
    names = {row[0] for row in table["rows"]}
    for needed in ("residuetype", "backbonetype", "structure", "sheet", "vx", "name", "polymer.protein"):
        require(needed in names, f"{needed} is listed")
    for keyword, _kind, _aliases, example in table["rows"]:
        result = m.invoke("object count", {"selection": example})
        if result["status"] == "error":
            message = result["error"]["message"]
            require("unknown selection keyword" not in message and "syntax" not in message and
                    "unexpected" not in message and "expected selection" not in message,
                    f"catalog example for {keyword!r} does not parse: {example!r}: {message}")

    # object bonds (atomselect getbonds / getbondorders / getbondtypes)
    ok(m.invoke("load", {"path": str(fixtures / "chemistry.sdf"), "name": "chem"}), "load chem")
    bonds = ok(m.invoke("object bonds"), "object bonds")
    rows = bonds["table"]["rows"]
    require(bonds["returned_count"] == len(rows) > 0 and bonds["truncated"] is False, "all bonds listed")
    require(all(row[6] in ("single", "double", "triple", "aromatic", "amide", "zero", "query", "unknown")
                for row in rows), "bond orders are named")
    first = rows[0]
    only = ok(m.invoke("object bonds", {"selection": f"index {first[0]}"}), "bonds of one atom")
    require(only["returned_count"] >= 1 and all(first[0] in (r[0], r[1]) for r in only["table"]["rows"]),
            "only bonds touching the selection")
    require(rows[0][6] == "triple", "the SDF fixture's bond order is reported")
    ok(m.invoke("load", {"path": str(fixtures / "chemistry.mol2"), "name": "mol2"}), "load mol2")
    short = ok(m.invoke("object bonds", {"limit": "3"}), "limited")
    require(short["returned_count"] == 3 and short["truncated"] is True, "limit truncates")
    require(ok(m.invoke("object bonds"), "all bonds")["returned_count"] == 6, "mol2 fixture has 6 bonds")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
