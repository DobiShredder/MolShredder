#!/usr/bin/env python3

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
    require(len(sys.argv) == 4, "expected module, CLI and BCIF fixture")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    fixture = pathlib.Path(sys.argv[3]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    load_args = {"path": str(fixture), "file-format": "bcif",
                 "name": "binary_models"}
    frame_args = {"frame": "1"}
    center_args = {"selection": "all", "mode": "centroid",
                   "precision": "6", "unit": "angstrom"}
    python_results = [
        molshredder.invoke("load", load_args),
        molshredder.invoke("traj frame", frame_args),
        molshredder.invoke("analyze center", center_args),
    ]
    require(all(item["status"] == "ok" for item in python_results),
            "Python BCIF load/seek/analysis workflow failed")
    require(python_results[0]["data"]["format"] == "bcif" and
            python_results[0]["data"]["atom_count"] == 2 and
            python_results[0]["data"]["frame_count"] == 2 and
            python_results[1]["data"]["frame"] == 1 and
            python_results[2]["data"]["position"] == [0.7, 0, 0],
            "Python BCIF result lost format, model state or coordinates")

    script = (
        "format json\n"
        f'invoke "load" --file-format "bcif" --name "binary_models" '
        f'--path "{fixture}"\n'
        'invoke "traj frame" --frame "1"\n'
        'invoke "analyze center" --mode "centroid" --precision "6" '
        '--selection "all" --unit "angstrom"\n'
        "exit\n"
    )
    completed = subprocess.run([str(cli_path), "console"],
                               input=portable_console_script(script),
                               text=True, capture_output=True, check=True)
    cli_results = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(cli_results == python_results,
            "CLI and Python BCIF result envelopes diverged")
    print("bcif-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
