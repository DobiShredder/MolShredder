import molshredder


info = molshredder.runtime_info()
assert info["mode"] == "embedded"
assert info["reset_allowed"] is False

try:
    molshredder.reset_runtime()
except RuntimeError as error:
    assert "host owns" in str(error)
else:
    raise AssertionError("embedded runtime reset unexpectedly succeeded")

view = molshredder.coordinate_view()
assert memoryview(view).readonly
print(f"embedded-coordinate-atoms={view.atom_count}")
