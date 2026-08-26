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


def run_console(executable: pathlib.Path, commands: list[str]) -> dict:
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
    for line in completed.stdout.splitlines():
        marker = line.find("{")
        if marker < 0:
            continue
        try:
            envelopes.append(json.loads(line[marker:]))
        except json.JSONDecodeError:
            pass
    require(envelopes, "CLI console emitted no JSON envelope")
    return envelopes[-1]


def main() -> int:
    require(
        len(sys.argv) == 5,
        "expected Python module, CLI, GUI probe and G96 fixture paths",
    )
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    gui_path = pathlib.Path(sys.argv[3]).resolve()
    fixture = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    operations = [
        ("show", {"representation": "wire", "selection": "all"}),
        ("show", {"representation": "spheres", "selection": "index 1"}),
        ("hide", {"representation": "lines", "selection": "index 1"}),
        ("as", {"representation": "licorice", "selection": "index 2"}),
        ("toggle", {"representation": "everything", "selection": "index 2"}),
    ]
    loaded = molshredder.invoke(
        "load", {"file-format": "g96", "path": str(fixture)}
    )
    require(loaded["status"] == "ok", "Python fixture load failed")
    python_result = None
    for command, arguments in operations:
        python_result = molshredder.invoke(command, arguments)

    quoted = str(fixture).replace('"', '\\"')
    console_commands = [
        f'invoke "load" --file-format "g96" --path "{quoted}"',
        *[
            f'invoke "{command}" --representation "{arguments["representation"]}" '
            f'--selection "{arguments["selection"]}"'
            for command, arguments in operations
        ],
    ]
    cli_result = run_console(cli_path, console_commands)
    gui_completed = subprocess.run(
        [str(gui_path), "representation-visibility", str(fixture)],
        check=True,
        capture_output=True,
        text=True,
    )
    gui_result = json.loads(gui_completed.stdout)

    require(
        python_result == cli_result == gui_result,
        "CLI, GUI adapter and Python representation visibility results diverged",
    )
    require(
        python_result["command"]
        == 'invoke "toggle" --representation "everything" --selection "index 2"',
        "toggle did not preserve canonical invocation provenance",
    )
    data = python_result["data"]
    require(
        data["operation"] == "toggle"
        and data["resolved_representations"]
        == "lines,sticks,spheres,ribbon,cartoon"
        and data["affected_atom_count"] == 1
        and data["visible_membership_count"] == 1
        and data["representation_count"] == 1,
        "aggregate toggle did not return the expected canonical state summary",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
