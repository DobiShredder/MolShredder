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


def console_result(executable: pathlib.Path, commands: list[str]) -> dict:
    completed = subprocess.run(
        [str(executable), "console"],
        input=portable_console_script(
            "format json\n" + "\n".join(commands) + "\nexit\n"
        ),
        check=True,
        capture_output=True,
        text=True,
    )
    envelopes = []
    for line in (completed.stdout + "\n" + completed.stderr).splitlines():
        start = line.find('{"schema_version"')
        if start >= 0:
            envelopes.append(json.loads(line[start:]))
    require(envelopes, "CLI console emitted no JSON envelope")
    return envelopes[-1]


def gui_result(executable: pathlib.Path, scenario: str,
               fixture: pathlib.Path) -> dict:
    completed = subprocess.run(
        [str(executable), scenario, str(fixture)],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def main() -> int:
    require(len(sys.argv) == 5,
            "expected Python module, CLI, GUI probe and PDB fixture paths")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    gui_path = pathlib.Path(sys.argv[3]).resolve()
    fixture = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    expression = "formal_charge > 0 or index * 2 = 4"
    load_args = {"file-format": "pdb", "path": str(fixture)}
    select_args = {
        "expression": expression,
        "name": "numeric_atoms",
        "update": "true",
    }
    show_args = {"representation": "spheres", "selection": "@numeric_atoms"}
    require(molshredder.invoke("load", load_args)["status"] == "ok",
            "Python numeric selection fixture load failed")
    require(molshredder.invoke("select", select_args)["status"] == "ok",
            "Python numeric selection definition failed")
    python_success = molshredder.invoke("show", show_args)

    quoted = str(fixture).replace('"', '\\"')
    cli_success = console_result(cli_path, [
        f'invoke "load" --file-format "pdb" --path "{quoted}"',
        f'invoke "select" --expression "{expression}" '
        '--name "numeric_atoms" --update "true"',
        'invoke "show" --representation "spheres" '
        '--selection "@numeric_atoms"',
    ])
    gui_success = gui_result(gui_path, "selection-numeric", fixture)
    require(python_success == cli_success == gui_success,
            "numeric selection success envelopes diverged across frontends")
    require(python_success["data"]["affected_atom_count"] == 2 and
            python_success["data"]["visible_membership_count"] == 2,
            "numeric expression selected an unexpected atom set")

    spatial_args = {
        "expression": "index 1 around 2",
        "name": "spatial_atoms",
        "update": "true",
    }
    require(molshredder.invoke("select", spatial_args)["status"] == "ok",
            "Python spatial selection definition failed")
    python_spatial = molshredder.invoke(
        "show", {"representation": "lines", "selection": "@spatial_atoms"}
    )
    cli_spatial = console_result(cli_path, [
        f'invoke "load" --file-format "pdb" --path "{quoted}"',
        f'invoke "select" --expression "{expression}" '
        '--name "numeric_atoms" --update "true"',
        'invoke "show" --representation "spheres" '
        '--selection "@numeric_atoms"',
        'invoke "select" --expression "index 1 around 2" '
        '--name "spatial_atoms" --update "true"',
        'invoke "show" --representation "lines" '
        '--selection "@spatial_atoms"',
    ])
    gui_spatial = gui_result(gui_path, "selection-spatial", fixture)
    require(python_spatial == cli_spatial == gui_spatial,
            "spatial selection envelopes diverged across frontends:\n" +
            json.dumps({"python": python_spatial, "cli": cli_spatial,
                        "gui": gui_spatial}, indent=2, sort_keys=True))
    require(python_spatial["status"] == "ok" and
            python_spatial["data"]["affected_atom_count"] == 1,
            "spatial expression selected an unexpected atom set:\n" +
            json.dumps(python_spatial, indent=2, sort_keys=True))

    bad_args = {
        "expression": "formal_charge / 0 > 1",
        "name": "bad_numeric",
        "update": "true",
    }
    python_error = molshredder.invoke("select", bad_args)
    cli_error = console_result(cli_path, [
        f'invoke "load" --file-format "pdb" --path "{quoted}"',
        'invoke "select" --expression "formal_charge / 0 > 1" '
        '--name "bad_numeric" --update "true"',
    ])
    gui_error = gui_result(gui_path, "selection-numeric-error", fixture)
    require(python_error == cli_error == gui_error,
            "numeric selection error envelopes diverged across frontends:\n" +
            json.dumps({"python": python_error, "cli": cli_error,
                        "gui": gui_error}, indent=2, sort_keys=True))
    require(python_error["status"] == "error" and
            python_error["error"]["code"] == "invalid_selection" and
            "divides by zero" in python_error["error"]["message"],
            "numeric selection error contract is unstable")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
