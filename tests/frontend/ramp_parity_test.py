#!/usr/bin/env python3
"""Color ramp frontend parity (SEQ-311 S6)."""

import importlib
import json
import pathlib
import subprocess
import sys

from console_script import portable_console_script


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require(len(sys.argv) == 6, "expected module, CLI, GUI probe, structure and map")
    module_path, cli_path, gui_path = (pathlib.Path(p).resolve() for p in sys.argv[1:4])
    structure, density = (pathlib.Path(p).resolve() for p in sys.argv[4:6])
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")
    for name, arguments in (("load", {"file-format": "pdb", "path": str(structure)}),
                            ("volume load", {"name": "dens", "path": str(density)})):
        require(molshredder.invoke(name, arguments)["status"] == "ok", f"setup failed: {name}")
    python_result = molshredder.invoke("ramp new", {"name": "r1", "source": "dens",
                                                    "range": "0 0.5 1", "color": "blue white red"})
    commands = [
        f'invoke "load" --file-format "pdb" --path "{structure}"',
        f'invoke "volume load" --name "dens" --path "{density}"',
        'invoke "ramp_new" --name "r1" --source "dens" --range "0 0.5 1" '
        '--color "blue white red"',
    ]
    completed = subprocess.run(
        [str(cli_path), "console"],
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
    cli_result = envelopes[-1]
    gui_result = json.loads(subprocess.run(
        [str(gui_path), "ramp", str(structure), str(density)],
        check=True, capture_output=True, text=True).stdout)
    require(python_result == cli_result == gui_result,
            "CLI, GUI adapter and Python ramp results diverged")
    data = python_result["data"]
    require(data["color_index"] == -10 and data["levels"] == [0.0, 0.5, 1.0]
            and data["source_kind"] == "map" and data["color_count"] == 3,
            "ramp response lost its definition")
    proximity = molshredder.invoke("ramp new", {"name": "r2", "source": "1", "range": "0 2 4",
                                                "color": "rainbow"})
    require(proximity["status"] == "ok" and proximity["data"]["source_kind"] == "molecule"
            and proximity["data"]["spectrum"] == 6 and proximity["data"]["color_index"] == -11,
            "proximity spectrum ramps must use the next color code")
    require(molshredder.invoke("color", {"color": "r2", "selection": "all"})["status"] == "ok",
            "ramp names must work as colors")
    updated = molshredder.invoke("ramp update", {"name": "r1", "range": "0 1"})
    require(updated["status"] == "ok" and updated["data"]["replaced"], "ramp update failed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
