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
    require(len(sys.argv) == 5,
            "expected module, CLI, generated TRR and output directory")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    trajectory = pathlib.Path(sys.argv[3]).resolve()
    directory = pathlib.Path(sys.argv[4]).resolve()
    topology = directory / "trr-writer-topology.xyz"
    output = directory / "frontend-roundtrip.trr"
    topology.write_text("2\nTRR writer topology\nC 0 0 0\nO 1 0 0\n")
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    load_args = {
        "path": str(topology), "name": "trr-system", "file-format": "xyz"
    }
    attach_args = {
        "path": str(trajectory), "file-format": "trr",
        "cache-mib": "1", "prefetch-frames": "0"
    }
    frame_args = {"frame": "1"}
    save_args = {
        "path": str(output), "file-format": "trr", "overwrite": "true"
    }
    python_results = [
        molshredder.invoke("load", load_args),
        molshredder.invoke("traj load", attach_args),
        molshredder.invoke("traj frame", frame_args),
        molshredder.invoke("traj save", save_args),
    ]
    require(all(item["status"] == "ok" for item in python_results),
            "Python TRR load/seek/save workflow failed")
    saved = python_results[-1]["data"]
    require(
        saved["format"] == "trr"
        and saved["frame_index"] == 1
        and saved["atom_count"] == 2
        and saved["has_time"]
        and saved["has_velocities"]
        and saved["has_forces"]
        and saved["has_unit_cell"]
        and saved["precision"] == "float32"
        and output.read_bytes()[:4] == bytes.fromhex("000007c9"),
        "Python TRR save response or XDR output lost trajectory channels",
    )

    script = (
        "format json\n"
        f'invoke "load" --file-format "xyz" --name "trr-system" '
        f'--path "{topology}"\n'
        f'invoke "traj load" --cache-mib "1" --file-format "trr" '
        f'--path "{trajectory}" --prefetch-frames "0"\n'
        'invoke "traj frame" --frame "1"\n'
        f'invoke "traj save" --file-format "trr" --overwrite "true" '
        f'--path "{output}"\n'
        "exit\n"
    )
    completed = subprocess.run(
        [str(cli_path), "console"], input=script, text=True,
        capture_output=True, check=True
    )
    cli_results = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    if cli_results != python_results:
        print(json.dumps({"cli": cli_results, "python": python_results},
                         indent=2), file=sys.stderr)
    require(cli_results == python_results,
            "CLI and Python TRR save result envelopes diverged")
    print("trr-writer-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
