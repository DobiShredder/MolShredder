#!/usr/bin/env python3
"""Map contour geometry parity with PyMOL 3.1.0 VRML exports (Q-019 a, Q-026 b).

Arguments: geometry probe, contour.pdb, contour.ccp4, pymol310_contour.json.
Triangles, mesh segments and dots (levels 0.5 and 2.0) and carve/buffer restricted
contours must match as sets within the tolerance.
"""

import itertools
import json
import math
import pathlib
import sys

from geometry_parity_test import TOLERANCE, require, run

CELL = 0.05


def cell(point) -> tuple:
    return tuple(math.floor(c / CELL) for c in point)


def centroid(points) -> tuple:
    return tuple(sum(p[i] for p in points) / len(points) for i in range(3))


def close_items(a, b) -> bool:
    return any(all(math.dist(p, q) <= TOLERANCE for p, q in zip(a, perm))
               for perm in itertools.permutations(b))


def same_sets(actual: list, expected: list) -> bool:
    """One-to-one match of point tuples (dots, segments, triangles)."""
    if len(actual) != len(expected):
        return False
    index = {}
    for position, item in enumerate(expected):
        index.setdefault(cell(centroid(item)), []).append(position)
    used = set()
    for item in actual:
        base = cell(centroid(item))
        found = None
        for delta in itertools.product((-1, 0, 1), repeat=3):
            key = tuple(base[i] + delta[i] for i in range(3))
            for position in index.get(key, ()):
                if position not in used and close_items(item, expected[position]):
                    found = position
                    break
            if found is not None:
                break
        if found is None:
            return False
        used.add(found)
    return True


def main() -> int:
    require(len(sys.argv) == 5, "expected probe, contour.pdb, contour.ccp4 and capture")
    probe, structure, density, capture = (pathlib.Path(arg).resolve() for arg in sys.argv[1:])
    cases = json.loads(capture.read_text())["cases"]
    for name, case in cases.items():
        level = str(case["level"])

        arguments = {"name": "c1", "map": "dens", "level": level}
        if "selection" in case:
            arguments["selection"] = case["selection"]
        for restriction in ("carve", "buffer"):
            if name.startswith(restriction):
                arguments[restriction] = name.split("_", 1)[1]

        def contour(mode: str) -> dict:
            return run(probe, [
                ("load", {"file-format": "pdb", "name": "lig", "path": str(structure)}),
                # A new object shows itself by default; the parity data holds only the contour.
                ("hide", {"representation": "everything", "selection": "all"}),
                ("volume load", {"name": "dens", "path": str(density)}),
                ("volume contour", {**arguments, "mode": mode}),
            ])

        surface = contour("surface")
        vertices = [tuple(v[0:3]) for v in surface["mesh_vertices"]]
        triangles = [[vertices[i] for i in triangle] for triangle in surface["mesh_triangles"]]
        require(same_sets(triangles, [[tuple(v) for v in t] for t in case["surface"]]),
                f"{name}: isosurface triangles differ from PyMOL ({len(triangles)} vs {len(case['surface'])})")
        mesh = contour("mesh")
        segments = [[tuple(s[0:3]), tuple(s[3:6])] for s in mesh["lines"]]
        require(same_sets(segments, [[tuple(p) for p in s] for s in case["mesh"]]),
                f"{name}: isomesh segments differ from PyMOL ({len(segments)} vs {len(case['mesh'])})")
        if "dots" in case:
            dots = [[tuple(s[0:3])] for s in contour("dot")["spheres"]]
            require(same_sets(dots, [[tuple(d)] for d in case["dots"]]),
                    f"{name}: isodot points differ from PyMOL ({len(dots)} vs {len(case['dots'])})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
