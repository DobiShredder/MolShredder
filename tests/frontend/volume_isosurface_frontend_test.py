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


def colors_near(left: list[float], right: list[float]) -> bool:
    return len(left) == len(right) and all(
        abs(a - b) < 1.0e-6 for a, b in zip(left, right)
    )


def main() -> int:
    require(len(sys.argv) == 4, "expected module, CLI and MRC fixture")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    fixture = pathlib.Path(sys.argv[3]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    load_args = {
        "path": str(fixture),
        "name": "density",
        "file-format": "mrc",
        "coordinate-unit": "angstrom",
    }
    surface_args = {
        "level": "50",
        "color": "orange",
        "opacity": "0.6",
        "replace": "true",
    }
    python_results = [
        molshredder.invoke("volume load", load_args),
        molshredder.invoke("volume isosurface", surface_args),
        molshredder.invoke("volume list", {}),
    ]
    require(all(item["status"] == "ok" for item in python_results),
            "Python isosurface workflow failed")
    surface = python_results[1]["data"]
    require(
        surface["algorithm"] == "marching-tetrahedra"
        and surface["level"] == 50.0
        and colors_near(surface["color"], [1.0, 0.5, 0.15, 0.6])
        and surface["vertex_count"] > 0
        and surface["triangle_count"] > 0
        and surface["bounds"] is not None
        and python_results[2]["data"]["volumes"][0]
        ["representation_count"] == 1,
        "Python isosurface response lost mesh or representation state",
    )

    script = (
        "format json\n"
        f'invoke "volume load" --coordinate-unit "angstrom" '
        f'--file-format "mrc" --name "density" --path "{fixture}"\n'
        'invoke "volume isosurface" --color "orange" --level "50" '
        '--opacity "0.6" --replace "true"\n'
        'invoke "volume list"\n'
        "exit\n"
    )
    completed = subprocess.run(
        [str(cli_path), "console"], input=portable_console_script(script),
        text=True,
        capture_output=True, check=True
    )
    cli_results = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    if cli_results != python_results:
        print(json.dumps({"cli": cli_results, "python": python_results},
                         indent=2), file=sys.stderr)
    require(cli_results == python_results,
            "CLI and Python isosurface result envelopes diverged")
    print("volume-isosurface-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
