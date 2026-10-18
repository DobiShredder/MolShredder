#!/usr/bin/env python3
"""SEQ-320 S2: `movie export-video` with the optional external ffmpeg adapter.

No real ffmpeg is needed: a stand-in executable records the argument vector it receives, checks that the numbered
PNG frames exist, and writes a small output file. A real ffmpeg run is not part of this test (no encoder was
installed when it was written); the argument vector follows the documented ffmpeg options.
"""

import importlib
import json
import os
import pathlib
import stat
import sys
import tempfile

FAKE = """#!{python}
import json, os, sys, pathlib
args = sys.argv[1:]
here = pathlib.Path(__file__).resolve().parent
mode = (here / "mode.txt").read_text().strip()
pattern = args[args.index("-i") + 1]
start = int(args[args.index("-start_number") + 1])
directory = pathlib.Path(pattern).parent
frames = sorted(p.name for p in directory.glob("frame_*.png"))
(here / "last_args.json").write_text(json.dumps({{"args": args, "frames": frames, "cwd": os.getcwd(), "start": start,
                                                 "env_keys": sorted(os.environ)}}))
if mode == "fail":
    sys.stderr.write("Unknown encoder 'libx264'\\n")
    sys.exit(3)
if mode == "empty":
    pathlib.Path(args[-1]).write_bytes(b"")
    sys.exit(0)
pathlib.Path(args[-1]).write_bytes(b"FAKEVIDEO" + json.dumps(args).encode())
"""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def ok(result: dict, what: str) -> dict:
    require(result["status"] == "ok", f"{what} failed: {result.get('error')}")
    return result["data"]


def fails(result: dict, what: str) -> str:
    require(result["status"] == "error", f"{what} should have failed")
    return result["error"]["message"]


