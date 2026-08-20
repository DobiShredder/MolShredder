import sys

import molshredder


loaded = molshredder.invoke(
    "load", {"path": sys.argv[1], "name": "script_success"}
)
if loaded["status"] != "ok":
    raise RuntimeError(f"load failed: {loaded}")

version = molshredder.invoke("version")
if version["status"] != "ok":
    raise RuntimeError(f"version failed: {version}")

print(f"script-argument={sys.argv[2]}")
