#!/usr/bin/env python3
"""Plugin SDK end to end through the Python module and the CLI.

Approval, discovery that runs nothing, a typed operation visible to every
frontend after the next start, host data access checked against the core
`analyze center`, tamper and revoke handling.
"""

import json
import os
import pathlib
import shutil
import subprocess
import sys

from console_script import portable_console_script


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


MANIFEST = """{
  "schema_version": 1, "id": "org.example.worker", "namespace": "sample", "name": "Sample worker",
  "version": "1.0.0", "license": "MIT", "sdk": {"abi_major": 1, "abi_minor": 0},
  "execution": {"mode": "worker", "command": ["worker", "--marker=%(marker)s"]},
  "required_capabilities": ["progress"], "optional_capabilities": ["host.selection.atoms"],
  "operations": [
    {"name": "echo", "summary": "Echo text", "parameters": [
      {"name": "text", "type": "text", "required": true},
      {"name": "times", "type": "unsigned_integer", "default": "1"}]},
    {"name": "centroid", "summary": "Centroid of a selection",
     "parameters": [{"name": "selection", "type": "text", "default": "all"}]},
    {"name": "sum"}, {"name": "fail"}, {"name": "crash"}, {"name": "hang"}, {"name": "flood"},
    {"name": "badjson"}, {"name": "wait-cancel"}, {"name": "badid"}, {"name": "table"},
    {"name": "environment"}, {"name": "directory"}, {"name": "nested"}]
}"""


def child_python(module_path: pathlib.Path, env: dict, code: str) -> dict:
    result = subprocess.run([sys.executable, "-c", code], env=env, capture_output=True, text=True,
                            check=True, cwd=str(module_path.parent))
    return json.loads(result.stdout.strip().splitlines()[-1])


def cli_envelope(cli_path, env, line: str) -> dict:
    script = f"format json\n{line}\nexit\n"
    completed = subprocess.run([str(cli_path), "console"], input=portable_console_script(script),
                               text=True, capture_output=True, check=True, env=env)
    envelopes = [json.loads(row[row.find('{"schema_version"'):])
                 for row in completed.stdout.splitlines() if '{"schema_version"' in row]
    require(len(envelopes) == 1, f"expected one CLI envelope: {completed.stdout[-400:]}")
    return envelopes[0]


