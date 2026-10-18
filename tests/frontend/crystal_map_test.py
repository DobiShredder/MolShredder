#!/usr/bin/env python3
"""SEQ-319 S2: X-PLOR/CNS, BRIX/DSN6, Delphi PHI, GRD, AVS field and ACNT maps.

The fixtures are synthetic (see tests/io/fixtures/maps/generate_maps.py); the
expected grids were cross-checked with Open-Source PyMOL 3.1.0.
"""

import importlib
import math
import pathlib
import re
import shutil
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


def val(i: int, j: int, k: int) -> float:
    return round(math.sin(0.7 * i) + 0.5 * math.cos(0.4 * j) + 0.1 * k + 0.3 * i * j / 10, 4)


def parse_dx(path: pathlib.Path):
    lines = path.read_text().splitlines()
    counts = [int(x) for x in re.search(r"counts\s+(\d+)\s+(\d+)\s+(\d+)", lines[1]).groups()]
    origin = [float(x) for x in next(l for l in lines if l.startswith("origin")).split()[1:4]]
    deltas = [[float(x) for x in l.split()[1:4]] for l in lines if l.startswith("delta")]
    start = next(i for i, l in enumerate(lines) if l.startswith("object 3")) + 1
    data = []
    for l in lines[start:]:
        if l.startswith(("attribute", "object", "component")):
            break
        data += [float(x) for x in l.split()]
    nx, ny, nz = counts
    require(len(data) == nx * ny * nz, "DX payload size")
    return counts, origin, deltas, [[[data[(i * ny + j) * nz + k] for k in range(nz)] for j in range(ny)] for i in range(nx)]


