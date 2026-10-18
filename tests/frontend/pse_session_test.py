#!/usr/bin/env python3
"""SEQ-319 S6: read-only PyMOL session (.pse) import through the restricted unpickler.

tests/io/fixtures/pse/session.pse was written by Open-Source PyMOL 3.1.0 from two small synthetic molecules
(2 states, per-atom B-factor/occupancy, secondary structure, alternate location, unit cell, sticks/spheres/
cartoon/surface/lines representations, two named selections and a distance object). Expected values come from
the same PyMOL session.
"""

import importlib
import os
import pathlib
import pickle
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


class Evil:
    def __init__(self, command: str):
        self.command = command

    def __reduce__(self):
        return (os.system, (self.command,))


def main() -> int:
    module_path = pathlib.Path(sys.argv[1]).resolve()
    fixtures = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")

    loaded = ok(m.invoke("load", {"path": str(fixtures / "pse" / "session.pse"), "file-format": "auto"}), "load pse")
    require(loaded["format"] == "pse" and loaded["structure_count"] == 2, f"two molecular objects: {loaded}")
    objects = {o["object_name"]: o for o in loaded["objects"]}
    require(set(objects) == {"pep", "pep2"}, f"object names come from the session: {set(objects)}")
    require(objects["pep"]["atom_count"] == 10 and objects["pep"]["frame_count"] == 2, "pep has 10 atoms and 2 states")
    require(objects["pep2"]["atom_count"] == 7 and objects["pep2"]["frame_count"] == 1, "pep2 has 7 atoms and 1 state")
    notes = " | ".join(loaded["import_notes"])
    require("surface" in notes and "measurement" in notes and "colors" in notes, f"unimported content is reported: {notes}")

    rows = {row[1]: row for row in ok(m.invoke("object list"), "object list")["table"]["rows"]}
    require(rows["pep"][3] is True and rows["pep2"][3] is False, "the disabled object stays hidden")
    require(rows["pep"][7] == 3 and rows["pep2"][7] == 1, f"sticks+spheres+cartoon / lines are rebuilt: {rows}")

    def atoms(name: str, properties: str, state: int = 0):
        ok(m.invoke("object activate", {"id": str(rows[name][0])}), "activate")
        return ok(m.invoke("object atoms", {"properties": properties, "limit": "100"}), "atoms")["table"]["rows"]

    table = atoms("pep", "name,resn,resi,chain,element,b,q,altloc,partial_charge,x,y,z")
    require([r[0] for r in table] == ["N", "CA", "C", "O", "CB", "H", "HA", "1HB", "2HB", "3HB"], "atom names")
    require({r[1] for r in table} == {"ALA"} and {r[2] for r in table} == {"2"} and {r[3] for r in table} == {"A"}, "residue identity")
    require([r[4] for r in table] == ["N", "C", "C", "O", "C", "H", "H", "H", "H", "H"], "elements")
    require([r[5] for r in table] == [float(i) for i in range(11, 21)] and all(abs(r[6] - 0.5) < 1e-9 for r in table), "B-factors and occupancy")
    require(table[0][7] == "A" and table[1][7] == "", "alternate location")
    require(abs(table[0][8] + 0.4157) < 1e-6 and abs(table[2][8] - 0.5973) < 1e-6, "partial charges")
    require(abs(table[0][9] + 0.6769) < 1e-4 and abs(table[3][9] - 2.0301) < 1e-4, "state 1 coordinates")
    require(ok(m.invoke("object bonds"), "bonds")["returned_count"] == 9, "nine bonds")
    count = lambda sel: ok(m.invoke("object count", {"selection": sel}), sel)["atom_count"]
    require(count("@backbone_") == 3, "named selection 'backbone_' imported (N, CA, C)")
    ok(m.invoke("traj frame", {"frame": "1"}), "state 2")
    state2 = ok(m.invoke("object atoms", {"properties": "x,y,z", "limit": "100"}), "state 2 atoms")["table"]["rows"]
    require(abs(state2[0][0] - 0.3231) < 1e-4 and abs(state2[3][2] - 2.4985) < 1e-4, f"second state is translated: {state2[:4]}")
    require(count("ss H") == 1, "secondary structure on CA")

    # ---- safe unpickler: code in the pickle is never executed
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        marker = root / "pwned.txt"
        for protocol in (0, 1, 2):
            bad = root / f"evil{protocol}.pse"
            bad.write_bytes(pickle.dumps({"names": [Evil(f"touch {marker}")]}, protocol=protocol))
            message = fails(m.invoke("load", {"path": str(bad), "name": "evil", "file-format": "pse"}), f"evil protocol {protocol}")
            require("not allowed" in message or "unsafe" in message.lower(), f"hostile global rejected: {message}")
        require(not marker.exists(), "no code ran while reading a hostile pickle")

        other = root / "other.pse"
        other.write_bytes(pickle.dumps({"hello": 1}, protocol=1))
        require("not a PyMOL session" in fails(m.invoke("load", {"path": str(other), "name": "x", "file-format": "pse"}), "other"), "plain pickle")
        empty_session = root / "empty.pse"
        empty_session.write_bytes(pickle.dumps({"names": [None], "version": 3000000}, protocol=1))
        require("no molecular object" in fails(m.invoke("load", {"path": str(empty_session), "name": "x", "file-format": "pse"}), "empty"), "empty session")

        raw = (fixtures / "pse" / "session.pse").read_bytes()
        for label, blob in (("truncated", raw[: len(raw) // 2]), ("garbage", b"\x00\x01 not a pickle at all"), ("empty", b""),
                            ("huge_length", b"\x80\x02X\xff\xff\xff\x7fabc."), ("stack_underflow", b"\x80\x02a."),
                            ("unknown_opcode", b"\x80\x02\x95\x00\x00\x00\x00\x00\x00\x00\x00P."), ("bad_memo", b"\x80\x02h\x07.")):
            bad = root / f"{label}.pse"
            bad.write_bytes(blob)
            result = m.invoke("load", {"path": str(bad), "name": "bad", "file-format": "pse"})
            require(result["status"] == "error", f"{label} pickle must be rejected")
        # byte flips never crash
        for position in range(0, len(raw), max(1, len(raw) // 200)):
            damaged = bytearray(raw)
            damaged[position] ^= 0xFF
            bad = root / "flip.pse"
            bad.write_bytes(bytes(damaged))
            m.invoke("load", {"path": str(bad), "name": "flip", "file-format": "pse"})
            for row in ok(m.invoke("object list"), "list")["table"]["rows"]:
                if row[1] == "flip" or row[1].startswith("flip"):
                    ok(m.invoke("object delete", {"object": row[1]}), "cleanup")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
