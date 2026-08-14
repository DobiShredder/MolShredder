#!/usr/bin/env python3

import importlib
import json
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def json_lines(text: str) -> list[dict]:
    results = []
    for line in text.splitlines():
        start = line.find('{"schema_version"')
        if start >= 0:
            results.append(json.loads(line[start:]))
    return results


def main() -> int:
    require(len(sys.argv) == 5, "expected module, CLI, GUI probe and fixture")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    gui_probe = pathlib.Path(sys.argv[3]).resolve()
    fixture = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    load_args = {"path": str(fixture), "name": "fixture"}
    center_args = {
        "selection": "chain A", "mode": "centroid",
        "precision": "6", "unit": "nanometer",
    }
    distance_args = {
        "from": "index 1", "to": "index 2", "mode": "atom",
        "pbc": "minimum-image",
        "precision": "6", "unit": "nanometer",
    }
    require(molshredder.invoke("load", load_args)["status"] == "ok",
            "Python load failed")
    python_results = [
        molshredder.invoke("analyze center", center_args),
        molshredder.invoke("analyze center", {
            "selection": "all", "mode": "com",
            "precision": "6", "unit": "angstrom",
        }),
        molshredder.invoke("measure distance", distance_args),
    ]

    script = (
        "format json\n"
        f'invoke "load" --file-format "auto" --name "fixture" --path "{fixture}"\n'
        'invoke "analyze center" --mode "centroid" --precision "6" '
        '--selection "chain A" --unit "nanometer"\n'
        'invoke "analyze center" --mode "com" --precision "6" '
        '--selection "all" --unit "angstrom"\n'
        'invoke "measure distance" --from "index 1" --mode "atom" '
        '--pbc "minimum-image" '
        '--precision "6" --to "index 2" --unit "nanometer"\nexit\n'
    )
    cli_run = subprocess.run([str(cli_path), "console"], input=script, text=True,
                             capture_output=True, check=True)
    cli_results = json_lines(cli_run.stdout)[1:]
    gui_run = subprocess.run([str(gui_probe), str(fixture)], text=True,
                             capture_output=True, check=True)
    gui_results = json_lines(gui_run.stdout)
    require(len(cli_results) == len(gui_results) == 3,
            "frontends did not emit three analysis results")
    require(python_results == cli_results == gui_results,
            "CLI, GUI and Python numerical envelopes diverged")
    require(python_results[0]["data"]["position"] == [0.15, 0.25, 0.35],
            "centroid/unit conversion result is unexpected")
    require(python_results[1]["data"]["mass_estimated"] is True and
            bool(python_results[1]["data"]["mass_source"]),
            "COM mass provenance is missing")
    require(python_results[2]["data"]["distance"] == 0.173205,
            "atom distance/unit conversion result is unexpected")
    require(python_results[2]["data"]["pbc"] == "minimum-image",
            "distance PBC provenance is missing")
    print("analysis-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
