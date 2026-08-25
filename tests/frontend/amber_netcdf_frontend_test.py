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
    require(len(sys.argv) == 6,
            "expected module, CLI, PRMTOP, NetCDF and build directory")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    prmtop = pathlib.Path(sys.argv[3]).resolve()
    trajectory = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    load_args = {"path": str(prmtop), "file-format": "prmtop",
                 "name": "amber-netcdf"}
    attach_args = {"path": str(trajectory), "file-format": "netcdf",
                   "cache-mib": "1", "prefetch-frames": "0",
                   "mapping": "index"}
    frame_args = {"frame": "1"}
    center_args = {"selection": "all", "mode": "centroid",
                   "precision": "6", "unit": "angstrom"}
    python_results = [
        molshredder.invoke("load", load_args),
        molshredder.invoke("traj load", attach_args),
        molshredder.invoke("traj frame", frame_args),
        molshredder.invoke("analyze center", center_args),
    ]
    require(all(item["status"] == "ok" for item in python_results),
            "Python PRMTOP/Amber NetCDF workflow failed")
    require(python_results[1]["data"]["format"] == "netcdf" and
            python_results[1]["data"]["frame_count"] == 2 and
            python_results[2]["data"]["frame"] == 1 and
            python_results[3]["data"]["position"] == [14.5, 15.5, 16.5],
            "Python NetCDF result lost format, seek or scaled coordinates")

    script = (
        "format json\n"
        f'invoke "load" --file-format "prmtop" --name "amber-netcdf" '
        f'--path "{prmtop}"\n'
        f'invoke "traj load" --cache-mib "1" --file-format "netcdf" '
        f'--path "{trajectory}" --prefetch-frames "0" --mapping "index"\n'
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
            "CLI and Python Amber NetCDF result envelopes diverged")
    print("amber-netcdf-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