def main() -> int:
    require(len(sys.argv) == 7, "expected module, CLI, worker, in-process library, fixture and output directory")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    worker = pathlib.Path(sys.argv[3]).resolve()
    native_library = pathlib.Path(sys.argv[4]).resolve()
    fixture = pathlib.Path(sys.argv[5]).resolve()
    output = pathlib.Path(sys.argv[6]).resolve() / "plugin-frontend"
    shutil.rmtree(output, ignore_errors=True)
    plugins = output / "plugins"
    directory = plugins / "sample"
    directory.mkdir(parents=True)
    shutil.copy(worker, directory / "worker")
    marker = directory / "started.log"
    (directory / "molshredder-plugin.json").write_text(MANIFEST % {"marker": marker}, encoding="utf-8")
    store = output / "trust.json"
    env = dict(os.environ, MOLSHREDDER_PLUGIN_TRUST_STORE=str(store))
    os.environ["MOLSHREDDER_PLUGIN_TRUST_STORE"] = str(store)

    sys.path.insert(0, str(module_path.parent))
    import importlib
    molshredder = importlib.import_module("molshredder")
    manifest_path = str(directory / "molshredder-plugin.json")

    # Discovery lists the plugin without running it.
    listed = molshredder.invoke("plugin list", {"directory": str(plugins)})
    require(listed["status"] == "ok" and listed["data"]["count"] == 1 and
            listed["data"]["plugins"][0]["state"] == "unapproved" and not marker.exists(),
            f"discovery must not execute anything: {listed}")
    refused = molshredder.invoke("plugin invoke", {"plugin": "org.example.worker", "operation": "echo",
                                                   "arguments-json": '{"text":"x"}'})
    require(refused["status"] == "error" and not marker.exists(), "an unapproved plugin must not run")

    # In-process approval needs an explicit flag; a worker plugin rejects it.
    bad_flag = molshredder.invoke("plugin approve", {"manifest": manifest_path, "allow-in-process": "true"})
    require(bad_flag["status"] == "error", "the in-process flag does not apply to workers")
    approved = molshredder.invoke("plugin approve", {"manifest": manifest_path})
    require(approved["status"] == "ok" and approved["data"]["mode"] == "worker" and
            len(approved["data"]["manifest_sha256"]) == 64 and
            "plugin sample echo" in approved["data"]["operations"], f"approve failed: {approved}")
    listed = molshredder.invoke("plugin list", {})
    require(listed["data"]["plugins"][0]["state"] == "approved", "approved plugin is listed")

    # Generic invocation works right away.
    echoed = molshredder.invoke("plugin invoke", {"plugin": "org.example.worker", "operation": "echo",
                                                  "arguments-json": '{"text":"ab","times":2}'})
    require(echoed["status"] == "ok" and echoed["data"]["text"] == "abab" and
            echoed["data"]["plugin"]["id"] == "org.example.worker", f"echo failed: {echoed}")

    # Host data: the plugin's centroid equals the core analysis of the same selection.
    molshredder.invoke("load", {"path": str(fixture), "name": "protein"})
    core = molshredder.invoke("analyze center", {"selection": "name CA", "mode": "centroid"})["data"]["position"]
    plugin = molshredder.invoke("plugin invoke", {"plugin": "org.example.worker", "operation": "centroid",
                                                  "arguments-json": '{"selection":"name CA"}'})
    require(plugin["status"] == "ok" and plugin["data"]["count"] == 76, f"centroid failed: {plugin}")
    core_xyz = core if isinstance(core, list) else [core["x"], core["y"], core["z"]]
    require(all(abs(a - b) < 1e-6 for a, b in zip(plugin["data"]["centroid"], core_xyz)),
            f"plugin centroid {plugin['data']['centroid']} differs from core {core_xyz}")
    empty = molshredder.invoke("plugin invoke", {"plugin": "org.example.worker", "operation": "centroid",
                                                 "arguments-json": '{"selection":"resn NOSUCHRESIDUE"}'})
    require(empty["status"] == "error" or empty["data"]["count"] == 0, "an empty selection must not crash the host")

    # Errors from the plugin and a crash keep the host alive.
    failed = molshredder.invoke("plugin invoke", {"plugin": "org.example.worker", "operation": "fail"})
    require(failed["status"] == "error" and failed["error"]["message"] == "boom", "plugin error is reported")
    crashed = molshredder.invoke("plugin invoke", {"plugin": "org.example.worker", "operation": "crash"})
    require(crashed["status"] == "error" and "exited" in crashed["error"]["message"], "a crash is reported")
    again = molshredder.invoke("plugin invoke", {"plugin": "org.example.worker", "operation": "echo",
                                                 "arguments-json": '{"text":"up"}'})
    require(again["status"] == "ok", "the host and plugin recover after a crash")
    status = molshredder.invoke("plugin status", {})
    require(status["status"] == "ok" and status["data"]["loaded"][0]["id"] == "org.example.worker",
            "status shows the loaded worker")

    # Typed operations appear in every frontend after the next start.
    code = ("import json,sys\nimport molshredder\n"
            "r=molshredder.invoke('plugin sample echo', {'text':'hi','times':'3'})\n"
            "print(json.dumps(r))")
    typed_python = child_python(module_path, env, code)
    require(typed_python["status"] == "ok" and typed_python["data"]["text"] == "hihihi",
            f"typed operation in a new Python process: {typed_python}")
    typed_cli = cli_envelope(cli_path, env, 'invoke "plugin sample echo" --text "hi" --times "3"')
    require(typed_cli["status"] == "ok" and typed_cli["data"] == typed_python["data"],
            "CLI and Python typed operations return the same data")
    bad_value = child_python(module_path, env, "import json,molshredder\n"
                             "print(json.dumps(molshredder.invoke('plugin sample echo', {'text':'x','times':'abc'})))")
    missing = child_python(module_path, env, "import json,molshredder\n"
                           "print(json.dumps(molshredder.invoke('plugin sample echo', {})))")
    require(bad_value["status"] == "error" and missing["status"] == "error",
            "typed arguments are validated by the registry before the plugin runs")
    startup = child_python(module_path, env, "import json,molshredder\n"
                           "print(json.dumps(molshredder.invoke('plugin status', {})))")
    require(startup["data"]["startup"][0]["status"] == "registered" and
            "plugin sample centroid" in startup["data"]["startup"][0]["operations"],
            "startup reports the registered operations")

    # An in-process plugin needs the explicit flag and reads host data too.
    native = plugins / "native"
    native.mkdir()
    shutil.copy(native_library, native / native_library.name)
    (native / "molshredder-plugin.json").write_text(
        '{"schema_version":1,"id":"org.example.native","namespace":"native","version":"1.0.0",'
        '"license":"GPL-3.0-or-later","sdk":{"abi_major":1,"abi_minor":0},'
        '"execution":{"mode":"in_process","library":"%s"},'
        '"optional_capabilities":["host.selection.atoms","progress"],'
        '"operations":[{"name":"echo","parameters":[{"name":"text","type":"text","required":true}]},'
        '{"name":"sum"},{"name":"spin"},{"name":"fail"},'
        '{"name":"atoms","parameters":[{"name":"selection","type":"text","default":"all"}]}]}'
        % native_library.name, encoding="utf-8")
    native_manifest = str(native / "molshredder-plugin.json")
    require(molshredder.invoke("plugin approve", {"manifest": native_manifest})["status"] == "error",
            "an in-process plugin is not approved without --allow-in-process")
    native_approved = molshredder.invoke("plugin approve", {"manifest": native_manifest, "allow-in-process": "true"})
    require(native_approved["status"] == "ok" and native_approved["data"]["mode"] == "in_process",
            f"in-process approval: {native_approved}")
    atoms = molshredder.invoke("plugin invoke", {"plugin": "org.example.native", "operation": "atoms",
                                                 "arguments-json": '{"selection":"name CA"}'})
    require(atoms["status"] == "ok" and atoms["data"]["host"]["count"] == 76 and
            atoms["data"]["host"]["object"] == "protein", f"in-process host data: {atoms}")
    native_echo = molshredder.invoke("plugin invoke", {"plugin": "org.example.native", "operation": "echo",
                                                       "arguments-json": '{"text":"hello"}'})
    require(native_echo["status"] == "ok" and native_echo["data"]["text"] == "hello", "in-process echo")
    typed_native = cli_envelope(cli_path, env, 'invoke "plugin native echo" --text "cli"')
    require(typed_native["status"] == "ok" and typed_native["data"]["text"] == "cli",
            "the in-process typed operation is visible in the CLI")

    # Tampering after approval is refused for the typed operation too.
    with open(directory / "worker", "ab") as handle:
        handle.write(b"tampered")
    starts = marker.read_text().count("started")
    typed_gone = child_python(module_path, env, code)
    require(typed_gone["status"] == "error" and typed_gone["error"]["code"] == "not_found",
            f"a changed plugin must not expose typed operations: {typed_gone}")
    generic = child_python(
        module_path, env,
        "import json,molshredder\nprint(json.dumps(molshredder.invoke('plugin invoke', "
        "{'plugin':'org.example.worker','operation':'echo','arguments-json':'{\"text\":\"x\"}'})))")
    require(generic["status"] == "error" and "changed since approval" in generic["error"]["message"] and
            marker.read_text().count("started") == starts,
            f"a modified worker must not start: {generic}")
    startup = child_python(module_path, env, "import json,molshredder\n"
                           "print(json.dumps(molshredder.invoke('plugin status', {})))")
    require(startup["data"]["startup"][0]["status"] == "skipped", "a changed plugin is skipped at startup")

    # Revoke removes it everywhere.
    revoked = molshredder.invoke("plugin revoke", {"id": "org.example.worker"})
    require(revoked["status"] == "ok", "revoke works")
    require(molshredder.invoke("plugin revoke", {"id": "org.example.native"})["status"] == "ok",
            "revoke works for the in-process plugin")
    require(molshredder.invoke("plugin invoke", {"plugin": "org.example.worker", "operation": "echo",
                                                 "arguments-json": '{"text":"x"}'})["status"] == "error",
            "a revoked plugin cannot run")
    require(child_python(module_path, env, "import json,molshredder\n"
                         "print(json.dumps(molshredder.invoke('plugin status', {})))")["data"]["startup"] == [],
            "a revoked plugin is not registered at startup")
    # A malformed trust store is an error, never treated as empty.
    store.write_text("{broken", encoding="utf-8")
    broken = molshredder.invoke("plugin list", {})
    require(broken["status"] == "error", "a malformed trust store is reported")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
