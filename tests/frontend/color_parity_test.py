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


def console_result(
    executable: pathlib.Path, fixture: pathlib.Path, workspace: bool = False
) -> dict:
    quoted = str(fixture).replace('"', '\\"')
    commands = [
        f'invoke "load" --file-format "g96" --path "{quoted}"',
        *(
            [f'invoke "load" --file-format "g96" --name "second" --path "{quoted}"']
            if workspace
            else []
        ),
        'invoke "show" --representation "spheres" '
        + ('--scope "workspace" ' if workspace else "")
        + '--selection "all"',
        'invoke "color define" --name "mycolor" --red "255" --green "128" '
        '--blue "0"',
        'invoke "color" --color "mycolor" '
        + ('--scope "workspace" ' if workspace else "")
        + '--selection "index 1"',
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
    require(envelopes, "CLI color sequence emitted no result")
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
        ("color define",
         {"name": "mycolor", "red": "255", "green": "128", "blue": "0"}),
    ):
        require(
            molshredder.invoke(name, arguments)["status"] == "ok",
            f"Python color setup failed: {name}",
        )
    python_result = molshredder.invoke(
        "color", {"color": "mycolor", "selection": "index 1"}
    )
    cli_result = console_result(cli_path, fixture)
    gui_result = json.loads(
        subprocess.run(
            [str(gui_path), "color", str(fixture)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    )
    require(
        python_result == cli_result == gui_result,
        "CLI, GUI adapter and Python color results diverged",
    )
    require(
        python_result["command"]
        == 'invoke "color" --color "mycolor" --scope "active" --selection "index 1"',
        "color canonical invocation drifted",
    )
    data = python_result["data"]
    # auto_color (default on) gives every atom an explicit load color, so all atoms count as colored.
    atom_total = molshredder.invoke("object count", {})["data"]["atom_count"]
    require(
        data["color_index"] == 5388
        and data["color_name"] == "mycolor"
        and data["affected_atom_count"] == 1
        and data["colored_atom_count"] == atom_total,
        "color response lost table resolution",
    )
    # Q-021 b: workspace scope colors the selection in every object.
    require(
        molshredder.invoke(
            "load", {"file-format": "g96", "name": "second", "path": str(fixture)}
        )["status"] == "ok"
        and molshredder.invoke(
            "show",
            {"representation": "spheres", "scope": "workspace", "selection": "all"},
        )["status"] == "ok",
        "Python second object setup failed",
    )
    workspace_python = molshredder.invoke(
        "color", {"color": "mycolor", "scope": "workspace", "selection": "index 1"}
    )
    workspace_cli = console_result(cli_path, fixture, workspace=True)
    workspace_gui = json.loads(
        subprocess.run(
            [str(gui_path), "color-workspace", str(fixture)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    )
    require(
        workspace_python == workspace_cli == workspace_gui
        and workspace_python["data"]["affected_atom_count"] == 2,
        "workspace-scope color diverged across frontends",
    )
    queried = molshredder.invoke("color get", {"color": "MYCOLOR"})
    require(
        queried["status"] == "ok"
        and queried["data"]["color_index"] == 5388
        and abs(queried["data"]["rgb"][1] - 128.0 / 255.0) < 1.0e-6,
        "color get must resolve custom colors case-insensitively",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
