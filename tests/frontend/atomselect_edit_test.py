#!/usr/bin/env python3
"""SEQ-318 S4: atom coordinate edits (VMD atomselect moveto/moveby/lmoveto/lmoveby/move,
PyMOL translate/transform_selection) through `edit move-atoms`."""

import importlib
import math
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


ATOMS = [("N", "ALA", 1, 0.0, 0.0, 0.0, "N"), ("CA", "ALA", 1, 1.0, 2.0, 3.0, "C"),
         ("C", "ALA", 1, 4.0, 5.0, 6.0, "C"), ("O", "ALA", 1, -1.0, 0.5, 2.0, "O")]


def main() -> int:
    module_path = pathlib.Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")
    with tempfile.TemporaryDirectory() as directory:
        path = pathlib.Path(directory) / "one.pdb"
        path.write_text("\n".join(
            f"ATOM  {i:5d} {n:^4s} {r:3s} A{s:4d}    {x:8.3f}{y:8.3f}{z:8.3f}  1.00 20.00          {e:>2s}"
            for i, (n, r, s, x, y, z, e) in enumerate(ATOMS, 1)) + "\nEND\n")
        ok(m.invoke("load", {"path": str(path), "name": "one"}), "load")

        def coords(selection="all"):
            rows = ok(m.invoke("object atoms", {"selection": selection, "properties": "name,x,y,z"}),
                      "atoms")["table"]["rows"]
            return {row[0]: tuple(round(float(v), 3) for v in row[1:4]) for row in rows}

        original = coords()
        # moveby: one vector for every selected atom
        moved = ok(m.invoke("edit move-atoms", {"selection": "name CA+C", "mode": "add", "values": "1 -2 0.5"}),
                   "move by")
        require(moved["moved_atom_count"] == 2, "two atoms moved")
        after = coords()
        require(after["CA"] == (2.0, 0.0, 3.5) and after["C"] == (5.0, 3.0, 6.5), f"moveby: {after}")
        require(after["N"] == original["N"] and after["O"] == original["O"], "other atoms untouched")
        ok(m.invoke("edit undo"), "undo")
        require(coords() == original, "undo restores the coordinates")
        ok(m.invoke("edit redo"), "redo")
        require(coords() == after, "redo reapplies the edit")
        ok(m.invoke("edit undo"), "undo again")

        # moveto: every selected atom gets the same position (VMD semantics)
        ok(m.invoke("edit move-atoms", {"selection": "name CA+C", "mode": "set", "values": "7,8,9"}), "moveto")
        require(coords()["CA"] == (7.0, 8.0, 9.0) and coords()["C"] == (7.0, 8.0, 9.0), "moveto collapses atoms")
        ok(m.invoke("edit undo"), "undo")

        # lmoveto / lmoveby: one vector per selected atom (atom order)
        ok(m.invoke("edit move-atoms", {"selection": "name CA+C", "mode": "set",
                                         "values": "1 1 1; 2 2 2"}), "lmoveto")
        require(coords()["CA"] == (1.0, 1.0, 1.0) and coords()["C"] == (2.0, 2.0, 2.0), "per-atom set")
        ok(m.invoke("edit undo"), "undo")
        ok(m.invoke("edit move-atoms", {"selection": "name CA+C", "mode": "add",
                                         "values": "1 0 0 0 1 0"}), "lmoveby")
        require(coords()["CA"] == (2.0, 2.0, 3.0) and coords()["C"] == (4.0, 6.0, 6.0), "per-atom offset")
        ok(m.invoke("edit undo"), "undo")

        # transform: 90 degree rotation about z plus a translation
        matrix = "0 -1 0 10  1 0 0 20  0 0 1 30  0 0 0 1"
        ok(m.invoke("edit move-atoms", {"selection": "all", "mode": "transform", "values": matrix}), "transform")
        rotated = coords()
        require(rotated["CA"] == (10.0 - 2.0, 20.0 + 1.0, 33.0), f"rotation + translation: {rotated['CA']}")
        require(all(math.isfinite(v) for xyz in rotated.values() for v in xyz), "finite result")
        ok(m.invoke("edit undo"), "undo")

        # state scope on a single-state object
        ok(m.invoke("edit move-atoms", {"selection": "all", "mode": "add", "values": "1 0 0", "state": "all"}),
           "all states")
        ok(m.invoke("edit undo"), "undo")
        ok(m.invoke("edit move-atoms", {"selection": "all", "mode": "add", "values": "1 0 0", "state": "1"}),
           "state 1")
        ok(m.invoke("edit undo"), "undo")
        require("state" in fails(m.invoke("edit move-atoms", {"selection": "all", "values": "1 0 0", "state": "2"}),
                                 "missing state"), "a missing state is rejected")

        # errors
        require("3 numbers" in fails(m.invoke("edit move-atoms", {"selection": "name CA+C", "values": "1 2 3 4"}),
                                     "wrong count"), "wrong value count")
        require("16" in fails(m.invoke("edit move-atoms", {"selection": "all", "mode": "transform", "values": "1 2 3"}),
                              "short matrix"), "matrix size")
        require("affine" in fails(m.invoke("edit move-atoms", {"selection": "all", "mode": "transform",
                                                                "values": "1 0 0 0  0 1 0 0  0 0 1 0  1 0 0 1"}),
                                  "projective matrix"), "only affine matrices")
        require("numbers" in fails(m.invoke("edit move-atoms", {"selection": "all", "values": "1 x 3"}), "text"),
                "non-numeric values")
        require("no atoms" in fails(m.invoke("edit move-atoms", {"selection": "name ZZ", "values": "1 2 3"}), "empty"),
                "empty selection")
        ok(m.invoke("object fix"), "fix")
        require("fixed" in fails(m.invoke("edit move-atoms", {"selection": "all", "values": "1 2 3"}), "fixed"),
                "fixed objects are not moved")
        ok(m.invoke("object free"), "free")
        require(coords() == original, "the failed edits changed nothing")

        # session replay
        edited = ok(m.invoke("edit move-atoms", {"selection": "name O", "mode": "add", "values": "0 0 1"}), "edit")
        require(edited["moved_atom_count"] == 1, "one atom")
        expected = coords()
        session = pathlib.Path(directory) / "move.mssession"
        ok(m.invoke("session save", {"path": str(session)}), "save")
        ok(m.invoke("edit move-atoms", {"selection": "all", "values": "5 5 5"}), "scramble")
        ok(m.invoke("session load", {"path": str(session)}), "load")
        require(coords() == expected, f"the edit replays from the session: {coords()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
