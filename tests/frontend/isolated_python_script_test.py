#!/usr/bin/env python3

import importlib
import json
import os
import pathlib
import subprocess
import sys
import time


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require(len(sys.argv) == 8, "expected module, CLI and five script fixtures")
    module = pathlib.Path(sys.argv[1]).resolve()
    cli = pathlib.Path(sys.argv[2]).resolve()
    success, failure, timeout, malformed, environment = map(
        lambda value: pathlib.Path(value).resolve(), sys.argv[3:]
    )

    completed = subprocess.run(
        [
            str(cli), "script", "run-isolated", "--path", str(success),
            "--arguments-json", '["cli"]', "--trust", "true",
            "--timeout-ms", "2000", "--format", "json",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    require(completed.returncode == 0, f"isolated CLI failed: {completed.stderr}")
    cli_result = json.loads(completed.stdout)
    require(
        cli_result["data"]["stdout"] == "isolated-argument=cli\n"
        and cli_result["data"]["stderr"] == "isolated-stderr\n"
        and cli_result["data"]["mutations_committed"] == 0,
        "isolated CLI protocol drifted",
    )

    sys.path.insert(0, str(module.parent))
    molshredder = importlib.import_module("molshredder")
    untrusted = molshredder.run_script_isolated(str(success))
    require(
        untrusted["status"] == "error"
        and "explicit trust" in untrusted["error"]["message"],
        "isolated trust gate did not fail closed",
    )
    succeeded = molshredder.run_script_isolated(
        str(success), ["python"], trusted=True, timeout_ms=2000
    )
    require(
        succeeded["status"] == "ok"
        and succeeded["data"]["protocol_version"] == 1
        and succeeded["data"]["isolation"] == "process",
        "isolated Python success contract drifted",
    )
    failed = molshredder.run_script_isolated(
        str(failure), trusted=True, timeout_ms=2000
    )
    require(
        failed["status"] == "error"
        and failed["error"]["code"] == "script_failed"
        and failed["error"]["details"]["error_type"] == "RuntimeError"
        and "isolated-fixture-failure" in failed["error"]["details"]["stderr"],
        "isolated exception provenance drifted",
    )
    timed_out = molshredder.run_script_isolated(
        str(timeout), trusted=True, timeout_ms=50
    )
    require(
        timed_out["status"] == "error"
        and timed_out["error"]["details"]["timed_out"] == "true",
        "isolated timeout did not kill the child",
    )
    malformed_result = molshredder.run_script_isolated(
        str(malformed), trusted=True, timeout_ms=2000
    )
    require(
        malformed_result["status"] == "error"
        and "malformed protocol" in malformed_result["error"]["message"],
        "malformed child protocol was accepted",
    )
    os.environ["MOLSHREDDER_ISOLATED_SECRET"] = "must-not-leak"
    minimal = molshredder.run_script_isolated(
        str(environment), trusted=True, timeout_ms=2000
    )
    inherited = molshredder.run_script_isolated(
        str(environment), trusted=True, timeout_ms=2000,
        environment_policy="inherit",
    )
    require(
        minimal["data"]["stdout"] == "missing\n"
        and minimal["data"]["environment_policy"] == "minimal",
        "minimal child environment leaked an unapproved variable",
    )
    require(
        inherited["data"]["stdout"] == "must-not-leak\n"
        and inherited["data"]["environment_policy"] == "inherit",
        "explicit inherited environment policy was not honored",
    )

    async_events: list[str] = []
    async_token = molshredder.subscribe(
        lambda event: async_events.append(event["command"])
    )
    task = molshredder.run_script_isolated_async(
        str(success), ["async"], trusted=True, timeout_ms=2000
    )
    try:
        task.result(timeout_ms=0)
    except RuntimeError as error:
        require("timed out" in str(error), "async wait returned wrong timeout")
    async_result = task.result(timeout_ms=2000)
    require(
        task.done
        and task.progress == 1.0
        and async_result["status"] == "ok"
        and async_result["data"]["stdout"] == "isolated-argument=async\n",
        "async isolated success lifecycle drifted",
    )
    task.result()
    require(len(async_events) == 1, "async completion event was delivered twice")
    task.close()
    molshredder.unsubscribe(async_token)

    cancelled_task = molshredder.run_script_isolated_async(
        str(timeout), trusted=True, timeout_ms=5000
    )
    time.sleep(0.05)
    require(cancelled_task.cancel(), "running async task rejected cancellation")
    cancelled_result = cancelled_task.result(timeout_ms=2000)
    require(
        cancelled_result["status"] == "error"
        and cancelled_result["error"]["code"] == "cancelled",
        "async cancellation did not kill the child",
    )
    require(not cancelled_task.cancel(), "completed task accepted cancellation")
    cancelled_task.close()
    require(
        molshredder.invoke("object list")["data"]["object_count"] == 0,
        "isolated scripts mutated the parent Workspace",
    )

    print("isolated-python-script-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
