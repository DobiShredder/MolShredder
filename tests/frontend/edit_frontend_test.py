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

    actions = (
        ("load", {"path": str(fixture), "name": "editable"}),
        ("edit atom-position", {
            "atom-id": "1", "x": "9", "y": "8", "z": "7",
            "expected-topology-version": "1",
            "expected-coordinate-source-revision": "1", "unit": "angstrom",
        }),
        ("edit undo", {}),
        ("edit redo", {}),
        ("edit history", {}),
    )
    python_envelopes = []
    for command, arguments in actions:
        result = molshredder.invoke(command, arguments)
        require(result["status"] == "ok", f"Python {command} failed")
        python_envelopes.append(result)

    script = (
        "format json\n"
        f'invoke "load" --name "editable" --path "{fixture}"\n'
        'invoke "edit atom-position" --atom-id "1" --x "9" --y "8" --z "7" '
        '--expected-topology-version "1" --expected-coordinate-source-revision "1" '
        '--unit "angstrom"\n'
        'invoke "edit undo"\n'
        'invoke "edit redo"\n'
        'invoke "edit history"\n'
        "exit\n"
    )
    completed = subprocess.run(
        [str(cli_path), "console"], input=portable_console_script(script),
        text=True, capture_output=True, check=True,
    )
    cli_envelopes = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(len(cli_envelopes) == 5, "CLI did not emit five edit workflow results")
    require(python_envelopes == cli_envelopes,
            "CLI and Python edit workflow results diverged")

    gui_completed = subprocess.run(
        [str(gui_probe), "edit-workflow", str(fixture)],
        text=True, capture_output=True, check=True,
    )
    gui_envelopes = [json.loads(line) for line in gui_completed.stdout.splitlines()
                     if line.strip()]
    require(python_envelopes == gui_envelopes,
            "GUI, CLI and Python edit workflow results diverged")

    edit = python_envelopes[1]["data"]
    undo = python_envelopes[2]["data"]
    redo = python_envelopes[3]["data"]
    history = python_envelopes[4]["data"]
    require(edit["transaction_id"] == 1 and edit["position"] == [9, 8, 7],
            "coordinate edit transaction is incorrect")
    require(edit["diff_schema_version"] == 1 and
            edit["transaction_kind"] == "atom_position" and
            edit["coordinate_source_revision"] == 2 and
            edit["previous_coordinate_source_revision"] == 1 and
            edit["previous_coordinate_revision"] == 1 and
            edit["affected_state_count"] == 1 and
            edit["state_scope"] == "all",
            "coordinate edit revision/scope is incorrect")
    require(undo["transaction_id"] == 1 and
            undo["transaction_kind"] == "atom_position" and
            undo["coordinate_source_revision"] == 3,
            "undo transaction is incorrect")
    require(redo["transaction_id"] == 1 and
            redo["transaction_kind"] == "atom_position" and
            redo["coordinate_source_revision"] == 4,
            "redo transaction is incorrect")
    require(history["undo_count"] == 1 and history["redo_count"] == 0 and
            history["memory_used_bytes"] > 0,
            "edit history accounting is incorrect")
    print("edit-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
