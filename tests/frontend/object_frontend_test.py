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
    require(len(sys.argv) == 4, "expected module, CLI and structure fixture")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    fixture = pathlib.Path(sys.argv[3]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    for name in ("alpha", "beta"):
        result = molshredder.invoke(
            "load", {"path": str(fixture), "name": name}
        )
        require(result["status"] == "ok", f"Python load failed for {name}")
    require(
        molshredder.invoke(
            "object visibility", {"id": "2", "visible": "false"}
        )["status"]
        == "ok",
        "Python object visibility failed",
    )
    require(
        molshredder.invoke("object activate", {"id": "1"})["status"]
        == "ok",
        "Python object activation failed",
    )
    python_objects = molshredder.invoke("object list")

    script = (
        "format json\n"
        f'invoke "load" --file-format "auto" --name "alpha" --path "{fixture}"\n'
        f'invoke "load" --file-format "auto" --name "beta" --path "{fixture}"\n'
        'invoke "object visibility" --id "2" --visible "false"\n'
        'invoke "object activate" --id "1"\n'
        'invoke "object list"\n'
        "exit\n"
    )
    completed = subprocess.run(
        [str(cli_path), "console"], input=portable_console_script(script),
        text=True,
        capture_output=True, check=True
    )
    envelopes = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(len(envelopes) == 5, "CLI did not emit five object workflow results")
    cli_objects = envelopes[-1]
    require(python_objects == cli_objects, "CLI and Python object state diverged")
    data = python_objects["data"]
    require(data["object_count"] == 2 and data["active_object_id"] == 1,
            "object list active/count fields are incorrect")
    rows = data["table"]["rows"]
    require(rows[0][2:5] == [True, True, True],
            "first object active/visibility state is incorrect")
    require(rows[1][2:5] == [False, False, False],
            "second object active/visibility state is incorrect")
    print("object-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
