#!/usr/bin/env python3

import importlib
import json
import pathlib
import subprocess
import sys
import tempfile


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require(len(sys.argv) == 4, "expected module, CLI and OpenDX fixture")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    fixture = pathlib.Path(sys.argv[3]).resolve()
    temporary = tempfile.TemporaryDirectory()
    output = pathlib.Path(temporary.name) / "roundtrip.dx"
    mrc_output = pathlib.Path(temporary.name) / "roundtrip.mrc"
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
        molshredder.invoke("volume save", {
            "path": str(output),
            "file-format": "opendx",
            "overwrite": "true",
        }),
        molshredder.invoke("volume save", {
            "path": str(mrc_output),
            "file-format": "mrc",
            "overwrite": "true",
        }),
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
        python_results[1]["data"]["format"] == "opendx"
        and python_results[1]["data"]["dimensions"] == [2, 2, 3]
        and python_results[1]["data"]["precision"] == "float64"
        and python_results[1]["data"]["value_count"] == 12
        and python_results[1]["data"]["loss_channel_count"] == 1
        and output.exists()
        and "data follows" in output.read_text()
        and python_results[2]["data"]["format"] == "mrc"
        and python_results[2]["data"]["precision"] == "float32"
        and mrc_output.read_bytes()[208:212] == b"MAP "
        and python_results[3]["data"]["volume_count"] == 1
        and python_results[3]["data"]["volumes"][0]["name"]
        == "electrostatic"
        and python_results[4]["data"]["format_count"] == 2
        and [item["id"] for item in python_results[4]["data"]["formats"]]
        == ["opendx", "mrc"],
        "volume save, MRC export, list or capability filter lost semantics",
    )

    script = (
        "format json\n"
        f'invoke "volume load" --coordinate-unit "nanometer" '
        f'--file-format "opendx" --name "electrostatic" '
        f'--path "{fixture}"\n'
        f'invoke "volume save" --file-format "opendx" --overwrite "true" '
        f'--path "{output}"\n'
        f'invoke "volume save" --file-format "mrc" --overwrite "true" '
        f'--path "{mrc_output}"\n'
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
    temporary.cleanup()
    print("opendx-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
