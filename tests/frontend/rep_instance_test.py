#!/usr/bin/env python3
"""SEQ-318 S6: VMD-style representation instances (mol addrep/modrep/delrep/... as `rep ...`)."""

import importlib
import pathlib
import struct
import sys
import tempfile
import zlib


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def ok(result: dict, what: str) -> dict:
    require(result["status"] == "ok", f"{what} failed: {result.get('error')}")
    return result["data"]


def fails(result: dict, what: str) -> str:
    require(result["status"] == "error", f"{what} should have failed")
    return result["error"]["message"]


def read_png(path: pathlib.Path):
    data = path.read_bytes()
    require(data[:8] == b"\x89PNG\r\n\x1a\n", "png signature")
    offset, header, idat = 8, None, b""
    while offset < len(data):
        length, kind = struct.unpack(">I4s", data[offset:offset + 8])
        body = data[offset + 8:offset + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        offset += 12 + length
    width, height = header[0], header[1]
    raw = zlib.decompress(idat)
    stride = width * 4
    pixels = []
    for row in range(height):
        line = raw[row * (stride + 1):(row + 1) * (stride + 1)]
        require(line[0] == 0, "png filter type 0")
        pixels.append(line[1:])
    return width, height, pixels


def dominant(path: pathlib.Path):
    width, height, rows = read_png(path)
    background = rows[0][0:3]
    red = green = blue = count = 0
    for row in rows:
        for x in range(width):
            r, g, b = row[4 * x], row[4 * x + 1], row[4 * x + 2]
            if (r, g, b) != tuple(background):
                red, green, blue, count = red + r, green + g, blue + b, count + 1
    require(count > 0, "the image shows something")
    return red / count, green / count, blue / count


def main() -> int:
    module_path = pathlib.Path(sys.argv[1]).resolve()
    fixtures = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")
    ok(m.invoke("load", {"path": str(fixtures / "1ubq.pdb"), "name": "ubq"}), "load")

    def reps(obj="current"):
        return ok(m.invoke("rep list", {"object": obj}), "rep list")["table"]["rows"]

    # defaults and the first instance
    defaults = ok(m.invoke("rep defaults"), "defaults")
    require(defaults["style"] == "Lines" and defaults["color"] == "Name" and defaults["selection"] == "all",
            f"VMD defaults: {defaults}")
    require(reps() == [], "a new object has no representation instances")
    added = ok(m.invoke("rep add", {"selection": "name CA", "style": "vdw", "color-id": "1"}), "add")
    require(added["index"] == 0 and added["name"] == "rep0", "first instance is rep0")
    row = reps()[0]
    require(row[2] == "VDW" and row[4] == "name CA" and row[5] == "ColorID" and row[6] == 1,
            f"canonical style and color: {row}")
    require(added["primitive_count"] == 76 and added["style_drawn"] is True, "one sphere per CA atom")

    # image colors follow the coloring method
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        ok(m.invoke("hide", {"representation": "everything"}), "hide the default representations")
        ok(m.invoke("image save", {"path": str(root / "red.png"), "width": "96"}), "image")
        red = dominant(root / "red.png")
        require(red[0] > 2 * red[2] and red[0] > 2 * red[1], f"ColorID 1 is red: {red}")
        ok(m.invoke("rep modify", {"rep": "0", "color-id": "0"}), "blue")
        ok(m.invoke("image save", {"path": str(root / "blue.png"), "width": "96"}), "image")
        blue = dominant(root / "blue.png")
        require(blue[2] > 2 * blue[0], f"ColorID 0 is blue: {blue}")

        # modify fields and show/hide
        ok(m.invoke("rep modify", {"rep": "rep0", "visible": "false"}), "hide")
        require(reps()[0][8] is False and reps()[0][13] == 0, "a hidden instance draws nothing")
        ok(m.invoke("rep modify", {"rep": "0", "visible": "true", "name": "cas", "style": "NewCartoon"}), "retarget")
        require(reps()[0][1] == "cas" and reps()[0][2] == "NewCartoon" and reps()[0][13] > 0, "rename and restyle")
        require(ok(m.invoke("rep index", {"name": "cas"}), "index")["index"] == 0 and
                ok(m.invoke("rep index", {"name": "none"}), "index")["index"] == -1, "rep index")

        # more instances, order and deletion
        ok(m.invoke("rep default", {"category": "style", "value": "Licorice"}), "default style")
        ok(m.invoke("rep default", {"category": "selection", "value": "resi 1-5"}), "default selection")
        second = ok(m.invoke("rep add"), "add with defaults")
        require(second["index"] == 1 and reps()[1][2] == "Licorice" and reps()[1][4] == "resi 1-5",
                "defaults feed rep add")
        surf = ok(m.invoke("rep add", {"style": "Surf"}), "unsupported style")
        require(surf["style_drawn"] is False and surf["primitive_count"] == 0,
                "styles without a renderer are kept but draw nothing")
        require(len(reps()) == 3, "three instances")
        left = ok(m.invoke("rep delete", {"rep": "1"}), "delete")
        require(left["rep_count"] == 2 and [r[1] for r in reps()] == ["cas", "rep2"],
                "deleting shifts the later instances and keeps names")

        # coloring methods draw
        for method in ("Name", "Element", "ResName", "ResID", "Chain", "Structure", "Beta", "Mass", "Index", "PosX"):
            changed = ok(m.invoke("rep modify", {"rep": "cas", "color": method}), f"color {method}")
            require(changed["primitive_count"] > 0 and changed["color_drawn"] is True, f"{method} colors atoms")
        user = ok(m.invoke("rep modify", {"rep": "cas", "color": "User"}), "User coloring")
        require(user["color_drawn"] is False, "unsupported coloring methods are flagged")
        ok(m.invoke("rep modify", {"rep": "cas", "scale-min": "0", "scale-max": "100", "color": "Beta"}), "scale range")
        ok(m.invoke("rep modify", {"rep": "cas", "scale-auto": "true"}), "scale auto")
        ok(m.invoke("rep modify", {"rep": "cas", "selection-update": "true", "color-update": "true",
                                    "smooth": "2", "draw-frames": "now"}), "update flags")

        # selection updates: a frozen selection keeps its atoms
        ok(m.invoke("rep modify", {"rep": "cas", "selection": "name CA", "selection-update": "false"}), "freeze")
        before = reps()[0][13]
        require(before > 0, "frozen instance draws")

        # errors
        require("style" in fails(m.invoke("rep add", {"style": "Bogus"}), "bad style"), "unknown style")
        require("coloring" in fails(m.invoke("rep modify", {"rep": "0", "color": "Bogus"}), "bad color"), "unknown color")
        require("material" in fails(m.invoke("rep modify", {"rep": "0", "material": "Bogus"}), "bad material"),
                "unknown material")
        require("does not exist" in fails(m.invoke("rep modify", {"rep": "9"}), "bad index"), "bad index")
        require("no representation" in fails(m.invoke("rep delete", {"rep": "nope"}), "bad name"), "bad name")
        require("exists" in fails(m.invoke("rep modify", {"rep": "cas", "name": "rep2"}), "duplicate"), "duplicate name")
        require("0 to 32" in fails(m.invoke("rep modify", {"rep": "0", "color-id": "40"}), "bad id"), "color id range")
        require(reps()[0][1] == "cas", "failed edits change nothing")

        # session journal replay
        expected = reps()
        session = root / "reps.mssession"
        ok(m.invoke("session save", {"path": str(session)}), "save")
        ok(m.invoke("rep delete", {"rep": "0"}), "scramble")
        ok(m.invoke("session load", {"path": str(session)}), "load")
        require(reps() == expected, f"representation instances replay from the session: {reps()} vs {expected}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
