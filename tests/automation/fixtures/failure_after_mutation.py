import sys

import molshredder


loaded = molshredder.invoke(
    "load", {"path": sys.argv[1], "name": "script_failure"}
)
if loaded["status"] != "ok":
    raise RuntimeError(f"load failed: {loaded}")

print("stdout-before-failure")
print("stderr-before-failure", file=sys.stderr)
raise RuntimeError("intentional-script-failure")
