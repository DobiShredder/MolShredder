#!/usr/bin/env python3
"""Golden test for PML -> canonical MolShredder invocation translation.

Every expected invocation must also be accepted by the real command registry
(`valid` comes from Registry::normalize in the probe), so a translation can
never name a command or parameter that does not exist.
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
    result = subprocess.run(arguments, input=case["pml"], text=True,
                            capture_output=True, check=True)
    return json.loads(result.stdout)


def main() -> int:
    probe, fixtures = sys.argv[1], pathlib.Path(sys.argv[2])
    cases = json.loads((fixtures / "pml_translation_cases.json").read_text("utf-8"))["cases"]
    require(len(cases) >= 100, "translation corpus is too small")
    failures = []
    commands_seen = set()
    for case in cases:
        translation = translate(probe, case)
        invocations, diagnostics, stop = [], [], False
        for statement in translation["statements"]:
            for invocation in statement["invocations"]:
                require(invocation["valid"],
                        f"{case['id']}: registry rejects {invocation['command']}: "
                        f"{invocation.get('error')}")
                invocations.append({"command": invocation["command"],
                                    "arguments": invocation["arguments"]})
                commands_seen.add(invocation["command"])
            diagnostics += [d["code"] for d in statement["diagnostics"]]
            stop = stop or statement["stop"]
        if (invocations, diagnostics, stop) != (
                case["invocations"], case["diagnostics"], case["stop"]):
            failures.append((case["id"], case["pml"], case["invocations"],
                             case["diagnostics"], invocations, diagnostics))
    for failure in failures:
        print("MISMATCH", failure[0], repr(failure[1]), "\n  expected",
              failure[2], failure[3], "\n  got     ", failure[4], failure[5])
    require(not failures, f"{len(failures)} translation cases diverge")
    # Every error diagnostic leaves no invocation behind for that statement.
    for case in cases:
        translation = translate(probe, case)
        for statement in translation["statements"]:
            if any(d["severity"] == "error" for d in statement["diagnostics"]):
                require(not statement["invocations"],
                        f"{case['id']}: failed statement still has invocations")
    print(f"{len(cases)} cases, {len(commands_seen)} distinct native commands")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
