#!/usr/bin/env python3
"""Cartoon and ribbon geometry parity with PyMOL 3.1.0 VRML exports (Q-019 a).

Arguments: geometry probe, 1ubq.pdb, pymol310_backbone.json.gz.
Meshes are compared as surfaces: every vertex of one mesh must lie close to
the other mesh (point-to-triangle distance) in both directions.
"""

import gzip
import json
import math
import pathlib
import sys
import tempfile

from geometry_parity_test import require, run, same, segments

P95_LIMIT = 0.01
MAX_LIMIT = 0.05
COLOR_SHARE = 0.99


def point_triangle(p, a, b, c) -> float:
    """Distance from p to triangle abc (closest-feature regions)."""
    sub = lambda u, v: (u[0] - v[0], u[1] - v[1], u[2] - v[2])
    dot = lambda u, v: u[0] * v[0] + u[1] * v[1] + u[2] * v[2]
    ab, ac, ap = sub(b, a), sub(c, a), sub(p, a)
    d1, d2 = dot(ab, ap), dot(ac, ap)
    if d1 <= 0 and d2 <= 0:
        return math.dist(p, a)
    bp = sub(p, b)
    d3, d4 = dot(ab, bp), dot(ac, bp)
    if d3 >= 0 and d4 <= d3:
        return math.dist(p, b)
    cp = sub(p, c)
    d5, d6 = dot(ab, cp), dot(ac, cp)
    if d6 >= 0 and d5 <= d6:
        return math.dist(p, c)
    vc = d1 * d4 - d3 * d2
    vb = d5 * d2 - d1 * d6
    va = d3 * d6 - d5 * d4
    try:
        if vc <= 0 and d1 >= 0 and d3 <= 0:
            v = d1 / (d1 - d3)
            return math.dist(p, tuple(a[i] + v * ab[i] for i in range(3)))
        if vb <= 0 and d2 >= 0 and d6 <= 0:
            w = d2 / (d2 - d6)
            return math.dist(p, tuple(a[i] + w * ac[i] for i in range(3)))
        if va <= 0 and (d4 - d3) >= 0 and (d5 - d6) >= 0:
            w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
            return math.dist(p, tuple(b[i] + w * (c[i] - b[i]) for i in range(3)))
        denominator = 1.0 / (va + vb + vc)
        v, w = vb * denominator, vc * denominator
        return math.dist(p, tuple(a[i] + ab[i] * v + ac[i] * w for i in range(3)))
    except ZeroDivisionError:
        return min(math.dist(p, a), math.dist(p, b), math.dist(p, c))


def distances(points: list, triangles: list, cell: float = 2.0) -> list:
    grid = {}
    for triangle in triangles:
        low = [min(v[i] for v in triangle) for i in range(3)]
        high = [max(v[i] for v in triangle) for i in range(3)]
        for x in range(math.floor(low[0] / cell), math.floor(high[0] / cell) + 1):
            for y in range(math.floor(low[1] / cell), math.floor(high[1] / cell) + 1):
                for z in range(math.floor(low[2] / cell), math.floor(high[2] / cell) + 1):
                    grid.setdefault((x, y, z), []).append(triangle)
    result = []
    for point in points:
        key = tuple(math.floor(c / cell) for c in point)
        best = math.inf
        for reach in range(4):
            for dx in range(-reach, reach + 1):
                for dy in range(-reach, reach + 1):
                    for dz in range(-reach, reach + 1):
                        if max(abs(dx), abs(dy), abs(dz)) != reach:
                            continue
                        for triangle in grid.get((key[0] + dx, key[1] + dy, key[2] + dz), ()):
                            best = min(best, point_triangle(point, *triangle))
            if best <= reach * cell:
                break
        result.append(best)
    return sorted(result)


def check_mesh(name: str, actual: dict, expected: dict) -> None:
    vertices = expected["vertices"]
    points = [tuple(vertices[i:i + 3]) for i in range(0, len(vertices), 3)]
    colors = [expected["palette"][index] for index in expected["colors"]]
    reference = [tuple(points[expected["triangles"][i + k]] for k in range(3))
                 for i in range(0, len(expected["triangles"]), 3)]
    mesh_points = [tuple(vertex[0:3]) for vertex in actual["mesh_vertices"]]
    mesh = [tuple(mesh_points[index] for index in triangle)
            for triangle in actual["mesh_triangles"]]
    require(mesh, f"{name}: MolShredder produced no mesh")
    require(abs(len(mesh) - len(reference)) <= 0.01 * len(reference),
            f"{name}: triangle count {len(mesh)} vs PyMOL {len(reference)}")
    for label, sample, surface in (("MolShredder->PyMOL", mesh_points, reference),
                                   ("PyMOL->MolShredder", points, mesh)):
        measured = distances(sample, surface)
        p95 = measured[int(len(measured) * 0.95)]
        require(p95 <= P95_LIMIT and measured[-1] <= MAX_LIMIT,
                f"{name}: {label} surface distance p95 {p95:.3f} max {measured[-1]:.3f}")
    # Vertex colors: nearest PyMOL vertex within 0.05 A has the same color.
    cell = {}
    for point, color in zip(points, colors):
        cell.setdefault(tuple(round(c, 1) for c in point), []).append((point, color))
    matched = 0
    for vertex in actual["mesh_vertices"]:
        key = tuple(round(c, 1) for c in vertex[0:3])
        best, best_color = math.inf, None
        for dx in (-0.1, 0.0, 0.1):
            for dy in (-0.1, 0.0, 0.1):
                for dz in (-0.1, 0.0, 0.1):
                    for point, color in cell.get((round(key[0] + dx, 1), round(key[1] + dy, 1),
                                                  round(key[2] + dz, 1)), ()):
                        distance = math.dist(point, vertex[0:3])
                        if distance < best:
                            best, best_color = distance, color
        if best <= 0.05 and all(abs(best_color[i] - vertex[3 + i]) <= 0.01 for i in range(3)):
            matched += 1
    require(matched >= COLOR_SHARE * len(actual["mesh_vertices"]),
            f"{name}: only {matched}/{len(actual['mesh_vertices'])} vertex colors match PyMOL")


