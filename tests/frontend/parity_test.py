#!/usr/bin/env python3

import importlib
import json
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_cli(executable: pathlib.Path, arguments: list[str], expected_code: int) -> dict:
    completed = subprocess.run(
        [str(executable), *arguments, "--format", "json"],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        completed.returncode == expected_code,
        f"CLI returned {completed.returncode}, expected {expected_code}: {completed.stderr}",
    )
    payload = completed.stdout if expected_code == 0 else completed.stderr
    require(bool(payload), "CLI did not produce its JSON result envelope")
    return json.loads(payload)


def run_gui(probe: pathlib.Path, scenario: str) -> dict:
    completed = subprocess.run(
        [str(probe), scenario], check=True, capture_output=True, text=True
    )
    return json.loads(completed.stdout)


def run_console(executable: pathlib.Path, commands: list[str]) -> dict:
    completed = subprocess.run(
        [str(executable), "console"],
        input="format json\n" + "\n".join(commands) + "\nexit\n",
        check=True,
        capture_output=True,
        text=True,
    )
    envelopes = []
    for line in completed.stdout.splitlines():
        marker = line.find("{")
        if marker < 0:
            continue
        try:
            envelopes.append(json.loads(line[marker:]))
        except json.JSONDecodeError:
            pass
    require(envelopes, "interactive CLI did not emit a JSON result envelope")
    return envelopes[-1]


