#!/usr/bin/env python3
"""Frontend assertions that close the SEQ-315 P0 evidence gaps.

Each section names the capability rows it provides evidence for. All checks run
through the shared typed action path (Python invoke) on checked-in fixtures.
"""

import importlib
import pathlib
import sys
import tempfile


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def ok(result: dict, what: str) -> dict:
    require(result["status"] == "ok", f"{what} failed: {result.get('error')}")
    return result["data"]


def main() -> int:
    require(len(sys.argv) == 3, "expected Python module and fixture directory")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    fixtures = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")

    def load(name: str, fixture: str, **extra):
        return ok(m.invoke("load", {"path": str(fixtures / fixture), "name": name, **extra}),
                  f"load {fixture}")

    # io.load.auto_detect: the format is chosen from the content/extension alone.
    detected = {
        "1ubq.pdb": "pdb", "chemistry.mol2": "mol2", "chemistry.sdf": "sdf",
        "electrostatics.pqr": "pqr", "synthetic_multimodel.cif": "mmcif",
        "trajectory.gro": "gro", "trajectory.g96": "g96", "multiframe.xyz": "xyz",
    }
    for index, (fixture, expected) in enumerate(detected.items()):
        data = load(f"auto{index}", fixture)
        require(data["format"] == expected, f"{fixture}: detected {data['format']}")

    # object.query.atom_count / state_count (object granularity).
    ubq = str(load("ubq", "1ubq.pdb")["object_id"])
    mf = str(load("mf", "multiframe.xyz")["object_id"])
    chem = str(load("chem", "chemistry.sdf")["object_id"])   # has bonds for bond-scope checks
    rows = {str(row[0]): row for row in ok(m.invoke("object list"), "object list")["table"]["rows"]}
    ok(m.invoke("object activate", {"id": chem}), "activate chem")
    require(rows[ubq][5] == 660 and rows[ubq][6] == 1, "ubq atom/frame count")
    require(rows[mf][5] == 3 and rows[mf][6] == 2 and rows[mf][8] is True,
            "multi-frame object count and trajectory flag")

    # setting.service.*: one numeric setting per maximum scope, set/get/unset/list/reset.
    listing = ok(m.invoke("setting list"), "setting list")["table"]["rows"]
    require(len(listing) >= 80, "setting catalog size")
    by_scope = {}
    for sid, name, kind, default, minimum, maximum, unit, scope in listing:
        if kind == "number" and (minimum is None or minimum <= 0.5) \
                and (maximum is None or maximum >= 0.5) and default != 0.5:
            by_scope.setdefault(scope, name)
    # A setting may be overridden at any scope up to its maximum scope.
    plan = (("global", "global"), ("object", "state"), ("state", "state"),
            ("atom", "atom"), ("bond", "bond"))
    require({maximum for _, maximum in plan} <= set(by_scope),
            f"catalog lacks numeric settings for scopes: {sorted(by_scope)}")
    for scope, maximum_scope in plan:
        name = by_scope[maximum_scope]
        target = {"target": "1"} if scope in ("atom", "bond") else {}
        baseline = ok(m.invoke("setting get", {"name": name, "scope": scope, **target}),
                      f"get {name}")
        require(baseline["source_scope"] in (None, "global"), "default has no override")
        changed = ok(m.invoke("setting set", {"name": name, "value": "0.5", "scope": scope,
                                              **target}), f"set {name}")
        require(changed["value"] == 0.5 and changed["source_scope"] == scope,
                f"{name} set at {scope}: {changed}")
        again = ok(m.invoke("setting get", {"name": name, "scope": scope, **target}), "get")
        require(again["value"] == 0.5 and again["override_count"] >= 1, "get after set")
        removed = ok(m.invoke("setting unset", {"name": name, "scope": scope, **target}),
                     f"unset {name}")
        restored = ok(m.invoke("setting get", {"name": name, "scope": scope, **target}), "get")
        require(restored["value"] == baseline["value"], f"{name}: unset restores the default")
        require(removed["removed"] in (True, False), "unset reports removal")
    ok(m.invoke("setting set", {"name": by_scope["global"], "value": "0.5"}), "set global")
    require(ok(m.invoke("setting list"), "list")["override_count"] >= 1, "override counted")
    ok(m.invoke("setting reset", {"scope": "global"}), "reset global")
    require(ok(m.invoke("setting list"), "list")["override_count"] == 0, "reset clears overrides")
    bad = m.invoke("setting set", {"name": "line_smooth", "value": "0.5"})
    require(bad["status"] == "error" and bad["error"]["code"] == "invalid_argument",
            "a value of the wrong type must be rejected")

    # scene.snapshot.* and scene.query.list: lifecycle and the restored channels.
    ok(m.invoke("scene clear"), "scene clear")
    ok(m.invoke("object activate", {"id": mf}), "activate mf")
    ok(m.invoke("show", {"representation": "sticks"}), "show sticks")
    ok(m.invoke("traj frame", {"frame": "1"}), "frame 1")
    base_view = ok(m.invoke("view get"), "view get")["camera"]
    ok(m.invoke("scene store", {"name": "a"}), "store a")
    ok(m.invoke("scene store", {"name": "b"}), "store b")
    names = [s["name"] for s in ok(m.invoke("scene list"), "scene list")["scenes"]]
    require(names == ["a", "b"], f"scene list order {names}")
    replaced = ok(m.invoke("scene store", {"name": "a"}), "replace a")
    require(replaced["replaced"] is True and replaced["count"] == 2, "store replaces by name")

    ok(m.invoke("hide", {"representation": "sticks"}), "hide")                 # representation
    ok(m.invoke("object visibility", {"id": mf, "visible": "false"}), "hide object")  # activity
    ok(m.invoke("traj frame", {"frame": "0"}), "frame 0")                        # frame
    ok(m.invoke("view zoom", {"buffer": "5"}), "zoom")                           # view
    moved = ok(m.invoke("view get"), "view get")["camera"]
    require(moved["distance"] != base_view["distance"], "the view changed before recall")
    ok(m.invoke("scene recall", {"name": "a"}), "recall a")
    rows = {str(row[0]): row for row in ok(m.invoke("object list"), "object list")["table"]["rows"]}
    require(rows[mf][3] is True, "activity channel restores object visibility")
    require(rows[mf][7] == 1, "representation channel restores representations")
    require(ok(m.invoke("view get"), "view get")["camera"]["distance"] == base_view["distance"],
            "view channel restores the camera")
    require(ok(m.invoke("traj frame", {"frame": "1"}), "frame")["frame"] == 1, "frame channel")
    ok(m.invoke("scene delete", {"name": "a"}), "delete a")
    require([s["name"] for s in ok(m.invoke("scene list"), "list")["scenes"]] == ["b"],
            "delete removes exactly one scene")
    missing = m.invoke("scene recall", {"name": "a"})
    require(missing["status"] == "error", "recalling a deleted scene fails")
    cleared = ok(m.invoke("scene clear"), "clear")
    require(cleared["cleared_all"] is True and cleared["count"] == 0, "clear removes all")

    # camera.stereo.walleye, camera.reset.object_transform.
    ok(m.invoke("stereo set", {"mode": "walleye"}), "stereo walleye")
    require(ok(m.invoke("stereo get"), "stereo get")["stereo"]["mode"] == "walleye",
            "wall-eye mode is stored")
    ok(m.invoke("stereo set", {"mode": "side_by_side"}), "stereo restore")
    reset = ok(m.invoke("view reset"), "view reset")
    require(reset["animation"]["committed_endpoint"] is True, "reset commits its endpoint")
    # camera.reset.object_transform: an object pivot is restored by `view reset --object`.
    pivot = ok(m.invoke("view origin", {"object": ubq, "position": "5,6,7"}), "object pivot")
    require(pivot["position"] == [5.0, 6.0, 7.0] and pivot["target"] == "object", "pivot set")
    restored = ok(m.invoke("view reset", {"object": ubq}), "object reset")
    require(restored["object_ids"] == [int(ubq)] and restored["target"] == "object" and
            restored["scene_version"] > pivot["scene_version"],
            f"object reset applies to exactly that object and bumps the scene: {restored}")
    every = ok(m.invoke("view reset", {"object": "all"}), "reset all")
    require(every["object_count"] >= 3 and int(ubq) in every["object_ids"], "reset all objects")

    # representation.visibility.mask.multiple, representation.cache.rebuild.
    ok(m.invoke("object activate", {"id": ubq}), "activate ubq")
    many = ok(m.invoke("show", {"representation": "everything"}), "show everything")
    require(many["representation_count"] == 6 and
            many["resolved_representations"].count(",") == 5,
            "a composite mask resolves to six representations")
    expected = sum(row[7] for row in ok(m.invoke("object list"), "list")["table"]["rows"])
    rebuilt = ok(m.invoke("recolor"), "recolor")
    require(rebuilt["representation_count"] == expected and expected >= 6,
            f"recolor rebuilds every visible representation: {rebuilt} vs {expected}")
    ok(m.invoke("hide", {"representation": "everything"}), "hide everything")

    # representation.volume.object_overwrite: rendering again reuses the volume object.
    volume = ok(m.invoke("volume load", {"path": str(fixtures / "potential.dx")}), "volume load")
    first = ok(m.invoke("volume render"), "volume render")
    second = ok(m.invoke("volume render"), "volume render again")
    require(first["object_id"] == second["object_id"] == volume["object_id"],
            "direct volume rendering overwrites the same object")

    # trajectory.rms.current_states: per-state RMSD without fit over the attached frames.
    ok(m.invoke("object activate", {"id": mf}), "activate mf")
    rmsd = ok(m.invoke("analyze trajectory rmsd", {"selection": "all", "fit": "none"}), "rmsd")
    require(rmsd["algorithm"].startswith("weighted RMSD without fit"), "no-fit algorithm")
    columns = rmsd["table"]["columns"]
    values = [row[columns.index("rmsd")] for row in rmsd["table"]["rows"]]
    require(rmsd["row_count"] == len(values) == 2, "one RMSD row per attached state")
    require(values[0] == 0.0 and values[1] > 0.0, f"reference state is 0, the other moved: {values}")

    # object.query.atom_count / state_count: selection counts without a state change.
    ok(m.invoke("object activate", {"id": ubq}), "activate ubq")
    counted = ok(m.invoke("object count", {"selection": "name CA"}), "count CA")
    require(counted["atom_count"] == 76 and counted["object_atom_count"] == 660 and
            counted["state_count"] == 1, f"CA count {counted}")
    require(ok(m.invoke("object count", {"object": mf}), "count mf")["state_count"] == 2,
            "state count of a two-state object")
    require(m.invoke("object count", {"selection": "bogus ("})["status"] == "error",
            "an invalid selection must be an error")

    # object.create.from_selection, object.copy.exact, object.extract.selection,
    # object.edit.remove_atoms.
    made = ok(m.invoke("object create", {"name": "cas", "selection": "name CA",
                                         "source": ubq}), "create")
    require(made["atom_count"] == 76 and made["removed_from_source_atom_count"] == 0,
            f"create keeps the source intact: {made}")
    before = ok(m.invoke("object count", {"object": ubq}), "count source")["atom_count"]
    require(before == 660, "create must not change the source")
    duplicate = m.invoke("object create", {"name": "cas", "selection": "name CA",
                                           "source": ubq})
    require(duplicate["status"] == "error", "object names stay unique")
    empty = m.invoke("object create", {"name": "empty", "selection": "resn XYZ", "source": ubq})
    require(empty["status"] == "error", "an empty selection creates nothing")

    copy_of_chem = ok(m.invoke("object copy", {"object": chem, "name": "chem-copy"}), "copy")
    chem_info = ok(m.invoke("object count", {"object": chem}), "count chem")
    require(copy_of_chem["atom_count"] == chem_info["atom_count"] and
            copy_of_chem["bond_count"] > 0, f"copy keeps atoms and bonds: {copy_of_chem}")
    mf_copy = ok(m.invoke("object copy", {"object": mf, "name": "mf-copy"}), "copy mf")
    require(mf_copy["state_count"] == 2, "copy keeps every state")
    for frame in ("0", "1"):
        centers = []
        for target in (mf, str(mf_copy["object_id"])):
            ok(m.invoke("object activate", {"id": target}), "activate")
            ok(m.invoke("traj frame", {"frame": frame}), "frame")
            centers.append(ok(m.invoke("analyze center"), "center"))
        keys = [k for k in centers[0] if k in ("center", "position", "x", "y", "z")]
        require(all(centers[0][k] == centers[1][k] for k in keys) and keys,
                f"copied coordinates match in state {frame}: {centers}")

    extracted = ok(m.invoke("object extract", {"name": "waters", "selection": "resn HOH",
                                               "source": ubq}), "extract")
    require(extracted["atom_count"] == 58 and extracted["removed_from_source_atom_count"] == 58,
            f"extract moves the atoms: {extracted}")
    require(ok(m.invoke("object count", {"object": ubq}), "count")["atom_count"] == 602,
            "extract removes the atoms from the source")
    require(ok(m.invoke("object count", {"object": ubq, "selection": "resn HOH"}),
               "count")["atom_count"] == 0, "no water remains in the source")

    ok(m.invoke("object activate", {"id": ubq}), "activate ubq")
    removed = ok(m.invoke("object remove-atoms", {"selection": "name CA"}), "remove")
    require(removed["removed_atom_count"] == 76 and removed["atom_count"] == 526,
            f"remove-atoms: {removed}")
    everything = m.invoke("object remove-atoms", {"selection": "all"})
    require(everything["status"] == "error", "removing every atom is refused")
    require(ok(m.invoke("object count", {"object": ubq}), "count")["atom_count"] == 526,
            "a refused removal changes nothing")

    # analysis.measure.rgyr / inertia: pure analysis results on the active object.
    ok(m.invoke("object activate", {"id": chem}), "activate chem")
    distance = ok(m.invoke("measure distance", {"from": "index 1", "to": "index 2",
                                                "precision": "9"}), "distance")
    pair = ok(m.invoke("analyze gyration", {"selection": "index 1+2", "mode": "centroid",
                                            "precision": "9"}), "gyration")
    value = distance.get("distance", distance.get("value"))
    require(abs(pair["radius_of_gyration"] - value / 2.0) < 1.0e-6,
            f"two-atom radius of gyration is half the distance: {pair} vs {distance}")
    require(pair["used_atom_count"] == 2 and pair["unit"] == "angstrom", "gyration bookkeeping")
    nano = ok(m.invoke("analyze gyration", {"selection": "index 1+2", "mode": "centroid",
                                            "unit": "nanometer", "precision": "9"}), "nm")
    require(abs(nano["radius_of_gyration"] * 10.0 - pair["radius_of_gyration"]) < 1.0e-6,
            "gyration honors the output unit")
    weighted = ok(m.invoke("analyze inertia", {"selection": "all"}), "inertia")
    moments = weighted["principal_moments"]
    require(moments == sorted(moments) and all(v >= 0.0 for v in moments),
            f"principal moments ascend: {moments}")
    require(weighted["mass_estimated"] in (True, False) and weighted["total_mass"] > 0.0 and
            weighted["moment_unit"].endswith("angstrom^2"), "mass provenance reported")
    axes = weighted["principal_axes"]
    dot = lambda a, b: sum(x * y for x, y in zip(a, b))
    require(all(abs(dot(axes[i], axes[i]) - 1.0) < 1.0e-5 for i in range(3)) and
            all(abs(dot(axes[i], axes[j])) < 1.0e-5 for i in range(3) for j in range(i)),
            "principal axes are orthonormal")
    cross = [axes[0][1] * axes[1][2] - axes[0][2] * axes[1][1],
             axes[0][2] * axes[1][0] - axes[0][0] * axes[1][2],
             axes[0][0] * axes[1][1] - axes[0][1] * axes[1][0]]
    require(all(abs(cross[i] - axes[2][i]) < 1.0e-5 for i in range(3)), "axes are right-handed")
    require(m.invoke("analyze gyration", {"selection": "resn XYZ"})["status"] == "error",
            "an empty selection is an error")

    # io.save.auto_detect: the extension selects the writer and the file reads back.
    fresh = str(load("fresh", "1ubq.pdb")["object_id"])
    ok(m.invoke("object activate", {"id": fresh}), "activate fresh")
    with tempfile.TemporaryDirectory() as directory:
        for extension, expected, writable in (
                ("pdb", "pdb", True), ("xyz", "xyz", True), ("gro", "gro", True),
                ("pqr", "pqr", False), ("cif", "mmcif", False),
                ("mol2", "mol2", False), ("sdf", "sdf", True)):
            path = pathlib.Path(directory) / f"out.{extension}"
            result = m.invoke("save", {"path": str(path)})
            if not writable:
                # This PDB source lacks SYBYL types / bond orders / unique mmCIF labels, so
                # the extension-selected writer must refuse with its own typed error and
                # leave no file behind.
                require(result["status"] == "error" and
                        result["error"]["code"] in ("unsupported", "invalid_argument") and
                        extension.upper() in result["error"]["message"].upper() and
                        not path.exists(), f"{extension}: typed refusal expected: {result}")
                continue
            saved = ok(result, f"save {extension}")
            require(path.exists() and path.stat().st_size > 0, f"{extension} file written")
            require(saved.get("format") == expected, f"{extension}: {saved.get('format')}")
            back = ok(m.invoke("load", {"path": str(path), "name": f"back-{extension}"}),
                      f"reload {extension}")
            require(back["atom_count"] == 660, f"{extension} round-trips 660 atoms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
