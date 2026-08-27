#!/usr/bin/env python3

import importlib
import json
import pathlib
import re
import subprocess
import sys

from console_script import portable_console_script


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def json_lines(text: str) -> list[dict]:
    results = []
    for line in text.splitlines():
        start = line.find('{"schema_version"')
        if start >= 0:
            results.append(json.loads(line[start:]))
    return results


def normalize_created_at(value):
    if isinstance(value, dict):
        result = {}
        for key, child in value.items():
            if key == "created_at_utc":
                require(isinstance(child, str) and
                        re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z", child),
                        "creation timestamp is not canonical UTC")
                result[key] = "<utc-timestamp>"
            else:
                result[key] = normalize_created_at(child)
        return result
    if isinstance(value, list):
        return [normalize_created_at(child) for child in value]
    if isinstance(value, str) and re.fullmatch(
            r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z", value):
        return "<utc-timestamp>"
    return value


def main() -> int:
    require(len(sys.argv) == 5, "expected module, CLI, GUI probe and fixture")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    gui_probe = pathlib.Path(sys.argv[3]).resolve()
    fixture = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    load_args = {"path": str(fixture), "name": "fixture"}
    center_args = {
        "selection": "chain A", "mode": "centroid",
        "precision": "6", "unit": "nanometer",
        "result-name": "chain-center",
    }
    distance_args = {
        "from": "index 1", "to": "index 2", "mode": "atom",
        "pbc": "minimum-image",
        "precision": "6", "unit": "nanometer",
    }
    angle_args = {
        "first": "index 1", "vertex": "index 2", "third": "index 3",
        "pbc": "minimum-image", "precision": "6",
        "result-name": "atom-angle",
    }
    dihedral_args = {
        "first": "index 1", "second": "index 2", "third": "index 3",
        "fourth": "index 4", "pbc": "minimum-image", "precision": "6",
        "result-name": "atom-dihedral",
    }
    sasa_args = {
        "selection": "all", "probe-radius": "1.4", "samples": "256",
        "evaluation-budget": "100000", "unit": "square-angstrom",
        "precision": "6", "result-name": "all-sasa",
    }
    rdf_args = {
        "first": "all", "maximum-radius": "5.0", "bin-width": "1.0",
        "normalization": "count", "pbc": "raw",
        "evaluation-budget": "100", "unit": "angstrom",
        "precision": "6", "result-name": "all-rdf",
    }
    require(molshredder.invoke("load", load_args)["status"] == "ok",
            "Python load failed")
    python_results = [
        molshredder.invoke("analyze center", center_args),
        molshredder.invoke("analyze center", {
            "selection": "all", "mode": "com",
            "precision": "6", "unit": "angstrom",
            "result-name": "all-com",
        }),
        molshredder.invoke("measure distance", distance_args | {
            "result-name": "atom-distance"}),
        molshredder.invoke("measure angle", angle_args),
        molshredder.invoke("measure dihedral", dihedral_args),
        molshredder.invoke("analyze sasa", sasa_args),
        molshredder.invoke("analyze rdf", rdf_args),
        molshredder.invoke("result get", {"id": "2"}),
        molshredder.invoke("result list", {}),
    ]

    script = (
        "format json\n"
        f'invoke "load" --file-format "auto" --name "fixture" --path "{fixture}"\n'
        'invoke "analyze center" --mode "centroid" --precision "6" '
        '--selection "chain A" --unit "nanometer" '
        '--result-name "chain-center"\n'
        'invoke "analyze center" --mode "com" --precision "6" '
        '--selection "all" --unit "angstrom" --result-name "all-com"\n'
        'invoke "measure distance" --from "index 1" --mode "atom" '
        '--pbc "minimum-image" '
        '--precision "6" --to "index 2" --unit "nanometer" '
        '--result-name "atom-distance"\n'
        'invoke "measure angle" --first "index 1" --vertex "index 2" '
        '--third "index 3" --pbc "minimum-image" --precision "6" '
        '--result-name "atom-angle"\n'
        'invoke "measure dihedral" --first "index 1" --second "index 2" '
        '--third "index 3" --fourth "index 4" --pbc "minimum-image" '
        '--precision "6" --result-name "atom-dihedral"\n'
        'invoke "analyze sasa" --selection "all" --probe-radius "1.4" '
        '--samples "256" --evaluation-budget "100000" '
        '--unit "square-angstrom" --precision "6" '
        '--result-name "all-sasa"\n'
        'invoke "analyze rdf" --first "all" --maximum-radius "5.0" '
        '--bin-width "1.0" --normalization "count" --pbc "raw" '
        '--evaluation-budget "100" --unit "angstrom" --precision "6" '
        '--result-name "all-rdf"\n'
        'invoke "result get" --id "2"\n'
        'invoke "result list"\nexit\n'
    )
    cli_run = subprocess.run([str(cli_path), "console"],
                             input=portable_console_script(script), text=True,
                             capture_output=True, check=True)
    cli_results = json_lines(cli_run.stdout)[1:]
    gui_run = subprocess.run([str(gui_probe), str(fixture)], text=True,
                             capture_output=True, check=True)
    gui_results = json_lines(gui_run.stdout)
    require(len(python_results) == len(cli_results) == len(gui_results) == 9,
            "frontends did not emit nine analysis/result lifecycle responses")
    normalized_python = normalize_created_at(python_results)
    normalized_cli = normalize_created_at(cli_results)
    normalized_gui = normalize_created_at(gui_results)
    require(normalized_python == normalized_cli == normalized_gui,
            "CLI, GUI and Python analysis/result envelopes diverged:\n" +
            json.dumps({"python": normalized_python, "cli": normalized_cli,
                        "gui": normalized_gui}, indent=2, sort_keys=True))
    require(python_results[0]["data"]["position"] == [0.15, 0.25, 0.35],
            "centroid/unit conversion result is unexpected")
    require(python_results[0]["data"]["algorithm_version"] ==
            "molshredder-center-v1" and
            python_results[0]["data"]["coordinate_scope"] == "current_frame",
            "immediate analysis response lost scientific contract")
    require(python_results[1]["data"]["mass_estimated"] is True and
            bool(python_results[1]["data"]["mass_source"]),
            "COM mass provenance is missing")
    require(python_results[2]["data"]["distance"] == 0.173205,
            "atom distance/unit conversion result is unexpected")
    require(python_results[2]["data"]["pbc"] == "minimum-image",
            "distance PBC provenance is missing")
    require(python_results[3]["data"]["angle_degrees"] == 125.26439 and
            python_results[3]["data"]["pbc"] == "minimum-image" and
            python_results[3]["data"]["algorithm_version"] ==
            "molshredder-angle-v1",
            "angle value or scientific provenance is unexpected")
    require(python_results[4]["data"]["angle_degrees"] == 135.0 and
            python_results[4]["data"]["pbc"] == "minimum-image" and
            python_results[4]["data"]["algorithm_version"] ==
            "molshredder-dihedral-v1",
            "dihedral value or scientific provenance is unexpected")
    require(python_results[5]["data"]["algorithm_version"] ==
            "molshredder-shrake-rupley-fibonacci-v1" and
            python_results[5]["data"]["samples_per_atom"] == 256 and
            python_results[5]["data"]["unit"] == "square-angstrom" and
            python_results[5]["data"]["selected_atom_count"] == 4,
            "SASA result or sampling provenance is unexpected")
    require(python_results[6]["data"]["algorithm_version"] ==
            "molshredder-rdf-histogram-v1" and
            python_results[6]["data"]["eligible_pair_count"] == 6 and
            python_results[6]["data"]["bin_count"] == 5 and
            python_results[6]["data"]["normalization"] == "count",
            "RDF result or pair/bin provenance is unexpected")
    contract = python_results[7]["data"]
    require(contract["contract_schema_version"] == 1 and
            contract["algorithm_version"] == "molshredder-center-v1" and
            contract["coordinate_scope"] == "current_frame" and
            contract["coordinate_source_revision"] == 1 and
            contract["coordinate_revision"] == 1 and
            contract["coordinate_revision_known"] is True and
            contract["input_coordinate_unit"] == "angstrom" and
            contract["output_unit"] == "angstrom" and
            contract["calculation_precision"] == "float64" and
            contract["presentation_precision"] == 6 and
            contract["absolute_tolerance"] == 1e-10 and
            contract["relative_tolerance"] == 1e-12 and
            contract["tolerance_unit"] == "angstrom" and
            contract["tolerance_known"] is True and
            contract["pbc_policy"] == "not_applicable" and
            contract["pbc_cell_required"] is False and
            contract["missing_data_policy"] == "error" and
            contract["source_status"] == "current" and
            python_results[8]["data"]["result_count"] == 7,
            "persistent result provenance/list state is unexpected")
    print("analysis-frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
