#!/usr/bin/env python3

import importlib
import pathlib
import sys
import tempfile


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def invoke_ok(molshredder, command: str, arguments: dict[str, str] | None = None):
    result = molshredder.invoke(command, arguments or {})
    require(result["status"] == "ok", f"{command} failed: {result}")
    return result


def main() -> int:
    require(len(sys.argv) == 5,
            "expected module, structure, trajectory and volume fixture paths")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    structure = pathlib.Path(sys.argv[2]).resolve()
    trajectory = pathlib.Path(sys.argv[3]).resolve()
    volume = pathlib.Path(sys.argv[4]).resolve()
    require(all(path.is_file() for path in (module_path, structure,
                                             trajectory, volume)),
            "M2 gate fixture is missing")
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")
    molshredder.reset_runtime()

    invoke_ok(molshredder, "load", {
        "path": str(structure), "name": "m2-structure",
        "file-format": "pdb",
    })
    invoke_ok(molshredder, "select", {
        "expression": "index * 2 >= 2", "name": "m2-dynamic",
        "update": "true",
    })
    shown = invoke_ok(molshredder, "show", {
        "representation": "spheres", "selection": "@m2-dynamic",
    })
    require(shown["data"]["affected_atom_count"] == 3,
            "dynamic selection did not cover the representative structure")
    surface = invoke_ok(molshredder, "surface show", {
        "kind": "vdw", "selection": "all", "probe-radius": "0",
        "grid-spacing": "1", "color": "cyan", "opacity": "0.7",
        "voxel-budget": "100000", "memory-budget-bytes": "8388608",
    })
    require(surface["data"]["triangle_count"] > 0,
            "representative molecular surface is empty")
    invoke_ok(molshredder, "analyze center", {
        "selection": "all", "mode": "centroid", "precision": "6",
        "unit": "angstrom", "result-name": "m2-center",
    })
    edit = invoke_ok(molshredder, "edit atom-position", {
        "atom-id": "1", "x": "0.25", "y": "0.5", "z": "0.75",
        "unit": "angstrom", "expected-topology-version": "1",
        "expected-coordinate-source-revision": "1",
    })
    require(edit["data"]["transaction_kind"] == "atom_position",
            "representative coordinate edit lost its typed transaction")
    invoke_ok(molshredder, "edit undo")
    invoke_ok(molshredder, "edit redo")

    attached = invoke_ok(molshredder, "traj load", {
        "path": str(trajectory), "file-format": "dcd", "cache-mib": "1",
        "mapping": "index", "coordinate-unit": "angstrom",
    })
    require(attached["data"]["frame_count"] == 4,
            "representative trajectory frame count drifted")
    invoke_ok(molshredder, "traj frame", {"frame": "2"})
    rmsd = invoke_ok(molshredder, "analyze trajectory rmsd", {
        "selection": "all", "reference": "0", "first": "0",
        "last": "3", "stride": "1", "fit": "none",
        "weight": "uniform", "missing": "error", "precision": "6",
        "unit": "angstrom", "result-name": "m2-rmsd",
    })
    require(len(rmsd["data"]["table"]["rows"]) == 4,
            "representative RMSD time series is incomplete")

    invoke_ok(molshredder, "volume load", {
        "path": str(volume), "name": "m2-density", "file-format": "mrc",
        "coordinate-unit": "angstrom",
    })
    volume_slice = invoke_ok(molshredder, "volume slice", {
        "axis": "z", "index": "1", "minimum-color": "blue",
        "maximum-color": "orange", "opacity": "0.7",
        "memory-budget-bytes": "1048576", "replace": "true",
    })
    require(volume_slice["data"]["triangle_count"] == 2,
            "representative volume slice is incomplete")
    invoke_ok(molshredder, "volume render", {
        "mode": "post-classified", "sampling-step": "0.5",
        "maximum-steps": "128", "lookup-table-samples": "64",
        "texture-budget-bytes": "1048576",
    })

    invoke_ok(molshredder, "scene store", {"name": "m2-baseline"})
    invoke_ok(molshredder, "movie configure", {
        "frames": "3", "fps": "24", "loop": "true",
    })
    invoke_ok(molshredder, "movie keyframe", {
        "frame": "2", "scene": "m2-baseline", "trajectory-frame": "2",
    })
    invoke_ok(molshredder, "movie seek", {"frame": "2"})

    with tempfile.TemporaryDirectory(prefix="molshredder-m2-") as root:
        temporary = pathlib.Path(root)
        script_path = temporary / "automation.py"
        script_path.write_text("print('m2-automation-ok')\n", encoding="utf-8")
        automated = molshredder.run_script(str(script_path), [], trusted=True)
        require(automated["status"] == "ok" and
                automated["data"]["stdout"] == "m2-automation-ok\n",
                "trusted local automation workflow failed")

        session_path = temporary / "workstation.msess"
        saved = invoke_ok(molshredder, "session save", {
            "path": str(session_path),
            "ui-visible-panels": "analysis,views",
        })
        require(saved["data"]["schema_version"] == 2 and
                session_path.is_file(), "M2 session was not published")
        invoke_ok(molshredder, "object visibility", {
            "id": "1", "visible": "false",
        })
        loaded = invoke_ok(molshredder, "session load", {
            "path": str(session_path),
        })
        require(loaded["data"]["extensions"]["ui.visible-panels"] ==
                "analysis,views", "stable UI state did not round-trip")

        objects = invoke_ok(molshredder, "object list")["data"]
        object_row = next(row for row in objects["table"]["rows"]
                          if row[0] == 1)
        require(object_row[3] is True,
                "session restore did not recover molecular visibility")
        results = invoke_ok(molshredder, "result list")["data"]
        require(results["result_count"] >= 2,
                "session restore lost scientific results")
        scenes = invoke_ok(molshredder, "scene list")["data"]
        movie = invoke_ok(molshredder, "movie status")["data"]["movie"]
        volumes = invoke_ok(molshredder, "volume list")["data"]
        history = invoke_ok(molshredder, "edit history")["data"]
        require(scenes["count"] == 1 and movie["frame_count"] == 3 and
                movie["security"] == "typed-scene-trajectory-keys-only" and
                volumes["volume_count"] == 1 and history["undo_count"] >= 1,
                "full-session workstation state is incomplete")

    print("m2-scientific-workstation-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
