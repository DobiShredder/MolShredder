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

    def visibility_of(object_id: str):
        for row in ok(m.invoke("object list"), "object list")["table"]["rows"]:
            if str(row[0]) == object_id:
                return row[3]
        return None

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
    # Files without polymer bonds (PDB, mmCIF, XYZ, ...) are connected by covalent distance on load,
    # so lines/sticks have something to draw; a file with its own bonds (SDF) is left as written.
    ubq_bonds = ok(m.invoke("object copy", {"object": ubq, "name": "ubq-bonds"}), "copy ubq")["bond_count"]
    require(500 < ubq_bonds < 800, f"1ubq must load with distance-inferred bonds: {ubq_bonds}")
    # A loaded file shows itself by residue class: ubiquitin = cartoon (protein) + lines (the 58 waters), a
    # small molecule = lines, and auto_color gives every atom a load color.
    require(rows[ubq][7] == 2, f"1ubq default look is cartoon + lines: {rows[ubq]}")
    require(rows[chem][7] == 1, f"a small molecule shows lines only: {rows[chem]}")
    mf_bonds = ok(m.invoke("object copy", {"object": mf, "name": "mf-bonds"}), "copy mf")["bond_count"]
    require(mf_bonds <= 3, f"three xyz atoms cannot have more than three bonds: {mf_bonds}")
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
    ok(m.invoke("as", {"representation": "sticks"}), "show only sticks")   # one representation, whatever the load showed
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

    # scene.navigate.*, scene.order.*, scene.snapshot.{rename,update,insert_*} and the
    # color channel of a scene.
    def scene_names():
        return [item["name"] for item in ok(m.invoke("scene list"), "list")["scenes"]]

    def current_scene():
        return [item["name"] for item in ok(m.invoke("scene list"), "list")["scenes"]
                if item["current"]]

    for name in ("s10", "s2", "s1"):
        ok(m.invoke("scene store", {"name": name}), f"store {name}")
    require(scene_names() == ["s10", "s2", "s1"], "scenes keep their storage order")
    ok(m.invoke("scene recall", {"name": "s2"}), "recall s2")
    require(ok(m.invoke("scene next"), "next")["scene"]["name"] == "s1" and
            ok(m.invoke("scene next"), "wrap")["scene"]["name"] == "s10" and
            ok(m.invoke("scene previous"), "previous")["scene"]["name"] == "s1" and
            ok(m.invoke("scene first"), "first")["scene"]["name"] == "s10" and
            ok(m.invoke("scene last"), "last")["scene"]["name"] == "s1",
            "next, previous, first and last follow the scene order and wrap")
    require(m.invoke("scene next", {"wrap": "false"})["status"] == "error" and
            current_scene() == ["s1"], "next without wrap stops at the end")
    ok(m.invoke("scene first"), "first")
    require(m.invoke("scene previous", {"wrap": "false"})["status"] == "error",
            "previous without wrap stops at the start")
    ok(m.invoke("scene order", {"sort": "true"}), "natural sort")
    require(scene_names() == ["s1", "s2", "s10"], "natural sort orders s2 before s10")
    ok(m.invoke("scene order", {"names": "s10", "location": "top"}), "move top")
    require(scene_names() == ["s10", "s1", "s2"], "move to top")
    ok(m.invoke("scene order", {"names": "s10 s1", "location": "bottom"}), "move bottom")
    require(scene_names() == ["s2", "s10", "s1"], "move to bottom keeps the given order")
    ok(m.invoke("scene recall", {"name": "s2"}), "recall s2")
    ok(m.invoke("scene order", {"names": "s1", "location": "current"}), "move to current")
    require(scene_names() == ["s1", "s2", "s10"], "current places scenes before the current scene")
    for bad in ({"names": "nope"}, {"names": "s1", "location": "middle"}, {}):
        require(m.invoke("scene order", bad)["status"] == "error", f"scene order rejected: {bad}")
    require(scene_names() == ["s1", "s2", "s10"], "a rejected order changes nothing")
    ok(m.invoke("scene recall", {"name": "s1"}), "recall s1")
    ok(m.invoke("scene store", {"name": "after-s1", "insert": "after"}), "insert after")
    require(scene_names() == ["s1", "after-s1", "s2", "s10"], "insert after the current scene")
    ok(m.invoke("scene recall", {"name": "s2"}), "recall s2")
    ok(m.invoke("scene store", {"name": "before-s2", "insert": "before"}), "insert before")
    require(scene_names() == ["s1", "after-s1", "before-s2", "s2", "s10"],
            "insert before the current scene")

    ok(m.invoke("scene recall", {"name": "s2"}), "recall s2")
    ok(m.invoke("movie configure", {"frames": "3", "fps": "10"}), "movie")
    ok(m.invoke("movie keyframe", {"frame": "2", "scene": "s2"}), "keyframe on s2")
    renamed = ok(m.invoke("scene rename", {"name": "s2", "new-name": "scene-two"}), "rename")
    require(renamed["scene"]["name"] == "scene-two" and renamed["scene"]["current"] is True and
            scene_names() == ["s1", "after-s1", "before-s2", "scene-two", "s10"],
            "rename keeps the position and the current marker")
    keyframes = ok(m.invoke("movie status"), "status")["movie"]["keyframes"]
    require(keyframes[0]["scene"] == "scene-two", "movie keyframes follow a renamed scene")
    for bad in ({"name": "scene-two", "new-name": "s1"}, {"name": "nope", "new-name": "x"},
                {"name": "s1", "new-name": ""}):
        require(m.invoke("scene rename", bad)["status"] == "error", f"rename rejected: {bad}")
    require("s1" in scene_names() and "scene-two" in scene_names(), "a rejected rename changes nothing")
    ok(m.invoke("movie clear"), "movie clear")

    ok(m.invoke("scene recall", {"name": "s1"}), "recall s1")
    ok(m.invoke("object visibility", {"id": mf, "visible": "false"}), "hide")
    updated = ok(m.invoke("scene update"), "update")
    require(updated["replaced"] is True and updated["scene"]["name"] == "s1" and
            scene_names()[0] == "s1", "update re-stores the current scene in place")
    ok(m.invoke("object visibility", {"id": mf, "visible": "true"}), "show")
    ok(m.invoke("scene recall", {"name": "s1"}), "recall updated scene")
    require(visibility_of(mf) is False, "the updated scene remembers the new state")
    ok(m.invoke("scene delete", {"name": "s1"}), "delete current")
    require(m.invoke("scene update")["status"] == "error", "update needs a current scene")
    ok(m.invoke("object visibility", {"id": mf, "visible": "true"}), "show again")

    ok(m.invoke("color", {"color": "red"}), "color red")
    red_usage = ok(m.invoke("color usage"), "usage red")
    require([c["color_name"] for c in red_usage["colors"]] == ["red"] and
            red_usage["atom_count"] == 3, f"color usage counts atoms per color: {red_usage}")
    ok(m.invoke("scene store", {"name": "scene-red"}), "store red")
    ok(m.invoke("color", {"color": "blue", "selection": "name O"}), "color blue")
    mixed = ok(m.invoke("color usage"), "usage mixed")
    require(sorted(c["color_name"] for c in mixed["colors"]) == ["blue", "red"] and
            sum(c["atom_count"] for c in mixed["colors"]) == 3, "two colors after recoloring one atom")
    ok(m.invoke("scene recall", {"name": "scene-red"}), "recall red")
    require([c["color_name"] for c in ok(m.invoke("color usage"), "usage")["colors"]] == ["red"],
            "a scene restores the atom colors it stored")
    require(m.invoke("color usage", {"selection": "bogus ("})["status"] == "error",
            "an invalid selection is an error")
    ok(m.invoke("scene clear"), "clear")

    # camera.named_view.*, camera.rotate.by / translate: stored views and relative moves.
    def camera():
        return ok(m.invoke("view get"), "view get")["camera"]

    def close(a, b):
        return all(abs(x - y) < 1.0e-9 for x, y in zip(a, b))

    ok(m.invoke("view clear"), "view clear")
    require(ok(m.invoke("view list"), "view list")["count"] == 0, "no stored views")
    home = camera()
    ok(m.invoke("view store", {"name": "home"}), "store home")
    ok(m.invoke("view turn", {"axis": "y", "angle": "90"}), "turn")
    turned = camera()
    require(not close(turned["orientation"], home["orientation"]), "a turn changes the orientation")
    ok(m.invoke("view store", {"name": "side"}), "store side")
    again = ok(m.invoke("view store", {"name": "side"}), "replace side")
    require(again["replaced"] is True and again["count"] == 2, "storing a name again replaces it")
    ok(m.invoke("view recall", {"name": "home"}), "recall home")
    require(close(camera()["orientation"], home["orientation"]) and
            camera()["distance"] == home["distance"], "recall restores the stored camera")
    ok(m.invoke("view recall", {"name": "side"}), "recall side")
    require(close(camera()["orientation"], turned["orientation"]), "the second view is distinct")
    ok(m.invoke("view turn", {"axis": "y", "angle": "-90"}), "turn back")
    require(close(camera()["orientation"], home["orientation"]),
            "opposite turns cancel")
    ok(m.invoke("view turn", {"axis": "x", "angle": "30"}), "turn x")
    ok(m.invoke("view turn", {"axis": "x", "angle": "30"}), "turn x again")
    twice = camera()["orientation"]
    ok(m.invoke("view recall", {"name": "home"}), "recall home")
    ok(m.invoke("view turn", {"axis": "x", "angle": "60"}), "turn x 60")
    require(close(camera()["orientation"], twice), "two 30 degree turns equal one 60 degree turn")
    before_move = camera()
    ok(m.invoke("view move", {"axis": "x", "distance": "5"}), "move")
    moved = camera()
    require(moved["target"] != before_move["target"] or moved["model_origin"] != before_move["model_origin"],
            "a local move changes the camera placement")
    ok(m.invoke("view move", {"axis": "x", "distance": "-5"}), "move back")
    require(close(camera()["target"], before_move["target"]), "opposite moves cancel")
    for bad in ({"axis": "w", "angle": "1"}, {"axis": "x", "angle": "abc"}):
        require(m.invoke("view turn", bad)["status"] == "error", f"turn rejected: {bad}")
    require(m.invoke("view recall", {"name": "missing"})["status"] == "error" and
            m.invoke("view delete", {"name": "missing"})["status"] == "error",
            "unknown view names are errors")
    deleted = ok(m.invoke("view delete", {"name": "side"}), "delete")
    require(deleted["count"] == 1 and ok(m.invoke("view list"), "list")["count"] == 1, "delete one view")
    cleared_views = ok(m.invoke("view clear"), "clear")
    require(ok(m.invoke("view list"), "list")["count"] == 0, f"clear removes all views: {cleared_views}")
    ok(m.invoke("movie configure", {"frames": "7", "fps": "10"}), "movie 7")
    require(ok(m.invoke("movie status"), "status")["movie"]["frame_count"] == 7,
            "the movie frame count can be queried")
    ok(m.invoke("movie clear"), "movie clear")
    ok(m.invoke("view reset"), "view reset")

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

    # selection.macro.slash*: PyMOL slash macros are accepted wherever a selection is.
    def count(expression, obj=ubq):
        return ok(m.invoke("object count", {"object": obj, "selection": expression}),
                  f"count {expression}")["atom_count"]

    require(count("/ubq//A/10/CA") == count("chain A and resi 10 and name CA") == 1,
            "absolute macro /object//chain/resi/name")
    require(count("A/10/CA") == 1 and count("A/10/") == count("chain A and resi 10"),
            "relative macros are right-aligned")
    require(count("/ubq///10+12/CA") == 2 and count("/ubq////CA") == 76,
            "macro lists and empty fields")
    require(count("/ubq//A//C*") == count("chain A and name C*"), "macro wildcards")
    require(count("/nope//A/10/CA") == 0, "an unknown object matches nothing")
    require(m.invoke("object count", {"selection": "/a/b/c/d/e/f"})["status"] == "error",
            "a macro with more than five fields is rejected")
    require(count("/ubq`1////") == 1 and count("/ubq`1////") == count("object ubq and index 1"),
            "object`index backtick qualifier")
    require(count("A/GLY`10/") == count("chain A and resn GLY and resi 10") > 0 and
            count("A/LEU`10/") == 0, "resn`resi backtick qualifier")
    require(m.invoke("object count", {"selection": "A/10/CA`"})["status"] == "error" and
            m.invoke("object count", {"selection": "/ubq//A`1/10/CA"})["status"] == "error",
            "backtick qualifiers need a value and a field that allows them")
    require(count("A/10/CA or A/12/CA") == 2 and count("not /ubq//A") == 0,
            "macros compose with boolean operators")

    # VMD residue-class keywords on ubiquitin (76 residues, hydrophobic set = 28 residues).
    require(count("polar and name CA") == 48 and count("amino and name CA") == 76,
            "polar excludes the 28 hydrophobic residues; amino covers every residue")
    require(count("charged and name CA") ==
            count("(acidic or basic) and name CA") ==
            count("resname ASP+GLU+ARG+HIS+LYS and name CA"), "charged is basic or acidic")
    require(count("small and name CA") + count("medium and name CA") +
            count("large and name CA") >= 70, "size classes cover the protein residues")
    require(count("atomicnumber = 16") == count("element S") == 1 and
            count("numbonds = 0") == count("resn HOH") == 58 and count("numbonds > 0") == 602,
            "atomic number and bond count fields (load connects every non-water atom; the 58 waters are single O atoms)")
    require(count("radius > 1.6") == count("element C or element S"),
            "Bondi radii: carbon 1.7 and sulfur 1.8 exceed 1.6, nitrogen and oxygen do not")
    require(m.invoke("object count", {"selection": "mass > 10"})["status"] == "error",
            "mass needs an explicit topology mass property")

    # selection.named.merge / state_scope / value.mixed_residue.
    ok(m.invoke("select", {"name": "grp", "expression": "name CA"}), "select grp")
    ok(m.invoke("select", {"name": "grp", "expression": "resn HOH", "merge": "true"}),
       "merge grp")
    require(count("@grp") == 76 + 58, "a static merge adds to the frozen membership")
    ok(m.invoke("select", {"name": "grp", "expression": "name N"}), "replace grp")
    require(count("@grp") == 76, "without --merge the selection is replaced")
    require(m.invoke("select", {"name": "grp", "expression": "name O", "merge": "true",
                                "update": "true"})["status"] == "error",
            "merging across update modes is refused")
    require(m.invoke("select", {"name": "grp", "expression": "bogus (", "merge": "true"}
                     )["status"] == "error" and count("@grp") == 76,
            "a failed merge leaves the selection unchanged")
    ok(m.invoke("select", {"name": "fresh", "expression": "name CA", "merge": "true"}),
       "merge into a new name")
    require(count("@fresh") == 76, "merge into a missing name behaves like select")
    ok(m.invoke("select", {"name": "dyn", "expression": "name CA", "update": "true"}), "dyn")
    ok(m.invoke("select", {"name": "dyn", "expression": "name N", "update": "true",
                           "merge": "true"}), "merge dyn")
    require(count("@dyn") == 152, "a dynamic merge is the union of both expressions")

    require(count("resi 10A+11-20", ubq) == count("resi 10A") + count("resi 11-20"),
            "mixed residue lists combine ids and ranges")

    # selection.named.default_name / select_list* and value.escape / range.alpha.
    ok(m.invoke("select", {"expression": "name CA"}), "select without a name")
    require(count("@sele") == 76, "the default selection name is sele")
    ok(m.invoke("select list", {"name": "byindex", "values": "1,3,5-7"}), "list by index")
    require(count("@byindex") == 5, "select list by index with a range")
    ids = ok(m.invoke("select list", {"name": "byid", "by": "id", "values": "1,2"}), "list by id")
    require(ids["expression"] == "id 1+2" and count("@byid") == 2, "select list by source id")
    require(ok(m.invoke("select list", {"name": "byrank", "by": "rank", "values": "1-3"}),
               "list by rank")["by"] == "rank" and count("@byrank") == 3,
            "select list by load-order rank")
    ok(m.invoke("select list", {"name": "byindex", "values": "9", "merge": "true"}), "merge list")
    require(count("@byindex") == 6, "select list can merge into an existing selection")
    require(m.invoke("select list", {"name": "bad", "values": "1,,3"})["status"] == "error" and
            m.invoke("select list", {"name": "bad", "values": "1,2", "by": "bogus"}
                     )["status"] == "error", "empty items and unknown identifier kinds fail")
    require(count("chain A:C") == count("chain A-C") == count("chain A"), "alpha range with : or -")
    require(count(r"name C\*") == 0 and count("name C*") == count("element C"),
            "an escaped wildcard is a literal star")

    ok(m.invoke("object activate", {"id": mf}), "activate mf")
    for state, low, high in (("1", 2, 1), ("2", 0, 3), ("all", 2, 3)):
        ok(m.invoke("select", {"name": f"lo{state}", "expression": "x < 0.1",
                               "state": state}), "state select")
        ok(m.invoke("select", {"name": f"hi{state}", "expression": "x > 0.1",
                               "state": state}), "state select")
        require(count(f"@lo{state}", mf) == low and count(f"@hi{state}", mf) == high,
                f"static selection evaluated in state {state}")
    ok(m.invoke("select list", {"name": "pinned", "values": "1,3", "state": "2"}), "list state")
    require(count("@pinned", mf) == 2, "select list accepts an explicit state")
    require(m.invoke("select list", {"name": "pinned", "values": "1", "state": "9"}
                     )["status"] == "error", "select list validates the state")
    for bad in ("0", "3", "x"):
        require(m.invoke("select", {"name": "badstate", "expression": "all", "state": bad}
                         )["status"] == "error", f"state {bad} is rejected")
    require(m.invoke("select", {"name": "badstate", "expression": "all", "state": "2",
                                "update": "true"})["status"] == "error",
            "a dynamic selection cannot pin a state")
    ok(m.invoke("object activate", {"id": ubq}), "activate ubq")

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

    # object.visibility.{enable,disable}.{named,selection}, object.query.covering_objects/type.
    def visibility():
        return {row[1]: row[3] for row in
                ok(m.invoke("object list"), "list")["table"]["rows"]}

    before = visibility()
    chem_names = sorted(name for name in before if name.startswith("chem"))
    require(len(chem_names) >= 3, f"the chem copies exist: {chem_names}")
    off = ok(m.invoke("object disable", {"pattern": "chem*"}), "disable pattern")
    after = visibility()
    require(off["object_count"] == len(chem_names) and
            all(after[name] is False for name in chem_names) and
            all(after[name] == before[name] for name in before if name not in chem_names),
            "a name pattern changes exactly the matching objects")
    one = ok(m.invoke("object enable", {"pattern": "chem-c?py"}), "enable pattern")
    shown = visibility()
    require(one["object_count"] == 1 and shown["chem-copy"] is True and
            sum(1 for name in chem_names if shown[name]) == 1, "? matches one character")
    snapshot = visibility()
    for bad in ({"pattern": "no-such-*"}, {"pattern": ""}, {},
                {"pattern": "chem*", "selection": "all"}):
        require(m.invoke("object enable", bad)["status"] == "error", f"rejected: {bad}")
    require(visibility() == snapshot, "a rejected request changes nothing")
    ok(m.invoke("object enable", {"pattern": "*"}), "enable everything")
    require(all(visibility().values()), "the * pattern enables every object")

    covered = ok(m.invoke("object covering", {"selection": "name N"}), "covering")
    expected_ids = sorted(int(row[0]) for row in
                          ok(m.invoke("object list"), "list")["table"]["rows"]
                          if count("name N", str(row[0])) > 0)
    require(sorted(covered["object_ids"]) == expected_ids and int(ubq) in expected_ids and
            int(mf) not in expected_ids, f"covering objects: {covered}")
    water_ids = ok(m.invoke("object covering", {"selection": "resn HOH"}), "water")["object_ids"]
    require(len(water_ids) >= 1 and int(ubq) not in water_ids, "only the extracted water object")
    hide_water = ok(m.invoke("object disable", {"selection": "resn HOH"}), "disable selection")
    require(hide_water["object_count"] == len(water_ids) and
            all(not visibility()[row["name"]] for row in hide_water["objects"]),
            "a selection hides the objects it covers")
    ok(m.invoke("object enable", {"selection": "resn HOH"}), "enable selection")
    require(m.invoke("object disable", {"selection": "resn NOPE"})["status"] == "error" and
            m.invoke("object covering", {"selection": "bogus ("})["status"] == "error",
            "an uncovered or invalid selection is an error")

    require(ok(m.invoke("object type", {"name": "ubq"}), "type")["type"] == "object:molecule" and
            ok(m.invoke("object type", {"name": "potential"}), "type")["type"] == "object:volume",
            "object kinds")
    ok(m.invoke("object activate", {"id": ubq}), "activate ubq")
    ok(m.invoke("select", {"name": "typed", "expression": "name N"}), "select typed")
    require(ok(m.invoke("object type", {"name": "typed"}), "type")["type"] == "selection" and
            m.invoke("object type", {"name": "nothing-here"})["status"] == "error",
            "selection kind and unknown names")

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

    # io.save.selection, io.string.write_pdb/read_*, io.load.memory.
    ok(m.invoke("object activate", {"id": ubq}), "activate ubq")
    atoms_before = count("all")
    with tempfile.TemporaryDirectory() as directory:
        out = pathlib.Path(directory)
        saved = ok(m.invoke("save", {"path": str(out / "n.pdb"), "selection": "name N"}),
                   "save selection")
        require(saved["atom_count"] == 76 and saved["selection"] == "name N",
                f"only the selection is written: {saved}")
        back = ok(m.invoke("load", {"path": str(out / "n.pdb"), "name": "n-back"}), "reload")
        require(back["atom_count"] == 76, "the saved selection reloads as 76 atoms")
        ok(m.invoke("object activate", {"id": ubq}), "activate ubq")
        require(count("all") == atoms_before, "saving a selection does not change the object")
        empty = m.invoke("save", {"path": str(out / "none.pdb"), "selection": "resn XYZ"})
        require(empty["status"] == "error" and not (out / "none.pdb").exists(),
                "an empty selection writes nothing")
        everything = ok(m.invoke("save", {"path": str(out / "all.pdb")}), "save all")
        require(everything["atom_count"] == atoms_before and everything["selection"] == "all",
                "the default selection is the whole object")
        ok(m.invoke("object activate", {"id": mf}), "activate mf")
        both = ok(m.invoke("save", {"path": str(out / "mf.xyz"), "frames": "all",
                                    "selection": "name C or name O"}), "save frames")
        require(both["atom_count"] == 2 and both["frame_count"] == 2,
                f"a selection keeps every state: {both}")
        ok(m.invoke("object activate", {"id": ubq}), "activate ubq")

    text = ok(m.invoke("save text", {"selection": "name N", "file-format": "pdb"}), "save text")
    lines = [line for line in text["content"].splitlines() if line.startswith(("ATOM", "HETATM"))]
    require(text["atom_count"] == 76 and len(lines) == 76 and text["byte_count"] == len(
        text["content"].encode()), "pdb text of the selection")
    loaded_text = ok(m.invoke("load text", {"content": text["content"], "name": "from-text",
                                            "file-format": "pdb"}), "load text")
    require(loaded_text["atom_count"] == 76 and loaded_text["object_name"] == "from-text" and
            loaded_text["format"] == "pdb", f"in-memory load: {loaded_text}")
    guessed_text = ok(m.invoke("load text", {"content": text["content"], "name": "guessed"}),
                      "load text (auto)")
    require(guessed_text["atom_count"] == 76, "the format is detected from the text")
    ok(m.invoke("object activate", {"id": chem}), "activate chem")
    for fmt in ("sdf", "mol", "xyz"):
        exported = ok(m.invoke("save text", {"file-format": fmt}), f"save text {fmt}")
        again = ok(m.invoke("load text", {"content": exported["content"], "file-format": fmt,
                                          "name": f"chem-{fmt}"}), f"load text {fmt}")
        require(again["atom_count"] == exported["atom_count"] > 0, f"{fmt} text round trip")
    sdf_text = ok(m.invoke("save text", {"file-format": "sdf"}), "sdf")["content"]
    reread = ok(m.invoke("object copy", {"object": chem, "name": "chem-ref"}), "reference copy")
    again_sdf = ok(m.invoke("load text", {"content": sdf_text, "file-format": "sdf",
                                          "name": "chem-sdf-bonds"}), "load sdf text")
    require(ok(m.invoke("object count", {"object": str(again_sdf["object_id"])}),
               "count")["atom_count"] == reread["atom_count"], "sdf text keeps the atoms")
    objects_before = len(ok(m.invoke("object list"), "list")["table"]["rows"])
    for bad in ({"content": ""}, {"content": "not a structure at all\n", "file-format": "pdb"},
                {"content": "x" * 10, "file-format": "sdf"}):
        require(m.invoke("load text", bad)["status"] == "error", f"rejected text: {bad}")
    require(len(ok(m.invoke("object list"), "list")["table"]["rows"]) == objects_before,
            "rejected text creates no object")
    ok(m.invoke("object activate", {"id": ubq}), "activate ubq")

    # io.format.ccp4.write / map.write: CCP4 and MAP are written by `volume save`.
    ok(m.invoke("volume load", {"path": str(fixtures / "potential.dx"), "name": "pot-save"}),
       "volume load")
    with tempfile.TemporaryDirectory() as directory:
        out = pathlib.Path(directory)
        for name, fmt in (("a.ccp4", "ccp4"), ("b.map", "map"), ("c.map", "auto"),
                          ("d.ccp4", "auto"), ("e.mrc", "mrc")):
            wrote = ok(m.invoke("volume save", {"path": str(out / name), "file-format": fmt}),
                       f"volume save {name}")
            require(wrote["dimensions"] == [2, 2, 3] and wrote["byte_count"] > 1000, name)
            reread_volume = ok(m.invoke("volume load", {"path": str(out / name)}), f"reload {name}")
            require(reread_volume["dimensions"] == [2, 2, 3] and
                    abs(reread_volume["minimum"] + 2.5) < 1.0e-6 and
                    abs(reread_volume["maximum"] - 9.5) < 1.0e-6, f"{name} round trip")

    # export.image.format / dimension_units / filename_extension / source and
    # export.movie.frame_sequence*: encodings, physical sizes and numbered frames.
    ok(m.invoke("object activate", {"id": ubq}), "activate ubq")
    ok(m.invoke("show", {"representation": "lines"}), "show lines")
    with tempfile.TemporaryDirectory() as directory:
        out = pathlib.Path(directory)

        def image(name, **arguments):
            return ok(m.invoke("image save", {"path": str(out / name), **arguments}),
                      f"image save {name}")

        png = image("a.png", width="200")
        require(png["format"] == "png" and png["width"] == png["height"] == 200 and
                png["primitive_count"] > 0, f"png export: {png}")
        require((out / "a.png").read_bytes()[:8] == b"\x89PNG\r\n\x1a\n", "PNG signature")
        ppm = image("b.ppm", height="40", **{"ray": "false"})
        require(ppm["format"] == "ppm" and ppm["height"] == 40 and ppm["width"] == 40,
                "ppm export with an aspect-preserving width")
        require((out / "b.ppm").read_bytes()[:2] == b"P6", "PPM signature")
        bare = image("noext", width="30")
        require(bare["path"].endswith("noext.png") and (out / "noext.png").exists() and
                not (out / "noext").exists(), "a missing extension gets the format's extension")
        forced = image("raw.dat", width="20", **{"image-format": "ppm"})
        require(forced["format"] == "ppm" and (out / "raw.dat").read_bytes()[:2] == b"P6",
                "an explicit format keeps the given file name")
        guessed = m.invoke("image save", {"path": str(out / "x.jpg")})
        require(guessed["status"] == "error" and not (out / "x.jpg").exists(),
                "an unknown extension cannot be guessed")
        for units, value, pixels in (("inches", "2", 200), ("centimeters", "2.54", 100),
                                     ("millimeters", "25.4", 100)):
            sized = image(f"{units}.png", width=value, units=units, dpi="100")
            require(sized["width"] == pixels and sized["dpi"] == 100.0, f"{units}: {sized}")
            require(b"pHYs" in (out / f"{units}.png").read_bytes(), "dpi is written to the PNG")
        require(image("default-dpi.png", width="1", units="inches")["width"] == 300,
                "physical sizes default to 300 dpi")
        for bad in ({"width": "10.5"}, {"width": "-1"}, {"units": "furlongs"},
                    {"width": "0.0001", "units": "inches", "dpi": "10"}):
            require(m.invoke("image save", {"path": str(out / "bad.png"), **bad}
                             )["status"] == "error", f"rejected: {bad}")
        require(not (out / "bad.png").exists(), "a rejected export writes nothing")
        draw = image("draw.png", width="16", ray="false")
        ray = image("ray.png", width="16", ray="true")
        require(draw["warnings"] != ray["warnings"], "draw and ray sources are distinguished")

        require(m.invoke("movie export", {"prefix": str(out / "none_")})["status"] == "error",
                "movie export needs a configured timeline")
        ok(m.invoke("scene clear"), "scene clear")
        ok(m.invoke("movie configure", {"frames": "3", "fps": "10"}), "movie configure")
        ok(m.invoke("scene store", {"name": "near"}), "scene near")
        ok(m.invoke("view zoom", {"buffer": "8"}), "zoom")
        ok(m.invoke("scene store", {"name": "far"}), "scene far")
        ok(m.invoke("movie keyframe", {"frame": "1", "scene": "near"}), "keyframe 1")
        ok(m.invoke("movie keyframe", {"frame": "3", "scene": "far"}), "keyframe 3")
        before = ok(m.invoke("movie status"), "status")["movie"]["current_frame"]
        sequence = ok(m.invoke("movie export", {"prefix": str(out / "mv_"), "width": "48"}),
                      "movie export")
        names = sorted(path.name for path in out.glob("mv_*"))
        require(names == ["mv_0001.png", "mv_0002.png", "mv_0003.png"] and
                sequence["frame_count"] == 3 and sequence["width"] == 48,
                f"numbered frames: {names} {sequence}")
        first_bytes = (out / "mv_0001.png").read_bytes()
        last_bytes = (out / "mv_0003.png").read_bytes()
        require(first_bytes != last_bytes, "each frame is rendered at its own timeline position")
        require(ok(m.invoke("movie status"), "status")["movie"]["current_frame"] == before,
                "the timeline position is restored after export")
        again = m.invoke("movie export", {"prefix": str(out / "mv_"), "width": "48"})
        require(again["status"] == "error" and (out / "mv_0001.png").read_bytes() == first_bytes,
                "existing frames are not replaced without --overwrite")
        ok(m.invoke("movie export", {"prefix": str(out / "mv_"), "width": "48",
                                     "overwrite": "true"}), "overwrite")
        ranged = ok(m.invoke("movie export", {"prefix": str(out / "r_"), "first": "2",
                                              "last": "3", "image-format": "ppm",
                                              "width": "16", "ray": "false"}), "range")
        require(sorted(path.name for path in out.glob("r_*")) == ["r_0002.ppm", "r_0003.ppm"] and
                ranged["frame_count"] == 2 and ranged["format"] == "ppm",
                "an inclusive range writes only those frames")
        for bad in ({"first": "3", "last": "2"}, {"first": "0"}, {"last": "9"}):
            require(m.invoke("movie export", {"prefix": str(out / "bad_"), **bad}
                             )["status"] == "error", f"range rejected: {bad}")
        require(not list(out.glob("bad_*")), "a rejected range writes nothing")
        ok(m.invoke("movie play"), "play")
        require(m.invoke("movie export", {"prefix": str(out / "play_")})["status"] == "error",
                "export is refused while the movie plays")
        ok(m.invoke("movie pause"), "pause")
        require(not list(out.glob("play_*")), "a refused export writes nothing")
        missing = m.invoke("movie export", {"prefix": str(out / "no-dir" / "f_"), "width": "8"})
        require(missing["status"] == "error" and not list(out.rglob("f_*")),
                "an unwritable target fails without leaving frames behind")

    # analysis.query.*, analysis.charge.*, analysis.mass.compute, iterate alternatives.
    queried = str(load("queried", "1ubq.pdb")["object_id"])
    ok(m.invoke("object activate", {"id": queried}), "activate queried")
    atoms = ok(m.invoke("object atoms", {"selection": "resi 1 and name CA",
                                         "properties": "index,id,name,resn,resi,chain,x,y,z,b,q,element,atomic_number"}),
               "atoms")
    row = atoms["table"]["rows"][0]
    require(atoms["returned_count"] == 1 and row[2] == "CA" and row[3] == "MET" and
            row[4] == "1" and row[5] == "A" and row[11] == "C" and row[12] == 6.0 and
            abs(row[6] - 26.266) < 1.0e-9, f"atom property table: {atoms['table']}")
    first_three = ok(m.invoke("object atoms", {"selection": "name CA", "limit": "3"}), "limit")
    require(first_three["returned_count"] == 3 and first_three["selected_atom_count"] == 76 and
            first_three["truncated"] is True, "the listing limit truncates and reports the total")
    indices = ok(m.invoke("object atoms", {"selection": "index 5-8", "properties": "index"}), "indices")
    require([r[0] for r in indices["table"]["rows"]] == [5, 6, 7, 8], "atom indices are 1-based")
    for bad in ({"properties": "bogus"}, {"properties": ""}, {"limit": "0"}, {"selection": "bogus ("}):
        require(m.invoke("object atoms", bad)["status"] == "error", f"atom listing rejected: {bad}")
    require(ok(m.invoke("object chains"), "chains")["chains"] == ["A"], "chain identifiers")
    require(ok(m.invoke("object chains", {"selection": "resn HOH"}), "water chains")["chain_count"] >= 0,
            "chain query on a selection")
    require(ok(m.invoke("analyze charge"), "formal")["charge"] == 0.0 and
            m.invoke("analyze charge", {"kind": "partial"})["status"] == "error",
            "formal charge sums; partial charge needs the property")
    mass = ok(m.invoke("analyze mass", {"selection": "name CA"}), "mass")
    require(abs(mass["mass"] - 76 * 12.011) < 1.0e-3 and mass["atom_count"] == 76 and
            mass["unit"] == "dalton" and mass["mass_estimated"] is True,
            f"mass of the carbon atoms: {mass}")
    require(m.invoke("analyze mass", {"selection": "resn XYZ"})["status"] == "error",
            "an empty mass selection is an error")
    angles = ok(m.invoke("analyze phi-psi", {"selection": "resi 23-26"}), "phi psi")
    helix = angles["table"]["rows"]
    require(len(helix) == 4 and all(-80.0 < r[4] < -40.0 and -60.0 < r[5] < -20.0 for r in helix),
            f"helical residues have phi near -60 and psi near -45: {helix}")
    end = ok(m.invoke("analyze phi-psi", {"selection": "resi 1"}), "first residue")["table"]["rows"][0]
    require(end[4] is None and end[5] is not None, "the first residue has no phi")
    require(m.invoke("analyze phi-psi", {"selection": "bogus ("})["status"] == "error",
            "an invalid selection is an error")

    # analysis.measure.fit, analysis.fit.identifier_matched/explicit_pairs: rigid alignment.
    import math
    reference = str(load("align-ref", "1ubq.pdb")["object_id"])
    ok(m.invoke("object activate", {"id": reference}), "activate reference")
    ca = ok(m.invoke("object atoms", {"selection": "name CA", "properties": "x,y,z,resi,name"}),
            "CA coordinates")["table"]["rows"]
    angle_z, angle_x = math.radians(40.0), math.radians(25.0)
    shift = (3.0, -2.0, 5.0)

    def rotate(point):
        x, y, z = point
        x, y = x * math.cos(angle_z) - y * math.sin(angle_z), x * math.sin(angle_z) + y * math.cos(angle_z)
        y, z = y * math.cos(angle_x) - z * math.sin(angle_x), y * math.sin(angle_x) + z * math.cos(angle_x)
        return (x + shift[0], y + shift[1], z + shift[2])

    moved = [rotate((row[0], row[1], row[2])) for row in ca]
    xyz_text = f"{len(moved)}\nrotated copy\n" + "".join(
        f"C {x:.6f} {y:.6f} {z:.6f}\n" for x, y, z in moved)
    ok(m.invoke("load text", {"content": xyz_text, "name": "align-moved", "file-format": "xyz"}),
       "load rotated")
    ref_ca = ok(m.invoke("object count", {"object": reference, "selection": "name CA"}), "count")
    require(ref_ca["atom_count"] == len(moved) == 76, "same number of fitted atoms")
    fit = ok(m.invoke("measure fit", {"target-object": reference, "mobile-object": "align-moved",
                                      "target-selection": "name CA", "precision": "6"}), "fit")
    require(fit["applied"] is False and fit["paired_atom_count"] == 76 and
            fit["rmsd_before"] > 5.0 and fit["rmsd_after"] < 1.0e-4,
            f"measure fit finds the superposition: {fit}")
    matrix = fit["matrix"]
    # The fit must undo the rotation: applying it to a moved point returns the original.
    sample_index = 10
    px, py, pz = moved[sample_index]
    mapped = (matrix[0] * px + matrix[1] * py + matrix[2] * pz + matrix[3],
              matrix[4] * px + matrix[5] * py + matrix[6] * pz + matrix[7],
              matrix[8] * px + matrix[9] * py + matrix[10] * pz + matrix[11])
    original = (ca[sample_index][0], ca[sample_index][1], ca[sample_index][2])
    require(all(abs(a - b) < 1.0e-4 for a, b in zip(mapped, original)),
            f"the matrix maps the moved atom back: {mapped} vs {original}")
    require(matrix[12:] == [0.0, 0.0, 0.0, 1.0], "affine bottom row")
    # measure fit changes nothing.
    again = ok(m.invoke("measure fit", {"target-object": reference, "mobile-object": "align-moved",
                                        "target-selection": "name CA"}), "fit again")
    require(abs(again["rmsd_before"] - fit["rmsd_before"]) < 1.0e-9, "measure fit is read-only")
    # Atom IDs/identifiers cannot pair the xyz copy; order pairing needs equal counts.
    require(m.invoke("measure fit", {"target-object": reference, "mobile-object": "align-moved",
                                     "target-selection": "name CA or name N"})["status"] == "error",
            "unequal selections are rejected for order matching")
    require(m.invoke("measure fit", {"target-object": reference, "mobile-object": reference
                                     })["status"] == "error", "an object cannot be fitted to itself")
    require(m.invoke("measure fit", {"target-object": reference, "mobile-object": "align-moved",
                                     "target-selection": "resi 1"})["status"] == "error",
            "fewer than three paired atoms are rejected")
    require(m.invoke("measure fit", {"target-object": "nope", "mobile-object": "align-moved"}
                     )["status"] == "error", "an unknown target is an error")
    # Explicit pairs (1-based indices in each object) reproduce the order match.
    ok(m.invoke("object activate", {"id": reference}), "activate reference")
    ca_index = [int(r[0]) for r in ok(m.invoke("object atoms", {"selection": "name CA", "properties": "index"}),
                                      "indices")["table"]["rows"]]
    pairs = ",".join(f"{i + 1}:{ca_index[i]}" for i in range(len(ca_index)))
    explicit = ok(m.invoke("measure fit", {"target-object": reference, "mobile-object": "align-moved",
                                           "pairs": pairs}), "explicit pairs")
    require(explicit["paired_atom_count"] == 76 and explicit["rmsd_after"] < 1.0e-4 and
            all(abs(a - b) < 1.0e-6 for a, b in zip(explicit["matrix"], fit["matrix"])),
            "explicit pairs give the same fit")
    for bad in ("1-2", "a:b", "0:1", "1:99999"):
        require(m.invoke("measure fit", {"target-object": reference, "mobile-object": "align-moved",
                                         "pairs": bad})["status"] == "error", f"pairs rejected: {bad}")
    # Identifier matching: two copies of the same structure pair by chain/residue/name.
    twin = str(load("align-twin", "1ubq.pdb")["object_id"])
    ident = ok(m.invoke("measure fit", {"target-object": reference, "mobile-object": twin,
                                        "match": "identifier", "target-selection": "polymer.protein",
                                        "mobile-selection": "polymer.protein"}), "identifier fit")
    require(ident["paired_atom_count"] == 602 and ident["rmsd_before"] < 1.0e-6,
            f"identifier matching pairs identical atoms: {ident}")
    mass_fit = ok(m.invoke("measure fit", {"target-object": reference, "mobile-object": "align-moved",
                                           "target-selection": "name CA", "weight": "mass"}), "mass fit")
    require(mass_fit["rmsd_after"] < 1.0e-4, "mass weighting also converges for an exact copy")

    # object align applies the same fit to the scene transform only.
    atoms_before = ok(m.invoke("object atoms", {"selection": "index 1-3", "properties": "x,y,z",
                                                "limit": "3"}), "coordinates")["table"]["rows"]
    ok(m.invoke("object activate", {"id": reference}), "activate reference")
    applied = ok(m.invoke("object align", {"target-object": reference, "mobile-object": "align-moved",
                                           "target-selection": "name CA"}), "align")
    require(applied["applied"] is True and applied["scene_version"] > 0 and
            applied["rmsd_after"] < 1.0e-4, f"object align applies the fit: {applied}")
    after_fit = ok(m.invoke("measure fit", {"target-object": reference, "mobile-object": "align-moved",
                                            "target-selection": "name CA"}), "fit after align")
    require(after_fit["rmsd_before"] < 1.0e-3,
            f"after aligning, the world coordinates already coincide: {after_fit}")
    ok(m.invoke("object activate", {"id": reference}), "activate reference")
    require(ok(m.invoke("object atoms", {"selection": "index 1-3", "properties": "x,y,z", "limit": "3"}),
               "coordinates")["table"]["rows"] == atoms_before, "object align never edits coordinates")
    moved_listing = ok(m.invoke("object atoms", {"selection": "index 1", "properties": "x,y,z"}), "x")
    ok(m.invoke("view reset", {"object": "align-moved"}), "reset the aligned object")
    restored = ok(m.invoke("measure fit", {"target-object": reference, "mobile-object": "align-moved",
                                           "target-selection": "name CA"}), "fit after reset")
    require(restored["rmsd_before"] > 5.0, "view reset --object undoes the alignment")
    ok(m.invoke("object align", {"target-object": reference, "mobile-object": "align-moved",
                                 "target-selection": "name CA"}), "align again")

    # io.load.state_append: more states for an existing object.
    stateful = str(load("stateful", "1ubq.pdb")["object_id"])
    ok(m.invoke("object activate", {"id": stateful}), "activate stateful")
    original_x = ok(m.invoke("object atoms", {"selection": "index 1", "properties": "x"}), "x")["table"]["rows"][0][0]
    pdb_text = ok(m.invoke("save text", {"file-format": "pdb"}), "pdb text")["content"]

    def shifted(text, delta):
        lines = []
        for line in text.splitlines():
            if line.startswith(("ATOM", "HETATM")):
                x = float(line[30:38]) + delta
                line = line[:30] + f"{x:8.3f}" + line[38:]
            lines.append(line)
        return "\n".join(lines) + "\n"

    with tempfile.TemporaryDirectory() as directory:
        out = pathlib.Path(directory)
        (out / "moved.pdb").write_text(shifted(pdb_text, 5.0), encoding="utf-8")
        appended = ok(m.invoke("load append", {"path": str(out / "moved.pdb"), "object": "stateful"}),
                      "append")
        require(appended["previous_state_count"] == 1 and appended["appended_state_count"] == 1 and
                appended["state_count"] == 2, f"one state appended: {appended}")
        info = {row[1]: row for row in ok(m.invoke("object list"), "list")["table"]["rows"]}
        require(info["stateful"][6] == 2 and info["stateful"][8] is True,
                "the object now has two states and a state timeline")
        ok(m.invoke("traj frame", {"frame": "1"}), "second state")
        second_x = ok(m.invoke("object atoms", {"selection": "index 1", "properties": "x"}), "x")["table"]["rows"][0][0]
        ok(m.invoke("traj frame", {"frame": "0"}), "first state")
        first_x = ok(m.invoke("object atoms", {"selection": "index 1", "properties": "x"}), "x")["table"]["rows"][0][0]
        require(abs(first_x - original_x) < 1.0e-9 and abs(second_x - (original_x + 5.0)) < 2.0e-3,
                f"states hold their own coordinates: {first_x} {second_x}")
        # Appending to a multi-state object keeps the current state.
        ok(m.invoke("traj frame", {"frame": "1"}), "seek")
        (out / "moved2.pdb").write_text(shifted(pdb_text, -5.0), encoding="utf-8")
        again = ok(m.invoke("load append", {"path": str(out / "moved2.pdb"), "object": "stateful"}),
                   "append again")
        require(again["state_count"] == 3, "a third state")
        require(ok(m.invoke("traj frame", {"frame": "2"}), "third state")["frame"] == 2,
                "the new state is reachable")
        # A file with different atoms is refused and nothing changes.
        renamed = pdb_text.replace(" CA ", " CX ", 1)
        (out / "renamed.pdb").write_text(renamed, encoding="utf-8")
        mismatch = m.invoke("load append", {"path": str(out / "renamed.pdb"), "object": "stateful"})
        require(mismatch["status"] == "error" and "differs" in mismatch["error"]["message"],
                f"different atom names are refused: {mismatch}")
        short = pdb_text.splitlines()
        drop = next(i for i, line in enumerate(short) if line.startswith("ATOM"))
        (out / "short.pdb").write_text("\n".join(short[:drop] + short[drop + 1:]) + "\n", encoding="utf-8")
        require(m.invoke("load append", {"path": str(out / "short.pdb"), "object": "stateful"}
                         )["status"] == "error", "a different atom count is refused")
        require(m.invoke("load append", {"path": str(out / "missing.pdb"), "object": "stateful"}
                         )["status"] == "error" and
                m.invoke("load append", {"path": str(out / "moved.pdb"), "object": "no-such"}
                         )["status"] == "error", "missing file or object is an error")
        multi = {row[1]: row for row in ok(m.invoke("object list"), "list")["table"]["rows"]}
        require(multi["stateful"][6] == 3, "refused appends leave the state count unchanged")
        # A multi-model object grows too: mf has two states, append its own file.
        ok(m.invoke("load append", {"path": str(fixtures / "multiframe.xyz"), "object": mf}),
           "append to multi-state")
        require({row[1]: row for row in ok(m.invoke("object list"), "list")["table"]["rows"]}["mf"][6] == 4,
                "two states plus two appended states")

    # pbc.wrap / pbc.unwrap: non-destructive frame processing (Q-035 a).
    def periodic_pdb(positions, cell="10.000   10.000   10.000"):
        out = [f"CRYST1   {cell}  90.00  90.00  90.00 P 1           1"]
        for index, (x, y, z) in enumerate(positions, 1):
            out += [f"MODEL     {index:4d}",
                    f"ATOM      1  C   GLY A   1    {x:8.3f}{y:8.3f}{z:8.3f}  1.00  0.00           C",
                    f"ATOM      2  O   GLY A   1    {x + 1:8.3f}{y:8.3f}{z:8.3f}  1.00  0.00           O",
                    "ENDMDL"]
        return "\n".join(out + ["END"]) + "\n"

    def x_in_state(frame, selection="name C"):
        ok(m.invoke("traj frame", {"frame": str(frame)}), "seek")
        return ok(m.invoke("object atoms", {"selection": selection, "properties": "x"}),
                  "x")["table"]["rows"][0][0]

    drift = [(8.0, 5.0, 5.0), (9.5, 5.0, 5.0), (1.0, 5.0, 5.0), (2.5, 5.0, 5.0)]
    ok(m.invoke("load text", {"content": periodic_pdb(drift), "name": "box", "file-format": "pdb"}),
       "load periodic")
    require(m.invoke("pbc mode", {"mode": "status"})["data"]["mode"] == "none", "mode starts as none")
    require([x_in_state(i) for i in range(4)] == [8.0, 9.5, 1.0, 2.5], "as loaded")
    unwrapped = ok(m.invoke("pbc mode", {"mode": "unwrap"}), "unwrap")
    require(unwrapped["changed"] is True and unwrapped["previous_mode"] == "none" and
            unwrapped["frame_count"] == 4, f"unwrap switches the mode: {unwrapped}")
    require([x_in_state(i) for i in range(4)] == [8.0, 9.5, 11.0, 12.5],
            "unwrap continues the drift across the periodic boundary")
    require(x_in_state(1) == 9.5 and x_in_state(3) == 12.5 and x_in_state(0) == 8.0,
            "jumping between frames gives the same unwrapped positions")
    same = ok(m.invoke("pbc mode", {"mode": "unwrap"}), "repeat")
    require(same["changed"] is False, "setting the active mode again is a no-op")
    ok(m.invoke("pbc mode", {"mode": "none"}), "restore")
    require([x_in_state(i) for i in range(4)] == [8.0, 9.5, 1.0, 2.5],
            "mode none restores the loaded coordinates (nothing was edited)")

    ok(m.invoke("load text", {"content": periodic_pdb([(23.0, -4.0, 5.0), (3.0, 6.0, 5.0)]),
                              "name": "outside", "file-format": "pdb"}), "load outside")
    ok(m.invoke("pbc mode", {"mode": "wrap"}), "wrap")
    require(abs(x_in_state(0) - 3.0) < 1.0e-3 and
            abs(ok(m.invoke("object atoms", {"selection": "name C", "properties": "y"}),
                   "y")["table"]["rows"][0][1 - 1] - 6.0) < 1.0e-3,
            "wrap folds atoms back into the primary cell")
    wrapped_x = x_in_state(1)
    require(abs(wrapped_x - 3.0) < 1.0e-3, "an atom already in the cell is unchanged")
    # Copies of a processed object carry the processed coordinates; editing is refused.
    copied = ok(m.invoke("object copy", {"object": "outside", "name": "outside-copy"}), "copy")
    ok(m.invoke("object activate", {"id": str(copied["object_id"])}), "activate copy")
    require(abs(x_in_state(0) - 3.0) < 1.0e-3 and
            m.invoke("pbc mode", {"mode": "status"})["data"]["mode"] == "none",
            "a copy bakes the processed coordinates and has no mode")
    ok(m.invoke("object activate", {"id": str(ok(m.invoke("object list"), "l")["table"]["rows"][-2][0])}),
       "back")
    outside_id = [r[0] for r in ok(m.invoke("object list"), "list")["table"]["rows"] if r[1] == "outside"][0]
    ok(m.invoke("object activate", {"id": str(outside_id)}), "activate outside")
    for refused in (("object remove-atoms", {"selection": "name O"}),
                    ("load append", {"path": str(fixtures / "multiframe.xyz"), "object": "outside"})):
        require(m.invoke(refused[0], refused[1])["status"] == "error",
                f"{refused[0]} is refused while PBC processing is active")
    require(len(ok(m.invoke("object atoms", {"selection": "all", "properties": "index"}),
                   "atoms")["table"]["rows"]) == 2, "refused edits change nothing")
    ok(m.invoke("pbc mode", {"mode": "none"}), "none")
    ok(m.invoke("object remove-atoms", {"selection": "name O"}), "editing works again")

    for bad in ({"mode": "spin"}, {"mode": "wrap", "object": "no-such"}):
        require(m.invoke("pbc mode", bad)["status"] == "error", f"pbc mode rejected: {bad}")
    ok(m.invoke("object activate", {"id": mf}), "activate mf")
    nocell = m.invoke("pbc mode", {"mode": "wrap"})
    require(nocell["status"] == "error" and "unit cell" in nocell["error"]["message"],
            "an object without a unit cell cannot be wrapped")
    ok(m.invoke("object activate", {"id": ubq}), "activate ubq")

    # object.split.chains, object.order.natural_sort, object.matrix.copy/reset,
    # io.load.extension_alias.
    def multi_chain_pdb(chains):
        lines = []
        for serial, chain in enumerate(chains, 1):
            lines.append(f"ATOM  {serial:5d}  CA  GLY {chain}{serial:4d}    {serial * 3.8:8.3f}{0.0:8.3f}{0.0:8.3f}  1.00  0.00           C")
        return "\n".join(lines + ["END"]) + "\n"

    def object_names():
        return [row[1] for row in ok(m.invoke("object list"), "list")["table"]["rows"]]

    ok(m.invoke("load text", {"content": multi_chain_pdb("AABBC"), "name": "tri", "file-format": "pdb"}),
       "load three chains")
    ok(m.invoke("load text", {"content": multi_chain_pdb("AAAA"), "name": "solo", "file-format": "pdb"}),
       "load one chain")
    require(m.invoke("object split", {"object": "solo"})["status"] == "error",
            "a single-chain object cannot be split")
    ok(m.invoke("load text", {"content": multi_chain_pdb("A"), "name": "tri_B", "file-format": "pdb"}),
       "occupy a target name")
    before_count = len(object_names())
    require(m.invoke("object split", {"object": "tri"})["status"] == "error" and
            len(object_names()) == before_count, "a name collision creates nothing")
    parts = ok(m.invoke("object split", {"object": "tri", "prefix": "part-"}), "split")
    counts = {row[1]: row[3] for row in parts["table"]["rows"]}
    require(parts["object_count"] == 3 and counts == {"part-A": 2, "part-B": 2, "part-C": 1},
            f"one object per chain: {counts}")
    require(ok(m.invoke("object count", {"object": "tri"}), "count")["atom_count"] == 5,
            "splitting leaves the source untouched")
    for name in ("s10", "s2", "s1"):
        ok(m.invoke("load text", {"content": multi_chain_pdb("A"), "name": name,
                                  "file-format": "pdb"}), f"load {name}")
    ordered = [n for n in object_names() if n in ("s10", "s2", "s1")]
    require(ordered == ["s10", "s2", "s1"], "loaded in the given order")
    ok(m.invoke("object sort"), "object sort")
    after_sort = object_names()
    require(after_sort == sorted(after_sort, key=lambda n: [int(t) if t.isdigit() else t
                                                              for t in __import__("re").split(r"(\d+)", n)]),
            f"objects are natural-sorted by name: {after_sort}")
    require([n for n in after_sort if n in ("s10", "s2", "s1")] == ["s1", "s2", "s10"],
            "s2 sorts before s10")

    ok(m.invoke("object activate", {"id": reference}), "activate reference")
    twin_b = str(load("matrix-twin", "1ubq.pdb")["object_id"])
    ok(m.invoke("object align", {"target-object": reference, "mobile-object": "align-moved",
                                 "target-selection": "name CA"}), "align moved")
    twin_c = str(load("matrix-twin2", "1ubq.pdb")["object_id"])
    ok(m.invoke("object activate", {"id": twin_b}), "activate twin")
    ok(m.invoke("object matrix-copy", {"source": "align-moved", "target": "matrix-twin"}), "copy placement")
    ok(m.invoke("object matrix-copy", {"source": "align-moved", "target": "matrix-twin2"}), "copy placement")
    together = ok(m.invoke("measure fit", {"target-object": "matrix-twin2", "mobile-object": "matrix-twin",
                                           "target-selection": "name CA", "mobile-selection": "name CA"}),
                  "copies agree")
    apart = ok(m.invoke("measure fit", {"target-object": reference, "mobile-object": "matrix-twin",
                                        "target-selection": "name CA", "mobile-selection": "name CA"}),
               "copy versus untouched reference")
    require(together["rmsd_before"] < 1.0e-6 and apart["rmsd_before"] > 1.0,
            f"copied placements agree and differ from the original pose: {together['rmsd_before']} {apart['rmsd_before']}")
    require(m.invoke("object matrix-copy", {"source": "matrix-twin", "target": "matrix-twin"})["status"] == "error" and
            m.invoke("object matrix-copy", {"source": "nope", "target": "matrix-twin"})["status"] == "error",
            "same or unknown objects are errors")
    ok(m.invoke("view reset", {"object": "matrix-twin"}), "reset the copied placement")
    restored_pose = ok(m.invoke("measure fit", {"target-object": reference, "mobile-object": "matrix-twin",
                                                "target-selection": "name CA", "mobile-selection": "name CA"}),
                       "after reset")
    require(restored_pose["rmsd_before"] < 1.0e-6, "view reset --object restores the original placement")

    with tempfile.TemporaryDirectory() as directory:
        out = pathlib.Path(directory)
        pdb_body = (fixtures / "1ubq.pdb").read_text(encoding="utf-8")
        for index, extension in enumerate((".ent", ".brk", ".PDB", ".Pdb")):
            path = out / f"alias{index}{extension}"
            path.write_text(pdb_body, encoding="utf-8")
            loaded = ok(m.invoke("load", {"path": str(path), "name": f"alias{index}"}), f"load {extension}")
            require(loaded["format"] == "pdb" and loaded["atom_count"] == 660, f"{extension} is a PDB alias")
        cif_path = out / "model.MMCIF"
        cif_path.write_text((fixtures / "synthetic_multimodel.cif").read_text(encoding="utf-8"), encoding="utf-8")
        require(ok(m.invoke("load", {"path": str(cif_path), "name": "alias-cif"}), "load MMCIF")["format"] == "mmcif",
                "extensions are case-insensitive")

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

    # object.hierarchy.group.*: workspace-level groups (Q-033 a).
    m.reset_runtime()
    ids = {name: str(load(name, "multiframe.xyz")["object_id"]) for name in "abcdef"}

    def groups():
        return {g["name"]: g for g in ok(m.invoke("group list"), "group list")["groups"]}

    def names():
        return [row[1] for row in ok(m.invoke("object list"), "list")["table"]["rows"]]

    made = ok(m.invoke("group add", {"name": "G", "members": "a b"}), "group add")
    require(made["created"] is True and made["member_count"] == 2 and
            made["affected"] == ["a", "b"], f"group add creates: {made}")
    nested = ok(m.invoke("group add", {"name": "H", "members": "G, c"}), "nested")
    listing = groups()
    require(listing["G"]["parent"] == "H" and listing["H"]["members"] == ["G", "c"] and
            listing["H"]["object_count"] == 3 and listing["G"]["open"] is True,
            f"groups nest: {listing}")
    for bad in ({"name": "G", "members": "H"}, {"name": "H", "members": "H"},
                {"name": "d", "members": "e"}, {"name": "X", "members": "nothing*"},
                {"name": "X", "members": ""}, {"name": "bad,name", "members": "e"}):
        require(m.invoke("group add", bad)["status"] == "error", f"group add rejected: {bad}")
    require(set(groups()) == {"G", "H"}, "rejected group requests change nothing")
    # An object belongs to one group: adding it elsewhere moves it.
    ok(m.invoke("group add", {"name": "K", "members": "a"}), "move a")
    require(groups()["G"]["members"] == ["b"] and groups()["K"]["members"] == ["a"],
            "adding to another group moves the member")
    ok(m.invoke("group add", {"name": "K", "members": "a b"}), "move b")
    require(groups()["G"]["members"] == [] and groups()["K"]["members"] == ["a", "b"],
            "members are appended in order")
    ok(m.invoke("group add", {"name": "G", "members": "a"}), "back to G")

    # Visibility follows group names; the group state is derived.
    ok(m.invoke("object disable", {"pattern": "H"}), "disable group")
    shown = {row[1]: row[3] for row in ok(m.invoke("object list"), "list")["table"]["rows"]}
    require(shown["a"] is False and shown["c"] is False and shown["d"] is True and
            groups()["H"]["enabled"] is False, "disabling a group hides everything below it")
    ok(m.invoke("object enable", {"pattern": "a"}), "enable member")
    require(groups()["H"]["enabled"] is True and groups()["G"]["enabled"] is True,
            "enabling one member enables its parent groups")
    ok(m.invoke("object enable", {"pattern": "H"}), "enable group")

    # remove / ungroup / empty.
    removed = ok(m.invoke("group remove", {"name": "K", "members": "b"}), "remove")
    require(removed["affected"] == ["b"] and groups()["K"]["members"] == [], "remove member")
    require(m.invoke("group remove", {"name": "K", "members": "c"})["status"] == "error" and
            m.invoke("group remove", {"name": "missing", "members": "c"})["status"] == "error",
            "removing a non-member or from a missing group fails")
    ok(m.invoke("group add", {"name": "K", "members": "d e"}), "fill K")
    ungrouped = ok(m.invoke("group ungroup", {"members": "d"}), "ungroup")
    require(ungrouped["affected"] == ["d"] and groups()["K"]["members"] == ["e"], "ungroup")
    require(m.invoke("group ungroup", {"members": "f"})["status"] == "error",
            "ungrouping an object that has no group fails")
    emptied = ok(m.invoke("group empty", {"name": "K"}), "empty")
    require(emptied["affected"] == ["e"] and groups()["K"]["members"] == [] and
            "e" in names(), "empty keeps the group and its former members")

    # open / close / toggle only change the panel state.
    require(ok(m.invoke("group close", {"name": "H"}), "close")["open"] is False,
            "close collapses")
    require(ok(m.invoke("group toggle", {"name": "H"}), "toggle")["open"] is True and
            ok(m.invoke("group open", {"name": "H"}), "open")["open"] is True,
            "toggle and open expand")

    # excise keeps the members in the group's place; purge deletes them.
    ok(m.invoke("group add", {"name": "P", "members": "f"}), "P")
    ok(m.invoke("group add", {"name": "H", "members": "P"}), "P into H")
    require(groups()["H"]["members"] == ["G", "c", "P"], "P joins H")
    excised = ok(m.invoke("group excise", {"name": "G"}), "excise")
    require(excised["deleted"] is True and groups()["H"]["members"] == ["a", "c", "P"] and
            "G" not in groups(), f"excise puts the members in the group's place: {groups()}")
    require(len(names()) == 6, "excise deletes no object")
    purged = ok(m.invoke("group purge", {"name": "H"}), "purge")
    require(sorted(purged["affected"]) == ["a", "c", "f"] and
            names() == ["b", "d", "e"] and set(groups()) == {"H", "K"} and
            groups()["H"]["members"] == [], f"purge deletes everything below: {names()} {groups()}")
    require(ok(m.invoke("object type", {"name": "H"}), "type")["type"] == "object:group",
            "a group has its own entity type")
    require(m.invoke("load text", {"content": "1\nx\nC 0 0 0\n", "name": "H"})["status"] == "error",
            "an object cannot take a group's name")
    require(m.invoke("object rename", {"object": "d", "name": "H"})["status"] == "error",
            "renaming onto a group name is refused")
    # Deleting a member object drops it from its group.
    ok(m.invoke("group add", {"name": "H", "members": "d e"}), "refill")
    ok(m.invoke("object delete", {"object": "d"}), "delete member")
    require(groups()["H"]["members"] == ["e"], "deleting an object removes it from its group")
    ok(m.invoke("group add", {"name": "Z", "members": "e"}), "regroup")

    # Session journal: every mutating operation added here must replay to the same
    # workspace (object create/copy/extract/remove, text load, list selections,
    # pattern/selection visibility, merged and state-scoped selections). A fresh
    # runtime keeps the journal free of the temporary files used above.
    m.reset_runtime()
    base = str(load("ubq", "1ubq.pdb")["object_id"])
    two_state = str(load("mf", "multiframe.xyz")["object_id"])
    ok(m.invoke("object activate", {"id": base}), "activate")
    ok(m.invoke("object create", {"name": "cas", "selection": "name CA", "source": base}), "create")
    ok(m.invoke("object copy", {"object": "mf", "name": "mf-copy"}), "copy")
    ok(m.invoke("object extract", {"name": "waters", "selection": "resn HOH", "source": base}),
       "extract")
    ok(m.invoke("object activate", {"id": base}), "activate")
    ok(m.invoke("object remove-atoms", {"selection": "name N"}), "remove")
    text_export = ok(m.invoke("save text", {"selection": "name C", "file-format": "pdb"}), "text")
    ok(m.invoke("load text", {"content": text_export["content"], "name": "from-text"}), "text")
    ok(m.invoke("object activate", {"id": base}), "activate")
    ok(m.invoke("select", {"name": "grp", "expression": "name CA"}), "select")
    ok(m.invoke("select", {"name": "grp", "expression": "name O", "merge": "true"}), "merge")
    ok(m.invoke("select list", {"name": "byindex", "values": "1,3,5-7"}), "list")
    ok(m.invoke("select", {"expression": "name C"}), "default name")
    ok(m.invoke("object disable", {"pattern": "mf*"}), "disable pattern")
    ok(m.invoke("object disable", {"selection": "resn HOH"}), "disable selection")
    ok(m.invoke("group add", {"name": "parts", "members": "cas waters"}), "group add")
    ok(m.invoke("group add", {"name": "all-parts", "members": "parts mf*"}), "nested group")
    ok(m.invoke("group close", {"name": "parts"}), "group close")
    for scene_name in ("x", "y", "z10", "z2"):
        ok(m.invoke("scene store", {"name": scene_name}), f"store {scene_name}")
    ok(m.invoke("scene order", {"names": "z10 z2", "sort": "true", "location": "top"}), "order")
    ok(m.invoke("scene rename", {"name": "x", "new-name": "x2"}), "rename scene")
    ok(m.invoke("scene recall", {"name": "y"}), "recall y")
    ok(m.invoke("scene next"), "next")
    ok(m.invoke("scene update"), "update")
    ok(m.invoke("load text", {"content": periodic_pdb(drift), "name": "box", "file-format": "pdb"}),
       "load periodic")
    ok(m.invoke("pbc mode", {"mode": "unwrap", "object": "box"}), "unwrap box")
    ok(m.invoke("object activate", {"id": base}), "activate base again")

    def workspace_state():
        rows = ok(m.invoke("object list"), "list")["table"]["rows"]
        objects = {row[1]: (row[5], row[3], row[6]) for row in rows}
        selections = {}
        for name in ("grp", "byindex", "sele"):
            counted = m.invoke("object count", {"object": "ubq", "selection": f"@{name}"})
            selections[name] = counted["data"]["atom_count"] if counted["status"] == "ok" else None
        listing = {g["name"]: (g["members"], g["parent"], g["open"])
                   for g in ok(m.invoke("group list"), "group list")["groups"]}
        scenes = [(item["name"], item["current"])
                  for item in ok(m.invoke("scene list"), "scene list")["scenes"]]
        modes = {name: ok(m.invoke("pbc mode", {"mode": "status", "object": name}),
                          "pbc status")["mode"] for name in objects}
        return objects, selections, listing, scenes, modes

    expected_state = workspace_state()
    require(expected_state[0]["ubq"][0] == 602 - 76 and expected_state[0]["waters"][0] == 58 and
            expected_state[0]["cas"][0] == 76 and expected_state[0]["mf-copy"][2] == 2 and
            expected_state[1]["grp"] is not None and expected_state[1]["byindex"] == 5 and
            expected_state[1]["sele"] is not None,
            f"the scenario produced the intended workspace: {expected_state}")
    with tempfile.TemporaryDirectory() as directory:
        session_path = pathlib.Path(directory) / "closure.mssession"
        ok(m.invoke("session save", {"path": str(session_path)}), "session save")
        ok(m.invoke("object delete", {"object": "cas"}), "scramble objects")
        ok(m.invoke("object enable", {"pattern": "*"}), "scramble visibility")
        require(workspace_state() != expected_state, "the workspace was changed before restore")
        ok(m.invoke("session load", {"path": str(session_path)}), "session load")
        restored_state = workspace_state()
        require(restored_state == expected_state,
                f"the session journal replays to the same workspace:\n{restored_state}\n{expected_state}")
    # mmCIF declares helices and strands (_struct_conf HELX_*, _struct_sheet_range) like PDB HELIX/SHEET;
    # a TURN_P row is not a helix, and a category with a single row is written without a loop.
    for fixture_name, helix_residues in (("ss_ranges_loop.cif", 2), ("ss_ranges_single.cif", 3)):
        loaded = load(fixture_name.replace(".", "_"), fixture_name)["object_id"]
        counts = {sel: ok(m.invoke("object count", {"object": str(loaded), "selection": sel}),
                          f"count {sel}")["atom_count"] for sel in ("ss H", "ss S")}
        require(counts == {"ss H": helix_residues, "ss S": 2},
                f"{fixture_name}: declared secondary structure {counts}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
