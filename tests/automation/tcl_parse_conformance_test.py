#!/usr/bin/env python3
"""Compares the adapter's Tcl word parsing and substitution with Tcl 8.5.9.

The fixture records the words a probe command received for 64 scripts run in
tclsh, including which scripts fail (the failing command and later ones do not
run, earlier ones already did).
"""

import json
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    probe, fixtures = sys.argv[1], pathlib.Path(sys.argv[2])
    capture = json.loads((fixtures / "tcl85_word_parsing.json").read_text("utf-8"))
    require(len(capture["cases"]) >= 60, "capture is missing cases")
    failures = []
    for case in capture["cases"]:
        result = subprocess.run([probe], input=case["script"], text=True,
                                capture_output=True, check=True)
        ours = json.loads(result.stdout)
        expected_error = case["error"] is not None
        if ours["calls"] != case["calls"] or (ours["error"] is not None) != expected_error:
            failures.append((case["id"], case["script"], case["calls"], case["error"],
                             ours["calls"], ours["error"]))
    for failure in failures:
        print("MISMATCH", failure[0], repr(failure[1]), "\n  expected", failure[2],
              failure[3], "\n  got     ", failure[4], failure[5])
    require(not failures, f"{len(failures)} Tcl cases diverge from tclsh")
    print(f"compared {len(capture['cases'])} Tcl scripts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
