#!/usr/bin/env python3

import importlib
import json
import pathlib
import re
import subprocess
import sys

from console_script import portable_console_script


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


def normalize_created_at(value):
    if isinstance(value, dict):
        result = {}
        for key, child in value.items():
            if key == "created_at_utc":
                require(isinstance(child, str) and
                        re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z", child),
                        "creation timestamp is not canonical UTC")
                result[key] = "<utc-timestamp>"
            else:
                result[key] = normalize_created_at(child)
        return result
    if isinstance(value, list):
        return [normalize_created_at(child) for child in value]
    if isinstance(value, str) and re.fullmatch(
            r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z", value):
        return "<utc-timestamp>"
    return value


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
        "result-name": "chain-center",
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
            "result-name": "all-com",
        }),
        molshredder.invoke("measure distance", distance_args | {
            "result-name": "atom-distance"}),
        molshredder.invoke("result get", {"id": "2"}),
        molshredder.invoke("result list", {}),
    ]

    script = (
        "format json\n"
        f'invoke "load" --file-format "auto" --name "fixture" --path "{fixture}"\n'
        'invoke "analyze center" --mode "centroid" --precision "6" '
        '--selection "chain A" --unit "nanometer" '
        '--result-name "chain-center"\n'
        'invoke "analyze center" --mode "com" --precision "6" '
        '--selection "all" --unit "angstrom" --result-name "all-com"\n'
        'invoke "measure distance" --from "index 1" --mode "atom" '
        '--pbc "minimum-image" '
        '--precision "6" --to "index 2" --unit "nanometer" '
        '--result-name "atom-distance"\n'
        'invoke "result get" --id "2"\n'
        'invoke "result list"\nexit\n'
    )
    cli_run = subprocess.run([str(cli_path), "console"],
                             input=portable_console_script(script), text=True,
                             capture_output=True, check=True)
    cli_results = json_lines(cli_run.stdout)[1:]
    gui_run = subprocess.run([str(gui_probe), str(fixture)], text=True,
                             capture_output=True, check=True)
    gui_results = json_lines(gui_run.stdout)
    require(len(python_results) == len(cli_results) == len(gui_results) == 5,
            "frontends did not emit five analysis/result lifecycle responses")
    normalized_python = normalize_created_at(python_results)
    normalized_cli = normalize_created_at(cli_results)
    normalized_gui = normalize_created_at(gui_results)
    require(normalized_python == normalized_cli == normalized_gui,
            "CLI, GUI and Python analysis/result envelopes diverged:\n" +
            json.dumps({"python": normalized_python, "cli": normalized_cli,
                        "gui": normalized_gui}, indent=2, sort_keys=True))
    require(python_results[0]["data"]["position"] == [0.15, 0.25, 0.35],
            "centroid/unit conversion result is unexpected")
    require(python_results[1]["data"]["mass_estimated"] is True and
            bool(python_results[1]["data"]["mass_source"]),
            "COM mass provenance is missing")
    require(python_results[2]["data"]["distance"] == 0.173205,
            "atom distance/unit conversion result is unexpected")
    require(python_results[2]["data"]["pbc"] == "minimum-image",
            "distance PBC provenance is missing")
    require(python_results[3]["data"]["algorithm_version"] ==
            "molshredder-center-v1" and
            python_results[3]["data"]["source_status"] == "current" and
            python_results[4]["data"]["result_count"] == 3,
            "persistent result provenance/list state is unexpected")
    print("analysis-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
