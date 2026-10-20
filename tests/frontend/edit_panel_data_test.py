#!/usr/bin/env python3
"""SEQ-330 G1: the data the atom editor panel reads — `object atoms` with coordinates/chemistry columns and
`object bonds` with the stable `bond_id` — and an edit built from exactly that data."""

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
    module_path = pathlib.Path(sys.argv[1]).resolve()
    fixtures = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")
    ok(m.invoke("load", {"path": str(fixtures / "chemistry.sdf"), "name": "nitrile"}), "load")

    atoms = ok(m.invoke("object atoms", {
        "selection": "all", "limit": "10",
        "properties": "index,id,name,element,resn,resi,chain,x,y,z,formal_charge,atomic_number"}), "atoms")["table"]
    columns = atoms["columns"]
    require(columns[-1] == "atomic_number" and "x" in columns and "formal_charge" in columns, f"columns {columns}")
    first = dict(zip(columns, atoms["rows"][0]))
    require(first["element"] == "C" and first["atomic_number"] == 6.0, f"first atom {first}")

    bonds = ok(m.invoke("object bonds", {"selection": "all", "limit": "10"}), "bonds")["table"]
    require(bonds["columns"][-1] == "bond_id", f"bond columns {bonds['columns']}")
    bond = dict(zip(bonds["columns"], bonds["rows"][0]))
    require(bond["bond_id"] > 0 and bond["order"] == "triple", f"bond {bond}")

    # an edit that quotes the listed bond id and the object's first revisions
    ok(m.invoke("edit bond-order", {
        "bond-id": str(int(bond["bond_id"])), "order": "double",
        "expected-topology-version": "1", "expected-coordinate-source-revision": "1"}), "edit bond-order")
    after = ok(m.invoke("object bonds", {"selection": "all", "limit": "10"}), "bonds after")["table"]
    require(after["rows"][0][after["columns"].index("order")] == "double", "bond order changed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
