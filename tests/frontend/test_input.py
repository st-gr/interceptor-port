# tests/frontend/test_input.py - the game controller (src/frontend/sdl/input.c, plan Task 7): --ctrl NAME@N pushes an SDL controller event at frame N, through the path of a real controller.
# The controller is a second source of the keyboard's keys (one table), so a control and its key must make the same core events. Observed through the frame: a run with the control and a run
# with the key (same seed, same frames) give the same picture, and that picture differs from a run with neither (the input acts). Needs build/interceptor and assets/; invented inputs only.
import os
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / "build" / "interceptor"
ASSETS = ROOT / "assets"
pytestmark = pytest.mark.skipif(not (EXE.exists() and (ASSETS / "VERSION").exists()), reason="build/interceptor or assets/ missing")

ENV = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
DEAD_ZONE = 8000   # CTRL_DEAD_ZONE in input.h (the manual states it)


def run(tmp_path, extra, frames=90, tag="f", flight=True, free=True):
    out = tmp_path / (tag + ".ppm")
    args = [str(EXE), "--assets", str(ASSETS), "--scale", "1", "--frames", str(frames), "--seed", "1", "--no-log", "--no-audio", "--dump", str(out)]
    args += ((["--freecam", "--throttle", "50"] if free else ["--flight"]) + ["--cockpit"] if flight else []) + extra
    r = subprocess.run(args, capture_output=True, text=True, env=ENV, timeout=300)
    assert r.returncode == 0, r.stderr
    return out.read_bytes()


# (control, the keyboard key it stands for, the flight that shows the key: "free" the free camera, "deck" the carrier deck, "" none within 90 frames)
PAIRS = [
    ("x-left", "Left", "free"), ("x-right", "Right", "free"), ("y-up", "Up", "free"), ("y-down", "Down", "free"),
    ("rx-left", ",", ""), ("rx-right", ".", ""),
    ("rs", "Keypad Enter", "free"), ("back", "M", "free"), ("dpad-left", "Keypad 4", "deck"), ("dpad-right", "Keypad 6", "deck"),
    ("dpad-up", "Keypad 8", ""), ("start", "P", ""), ("rt", "Space", ""), ("lt", "\\", ""), ("b", "G", ""),
    ("x", "T", ""), ("y", "R", "deck"), ("ry-up", "=", "deck"), ("ry-down", "-", ""), ("lb", "F", ""), ("rb", "C", ""), ("ls", "J", ""), ("a", "Return", ""),
]


@pytest.mark.parametrize("ctrl,key,visible", PAIRS)
def test_a_control_makes_the_picture_of_its_key(tmp_path, ctrl, key, visible):
    free = visible != "deck"
    with_ctrl = run(tmp_path, ["--ctrl", f"{ctrl}@10"], tag="c", free=free)
    with_key = run(tmp_path, ["--key", f"{key}@10"], tag="k", free=free)
    assert with_ctrl == with_key
    if visible:
        assert with_ctrl != run(tmp_path, [], tag="n", free=free)


def test_a_held_stick_ramps_and_its_release_lets_go(tmp_path):
    """The stick is the digital stick of note 05 17.4: held from frame 10 to 40 equals the key held for the same frames; a release comes through --ctrlup."""
    c = run(tmp_path, ["--ctrl", "x-left@10", "--ctrlup", "x-left@40"], tag="c")
    k = run(tmp_path, ["--key", "Left@10", "--keyup", "Left@40"], tag="k")
    assert c == k
    assert c != run(tmp_path, ["--ctrl", "x-left@10"], tag="held")   # let go at 40 is not held to the end


def test_the_dead_zone(tmp_path):
    """A deflection at the dead zone is no input; one above it is the key."""
    none = run(tmp_path, [], tag="n")
    assert run(tmp_path, ["--ctrl", f"x-left@10:{DEAD_ZONE}"], tag="a") == none
    assert run(tmp_path, ["--ctrl", f"x-left@10:{DEAD_ZONE + 1}"], tag="b") == run(tmp_path, ["--key", "Left@10"], tag="k")


def test_a_stick_crossing_zero_releases_one_direction_and_presses_the_other(tmp_path):
    c = run(tmp_path, ["--ctrl", "x-left@10", "--ctrlup", "x-left@30", "--ctrl", "x-right@30"], tag="c")
    k = run(tmp_path, ["--key", "Left@10", "--keyup", "Left@30", "--key", "Right@30"], tag="k")
    assert c == k


def test_the_controller_is_not_menu_input(tmp_path):
    """Note 08 5.9: outside the flight only the table's screen entry (a, RETURN) is taken; the others do not even leave the credits."""
    credits = run(tmp_path, [], frames=30, tag="n", flight=False)
    assert run(tmp_path, ["--ctrl", "rt@5"], frames=30, tag="rt", flight=False) == credits
    assert run(tmp_path, ["--ctrl", "a@5"], frames=30, tag="a", flight=False) != credits


def test_a_name_outside_the_table_is_refused(tmp_path):
    r = subprocess.run([str(EXE), "--assets", str(ASSETS), "--frames", "1", "--ctrl", "nonsense@3"], capture_output=True, text=True, env=ENV)
    assert r.returncode == 2 and "x-left" in r.stderr
    r = subprocess.run([str(EXE), "--bogus"], capture_output=True, text=True, env=ENV)
    assert "--seed" in r.stderr and "--ctrl" in r.stderr
