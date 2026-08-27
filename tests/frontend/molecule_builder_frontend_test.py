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
    require(len(sys.argv) == 4, "expected module, CLI and GUI probe")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    gui_probe = pathlib.Path(sys.argv[3]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")
    arguments = {
        "name": "carbonyl",
        "atoms": "C,6,0,0,0,0;O,8,1.2,0,0,0",
        "bonds": "1,2,double",
        "residue-name": "LIG",
        "chain": "A",
        "residue-number": "1",
        "unit": "angstrom",
        "memory-budget-bytes": "1048576",
    }
    python_results = [molshredder.invoke("build molecule", arguments)]
    python_results.append(molshredder.invoke("edit undo"))
    python_results.append(molshredder.invoke("edit redo"))
    python_results.append(molshredder.invoke(
        "edit atom-properties", {
            "atom-id": "1", "name": "C1", "formal-charge": "1",
            "expected-topology-version": "1",
            "expected-coordinate-source-revision": "1",
        }))
    python_results.append(molshredder.invoke(
        "edit residue-properties", {
            "atom-id": "1", "name": "CRB", "chain": "B",
            "residue-number": "7", "expected-topology-version": "2",
            "expected-coordinate-source-revision": "2",
        }))
    python_results.append(molshredder.invoke(
        "edit bond-order", {
            "bond-id": "1", "order": "single",
            "expected-topology-version": "3",
            "expected-coordinate-source-revision": "3",
        }))
    python_results.append(molshredder.invoke("edit undo"))
    python_results.append(molshredder.invoke("edit redo"))
    python_results.append(molshredder.invoke("object chemistry"))
    python_results.append(molshredder.invoke("edit history"))
    require(all(result["status"] == "ok" for result in python_results),
            "Python molecule builder undo/redo workflow failed")

    script = (
        "format json\n"
        'invoke "build molecule" --name "carbonyl" '
        '--atoms "C,6,0,0,0,0;O,8,1.2,0,0,0" '
        '--bonds "1,2,double" --residue-name "LIG" --chain "A" '
        '--residue-number "1" --unit "angstrom" '
        '--memory-budget-bytes "1048576"\n'
        'invoke "edit undo"\n'
        'invoke "edit redo"\n'
        'invoke "edit atom-properties" --atom-id "1" --name "C1" '
        '--formal-charge "1" --expected-topology-version "1" '
        '--expected-coordinate-source-revision "1"\n'
        'invoke "edit residue-properties" --atom-id "1" --name "CRB" '
        '--chain "B" --residue-number "7" --expected-topology-version "2" '
        '--expected-coordinate-source-revision "2"\n'
        'invoke "edit bond-order" --bond-id "1" --order "single" '
        '--expected-topology-version "3" '
        '--expected-coordinate-source-revision "3"\n'
        'invoke "edit undo"\n'
        'invoke "edit redo"\n'
        'invoke "object chemistry"\n'
        'invoke "edit history"\n'
        "exit\n"
    )
    completed = subprocess.run(
        [str(cli_path), "console"], input=portable_console_script(script),
        text=True, capture_output=True, check=True,
    )
    cli_results = [
        json.loads(line[line.find('{"schema_version"'):])
        for line in completed.stdout.splitlines()
        if '{"schema_version"' in line
    ]
    require(len(cli_results) == 10, "CLI did not emit ten editing results")
    require(python_results == cli_results,
            "CLI and Python molecule builder results diverged")

    gui_completed = subprocess.run(
        [str(gui_probe), "molecule-builder"], text=True,
        capture_output=True, check=True,
    )
    gui_results = [json.loads(line) for line in gui_completed.stdout.splitlines()
                   if line.strip()]
    if gui_results != python_results:
        mismatch = next(
            (index for index, pair in enumerate(zip(python_results, gui_results))
             if pair[0] != pair[1]),
            min(len(python_results), len(gui_results)),
        )
        print(f"first frontend mismatch at result {mismatch}", file=sys.stderr)
        if mismatch < min(len(python_results), len(gui_results)):
            print(json.dumps({"python": python_results[mismatch],
                              "gui": gui_results[mismatch]}, indent=2),
                  file=sys.stderr)
    require(gui_results == python_results,
            "GUI, CLI and Python molecule builder results diverged")
    data = python_results[0]["data"]
    require(data["atom_ids"] == [1, 2] and data["bond_ids"] == [1] and
            data["atom_count"] == 2 and data["bond_count"] == 1 and
            data["residue_count"] == 1 and data["topology_version"] == 1,
            "builder stable identity or topology result is incorrect")
    require(data["transaction_id"] == 1 and data["undo_bytes"] > 0 and
            data["diff_schema_version"] == 1 and
            data["transaction_kind"] == "molecule_build" and
            python_results[1]["data"]["transaction_kind"] == "molecule_build" and
            python_results[1]["data"]["coordinate_source_revision"] == 0 and
            python_results[2]["data"]["coordinate_source_revision"] == 1 and
            python_results[3]["data"]["topology_version"] == 2 and
            python_results[4]["data"]["topology_version"] == 3 and
            python_results[5]["data"]["topology_version"] == 4 and
            python_results[6]["data"]["coordinate_source_revision"] == 5 and
            python_results[7]["data"]["coordinate_source_revision"] == 6 and
            python_results[9]["data"]["undo_count"] == 4,
            "builder transaction did not round-trip through bounded undo/redo")
    require(python_results[3]["data"]["name"] == "C1" and
            python_results[3]["data"]["transaction_kind"] == "atom_properties" and
            python_results[3]["data"]["previous_name"] == "C" and
            python_results[3]["data"]["previous_formal_charge"] == 0 and
            python_results[3]["data"]["formal_charge"] == 1 and
            python_results[4]["data"]["transaction_kind"] == "residue_properties" and
            python_results[4]["data"]["previous_name"] == "LIG" and
            python_results[4]["data"]["previous_chain"] == "A" and
            python_results[4]["data"]["name"] == "CRB" and
            python_results[4]["data"]["chain"] == "B" and
            python_results[5]["data"]["transaction_kind"] == "bond_order" and
            python_results[5]["data"]["previous_order"] == "double" and
            python_results[5]["data"]["order"] == "single" and
            python_results[6]["data"]["transaction_kind"] == "bond_order" and
            python_results[7]["data"]["transaction_kind"] == "bond_order" and
            python_results[8]["data"]["formal_charge_present_count"] == 2 and
            python_results[8]["data"]["topology_version"] == 4,
            "atom/residue/bond property edits did not persist after redo")

    malformed = molshredder.invoke(
        "build molecule", {**arguments, "name": "bad", "bonds": "1,3,single"}
    )
    require(malformed["status"] == "error",
            "out-of-range builder bond must fail")
    print("molecule-builder-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
