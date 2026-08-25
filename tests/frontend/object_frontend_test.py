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
    require(len(sys.argv) == 5, "expected module, CLI, GUI probe and structure fixture")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    gui_probe = pathlib.Path(sys.argv[3]).resolve()
    fixture = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")
    python_envelopes = []

    for name in ("alpha", "beta", "gamma"):
        result = molshredder.invoke(
            "load", {"path": str(fixture), "name": name}
        )
        require(result["status"] == "ok", f"Python load failed for {name}")
        python_envelopes.append(result)
    visibility = molshredder.invoke(
        "object visibility", {"id": "2", "visible": "false"}
    )
    require(visibility["status"] == "ok",
        "Python object visibility failed",
    )
    python_envelopes.append(visibility)
    activation = molshredder.invoke("object activate", {"id": "1"})
    require(activation["status"] == "ok",
        "Python object activation failed",
    )
    python_envelopes.append(activation)
    for command, arguments in (
        ("object rename", {"object": "2", "name": "delta"}),
        ("object reorder", {"object": "3", "position": "1"}),
        ("object delete", {"object": "current"}),
    ):
        result = molshredder.invoke(command, arguments)
        require(result["status"] == "ok",
                f"Python {command} failed")
        python_envelopes.append(result)
    topology = molshredder.invoke(
        "object topology-retain",
        {"atom-ids": "3,1", "expected-version": "1"},
    )
    require(topology["status"] == "ok", "Python topology remap failed")
    python_envelopes.append(topology)
    python_objects = molshredder.invoke("object list")
    python_envelopes.append(python_objects)

    script = (
        "format json\n"
        f'invoke "load" --file-format "auto" --name "alpha" --path "{fixture}"\n'
        f'invoke "load" --file-format "auto" --name "beta" --path "{fixture}"\n'
        f'invoke "load" --file-format "auto" --name "gamma" --path "{fixture}"\n'
        'invoke "object visibility" --id "2" --visible "false"\n'
        'invoke "object activate" --id "1"\n'
        'invoke "object rename" --name "delta" --object "2"\n'
        'invoke "object reorder" --object "3" --position "1"\n'
        'invoke "object delete" --object "current"\n'
        'invoke "object topology-retain" --atom-ids "3,1" --expected-version "1"\n'
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
    require(len(envelopes) == 10, "CLI did not emit ten object workflow results")
    cli_objects = envelopes[-1]
    require(python_envelopes == envelopes,
            "CLI and Python lifecycle results diverged")
    gui_completed = subprocess.run(
        [str(gui_probe), "object-lifecycle", str(fixture)],
        text=True, capture_output=True, check=True
    )
    gui_envelopes = [json.loads(line) for line in gui_completed.stdout.splitlines()
                     if line.strip()]
    require(python_envelopes == gui_envelopes,
            "GUI, CLI and Python lifecycle results diverged")
    data = python_objects["data"]
    require(data["object_count"] == 2 and data["active_object_id"] == 2,
            "object list active/count fields are incorrect")
    rows = data["table"]["rows"]
    require(rows[0][0:3] == [3, "gamma", False] and rows[0][3:5] == [True, True],
            "reordered gamma state is incorrect")
    require(rows[1][0:3] == [2, "delta", True] and rows[1][3:5] == [False, False],
            "renamed active delta state is incorrect")
    require(rows[1][5] == 2 and
            topology["data"]["ordered_atom_ids"] == [3, 1] and
            topology["data"]["topology_version"] == 2,
            "stable topology mutation result is incorrect")
    print("object-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
