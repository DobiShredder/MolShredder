#!/usr/bin/env python3
"""Measurement dash geometry parity with PyMOL 3.1.0 VRML exports (Q-019 a).

Arguments: geometry probe, 1ubq.pdb, pymol310_measure.json.
"""

import json
import math
import pathlib
import sys

from geometry_parity_test import TOLERANCE, require, run

COMMANDS = {"distance": ("measure distance", ["from", "to"]),
            "angle": ("measure angle", ["first", "vertex", "third"]),
            "dihedral": ("measure dihedral", ["first", "second", "third", "fourth"])}


def ordered(segment: list) -> tuple:
    return tuple(sorted((tuple(segment[0:3]), tuple(segment[3:6]))))


def matches(actual: list, expected: list) -> bool:
    if len(actual) != len(expected):
        return False
    unused = list(expected)
    for segment in actual:
        found = next((other for other in unused
                      if all(math.dist(a, b) <= TOLERANCE for a, b in zip(segment, other))), None)
        if found is None:
            return False
        unused.remove(found)
    return True


def main() -> int:
    require(len(sys.argv) == 4, "expected probe, 1ubq.pdb and pymol310_measure.json")
    probe, ubq, capture = (pathlib.Path(arg).resolve() for arg in sys.argv[1:])
    cases = json.loads(capture.read_text())["cases"]
    for name, case in cases.items():
        command, keys = COMMANDS[case["kind"]]
        script = [("load", {"file-format": "pdb", "name": "m", "path": str(ubq)})]
        script += [("setting set", {"name": key, "value": str(value)})
                   for key, value in case["settings"].items()]
        script.append((command, dict(zip(keys, case["atoms"]))))
        result = run(probe, script)
        actual = [ordered(item[0:6]) for item in result["overlay_lines"]]
        expected = [tuple(sorted((tuple(c[0]), tuple(c[1])))) for c in case["cylinders"]]
        require(matches(actual, expected), f"{name}: measurement dashes differ from PyMOL")
        widths = {item[6] for item in result["overlay_lines"]}
        require(widths == {float(case["settings"].get("dash_width", 2.5))},
                f"{name}: dash width must follow dash_width")
        require(all(item[7:10] == [1.0, 1.0, 0.0] for item in result["overlay_lines"]),
                f"{name}: measurement objects must be yellow like PyMOL")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
