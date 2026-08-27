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

    actions = (
        ("load", {"name": "session-object", "path": str(fixture)}),
        ("show", {"representation": "spheres", "selection": "all"}),
        ("scene store", {"name": "baseline"}),
        ("movie configure", {"fps": "24", "frames": "3", "loop": "true"}),
        ("movie keyframe", {"frame": "2", "scene": "baseline"}),
        ("object visibility", {"id": "1", "visible": "false"}),
        ("movie seek", {"frame": "2"}),
        ("movie play", {}),
        ("movie pause", {}),
        ("movie status", {}),
    )
    python_results = []
    for command, arguments in actions:
        result = molshredder.invoke(command, arguments)
        require(result["status"] == "ok", f"Python {command} failed")
        python_results.append(result)

    script_lines = ["format json"]
    for command, arguments in actions:
        options = " ".join(
            f'--{name} "{str(value).replace(chr(92), chr(47))}"'
            for name, value in arguments.items()
        )
        script_lines.append(f'invoke "{command}" {options}'.rstrip())
    script_lines.append("exit")
    completed = subprocess.run(
        [str(cli_path), "console"],
        input=portable_console_script("\n".join(script_lines) + "\n"),
        text=True,
        capture_output=True,
        check=True,
    )
    cli_results = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(python_results == cli_results,
            "CLI and Python scene/movie results diverged")

    gui_completed = subprocess.run(
        [str(gui_probe), "session-workflow", str(fixture)],
        text=True,
        capture_output=True,
        check=True,
    )
    gui_results = [json.loads(line) for line in gui_completed.stdout.splitlines()
                   if line.strip()]
    require(python_results == gui_results,
            "GUI adapter, CLI and Python scene/movie results diverged")

    movie = python_results[-1]["data"]["movie"]
    require(movie["frame_count"] == 3 and movie["current_frame"] == 2 and
            movie["fps"] == 24 and movie["loop"] and not movie["playing"] and
            movie["security"] == "typed-scene-trajectory-keys-only" and
            movie["keyframes"] == [{"frame": 2, "scene": "baseline",
                                     "trajectory_frame": None}],
            "typed movie state is incorrect")
    print("session-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
