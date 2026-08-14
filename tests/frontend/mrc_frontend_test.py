#!/usr/bin/env python3

import importlib
import json
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def near(left: float, right: float, tolerance: float = 1.0e-5) -> bool:
    return abs(left - right) <= tolerance


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
        "coordinate-unit": "nanometer",
    }
    python_results = [
        molshredder.invoke("volume load", load_args),
        molshredder.invoke("volume list", {}),
        molshredder.invoke("format list", {"family": "volume"}),
    ]
    require(all(item["status"] == "ok" for item in python_results),
            "Python MRC workflow failed")
    loaded = python_results[0]["data"]
    require(
        loaded["format"] == "mrc"
        and loaded["dimensions"] == [2, 2, 3]
        and loaded["value_count"] == 12
        and loaded["precision"] == "float32"
        and loaded["origin"] == [1.0, 2.0, 3.0]
        and loaded["minimum"] == 0.0
        and loaded["maximum"] == 112.0
        and loaded["coordinate_unit"] == "nanometer"
        and near(loaded["deltas"][0][0], 0.1)
        and near(loaded["deltas"][1][0], 0.1)
        and near(loaded["deltas"][1][1], 0.1732050808)
        and near(loaded["deltas"][2][2], 0.15),
        "Python MRC result lost axis permutation, geometry, precision or range",
    )
    require(
        python_results[1]["data"]["volume_count"] == 1
        and python_results[1]["data"]["volumes"][0]["name"] == "density"
        and python_results[2]["data"]["format_count"] == 2
        and [item["id"] for item in python_results[2]["data"]["formats"]]
        == ["opendx", "mrc"],
        "volume list or capability filter lost the MRC object",
    )

    script = (
        "format json\n"
        f'invoke "volume load" --coordinate-unit "nanometer" '
        f'--file-format "mrc" --name "density" --path "{fixture}"\n'
        'invoke "volume list"\n'
        'invoke "format list" --family "volume"\n'
        "exit\n"
    )
    completed = subprocess.run(
        [str(cli_path), "console"], input=script, text=True,
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
            "CLI and Python MRC result envelopes diverged")
    print("mrc-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