def close(a, b, tolerance=1.0e-5):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def main() -> int:
    module_path = pathlib.Path(sys.argv[1]).resolve()
    maps = pathlib.Path(sys.argv[2]).resolve() / "maps"
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")

    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)

        def load(name: str, fixture: str, file_format: str = "auto", source: pathlib.Path | None = None):
            path = (source or maps) / fixture
            ok(m.invoke("volume load", {"path": str(path), "name": name, "file-format": file_format}), f"load {fixture}")
            out = root / f"{name}.dx"
            ok(m.invoke("volume save", {"path": str(out), "overwrite": "true"}), f"save {name}")
            return parse_dx(out)

        def check_function(counts, data, offset=(0, 0, 0), tolerance=1.0e-5):
            worst = 0.0
            for i in range(counts[0]):
                for j in range(counts[1]):
                    for k in range(counts[2]):
                        worst = max(worst, abs(data[i][j][k] - val(i + offset[0], j + offset[1], k + offset[2])))
            require(worst < tolerance, f"values follow the analytic function (worst {worst})")

        # X-PLOR/CNS: orthogonal and triclinic cells; origin = cell * min/div, deltas = cell / div
        counts, origin, deltas, data = load("xo", "ortho.xplor")
        require(counts == [6, 6, 7] and close(origin, [-2.0, 0.0, 4.0]) and
                close(deltas[0] + deltas[1] + deltas[2], [2, 0, 0, 0, 2, 0, 0, 0, 2]), f"xplor grid {counts} {origin} {deltas}")
        check_function(counts, data)
        counts, origin, deltas, data = load("xt", "tri.xplor")
        require(counts == [6, 6, 7] and close(origin, [-1.651377, 0.76678, 3.910308], 1.0e-5), f"triclinic origin {origin}")
        require(close(deltas[1], [-0.347296, 1.969616, 0.0], 1.0e-5) and close(deltas[2], [0.174311, 0.38339, 1.955154], 1.0e-5),
                f"triclinic cell vectors {deltas}")
        check_function(counts, data)
        # `.map` is shared with CCP4: a text map with a title block reads as X-PLOR
        shutil.copy(maps / "ortho.xplor", root / "shared.map")
        counts, _, _, data = load("xm", "shared.map", "map", root)
        check_function(counts, data)

        # BRIX (ASCII header) and DSN6 (binary): identical bricks, normalized to mean 0 / sigma 1
        counts, origin, deltas, brix = load("br", "ascii.brix")
        require(counts == [11, 10, 9] and close(origin, [-4.0, 6.0, 2.0]) and close(deltas[2], [0, 0, 2.0]),
                f"brix grid {counts} {origin}")
        flat = [v for plane in brix for row in plane for v in row]
        mean = sum(flat) / len(flat)
        sigma = math.sqrt(sum((v - mean) ** 2 for v in flat) / (len(flat) - 1))
        require(abs(mean) < 1.0e-5 and abs(sigma - 1.0) < 1.0e-5, f"O maps are normalized: {mean} {sigma}")
        _, _, _, dsn6 = load("ds", "binary.dsn6")
        require(all(abs(a - b) < 1.0e-6 for p, q in zip(brix, dsn6) for r, s in zip(p, q) for a, b in zip(r, s)),
                "the DSN6 and ASCII BRIX encodings of the same bricks agree")
        # the raw brick values reproduce the generator's formula before normalization
        raw = [max(0, min(255, round(100 + 60 * math.sin(0.5 * x) + 30 * math.cos(0.3 * y) + 5 * z)))
               for x in range(11) for y in range(10) for z in range(9)]
        raw = [(v - 10.0) / 0.5 for v in raw]
        rmean = sum(raw) / len(raw)
        rsigma = math.sqrt(sum((v - rmean) ** 2 for v in raw) / (len(raw) - 1))
        require(abs(brix[3][4][5] - (raw[(3 * 10 + 4) * 9 + 5] - rmean) / rsigma) < 1.0e-5, "brick decode")

        # Delphi PHI: centered grid, spacing 1 / scale
        counts, origin, deltas, data = load("ph", "little.phi")
        require(counts == [5, 5, 5] and close(origin, [0.5, -3.0, 2.25]) and close(deltas[0], [0.5, 0, 0]),
                f"phi grid {counts} {origin}")
        check_function(counts, data)

        # GRD (ASCII)
        counts, origin, deltas, data = load("gr", "ortho.grd")
        require(counts == [6, 5, 4] and close(origin, [-4.0, 0.0, 4.666667], 1.0e-5) and
                close(deltas[2], [0, 0, 2.333333], 1.0e-5), f"grd grid {counts} {origin} {deltas}")
        check_function(counts, data)

        # AVS field
        counts, origin, deltas, data = load("fl", "le.fld")
        require(counts == [5, 4, 3] and close(origin, [-1.5, 2.0, 0.5]) and close(deltas[2], [0, 0, 1.5]),
                f"fld grid {counts} {origin}")
        check_function(counts, data)

        # ACNT: header lines list y, x, z; z is the slowest axis, y the fastest
        counts, origin, deltas, data = load("ac", "t.acnt")
        require(counts == [4, 3, 5] and close(origin, [-1.0, 2.0, 3.0]) and close(deltas[0], [0.25, 0, 0]) and
                close(deltas[1], [0, 0.5, 0]), f"acnt grid {counts} {origin}")
        check_function(counts, data)

        # damaged files are rejected with a format-specific message
        for fixture, label, mutate in (
                ("ortho.xplor", "xplor", lambda b: b[: len(b) // 2]),
                ("ascii.brix", "brix", lambda b: b[:600]),
                ("binary.dsn6", "dsn6", lambda b: b[:700]),
                ("little.phi", "phi", lambda b: b[:200]),
                ("ortho.grd", "grd", lambda b: b[:300]),
                ("le.fld", "fld", lambda b: b[:200]),
                ("t.acnt", "acnt", lambda b: b[:200])):
            bad = root / f"bad_{fixture}"
            bad.write_bytes(mutate((maps / fixture).read_bytes()))
            format_name = "brix" if fixture.endswith(("brix", "dsn6")) else fixture.rsplit(".", 1)[1]
            message = fails(m.invoke("volume load", {"path": str(bad), "name": "bad", "file-format": format_name}), label)
            require(label[:3].upper() in message.upper() or "map" in message.lower(), f"{label}: {message}")
        huge = root / "huge.xplor"
        text = (maps / "ortho.xplor").read_text().replace("      10      -1       4      12       0       5      14       2       8",
                                                          "      10  -90000   90000      12  -90000   90000      14       2       8", 1)
        huge.write_text(text)
        require("larger" in fails(m.invoke("volume load", {"path": str(huge), "name": "bad2", "file-format": "xplor"}), "huge"),
                "an oversized grid is refused before allocation")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
