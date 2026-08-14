#!/usr/bin/env python3

import importlib
import json
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require(len(sys.argv) == 4, "expected module, CLI and OpenDX fixture")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    fixture = pathlib.Path(sys.argv[3]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    load_args = {
        "path": str(fixture),
        "name": "electrostatic",
        "file-format": "opendx",
        "coordinate-unit": "nanometer",
    }
    python_results = [
        molshredder.invoke("volume load", load_args),
        molshredder.invoke("volume list", {}),
        molshredder.invoke("format list", {"family": "volume"}),
    ]
    require(all(item["status"] == "ok" for item in python_results),
            "Python OpenDX workflow failed")
    loaded = python_results[0]["data"]
    require(
        loaded["format"] == "opendx"
        and loaded["dimensions"] == [2, 2, 3]
        and loaded["value_count"] == 12
        and loaded["precision"] == "float64"
        and loaded["origin"] == [-1.0, 2.0, 0.5]
        and loaded["minimum"] == -2.5
        and loaded["maximum"] == 9.5
        and loaded["coordinate_unit"] == "nanometer",
        "Python OpenDX result lost grid geometry, precision or range",
    )
    require(
        python_results[1]["data"]["volume_count"] == 1
        and python_results[1]["data"]["volumes"][0]["name"]
        == "electrostatic"
        and python_results[2]["data"]["format_count"] == 2
        and [item["id"] for item in python_results[2]["data"]["formats"]]
        == ["opendx", "mrc"],
        "volume list or capability filter lost the OpenDX object",
    )

    script = (
        "format json\n"
        f'invoke "volume load" --coordinate-unit "nanometer" '
        f'--file-format "opendx" --name "electrostatic" '
        f'--path "{fixture}"\n'
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
            "CLI and Python OpenDX result envelopes diverged")
    print("opendx-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
