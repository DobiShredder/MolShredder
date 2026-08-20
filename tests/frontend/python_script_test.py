#!/usr/bin/env python3

import importlib
import json
import os
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_cli(executable: pathlib.Path, arguments: list[str], code: int) -> dict:
    completed = subprocess.run(
        [str(executable), *arguments, "--format", "json"],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        completed.returncode == code,
        f"CLI returned {completed.returncode}, expected {code}: "
        f"stdout={completed.stdout!r} stderr={completed.stderr!r}",
    )
    payload = completed.stdout if code == 0 else completed.stderr
    require(bool(payload), "CLI did not emit a result envelope")
    return json.loads(payload)


def cli_arguments(script: pathlib.Path, structure: pathlib.Path, label: str) -> list[str]:
    return [
        "script",
        "run",
        "--path",
        str(script),
        "--arguments-json",
        json.dumps([str(structure), label], separators=(",", ":")),
        "--trust",
        "true",
    ]


def validate_success(result: dict, label: str) -> None:
    require(result["schema_version"] == 2, "script result must use envelope v2")
    require(result["status"] == "ok", "script must succeed")
    data = result["data"]
    require(data["stdout"] == f"script-argument={label}\n", "stdout was lost")
    require(data["stderr"] == "", "unexpected successful stderr")
    require(data["invocation_count"] == 2, "nested invocations were not counted")
    require(data["mutations_committed"] == 1, "mutation provenance was not counted")
    require(len(data["source_sha256"]) == 64, "source SHA-256 is missing")
    require(
        data["nested_invocations"][0].startswith('ok\tinvoke "load"'),
        "canonical nested load invocation is missing",
    )


def main() -> int:
    require(len(sys.argv) == 6, "expected module, CLI, scripts and structure paths")
    module = pathlib.Path(sys.argv[1]).resolve()
    cli = pathlib.Path(sys.argv[2]).resolve()
    success_script = pathlib.Path(sys.argv[3]).resolve()
    failure_script = pathlib.Path(sys.argv[4]).resolve()
    structure = pathlib.Path(sys.argv[5]).resolve()
    syntax_script = success_script.with_name("syntax_error.py")

    untrusted = run_cli(
        cli, ["script", "run", "--path", str(success_script), "--trust", "false"], 2
    )
    require(
        untrusted["error"]["code"] == "invalid_argument"
        and "explicit trust" in untrusted["error"]["message"],
        "untrusted scripts must fail before execution",
    )

    cli_success = run_cli(
        cli, cli_arguments(success_script, structure, "cli-api"), 0
    )
    validate_success(cli_success, "cli-api")

    cli_failure = run_cli(
        cli,
        [
            "script",
            "run",
            "--path",
            str(failure_script),
            "--arguments-json",
            json.dumps([str(structure)], separators=(",", ":")),
            "--trust",
            "true",
        ],
        2,
    )
    details = cli_failure["error"]["details"]
    require(cli_failure["error"]["code"] == "script_failed", "wrong runtime error")
    require(details["partial_mutation"] == "true", "partial mutation was hidden")
    require(details["mutations_committed"] == "1", "mutation count was lost")
    require(details["stdout"] == "stdout-before-failure\n", "failure stdout lost")
    require("intentional-script-failure" in details["stderr"], "traceback lost")
    require(details["nested_invocations"].startswith('ok\tinvoke "load"'), "history lost")

    syntax_failure = run_cli(
        cli, ["script", "run", "--path", str(syntax_script), "--trust", "true"], 2
    )
    require(
        syntax_failure["error"]["code"] == "script_failed"
        and "SyntaxError" in syntax_failure["error"]["details"]["stderr"],
        "syntax errors must retain their traceback",
    )

    sys.path.insert(0, str(module.parent))
    molshredder = importlib.import_module("molshredder")
    original_argv = list(sys.argv)
    original_cwd = os.getcwd()
    python_success = molshredder.run_script(
        str(success_script), [str(structure), "python-api"], trusted=True
    )
    validate_success(python_success, "python-api")
    require(sys.argv == original_argv, "script execution did not restore sys.argv")
    require(os.getcwd() == original_cwd, "script execution did not restore cwd")

    objects = molshredder.invoke("object list")
    require(
        objects["status"] == "ok"
        and any(row[1] == "script_success" for row in objects["data"]["table"]["rows"]),
        "script API did not mutate the same Python-module Workspace",
    )

    python_failure = molshredder.run_script(
        str(failure_script), [str(structure)], trusted=True
    )
    require(
        python_failure["status"] == "error"
        and python_failure["error"]["details"]["partial_mutation"] == "true",
        "Python API did not preserve structured partial failure",
    )

    print("python-script-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
