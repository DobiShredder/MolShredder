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
            "expected module, CLI, PRMTOP, RST7 and output directory")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    topology = pathlib.Path(sys.argv[3]).resolve()
    restart = pathlib.Path(sys.argv[4]).resolve()
    output = pathlib.Path(sys.argv[5]).resolve() / "frontend-roundtrip.rst7"
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    load_args = {
        "path": str(topology), "name": "amber", "file-format": "prmtop"
    }
    attach_args = {
        "path": str(restart), "file-format": "rst7",
        "cache-mib": "1", "prefetch-frames": "0"
    }
    save_args = {
        "path": str(output), "file-format": "rst7",
        "title": "frontend restart", "overwrite": "true"
    }
    python_results = [
        molshredder.invoke("load", load_args),
        molshredder.invoke("traj load", attach_args),
        molshredder.invoke("traj save", save_args),
    ]
    require(all(item["status"] == "ok" for item in python_results),
            "Python RST7 load/attach/save workflow failed")
    saved = python_results[-1]["data"]
    require(
        saved["format"] == "rst7"
        and saved["frame_index"] == 0
        and saved["atom_count"] == 4
        and saved["has_time"]
        and saved["has_temperature"]
        and saved["has_velocities"]
        and saved["has_unit_cell"]
        and saved["loss_channel_count"] == 5
        and output.read_text().startswith("frontend restart\n     4 "),
        "Python RST7 save response or output lost restart channels",
    )

    script = (
        "format json\n"
        f'invoke "load" --file-format "prmtop" --name "amber" '
        f'--path "{topology}"\n'
        f'invoke "traj load" --cache-mib "1" --file-format "rst7" '
        f'--path "{restart}" --prefetch-frames "0"\n'
        f'invoke "traj save" --file-format "rst7" --overwrite "true" '
        f'--path "{output}" --title "frontend restart"\n'
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
            "CLI and Python RST7 save result envelopes diverged")
    print("rst7-writer-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
