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
    require(len(sys.argv) == 5,
            "expected Python module, CLI, molfile plugin and output directory")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    plugin_path = pathlib.Path(sys.argv[3]).resolve()
    output_directory = pathlib.Path(sys.argv[4]).resolve()
    input_path = output_directory / "frontend-molfile.pqr"
    input_path.write_text("synthetic PQR input\n", encoding="utf-8")

    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")
    base = {
        "file-format": "pqr",
        "name": "molfile-frontend",
        "path": str(input_path),
        "provider": "molfile:pqr",
    }
    rejected = molshredder.invoke("load", base)
    require(rejected["status"] == "error" and
            rejected["error"]["code"] == "unsupported",
            "Python action accepted an unregistered molfile provider")

    python_arguments = dict(base)
    python_arguments["plugin-path"] = str(plugin_path)
    python_result = molshredder.invoke("load", python_arguments)
    require(python_result["status"] == "ok" and
            python_result["data"]["provider"]["id"] == "molfile:pqr" and
            python_result["data"]["provider"]["origin"] == "dynamic_plugin" and
            python_result["data"]["provider"]["trust"] == "untrusted" and
            python_result["data"]["atom_count"] == 2,
            "Python did not use the explicit molfile provider action")

    script = (
        "format json\n"
        f'invoke "load" --file-format "pqr" --name "molfile-frontend" '
        f'--path "{input_path}" --plugin-path "{plugin_path}" '
        '--provider "molfile:pqr"\n'
        "exit\n"
    )
    completed = subprocess.run(
        [str(cli_path), "console"], input=portable_console_script(script),
        text=True,
        capture_output=True, check=True
    )
    envelopes = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(len(envelopes) == 1 and envelopes[0] == python_result,
            "CLI and Python molfile load envelopes diverged")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
