#!/usr/bin/env python3
"""VMD Tcl adapter parity: Python and CLI see the same translation and run result."""

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

    script = (f'set f {{{fixture}}}\nset m [mol new $f type pdb waitfor all]\n'
              'display projection Orthographic\ncolor Display Background white\n'
              'mol rename $m protein')
    translated = molshredder.invoke("tcl translate", {"text": script})
    require(translated["status"] == "ok", f"translate failed: {translated}")
    data = translated["data"]
    require(data["language"] == "tcl" and data["command_count"] == 5 and
            data["translated_commands"] == 4 and data["failed_commands"] == 0 and
            data["invocation_count"] == 4 and len(data["source_sha256"]) == 64,
            f"unexpected translation summary {data}")

    one_line = "display resetview; display projection Perspective"
    py_pure = molshredder.invoke("tcl translate", {"text": one_line})
    cli_pure = cli_envelope(cli_path, f'invoke "tcl translate" --text "{one_line}"')
    require(cli_pure["status"] == "ok" and cli_pure["data"] == py_pure["data"],
            "CLI and Python translations diverged")

    ran = molshredder.invoke("tcl run", {"text": script})
    require(ran["status"] == "ok" and ran["data"]["executed_invocations"] == 4 and
            ran["data"]["failed_commands"] == 0, f"run failed: {ran}")
    table = molshredder.invoke("object list", {})["data"]["table"]
    names = [row[table["columns"].index("name")] for row in table["rows"]]
    require(names == ["protein"], f"load and rename must act on the object: {names}")

    # An unsupported command stops by default and is skipped with continue.
    text = "display resetview\nmol representation Lines\ndisplay projection Orthographic"
    stopped = molshredder.invoke("tcl run", {"text": text})
    require(stopped["status"] == "error" and
            stopped["error"]["details"]["failed_line"] == "2" and
            stopped["error"]["details"]["executed_invocations"] == "1",
            f"unexpected failure report {stopped}")
    carried = molshredder.invoke("tcl run", {"text": text, "on-error": "continue"})
    require(carried["status"] == "ok" and carried["data"]["failed_commands"] == 1 and
            carried["data"]["executed_invocations"] == 2, "continue policy")

    # A Tcl error aborts the script whatever the policy says.
    tcl_error = molshredder.invoke(
        "tcl run", {"text": "display resetview\nmol delete $nosuch\ndisplay resetview",
                    "on-error": "continue"})
    require(tcl_error["status"] == "error" and
            tcl_error["error"]["details"]["executed_invocations"] == "1",
            f"a Tcl error must abort the script: {tcl_error}")

    exited = molshredder.invoke("tcl run", {"text": "display resetview\nexit\ndisplay resetview"})
    require(exited["status"] == "ok" and exited["data"]["stopped"] is True and
            exited["data"]["executed_invocations"] == 1, "exit must stop the script")

    # Code execution and control flow are refused, never run.
    marker = output / "tcl-must-not-exist"
    marker.unlink(missing_ok=True)
    for refused in (f"exec touch {marker}", f"open {marker} w", "foreach i {1 2} { display resetview }"):
        result = molshredder.invoke("tcl run", {"text": refused})
        require(result["status"] == "error", f"must be refused: {refused!r} -> {result}")
    require(not marker.exists(), "a refused command must not run")

    path = output / "frontend.tcl"
    path.write_text("display resetview\n", encoding="utf-8")
    from_file = molshredder.invoke("tcl translate", {"path": str(path)})
    require(from_file["status"] == "ok" and from_file["data"]["source"] == "path",
            "file source")
    both = molshredder.invoke("tcl translate", {"path": str(path), "text": "exit"})
    require(both["status"] == "error", "exactly one of path and text is required")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
