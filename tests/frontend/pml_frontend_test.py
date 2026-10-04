#!/usr/bin/env python3
"""PML adapter parity: Python and CLI see the same translation and run result."""

import importlib
import json
import pathlib
import subprocess
import sys

from console_script import portable_console_script


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def cli_envelope(cli_path, line: str) -> dict:
    script = f"format json\n{line}\nexit\n"
    completed = subprocess.run([str(cli_path), "console"],
                               input=portable_console_script(script), text=True,
                               capture_output=True, check=True)
    envelopes = [json.loads(row[row.find('{"schema_version"'):])
                 for row in completed.stdout.splitlines()
                 if '{"schema_version"' in row]
    require(len(envelopes) == 1, "expected one CLI envelope")
    return envelopes[0]


def main() -> int:
    require(len(sys.argv) == 5, "expected module, CLI, fixture and output directory")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    fixture = pathlib.Path(sys.argv[3]).resolve()
    output = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    text = (f'load {fixture}, protein\nshow cartoon\ncolor red, resi 1-10\n'
            'select site, resi 5-9\nzoom site\nbg_color white')
    translated = molshredder.invoke("pml translate", {"text": text})
    require(translated["status"] == "ok", f"translate failed: {translated}")
    data = translated["data"]
    require(data["statement_count"] == 6 and data["translated_statements"] == 6 and
            data["failed_statements"] == 0 and data["invocation_count"] == 6 and
            data["language"] == "pml" and len(data["source_sha256"]) == 64,
            f"unexpected translation summary {data}")
    require([s["invocations"][0]["command"] for s in data["statements"]] ==
            ["load", "show", "color", "select", "view zoom", "background set"],
            "unexpected canonical commands")

    # Pure translation is identical through the CLI (separate process, same text).
    text_one_line = "show cartoon; color red, chain A; zoom"
    py_pure = molshredder.invoke("pml translate", {"text": text_one_line})
    cli_pure = cli_envelope(
        cli_path, f'invoke "pml translate" --text "{text_one_line}"')
    require(cli_pure["status"] == "ok" and
            cli_pure["data"] == py_pure["data"], "CLI and Python translations diverged")

    # Execution: state changes through the same operations a user would invoke.
    ran = molshredder.invoke("pml run", {"text": text})
    require(ran["status"] == "ok", f"run failed: {ran}")
    require(ran["data"]["executed_invocations"] == 6 and
            ran["data"]["failed_statements"] == 0 and
            ran["data"]["mutations_committed"] >= 4, f"unexpected run summary {ran['data']}")
    listing = molshredder.invoke("object list", {})
    table = listing["data"]["table"]
    names = [row[table["columns"].index("name")] for row in table["rows"]]
    reps = [row[table["columns"].index("representation_count")] for row in table["rows"]]
    require(names == ["protein"] and reps[0] >= 1,
            f"load and show must create the object and a representation: {table}")

    # Default policy stops at the first failing statement and reports what ran.
    failing = molshredder.invoke("pml run", {"text": "show nosuchrep\nbg_color black"})
    require(failing["status"] == "error" and
            "line 1" in failing["error"]["message"] and
            failing["error"]["details"]["failed_line"] == "1" and
            failing["error"]["details"]["executed_invocations"] == "0",
            f"unexpected failure report {failing}")
    keep_going = molshredder.invoke(
        "pml run", {"text": "show nosuchrep\nbg_color black", "on-error": "continue"})
    require(keep_going["status"] == "ok" and
            keep_going["data"]["failed_statements"] == 1 and
            keep_going["data"]["executed_invocations"] == 1, "continue policy")

    # quit stops the script without running later statements.
    stopped = molshredder.invoke("pml run", {"text": "bg_color red\nquit\nbg_color blue"})
    require(stopped["status"] == "ok" and stopped["data"]["stopped"] is True and
            stopped["data"]["executed_invocations"] == 1, "quit must stop the script")

    # Code execution is refused, never run.
    marker = output / "pml-must-not-exist"
    marker.unlink(missing_ok=True)
    for refused in (f"system touch {marker}", "import os", "python\nopen('x','w')\npython end"):
        result = molshredder.invoke("pml run", {"text": refused})
        require(result["status"] == "error" and "trust" in result["error"]["message"],
                f"code execution must be refused: {refused!r} -> {result}")
    require(not marker.exists(), "a refused statement must not run")

    # A script file works like text, and both are hashed.
    script = output / "frontend.pml"
    script.write_text("bg_color grey50\n", encoding="utf-8")
    from_file = molshredder.invoke("pml translate", {"path": str(script)})
    require(from_file["status"] == "ok" and from_file["data"]["source"] == "path" and
            from_file["data"]["source_path"].endswith("frontend.pml"), "file source")
    both = molshredder.invoke("pml translate", {"path": str(script), "text": "zoom"})
    neither = molshredder.invoke("pml translate", {})
    require(both["status"] == "error" and neither["status"] == "error",
            "exactly one of path and text is required")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
