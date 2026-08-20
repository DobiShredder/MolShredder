import molshredder


loaded = molshredder.invoke(
    "load",
    {
        "path": "../../io/fixtures/synthetic_multimodel.pdb",
        "name": "gui_script",
    },
)
if loaded["status"] != "ok":
    raise RuntimeError(f"load failed: {loaded}")

print("gui-script-output")
