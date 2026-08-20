#!/usr/bin/env python3

import importlib
import json
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_json(arguments: list[str]) -> dict:
    completed = subprocess.run(arguments, check=True, capture_output=True, text=True)
    return json.loads(completed.stdout)


def main() -> int:
    require(
        len(sys.argv) == 5,
        "expected module, CLI, GUI probe and support manifest paths",
    )
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    gui_probe = pathlib.Path(sys.argv[3]).resolve()
    manifest_path = pathlib.Path(sys.argv[4]).resolve()

    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")
    python_result = molshredder.invoke("system info")
    cli_result = run_json([str(cli_path), "system", "info", "--format", "json"])
    gui_result = run_json([str(gui_probe), "info"])
    require(
        python_result == cli_result == gui_result,
        "CLI, GUI adapter and Python system info results diverged",
    )
    require(
        python_result["command"] == 'invoke "system info"',
        "system info did not preserve its canonical invocation",
    )

    data = python_result["data"]
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    require(data["configuration_schema_version"] == 1, "schema version drifted")
    for name in ("project_version", "platform", "toolchain", "features", "dependencies"):
        require(data[name] == manifest[name], f"runtime/manifest {name} diverged")
    require(data["toolchain"]["cxx_standard"] == 20, "C++ standard drifted")
    require(data["features"]["embedded_python"], "Python support must be visible")
    require(data["features"]["hdf5"] and data["features"]["netcdf"],
            "required scientific I/O dependencies must be visible")
    graphics = data["runtime"]["graphics"]
    require(graphics["status"] == "unavailable",
            "headless frontends must not imply an initialized GPU")
    require(graphics["device_name"] is None and graphics["failure_reason"],
            "headless graphics diagnostics must be explicit and typed")
    print("system-info-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
