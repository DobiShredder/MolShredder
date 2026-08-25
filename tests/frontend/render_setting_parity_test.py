#!/usr/bin/env python3

import importlib
import json
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def console_result(executable: pathlib.Path, fixture: pathlib.Path) -> dict:
    quoted = str(fixture).replace('"', '\\"')
    commands = [
        f'invoke "load" --file-format "g96" --path "{quoted}"',
        'invoke "show" --representation "spheres" --selection "all"',
        'invoke "setting set" --name "sphere_scale" --value "2.5" '
        '--scope "atom" --target "1"',
        'invoke "setting get" --name "sphere_scale" --scope "atom" '
        '--target "1"',
    ]
    completed = subprocess.run(
        [str(executable), "console"],
        input="format json\n" + "\n".join(commands) + "\nexit\n",
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
    require(envelopes, "CLI setting sequence emitted no result")
    return envelopes[-1]


def main() -> int:
    require(len(sys.argv) == 5, "expected module, CLI, GUI probe and fixture")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    gui_path = pathlib.Path(sys.argv[3]).resolve()
    fixture = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    require(
        molshredder.invoke(
            "load", {"file-format": "g96", "path": str(fixture)}
        )["status"]
        == "ok",
        "Python setting fixture load failed",
    )
    require(
        molshredder.invoke(
            "show", {"representation": "spheres", "selection": "all"}
        )["status"]
        == "ok",
        "Python sphere setup failed",
    )
    require(
        molshredder.invoke(
            "setting set",
            {
                "name": "sphere_scale",
                "value": "2.5",
                "scope": "atom",
                "target": "1",
            },
        )["status"]
        == "ok",
        "Python setting mutation failed",
    )
    python_result = molshredder.invoke(
        "setting get", {"name": "sphere_scale", "scope": "atom", "target": "1"}
    )
    cli_result = console_result(cli_path, fixture)
    gui_result = json.loads(
        subprocess.run(
            [str(gui_path), "render-setting", str(fixture)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    )
    require(
        python_result == cli_result == gui_result,
        "CLI, GUI adapter and Python render setting results diverged",
    )
    require(
        python_result["command"]
        == 'invoke "setting get" --name "sphere_scale" --object "current" '
        '--scope "atom" --state "current" --target "1"',
        "render setting canonical invocation drifted",
    )
    data = python_result["data"]
    require(
        data["value"] == 2.5
        and data["source_scope"] == "atom"
        and data["requested_scope"] == "atom"
        and data["type"] == "number",
        "render setting response lost typed resolution provenance",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
