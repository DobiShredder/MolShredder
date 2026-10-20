"""A script that drives the open workspace; it needs the in-process mode of the Run Python script panel."""

import molshredder

result = molshredder.invoke("object list")
print("objects:", len(result["data"]["table"]["rows"]))
