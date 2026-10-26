#!/usr/bin/env python3
"""The first molecule of an empty workspace is framed; later ones leave the camera alone."""

import importlib
import pathlib
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def ok(result: dict, what: str) -> dict:
    require(result["status"] == "ok", f"{what} failed: {result.get('error')}")
    return result["data"]


def main() -> int:
    require(len(sys.argv) == 3, "expected Python module and fixture directory")
    sys.path.insert(0, str(pathlib.Path(sys.argv[1]).resolve().parent))
    fixtures = pathlib.Path(sys.argv[2]).resolve()
    m = importlib.import_module("molshredder")

    def camera():
        return ok(m.invoke("view get"), "view get")["camera"]

    ok(m.invoke("load", {"path": str(fixtures / "1ubq.pdb"), "name": "first"}), "load first")
    framed = camera()
    # The camera target is the middle of the molecule, not the origin (1ubq sits far from it).
    target = framed["target"]
    require(abs(target[0]) + abs(target[1]) + abs(target[2]) > 1.0, f"camera still looks at the origin: {target}")
    ok(m.invoke("view reset"), "view reset")
    require(camera() == framed, "loading the first molecule already framed the scene (same as view reset)")

    # A user move, then a second structure: the camera stays where it is.
    ok(m.invoke("view zoom", {"buffer": "10", "duration": "0"}), "user zoom")
    moved = camera()
    require(moved != framed, "the zoom changed the camera")
    ok(m.invoke("load", {"path": str(fixtures / "chemistry.mol2"), "name": "second"}), "load second")
    require(camera() == moved, "a later load must keep the camera focus")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
