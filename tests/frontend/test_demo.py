# tests/frontend/test_demo.py - the demo replay through the frontend binary against the three emulator captures of the demo (extracted/captures/demo-deck.ppm,
# demo-external.ppm, demo-cockpit.ppm; tools/vamiga-ref/scripts/demo-flight.txt). The captures were taken by wall-clock waits (jitter of a second or two), so their stream
# step is not recorded: each capture is compared at the stream step where its scene stands (the deck at 0 kts, the external view at 92 kts, the airborne cockpit at 360 kts
# and 2,390 ft), found by the scan in the report of milestone 4 task 6; the test pins the differing pixel count at that step.
#
# The keys of the run: any key on the credits, the menu's digit 1 (no log-disk prompt) (--key NAME@N pushes an SDL key at the start of frame N; one frame is one
# core step). The first stream step is applied 44 steps after the menu key (tests/test_replay.c pins this count on the real assets): with the key at frame 20, a run of
# FIRST + S frames has applied S stream frames (7.8: frame i at step i, no conversion). Skips without the binary, the assets or the captures.
import os
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "interceptor"
ASSETS = ROOT / "assets"
CAP = ROOT / "extracted" / "captures"
FIRST = 63                    # 19 + 44: the frames before the first stream step has been applied (menu key at frame 20)
KEYS = ["x@3", "1@20"]   # the log-disk check passes by default (note 08 5.3): any key on the credits goes to the menu

needed = [ASSETS / "tables" / "demo.bin", ASSETS / "models" / "index.txt", ASSETS / "fonts" / "menu5x7.bin", CAP / "demo-deck.ppm", CAP / "demo-external.ppm",
          CAP / "demo-cockpit.ppm"]
pytestmark = [
    pytest.mark.skipif(not BIN.exists(), reason="build/interceptor not built"),
    pytest.mark.skipif(not all(p.exists() for p in needed), reason="assets or demo captures absent"),
]


def ppm(path):
    parts = Path(path).read_bytes().split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def demo_frame(applied, tmp):
    """The frontend's frame after `applied` stream frames."""
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    out = Path(tmp) / f"demo{applied}.ppm"
    cmd = [str(BIN), "--assets", str(ASSETS), "--scale", "1", "--frames", str(FIRST + applied), "--dump", str(out)]
    for k in KEYS:
        cmd += ["--key", k]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=120)
    assert r.returncode == 0, r.stderr
    return ppm(out)[2]


def differing(ref_name, mine):
    """Differing pixels of the 320x200 game area: the capture is the emulator's screen (912 wide), the game area at x 186 in double-width pixels, y 42."""
    rw, _, rpx = ppm(CAP / ref_name)
    bad = []
    for y in range(200):
        for x in range(320):
            o = 3 * ((42 + y) * rw + 186 + 2 * x)
            q = 3 * (y * 320 + x)
            if rpx[o:o + 3] != mine[q:q + 3]:
                bad.append((x, y))
    return bad


def test_the_deck_frame_matches_the_capture_up_to_the_aircraft_model(tmp_path):
    # the deck at 0 kts, outside view behind the aircraft: the view is the same for the whole span of stream steps 15 to 26 (the throttle has not yet moved the aircraft)
    counts = [len(differing("demo-deck.ppm", demo_frame(s, tmp_path))) for s in (15, 20, 26)]
    assert counts == [85, 85, 85], counts          # all of them inside the aircraft model's polygons (rows 66 to 121): the world renderer's, not the replay's
    assert len(differing("demo-deck.ppm", demo_frame(27, tmp_path))) > 5000   # the next step the camera has moved: the capture is not this frame


def test_the_external_frame_matches_the_capture_at_stream_step_99(tmp_path):
    bad = differing("demo-external.ppm", demo_frame(99, tmp_path))
    assert len(bad) == 242, len(bad)               # 242: the aircraft model's rows 64 to 128
    assert min(y for _, y in bad) >= 60


def test_the_cockpit_frame_matches_the_capture_at_stream_step_257(tmp_path):
    bad = differing("demo-cockpit.ppm", demo_frame(257, tmp_path))
    assert len(bad) == 0, len(bad)                 # the radar's threat alert on the message line (note 07 12.3) and the HUD target data complete the frame: every pixel equals the capture's
    assert len(differing("demo-cockpit.ppm", demo_frame(255, tmp_path))) > 500   # the neighbouring steps are clearly worse: the step is pinned, not a plateau
