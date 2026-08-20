#!/usr/bin/env python3

import importlib
import json
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_cli(executable: pathlib.Path, arguments: list[str], expected_code: int) -> dict:
    completed = subprocess.run(
        [str(executable), *arguments, "--format", "json"],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        completed.returncode == expected_code,
        f"CLI returned {completed.returncode}, expected {expected_code}: {completed.stderr}",
    )
    payload = completed.stdout if expected_code == 0 else completed.stderr
    require(bool(payload), "CLI did not produce its JSON result envelope")
    return json.loads(payload)


def run_gui(probe: pathlib.Path, scenario: str) -> dict:
    completed = subprocess.run(
        [str(probe), scenario], check=True, capture_output=True, text=True
    )
    return json.loads(completed.stdout)


def main() -> int:
    require(len(sys.argv) == 4, "expected module, CLI and GUI probe paths")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    gui_probe_path = pathlib.Path(sys.argv[3]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    version_python = molshredder.invoke("version")
    version_cli = run_cli(cli_path, ["version"], 0)
    version_gui = run_gui(gui_probe_path, "version")
    require(
        version_python == version_cli == version_gui,
        "CLI, GUI and Python version actions diverged",
    )
    require(
        version_python["command"] == 'invoke "system version"',
        "version alias was not normalized canonically",
    )
    require(
        version_python["data"] == {"result_schema_version": 2, "version": "0.1.0"},
        "typed version fields did not survive all frontend conversions",
    )

    center_python = molshredder.invoke("com", {"selection": "protein"})
    center_cli = run_cli(cli_path, ["com", "--selection", "protein"], 2)
    center_gui = run_gui(gui_probe_path, "com")
    require(
        center_python == center_cli == center_gui,
        "CLI, GUI and Python COM actions diverged",
    )
    require(
        center_python["error"]["code"] == "not_found",
        "shared unloaded-workspace operation did not preserve its stable error",
    )
    require(
        center_python["command"]
        == 'invoke "analyze center" --mode "com" --precision "6" '
        '--selection "protein" --unit "angstrom"',
        "frontend parity did not include normalized defaults",
    )

    print("frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
