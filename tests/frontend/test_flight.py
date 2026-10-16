# tests/frontend/test_flight.py - flight-mode rendering of the built frontend, headless.
# Real assets are git-ignored, so these tests skip when build/interceptor or assets/ with models
# is absent.
import os
import shutil
import subprocess
from collections import Counter
from pathlib import Path

import pytest

from emu_palette import convert

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "interceptor"
ASSETS = ROOT / "assets"

pytestmark = [
    pytest.mark.skipif(not BIN.exists(), reason="build/interceptor not built"),
    pytest.mark.skipif(not (ASSETS / "models" / "index.txt").exists()
                       or not (ASSETS / "palette" / "flight.pal").exists(),
                       reason="real assets (models, palette) not extracted"),
]

HDR = b"P6\n320 200\n255\n"


def run(args, assets=ASSETS):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    return subprocess.run([str(BIN), "--assets", str(assets), "--scale", "1", *args],
                          capture_output=True, text=True, env=env)


def palette():
    raw = (ASSETS / "palette" / "flight.pal").read_bytes()
    return convert(raw)


def load(path):
    data = Path(path).read_bytes()
    assert data.startswith(HDR), "dump is not a 320x200 P6 PPM"
    px = data[len(HDR):]
    assert len(px) == 320 * 200 * 3
    return [tuple(px[i:i + 3]) for i in range(0, len(px), 3)]


def world_rows(pix, first=1, last=167):
    return [pix[y * 320:(y + 1) * 320] for y in range(first, last + 1)]


def check_sky_and_ground(pix, pal):
    sky, ground = pal[5], pal[6]
    rows = world_rows(pix)
    # level camera: sky in the upper part, ground in the lower part of the world area
    assert any(p == sky for row in rows[:60] for p in row), "no sky in rows 1..60"
    assert any(p == ground for row in rows[120:] for p in row), "no ground in rows 121..167"
    assert not any(p == ground for row in rows[:40] for p in row), "ground above the horizon"
    assert pix[0] == (0, 0, 0), "row 0 must stay black"
    # below row 167 is black in the outside view except the text strip (rows 184 to 188, note 06 "External view text strip")
    assert all(p == (0, 0, 0) for p in pix[168 * 320:184 * 320] + pix[189 * 320:]), "below row 167 must be black in the outside view"
    return sky, ground


def test_flight_dump_has_sky_and_ground(tmp_path):
    out = tmp_path / "f.ppm"
    r = run(["--freecam", "--frames", "10", "--dump", str(out)])
    assert r.returncode == 0, r.stderr
    check_sky_and_ground(load(out), palette())


# Full throttle from the start (behind the carrier, looking along the deck) brings the deck into
# view at frames 10..15.
CARRIER_ARGS = ["--freecam", "--throttle", "100", "--frames", "12"]


def polygon_pixels(pix, pal):
    """Pixels of the window rows (1..167) that are neither sky nor ground. The sky and ground fill every window pixel, so what is
    left (black included: the carrier's pen 3 is black through the emulator's colour curve) was drawn by a polygon."""
    sky, ground = pal[5], pal[6]
    return [(x, y) for y in range(1, 168) for x in range(320) if pix[y * 320 + x] not in (sky, ground)]


def test_polygons_appear_when_the_carrier_is_in_view(tmp_path):
    out = tmp_path / "f.ppm"
    r = run([*CARRIER_ARGS, "--dump", str(out)])
    assert r.returncode == 0, r.stderr
    pix = load(out)
    pal = palette()
    assert any(p == pal[5] for p in pix[1 * 320:60 * 320]), "no sky"
    others = polygon_pixels(pix, pal)
    assert len(others) > 100, "no polygon pixels of another colour"
    assert {pix[y * 320 + x] for x, y in others} <= set(pal), "pixels outside the flight palette"


