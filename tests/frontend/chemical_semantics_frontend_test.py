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
    require(len(sys.argv) == 5, "expected module, CLI, GUI probe and SDF fixture")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    gui_probe = pathlib.Path(sys.argv[3]).resolve()
    fixture = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    loaded = molshredder.invoke("load", {"file-format": "sdf", "path": str(fixture)})
    require(loaded["status"] == "ok", "Python chemistry fixture load failed")
    activated = molshredder.invoke("object activate", {"id": "1"})
    require(activated["status"] == "ok", "Python chemistry activation failed")
    python_result = molshredder.invoke("object chemistry")
    python_perception = molshredder.invoke("object perceive-chemistry")
    python_apply = molshredder.invoke(
        "object perceive-chemistry", {"apply": "true"}
    )
    python_after_apply = molshredder.invoke("object chemistry")

    script = (
        "format json\n"
        f'invoke "load" --file-format "sdf" --path "{fixture}"\n'
        'invoke "object activate" --id "1"\n'
        'invoke "object chemistry"\n'
        'invoke "object perceive-chemistry"\n'
        'invoke "object perceive-chemistry" --apply "true"\n'
        'invoke "object chemistry"\n'
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
    require(len(envelopes) == 6, "CLI did not emit chemistry workflow results")
    cli_result = envelopes[-4]
    cli_perception = envelopes[-3]
    cli_apply = envelopes[-2]
    cli_after_apply = envelopes[-1]

    gui_completed = subprocess.run(
        [str(gui_probe), "object-chemistry", str(fixture)],
        text=True, capture_output=True, check=True
    )
    gui_result = json.loads(gui_completed.stdout)
    require(python_result == cli_result == gui_result,
            "CLI, GUI and Python chemical semantics results diverged")
    gui_perception_completed = subprocess.run(
        [str(gui_probe), "object-perception", str(fixture)],
        text=True, capture_output=True, check=True
    )
    gui_perception = json.loads(gui_perception_completed.stdout)
    require(python_perception == cli_perception == gui_perception,
            "CLI, GUI and Python chemical perception results diverged")
    gui_apply_completed = subprocess.run(
        [str(gui_probe), "object-perception-apply", str(fixture)],
        text=True, capture_output=True, check=True
    )
    gui_apply = json.loads(gui_apply_completed.stdout)
    require(python_apply == cli_apply == gui_apply,
            "CLI, GUI and Python chemical perception apply diverged")
    gui_after_completed = subprocess.run(
        [str(gui_probe), "object-chemistry-after-perception", str(fixture)],
        text=True, capture_output=True, check=True
    )
    gui_after_apply = json.loads(gui_after_completed.stdout)
    require(python_after_apply == cli_after_apply == gui_after_apply,
            "CLI, GUI and Python post-perception chemistry diverged")

    data = python_result["data"]
    require(data["chemical_semantics_schema_version"] == 1,
            "chemical semantics schema version drifted")
    require(data["object_id"] == 1 and data["atom_count"] == 4 and
            data["bond_count"] == 3,
            "active chemistry object identity/counts are incorrect")
    require(data["formal_charge_present_count"] == 2 and
            data["isotope_atom_count"] == 1 and
            data["radical_atom_count"] == 1,
            "normalized atom chemistry counts are incorrect")
    rows = {row[0]: row[1] for row in data["table"]["rows"]}
    require(rows["bond_single"] == 1 and rows["bond_double"] == 1 and
            rows["bond_aromatic"] == 1 and
            rows["explicit_bond_annotation"] == 3,
            "normalized bond chemistry counts are incorrect")
    perception = python_perception["data"]
    require(perception["chemical_perception_schema_version"] == 1 and
            perception["rule_set"] == "molshredder-conservative-chemistry" and
            perception["rule_version"] == 1,
            "chemical perception provenance drifted")
    require(perception["proposed_bond_count"] == 1 and
            perception["proposed_residue_count"] == 1 and
            perception["evaluated_pair_count"] == 3,
            "chemical perception proposal counts are incorrect")
    applied = python_apply["data"]
    require(applied["applied"] is True and
            applied["result_topology_version"] == 2 and
            applied["result_bond_count"] == 4,
            "chemical perception apply transaction is incorrect")
    after_rows = {row[0]: row[1]
                  for row in python_after_apply["data"]["table"]["rows"]}
    require(python_after_apply["data"]["bond_count"] == 4 and
            after_rows["inferred_bond_annotation"] == 1 and
            after_rows["residue_ligand"] == 1 and
            after_rows["inferred_residue_annotation"] == 1,
            "applied inferred bond is missing from canonical topology")
    print("chemical-semantics-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
