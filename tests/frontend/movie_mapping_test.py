#!/usr/bin/env python3
"""SEQ-320 S1: movie frame -> state mapping (`movie map`, PyMOL mset/madd syntax) and the PML bridge.

Expected frame sequences were produced with Open-Source PyMOL 3.1.0 (cmd.mset, then cmd.frame/get_state for every
frame) on a six-state object.
"""

import importlib
import pathlib
import random
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


def six_models() -> str:
    lines = []
    for model in range(6):
        lines.append(f"MODEL     {model + 1:4d}")
        lines.append(f"ATOM      1  C   ALA A   1    {10.0 * model:8.3f}   0.000   0.000  1.00  0.00           C")
        lines.append("ENDMDL")
    lines.append("END")
    return "\n".join(lines) + "\n"


def main() -> int:
    module_path = pathlib.Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")

    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        (root / "six.pdb").write_text(six_models())
        ok(m.invoke("load", {"path": str(root / "six.pdb"), "name": "six", "file-format": "pdb"}), "load")

        def state_now() -> int:
            x = ok(m.invoke("object atoms", {"properties": "x", "limit": "1"}), "atoms")["table"]["rows"][0][0]
            return int(round(x / 10.0)) + 1

        def sequence() -> list:
            movie = ok(m.invoke("movie status"), "status")["movie"]
            seen = []
            for frame in range(1, movie["frame_count"] + 1):
                ok(m.invoke("movie seek", {"frame": str(frame)}), f"seek {frame}")
                seen.append(state_now())
            return seen

        def mapped(spec: str, **kw) -> dict:
            args = {"spec": spec}
            args.update({k.replace("_", "-"): str(v) for k, v in kw.items()})
            return ok(m.invoke("movie map", args), f"movie map {spec!r}")["movie"]

        # ---- PyMOL 3.1.0 sequences
        cases = {
            "1 x30 1 -5 5 x3 5 -1": [1] * 31 + [2, 3, 4, 5, 5, 5, 5, 5, 4, 3, 2, 1],
            "1 -5": [1, 2, 3, 4, 5],
            "3 x2 -1 2": [3, 3, 2, 1, 2],
            "-3": [1, 2, 3],
            "2 -4 x3 -1": [2, 3, 4, 4, 4, 3, 2, 1],
            "1-5": [1, 2, 3, 4, 5],            # no space before the dash
            "1 - 3": [1, 2, 3],                # space after the dash
            "4 x 2": [4, 4],                   # space after x
        }
        for spec, expected in cases.items():
            movie = mapped(spec)
            require(movie["frame_count"] == len(expected), f"{spec!r}: length {movie['frame_count']} != {len(expected)}")
            require(sequence() == expected, f"{spec!r}: states {sequence()} != {expected}")
        # x with nothing before it repeats the current state (PyMOL: cmd.get_state()); here state 5
        ok(m.invoke("traj frame", {"frame": "4"}), "select state 5")
        mapped("x4")
        require(sequence() == [5, 5, 5, 5], "x4 repeats the current state")
        # partial rewrite (mset spec, frame) and madd append, truncation by an empty spec
        mapped("1 -5")
        mapped("6 x2", frame=3)
        require(sequence() == [1, 2, 6, 6], "mset at frame 3 truncates and appends")
        mapped("1 -3")
        mapped("5 x2", append="true")
        require(sequence() == [1, 2, 3, 5, 5], "madd appends")
        mapped("1 -3")
        movie = mapped("", frame=2)
        require(movie["frame_count"] == 1 and sequence() == [1], "empty spec truncates")
        # the movie disappears when nothing is left
        ok(m.invoke("movie map", {"spec": ""}), "empty spec at frame 1")
        require(fails(m.invoke("movie status"), "no movie") != "", "movie removed")

        # ---- explicit keyframes win over the map; seeking keeps unmapped frames consistent
        mapped("1 -5")
        ok(m.invoke("movie keyframe", {"frame": "2", "trajectory-frame": "5"}), "keyframe")
        ok(m.invoke("movie seek", {"frame": "2"}), "seek key")
        require(state_now() == 6, "a trajectory keyframe overrides the map")
        ok(m.invoke("movie seek", {"frame": "3"}), "seek 3")
        require(state_now() == 3, "other frames still follow the map")

        ok(m.invoke("movie clear"), "drop the keyframes")

        # ---- the compact form reported by `movie status` parses back to the same mapping
        random.seed(5)
        for _ in range(25):
            states = []
            while len(states) < 40:
                kind = random.random()
                s = random.randint(1, 6)
                if kind < 0.3:
                    states += [s] * random.randint(1, 5)
                elif kind < 0.6:
                    e = random.randint(1, 6)
                    states += list(range(s, e + 1)) if e >= s else list(range(s, e - 1, -1))
                else:
                    states.append(s)
            spec = " ".join(str(s) for s in states)
            first = mapped(spec)
            seq1 = sequence()
            require(seq1 == states, f"plain list maps one to one: {states} vs {seq1}")
            compact = first["state_map"]
            second = mapped(compact)
            require(sequence() == states, f"compact form {compact!r} reproduces the mapping")
            require(len(compact) <= len(spec), "compact form is not longer")

        # ---- errors leave the movie unchanged
        mapped("1 -3")
        for bad, fragment in (("1 x", "repeat count"), ("1 -", "state number"), ("0", "numbered from 1"), ("7", "6 state"),
                              ("1 y", "unexpected"), ("1 x9999999", "exceeds"), ("1 -99999999", "state number")):
            message = fails(m.invoke("movie map", {"spec": bad}), bad)
            require(fragment in message, f"{bad!r}: expected {fragment!r} in {message!r}")
        require(sequence() == [1, 2, 3], "failed mappings change nothing")
        require("mutually exclusive" in fails(m.invoke("movie map", {"spec": "1", "frame": "2", "append": "true"}), "both"),
                "frame and append conflict")

        # ---- keyframes beyond the new length are dropped, current frame is clamped
        mapped("1 -5")
        ok(m.invoke("movie keyframe", {"frame": "5", "trajectory-frame": "0"}), "key 5")
        ok(m.invoke("movie seek", {"frame": "5"}), "seek 5")
        movie = mapped("1 -2")
        require(movie["frame_count"] == 2 and movie["current_frame"] == 2 and not movie["keyframes"], "shrinking drops keys and clamps")

        # ---- session journal replay restores the mapping
        mapped("1 x2 3 -5 2")
        ok(m.invoke("movie keyframe", {"frame": "2", "trajectory-frame": "4"}), "key before save")
        expected_status = ok(m.invoke("movie status"), "status")["movie"]
        session = root / "movie.mssession"
        ok(m.invoke("session save", {"path": str(session)}), "session save")
        ok(m.invoke("movie clear"), "clear before load")
        ok(m.invoke("session load", {"path": str(session)}), "session load")
        restored = ok(m.invoke("movie status"), "status after load")["movie"]
        require(restored["state_map"] == expected_status["state_map"] and restored["keyframes"] == expected_status["keyframes"],
                f"movie map survives a session round trip: {restored} vs {expected_status}")

        # ---- PML bridge: mset / madd / mclear
        script = root / "movie.pml"
        script.write_text("mset 1 -4\nmadd 6 x2\nmset 5 x2, 2\n")
        ok(m.invoke("pml run", {"path": str(script)}), "pml mset")
        require(sequence() == [1, 5, 5], f"PML mset/madd map to movie map: {sequence()}")
        (root / "clear.pml").write_text("mclear\n")
        ok(m.invoke("pml run", {"path": str(root / "clear.pml")}), "pml mclear")
        fails(m.invoke("movie status"), "cleared by mclear")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
