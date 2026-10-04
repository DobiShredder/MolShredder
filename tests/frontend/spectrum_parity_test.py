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
        'invoke "bg_color" --color "white"',
        'invoke "spectrum" --expression "count" --palette "blue_red" '
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
    require(envelopes, "CLI spectrum sequence emitted no result")
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
        ("show", {"representation": "spheres", "selection": "all"}),
        ("bg_color", {"color": "white"}),
    ):
        require(
            molshredder.invoke(name, arguments)["status"] == "ok",
            f"Python spectrum setup failed: {name}",
        )
    python_result = molshredder.invoke(
        "spectrum",
        {"expression": "count", "palette": "blue_red", "selection": "all"},
    )
    cli_result = console_result(cli_path, fixture)
    gui_result = json.loads(
        subprocess.run(
            [str(gui_path), "spectrum", str(fixture)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    )
    require(
        python_result == cli_result == gui_result,
        "CLI, GUI adapter and Python spectrum results diverged",
    )
    require(
        python_result["command"]
        == 'invoke "spectrum" --byres "false" --expression "count" '
        '--interpolation "rgb" --palette "blue_red" --selection "all"',
        "spectrum canonical invocation drifted",
    )
    data = python_result["data"]
    require(
        data["mode"] == "palette"
        and data["palette"] == "blue_red"
        and data["minimum"] == 1.0
        and data["affected_atom_count"] == data["colored_atom_count"],
        "spectrum response lost range or mode",
    )
    background = molshredder.invoke("background get", {})
    require(
        background["status"] == "ok"
        and background["data"]["color_name"] == "white"
        and background["data"]["rgb"] == [1.0, 1.0, 1.0],
        "bg_color alias must reach the canonical background state",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
