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
    require(len(sys.argv) == 5, "expected module, CLI, GUI probe and fixture")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    gui_probe = pathlib.Path(sys.argv[3]).resolve()
    fixture = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")
    arguments = {
        "file-format": "pdb",
        "names": "batch-one;batch-two",
        "paths": f"{fixture};{fixture}",
    }
    python_result = molshredder.invoke("load batch", arguments)
    require(python_result["status"] == "ok", "Python batch load failed")

    script = (
        "format json\n"
        f'invoke "load batch" --file-format "pdb" '
        f'--names "batch-one;batch-two" --paths "{fixture};{fixture}"\n'
        "exit\n"
    )
    completed = subprocess.run(
        [str(cli_path), "console"], input=portable_console_script(script),
        text=True, capture_output=True, check=True
    )
    cli_results = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(cli_results == [python_result], "CLI and Python batch results diverged")

    gui_completed = subprocess.run(
        [str(gui_probe), "load-batch", str(fixture)],
        text=True, capture_output=True, check=True
    )
    gui_results = [json.loads(line) for line in gui_completed.stdout.splitlines()
                   if line.strip()]
    require(gui_results == [python_result],
            "GUI, CLI and Python batch results diverged")
    data = python_result["data"]
    require(data["input_count"] == 2 and data["structure_count"] == 2 and
            data["object_ids"] == [1, 2] and data["active_object_id"] == 2,
            "batch result identity/count contract is incorrect")
    print("batch-load-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