def main() -> int:
    require(
        len(sys.argv) == 5,
        "expected module, CLI, GUI probe and multi-state fixture paths",
    )
    module_path = pathlib.Path(sys.argv[1]).resolve()
    cli_path = pathlib.Path(sys.argv[2]).resolve()
    gui_probe_path = pathlib.Path(sys.argv[3]).resolve()
    multi_state_fixture = pathlib.Path(sys.argv[4]).resolve()
    sys.path.insert(0, str(module_path.parent))
    molshredder = importlib.import_module("molshredder")

    version_python = molshredder.invoke("version")
    version_cli = run_cli(cli_path, ["version"], 0)
    version_gui = run_gui(gui_probe_path, "version")
    require(
        version_python == version_cli == version_gui,
        "CLI, GUI and Python version actions diverged",
    )
    require(
        version_python["command"] == 'invoke "system version"',
        "version alias was not normalized canonically",
    )
    require(
        version_python["data"] == {"result_schema_version": 2, "version": "0.1.0"},
        "typed version fields did not survive all frontend conversions",
    )

    center_python = molshredder.invoke("com", {"selection": "protein"})
    center_cli = run_cli(cli_path, ["com", "--selection", "protein"], 2)
    center_gui = run_gui(gui_probe_path, "com")
    require(
        center_python == center_cli == center_gui,
        "CLI, GUI and Python COM actions diverged",
    )
    require(
        center_python["error"]["code"] == "not_found",
        "shared unloaded-workspace operation did not preserve its stable error",
    )
    require(
        center_python["command"]
        == 'invoke "analyze center" --mode "com" --precision "6" '
        '--selection "protein" --unit "angstrom"',
        "frontend parity did not include normalized defaults",
    )

    view_arguments = {
        "distance": "25",
        "far-clip": "500",
        "near-clip": "0.5",
        "projection": "orthographic",
        "orthographic-height": "50",
        "target-x": "3",
        "target-y": "-2",
        "target-z": "1",
    }
    view_python = molshredder.invoke("view set", view_arguments)
    view_cli = run_cli(
        cli_path,
        [
            "view",
            "set",
            "--distance",
            "25",
            "--far-clip",
            "500",
            "--near-clip",
            "0.5",
            "--projection",
            "orthographic",
            "--orthographic-height",
            "50",
            "--target-x",
            "3",
            "--target-y",
            "-2",
            "--target-z",
            "1",
        ],
        0,
    )
    view_gui = run_gui(gui_probe_path, "view")
    require(
        view_python == view_cli == view_gui,
        "CLI, GUI adapter and Python camera actions diverged",
    )
    require(
        view_python["data"]["camera"]["target"] == [3.0, -2.0, 1.0]
        and view_python["data"]["camera"]["projection"] == "orthographic"
        and view_python["data"]["camera"]["near_clip"] == 0.5
        and view_python["data"]["camera"]["far_clip"] == 500.0,
        "typed camera snapshot fields were not preserved across frontends",
    )

    molshredder.invoke("view reset")
    origin_position_arguments = {"position": "1.5,-2,3.25"}
    origin_position_python = molshredder.invoke(
        "view origin", origin_position_arguments
    )
    origin_position_cli = run_cli(
        cli_path,
        ["view", "origin", "--position", "1.5,-2,3.25"],
        0,
    )
    origin_position_gui = run_gui(gui_probe_path, "view-origin-position")
    require(
        origin_position_python == origin_position_cli == origin_position_gui,
        "CLI, GUI adapter and Python explicit camera origins diverged",
    )
    require(
        origin_position_python["data"]["target"] == "camera"
        and origin_position_python["data"]["source"] == "position"
        and origin_position_python["data"]["position"] == [1.5, -2.0, 3.25]
        and origin_position_python["data"]["camera"]["target"]
        == [0.0, 0.0, 0.0],
        "explicit camera origin did not preserve target or typed dispatch metadata",
    )

    molshredder.invoke("view reset")
    projection_arguments = {
        "field-of-view-degrees": "60",
        "mode": "orthographic",
        "preserve-scale": "true",
    }
    projection_python = molshredder.invoke(
        "view projection", projection_arguments
    )
    projection_cli = run_cli(
        cli_path,
        [
            "view",
            "projection",
            "--field-of-view-degrees",
            "60",
            "--mode",
            "orthographic",
            "--preserve-scale",
            "true",
        ],
        0,
    )
    projection_gui = run_gui(gui_probe_path, "projection")
    require(
        projection_python == projection_cli == projection_gui,
        "CLI, GUI adapter and Python projection actions diverged",
    )
    require(
        projection_python["data"]["mode"] == "orthographic"
        and projection_python["data"]["previous_mode"] == "perspective"
        and projection_python["data"]["preserve_scale"]
        and abs(
            projection_python["data"]["vertical_span"]
            - projection_python["data"]["previous_vertical_span"]
        )
        < 1.0e-10
        and abs(projection_python["data"]["field_of_view_degrees"] - 60.0)
        < 1.0e-12,
        "projection result did not preserve screen scale or degree FOV metadata",
    )

    stereo_arguments = {
        "angle-scale": "2.1",
        "enabled": "true",
        "mode": "crosseye",
        "anaglyph-mode": "optimized",
        "shift-percent": "2.5",
        "swap-eyes": "true",
    }
    stereo_python = molshredder.invoke("stereo set", stereo_arguments)
    stereo_cli = run_cli(
        cli_path,
        [
            "stereo", "set", "--angle-scale", "2.1", "--enabled", "true",
            "--mode", "crosseye", "--shift-percent", "2.5",
            "--swap-eyes", "true", "--anaglyph-mode", "optimized",
        ],
        0,
    )
    stereo_gui = run_gui(gui_probe_path, "stereo")
    require(
        stereo_python == stereo_cli == stereo_gui,
        "CLI, GUI adapter and Python stereo actions diverged",
    )
    require(
        stereo_python["data"]["stereo"] == {
            "angle_scale": 2.1,
            "anaglyph_mode": "optimized",
            "enabled": True,
            "mode": "crosseye",
            "shift_percent": 2.5,
            "swap_eyes": True,
        }
        and not stereo_python["data"]["render_active"],
        "headless stereo configuration did not expose typed state honestly",
    )

    anaglyph_arguments = {
        "angle-scale": "2.1",
        "anaglyph-mode": "half_color",
        "enabled": "true",
        "mode": "anaglyph",
        "shift-percent": "2.5",
        "swap-eyes": "true",
    }
    anaglyph_python = molshredder.invoke("stereo set", anaglyph_arguments)
    anaglyph_cli = run_cli(
        cli_path,
        [
            "stereo", "set", "--angle-scale", "2.1",
            "--anaglyph-mode", "half_color", "--enabled", "true",
            "--mode", "anaglyph", "--shift-percent", "2.5",
            "--swap-eyes", "true",
        ],
        0,
    )
    anaglyph_gui = run_gui(gui_probe_path, "anaglyph")
    require(
        anaglyph_python["data"]["stereo"]
        == anaglyph_cli["data"]["stereo"]
        == anaglyph_gui["data"]["stereo"]
        and anaglyph_python["data"]["render_active"]
        == anaglyph_cli["data"]["render_active"]
        == anaglyph_gui["data"]["render_active"]
        and anaglyph_python["data"]["stereo"]["anaglyph_mode"]
        == "half_color",
        "CLI, GUI adapter and Python anaglyph actions diverged",
    )

    reset_python = molshredder.invoke("view reset")
    reset_cli = run_cli(cli_path, ["view", "reset"], 0)
    reset_gui = run_gui(gui_probe_path, "view-reset")
    require(
        reset_python == reset_cli == reset_gui,
        "CLI, GUI adapter and Python camera reset actions diverged",
    )
    require(
        reset_python["data"]["camera"]["target"] == [0.0, 0.0, 0.0]
        and reset_python["data"]["molecular_object_count"] == 0
        and reset_python["data"]["volume_object_count"] == 0
        and not reset_python["data"]["animation"]["active"],
        "empty-workspace reset did not preserve the typed camera contract",
    )

    get_clip_python = molshredder.invoke("view get-clip")
    get_clip_cli = run_cli(cli_path, ["view", "get-clip"], 0)
    get_clip_gui = run_gui(gui_probe_path, "get-clip")
    require(
        get_clip_python == get_clip_cli == get_clip_gui,
        "CLI, GUI adapter and Python clip queries diverged",
    )
    require(
        get_clip_python["data"]["near_clip"] == 0.1
        and get_clip_python["data"]["far_clip"] == 10000.0,
        "clip query did not expose the canonical camera range",
    )

    clip_arguments = {"distance": "2", "mode": "near-set"}
    clip_python = molshredder.invoke("view clip", clip_arguments)
    clip_cli = run_cli(
        cli_path,
        ["view", "clip", "--distance", "2", "--mode", "near-set"],
        0,
    )
    clip_gui = run_gui(gui_probe_path, "clip")
    require(
        clip_python == clip_cli == clip_gui,
        "CLI, GUI adapter and Python clipping actions diverged",
    )
    require(
        clip_python["data"]["near_clip"] == 2.0
        and clip_python["data"]["far_clip"] == 10000.0,
        "absolute near clipping did not preserve the far plane",
    )

    molshredder.invoke("view reset")
    move_arguments = {"axis": "x", "distance": "3"}
    move_python = molshredder.invoke("view move", move_arguments)
    move_cli = run_cli(
        cli_path, ["view", "move", "--axis", "x", "--distance", "3"], 0
    )
    move_gui = run_gui(gui_probe_path, "move")
    require(
        move_python == move_cli == move_gui,
        "CLI, GUI adapter and Python camera move actions diverged",
    )
    require(
        move_python["data"]["camera"]["target"] == [-3.0, 0.0, 0.0]
        and move_python["data"]["camera"]["model_origin"] == [0.0, 0.0, 0.0],
        "camera-local X move did not preserve the model origin",
    )

    molshredder.invoke("view reset")
    turn_arguments = {"angle": "90", "axis": "z"}
    turn_python = molshredder.invoke("view turn", turn_arguments)
    turn_cli = run_cli(
        cli_path, ["view", "turn", "--angle", "90", "--axis", "z"], 0
    )
    turn_gui = run_gui(gui_probe_path, "turn")
    require(
        turn_python == turn_cli == turn_gui,
        "CLI, GUI adapter and Python camera turn actions diverged",
    )
    require(
        turn_python["data"]["camera"]["orientation"]
        == [0.7071067811865476, 0.0, 0.0, 0.7071067811865475],
        "positive Z turn did not produce the expected local-axis orientation",
    )

    loaded_state_python = molshredder.invoke(
        "load", {"path": str(multi_state_fixture), "file-format": "g96"}
    )
    require(
        loaded_state_python["status"] == "ok",
        "Python could not load the multi-state parity fixture",
    )
    molshredder.invoke("view reset")
    state_arguments = {"move-origin": "true", "selection": "all", "state": "all"}
    state_python = molshredder.invoke("view center", state_arguments)
    fixture_quoted = str(multi_state_fixture).replace('"', '\\"')
    state_cli = run_console(
        cli_path,
        [
            f'invoke "load" --file-format "g96" --path "{fixture_quoted}"',
            'invoke "view reset"',
            'invoke "view center" --move-origin "true" '
            '--selection "all" --state "all"',
        ],
    )
    completed_gui_state = subprocess.run(
        [str(gui_probe_path), "state-all", str(multi_state_fixture)],
        check=True,
        capture_output=True,
        text=True,
    )
    state_gui = json.loads(completed_gui_state.stdout)
    require(
        state_python == state_cli == state_gui,
        "CLI console, GUI adapter and Python all-state camera framing diverged",
    )
    require(
        state_python["data"]["state"] == "all"
        and state_python["data"]["extent"]["evaluated_frame_count"] == 2
        and state_python["data"]["extent"]["used_atom_count"] == 4,
        "all-state framing did not aggregate both fixture frames",
    )

    molshredder.invoke("view reset")
    orient_arguments = {"selection": "all", "state": "all"}
    orient_python = molshredder.invoke("view orient", orient_arguments)
    orient_cli = run_console(
        cli_path,
        [
            f'invoke "load" --file-format "g96" --path "{fixture_quoted}"',
            'invoke "view reset"',
            'invoke "view orient" --selection "all" --state "all"',
        ],
    )
    completed_gui_orient = subprocess.run(
        [str(gui_probe_path), "orient-all", str(multi_state_fixture)],
        check=True,
        capture_output=True,
        text=True,
    )
    orient_gui = json.loads(completed_gui_orient.stdout)
    require(
        orient_python == orient_cli == orient_gui,
        "CLI console, GUI adapter and Python principal-axis orient diverged",
    )
    require(
        orient_python["data"]["state"] == "all"
        and orient_python["data"]["principal_axes"]["sample_count"] == 4
        and orient_python["data"]["extent"]["evaluated_frame_count"] == 2,
        "all-state orient did not aggregate both fixture frames",
    )

    pymol_values = "0,1,0,-1,0,0,0,0,1,2,-3,-40,10,20,30,.5,100,60"
    pymol_python = molshredder.invoke(
        "view import-pymol", {"values": pymol_values}
    )
    pymol_cli = run_cli(
        cli_path,
        ["view", "import-pymol", "--values", pymol_values],
        0,
    )
    pymol_gui = run_gui(gui_probe_path, "pymol-view")
    require(
        pymol_python == pymol_cli == pymol_gui,
        "CLI, GUI adapter and Python PyMOL-view imports diverged",
    )
    require(
        pymol_python["data"]["camera"]["target"] == [13.0, 22.0, 30.0]
        and pymol_python["data"]["camera"]["model_origin"]
        == [10.0, 20.0, 30.0]
        and len(pymol_python["data"]["values"]) == 18,
        "PyMOL view conversion did not preserve pivot and typed layout",
    )

    print("frontend-parity-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
