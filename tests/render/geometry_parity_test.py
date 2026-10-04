#!/usr/bin/env python3
"""Primitive-level geometry parity with PyMOL 3.1.0 exports (Q-019 a).

Arguments: geometry probe, 1ubq.pdb, benzene.mol, pymol310_primitives.json.
"""

import json
import math
import pathlib
import subprocess
import sys
import tempfile

TOLERANCE = 5.0e-3
BOND_SETTINGS = {"stick_color", "line_color", "stick_radius", "line_width",
                 "stick_transparency", "valence"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run(probe: pathlib.Path, script: list) -> dict:
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as handle:
        for name, arguments in script:
            handle.write("\t".join([name] + [f"{k}={v}" for k, v in arguments.items()]) + "\n")
        path = handle.name
    completed = subprocess.run([str(probe), path], check=True, capture_output=True, text=True)
    pathlib.Path(path).unlink()
    return json.loads(completed.stdout)


def setting(name: str, value: str, selection: str | None = None) -> tuple:
    arguments = {"name": name, "value": value}
    if selection is not None:
        arguments.update({"scope": "bond" if name in BOND_SETTINGS else "atom",
                          "selection": selection})
    return ("setting set", arguments)


def key(point) -> tuple:
    return tuple(round(value, 3) for value in point)


def joint(point) -> tuple:
    """Coarse endpoint key so float and double exports meet at shared joints."""
    return tuple(round(value, 2) for value in point)


def segments(items: list, use_radius: bool) -> list:
    """Merges collinear same-style segments sharing an endpoint."""
    parts = [[tuple(item[0:3]), tuple(item[3:6]), round(item[6], 3) if use_radius else 0.0,
              tuple(round(c, 2) for c in item[7:10])] for item in items]
    changed = True
    while changed:
        changed = False
        ends = {}
        for index, (a, b, radius, color) in enumerate(parts):
            for point in (a, b):
                ends.setdefault((joint(point), radius, color), []).append(index)
        for (point, radius, color), indices in ends.items():
            if len(indices) != 2:
                continue
            first, second = (parts[i] for i in indices)
            shared = first[0] if joint(first[0]) == point else first[1]
            far1 = first[1] if joint(first[0]) == point else first[0]
            far2 = second[1] if joint(second[0]) == point else second[0]
            u = [far1[i] - shared[i] for i in range(3)]
            v = [far2[i] - shared[i] for i in range(3)]
            nu, nv = math.hypot(*u), math.hypot(*v)
            if nu < 1e-9 or nv < 1e-9:
                continue
            if sum(u[i] * v[i] for i in range(3)) / (nu * nv) < -0.99999:
                parts = [p for i, p in enumerate(parts) if i not in indices] + [[far1, far2, radius, color]]
                changed = True
                break
    out = []
    for a, b, radius, color in parts:
        a, b = sorted((key(a), key(b)))
        out.append((a, b, radius, color))
    return sorted(out)


def spheres(items: list) -> list:
    return sorted({(key(item[0:3]), round(item[3], 3), tuple(round(c, 2) for c in item[4:7]))
                   for item in items})


def same(left: list, right: list) -> bool:
    if len(left) != len(right):
        return False
    flat = lambda item: [v for part in item for v in (part if isinstance(part, tuple) else (part,))]
    return all(all(abs(p - q) <= TOLERANCE for p, q in zip(flat(a), flat(b))) for a, b in zip(left, right))


def main() -> int:
    require(len(sys.argv) == 5, "expected probe, 1ubq.pdb, benzene.mol and capture JSON")
    probe, ubq, benzene, capture_path = (pathlib.Path(arg).resolve() for arg in sys.argv[1:])
    capture = json.loads(capture_path.read_text())
    cases = capture["cases"]

    def protein(representation: str, selection: str = "resi 1-10") -> list:
        # PyMOL infers bonds and template bond orders on load; MolShredder
        # perceives connectivity explicitly and the capture disables valence.
        return [("load", {"file-format": "pdb", "name": "m", "path": str(ubq)}),
                ("object perceive-chemistry", {"apply": "true"}),
                ("hide", {"representation": "everything", "selection": "all"}),
                ("color", {"color": "atomic", "selection": "all"}),
                ("show", {"representation": representation, "selection": selection}),
                setting("valence", "false")]

    def ring(representation: str, zero: bool = False) -> list:
        script = [("load", {"file-format": "mol", "name": "bz", "path": str(benzene)})]
        if zero:
            script.append(("edit bond-order", {"bond-id": "1", "order": "zero",
                                               "expected-topology-version": "{topology_version}",
                                               "expected-coordinate-source-revision":
                                                   "{coordinate_source_revision}"}))
        return script + [("hide", {"representation": "everything", "selection": "all"}),
                         ("color", {"color": "atomic", "selection": "all"}),
                         ("show", {"representation": representation, "selection": "all"})]

    scripts = {
        "spheres_default": protein("spheres"),
        "spheres_scale": protein("spheres") + [setting("sphere_scale", "0.5", "resi 1-5")],
        "spheres_color": protein("spheres") + [setting("sphere_scale", "0.5", "resi 1-5"),
                                               setting("sphere_color", "red", "resi 2")],
        "sphere_use_shader_0": protein("spheres") + [setting("sphere_use_shader", "false")],
        "sticks_default": protein("sticks"),
        "sticks_radius": protein("sticks") + [setting("stick_radius", "0.4")],
        "sticks_color": protein("sticks") + [setting("stick_radius", "0.4"),
                                             setting("stick_color", "blue", "resi 2")],
        "sticks_ball": protein("sticks") + [setting("stick_ball", "true")],
        "sticks_ball_ratio": protein("sticks") + [setting("stick_ball", "true"),
                                                  setting("stick_ball_ratio", "1.5")],
        "half_bonds_off": protein("sticks", "resi 1-10 and not name CB"),
        "half_bonds_on": protein("sticks", "resi 1-10 and not name CB") + [setting("half_bonds", "true")],
        "lines_default": protein("lines"),
        "lines_color": protein("lines") + [setting("line_color", "red", "resi 2")],
        "lines_radius": protein("lines") + [setting("line_color", "red", "resi 2"),
                                            setting("line_width", "3"), setting("line_radius", "0.2")],
        "benzene_sticks_valence": ring("sticks"),
        "benzene_sticks_no_valence": ring("sticks") + [setting("valence", "false")],
        "benzene_sticks_h_scale_1": ring("sticks") + [setting("stick_h_scale", "1.0")],
        "benzene_lines_valence": ring("lines"),
    }
    for mode in (-1, 0, 1, 2, 3, 9):
        scripts[f"sphere_mode_{mode}"] = protein("spheres") + [setting("sphere_mode", str(mode))]
    for mode in (0, 1, 2):
        scripts[f"zero_order_sticks_mode_{mode}"] = ring("sticks", True) + [
            setting("valence_zero_mode", str(mode))]
        scripts[f"zero_order_lines_mode_{mode}"] = ring("lines", True) + [
            setting("valence_zero_mode", str(mode))]

    for name, script in scripts.items():
        actual = run(probe, script)
        expected = cases[name]
        lines = "lines" in name
        actual_segments = [item[0:7] + item[7:10] for item in actual["cylinders"]]
        if lines:
            actual_segments += [item[0:6] + [0.0] + item[7:10] for item in actual["lines"]]
        use_radius = not lines or name == "lines_radius"
        expected_spheres = expected["spheres"]
        if name == "zero_order_lines_mode_1":
            # PyMOL's VRML export writes each dashed-line dash as a sphere at
            # its midpoint; compare the dashes by midpoint and 0.15 A length.
            dashes = [item for item in actual["lines"]
                      if abs(math.dist(item[0:3], item[3:6]) - 0.15) < 1.0e-3]
            midpoints = sorted(tuple((d[i] + d[i + 3]) / 2.0 for i in range(3)) for d in dashes)
            reference = sorted(tuple(item[0:3]) for item in expected_spheres)
            require(len(midpoints) == len(reference) and
                    all(math.dist(a, b) <= TOLERANCE for a, b in zip(midpoints, reference)),
                    "dashed zero-order line midpoints drifted")
            actual_segments = [item for item in actual_segments
                               if abs(math.dist(item[0:3], item[3:6]) - 0.15) >= 1.0e-3]
            expected_spheres = []
        require(same(spheres(actual["spheres"]), spheres(expected_spheres)),
                f"{name}: spheres differ from the PyMOL export")
        require(same(segments(actual_segments, use_radius), segments(expected["cylinders"], use_radius)),
                f"{name}: bond segments differ from the PyMOL export")

    # line_width is a pixel width: PyMOL's ray radius scales with it.
    width = run(probe, scripts["lines_radius"][:-1])
    radii = [item[6] for item in cases["lines_width"]["cylinders"]]
    default_radii = [item[6] for item in cases["lines_default"]["cylinders"]]
    require(width["lines"] and all(abs(item[6] - 3.0) < 1e-6 for item in width["lines"]),
            "line_width must set the line pixel width")
    require(abs(max(radii) / max(default_radii) - 3.0 / 1.49) < 0.01,
            "PyMOL ray line radius must scale with line_width like the pixel width")

    # Transparency: PyMOL ray pixel alpha against a transparent background.
    alpha = capture["alpha"]
    for value, key_name in (("0.5", "sphere_transparency_0.5"), ("0.8", "sphere_transparency_0.8")):
        actual = run(probe, protein("spheres", "resi 1 and name N") +
                     [setting("sphere_transparency", value, "resi 1 and name N")])
        require(len(actual["spheres"]) == 1 and
                abs(actual["spheres"][0][7] * 255.0 - alpha[key_name][3]) <= 2.0,
                f"{key_name}: sphere alpha differs from the PyMOL pixel")
    actual = run(probe, protein("sticks", "resi 1 and name N+CA") +
                 [setting("stick_transparency", "0.3", "resi 1 and name N+CA")])
    require(actual["cylinders"] and
            all(abs(item[10] * 255.0 - alpha["stick_transparency_0.3"][3]) <= 2.0
                for item in actual["cylinders"]),
            "stick_transparency alpha differs from the PyMOL pixel")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
