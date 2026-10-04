#!/usr/bin/env python3
"""Compares the PML parser with arguments captured from PyMOL 3.1.0.

The fixtures record what probe commands registered in PyMOL received for each
input (see tests/automation/fixtures/README.md). Calls that come from running
Python or data blocks are removed from the expectation because MolShredder
never executes them; every removal is listed in INTENTIONAL_DIVERGENCES.
"""

import json
import pathlib
import subprocess
import sys

PROBES = ["probe:11", "probex:11", "uniqcmd_zzz:11"]
MODE_PROBES = ["probe_m11:11", "probe_m12:12", "probe_m13:13", "probe_m21:21"]

# capture call (command, args) -> reason it is not expected from the parser
INTENTIONAL_DIVERGENCES = {
    ("probe", ("from_python",)): "executed by a python block, which is only reported",
    ("probe", ("nested",)): "executed by a python block, which is only reported",
    ("probe", ("not_a_command",)): "PyMOL ran embedded data lines as commands; the parser skips them",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def parse(probe: str, text: str, extra) -> dict:
    result = subprocess.run([probe, *extra], input=text, text=True,
                            capture_output=True, check=True)
    return json.loads(result.stdout)


def our_calls(parsed: dict, names) -> list:
    return [
        {"command": s["command"], "args": s["args"], "kwargs": s["kwargs"]}
        for s in parsed["statements"]
        if s["kind"] == "command" and s["command"] in names
    ]


def main() -> int:
    probe, fixtures = sys.argv[1], pathlib.Path(sys.argv[2])
    capture = json.loads((fixtures / "pymol310_pml_parsing.json").read_text("utf-8"))
    require(capture["pymol_version"] == "3.1.0", "unexpected capture version")
    require(len(capture["cases"]) >= 100, "capture is missing cases")
    names = {"probe", "probex", "uniqcmd_zzz"}
    failures = []
    skipped_routes = 0
    for case in capture["cases"]:
        if case["id"].endswith("@at"):
            skipped_routes += 1  # same text as the load route, already compared
            continue
        expected = []
        for call in case["calls"]:
            key = (call["command"], tuple(call["args"]))
            if key in INTENTIONAL_DIVERGENCES:
                continue
            expected.append({"command": call["command"], "args": call["args"],
                             "kwargs": call["kwargs"]})
        actual = our_calls(parse(probe, case["input"], PROBES), names)
        if actual != expected:
            failures.append((case["id"], case["input"], expected, actual))
    for failure in failures:
        print("MISMATCH", failure[0], repr(failure[1]), "expected", failure[2],
              "got", failure[3])
    require(not failures, f"{len(failures)} PML parsing cases diverge from PyMOL")

    modes = json.loads((fixtures / "pymol310_pml_modes.json").read_text("utf-8"))
    mode_failures = []
    compared = 0
    for record in modes["records"]:
        if record["mode"] not in (11, 12, 13, 21):
            continue
        name = f"probe_m{record['mode']}"
        expected = [{"command": name, "args": c["args"], "kwargs": c["kwargs"]}
                    for c in record["calls"]]
        actual = our_calls(parse(probe, f"{name} {record['input']}", MODE_PROBES),
                           {name})
        compared += 1
        if actual != expected:
            mode_failures.append((record["mode"], record["input"], expected, actual))
    for failure in mode_failures:
        print("MODE MISMATCH", failure)
    require(compared == 4 * 18 and not mode_failures,
            f"{len(mode_failures)} parser-mode cases diverge from PyMOL")

    # Statement kinds and diagnostics that have no PyMOL probe equivalent.
    script = parse(probe,
                   "load a.pdb\npython\nx = 1\npython end\nimport os\n/y = 2\n"
                   "@other.pml\nskip\nshow cartoon\nskip end\nembed d, txt\nzoom\n"
                   "embed end\nzoom all\nabort\nhide all\n", [])
    kinds = [(s["kind"], s["command"]) for s in script["statements"]]
    require(kinds == [("command", "load"), ("python_block", "python"),
                      ("python_statement", "import"), ("python_statement", ""),
                      ("include_script", "@"), ("skip_block", "skip"),
                      ("embed_block", "embed"), ("command", "zoom"),
                      ("abort", "abort")], f"unexpected statements {kinds}")
    require(script["aborted"] is True, "abort must stop the script")
    block = next(s for s in script["statements"] if s["kind"] == "python_block")
    require(block["block_lines"] == 1 and
            block["diagnostics"] == ["python_block_requires_trust"],
            "python block must be reported, not run")
    unterminated = parse(probe, "python\nx = 1\n", [])
    require(unterminated["statements"][0]["diagnostics"] ==
            ["unterminated_python_block"], "unterminated python block")
    abbreviated = parse(probe, "zo all\norie\nhid\nsho\n", [])
    require([s["command"] for s in abbreviated["statements"][:3]] ==
            ["zoom", "orient", "hide"] and
            all(s["kind"] == "command" for s in abbreviated["statements"][:3]) and
            abbreviated["statements"][3]["diagnostics"] == ["ambiguous_command"],
            "unique prefixes must expand and ambiguous ones must be reported")
    unknown = parse(probe, "frobnicate 1\nSHOW all\n", [])
    require([s["kind"] for s in unknown["statements"]] ==
            ["unknown_command", "unknown_command"] and
            unknown["statements"][0]["diagnostics"] == ["unknown_command"],
            "unknown commands are reported")
    print(f"compared {len(capture['cases']) - skipped_routes} script/argument cases "
          f"and {compared} parser-mode cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
