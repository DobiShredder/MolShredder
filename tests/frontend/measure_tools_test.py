#!/usr/bin/env python3
"""SEQ-321 S1: `analyze dipole`, `analyze surface-atoms` and `analyze trajectory cluster` (definitions: Q-039).

There is no VMD oracle here; expected values are analytic (dipole), geometric (a filled lattice has a 98-atom shell)
or follow the quality-threshold definition on one-dimensional frames.
"""

import importlib
import math
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


def pqr_line(serial, name, x, y, z, charge, radius=1.0):
    return f"ATOM  {serial:5d} {name:<4s} WAT A   1    {x:8.3f}{y:8.3f}{z:8.3f}{charge:8.4f}{radius:7.4f}"


def pdb_line(serial, x, y, z):
    return f"ATOM  {serial:5d}  C   ALA A{serial:4d}    {x:8.3f}{y:8.3f}{z:8.3f}  1.00  0.00           C"


def main() -> int:
    module_path = pathlib.Path(sys.argv[1]).resolve()
    fixtures = pathlib.Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")

    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)

        def load(name: str, text: str, ext: str) -> None:
            path = root / f"{name}.{ext}"
            path.write_text(text)
            ok(m.invoke("load", {"path": str(path), "name": name, "file-format": ext}), f"load {name}")

        def clear() -> None:
            for row in ok(m.invoke("object list"), "list")["table"]["rows"]:
                ok(m.invoke("object delete", {"object": row[1]}), "delete")

        # ---- dipole: neutral pair +1 at x=0, -1 at x=2 -> (-2, 0, 0) e*A for every reference
        load("pair", "\n".join([pqr_line(1, "C", 0, 0, 0, 1.0), pqr_line(2, "O", 2, 0, 0, -1.0), "END"]) + "\n", "pqr")
        for reference in ("geometric", "mass", "origin"):
            data = ok(m.invoke("analyze dipole", {"reference": reference}), f"dipole {reference}")
            require(data["dipole"] == [-2.0, 0.0, 0.0] and data["magnitude"] == 2.0 and data["net_charge"] == 0.0, f"{reference}: {data}")
            require(data["reference_origin_independent"] is True, "neutral dipole is origin independent")
        debye = ok(m.invoke("analyze dipole", {"dipole-unit": "debye", "precision": "4"}), "debye")
        require(abs(debye["magnitude"] - 2.0 * 4.8032) < 1e-3 and debye["unit"] == "debye", f"debye: {debye}")
        clear()
        # charged: +1 at 0 and 0 at 2 -> reference matters; mass-centered uses C (12.011) and O (15.999) masses
        load("ion", "\n".join([pqr_line(1, "C", 0, 0, 0, 1.0), pqr_line(2, "O", 2, 0, 0, 0.0), "END"]) + "\n", "pqr")
        geometric = ok(m.invoke("analyze dipole", {"reference": "geometric"}), "ion geometric")
        origin = ok(m.invoke("analyze dipole", {"reference": "origin"}), "ion origin")
        mass = ok(m.invoke("analyze dipole", {"reference": "mass"}), "ion mass")
        require(geometric["dipole"][0] == -1.0 and origin["dipole"][0] == 0.0, "reference point changes a charged dipole")
        require(geometric["reference_origin_independent"] is False and geometric["net_charge"] == 1.0, "net charge reported")
        require(-2.0 < mass["dipole"][0] < -1.0 and mass["mass_estimated"] is not None, f"mass-centered: {mass}")
        # selections and failures
        one = ok(m.invoke("analyze dipole", {"selection": "name O"}), "single atom")
        require(one["atom_count"] == 1 and one["magnitude"] == 0.0, "single uncharged atom has no dipole")
        require("matches no atoms" in fails(m.invoke("analyze dipole", {"selection": "name ZZ"}), "empty"), "empty selection")
        clear()
        load("nocharge", "\n".join([pdb_line(1, 0, 0, 0), pdb_line(2, 1.5, 0, 0), "END"]) + "\n", "pdb")
        require("partial_charge" in fails(m.invoke("analyze dipole"), "no charges"), "partial charges are required")
        clear()

        # ---- surface atoms of a filled 5x5x5 lattice (spacing 1.5): the 98-atom shell is exposed, the 27 inner atoms are not
        lines = []
        serial = 0
        for x in range(5):
            for y in range(5):
                for z in range(5):
                    serial += 1
                    lines.append(pdb_line(serial, 1.5 * x, 1.5 * y, 1.5 * z))
        load("lattice", "\n".join(lines + ["END"]) + "\n", "pdb")
        data = ok(m.invoke("analyze surface-atoms", {"gridsize": "0.5", "radius": "1.0", "select-name": "shell"}), "surface")
        require(data["surface_atom_count"] == 98 and data["selection_created"] == "shell", f"shell: {data}")
        require(ok(m.invoke("object count", {"selection": "@shell"}), "shell count")["atom_count"] == 98, "named selection holds the shell")
        require(ok(m.invoke("object count", {"selection": "not @shell"}), "inner count")["atom_count"] == 27, "27 buried atoms")
        deeper = ok(m.invoke("analyze surface-atoms", {"gridsize": "0.5", "radius": "1.0", "depth": "1.6"}), "deeper")
        require(deeper["surface_atom_count"] > 98, "a larger depth reaches into the second layer")
        require(ok(m.invoke("analyze surface-atoms", {"selection": "index 63", "gridsize": "0.5"}), "single")["surface_atom_count"] == 1,
                "an isolated atom is a surface atom")
        require("must be positive" in fails(m.invoke("analyze surface-atoms", {"gridsize": "0"}), "bad grid"), "bad grid spacing")
        require("exceed" in fails(m.invoke("analyze surface-atoms", {"gridsize": "0.001"}), "grid budget"), "grid budget")
        clear()

        # ---- periodic images: orthorhombic 10 A cell, cutoff 2 A, cell centre (5, 5, 5)
        cryst = "CRYST1   10.000   10.000   10.000  90.00  90.00  90.00 P 1           1"
        load("cell", "\n".join([cryst, pdb_line(1, 1, 1, 5), pdb_line(2, 9, 5, 5), pdb_line(3, 5, 5, 5), "END"]) + "\n", "pdb")
        data = ok(m.invoke("analyze pbc-neighbors", {"cutoff": "2", "center": "5,5,5"}), "pbc neighbors")
        rows = sorted([tuple(r) for r in data["table"]["rows"]])
        expected = sorted([(1, 0, 1, 0, "1.0", "11.0", "5.0"), (1, 1, 0, 0, "11.0", "1.0", "5.0"), (1, 1, 1, 0, "11.0", "11.0", "5.0"),
                           (2, -1, 0, 0, "-1.0", "5.0", "5.0")])
        norm = sorted([(r[0], r[1], r[2], r[3], float(r[4]), float(r[5]), float(r[6])) for r in rows])
        want = sorted([(e[0], e[1], e[2], e[3], float(e[4]), float(e[5]), float(e[6])) for e in expected])
        require(data["image_count"] == 4 and norm == want, f"image atoms: {norm}")
        wide = ok(m.invoke("analyze pbc-neighbors", {"cutoff": "6", "center": "5,5,5"}), "wide cutoff")
        require(wide["image_count"] > 26 * 3 // 2, "a larger cutoff adds images from the second shell")
        require("positive" in fails(m.invoke("analyze pbc-neighbors", {"cutoff": "0"}), "zero cutoff"), "cutoff must be positive")
        require("three numbers" in fails(m.invoke("analyze pbc-neighbors", {"cutoff": "2", "center": "5,5"}), "bad center"), "center syntax")
        default_center = ok(m.invoke("analyze pbc-neighbors", {"cutoff": "2"}), "default centre")
        require(default_center["center_source"] == "selection centroid", "centre defaults to the selection centroid")
        clear()
        load("plain", pdb_line(1, 0, 0, 0) + "\nEND\n", "pdb")
        require("no unit cell" in fails(m.invoke("analyze pbc-neighbors", {"cutoff": "2"}), "no cell"), "cells are required")
        clear()
        # triclinic cell (gamma 60): the matrix maps b = (5, 8.660254, 0) onto (0, 10, 0)
        tri = "CRYST1   10.000   10.000   10.000  90.00  90.00  60.00 P 1           1"
        load("tri", "\n".join([tri, pdb_line(1, 0, 0, 0), "END"]) + "\n", "pdb")
        matrix = ok(m.invoke("analyze pbc-orthonormal", {"center": "0,0,0"}), "orthonormal")["matrix"]
        require(abs(matrix[0][0] - 1.0) < 1e-5 and abs(matrix[0][1] + 0.57735) < 1e-5 and abs(matrix[1][1] - 1.1547) < 1e-4 and matrix[3] == [0.0, 0.0, 0.0, 1.0],
                f"triclinic matrix: {matrix}")
        b = (5.0, 8.660254, 0.0)
        mapped = [sum(matrix[r][c] * b[c] for c in range(3)) + matrix[r][3] for r in range(3)]
        require(abs(mapped[0]) < 1e-4 and abs(mapped[1] - 10.0) < 1e-4 and abs(mapped[2]) < 1e-9, f"b maps to the y axis: {mapped}")
        offset = ok(m.invoke("analyze pbc-orthonormal", {"center": "3,4,5"}), "centre kept fixed")["matrix"]
        fixed = [sum(offset[r][c] * (3, 4, 5)[c] for c in range(3)) + offset[r][3] for r in range(3)]
        require(all(abs(a - b) < 1e-4 for a, b in zip(fixed, (3, 4, 5))), "the cell centre is a fixed point")
        clear()

        # ---- all interatomic distances within a cutoff (PyMOL `distance mode=0`) = analyze contacts without the bond filter
        if fixtures is not None:
            load("ubq", (fixtures / "1ubq.pdb").read_text(), "pdb")
            def coords(selection):
                rows = ok(m.invoke("object atoms", {"properties": "index,x,y,z", "selection": selection, "limit": "2000"}), "atoms")["table"]["rows"]
                return {int(r[0]): (r[1], r[2], r[3]) for r in rows}
            first, second = coords("resi 1-10"), coords("resi 12-30")
            brute = {(a, b) for a, p in first.items() for b, q in second.items() if math.dist(p, q) <= 6.0}
            pairs = ok(m.invoke("analyze contacts", {"first": "resi 1-10", "second": "resi 12-30", "cutoff": "6.0", "exclude-bonded": "false"}), "pairs")
            found = {(int(r[0]), int(r[1])) for r in pairs["table"]["rows"]}
            found |= {(b, a) for a, b in found if (b, a) in brute}
            require(len(brute) > 20 and brute == {pair for pair in found if pair in brute} and pairs["pair_count"] == len(brute),
                    f"all pairs within the cutoff match a brute-force search: {pairs['pair_count']} vs {len(brute)}")
            clear()

        # ---- pbc get / set: give a bare structure a cell, read it back, use it, undo, remove it
        load("bare", "\n".join([pdb_line(1, 1, 1, 5), pdb_line(2, 9, 5, 5), "END"]) + "\n", "pdb")
        require(ok(m.invoke("pbc get"), "no cell")["has_cell"] is False, "no cell yet")
        ok(m.invoke("pbc set", {"a": "10", "b": "10", "c": "10"}), "set orthorhombic")
        cell = ok(m.invoke("pbc get"), "cell")
        require(cell["a"] == 10.0 and cell["alpha"] == 90.0 and cell["volume"] == 1000.0, f"orthorhombic: {cell}")
        require(ok(m.invoke("analyze pbc-neighbors", {"cutoff": "2", "center": "5,5,5"}), "neighbours after set")["image_count"] == 4, "images use the new cell")
        ok(m.invoke("pbc set", {"a": "10", "b": "12", "c": "14", "alpha": "80", "beta": "95", "gamma": "70"}), "set triclinic")
        tri = ok(m.invoke("pbc get"), "triclinic")
        require(all(abs(tri[k] - v) < 1e-4 for k, v in (("a", 10.0), ("b", 12.0), ("c", 14.0), ("alpha", 80.0), ("beta", 95.0), ("gamma", 70.0))), f"triclinic: {tri}")
        require(abs(tri["volume"] - 10 * 12 * 14 * math.sqrt(1 - math.cos(math.radians(80)) ** 2 - math.cos(math.radians(95)) ** 2 - math.cos(math.radians(70)) ** 2
                                                              + 2 * math.cos(math.radians(80)) * math.cos(math.radians(95)) * math.cos(math.radians(70)))) < 1e-3,
                "triclinic volume")
        ok(m.invoke("edit undo"), "undo the triclinic cell")
        require(ok(m.invoke("pbc get"), "after undo")["alpha"] == 90.0, "undo restores the previous cell")
        ok(m.invoke("pbc set", {"clear": "true"}), "clear")
        require(ok(m.invoke("pbc get"), "cleared")["has_cell"] is False, "cell removed")
        require("needs --a" in fails(m.invoke("pbc set", {"a": "10"}), "missing lengths"), "all lengths are required")
        require("between 0 and 180" in fails(m.invoke("pbc set", {"a": "10", "b": "10", "c": "10", "alpha": "190"}), "angle"), "angle range")
        require("valid parallelepiped" in fails(m.invoke("pbc set", {"a": "10", "b": "10", "c": "10", "alpha": "10", "beta": "10", "gamma": "170"}), "degenerate"),
                "degenerate cell")
        clear()

        # ---- point groups from loaded structures (element-aware, centre of mass origin)
        def xyz(rows):
            return f"{len(rows)}\nmolecule\n" + "\n".join(f"{e} {x:.6f} {y:.6f} {z:.6f}" for e, x, y, z in rows) + "\n"
        molecules = {
            "water": ([("O", 0, 0, 0.117), ("H", 0.757, 0, -0.469), ("H", -0.757, 0, -0.469)], "C2v", {"C2": 1}, 2),
            "methane": ([("C", 0, 0, 0)] + [("H", 0.629 * a, 0.629 * b, 0.629 * c) for a, b, c in ((1, 1, 1), (1, -1, -1), (-1, 1, -1), (-1, -1, 1))], "Td", {"C2": 3, "C3": 4}, 6),
            "co2": ([("O", 0, 0, 1.16), ("C", 0, 0, 0), ("O", 0, 0, -1.16)], "Dinfh", {}, 0),
        }
        for name, (rows, group, axes, planes) in molecules.items():
            load(name, xyz(rows), "xyz")
            data = ok(m.invoke("analyze symmetry"), f"symmetry {name}")
            require(data["point_group"] == group, f"{name}: {data['point_group']} != {group}")
            require(data["rotation_axes"] == axes and (name == "co2" or data["mirror_planes"] == planes), f"{name} elements: {data}")
            clear()
        load("ethene", xyz([("C", 0.667, 0, 0), ("C", -0.667, 0, 0)] + [("H", 1.24 * a, 0.92 * b, 0) for a in (1, -1) for b in (1, -1)]), "xyz")
        require(ok(m.invoke("analyze symmetry"), "ethene")["point_group"] == "D2h", "D2h")
        require(ok(m.invoke("analyze symmetry", {"selection": "element C"}), "carbons only")["point_group"] == "Dinfh", "the carbon pair alone is linear")
        require("tolerance must be positive" in fails(m.invoke("analyze symmetry", {"tolerance": "0"}), "tolerance"), "tolerance must be positive")
        clear()

        # ---- QT clustering of trajectory frames: one atom at x = 0, 0.5, 1.0, 5.0, 5.2, 9.0 (RMSD without fit = |dx|)
        positions = [0.0, 0.5, 1.0, 5.0, 5.2, 9.0]
        models = []
        for index, x in enumerate(positions):
            models += [f"MODEL     {index + 1:4d}", pdb_line(1, x, 0, 0), "ENDMDL"]
        load("frames", "\n".join(models + ["END"]) + "\n", "pdb")
        data = ok(m.invoke("analyze trajectory cluster", {"cutoff": "1.0", "fit": "none"}), "cluster")
        rows = ok(m.invoke("analyze trajectory cluster", {"cutoff": "1.0", "fit": "none"}), "cluster")  # deterministic
        require(data["cluster_count"] == 3 and data["frame_count"] == 6, f"clusters: {data}")
        table = data.get("table", {}).get("rows") if isinstance(data.get("table"), dict) else None
        require(table is not None, "cluster table present")
        require([r[1] for r in table] == [3, 2, 1] and table[0][5] == "0,1,2" and table[1][5] == "3,4" and table[2][5] == "5", f"table: {table}")
        require(abs(table[0][3] - 1.0) < 1e-9 and table[0][2] == 0, "diameter and seed frame")
        tight = ok(m.invoke("analyze trajectory cluster", {"cutoff": "0.4", "fit": "none"}), "tight")
        require(tight["cluster_count"] == 5, f"tight cutoff splits further: {tight['cluster_count']}")
        strided = ok(m.invoke("analyze trajectory cluster", {"cutoff": "1.0", "fit": "none", "stride": "2"}), "stride")
        require(strided["frame_count"] == 3, "stride selects frames 0, 2, 4")
        require("positive" in fails(m.invoke("analyze trajectory cluster", {"cutoff": "0"}), "zero cutoff"), "cutoff must be positive")
        require("unit" in rows and rows["unit"] == "angstrom", "unit reported")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