def main() -> int:
    require(len(sys.argv) == 4, "expected probe, 1ubq.pdb and pymol310_backbone.json.gz")
    probe, ubq, capture_path = (pathlib.Path(arg).resolve() for arg in sys.argv[1:])
    with gzip.open(capture_path, "rt") as handle:
        cases = json.load(handle)["cases"]
    # 1ubq without residues 30-32 (header kept) for gap handling.
    gapped = pathlib.Path(tempfile.mkdtemp()) / "1ubq_gap.pdb"
    with open(ubq) as source, open(gapped, "w") as target:
        for line in source:
            if line.startswith(("ATOM", "HETATM", "ANISOU")) and 30 <= int(line[22:26]) <= 32:
                continue
            target.write(line)

    def base(path: pathlib.Path = ubq, representation: str = "cartoon") -> list:
        return [("load", {"file-format": "pdb", "name": "m", "path": str(path)}),
                ("hide", {"representation": "everything", "selection": "all"}),
                ("color", {"color": "atomic", "selection": "all"}),
                ("color", {"color": "red", "selection": "resi 5"}),
                ("show", {"representation": representation, "selection": "all"})]

    def cartoon(kind: str, selection: str) -> tuple:
        return ("cartoon", {"type": kind, "selection": selection})

    def setting(name: str, value: str) -> tuple:
        return ("setting set", {"name": name, "value": value})

    meshes = {
        "cartoon_default": base(),
        "cartoon_loop": base() + [cartoon("loop", "resi 20-40")],
        "cartoon_rectangle": base() + [cartoon("rectangle", "resi 1-17")],
        "cartoon_tube": base() + [cartoon("tube", "resi 50-60")],
        "cartoon_automatic": base() + [cartoon("loop", "all"), cartoon("automatic", "all")],
        "cartoon_skip": base() + [cartoon("skip", "resi 10-12")],
        "cartoon_rect_size": base() + [setting("cartoon_rect_length", "2.0"),
                                       setting("cartoon_rect_width", "0.6")],
        "cartoon_sampling_3": base() + [setting("cartoon_sampling", "3")],
        "cartoon_hidden_gap": base() + [("hide", {"representation": "cartoon",
                                                  "selection": "resi 30-32"})],
        "cartoon_dash_gap": base(gapped),
        "cartoon_gap_cutoff_2": base(gapped) + [setting("cartoon_gap_cutoff", "2")],
    }
    for name, script in meshes.items():
        expected = cases[name]
        if "same_as" in expected:
            expected = cases[expected["same_as"]]
        check_mesh(name, run(probe, script), expected)

    ribbons = {
        "ribbon_default": base(representation="ribbon"),
        "ribbon_sampling_4": base(representation="ribbon") + [setting("ribbon_sampling", "4")],
        "ribbon_width_5": base(representation="ribbon") + [setting("ribbon_width", "5")],
    }
    widths = {}
    for name, script in ribbons.items():
        actual = run(probe, script)
        lines = [item[0:6] + [0.0] + item[7:10] for item in actual["lines"]]
        require(same(segments(lines, False), segments(cases[name]["cylinders"], False)),
                f"{name}: ribbon segments differ from the PyMOL export")
        widths[name] = ({item[6] for item in actual["lines"]},
                        {item[6] for item in cases[name]["cylinders"]})
    # Ribbon width is a pixel width; PyMOL's ray radius scales the same way.
    (default_pixels, default_radius) = widths["ribbon_default"]
    (wide_pixels, wide_radius) = widths["ribbon_width_5"]
    require(default_pixels == {3.0} and wide_pixels == {5.0} and
            abs(max(wide_radius) / max(default_radius) - 5.0 / 3.0) < 0.01,
            "ribbon_width must scale the line width like PyMOL")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
