# tests/frontend/test_views.py - the key map and the view gates of the built frontend, headless (docs/re/06-cockpit-hud.md "Head views" and
# "View kinds, settled"). The keys are pushed as SDL events at a frame (--key NAME@N, --keyup NAME@N), so the test goes through the real key map,
# the core's view state and the scene: the frontend keeps no view state and draws the core's selector.
import os
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "interceptor"
ASSETS = ROOT / "assets"
NONE = 255

needed = [ASSETS / "fonts" / "hud.bin", ASSETS / "cockpit" / "masks.bin", ASSETS / "pix" / "inst5.idx", ASSETS / "pix" / "frnt5.idx",
          ASSETS / "palette" / "flight.pal", ASSETS / "cockpit" / "mapshapes.bin", ASSETS / "models" / "index.txt"]
pytestmark = [
    pytest.mark.skipif(not BIN.exists(), reason="build/interceptor not built"),
    pytest.mark.skipif(not all(p.exists() for p in needed), reason="assets not extracted (run the extractor)"),
]

_COUNTER = [0]


def run(keys=(), ups=(), frames=12, extra=(), tmp=None, cockpit=True):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    _COUNTER[0] += 1
    out = Path(tmp) / f"v{_COUNTER[0]}"
    cmd = [str(BIN), "--assets", str(ASSETS), "--scale", "1", "--flight", *(["--cockpit"] if cockpit else []), "--frames", str(frames),
           "--dump", f"{out}.ppm", "--dump-layer", f"{out}.layer"]
    for k in keys:
        cmd += ["--key", k]
    for k in ups:
        cmd += ["--keyup", k]
    cmd += list(extra)
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    assert r.returncode == 0, r.stderr
    return Path(f"{out}.ppm").read_bytes(), Path(f"{out}.layer").read_bytes()


def count(layer, pen, y0=0, y1=199):
    return sum(1 for y in range(y0, y1 + 1) for x in range(320) if layer[y * 320 + x] == pen)


def grid_columns(layer):
    """The map's vertical grid lines: pen 8, more than 120 pixels high (note 06 'Map renderer'); the cockpit HUD has none."""
    return [x for x in range(320) if sum(1 for y in range(1, 170) if layer[y * 320 + x] == 8) > 120]


