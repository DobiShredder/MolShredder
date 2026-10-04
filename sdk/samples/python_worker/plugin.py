"""Sample MolShredder worker plugin: statistics over a selection of atoms."""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import molshredder_plugin as sdk  # noqa: E402


def _atoms(arguments, context):
    return context.host_request("selection.atoms", {"selection": arguments.get("selection", "all")})


def centroid(arguments, context):
    atoms = _atoms(arguments, context)
    count = atoms["count"]
    if count == 0:
        raise sdk.PluginError("the selection is empty", "invalid_argument", "choose a selection with atoms")
    sums = [0.0, 0.0, 0.0]
    for position in atoms["positions"]:
        for axis in range(3):
            sums[axis] += position[axis]
    return sdk.Result("centroid of %d atoms" % count,
                      {"centroid": [value / count for value in sums], "count": count, "unit": atoms["unit"]})


def radius_of_gyration(arguments, context):
    atoms = _atoms(arguments, context)
    count = atoms["count"]
    if count == 0:
        raise sdk.PluginError("the selection is empty", "invalid_argument")
    center = [sum(p[axis] for p in atoms["positions"]) / count for axis in range(3)]
    total = 0.0
    for index, position in enumerate(atoms["positions"]):
        total += sum((position[axis] - center[axis]) ** 2 for axis in range(3))
        if index % 1000 == 0:
            if context.cancelled:
                raise sdk.PluginError("stopped on request", "cancelled")
            context.progress(index / count, "summing")
    return sdk.Result("radius of gyration", {"radius_of_gyration": math.sqrt(total / count), "count": count})


def element_table(arguments, context):
    atoms = _atoms(arguments, context)
    counts = {}
    for number in atoms["atomic_numbers"]:
        counts[number] = counts.get(number, 0) + 1
    rows = [[number, counts[number]] for number in sorted(counts)]
    return sdk.Result("element counts", {"elements": len(rows)},
                      {"columns": ["atomic_number", "count"], "rows": rows})


sdk.serve("org.molshredder.sample-python", "1.0.0",
          {"centroid": centroid, "radius-of-gyration": radius_of_gyration, "element-table": element_table})
