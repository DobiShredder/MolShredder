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
            "expected module, CLI, topology, BINPOS and build directory")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    topology = pathlib.Path(sys.argv[3]).resolve()
    trajectory = pathlib.Path(sys.argv[4]).resolve()
    output = pathlib.Path(sys.argv[5]).resolve() / "frontend-roundtrip.binpos"
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    load_args = {"path": str(topology), "file-format": "pdb",
                 "name": "binpos"}
    attach_args = {"path": str(trajectory), "file-format": "binpos",
                   "cache-mib": "1", "prefetch-frames": "0"}
    frame_args = {"frame": "1"}
    center_args = {"selection": "all", "mode": "centroid",
                   "precision": "6", "unit": "angstrom"}
    save_args = {"path": str(output), "file-format": "binpos",
                 "overwrite": "true"}
    roundtrip_args = {"path": str(output), "file-format": "binpos",
                      "cache-mib": "1", "prefetch-frames": "0"}
    python_results = [
        molshredder.invoke("load", load_args),
        molshredder.invoke("traj load", attach_args),
        molshredder.invoke("traj frame", frame_args),
        molshredder.invoke("analyze center", center_args),
        molshredder.invoke("traj save", save_args),
        molshredder.invoke("traj load", roundtrip_args),
    ]
    require(all(item["status"] == "ok" for item in python_results),
            "Python PDB/BINPOS workflow failed")
    require(python_results[1]["data"]["format"] == "binpos" and
            python_results[1]["data"]["frame_count"] == 2 and
            python_results[1]["data"]["provider"]["id"] == "native" and
            python_results[2]["data"]["frame"] == 1 and
            python_results[3]["data"]["position"] == [13, 14, 15] and
            python_results[4]["data"]["format"] == "binpos" and
            python_results[4]["data"]["byte_count"] == 44 and
            python_results[4]["data"]["provider"]["id"] == "native" and
            python_results[5]["data"]["format"] == "binpos" and
            python_results[5]["data"]["frame_count"] == 1 and
            output.read_bytes()[:8] == b"fxyz\x03\x00\x00\x00",
            "Python BINPOS result lost read, seek, write or round-trip data")

    script = (
        "format json\n"
        f'invoke "load" --file-format "pdb" --name "binpos" '
        f'--path "{topology}"\n'
        f'invoke "traj load" --cache-mib "1" --file-format "binpos" '
        f'--path "{trajectory}" --prefetch-frames "0"\n'
        'invoke "traj frame" --frame "1"\n'
        'invoke "analyze center" --mode "centroid" --precision "6" '
        '--selection "all" --unit "angstrom"\n'
        f'invoke "traj save" --file-format "binpos" --overwrite "true" '
        f'--path "{output}"\n'
        f'invoke "traj load" --cache-mib "1" --file-format "binpos" '
        f'--path "{output}" --prefetch-frames "0"\n'
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
            "CLI and Python BINPOS result envelopes diverged")
    print("binpos-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