def test_holding_right_moves_polygons_left(tmp_path):
    cx = {}
    for stick in (-40, 0, 40):
        out = tmp_path / f"s{stick}.ppm"
        r = run([*CARRIER_ARGS, "--stick", str(stick), "0", "--dump", str(out)])
        assert r.returncode == 0, r.stderr
        pts = polygon_pixels(load(out), palette())
        assert len(pts) > 100
        cx[stick] = sum(x for x, _ in pts) / len(pts)
    # turning right: the scenery drifts to the left of the screen (and the reverse)
    assert cx[40] < cx[0] - 10 < cx[0] + 10 < cx[-40]


def test_cockpit_view_draws_the_panel(tmp_path):
    if not (ASSETS / "pix" / "inst5.png").exists():
        pytest.skip("inst5 picture not extracted")
    out = tmp_path / "c.ppm"
    r = run(["--flight", "--cockpit", "--frames", "5", "--dump", str(out)])
    assert r.returncode == 0, r.stderr
    pix = load(out)
    # the panel covers the bottom rows with non-black pixels; sky stays above the arch
    assert sum(1 for p in pix[190 * 320:199 * 320] if p != (0, 0, 0)) > 500   # pen 3 is black through the emulator curve
    assert pix[5 * 320 + 160] == palette()[5]


def test_flight_with_no_models_still_renders_sky_and_ground(tmp_path):
    needed = [ASSETS / "terrain" / "terrain.bin", ASSETS / "terrain" / "positions.txt",
              ASSETS / "tables" / "lod.bin"]
    if not all(p.exists() for p in needed):
        pytest.skip("terrain/tables not extracted")
    synth = tmp_path / "assets"
    for sub in ("terrain", "tables", "palette", "pix"):
        shutil.copytree(ASSETS / sub, synth / sub)
    (synth / "models").mkdir()
    (synth / "models" / "index.txt").write_text("# id\tfile\tstatus\n")   # no models at all
    shutil.copy(ASSETS / "VERSION", synth / "VERSION")
    shutil.copy(ASSETS / "manifest.json", synth / "manifest.json")
    out = tmp_path / "f.ppm"
    r = run(["--freecam", "--frames", "5", "--dump", str(out)], assets=synth)
    assert r.returncode == 0, r.stderr
    check_sky_and_ground(load(out), palette())


def test_flight_flag_runs_the_aircraft_in_the_cockpit(tmp_path):
    out = tmp_path / "a.ppm"
    r = run(["--flight", "--cockpit", "--frames", "3", "--dump", str(out)])
    assert r.returncode == 0, r.stderr
    assert len(load(out)) == 320 * 200


CRASH = ["--flight", "--cockpit", "--stick", "0", "-100", "--throttle", "100"]   # full power, nose down off the deck: a sea crash


def test_crash_hold_ends_in_one_uniform_orange_frame(tmp_path):
    # note 05 17.17 (xii): a single 20 ms frame, the palette block's 0x0FB2 through the measured curve (R 15 -> 255, G 11 -> 159, B 2 -> 0),
    # presented before the restart frame of the step where the hold ends; never before the crash.
    flash, last = tmp_path / "flash.ppm", tmp_path / "last.ppm"
    early = run([*CRASH, "--frames", "40", "--dump", str(last), "--dump-flash", str(flash)])
    assert early.returncode != 0 and not flash.exists()      # no hold yet: no flash
    r = run([*CRASH, "--frames", "140", "--dump", str(last), "--dump-flash", str(flash)])
    assert r.returncode == 0, r.stderr
    px = load(flash)
    assert set(px) == {(236, 159, 0)}
    assert set(load(last)) != {(236, 159, 0)}                # the frame after it is the restart frame


def test_seed_pins_the_run_and_is_accepted(tmp_path):
    a, b = tmp_path / "a.ppm", tmp_path / "b.ppm"
    r1 = run(["--seed", "7", "--flight", "--frames", "5", "--dump", str(a)])
    r2 = run(["--seed", "7", "--flight", "--frames", "5", "--dump", str(b)])
    assert r1.returncode == 0 and r2.returncode == 0, r1.stderr + r2.stderr
    assert a.read_bytes() == b.read_bytes()
