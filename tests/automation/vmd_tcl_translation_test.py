#!/usr/bin/env python3
"""Golden test for VMD Tcl -> canonical MolShredder invocation translation.

Every expected invocation is also checked against the real command registry
(`valid` comes from Registry::normalize in the probe).
"""

import json
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def translate(probe: str, case: dict) -> dict:
    arguments = [probe, "--translate"] + [f"--object={o}" for o in case["objects"]]
    result = subprocess.run(arguments, input=case["tcl"], text=True,
                            capture_output=True, check=True)
    return json.loads(result.stdout)


def main() -> int:
    probe, fixtures = sys.argv[1], pathlib.Path(sys.argv[2])
    cases = json.loads((fixtures / "vmd_tcl_translation_cases.json").read_text("utf-8"))["cases"]
    require(len(cases) >= 90, "translation corpus is too small")
    failures = []
    commands_seen = set()
    for case in cases:
        translation = translate(probe, case)
        invocations, diagnostics, stop, tcl_error = [], [], False, False
        for command in translation["commands"]:
            for invocation in command["invocations"]:
                require(invocation["valid"],
                        f"{case['id']}: registry rejects {invocation['command']}: "
                        f"{invocation.get('error')}")
                invocations.append({"command": invocation["command"],
                                    "arguments": invocation["arguments"]})
                commands_seen.add(invocation["command"])
            diagnostics += [d["code"] for d in command["diagnostics"]]
            stop = stop or command["stop"]
            tcl_error = tcl_error or command["tcl_error"]
            if any(d["severity"] == "error" for d in command["diagnostics"]):
                require(not command["invocations"],
                        f"{case['id']}: failed command still has invocations")
        if (invocations, diagnostics, stop, tcl_error) != (
                case["invocations"], case["diagnostics"], case["stop"], case["tcl_error"]):
            failures.append((case["id"], case["tcl"], case["invocations"], case["diagnostics"],
                             invocations, diagnostics, stop, tcl_error))
    for failure in failures:
        print("MISMATCH", failure[0], repr(failure[1]), "\n  expected",
              failure[2], failure[3], "\n  got     ", failure[4], failure[5], failure[6], failure[7])
    require(not failures, f"{len(failures)} Tcl translation cases diverge")
    print(f"{len(cases)} cases, {len(commands_seen)} distinct native commands")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
