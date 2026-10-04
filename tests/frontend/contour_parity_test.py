#!/usr/bin/env python3
"""Contour/isolevel, cartoon type and surface setting parity (SEQ-311 S5).

Expected values: pymol-contour-cartoon-blackbox_20261003T090000Z (Open-Source PyMOL 3.1.0
with the same synthetic map and atoms).
"""

import importlib
import json
import pathlib
import subprocess
import sys

from console_script import portable_console_script


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def close(actual, expected, tolerance=1.0e-9):
    return all(abs(a - b) <= tolerance for a, b in zip(actual, expected))


def console_result(executable, structure, density):
    commands = [
        f'invoke "load" --file-format "pdb" --path "{structure}"',
        f'invoke "volume load" --name "dens" --path "{density}"',
        'invoke "isomesh" --name "carve1" --map "dens" --level "0.5" '
        '--selection "name C1" --carve "1.5"',
    ]
    completed = subprocess.run(
        [str(executable), "console"],
        input=portable_console_script("format json\n" + "\n".join(commands) + "\nexit\n"),
        check=True, capture_output=True, text=True)
    envelopes = []
    for line in completed.stdout.splitlines():
        marker = line.find("{")
        if marker >= 0:
            try:
                envelopes.append(json.loads(line[marker:]))
            except json.JSONDecodeError:
                pass
    require(envelopes, "CLI contour sequence emitted no result")
    return envelopes[-1]


def main() -> int:
    require(len(sys.argv) == 6, "expected module, CLI, GUI probe, structure and map")
    module_path, cli_path, gui_path = (pathlib.Path(p).resolve() for p in sys.argv[1:4])
    structure, density = (pathlib.Path(p).resolve() for p in sys.argv[4:6])
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")
    require(molshredder.invoke("load", {"file-format": "pdb", "path": str(structure)})["status"] == "ok",
            "structure load failed")
    require(molshredder.invoke("volume load", {"name": "dens", "path": str(density)})["status"] == "ok",
            "map load failed")

    python_result = molshredder.invoke("volume contour", {
        "name": "carve1", "map": "dens", "level": "0.5", "mode": "mesh",
        "selection": "name C1", "carve": "1.5"})
    cli_result = console_result(cli_path, structure, density)
    gui_result = json.loads(subprocess.run(
        [str(gui_path), "contour", str(structure), str(density)],
        check=True, capture_output=True, text=True).stdout)
    require(python_result == cli_result == gui_result,
            "CLI, GUI adapter and Python contour results diverged")

    map_extent = ([-3.0, -3.0, -3.0], [5.0, 5.5, 2.5])
    data = python_result["data"]
    require(close(data["extent_minimum"], [-1.5] * 3) and close(data["extent_maximum"], [1.5] * 3),
            "carve extent must be the selection padded by the carve distance")
    for name, mode in (("surf1", "surface"), ("mesh1", "mesh"), ("dot1", "dot")):
        created = molshredder.invoke("volume contour", {"name": name, "map": "dens", "level": "0.5",
                                                         "mode": mode})
        require(created["status"] == "ok" and created["data"]["primitive_count"] > 0
                and close(created["data"]["extent_minimum"], map_extent[0])
                and close(created["data"]["extent_maximum"], map_extent[1]),
                f"{mode} contour must cover the map extent")
        require(molshredder.invoke("volume isolevel get", {"name": name})["data"]["level"] == 0.5,
                "isolevel query must return the level")
    buffered = molshredder.invoke("isomesh", {"name": "buffer1", "map": "dens", "level": "0.5",
                                              "selection": "name C1", "buffer": "1.0"})
    require(close(buffered["data"]["extent_minimum"], [-1.0] * 3), "buffer extent drifted")
    require(molshredder.invoke("isolevel", {"name": "mesh1", "level": "0.8"})["status"] == "ok",
            "isolevel update failed")
    require(molshredder.invoke("volume isolevel get", {"name": "mesh1"})["data"]["level"]
            == 0.800000011920929, "isolevel must store float32 levels")
    replaced = molshredder.invoke("isomesh", {"name": "mesh1", "map": "dens", "level": "0.3"})
    require(replaced["data"]["replaced"] and replaced["data"]["level"] == 0.30000001192092896,
            "same-name same-mode contours must be replaced")
    conflict = molshredder.invoke("isosurface", {"name": "mesh1", "map": "dens", "level": "0.4"})
    require(conflict["status"] != "ok" and "another type" in conflict["error"]["message"],
            "a name used by another contour mode must fail")
    require(replaced["data"]["warnings"] == [], "default isosurface_algorithm must not warn")
    molshredder.invoke("setting set", {"name": "isosurface_algorithm", "value": "2"})
    fallback = molshredder.invoke("isosurface", {"name": "surf_alg", "map": "dens", "level": "0.5"})
    require(fallback["status"] == "ok" and len(fallback["data"]["warnings"]) == 1
            and "isosurface_algorithm 2" in fallback["data"]["warnings"][0],
            "unimplemented isosurface_algorithm must report an explicit fallback")
    molshredder.invoke("setting set", {"name": "isosurface_algorithm", "value": "0"})
    require(molshredder.invoke("isomesh", {"name": "x", "map": "no_map", "level": "0.5"})["status"] != "ok",
            "unknown maps must fail")
    empty = molshredder.invoke("isomesh", {"name": "empty1", "map": "dens", "level": "1000"})
    require(empty["status"] == "ok" and empty["data"]["primitive_count"] == 0,
            "levels above the map maximum create empty contours")
    require(molshredder.invoke("isolevel", {"name": "nope", "level": "1"})["status"] != "ok",
            "isolevel on an unknown object must fail")

    expected_codes = {"skip": -1, "automatic": 0, "loop": 1, "rectangle": 2, "oval": 3, "tube": 4,
                      "arrow": 5, "dumbbell": 6, "putty": 7, "dash": 8, "cylinder": 9}
    for name, code in expected_codes.items():
        assigned = molshredder.invoke("cartoon", {"type": name, "selection": "all"})
        require(assigned["status"] == "ok" and assigned["data"]["code"] == code,
                f"cartoon type code drifted: {name}")
    for bad in ("bogus", "3"):
        rejected = molshredder.invoke("cartoon", {"type": bad})
        require(rejected["status"] != "ok" and "unknown type" in rejected["error"]["message"],
                f"cartoon type {bad} must fail")
    for name, value, ok in (("transparency", "0.5", True), ("isosurface_algorithm", "2", True),
                            ("isosurface_algorithm", "9", False), ("surface_color", "red", True)):
        result = molshredder.invoke("setting set", {"name": name, "value": value, "scope": "global"})
        require((result["status"] == "ok") == ok, f"setting {name}={value} drifted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
