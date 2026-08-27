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
    require(len(sys.argv) == 4, "expected module, CLI and PDB fixture")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    fixture = pathlib.Path(sys.argv[3]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    load_args = {"path": str(fixture)}
    surface_args = {
        "kind": "vdw",
        "selection": "all",
        "probe-radius": "0",
        "grid-spacing": "0.5",
        "color": "cyan",
        "opacity": "0.72",
        "voxel-budget": "100000",
        "memory-budget-bytes": "8388608",
    }
    python_results = [
        molshredder.invoke("load", load_args),
        molshredder.invoke("surface show", surface_args),
    ]
    require(all(item["status"] == "ok" for item in python_results),
            "Python molecular surface workflow failed")
    surface = python_results[1]["data"]
    require(
        surface["algorithm"] == "union-sphere-signed-distance"
        and surface["algorithm_version"] == "1"
        and surface["kind"] == "van-der-waals"
        and surface["probe_radius_angstrom"] == 0.0
        and surface["grid_spacing_angstrom"] == 0.5
        and surface["selection"] == "all"
        and surface["voxel_count"] <= surface["voxel_budget"]
        and surface["vertex_count"] > 0
        and surface["triangle_count"] > 0
        and surface["pick_target_count"] == 1,
        "Python molecular surface response lost scientific or resource provenance",
    )

    script = (
        "format json\n"
        f'invoke "load" --path "{fixture}"\n'
        'invoke "surface show" --kind "vdw" --selection "all" '
        '--probe-radius "0" --grid-spacing "0.5" --color "cyan" '
        '--opacity "0.72" --voxel-budget "100000" '
        '--memory-budget-bytes "8388608"\n'
        "exit\n"
    )
    completed = subprocess.run(
        [str(cli_path), "console"], input=portable_console_script(script),
        text=True, capture_output=True, check=True
    )
    cli_results = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(cli_results == python_results,
            "GUI registry, CLI and Python surface envelopes diverged")
    print("molecular-surface-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
