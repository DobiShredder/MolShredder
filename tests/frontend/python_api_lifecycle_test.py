#!/usr/bin/env python3

import importlib
import pathlib
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require(len(sys.argv) == 4, "expected module, structure and embedded script")
    module = pathlib.Path(sys.argv[1]).resolve()
    structure = pathlib.Path(sys.argv[2]).resolve()
    embedded_script = pathlib.Path(sys.argv[3]).resolve()
    sys.path.insert(0, str(module.parent))
    molshredder = importlib.import_module("molshredder")

    initial = molshredder.runtime_info()
    require(initial["mode"] == "headless", "extension must own a headless runtime")
    reset = molshredder.reset_runtime()
    require(
        reset["generation"] == initial["generation"] + 1,
        "runtime generation did not advance",
    )

    events: list[tuple[str, str]] = []

    def callback(event: dict) -> None:
        events.append((event["command"], event["status"]))
        # Reentrant dispatch is allowed, but recursive callback delivery is
        # suppressed until the outer callback snapshot is complete.
        if event["command"].startswith('invoke "load"'):
            nested = molshredder.invoke("object list")
            require(nested["status"] == "ok", "reentrant read operation failed")

    token = molshredder.subscribe(callback)
    loaded = molshredder.invoke("load", {"path": str(structure), "name": "api"})
    require(loaded["status"] == "ok", "structure load failed")
    require(len(events) == 1 and events[0][1] == "ok", "callback ordering drifted")

    view = molshredder.coordinate_view()
    buffer = memoryview(view)
    require(buffer.readonly, "coordinate buffer must be read-only")
    require(buffer.ndim == 2 and buffer.shape == (3, 3), "coordinate shape drifted")
    require(buffer.format in ("f", "d"), "coordinate precision is not numeric")
    before_reset = buffer.tolist()
    require(view.object_id == loaded["data"]["object_id"], "view identity drifted")

    embedded = molshredder.run_script(str(embedded_script), trusted=True)
    require(embedded["status"] == "ok", "embedded lifecycle fixture failed")
    require(
        embedded["data"]["stdout"] == "embedded-coordinate-atoms=3\n",
        "embedded coordinate view output drifted",
    )

    delivered_before_unsubscribe = len(events)
    require(molshredder.unsubscribe(token), "subscription token was not removed")
    require(not molshredder.unsubscribe(token), "unsubscribe was not idempotent")
    molshredder.invoke("object list")
    require(
        len(events) == delivered_before_unsubscribe,
        "unsubscribed callback was delivered",
    )

    bad_token = molshredder.subscribe(
        lambda _event: (_ for _ in ()).throw(ValueError("callback-fixture"))
    )
    result = molshredder.invoke("object list")
    require(result["status"] == "ok", "callback failure changed operation result")
    errors = molshredder.callback_errors()
    require(
        len(errors) == 1
        and errors[0]["token"] == bad_token
        and errors[0]["type"] == "ValueError",
        "callback failure was not isolated",
    )
    require(molshredder.callback_errors() == [], "callback error drain failed")
    molshredder.unsubscribe(bad_token)

    final = molshredder.reset_runtime()
    require(not final["workspace_active"], "reset retained the old Workspace")
    require(
        buffer.tolist() == before_reset and view.atom_count == 3,
        "immutable coordinate view did not retain its snapshot lifetime",
    )

    print("python-api-lifecycle-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
