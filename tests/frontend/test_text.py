# tests/frontend/test_text.py - the text drawer (src/frontend/sdl/text.c) and the log file (logfile.c), headless through build/text_driver.
# The glyphs here are invented: the game's font and text are never in the repository. The capture comparisons use the extracted assets and
# extracted/captures at run time and skip when they are absent.
import os
import subprocess
from pathlib import Path

import pytest

from emu_palette import CURVE

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "build" / "text_driver"
BIN = ROOT / "build" / "interceptor"
ASSETS = ROOT / "assets"
CAPTURES = ROOT / "extracted" / "captures"
W, H = 320, 200

pytestmark = pytest.mark.skipif(not DRIVER.exists(), reason="build/text_driver not built (run make)")


def invented_font():
    """64 glyphs x 7 rows, pixels in bits 7..3; every glyph differs and none is empty (except the space)."""
    out = bytearray()
    for c in range(64):
        for r in range(7):
            out.append(0 if c == 0 else (((c * 7 + r * 13 + 5) % 31 + 1) << 3) & 0xF8)
    return bytes(out)


FONT = invented_font()


def glyph_pixels(ch, x0, y0):
    g = FONT[(ch - 0x20) * 7:(ch - 0x20) * 7 + 7]
    return {(x0 + c, y0 + r) for r in range(7) for c in range(5) if (g[r] >> (7 - c)) & 1}


def run(script, tmp_path, assets=None, font_bytes=None):
    font = tmp_path / "font.bin"
    font.write_bytes(font_bytes or FONT)
    args = [str(DRIVER), str(font)] + ([str(assets)] if assets else [])
    r = subprocess.run(args, input=script.encode(), capture_output=True)
    assert r.returncode == 0, r.stderr
    return r.stdout


def screen(script, tmp_path, assets=None, font_bytes=None):
    """The pen buffer after the script's commands (text output lines before the dump are returned too)."""
    out = run(script + "dump\n", tmp_path, assets, font_bytes)
    i = out.index(b"PENS 64000\n")
    pens = out[i + len(b"PENS 64000\n"):]
    assert len(pens) == W * H
    return pens, out[:i].decode()


def hexs(s):
    return s.encode().hex()


