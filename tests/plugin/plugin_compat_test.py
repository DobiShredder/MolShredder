#!/usr/bin/env python3
"""Runs the SDK compatibility kit against good and deliberately broken plugins."""

import json
import pathlib
import shutil
import stat
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def check(kit, manifest, *flags, expect_pass=None):
    result = subprocess.run([kit, "--manifest", str(manifest), "--json", *flags],
                            capture_output=True, text=True, timeout=120)
    report = json.loads(result.stdout)
    require((result.returncode == 0) == report["passed"], "exit code must follow the report")
    if expect_pass is not None:
        require(report["passed"] is expect_pass,
                f"{manifest}: expected passed={expect_pass}: {json.dumps(report['checks'], indent=1)}")
    return {c["name"]: c for c in report["checks"]}, report


def main() -> int:
    kit, worker, libraries, sdk, output = sys.argv[1:6]
    kit_path = pathlib.Path(kit)
    root = pathlib.Path(output) / "plugin-compat"
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    sdk = pathlib.Path(sdk)
    libraries = dict(line.split("=", 1) for line in pathlib.Path(libraries).read_text().splitlines() if "=" in line)

    # Python SDK sample: pinned files, launcher, synthetic molecule.
    py_dir = root / "python"
    py_dir.mkdir()
    shutil.copy(sdk / "samples/python_worker/plugin.py", py_dir / "plugin.py")
    shutil.copy(sdk / "python/molshredder_plugin.py", py_dir / "molshredder_plugin.py")
    launcher = py_dir / "run.sh"
    launcher.write_text(f'#!/bin/sh\nexec "{sys.executable}" "$MOLSHREDDER_PLUGIN_DIR/plugin.py"\n')
    launcher.chmod(launcher.stat().st_mode | stat.S_IXUSR)
    manifest = json.loads((sdk / "samples/python_worker/molshredder-plugin.json").read_text())
    manifest["examples"] = [
        {"operation": "centroid", "arguments": {"selection": "all"}},
        {"operation": "radius-of-gyration", "arguments": {}},
        {"operation": "element-table", "arguments": {}},
    ]
    (py_dir / "molshredder-plugin.json").write_text(json.dumps(manifest))
    checks, report = check(kit_path, py_dir / "molshredder-plugin.json", expect_pass=True)
    require(all(c["status"] == "pass" for c in checks.values()), f"python sample: {report}")
    require("example 'centroid' runs" in checks and "example 'element-table' runs" in checks and
            "example 'radius-of-gyration' is repeatable on the same input" in checks,
            "every example is run and checked for repeatability")
    require("summary: centroid of 12 atoms" in checks["example 'centroid' runs"]["detail"],
            "the synthetic molecule has 12 atoms")

    # C++ worker with an example.
    cpp_dir = root / "worker"
    cpp_dir.mkdir()
    shutil.copy(worker, cpp_dir / "worker")
    ops = ["echo", "sum", "centroid", "fail", "crash", "hang", "flood", "badjson", "wait-cancel", "badid", "table",
           "environment", "directory", "nested"]
    base = {"schema_version": 1, "id": "org.example.worker", "namespace": "sample", "version": "1.0.0",
            "license": "MIT", "sdk": {"abi_major": 1, "abi_minor": 0},
            "execution": {"mode": "worker", "command": ["worker"]},
            "required_capabilities": [], "optional_capabilities": ["progress", "host.selection.atoms"],
            "operations": [{"name": n, "parameters": ([{"name": "text", "type": "text", "required": True}] if n == "echo" else [])}
                           for n in ops]}
    good = dict(base, examples=[{"operation": "echo", "arguments": {"text": "hi"}},
                                {"operation": "centroid", "arguments": {}}])
    (cpp_dir / "molshredder-plugin.json").write_text(json.dumps(good))
    check(kit_path, cpp_dir / "molshredder-plugin.json", expect_pass=True)

    def broken(name, mutate, expected_failure):
        directory = root / name
        directory.mkdir()
        shutil.copy(worker, directory / "worker")
        value = json.loads(json.dumps(good))
        mutate(value, directory)
        (directory / "molshredder-plugin.json").write_text(json.dumps(value))
        checks, report = check(kit_path, directory / "molshredder-plugin.json", expect_pass=False)
        failed = [n for n, c in checks.items() if c["status"] == "fail"]
        require(any(expected_failure in n for n in failed), f"{name}: expected a failing '{expected_failure}', got {failed}")
        return checks

    broken("invalid-manifest", lambda m, d: m.pop("license"), "manifest is valid")
    broken("newer-sdk", lambda m, d: m["sdk"].update(abi_minor=9), "negotiate")
    broken("missing-capability", lambda m, d: m.update(required_capabilities=["quantum.teleport"]), "negotiate")
    broken("missing-entry", lambda m, d: (d / "worker").unlink(), "entry file")
    broken("wrong-identity", lambda m, d: m.update(id="org.example.somebody-else"), "handshake")
    broken("failing-example", lambda m, d: m.update(examples=[{"operation": "fail", "arguments": {}}]), "example 'fail' runs")
    broken("crashing-example", lambda m, d: m.update(examples=[{"operation": "crash", "arguments": {}}]), "example 'crash' runs")
    broken("handshake-operations", lambda m, d: m["execution"].update(command=["worker", "--mode=badops"]), "handshake")
    broken("protocol-major", lambda m, d: m["execution"].update(command=["worker", "--mode=badprotocol"]), "handshake")

    # In-process: needs the explicit flag; broken variants are reported.
    def native(name, library):
        directory = root / name
        directory.mkdir()
        shutil.copy(library, directory / pathlib.Path(library).name)
        value = {"schema_version": 1, "id": "org.example.native", "namespace": "native", "version": "1.0.0",
                 "license": "GPL-3.0-or-later", "sdk": {"abi_major": 1, "abi_minor": 0},
                 "execution": {"mode": "in_process", "library": pathlib.Path(library).name},
                 "optional_capabilities": ["progress", "host.selection.atoms"],
                 "operations": [{"name": n, "parameters": ([{"name": "text", "type": "text", "required": True}]
                                                           if n == "echo" else [])}
                                for n in ["echo", "sum", "spin", "atoms", "fail"]],
                 "examples": [{"operation": "echo", "arguments": {"text": "x"}},
                              {"operation": "sum", "arguments": {}}, {"operation": "atoms", "arguments": {}}]}
        (directory / "molshredder-plugin.json").write_text(json.dumps(value))
        return directory / "molshredder-plugin.json"

    manifest_path = native("native-good", libraries["good"])
    refused, _ = check(kit_path, manifest_path, expect_pass=False)
    require(refused["files can be pinned for approval"]["status"] == "fail" and
            "allow-in-process" in refused["files can be pinned for approval"]["detail"],
            "an in-process plugin needs --allow-in-process even for the kit")
    checks, report = check(kit_path, manifest_path, "--allow-in-process", expect_pass=True)
    require(checks["example 'atoms' runs"]["status"] == "pass" and
            "summary: host data" in checks["example 'atoms' runs"]["detail"],
            "the in-process example gets the synthetic host data")
    for variant in ("abi_major", "abi_minor", "small_struct", "bad_describe", "no_entry", "fail_entry"):
        path = native("native-" + variant, libraries[variant])
        checks, _ = check(kit_path, path, "--allow-in-process", expect_pass=False)
        require(checks["library loads and its description matches"]["status"] == "fail",
                f"{variant} must fail the load check")
    path = native("native-bad-result", libraries["bad_result"])
    checks, _ = check(kit_path, path, "--allow-in-process", expect_pass=False)
    require(checks["example 'echo' runs"]["status"] == "fail", "a non-JSON result fails the example")

    # Usage errors.
    require(subprocess.run([kit]).returncode == 2, "missing arguments is a usage error")
    text = subprocess.run([kit, "--manifest", str(py_dir / "molshredder-plugin.json")], capture_output=True, text=True)
    require(text.returncode == 0 and "compatible with SDK 1.0" in text.stdout, "human-readable report")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
