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
            "expected module, CLI, PRMTOP, MDCRD and build directory")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    prmtop = pathlib.Path(sys.argv[3]).resolve()
    mdcrd = pathlib.Path(sys.argv[4]).resolve()
    output = pathlib.Path(sys.argv[5]).resolve() / "frontend-roundtrip.mdcrd"
    crdbox_output = pathlib.Path(sys.argv[5]).resolve() / "frontend-roundtrip.crdbox"
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    load_args = {"path": str(prmtop), "file-format": "prmtop",
                 "name": "amber"}
    attach_args = {"path": str(mdcrd), "file-format": "mdcrd",
                   "cache-mib": "1", "prefetch-frames": "0"}
    frame_args = {"frame": "1"}
    save_args = {"path": str(output), "file-format": "mdcrd",
                 "title": "frontend CRD", "overwrite": "true"}
    crdbox_save_args = {"path": str(crdbox_output),
                        "file-format": "crdbox",
                        "title": "frontend CRDBOX", "overwrite": "true"}
    crdbox_roundtrip_args = {"path": str(crdbox_output),
                             "file-format": "crdbox", "cache-mib": "1",
                             "prefetch-frames": "0"}
    roundtrip_args = {"path": str(output), "file-format": "mdcrd",
                      "cache-mib": "1", "prefetch-frames": "0"}
    python_results = [
        molshredder.invoke("load", load_args),
        molshredder.invoke("traj load", attach_args),
        molshredder.invoke("traj frame", frame_args),
        molshredder.invoke("traj save", save_args),
        molshredder.invoke("traj save", crdbox_save_args),
        molshredder.invoke("traj load", crdbox_roundtrip_args),
        molshredder.invoke("traj load", roundtrip_args),
    ]
    require(all(item["status"] == "ok" for item in python_results),
            "Python PRMTOP/MDCRD workflow failed")
    require(python_results[1]["data"]["format"] == "mdcrd" and
            python_results[1]["data"]["frame_count"] == 2 and
            python_results[2]["data"]["frame"] == 1 and
            python_results[3]["data"]["format"] == "mdcrd" and
            not python_results[3]["data"]["has_unit_cell"] and
            python_results[3]["data"]["loss_item_count"] > 0 and
            python_results[4]["data"]["format"] == "crdbox" and
            python_results[4]["data"]["has_unit_cell"] and
            python_results[5]["data"]["format"] == "crdbox" and
            python_results[5]["data"]["frame_count"] == 1 and
            python_results[6]["data"]["frame_count"] == 1 and
            output.read_text(encoding="ascii").startswith(
                "frontend CRD\n   1.500") and
            crdbox_output.read_text(encoding="ascii").endswith(
                "  10.500  11.500  12.500\n"),
            "Python MDCRD/CRDBOX result lost read, seek, save or round-trip state")

    script = (
        "format json\n"
        f'invoke "load" --file-format "prmtop" --name "amber" '
        f'--path "{prmtop}"\n'
        f'invoke "traj load" --cache-mib "1" --file-format "mdcrd" '
        f'--path "{mdcrd}" --prefetch-frames "0"\n'
        'invoke "traj frame" --frame "1"\n'
        f'invoke "traj save" --file-format "mdcrd" --overwrite "true" '
        f'--path "{output}" --title "frontend CRD"\n'
        f'invoke "traj save" --file-format "crdbox" --overwrite "true" '
        f'--path "{crdbox_output}" --title "frontend CRDBOX"\n'
        f'invoke "traj load" --cache-mib "1" --file-format "crdbox" '
        f'--path "{crdbox_output}" --prefetch-frames "0"\n'
        f'invoke "traj load" --cache-mib "1" --file-format "mdcrd" '
        f'--path "{output}" --prefetch-frames "0"\n'
        "exit\n"
    )
    completed = subprocess.run([str(cli_path), "console"],
                               input=portable_console_script(script),
                               text=True, capture_output=True, check=True)
    cli_results = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(cli_results == python_results,
            "CLI and Python MDCRD result envelopes diverged")
    print("mdcrd-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
