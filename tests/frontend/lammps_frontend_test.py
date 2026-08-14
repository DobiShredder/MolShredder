#!/usr/bin/env python3

import importlib
import json
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require(len(sys.argv) == 6,
            "expected module, CLI, topology, LAMMPS dump and build directory")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    topology = pathlib.Path(sys.argv[3]).resolve()
    trajectory = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    load_args = {"path": str(topology), "file-format": "pdb",
                 "name": "lammps"}
    attach_args = {"path": str(trajectory), "file-format": "lammps",
                   "coordinate-unit": "angstrom", "cache-mib": "1",
                   "prefetch-frames": "0"}
    frame_args = {"frame": "1"}
    python_results = [
        molshredder.invoke("load", load_args),
        molshredder.invoke("traj load", attach_args),
        molshredder.invoke("traj frame", frame_args),
    ]
    require(all(item["status"] == "ok" for item in python_results),
            "Python PDB/LAMMPS workflow failed")
    require(python_results[1]["data"]["format"] == "lammps" and
            python_results[1]["data"]["frame_count"] == 2 and
            python_results[2]["data"]["source_step"] == 25,
            "Python LAMMPS result lost format, frame count or timestep")

    script = (
        "format json\n"
        f'invoke "load" --file-format "pdb" --name "lammps" '
        f'--path "{topology}"\n'
        f'invoke "traj load" --cache-mib "1" '
        f'--coordinate-unit "angstrom" --file-format "lammps" '
        f'--path "{trajectory}" --prefetch-frames "0"\n'
        'invoke "traj frame" --frame "1"\n'
        "exit\n"
    )
    completed = subprocess.run([str(cli_path), "console"], input=script,
                               text=True, capture_output=True, check=True)
    cli_results = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(cli_results == python_results,
            "CLI and Python LAMMPS result envelopes diverged")

    missing_unit = subprocess.run(
        [str(cli_path), "console"],
        input=("format json\n"
               f'invoke "load" --file-format "pdb" --path "{topology}"\n'
               f'invoke "traj load" --file-format "lammps" '
               f'--path "{trajectory}"\nexit\n'),
        text=True, capture_output=True, check=True)
    require("does not encode its coordinate unit" in missing_unit.stderr,
            "CLI must reject an implicit LAMMPS coordinate unit")
    print("lammps-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
