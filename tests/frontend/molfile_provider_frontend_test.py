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
    require(len(sys.argv) == 6,
            "expected Python module, CLI, PQR plugin, PSF channel plugin and "
            "output directory")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    plugin_path = pathlib.Path(sys.argv[3]).resolve()
    psf_plugin_path = pathlib.Path(sys.argv[4]).resolve()
    output_directory = pathlib.Path(sys.argv[5]).resolve()
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

    psf_input = output_directory / "frontend-molfile.psf"
    psf_input.write_text("synthetic PSF input\n", encoding="utf-8")
    psf_arguments = {
        "file-format": "psf",
        "name": "molfile-frontend-psf",
        "path": str(psf_input),
        "plugin-path": str(psf_plugin_path),
        "provider": "molfile:psf",
    }
    psf_result = molshredder.invoke("load", psf_arguments)
    require(psf_result["status"] == "ok" and
            psf_result["data"]["provider"]["id"] == "molfile:psf" and
            psf_result["data"]["atom_count"] == 3 and
            psf_result["data"]["objects"][0]["frame_count"] == 0,
            "Python did not dispatch the generalized molfile structure channel")
    mismatch = molshredder.invoke("load", dict(psf_arguments, **{
        "file-format": "gro", "name": "molfile-frontend-mismatch"}))
    require(mismatch["status"] == "error" and
            mismatch["error"]["code"] == "invalid_argument",
            "mismatched --file-format must be rejected for molfile:psf")

    script = (
        "format json\n"
        f'invoke "load" --file-format "psf" --name "molfile-frontend-psf" '
        f'--path "{psf_input}" --plugin-path "{psf_plugin_path}" '
        '--provider "molfile:psf"\n'
        "exit\n"
    )
    completed = subprocess.run(
        [str(cli_path), "console"], input=portable_console_script(script),
        text=True, capture_output=True, check=True
    )
    envelopes = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    # The Python session already holds earlier objects, so object IDs differ;
    # every other field of the shared action result must match.
    def without_ids(envelope):
        data = dict(envelope["data"])
        for key in ("active_object_id", "object_id", "object_ids", "objects"):
            data.pop(key, None)
        return {**envelope, "data": data}

    require(len(envelopes) == 1 and
            without_ids(envelopes[0]) == without_ids(psf_result) and
            envelopes[0]["data"]["objects"][0]["atom_count"] == 3 and
            envelopes[0]["data"]["objects"][0]["frame_count"] == 0,
            "CLI and Python molfile:psf load envelopes diverged")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
