#!/usr/bin/env python3
"""Image export parity and PyMOL 3.1.0 PNG/alpha reference checks (SEQ-311 S4)."""

import importlib
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib

from console_script import portable_console_script


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def decode_png(path: pathlib.Path) -> dict:
    data = path.read_bytes()
    require(data[:8] == b"\x89PNG\r\n\x1a\n", "PNG signature missing")
    position = 8
    idat = b""
    info: dict = {}
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        crc = struct.unpack(">I", data[position + 8 + length:position + 12 + length])[0]
        require(zlib.crc32(kind + body) & 0xFFFFFFFF == crc, f"bad CRC in {kind!r}")
        if kind == b"IHDR":
            width, height, depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", body)
            info.update(width=width, height=height, bit_depth=depth, color_type=color_type,
                        interlace=interlace)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"pHYs":
            info["pHYs"] = list(struct.unpack(">IIB", body))
        position += 12 + length
    raw = zlib.decompress(idat)
    stride = info["width"] * 4
    rows = [raw[row * (stride + 1) + 1:(row + 1) * (stride + 1)] for row in range(info["height"])]
    require(all(raw[row * (stride + 1)] == 0 for row in range(info["height"])), "unexpected filter")
    info["corner"] = list(rows[0][:4])
    info["opaque_red"] = sum(1 for row in rows for x in range(info["width"])
                             if row[x * 4 + 3] == 255 and row[x * 4] > row[x * 4 + 2])
    middle = rows[info["height"] // 2]
    info["center"] = list(middle[(info["width"] // 2) * 4:(info["width"] // 2) * 4 + 4])
    return info


def console_result(executable: pathlib.Path, fixture: pathlib.Path, output: pathlib.Path) -> dict:
    quoted = str(fixture).replace('"', '\\"')
    commands = [
        f'invoke "load" --file-format "g96" --path "{quoted}"',
        'invoke "show" --representation "spheres" --selection "all"',
        'invoke "view orient"',
        'invoke "view clip" --mode "slab" --distance "5"',
        'invoke "color" --color "red" --selection "all"',
        'invoke "bg_color" --color "blue"',
        f'invoke "png" --path "{output}" --width "64" --height "48"',
    ]
    completed = subprocess.run(
        [str(executable), "console"],
        input=portable_console_script("format json\n" + "\n".join(commands) + "\nexit\n"),
        check=True, capture_output=True, text=True)
    envelopes = []
    for line in completed.stdout.splitlines():
        marker = line.find("{")
        if marker < 0:
            continue
        try:
            envelopes.append(json.loads(line[marker:]))
        except json.JSONDecodeError:
            pass
    require(envelopes, "CLI image sequence emitted no result")
    return envelopes[-1]


def without_path(result: dict) -> dict:
    copy = json.loads(json.dumps(result))
    copy["data"].pop("path", None)
    copy.pop("command", None)
    return copy


def main() -> int:
    require(len(sys.argv) == 5, "expected module, CLI, GUI probe and fixture")
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    gui_path = pathlib.Path(sys.argv[3]).resolve()
    fixture = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")
    work = pathlib.Path(tempfile.mkdtemp(prefix="molshredder-image-"))

    for name, arguments in (
        ("load", {"file-format": "g96", "path": str(fixture)}),
        ("show", {"representation": "spheres", "selection": "all"}),
        ("view orient", {}),
        ("view clip", {"mode": "slab", "distance": "5"}),
        ("color", {"color": "red", "selection": "all"}),
        ("bg_color", {"color": "blue"}),
    ):
        require(molshredder.invoke(name, arguments)["status"] == "ok", f"setup failed: {name}")
    python_path = work / "python.png"
    python_result = molshredder.invoke(
        "image save", {"path": str(python_path), "width": "64", "height": "48"})
    cli_path_png = work / "cli.png"
    cli_result = console_result(cli_path, fixture, cli_path_png)
    gui_png = work / "gui.png"
    gui_result = json.loads(subprocess.run(
        [str(gui_path), "image", str(fixture), str(gui_png)],
        check=True, capture_output=True, text=True).stdout)
    require(without_path(python_result) == without_path(cli_result) == without_path(gui_result),
            "CLI, GUI adapter and Python image results diverged")
    require(python_path.read_bytes() == cli_path_png.read_bytes() == gui_png.read_bytes(),
            "frontends must write byte-identical images")

    # Reference: pymol-render-image-blackbox_20261003T060000Z (opaque_background off by
    # default -> transparent background keeping the background RGB, RGBA8, pHYs for dpi).
    info = decode_png(python_path)
    require(info["width"] == 64 and info["height"] == 48 and info["bit_depth"] == 8
            and info["color_type"] == 6 and info["interlace"] == 0, "PNG header drifted")
    require(info["corner"] == [0, 0, 255, 0], "default background must be transparent blue")
    require(info["opaque_red"] > 0, "sphere pixels must be opaque red")
    data = python_result["data"]
    require(data["renderer"] == "molshredder-reference-raycast" and not data["opaque_background"]
            and any("ray" in warning for warning in data["warnings"]),
            "image result must report the ray fallback")

    expected_alpha = {(0, -1): 0, (0, 0): 0, (0, 1): 255, (1, -1): 255, (1, 0): 0, (1, 1): 255}
    for (opaque, ray_opaque), alpha in expected_alpha.items():
        molshredder.invoke("setting set", {"name": "opaque_background",
                                           "value": "true" if opaque else "false"})
        molshredder.invoke("setting set", {"name": "ray_opaque_background", "value": str(ray_opaque)})
        path = work / f"alpha_{opaque}_{ray_opaque}.png"
        result = molshredder.invoke("image save", {"path": str(path), "width": "24", "height": "16"})
        require(result["status"] == "ok", "alpha image failed")
        corner = decode_png(path)["corner"]
        require(corner == [0, 0, 255, alpha],
                f"alpha drifted for opaque={opaque} ray={ray_opaque}: {corner}")
    draw = work / "draw.png"
    molshredder.invoke("setting set", {"name": "opaque_background", "value": "false"})
    molshredder.invoke("setting set", {"name": "ray_opaque_background", "value": "1"})
    molshredder.invoke("image save", {"path": str(draw), "width": "8", "height": "8", "ray": "false"})
    require(decode_png(draw)["corner"][3] == 0, "draw images must use opaque_background")

    dpi_path = work / "dpi.png"
    molshredder.invoke("image save", {"path": str(dpi_path), "width": "20", "height": "20", "dpi": "150"})
    require(decode_png(dpi_path)["pHYs"] == [5905, 5905, 1], "dpi must write pHYs pixels per metre")
    aspect_path = work / "aspect.png"
    aspect = molshredder.invoke("image save", {"path": str(aspect_path), "width": "10"})
    require(aspect["status"] == "ok" and aspect["data"]["height"] >= 1, "height must follow camera aspect")
    missing = molshredder.invoke("image save", {"path": str(work / "missing" / "x.png")})
    require(missing["status"] != "ok" and not (work / "missing").exists(),
            "missing output directories must fail explicitly")
    refreshed = molshredder.invoke("refresh", {})
    require(refreshed["status"] == "ok", "refresh alias must succeed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