def test_an_unknown_key_name_is_refused(tmp_path):
    r = subprocess.run([str(BIN), "--assets", str(ASSETS), "--frames", "1", "--key", "NoSuchKey@1"], capture_output=True, text=True,
                       env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy"))
    assert r.returncode == 2


def test_the_keypad_keys_reach_the_core_as_head_keys(tmp_path):
    base = run(tmp=tmp_path)
    kp6 = run(["Keypad 6@2"], tmp=tmp_path)
    assert kp6 != base                                                   # KP6: head code 1 (panel and HUD shifted, camera turned)
    assert run(["Keypad 6@2", "Keypad 4@3"], tmp=tmp_path) == base      # KP4 steps back to code 0
    assert run(["Keypad 6@2", "Keypad 8@3"], tmp=tmp_path) == base      # KP8: code 0
    assert run(["Keypad 6@2", "Keypad 9@3"], tmp=tmp_path) == base      # KP9 reaches the same path: code 0, not a second forward view
    assert run(["Keypad 6@2", "Keypad 5@3"], tmp=tmp_path) == kp6       # KP5 locks the view, no change of the code
    assert run(["Keypad 7@2"], tmp=tmp_path) != base                     # KP7: looking up (window row 167, no panel)
    assert run(["Keypad 0@2"], tmp=tmp_path) == base                     # KP0 has no handler


def test_a_head_view_is_momentary_after_the_key_release(tmp_path):
    base = run(frames=64, tmp=tmp_path)
    held = run(["Keypad 6@2"], frames=64, tmp=tmp_path)
    assert held != base                                                  # held: nothing recentres (this frontend counts a second as FLIGHT_STEP_HZ steps)
    released = run(["Keypad 6@2"], ["Keypad 6@3"], frames=64, tmp=tmp_path)
    assert released == base                                              # 5 timer seconds after the release the view is back at code 0
    assert run(["Keypad 6@2"], ["Keypad 6@3"], frames=20, tmp=tmp_path) != run(frames=20, tmp=tmp_path)   # but not before


def test_the_hold_timer_does_not_run_in_a_pause(tmp_path):
    """Note 06 "Head views": the per-second counters run in drawn frames only. Seven seconds of pause count as one change of the seconds (the jump), so the view is still held
    where an unpaused run has recentred it (5 changes), and recentres only after the remaining seconds."""
    held = lambda n: run(["Keypad 6@2", "P@4", "P@62"], ["Keypad 6@3"], frames=n, tmp=tmp_path)
    same = lambda n: run(["P@4", "P@62"], frames=n, tmp=tmp_path)           # the same pause without the head key: the cockpit at code 0
    assert held(80) != same(80)                                              # 18 steps after the unpause: still held
    assert held(62 + 6 * 8) == same(62 + 6 * 8)                              # the jump and four more seconds, then the recentre


def test_the_bracket_keys_are_the_cockpit_zoom_and_the_magnification_digit_follows(tmp_path):
    base = run(tmp=tmp_path)
    closer = run(["]@2"], tmp=tmp_path)
    assert closer != base
    assert run(["]@2", "[@3"], tmp=tmp_path) == base                     # [ doubles it back to 0x80
    assert run(["]@2", "]@3", "]@4"], tmp=tmp_path) == run(["]@2", "]@3"], tmp=tmp_path)   # the stop at 0x20


def overlay(layer, y0, y1):
    return sum(1 for y in range(y0, y1 + 1) for x in range(320) if layer[y * 320 + x] != NONE)


def test_kp_enter_requests_the_external_view_and_draws_the_text_strip_without_the_cockpit(tmp_path):
    _, cock = run(tmp=tmp_path)
    _, ext = run(["Keypad Enter@2"], tmp=tmp_path)
    assert overlay(cock, 20, 140) > 100 and overlay(ext, 20, 140) == 0   # the HUD is a cockpit overlay
    assert overlay(ext, 184, 188) > 0                                    # the external view draws the text strip (HDG, KTS, FT)
    _, back = run(["Keypad Enter@2", "Keypad Enter@4"], tmp=tmp_path)    # KP-Enter again: a new request, the cockpit
    assert overlay(back, 20, 140) > 100
    _, one = run(["Keypad Enter@2", "Keypad Enter@2"], tmp=tmp_path)     # two presses in one pass are one toggle
    assert overlay(one, 20, 140) == 0


PAUSE_TEXT = (128, 43, 197, 50)   # the pause text record (note 08 2.4: row 43, byte column 16, 10 characters, x 128 to 196)


def brightness(ppm, skip_text=True):
    """Sum of all colour channels, optionally leaving out the pause text's cells."""
    px = ppm[len(b"P6\n320 200\n255\n"):]
    x0, y0, x1, y1 = PAUSE_TEXT
    total = 0
    for i in range(320 * 200):
        if skip_text and x0 <= i % 320 < x1 and y0 <= i // 320 < y1:
            continue
        total += px[3 * i] + px[3 * i + 1] + px[3 * i + 2]
    return total


def dimmed_picture_with_the_pause_text(ppm, unpaused):
    """The pause screen is the last flight picture dimmed to fade level 10 with the pause text over it (note 08 5.14, note 06 item 8): not black, darker than the running picture,
    and the text cells hold pixels."""
    px = ppm[len(b"P6\n320 200\n255\n"):]
    x0, y0, x1, y1 = PAUSE_TEXT
    text = [px[3 * (y * 320 + x):3 * (y * 320 + x) + 3] for y in range(y0, y1) for x in range(x0, x1)]
    return 0 < brightness(ppm) < brightness(unpaused) and any(p != b"\0\0\0" for p in text)


def test_the_map_is_drawn_only_when_the_core_selects_it(tmp_path):
    _, cock = run(tmp=tmp_path)
    assert grid_columns(cock) == []
    _, tower = run(["M@2"], tmp=tmp_path)                                # M in the cockpit: the switch is applied by the next pass
    assert len(grid_columns(tower)) >= 1                                 # the grid lines (pen 8)
    _, tower_ext = run(["Keypad Enter@2", "M@5"], tmp=tmp_path)          # M in an external view shows the map at once
    assert len(grid_columns(tower_ext)) >= 1
    _, back = run(["M@2", "M@4"], tmp=tmp_path)                          # M in the map returns to the cockpit
    assert grid_columns(back) == []
    _, back = run(["M@2", "Keypad Enter@4"], tmp=tmp_path)               # so does KP-Enter
    assert grid_columns(back) == []
    ppm, _ = run(["P@1", "M@2"], frames=8, tmp=tmp_path)                 # paused: the key is processed but nothing is redrawn
    running, _ = run(frames=8, tmp=tmp_path)
    assert dimmed_picture_with_the_pause_text(ppm, running)              # the last picture dimmed to level 10 with the pause text (5.14)
    _, unpaused = run(["P@1", "M@2", "P@5"], frames=9, tmp=tmp_path)     # the first flight pass applies the pending request once
    assert len(grid_columns(unpaused)) >= 1


def test_the_map_altitude_keys_move_the_grid(tmp_path):
    _, a = run(["M@2"], tmp=tmp_path)
    _, b = run(["M@2", "]@4"], tmp=tmp_path)                             # ] lowers the camera to 0x1000000
    assert a != b and b != run(tmp=tmp_path)[1]
    _, c = run(["M@2", "]@4", "[@5"], tmp=tmp_path)                      # [ raises it back
    assert c == a


def test_the_pause_dims_the_picture_and_keeps_the_keys(tmp_path):
    ppm, _ = run(["P@2"], frames=8, tmp=tmp_path)
    running, _ = run(frames=8, tmp=tmp_path)
    assert dimmed_picture_with_the_pause_text(ppm, running)
    base_ppm, _ = run(frames=10, tmp=tmp_path)
    after, _ = run(["P@2", "Keypad 6@4", "P@6", "Keypad 4@7"], frames=10, tmp=tmp_path)
    assert set(after[len(b"P6\n320 200\n255\n"):]) != {0}                # unpaused again


# ---- the reference captures (git-ignored; the flight starts on the runway, start block 0, which is the capture's aircraft state) --------------------------

CAPTURES = ROOT / "extracted" / "captures" / "views"


def ref_rgb(path):
    """The reference frame cropped to the 320 x 200 picture: the capture is 2x wide, the picture starts at (186, 42)."""
    data = Path(path).read_bytes()
    parts = data.split(b"\n", 3)
    w, _ = map(int, parts[1].split())
    px = parts[3]
    return [tuple(px[3 * ((42 + y) * w + 186 + 2 * x):3 * ((42 + y) * w + 186 + 2 * x) + 3]) for y in range(200) for x in range(320)]


def mine_rgb(ppm):
    px = ppm[len(b"P6\n320 200\n255\n"):]
    return [tuple(px[i:i + 3]) for i in range(0, len(px), 3)]


@pytest.mark.skipif(not (CAPTURES / "c-KP7.ppm").exists(), reason="reference captures not present")
def test_the_head_view_up_equals_the_capture_c_kp7(tmp_path):
    """KP7 on the runway start: the composed camera (22176, 2176), the window of 167 rows and no panel; the whole frame equals the reference emulator's."""
    ppm, _ = run(["Keypad 7@2"], frames=8, extra=("--runway",), tmp=tmp_path)
    assert mine_rgb(ppm) == ref_rgb(CAPTURES / "c-KP7.ppm")


@pytest.mark.skipif(not (CAPTURES / "ext-L-1.ppm").exists(), reason="reference captures not present")
def test_the_external_chase_view_equals_the_capture_ext_l_1_outside_the_aircraft_model(tmp_path):
    """KP-Enter on the runway start: the ring camera (8653, 3072, -16275 from the aircraft) aimed at (720, 2240), the matrix of the aim pair (fx_cam_yp). The sky, the ground
    and the scenery equal the reference; the remaining differences are the grey bands of the player's own model (rows 108 to 130, the draw stage's near-body colours)."""
    ppm, _ = run(frames=8, extra=("--runway",), cockpit=False, tmp=tmp_path)   # the default start: KP-Enter on the first frame
    a, b = mine_rgb(ppm), ref_rgb(CAPTURES / "ext-L-1.ppm")
    same = sum(1 for p, q in zip(a, b) if p == q)
    assert same > 0.96 * 64000, same / 64000
    band = sum(1 for y in range(105, 135) for x in range(320) if a[y * 320 + x] != b[y * 320 + x])
    assert (64000 - same) - band < 300     # outside the player's model: a few edge pixels of the scenery only (the fx_cam_yp matrix puts the horizon on the capture's row)
