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
    require(len(sys.argv) == 5,
            "expected module, CLI, multi-frame XYZ and output directory")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    trajectory = pathlib.Path(sys.argv[3]).resolve()
    output = pathlib.Path(sys.argv[4]).resolve() / "frontend-roundtrip.dcd"
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    load_args = {
        "path": str(trajectory), "name": "dcd-system", "file-format": "xyz"
    }
    frame_args = {"frame": "1"}
    save_args = {
        "path": str(output), "file-format": "dcd",
        "title": "frontend DCD", "overwrite": "true"
    }
    attach_args = {
        "path": str(output), "file-format": "dcd",
        "cache-mib": "1", "prefetch-frames": "0", "mapping": "index"
    }
    python_results = [
        molshredder.invoke("load", load_args),
        molshredder.invoke("traj frame", frame_args),
        molshredder.invoke("traj save", save_args),
        molshredder.invoke("traj load", attach_args),
    ]
    require(all(item["status"] == "ok" for item in python_results),
            "Python XYZ seek/DCD save/attach workflow failed")
    saved = python_results[2]["data"]
    attached = python_results[3]["data"]
    require(
        saved["format"] == "dcd"
        and saved["frame_index"] == 1
        and saved["atom_count"] == 3
        and not saved["has_unit_cell"]
        and saved["precision"] == "float32"
        and saved["loss_item_count"] > 0
        and attached["format"] == "dcd"
        and attached["frame_count"] == 1
        and output.read_bytes()[:8] == bytes.fromhex("54000000434f5244"),
        "Python DCD response, header or read-back contract is incomplete",
    )

    script = (
        "format json\n"
        f'invoke "load" --file-format "xyz" --name "dcd-system" '
        f'--path "{trajectory}"\n'
        'invoke "traj frame" --frame "1"\n'
        f'invoke "traj save" --file-format "dcd" --overwrite "true" '
        f'--path "{output}" --title "frontend DCD"\n'
        f'invoke "traj load" --cache-mib "1" --file-format "dcd" '
        f'--path "{output}" --prefetch-frames "0" --mapping "index"\n'
        "exit\n"
    )
    completed = subprocess.run(
        [str(cli_path), "console"], input=portable_console_script(script),
        text=True,
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
            "CLI and Python DCD result envelopes diverged")
    print("dcd-writer-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
