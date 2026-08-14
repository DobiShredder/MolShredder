#!/usr/bin/env python3

import importlib
import json
import os
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require(len(sys.argv) == 6,
            "expected module, CLI, topology, H5MD and build directory")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    topology = pathlib.Path(sys.argv[3]).resolve()
    trajectory = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    load_args = {"path": str(topology), "file-format": "pdb",
                 "name": "h5md"}
    attach_args = {"path": str(trajectory), "file-format": "h5md",
                   "particle-group": "trajectory", "cache-mib": "1",
                   "prefetch-frames": "0"}
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
            "Python PDB/H5MD workflow failed")
    require(python_results[1]["data"]["format"] == "h5md" and
            python_results[1]["data"]["frame_count"] == 2 and
            python_results[2]["data"]["source_step"] == 20 and
            python_results[2]["data"]["physical_time"] == 0.5 and
            python_results[3]["data"]["position"] == [12.0, 14.0, 16.0],
            "Python H5MD result lost format, timing, mapping or units")

    script = (
        "format json\n"
        f'invoke "load" --file-format "pdb" --name "h5md" '
        f'--path "{topology}"\n'
        f'invoke "traj load" --cache-mib "1" --file-format "h5md" '
        f'--particle-group "trajectory" --path "{trajectory}" '
        '--prefetch-frames "0"\n'
        'invoke "traj frame" --frame "1"\n'
        'invoke "analyze center" --mode "centroid" --precision "6" '
        '--selection "all" --unit "angstrom"\n'
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
            "CLI and Python H5MD result envelopes diverged")

    shutdown_script = (
        "import molshredder\n"
        f"assert molshredder.invoke('load', {{'path': {str(topology)!r}, "
        "'file-format': 'pdb'})['status'] == 'ok'\n"
        "assert molshredder.invoke('traj load', "
        f"{{'path': {str(trajectory)!r}, 'file-format': 'h5md', "
        "'cache-mib': '1', 'prefetch-frames': '0'})['status'] == 'ok'\n"
    )
    shutdown = subprocess.run(
        [sys.executable, "-c", shutdown_script], text=True,
        capture_output=True, env={**os.environ,
                                  "PYTHONPATH": str(module_path.parent)},
        check=True)
    require("HDF5-DIAG" not in shutdown.stderr,
            "embedded Python shutdown attempted to close stale HDF5 IDs")
    print("h5md-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
