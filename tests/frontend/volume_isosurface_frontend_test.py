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
    slice_args = {
        "axis": "z",
        "index": "1",
        "minimum-color": "blue",
        "maximum-color": "orange",
        "opacity": "0.7",
        "memory-budget-bytes": "1048576",
        "replace": "true",
    }
    python_results = [
        molshredder.invoke("volume load", load_args),
        molshredder.invoke("volume isosurface", surface_args),
        molshredder.invoke("volume slice", slice_args),
        molshredder.invoke("volume ramp set", {"preset": "fire"}),
        molshredder.invoke("volume ramp get", {}),
        molshredder.invoke("volume ramp define", {
            "name": "focus",
            "points": "0,0,0,0,0;100,1,0.5,0,0.8",
        }),
        molshredder.invoke("volume ramp get", {}),
        molshredder.invoke("volume render", {
            "mode": "post-classified",
            "sampling-step": "0.5",
            "maximum-steps": "128",
            "lookup-table-samples": "64",
            "texture-budget-bytes": "1048576",
        }),
        molshredder.invoke("volume hide", {}),
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
        and python_results[2]["data"]["algorithm"] == "orthogonal-grid-slice"
        and python_results[2]["data"]["algorithm_version"] == 1
        and python_results[2]["data"]["axis"] == "z"
        and python_results[2]["data"]["index"] == 1
        and python_results[2]["data"]["vertex_count"] == 4
        and python_results[2]["data"]["triangle_count"] == 2
        and python_results[2]["data"]["pick_target_count"] == 1
        and python_results[2]["data"]["required_bytes"] > 0
        and python_results[2]["data"]["required_bytes"] <= 1048576
        and python_results[3]["data"]["algorithm"] == "piecewise-linear-rgba"
        and python_results[3]["data"]["algorithm_version"] == 1
        and python_results[3]["data"]["name"] == "fire"
        and python_results[3]["data"] == python_results[4]["data"]
        and len(python_results[3]["data"]["points"]) == 4
        and python_results[5]["data"]["name"] == "focus"
        and python_results[5]["data"] == python_results[6]["data"]
        and len(python_results[5]["data"]["points"]) == 2
        and python_results[7]["data"]["algorithm"] == "front-to-back-volume-ray-march"
        and python_results[7]["data"]["algorithm_version"] == 1
        and python_results[7]["data"]["mode"] == "post-classified"
        and python_results[7]["data"]["transfer_function"] == "focus"
        and python_results[7]["data"]["required_texture_bytes"] <= 1048576
        and python_results[8]["data"]["removed"] is True
        and python_results[9]["data"]["volumes"][0]
        ["representation_count"] == 0,
        "Python volume response lost surface, slice or representation state: "
        + json.dumps(python_results, sort_keys=True),
    )

    script = (
        "format json\n"
        f'invoke "volume load" --coordinate-unit "angstrom" '
        f'--file-format "mrc" --name "density" --path "{fixture}"\n'
        'invoke "volume isosurface" --color "orange" --level "50" '
        '--opacity "0.6" --replace "true"\n'
        'invoke "volume slice" --axis "z" --index "1" '
        '--minimum-color "blue" --maximum-color "orange" '
        '--opacity "0.7" --memory-budget-bytes "1048576" '
        '--replace "true"\n'
        'invoke "volume ramp set" --preset "fire"\n'
        'invoke "volume ramp get"\n'
        'invoke "volume ramp define" --name "focus" '
        '--points "0,0,0,0,0;100,1,0.5,0,0.8"\n'
        'invoke "volume ramp get"\n'
        'invoke "volume render" --mode "post-classified" '
        '--sampling-step "0.5" --maximum-steps "128" '
        '--lookup-table-samples "64" --texture-budget-bytes "1048576"\n'
        'invoke "volume hide"\n'
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
