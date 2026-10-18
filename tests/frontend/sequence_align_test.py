#!/usr/bin/env python3
"""SEQ-321 S4: sequence-guided alignment with outlier rejection (`measure fit|object align --match sequence`).

tests/io/fixtures/align/ubq_modified.pdb is 1UBQ with residues 54-57 deleted, residues 70-76 displaced by 10/-6 A,
residue 20 renamed VAL, 0.35 A Gaussian noise, then rotated and translated. The expected numbers come from
Open-Source PyMOL 3.1.0 `cmd.align(..., cycles=N, cutoff=2.0)` on the same two files.
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


def fails(result: dict, what: str) -> str:
    require(result["status"] == "error", f"{what} should have failed")
    return result["error"]["message"]


def main() -> int:
    module_path = pathlib.Path(sys.argv[1]).resolve()
    fixtures = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")
    ok(m.invoke("load", {"path": str(fixtures / "1ubq.pdb"), "name": "tgt", "file-format": "pdb"}), "load target")
    ok(m.invoke("load", {"path": str(fixtures / "align" / "ubq_modified.pdb"), "name": "mob", "file-format": "pdb"}), "load mobile")

    def fit(selection: str, cycles: int, **extra) -> dict:
        arguments = {"target-object": "tgt", "mobile-object": "mob", "mobile-selection": selection, "target-selection": selection,
                     "match": "sequence", "cycles": str(cycles), "cutoff": "2.0"}
        arguments.update(extra)
        return ok(m.invoke("measure fit", arguments), f"fit {selection} cycles={cycles}")

    # PyMOL 3.1.0: CA, cycles=0 -> rms 2.9800 over 72 pairs; cycles=5 -> rms 0.5845, 65 atoms, 2 passes
    plain = fit("name CA", 0)
    require(plain["paired_atom_count"] == 72 and abs(plain["rmsd_after"] - 2.9800) < 5e-4, f"unrefined CA fit: {plain}")
    require(plain["aligned_residue_count"] == 72 and abs(plain["alignment_score"] - 345.5) < 1e-9 and plain["cycles_run"] == 0,
            "BLOSUM62 score and residue count match PyMOL")
    refined = fit("name CA", 5)
    require(refined["initial_pair_count"] == 72 and refined["paired_atom_count"] == 65 and refined["rejected_pair_count"] == 7,
            f"outlier rejection keeps 65 of 72 CA pairs: {refined}")
    require(abs(refined["rmsd_after"] - 0.5845) < 5e-4 and refined["cycles_run"] == 2, f"refined CA fit: {refined}")
    # all atoms: PyMOL pairs 570 atoms (rms 3.0574), refinement keeps 506 (rms 0.5791) after 4 passes
    all_plain = fit("all", 0)
    require(all_plain["paired_atom_count"] == 570 and abs(all_plain["rmsd_after"] - 3.0574) < 5e-4, f"all atoms unrefined: {all_plain}")
    all_refined = fit("all", 5)
    require(all_refined["paired_atom_count"] == 506 and abs(all_refined["rmsd_after"] - 0.5791) < 5e-4 and all_refined["cycles_run"] == 4,
            f"all atoms refined: {all_refined}")
    # a looser cutoff rejects fewer pairs, a tighter one more; cycles bounds the loop
    loose = fit("name CA", 5, cutoff="4.0")
    tight = fit("name CA", 5, cutoff="1.0")
    require(loose["paired_atom_count"] > refined["paired_atom_count"] > tight["paired_atom_count"] >= 3, "cutoff controls the rejection")
    require(fit("name CA", 1)["cycles_run"] == 1, "cycles caps the passes")
    # the order and identifier matches also accept refinement
    ident = ok(m.invoke("measure fit", {"target-object": "tgt", "mobile-object": "mob", "mobile-selection": "name CA", "target-selection": "name CA",
                                          "match": "identifier", "cycles": "5"}), "identifier fit")
    require(ident["paired_atom_count"] <= ident["initial_pair_count"] and ident["rmsd_after"] < 3.0, "identifier matching with refinement")

    # object align applies the refined transform: afterwards the unrefined fit of the kept core is small
    ok(m.invoke("object align", {"target-object": "tgt", "mobile-object": "mob", "mobile-selection": "name CA", "target-selection": "name CA",
                                   "match": "sequence", "cycles": "5"}), "object align")
    after = fit("name CA", 5)
    require(abs(after["rmsd_after"] - 0.5845) < 5e-4 and all(abs(after["matrix"][i] - (1.0 if i % 5 == 0 else 0.0)) < 1e-4 for i in range(16)),
            f"an aligned object needs no further transform: {after}")

    # errors
    require("fewer than three" in fails(m.invoke("measure fit", {"target-object": "tgt", "mobile-object": "mob", "mobile-selection": "name CA",
                                                                  "target-selection": "name CA", "match": "sequence", "cycles": "5", "cutoff": "0.0001"}),
                                         "too tight"), "over-rejection is reported")
    require("cutoff must be positive" in fails(m.invoke("measure fit", {"target-object": "tgt", "mobile-object": "mob", "match": "sequence", "cutoff": "0"}), "cutoff"),
            "cutoff must be positive")
    fails(m.invoke("measure fit", {"target-object": "tgt", "mobile-object": "mob", "mobile-selection": "name ZZ", "match": "sequence"}), "empty selection")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