def main() -> int:
    module_path = pathlib.Path(sys.argv[1]).resolve()
    fixtures = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(module_path.parent))
    m = importlib.import_module("molshredder")

    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        scratch = root / "tmp"
        scratch.mkdir()
        os.environ["TMPDIR"] = str(scratch)
        encoder_dir = root / "bin"
        encoder_dir.mkdir()
        fake = encoder_dir / "ffmpeg"
        fake.write_text(FAKE.format(python=sys.executable))
        fake.chmod(fake.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        mode = encoder_dir / "mode.txt"
        mode.write_text("ok")
        last = encoder_dir / "last_args.json"

        ok(m.invoke("load", {"path": str(fixtures / "1ubq.pdb"), "name": "ubq", "file-format": "pdb"}), "load")
        ok(m.invoke("show", {"representation": "lines", "selection": "all"}), "show lines")
        # no movie yet
        require("movie" in fails(m.invoke("movie export-video", {"path": str(root / "a.mp4"), "encoder-path": str(fake)}), "no movie"),
                "a movie is required")
        ok(m.invoke("movie configure", {"frames": "4", "fps": "12"}), "configure")

        def video(path: str, **kw) -> dict:
            arguments = {"path": str(root / path), "encoder-path": str(fake), "width": "65", "height": "49"}
            arguments.update({k.replace("_", "-"): str(v) for k, v in kw.items()})
            return ok(m.invoke("movie export-video", arguments), f"export-video {path}")

        data = video("a.mp4")
        recorded = json.loads(last.read_text())
        args = recorded["args"]
        require(data["format"] == "mp4" and data["frame_count"] == 4 and data["fps"] == 12.0, f"result: {data}")
        require((root / "a.mp4").read_bytes().startswith(b"FAKEVIDEO"), "the encoder's output is moved into place")
        require(len(recorded["frames"]) == 4 and recorded["frames"][0] == "frame_0001.png", f"frames rendered for the encoder: {recorded['frames']}")
        require(args[args.index("-framerate") + 1] == "12" and "libx264" in args and "yuv420p" in args and "+faststart" in args,
                f"mp4 arguments: {args}")
        require(args[args.index("-crf") + 1] == "21", "quality 70 -> crf 21")
        require(any("scale=trunc(iw/2)*2" in a for a in args), "odd frame sizes are scaled to even dimensions")
        require("odd" in " ".join(data["warnings"]), "odd size warning")
        require(not (root / "a.molshredder-tmp.mp4").exists(), "no temporary video left")
        require(list(scratch.iterdir()) == [], "scratch frames are removed")
        require(set(recorded["env_keys"]) <= {"LANG", "MOLSHREDDER_PLUGIN_DIR", "PATH"} and "TMPDIR" not in recorded["env_keys"],
                f"the encoder gets a minimal environment: {recorded['env_keys']}")
        require(not any(str(a).startswith("/bin/sh") for a in args), "no shell is involved")

        # other formats and options
        for name, needle in (("b.webm", "libvpx-vp9"), ("c.mov", "libx264"), ("d.mpg", "mpeg1video"), ("e.gif", "palettegen")):
            video(name, quality=100, fps=24)
            args = json.loads(last.read_text())["args"]
            require(any(needle in a for a in args), f"{name}: {needle} in {args}")
            require(args[args.index("-framerate") + 1] == "24", f"{name}: fps option")
        require("+faststart" not in json.loads(last.read_text())["args"], "movflags only for mp4")
        video("f.dat", video_format="webm", first=2, last=3)
        recorded = json.loads(last.read_text())
        require(recorded["frames"] == ["frame_0002.png", "frame_0003.png"] and recorded["start"] == 2, "first/last frames")
        data = video("g.mp4", keep_frames="true")
        kept = pathlib.Path(data["frames_directory"])
        require(kept.is_dir() and len(list(kept.glob("frame_*.png"))) == 4, "kept frames")

        # errors
        require("exists" in fails(m.invoke("movie export-video", {"path": str(root / "a.mp4"), "encoder-path": str(fake)}), "exists"),
                "existing output is protected")
        video("a.mp4", overwrite="true")
        require("cannot guess" in fails(m.invoke("movie export-video", {"path": str(root / "x.xyz"), "encoder-path": str(fake)}), "ext"),
                "unknown extension")
        require("between 0 and 100" in fails(m.invoke("movie export-video", {"path": str(root / "q.mp4"), "encoder-path": str(fake), "quality": "101"}), "q"),
                "quality range")
        require("not an executable" in fails(m.invoke("movie export-video", {"path": str(root / "n.mp4"), "encoder-path": str(root / "nope")}), "missing"),
                "explicit encoder path must exist")
        require("does not exist" in fails(m.invoke("movie export-video", {"path": str(root / "no" / "dir.mp4"), "encoder-path": str(fake)}), "dir"),
                "output directory must exist")
        before = set(scratch.iterdir())
        mode.write_text("fail")
        message = fails(m.invoke("movie export-video", {"path": str(root / "h.mp4"), "encoder-path": str(fake)}), "encoder failure")
        require("exit 3" in message and "libx264" in message, f"encoder diagnostics are reported: {message}")
        require(not (root / "h.mp4").exists() and set(scratch.iterdir()) == before, "failed encodes leave no output or scratch files")
        mode.write_text("empty")
        require("without writing" in fails(m.invoke("movie export-video", {"path": str(root / "i.mp4"), "encoder-path": str(fake)}), "empty"),
                "an empty output is an error")
        mode.write_text("ok")
        # no encoder anywhere: PATH is emptied and MOLSHREDDER_FFMPEG is unset
        saved_path = os.environ.get("PATH")
        os.environ["PATH"] = str(root / "empty")
        os.environ.pop("MOLSHREDDER_FFMPEG", None)
        try:
            require("ffmpeg was not found" in fails(m.invoke("movie export-video", {"path": str(root / "j.mp4")}), "no encoder"),
                    "missing encoder is reported before any rendering")
            require(set(scratch.iterdir()) == before, "no frames are rendered when the encoder is missing")
            os.environ["MOLSHREDDER_FFMPEG"] = str(fake)
            video_data = ok(m.invoke("movie export-video", {"path": str(root / "k.mp4")}), "env encoder")
            require(video_data["encoder"] == str(fake), "MOLSHREDDER_FFMPEG selects the encoder")
            os.environ.pop("MOLSHREDDER_FFMPEG")
            os.environ["PATH"] = str(encoder_dir)
            found = ok(m.invoke("movie export-video", {"path": str(root / "l.mp4")}), "PATH encoder")
            require(found["encoder"] == str(fake), "PATH lookup finds ffmpeg")
        finally:
            if saved_path is not None:
                os.environ["PATH"] = saved_path
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
