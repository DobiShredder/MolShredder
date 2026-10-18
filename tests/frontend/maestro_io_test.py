#!/usr/bin/env python3
"""SEQ-319 S5: MacroModel (MMOD) and Maestro (MAE) structures.

two.mmd was generated with Open-Source PyMOL 3.1.0 (atom types edited by hand); the expected reading
(elements from the type ranges, chain/residue columns, charges, bond orders, states) was compared with PyMOL.
PyMOL's open-source build cannot read or write MAE, so sample.mae is a hand-written Maestro file and the
writers are checked by round trips.
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


def fails(result: dict, what: str) -> str:
    require(result["status"] == "error", f"{what} should have failed")
    return result["error"]["message"]


def main() -> int:
    module_path = pathlib.Path(sys.argv[1]).resolve()
    fixtures = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")
    maestro = fixtures / "maestro"

    def load(name: str, path: pathlib.Path, file_format: str = "auto") -> dict:
        return ok(m.invoke("load", {"path": str(path), "name": name, "file-format": file_format}), f"load {path.name}")

    def atoms(properties: str = "name,resn,resi,chain,element,formal_charge,partial_charge,b,q,x,y,z"):
        return ok(m.invoke("object atoms", {"properties": properties, "limit": "5000"}), "atoms")["table"]["rows"]

    def clear() -> None:
        for row in ok(m.invoke("object list"), "list")["table"]["rows"]:
            ok(m.invoke("object delete", {"object": row[1]}), "delete")

    def bonds() -> int:
        return ok(m.invoke("object bonds"), "bonds")["returned_count"]

    # ---- MMOD: two blocks with the same atoms become two frames (PyMOL states) of one object
    loaded = load("mm", maestro / "two.mmd")
    objects = ok(m.invoke("object list"), "list")["table"]["rows"]
    require(len(objects) == 1, f"two same-atom blocks form one object: {objects}")
    rows = atoms()
    require([r[0] for r in rows] == ["N", "CA", "C", "O", "CB", "H", "HA", "1HB", "2HB", "3HB"], f"names: {[r[0] for r in rows]}")
    require([r[4] for r in rows] == ["N", "C", "C", "O", "C", "H", "H", "H", "H", "H"],
            "elements: type ranges for the first block, atom names for type 64")
    require({r[1] for r in rows} == {"ALA"} and {r[2] for r in rows} == {"2"} and {r[3] for r in rows} == {"A"}, "residue columns")
    require(abs(rows[0][6] + 0.4157) < 1e-5 and abs(rows[2][6] - 0.5973) < 1e-5, "charges from the charge column")
    require(abs(rows[0][9] + 0.6769) < 1e-6 and abs(rows[3][10] + 1.2273) < 1e-5, "coordinates")
    require(bonds() == 9, "nine bonds from the neighbour fields")
    require(loaded["frame_count"] == 2, "second block is a second state")
    # ---- MMOD writer: round trip keeps elements, names, residues, bonds and coordinates
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        saved = ok(m.invoke("save", {"path": str(root / "out.mmd"), "file-format": "mmod"}), "save mmod")
        require(saved["format"] == "mmod" if "format" in saved else True, "format reported")
        text = (root / "out.mmd").read_text()
        require(text.splitlines()[0].split()[0] == "10", "header holds the atom count")
        clear()
        load("rt", root / "out.mmd")
        rt = atoms()
        require([(r[0], r[4], r[1], r[3]) for r in rt] == [(r[0], r[4], r[1], r[3]) for r in rows], "MMOD round trip identities")
        require(all(abs(a[9] - b[9]) < 1e-5 and abs(a[11] - b[11]) < 1e-5 for a, b in zip(rt, rows)), "MMOD round trip coordinates")
        require(abs(rt[1][6] - rows[1][6]) < 1e-5 and bonds() == 9, "MMOD round trip charges and bonds")
        clear()

        # ---- MAE reader: hand-written Maestro sample with two structures
        load("mae", maestro / "sample.mae")
        listed = ok(m.invoke("object list"), "list")["table"]["rows"]
        require(len(listed) == 2, f"two f_m_ct blocks become two objects: {listed}")
        water = [row for row in listed if row[5] == 3][0]
        gly = [row for row in listed if row[5] == 5][0]
        ok(m.invoke("object activate", {"id": str(water[0])}), "activate water")
        water_rows = atoms()
        require([r[0] for r in water_rows] == ["O", "H1", "H2"] and bonds() == 2, f"water block: {water_rows}")
        ok(m.invoke("object activate", {"id": str(gly[0])}), "activate glycine")
        gly_rows = atoms()
        require([r[0] for r in gly_rows] == ["N", "CA", "C", "O", "H"], f"MAE names trimmed: {gly_rows}")
        require([r[4] for r in gly_rows] == ["N", "C", "C", "O", "H"], "atomic numbers")
        require([r[5] for r in gly_rows] == [1, 0, 0, -1, 0] and abs(gly_rows[3][6] + 0.8) < 1e-9, "formal and partial charges")
        require({r[1] for r in gly_rows} == {"GLY"} and all(str(r[2]).startswith("7") for r in gly_rows) and gly_rows[2][2] != gly_rows[1][2] and {r[3] for r in gly_rows} == {"A"}, "residue")
        require(abs(gly_rows[1][7] - 12.5) < 1e-9 and abs(gly_rows[1][8] - 0.5) < 1e-9, "B-factor and occupancy")
        require(abs(gly_rows[3][9] - 4.0) < 1e-9 and abs(gly_rows[3][10] - 2.5) < 1e-9, "coordinates")
        require(bonds() == 4, "m_bond rows")
        # writer round trip of the glycine structure
        ok(m.invoke("save", {"path": str(root / "out.mae"), "file-format": "mae"}), "save mae")
        mae_text = (root / "out.mae").read_text()
        require("f_m_ct {" in mae_text and "m_atom[5]" in mae_text and "m_bond[4]" in mae_text, "Maestro blocks written")
        clear()
        load("rt", root / "out.mae")
        back = atoms()
        require([(r[0], r[4], r[1], r[2], r[3], r[5]) for r in back] == [(r[0], r[4], r[1], r[2], r[3], r[5]) for r in gly_rows],
                "MAE round trip identities")
        require(all(abs(a[6] - b[6]) < 1e-6 and abs(a[9] - b[9]) < 1e-5 for a, b in zip(back, gly_rows)),
                "MAE round trip charges and coordinates")
        require(abs(back[1][7] - 12.5) < 1e-6 and abs(back[1][8] - 0.5) < 1e-6, "MAE round trip B-factor and occupancy")
        require(bonds() == 4, "MAE round trip bonds")
        clear()

        # a protein through both writers keeps the atom table and the bonds
        load("ubq", fixtures / "1ubq.pdb")
        source = atoms("name,resn,resi,chain,element,x,y,z")
        source_bonds = bonds()
        for fmt, ext in (("mae", "mae"), ("mmod", "mmd")):
            ok(m.invoke("save", {"path": str(root / f"ubq.{ext}"), "file-format": fmt}), f"save {fmt}")
            load(f"ubq_{fmt}", root / f"ubq.{ext}")
            rows = atoms("name,resn,resi,chain,element,x,y,z")
            require(len(rows) == 2 * len(source) or len(rows) == len(source), f"{fmt}: atom count")
            tail = rows[-len(source):]
            require([r[:5] for r in tail] == [r[:5] for r in source], f"{fmt}: names, residues, chains and elements survive")
            require(all(abs(a[5] - b[5]) < 1e-3 and abs(a[7] - b[7]) < 1e-3 for a, b in zip(tail, source)), f"{fmt}: coordinates")
            require(bonds() >= source_bonds, f"{fmt}: bonds")
            ok(m.invoke("object delete", {"object": f"ubq_{fmt}"}), "cleanup")
        clear()

        # ---- damaged input
        cases = {
            "mae_unterminated.mae": "f_m_ct {\n s_m_title\n :::\n \"x\n",
            "mae_count.mae": "f_m_ct {\n :::\n m_atom[3] {\n r_m_x_coord\n :::\n 1 0.0\n :::\n }\n}\n",
            "mae_nothing.mae": "{\n s_m_m2io_version\n :::\n 2.0.0\n}\n",
            "mae_bond.mae": ("f_m_ct {\n :::\n m_atom[1] {\n r_m_x_coord\n r_m_y_coord\n r_m_z_coord\n :::\n 1 0.0 0.0 0.0\n :::\n }\n"
                             " m_bond[1] {\n i_m_from\n i_m_to\n :::\n 1 1 9\n :::\n }\n}\n"),
            "mmod_short.mmd": "    2  title\n  3     0 0     0 0     0 0     0 0     0 0     0 0\n",
            "mmod_bad_count.mmd": "  abc  title\n",
        }
        for label, text in cases.items():
            bad = root / label
            bad.write_text(text)
            result = m.invoke("load", {"path": str(bad), "name": "bad", "file-format": label.rsplit(".", 1)[1].replace("mmd", "mmod")})
            require(result["status"] == "error", f"{label} must fail")
        sample = (maestro / "sample.mae").read_text()
        for cut in (len(sample) // 3, len(sample) // 2, len(sample) - 40):
            bad = root / "cut.mae"
            bad.write_text(sample[:cut])
            m.invoke("load", {"path": str(bad), "name": "cut", "file-format": "mae"})  # must not crash
            clear()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
