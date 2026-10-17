#!/usr/bin/env python3
"""SEQ-318 S1: split/join/delete of object states (object.state.* rows).

Expected values for the PyMOL-compatible parts were captured from PyMOL 3.1.0
(`split_states`, `delete_states`, `join_states`) on the same synthetic
structures. Known, documented differences are asserted too: deleting every state
and joining non-identical objects with --mode identical are errors here, and
discrete / sequence-alignment joins are not offered.
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


BASE = [("N", "ALA", 1, 0.0, 0.0, 0.0, "N"), ("CA", "ALA", 1, 1.4, 0.0, 0.0, "C"),
        ("C", "ALA", 1, 2.0, 1.4, 0.0, "C"), ("O", "ALA", 1, 1.5, 2.4, 0.0, "O"),
        ("N", "GLY", 2, 3.3, 1.2, 0.1, "N"), ("CA", "GLY", 2, 4.6, 1.9, 0.2, "C")]
# permuted, GLY CA missing, extra ALA CB
PERMUTED = [BASE[1], BASE[0], BASE[3], BASE[2], BASE[4], ("CB", "ALA", 1, 1.9, -1.3, 0.0, "C")]


def pdb(shifts, atoms) -> str:
    lines = []
    for model, shift in enumerate(shifts, 1):
        lines.append(f"MODEL     {model:4d}")
        for serial, (name, res, resi, x, y, z, element) in enumerate(atoms, 1):
            lines.append(f"ATOM  {serial:5d} {name:^4s} {res:3s} A{resi:4d}    {x + shift:8.3f}{y:8.3f}"
                         f"{z:8.3f}  1.00 20.00          {element:>2s}")
        lines.append("ENDMDL")
    lines.append("END")
    return "\n".join(lines) + "\n"


def main() -> int:
    require(len(sys.argv) == 2, "expected the Python module path")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")

    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        (root / "multi.pdb").write_text(pdb([0, 10, 20], BASE))
        (root / "a.pdb").write_text(pdb([0, 10], BASE))
        (root / "b.pdb").write_text(pdb([100], PERMUTED))

        def load(name: str, fixture: str) -> int:
            return ok(m.invoke("load", {"path": str(root / fixture), "name": name}), f"load {name}")["object_id"]

        def names():
            return [row[1] for row in ok(m.invoke("object list"), "object list")["table"]["rows"]]

        def object_id(obj: str) -> str:
            for row in ok(m.invoke("object list"), "object list")["table"]["rows"]:
                if row[1] == obj:
                    return str(row[0])
            raise AssertionError(f"object {obj} not listed")

        def state_rows(obj: str, state: int):
            ok(m.invoke("object activate", {"id": object_id(obj)}), f"activate {obj}")
            if state_count(obj) > 1:
                ok(m.invoke("traj frame", {"frame": str(state)}), f"frame {state}")
            table = ok(m.invoke("object atoms", {"selection": "all", "properties": "name,resn,resi,x"}),
                       "atoms")["table"]
            return [(row[0], row[1], str(row[2]), round(float(row[3]), 3)) for row in table["rows"]]

        def state_count(obj: str) -> int:
            for row in ok(m.invoke("object list"), "object list")["table"]["rows"]:
                if row[1] == obj or str(row[0]) == obj:
                    return row[6]
            raise AssertionError(f"object {obj} not listed")

        # --- split_states (object.state.split): PyMOL names <object>_<state:04d>
        load("m", "multi.pdb")
        split = ok(m.invoke("object split-states", {"object": "m"}), "split")
        require(split["object_count"] == 3, "one object per state")
        for expected in ("m_0001", "m_0002", "m_0003"):
            require(expected in names(), f"split object {expected} exists: {names()}")
        require(state_count("m_0002") == 1, "split objects have one state")
        require(state_rows("m_0003", 0)[0][3] == 20.0, "split keeps the state coordinates")
        require(state_count("m") == 3, "the source keeps all its states")
        require("already exists" in fails(m.invoke("object split-states", {"object": "m"}), "split twice"),
                "a second split refuses duplicate names")
        for name in ("m_0001", "m_0002", "m_0003"):
            ok(m.invoke("object delete", {"object": name}), f"delete {name}")
        ok(m.invoke("object split-states", {"object": "m", "first": "2", "last": "3", "prefix": "hit"}),
           "split range with prefix")
        listed = names()
        require("hit0002" in listed and "hit0003" in listed and "hit0001" not in listed,
                f"prefix numbering follows the state number: {listed}")
        require(state_rows("hit0002", 0)[0][3] == 10.0, "prefixed split object holds state 2")
        ok(m.invoke("object delete", {"object": "hit0002"}), "cleanup")
        ok(m.invoke("object delete", {"object": "hit0003"}), "cleanup")
        require("no states" in fails(m.invoke("object split-states", {"object": "m", "first": "9"}),
                                     "split beyond the last state"), "empty range is an error")

        # --- delete_states (object.state.delete)
        ok(m.invoke("object delete-states", {"object": "m", "states": "2"}), "delete 2")
        require(state_count("m") == 2 and [state_rows("m", s)[0][3] for s in (0, 1)] == [0.0, 20.0],
                "deleting state 2 keeps states 1 and 3")
        ok(m.invoke("object delete", {"object": "m"}), "reload")
        load("m", "multi.pdb")
        removed = ok(m.invoke("object delete-states", {"object": "m", "states": "1-2"}), "delete 1-2")
        require(removed["removed_state_count"] == 2 and state_count("m") == 1 and
                state_rows("m", 0)[0][3] == 20.0, "ranges delete inclusively")
        ok(m.invoke("object delete", {"object": "m"}), "reload")
        load("m", "multi.pdb")
        out_of_range = ok(m.invoke("object delete-states", {"object": "m", "states": "5-9"}), "delete 5-9")
        require(out_of_range["removed_state_count"] == 0 and state_count("m") == 3,
                "out-of-range states are ignored like PyMOL")
        ok(m.invoke("object delete-states", {"object": "m", "states": "3, 1"}), "comma list")
        require(state_count("m") == 1 and state_rows("m", 0)[0][3] == 10.0, "comma separated list")
        require("every state" in fails(m.invoke("object delete-states", {"object": "m", "states": "1"}),
                                       "delete the last state"), "an object keeps at least one state")
        require("states" in fails(m.invoke("object delete-states", {"object": "m", "states": "x"}),
                                  "bad list"), "malformed state lists are rejected")
        ok(m.invoke("object delete", {"object": "m"}), "cleanup")

        # --- VMD-style frame editing (trajectory.playback.delete_range / dup), 0-based
        load("f", "multi.pdb")
        ok(m.invoke("traj delete-frames", {"object": "f", "beg": "1", "end": "1"}), "delete one frame")
        require(state_count("f") == 2 and [state_rows("f", s)[0][3] for s in (0, 1)] == [0.0, 20.0],
                "delete-frames removes the inclusive range")
        ok(m.invoke("object delete", {"object": "f"}), "cleanup")
        load("f", "multi.pdb")
        kept = ok(m.invoke("traj delete-frames", {"object": "f", "skip": "2"}), "delete with stride")
        require(kept["removed_state_count"] == 1 and state_count("f") == 2 and
                [state_rows("f", s)[0][3] for s in (0, 1)] == [0.0, 20.0],
                "skip keeps every second frame from the start of the range")
        ok(m.invoke("traj dup-frame", {"object": "f", "frame": "1"}), "dup frame 1")
        require(state_count("f") == 3 and state_rows("f", 2)[0][3] == 20.0, "dup appends a copy of the frame")
        ok(m.invoke("traj dup-frame", {"object": "f"}), "dup current frame")
        require(state_count("f") == 4, "dup defaults to the current frame")
        require("does not exist" in fails(m.invoke("traj dup-frame", {"object": "f", "frame": "9"}), "dup missing"),
                "duplicating a missing frame is an error")
        require("every frame" in fails(m.invoke("traj delete-frames", {"object": "f"}), "delete all frames"),
                "deleting every frame is refused")
        require("empty" in fails(m.invoke("traj delete-frames", {"object": "f", "beg": "9", "end": "8"}),
                                 "empty range"), "an empty range is an error")
        ok(m.invoke("object delete", {"object": "f"}), "cleanup")

        # --- join_states (object.state.join*): PyMOL modes 2 and 1
        load("oa", "a.pdb")
        load("ob", "b.pdb")
        joined = ok(m.invoke("object join-states", {"name": "j", "objects": "oa,ob", "mode": "intersection"}),
                    "join intersection")
        require(joined["atom_count"] == 5 and joined["state_count"] == 3,
                "intersection keeps the 5 shared atoms and 2+1 states")
        # third state: b's coordinates matched to a's atom order by identifier (PyMOL oracle)
        require(state_rows("j", 2) == [("N", "ALA", "1", 100.0), ("CA", "ALA", "1", 101.4),
                                        ("C", "ALA", "1", 102.0), ("O", "ALA", "1", 101.5),
                                        ("N", "GLY", "2", 103.3)], f"matched by identifier: {state_rows('j', 2)}")
        require(state_rows("j", 1)[0][3] == 10.0, "first object's states come first")
        require("oa" in names() and "ob" in names(), "join keeps its sources")
        require("already exists" in fails(m.invoke("object join-states", {"name": "j", "objects": "oa"}),
                                          "duplicate name"), "joined name must be free")
        require("identical" in fails(m.invoke("object join-states",
                                              {"name": "bad", "objects": "oa,ob", "mode": "identical"}),
                                     "identical on different atoms"),
                "mode identical refuses objects with different atoms (PyMOL leaves this undefined)")
        ok(m.invoke("object delete", {"object": "j"}), "cleanup")
        load("oc", "multi.pdb")
        same = ok(m.invoke("object join-states", {"name": "k", "objects": "oa,oc", "mode": "identical"}),
                  "join identical")
        require(same["state_count"] == 5 and same["atom_count"] == 6, "identical topology keeps every atom")
        require([state_rows("k", s)[0][3] for s in range(5)] == [0.0, 10.0, 0.0, 10.0, 20.0],
                "identical join concatenates the states of each object (PyMOL oracle)")

        # --- selection restricts the atoms taken from each object
        sel = ok(m.invoke("object join-states", {"name": "ala", "objects": "oa,oc", "selection": "resn ALA",
                                                  "mode": "identical"}), "join selection")
        require(sel["atom_count"] == 4 and sel["state_count"] == 5, "selection limits the atoms")

        # --- object.molecule.fix / free: placement is protected while fixed
        def fixed_flag(obj: str):
            for row in ok(m.invoke("object list"), "object list")["table"]["rows"]:
                if row[1] == obj:
                    return row[10]
            raise AssertionError(obj)

        require(fixed_flag("oa") is False, "objects start free")
        ok(m.invoke("object fix", {"object": "oa"}), "fix oa")
        require(fixed_flag("oa") is True and fixed_flag("oc") is False, "only oa is fixed")
        require("fixed" in fails(m.invoke("object matrix-copy", {"source": "oc", "target": "oa"}),
                                 "matrix copy onto a fixed object"), "fixed objects refuse placement copies")
        require("fixed" in fails(m.invoke("object align", {"target-object": "oc", "mobile-object": "oa",
                                                            "match": "identifier"}), "align a fixed object"),
                "fixed objects refuse alignment")
        require("fixed" in fails(m.invoke("view reset", {"object": "oa"}), "reset a fixed object"),
                "fixed objects refuse a placement reset")
        ok(m.invoke("object free", {"object": "oa"}), "free oa")
        require(fixed_flag("oa") is False, "free clears the flag")
        ok(m.invoke("object matrix-copy", {"source": "oc", "target": "oa"}), "matrix copy after free")
        ok(m.invoke("object fix", {"object": "oa"}), "fix again for the session round trip")

        # --- session journal replay (commands are undoable/journaled)
        def snapshot():
            return {name: (state_count(name), fixed_flag(name)) for name in names()}

        expected = snapshot()
        session = root / "states.mssession"
        ok(m.invoke("session save", {"path": str(session)}), "session save")
        ok(m.invoke("object delete", {"object": "ala"}), "scramble")
        require(snapshot() != expected, "scrambled before restore")
        ok(m.invoke("session load", {"path": str(session)}), "session load")
        require(snapshot() == expected, f"state edits replay from the session: {snapshot()} vs {expected}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
