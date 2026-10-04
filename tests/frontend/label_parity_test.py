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


def console_result(executable: pathlib.Path, fixture: pathlib.Path) -> dict:
    quoted = str(fixture).replace('"', '\\"')
    commands = [
        f'invoke "load" --file-format "g96" --path "{quoted}"',
        'invoke "show" --representation "spheres" --selection "all"',
        'invoke "setting set" --name "label_digits" --value "2"',
        'invoke "label" --expression "\\"%s-%s\\" % (resn, resi)" '
        '--selection "all"',
    ]
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
    require(envelopes, "CLI label sequence emitted no result")
    return envelopes[-1]


def main() -> int:
    require(len(sys.argv) == 5, "expected module, CLI, GUI probe and fixture")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    gui_path = pathlib.Path(sys.argv[3]).resolve()
    fixture = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    for name, arguments in (
        ("load", {"file-format": "g96", "path": str(fixture)}),
        ("setting set", {"name": "label_digits", "value": "2"}),
    ):
        require(
            molshredder.invoke(name, arguments)["status"] == "ok",
            f"Python label setup failed: {name}",
        )
    python_result = molshredder.invoke(
        "label", {"expression": '"%s-%s" % (resn, resi)', "selection": "all"}
    )
    cli_result = console_result(cli_path, fixture)
    gui_result = json.loads(
        subprocess.run(
            [str(gui_path), "label", str(fixture)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    )
    require(
        python_result == cli_result == gui_result,
        "CLI, GUI adapter and Python label results diverged",
    )
    data = python_result["data"]
    require(
        data["affected_atom_count"] == data["labeled_atom_count"]
        and data["visible_label_count"] == data["labeled_atom_count"]
        and "-" in data["first_text"],
        "label response lost evaluated text or visibility",
    )
    numeric = molshredder.invoke("label", {"expression": "b * 2", "selection": "all"})
    require(
        numeric["status"] == "ok" and numeric["data"]["first_text"].count(".") == 1
        and len(numeric["data"]["first_text"].split(".")[1]) == 2,
        "label_digits must format float labels",
    )
    failed = molshredder.invoke("label", {"expression": "undefined_name"})
    require(failed["status"] != "ok", "unknown label names must fail")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
