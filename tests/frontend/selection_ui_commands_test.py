#!/usr/bin/env python3
"""SEQ-330: commands emitted by the object/selection rows of the new shell.

Covers `select delete`, PML names that refer to a selection made in an earlier
`pml run`, and PML enable/disable.
"""

import importlib
import pathlib
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def ok(result: dict, what: str) -> dict:
    require(result["status"] == "ok", f"{what} failed: {result.get('error')}")
    return result["data"]


def pml(m, text: str) -> dict:
    return ok(m.invoke("pml run", {"text": text}), text)


def main() -> int:
    module_path = pathlib.Path(sys.argv[1]).resolve()
    fixtures = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")
    ok(m.invoke("load", {"path": str(fixtures / "1ubq.pdb")}), "load")

    # a selection from an earlier run is usable by later PML runs
    pml(m, "select sele, resi 10-20")
    for text in ("zoom sele", "center sele", "orient sele", "show sticks, sele", "hide lines, sele",
                 "color red, sele", "label sele, name"):
        pml(m, text)

    # enable / disable by object name, selection name and "all"
    objects = ok(m.invoke("object list"), "object list")["table"]["rows"]
    name = objects[0][1] if len(objects) and len(objects[0]) > 1 else None
    pml(m, "disable all")
    pml(m, "enable all")
    if name:
        pml(m, f"disable {name}")
        pml(m, f"enable {name}")

    # delete removes the selection, a second delete reports an error
    pml(m, "delete sele")
    again = m.invoke("select delete", {"name": "sele"})
    require(again["status"] == "error", "deleting a missing selection must fail")
    after = m.invoke("pml run", {"text": "zoom sele"})
    require(after["status"] == "error", "a deleted selection is no longer a name")

    # canonical command form
    ok(m.invoke("select", {"name": "keep", "expression": "resi 1-5"}), "select keep")
    pml(m, "zoom keep")
    ok(m.invoke("select delete", {"name": "keep"}), "select delete keep")
    return 0


if __name__ == "__main__":
    sys.exit(main())