def lit(pens, bg):
    return {(i % W, i // W): pens[i] for i in range(W * H) if pens[i] != bg}


def test_a_line_sets_exactly_its_glyph_pixels_in_the_pen(tmp_path):
    pens, _ = screen(f"clear 0\nline 10 3 7 3 {hexs('AB!')}\n", tmp_path)
    want = set()
    for j, ch in enumerate(b"AB!"):
        want |= glyph_pixels(ch, 8 * 3 + 7 * j, 10)
    got = lit(pens, 0)
    assert set(got) == want and set(got.values()) == {7}


def test_a_clear_glyph_bit_leaves_the_screen_as_it_is(tmp_path):
    pens, _ = screen(f"clear 9\nline 20 0 2 2 {hexs('QR')}\n", tmp_path)
    want = glyph_pixels(ord("Q"), 0, 20) | glyph_pixels(ord("R"), 7, 20)
    assert set(lit(pens, 9)) == want                      # everything else is still the old pen 9, cells are never filled
    assert all(pens[i] in (2, 9) for i in range(W * H))


def test_the_pen_can_be_black_over_a_lit_screen(tmp_path):
    pens, _ = screen(f"clear 5\nline 0 0 0 1 {hexs('A')}\n", tmp_path)
    assert set(lit(pens, 5)) == glyph_pixels(ord("A"), 0, 0) and set(lit(pens, 5).values()) == {0}


def test_shown_limits_the_characters(tmp_path):
    for shown in (0, 1, 4, 9):
        pens, _ = screen(f"clear 0\nline 40 2 6 {shown} {hexs('ABCD')}\n", tmp_path)
        want = set()
        for j, ch in enumerate(b"ABCD"[:shown]):
            want |= glyph_pixels(ch, 16 + 7 * j, 40)
        assert set(lit(pens, 0)) == want, shown


def test_characters_outside_the_font_draw_nothing_but_keep_their_cell(tmp_path):
    pens, _ = screen(f"clear 0\nline 8 1 3 4 1f41 60 80\n".replace("1f41 60 80", "1f416080"), tmp_path)
    assert set(lit(pens, 0)) == glyph_pixels(0x41, 8 + 7, 8)


def test_the_glyph_halves_are_drawn_in_their_own_cells(tmp_path):
    # 0x24 then 0x5E (the trademark pair) and a lone 0x26 (the copyright sign): ordinary characters of the font, one cell each (note 08 2.2 quirk 4)
    pens, _ = screen(f"clear 0\nline 30 4 1 5 {hexs('$^&')}\n".replace(hexs("$^&"), "245e26"), tmp_path)
    want = glyph_pixels(0x24, 32, 30) | glyph_pixels(0x5E, 39, 30) | glyph_pixels(0x26, 46, 30)
    assert set(lit(pens, 0)) == want and want


def test_the_right_edge_is_cut_at_pixel_319_and_never_wraps(tmp_path):
    # col 38: x = 304; characters 0..2 start at 304, 311, 318: the third has pixels at 318..322, those from 320 on are the engine's
    # byte-41 writes (written back unchanged) and are not drawn
    pens, _ = screen(f"clear 0\nline 50 38 4 3 {hexs('ZZZ')}\nline 51 39 4 2 {hexs('ZZ')}\n", tmp_path)
    got = lit(pens, 0)
    assert all(x < W for x, _ in got)
    want = set()
    for j in range(3):
        want |= {p for p in glyph_pixels(ord("Z"), 304 + 7 * j, 50) if p[0] < W}
    for j in range(2):
        want |= {p for p in glyph_pixels(ord("Z"), 312 + 7 * j, 51) if p[0] < W}
    assert set(got) == want
    # the first bytes of the next scanline (what a long-word write past byte 39 would have touched) hold no pixel of the row above
    assert all(pens[y * W + x] == 0 for y in range(51, 58) for x in range(0, 4) if (x, y) not in want)


def test_the_last_cell_of_a_shipped_width_line_is_complete(tmp_path):
    # 8 * col + 7 (n - 1) + 5 <= 320 (note 08 2.3): the seven-character field at column 34 ends at pixel 318
    pens, _ = screen(f"clear 0\nline 72 34 4 7 {hexs('MMMMMMM')}\n", tmp_path)
    want = set()
    for j in range(7):
        want |= glyph_pixels(ord("M"), 272 + 7 * j, 72)
    assert set(lit(pens, 0)) == want and max(x for x, _ in want) == 318


def test_the_bottom_edge_is_clipped_at_scanline_199(tmp_path):
    # the engine would write past the plane; the port clips (note 08 3.4 rule 6, a port choice: the shipped data never reaches it)
    pens, _ = screen(f"clear 0\nline 196 2 4 2 {hexs('AB')}\nline 199 20 4 1 {hexs('A')}\nline 200 0 4 1 {hexs('A')}\nline 250 0 4 1 {hexs('A')}\n", tmp_path)
    want = {p for p in glyph_pixels(ord("A"), 16, 196) | glyph_pixels(ord("B"), 23, 196) if p[1] < H}
    want |= {p for p in glyph_pixels(ord("A"), 160, 199) if p[1] < H}
    assert set(lit(pens, 0)) == want and all(y < H for _, y in want)


def test_nothing_is_written_with_a_nonsense_position(tmp_path):
    pens, _ = screen(f"clear 0\nline 10 60 4 3 {hexs('ABC')}\nline -5 0 4 3 {hexs('ABC')}\n".replace("-5", "4294967291"), tmp_path)
    assert not lit(pens, 0) or all(0 <= y < H for _, y in lit(pens, 0))


def test_a_scene_line_is_clamped_by_shown_and_length(tmp_path):
    h = hexs("ABCD")
    for shown, length, n in ((0, 4, 0), (2, 4, 2), (9, 4, 4), (4, 2, 2), (4, 0, 0)):
        pens, out = screen(f"clear 0\nscene 20 5 8 {shown} {length} {h}\n", tmp_path)
        want = set()
        for j, ch in enumerate(b"ABCD"[:n]):
            want |= glyph_pixels(ch, 40 + 7 * j, 20)
        assert set(lit(pens, 0)) == want and set(lit(pens, 0).values()) <= {8}, (shown, length)


# ---- the log file (Review Focus 3) ----

def test_logfile_round_trip_creates_the_directories(tmp_path):
    path = tmp_path / "a" / "b" / "flightlog.bin"
    out = run(f"save {path} 0102ff00\nload {path} 16\n", tmp_path).decode().split("\n")
    assert out[0] == "save 0" and out[1] == "load 4 0102ff00"


def test_logfile_missing_is_minus_one_and_oversize_minus_three(tmp_path):
    out = run(f"load {tmp_path}/none.bin 8\nsave {tmp_path}/big.bin 0102030405\nload {tmp_path}/big.bin 4\nload {tmp_path}/big.bin 5\n", tmp_path).decode().split("\n")
    assert out[0] == "load -1 " and out[1] == "save 0" and out[2].startswith("load -3") and out[3] == "load 5 0102030405"


def test_logfile_save_to_an_unwritable_path_fails_cleanly(tmp_path):
    blocker = tmp_path / "file"
    blocker.write_text("x")
    for bad in (blocker / "dir" / "log.bin", Path("/proc/nope/log.bin"), tmp_path):   # a parent that is a file, an unwritable tree, a directory as the target
        out = run(f"save {bad} 00\nload {tmp_path}/none 1\n", tmp_path).decode().split("\n")
        assert out[0].startswith("save -"), (bad, out)
        assert out[1] == "load -1 "                       # the driver (the game) goes on
    assert blocker.read_text() == "x"


def test_a_failed_save_does_not_destroy_the_existing_log(tmp_path):
    path = tmp_path / "log.bin"
    run(f"save {path} aabb\n", tmp_path)
    os.chmod(tmp_path, 0o500)                              # no new files: the temporary cannot be created
    try:
        if os.access(tmp_path, os.W_OK):
            pytest.skip("directory permissions are not enforced (running as root)")
        out = run(f"save {path} ccdd\n", tmp_path).decode()
        assert out.startswith("save -")
    finally:
        os.chmod(tmp_path, 0o700)
    assert path.read_bytes() == bytes.fromhex("aabb")


def test_default_path_follows_xdg_then_home(tmp_path, monkeypatch):
    font = tmp_path / "f.bin"
    font.write_bytes(FONT)

    def path_with(env):
        e = {k: v for k, v in os.environ.items() if k not in ("XDG_CONFIG_HOME", "HOME")}
        e.update(env)
        return subprocess.run([str(DRIVER), str(font)], input=b"defpath\n", capture_output=True, env=e).stdout.decode().split()

    assert path_with({"XDG_CONFIG_HOME": "/x/cfg", "HOME": "/h"}) == ["defpath", "0", "/x/cfg/interceptor/flightlog.bin"]
    assert path_with({"HOME": "/h"}) == ["defpath", "0", "/h/.config/interceptor/flightlog.bin"]
    assert path_with({})[:2] == ["defpath", "-1"]


# ---- captures (skipped without the extracted data) ----

NEEDED = [ASSETS / "fonts" / "menu5x7.bin", ASSETS / "palette" / "menu.pal", ASSETS / "tables" / "text.bin", ASSETS / "tables" / "text_ids.bin"]
have_data = all(p.exists() for p in NEEDED)
OX, OY = 186, 42          # the text screens' game window in the emulator frame (note 01a): two frame pixels per game pixel across, one down


def read_ppm(path):
    data = Path(path).read_bytes()
    parts = data.split(b"\n", 3)
    assert parts[0] == b"P6"
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def capture_window(path):
    w, h, px = read_ppm(path)
    out = []
    for y in range(H):
        for x in range(W):
            i = ((OY + y) * w + OX + 2 * x) * 3
            assert px[i:i + 3] == px[i + 3:i + 6], (x, y)   # a game pixel is two frame pixels wide
            out.append(tuple(px[i:i + 3]))
    return out


def menu_colours():
    raw = (ASSETS / "palette" / "menu.pal").read_bytes()
    return [tuple(CURVE[raw[3 * i + c] // 17] for c in range(3)) for i in range(32)]


def compare(pens, capture):
    pal = menu_colours()
    want = capture_window(capture)
    return sum(1 for i in range(W * H) if pal[pens[i]] != want[i])


# the ids of each captured screen (note 08 2.4) and the line step of the screen
SCREENS = {
    "credits": ([0x0F], 11),
    "menu": ([0x06, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D], 12),
    "log-disk-prompt": ([0x46, 0x5E], 11),
}


@pytest.mark.skipif(not have_data, reason="assets not extracted")
@pytest.mark.parametrize("name", sorted(SCREENS))
def test_driver_records_equal_the_capture(name, tmp_path):
    cap = CAPTURES / f"{name}.ppm"
    if not cap.exists():
        pytest.skip("extracted/captures absent")
    ids, step = SCREENS[name]
    script = "clear 0\n" + "".join(f"rec {i} {step}\n" for i in ids)
    pens, _ = screen(script, tmp_path, ASSETS, (ASSETS / "fonts" / "menu5x7.bin").read_bytes())
    diff = compare(pens, cap)
    print(name, "differing pixels:", diff)
    assert diff == 0


# ---- the text modes of the binary: palette regimes, fades, cuts, keys (synthetic assets, the scene overridden with --text-test) ----

import json
import re
import sys

sys.path.insert(0, str(ROOT / "tools" / "extract"))
from extract.png import write_png  # noqa: E402

VERSION = int(re.search(r"#define\s+CORE_ASSETS_VERSION\s+(\d+)", (ROOT / "src" / "core" / "assets.h").read_text()).group(1))
need_bin = pytest.mark.skipif(not BIN.exists(), reason="build/interceptor not built")


def menu_pal_bytes():
    return bytes(v for i in range(32) for v in (17 * (i % 16) if i % 16 else 0, 17 * ((i * 3) % 16) if i else 0, 17 * ((i * 5) % 16) if i else 0))


def fade_pal_bytes():
    out = bytearray()
    for s in range(16):
        for c in range(16):
            out += bytes((0, 0, 0) if s >= 13 or c == 0 else (17 * ((c + s) % 16), 17 * ((2 * c + s) % 16), 17 * ((c + 3 * s) % 16)))
    return bytes(out)


def synth_assets(tmp_path):
    a = tmp_path / "assets"
    for d in ("pix", "fonts", "palette"):
        (a / d).mkdir(parents=True)
    write_png(a / "pix" / "splsh.png", 320, 200, bytes(320 * 200), [(0, 0, 0), (255, 0, 0)])
    (a / "pix" / "index.txt").write_text("splsh\n")
    (a / "VERSION").write_text(f"{VERSION}\n")
    (a / "manifest.json").write_text(json.dumps({"extractor_version": VERSION, "files": {}}))
    (a / "fonts" / "menu5x7.bin").write_bytes(FONT)
    (a / "palette" / "menu.pal").write_bytes(menu_pal_bytes())
    (a / "palette" / "fade.pal").write_bytes(fade_pal_bytes())
    return a


def run_bin(tmp_path, spec, frames, extra=()):
    a = synth_assets(tmp_path) if not (tmp_path / "assets").exists() else tmp_path / "assets"
    out = tmp_path / "f.ppm"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    r = subprocess.run([str(BIN), "--assets", str(a), "--frames", str(frames), "--scale", "1", "--dump", str(out), "--text-test", spec, *extra],
                       capture_output=True, text=True, env=env)
    assert r.returncode == 0, r.stderr
    px = out.read_bytes()[len(b"P6\n320 200\n255\n"):]
    return [tuple(px[3 * i:3 * i + 3]) for i in range(W * H)], r.stderr


def menu_colour(pen):
    raw = menu_pal_bytes()
    return tuple(CURVE[raw[3 * pen + c] // 17] for c in range(3))


def flight_colour(step, pen):
    raw = fade_pal_bytes()
    o = (step * 16 + pen) * 3
    return tuple(CURVE[raw[o + c] // 17] for c in range(3))


LINES = "t=40:4:9:3:3:ABC;t=60:10:5:2:4:XYZW"


def text_pixels():
    return glyph_pixels(ord("A"), 32, 40) | glyph_pixels(ord("B"), 39, 40) | glyph_pixels(ord("C"), 46, 40)


@need_bin
def test_menu_regime_draws_text_in_menu_palette_pens_on_black(tmp_path):
    px, _ = run_bin(tmp_path, f"mode=menu;regime=0;fade=15;{LINES}", 2)
    p9, p5 = menu_colour(9), menu_colour(5)
    want9 = text_pixels()
    want5 = glyph_pixels(ord("X"), 80, 60) | glyph_pixels(ord("Y"), 87, 60)       # shown = 2 of 4
    for i, c in enumerate(px):
        xy = (i % W, i // W)
        assert c == (p9 if xy in want9 else p5 if xy in want5 else (0, 0, 0)), xy


@need_bin
def test_a_menu_screen_is_a_cut_the_fade_target_does_not_touch_its_colours(tmp_path):
    a, _ = run_bin(tmp_path, f"mode=menu;regime=0;fade=15;{LINES}", 12)
    b, _ = run_bin(tmp_path, f"mode=credits;regime=0;fade=0;{LINES}", 12)
    assert a == b


@need_bin
def test_flight_regime_text_follows_the_fade_through_the_flight_palette(tmp_path):
    # the pause text over the black picture: the pens fade with the picture (target 10 = fade step 5; 15 to 10 takes about 11 blanks, 2 frames of 6.25)
    px, _ = run_bin(tmp_path, f"mode=paused;regime=1;fade=10;blank=1;{LINES}", 10)
    want = text_pixels()
    assert all(px[y * W + x] == flight_colour(5, 9) for x, y in want)
    assert all(px[i] == (0, 0, 0) for i in range(W * H) if (i % W, i // W) not in want | glyph_pixels(ord("X"), 80, 60) | glyph_pixels(ord("Y"), 87, 60))
    # target 15: the full flight palette (step 0)
    px, _ = run_bin(tmp_path, f"mode=select_location;regime=1;fade=15;blank=1;{LINES}", 3)
    assert all(px[y * W + x] == flight_colour(0, 9) for x, y in want)


@need_bin
def test_flight_regime_fade_to_zero_ends_black_and_a_lower_frame_count_is_in_between(tmp_path):
    spec = f"mode=select_aircraft;regime=1;fade=0;blank=1;{LINES}"
    px, _ = run_bin(tmp_path, spec, 12)                               # 75 blanks: level 0
    assert all(px[y * W + x] == (0, 0, 0) for x, y in text_pixels())
    px, _ = run_bin(tmp_path, spec, 2)                                # 12.5 blanks: part way
    c = {px[y * W + x] for x, y in text_pixels()}
    assert len(c) == 1 and c != {(0, 0, 0)} and c != {flight_colour(0, 9)}


@need_bin
def test_text_is_drawn_over_the_scene_behind_it(tmp_path):
    px, _ = run_bin(tmp_path, f"mode=select_location;regime=1;fade=15;blank=0;{LINES}", 3)
    want = text_pixels()
    assert all(px[y * W + x] == flight_colour(0, 9) for x, y in want)


@need_bin
def test_a_missing_font_is_reported_once_and_does_not_stop_the_game(tmp_path):
    a = synth_assets(tmp_path)
    (a / "fonts" / "menu5x7.bin").unlink()
    px, err = run_bin(tmp_path, f"mode=menu;regime=0;fade=15;{LINES}", 3)
    assert err.count("menu5x7.bin") == 1 and all(c == (0, 0, 0) for c in px)


@need_bin
def test_esc_quits_on_the_credits_only(tmp_path):
    _, err = run_bin(tmp_path, "mode=credits;regime=0;fade=15", 50, ("--key", "Escape@3"))
    assert "quit" in err
    for mode in ("logdisk", "menu", "select_location", "gameover", "flight", "paused"):
        _, err = run_bin(tmp_path, f"mode={mode};regime=0;fade=15", 50, ("--key", "Escape@3"))
        assert "quit" not in err, mode


@need_bin
def test_a_held_key_is_one_press(tmp_path):
    # the repeats of a held key (SDL's repeat flag) are dropped on every screen, so a held ESC or RETURN never carries into the next screen:
    # on the credits a repeat of ESC does not quit; a first press does
    _, err = run_bin(tmp_path, "mode=credits;regime=0;fade=15", 50, ("--keyrep", "Escape@3"))
    assert "quit" not in err
    _, err = run_bin(tmp_path, "mode=credits;regime=0;fade=15", 50, ("--key", "Escape@3", "--keyrep", "Escape@4", "--keyrep", "Escape@5"))
    assert "quit" in err


# ---- the screens of the core through the binary against the captures (real assets and captures, skipped without) ----

have_core_screens = have_data and (ASSETS / "tables" / "scenario.bin").exists() and BIN.exists()


def binary_frame(tmp_path, frames, keys=(), logdisk=3):
    out = tmp_path / "frame.ppm"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    args = [str(BIN), "--assets", str(ASSETS), "--scale", "1", "--frames", str(frames), "--dump", str(out), "--logdisk", str(logdisk)]   # the captures show the write-protected disk prompt (note 08 5.3)
    for opt, name in keys:
        args += [opt, name]
    r = subprocess.run(args, capture_output=True, text=True, env=env)
    assert r.returncode == 0, r.stderr
    px = out.read_bytes()[len(b"P6\n320 200\n255\n"):]
    return [tuple(px[3 * i:3 * i + 3]) for i in range(W * H)]


def frame_diff(frame, capture):
    want = capture_window(capture)
    return sum(1 for a, b in zip(frame, want) if a != b)


# (capture, frames, keys): the credits fully typed; the log-disk prompt after any key; the menu after RETURN (the first-event filter and the build delay need the frames)
SCENARIOS = {
    "credits": (60, []),
    "log-disk-prompt": (100, [("--key", "A@60")]),
    "menu": (160, [("--key", "A@60"), ("--key", "Return@100")]),
}


@pytest.mark.skipif(not have_core_screens, reason="real assets or build/interceptor absent")
@pytest.mark.parametrize("name", sorted(SCENARIOS))
def test_the_core_screens_through_the_binary_equal_the_capture(name, tmp_path):
    cap = CAPTURES / f"{name}.ppm"
    if not cap.exists():
        pytest.skip("extracted/captures absent")
    frames, keys = SCENARIOS[name]
    diff = frame_diff(binary_frame(tmp_path, frames, keys), cap)
    print(name, "differing pixels:", diff)
    assert diff == 0


@pytest.mark.skipif(not have_core_screens, reason="real assets or build/interceptor absent")
def test_a_held_return_is_one_screen_change(tmp_path):
    cap = CAPTURES / "log-disk-prompt.ppm"
    if not cap.exists():
        pytest.skip("extracted/captures absent")
    # RETURN held down across the credits (any key) and the log-disk prompt (RETURN): the first press leaves the credits, its repeats do not leave the prompt
    held = [("--key", "Return@60")] + [("--keyrep", f"Return@{f}") for f in range(61, 95, 3)]
    assert frame_diff(binary_frame(tmp_path, 100, held), cap) == 0
    fresh = [("--key", "Return@60"), ("--key", "Return@70")]
    assert frame_diff(binary_frame(tmp_path, 100, fresh), cap) > 0                 # a new press does


@pytest.mark.skipif(not have_core_screens, reason="real assets or build/interceptor absent")
def test_the_start_keys_reach_the_flight(tmp_path):
    out = tmp_path / "f2.ppm"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    r = subprocess.run([str(BIN), "--assets", str(ASSETS), "--scale", "1", "--start-keys", "--frames", "160", "--dump", str(out)], capture_output=True, text=True, env=env)
    assert r.returncode == 0, r.stderr
    px = out.read_bytes()[len(b"P6\n320 200\n255\n"):]
    assert len({px[i:i + 3] for i in range(0, len(px), 3)}) >= 8            # a flight frame (sky, ground, panel), not a text screen of at most six colours


@pytest.mark.skipif(not have_core_screens, reason="real assets or build/interceptor absent")
@pytest.mark.parametrize("name,frames,keys,limit", [
    ("freeflight-select", 62, [("--key", "A@2"), ("--key", "2@15")], 6),
    ("aircraft-select", 75, [("--key", "A@2"), ("--key", "2@15"), ("--key", "1@50")], 8),
])
def test_the_selection_screens_show_the_bay_map_with_the_numbers_like_the_capture(name, frames, keys, limit, tmp_path):
    """The core puts the map overview (the camera above cell (66, 67) at height 0x3000000, looking straight down) and the numbers of the starts into the scene
    (note 08 5.6): the whole 320 x 200 frame equals the capture except a few pixels of the map renderer (a small polygon edge)."""
    cap = CAPTURES / f"{name}.ppm"
    if not cap.exists():
        pytest.skip("extracted/captures absent")
    diff = frame_diff(binary_frame(tmp_path, frames, keys, logdisk=0), cap)   # the default check result: no log-disk prompt, any key goes to the menu
    print(name, "differing pixels:", diff)
    assert diff <= limit
