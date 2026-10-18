#!/usr/bin/env python3
"""SEQ-321 S6: `volume combine` voxel-wise operations on volumes that share one grid."""

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


def fails(result: dict, what: str) -> str:
    require(result["status"] == "error", f"{what} should have failed")
    return result["error"]["message"]


def dx(values, counts=(2, 2, 2), origin=(0.0, 0.0, 0.0), delta=1.0) -> str:
    n = counts[0] * counts[1] * counts[2]
    body = "\n".join(" ".join(str(v) for v in values[i:i + 3]) for i in range(0, n, 3))
    return (f"object 1 class gridpositions counts {counts[0]} {counts[1]} {counts[2]}\n"
            f"origin {origin[0]} {origin[1]} {origin[2]}\ndelta {delta} 0 0\ndelta 0 {delta} 0\ndelta 0 0 {delta}\n"
            f"object 2 class gridconnections counts {counts[0]} {counts[1]} {counts[2]}\n"
            f"object 3 class array type double rank 0 items {n} data follows\n{body}\n"
            'attribute "dep" string "positions"\nobject "f" class field\n'
            'component "positions" value 1\ncomponent "connections" value 2\ncomponent "data" value 3\nend\n')


def main() -> int:
    module_path = pathlib.Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        a = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0]
        b = [8.0, 7.0, 6.0, 5.0, 4.0, 3.0, 2.0, 1.0]
        c = [0.0, 0.0, 1.0, 1.0, 2.0, 2.0, 3.0, 3.0]
        for name, values, kwargs in (("a", a, {}), ("b", b, {}), ("c", c, {}), ("shifted", a, {"origin": (0.5, 0, 0)}),
                                     ("coarse", a, {"delta": 2.0}), ("big", list(range(12)), {"counts": (2, 2, 3)})):
            path = root / f"{name}.dx"
            path.write_text(dx(values, **kwargs))
            ok(m.invoke("volume load", {"path": str(path), "name": name, "file-format": "opendx"}), f"load {name}")

        expected = {
            "sum": [x + y for x, y in zip(a, b)],
            "difference": [x - y for x, y in zip(a, b)],
            "average": [(x + y) / 2 for x, y in zip(a, b)],
            "product": [x * y for x, y in zip(a, b)],
            "maximum": [max(x, y) for x, y in zip(a, b)],
            "minimum": [min(x, y) for x, y in zip(a, b)],
        }
        for mode, values in expected.items():
            data = ok(m.invoke("volume combine", {"volumes": "a;b", "name": f"r_{mode}", "mode": mode}), mode)
            require(data["minimum"] == min(values) and data["maximum"] == max(values) and data["value_count"] == 8, f"{mode}: {data}")
        # three sources: average of a, b, c and difference a - b - c
        three = ok(m.invoke("volume combine", {"volumes": "a;b;c", "name": "avg3", "mode": "average"}), "average of three")
        values3 = [(x + y + z) / 3 for x, y, z in zip(a, b, c)]
        require(abs(three["minimum"] - min(values3)) < 1e-12 and abs(three["maximum"] - max(values3)) < 1e-12, f"three-way average: {three}")
        diff3 = ok(m.invoke("volume combine", {"volumes": "a;b;c", "name": "diff3", "mode": "difference"}), "difference of three")
        values_d = [x - y - z for x, y, z in zip(a, b, c)]
        require(diff3["minimum"] == min(values_d) and diff3["maximum"] == max(values_d), "first minus the rest")
        names = {row[1] for row in ok(m.invoke("volume list"), "list")["table"]["rows"]}
        require({"r_sum", "r_difference", "avg3", "diff3"} <= names, f"combined volumes are listed: {names}")

        # errors
        require("do not share one grid" in fails(m.invoke("volume combine", {"volumes": "a;shifted", "name": "bad1"}), "origin"), "different origin")
        require("do not share one grid" in fails(m.invoke("volume combine", {"volumes": "a;coarse", "name": "bad2"}), "spacing"), "different spacing")
        require("do not share one grid" in fails(m.invoke("volume combine", {"volumes": "a;big", "name": "bad3"}), "shape"), "different shape")
        require("no volume named" in fails(m.invoke("volume combine", {"volumes": "a;nope", "name": "bad4"}), "missing"), "unknown volume")
        require("at least two" in fails(m.invoke("volume combine", {"volumes": "a", "name": "bad5"}), "single"), "needs two volumes")
        require("already exists" in fails(m.invoke("volume combine", {"volumes": "a;b", "name": "a"}), "duplicate name"), "result name must be unique")
        require("empty batch item" in fails(m.invoke("volume combine", {"volumes": "a;;b", "name": "bad6"}), "empty item"), "empty list item")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
