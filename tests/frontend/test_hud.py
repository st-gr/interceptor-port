# tests/frontend/test_hud.py - HUD, panel, warning line, strip and head views of the built frontend, headless.
# Rules are those of docs/re/06-cockpit-hud.md. The scene is injected with --hud-test (see hud.h), the overlay is
# read back as pens with --dump-layer (320x200 bytes, 255 = nothing drawn) and as pixels with --dump-window.
# Real assets are git-ignored, so these tests skip when build/interceptor or assets/ is absent.
import os
import struct
import subprocess
from pathlib import Path

import pytest

from emu_palette import convert

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "interceptor"
DRIVER = ROOT / "build" / "hud_driver"
ASSETS = ROOT / "assets"
CAPTURES = ROOT / "extracted" / "captures"
NONE = 255

needed = [ASSETS / "fonts" / "hud.bin", ASSETS / "cockpit" / "masks.bin", ASSETS / "cockpit" / "warnings.bin",
          ASSETS / "pix" / "inst5.idx", ASSETS / "pix" / "frnt5.idx", ASSETS / "palette" / "flight.pal",
          ASSETS / "cockpit" / "arcs.bin", ASSETS / "cockpit" / "markers.bin", ASSETS / "cockpit" / "mapshapes.bin"]
pytestmark = [
    pytest.mark.skipif(not BIN.exists(), reason="build/interceptor not built"),
    pytest.mark.skipif(not all(p.exists() for p in needed), reason="cockpit assets not extracted (run the extractor)"),
]

FONT = (ASSETS / "fonts" / "hud.bin").read_bytes() if needed[0].exists() else b""
MASKS = (ASSETS / "cockpit" / "masks.bin").read_bytes() if needed[1].exists() else b""
WARN = (ASSETS / "cockpit" / "warnings.bin").read_bytes() if needed[2].exists() else b""
ARCS = (ASSETS / "cockpit" / "arcs.bin").read_bytes() if needed[6].exists() else b""
MARKERS = (ASSETS / "cockpit" / "markers.bin").read_bytes() if needed[7].exists() else b""


def s8(b):
    return b - 256 if b > 127 else b


def marker_points(first, count, cx, cy):
    return {(cx + s8(MARKERS[2 * (first + i)]), cy + s8(MARKERS[2 * (first + i) + 1])) for i in range(count)}


def lock_circle_points(cx, cy):
    """The quarter arc of the extracted list (20 pairs) reflected four ways (note 06 'Lock circle point list')."""
    pts = set()
    for i in range(20):
        dx, dy = s8(ARCS[2 * i]), s8(ARCS[2 * i + 1])
        pts |= {(cx + dx, cy - dy), (cx + dx, cy + dy - 1), (cx - dx, cy + dy - 1), (cx - dx, cy - dy)}
    return pts
# the flight state of the note's "Cockpit frame, runway idle" example (class, weapon byte, stock, class etc.)
BASE = "class=17,sel=13,stock=36,throttle=9,w00=4552,rounds=500,heading80=2240,y=1800,fuel=6399880,zoom=128"


_COUNTER = [0]
HIGH = 3000000      # camera altitude (position units): high above the carrier, so the window shows sea and sky, no black deck polygons


def run(spec="", frames=12, scale=1, extra=(), tmp=None, view_args=("--flight", "--cockpit"), window=None, assets=None):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    _COUNTER[0] += 1
    out = Path(tmp) / f"t{_COUNTER[0]}"
    cmd = [str(BIN), "--assets", str(assets or ASSETS), "--scale", str(scale), *view_args, "--frames", str(frames),
           "--dump", f"{out}.ppm", "--dump-layer", f"{out}.layer", "--dump-window", f"{out}.win.ppm", *extra]
    if spec:
        cmd += ["--hud-test", spec]
    if window:
        cmd += ["--window-size", window]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    assert r.returncode == 0, r.stderr
    return Layer(Path(f"{out}.layer").read_bytes()), Path(f"{out}.ppm"), Path(f"{out}.win.ppm")


class Layer:
    def __init__(self, data):
        assert len(data) == 320 * 200
        self.d = data

    def pen(self, x, y):
        return self.d[y * 320 + x]

    def pixels(self, pen, x0=0, x1=319, y0=0, y1=199):
        return {(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1) if self.d[y * 320 + x] == pen}

    def glyph(self, x, y, pen, letters=False):
        """The character of the 3x5 cell at (x, y) whose glyph pixels are `pen`. The font draws S like 5, so
        `letters` prefers the letters when it is ambiguous."""
        bits = tuple(sum(1 << (2 - c) for c in range(3) if self.pen(x + c, y + r) == pen) for r in range(5))
        return glyph_char(bits, letters)

    def text(self, xs, y, pen, letters=False):
        return "".join(self.glyph(x, y, pen, letters) for x in xs)


def font_bits(ch):
    g = FONT[5 * (ord(ch) - 0x20):5 * (ord(ch) - 0x20) + 5]
    return tuple(b >> 5 for b in g)


CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ -.:/"
_GLYPHS, _LETTERS = {}, {}
for _c in CHARS:
    if FONT:
        _GLYPHS.setdefault(font_bits(_c), _c)
for _c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ" + CHARS:
    if FONT:
        _LETTERS.setdefault(font_bits(_c), _c)


def glyph_char(bits, letters=False):
    return (_LETTERS if letters else _GLYPHS).get(bits, "?")


def glyph_pixels(ch, x, y):
    b = font_bits(ch)
    return {(x + c, y + r) for r in range(5) for c in range(3) if (b[r] >> (2 - c)) & 1}


def palette():
    raw = (ASSETS / "palette" / "flight.pal").read_bytes()
    return convert(raw)


def read_ppm(path):
    data = Path(path).read_bytes()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    assert len(px) == w * h * 3
    return w, h, [tuple(px[i:i + 3]) for i in range(0, len(px), 3)]


# ---- fixed marks (note 06, "Fixed marks") ------------------------------------------------------------------------

def test_fixed_marks(tmp_path):
    L, _, _ = run(BASE, tmp=tmp_path)
    assert L.pixels(10, 93, 224, 37, 37) == {(x, 37) for x in range(93, 225)}                 # outline top
    assert {(85 + i, 45 - i) for i in range(8)} <= L.pixels(8)                                  # left chamfer (85,45)-(92,38)
    assert {(225 + i, 37 + i) for i in range(9)} <= L.pixels(8)                                 # right chamfer (225,37)-(233,45)
    assert all(L.pen(85, y) == 0 and L.pen(233, y) == 0 for y in range(46, 145))              # black sides over the world
    crosshair = {(159, y) for y in (71, 72, 74, 75)} | {(x, 73) for x in range(157, 162)}
    assert crosshair <= L.pixels(10)
    assert {(x, 91) for x in list(range(141, 146)) + list(range(173, 178))} <= L.pixels(10)    # level marks
    datum = {(x, y) for x in (159, 160) for y in (129, 130, 131)} | {(x, 132) for x in range(158, 162)}
    assert datum <= L.pixels(8)


def test_class_zero_has_no_outline(tmp_path):
    L, _, _ = run(BASE.replace("class=17", "class=0"), tmp=tmp_path)
    assert not L.pixels(10, 93, 224, 37, 37)
    assert L.pixels(10, 141, 145, 91, 91)            # the level marks are not class dependent


# ---- heading tape --------------------------------------------------------------------------------------------------

# The heading tape rule of note 06 ("Heading tape"), worked out by hand for explicit headings: h in tenths of a degree = word >> 3; r = h mod 100;
# r above 40 takes the next multiple of 10 as the base label and s = r - 100, else the multiple below and s = r; o = -(s / 5) truncated;
# base tick x = 159 + o, ticks every 10 pixels for x 134..184, two pixels tall (rows 60, 61) at the base and every 20 pixels from it, else row 61;
# labels are two glyphs at x and x + 4, rows 53..57: the base at 156 + o, base + 10 at 176 + o when o is at most 2, base - 10 at 136 + o when o is
# at least -2; the centre mark x 159 rows 63..65. (heading word, o, tall ticks, short ticks, [(label, x)])
TAPE_CASES = [
    (0,        0, [139, 159, 179],      [149, 169],      [("00", 156), ("01", 176), ("35", 136)]),
    (90 * 80,  0, [139, 159, 179],      [149, 169],      [("09", 156), ("10", 176), ("08", 136)]),
    (180 * 80, 0, [139, 159, 179],      [149, 169],      [("18", 156), ("19", 176), ("17", 136)]),
    (270 * 80, 0, [139, 159, 179],      [149, 169],      [("27", 156), ("28", 176), ("26", 136)]),
    (359 * 80, 2, [141, 161, 181],      [151, 171],      [("00", 158), ("01", 178), ("35", 138)]),   # h 359.0: r = 90, base 36 wraps to 00
    (45 * 80,  10, [149, 169],          [139, 159, 179], [("05", 166), ("04", 146)]),                 # r = 50, base 50, s = -50
    (2240,     4, [143, 163, 183],      [153, 173],      [("03", 160), ("02", 140)]),                 # the note's worked example, heading 28.0
    (2256,     3, [142, 162, 182],      [152, 172],      [("03", 159), ("02", 139)]),                 # 28.2 rounds to 30 (note 06)
    (1920,    -8, [151, 171],           [141, 161, 181], [("02", 148), ("03", 168)]),                 # 24.0 stays 20 (note 06)
]


@pytest.mark.parametrize("h80,o,tall,short,labels", TAPE_CASES)
def test_heading_tape_explicit_cases(tmp_path, h80, o, tall, short, labels):
    L, _, _ = run(BASE.replace("heading80=2240", f"heading80={h80}"), tmp=tmp_path)
    exp = {(x, 61) for x in tall + short} | {(x, 60) for x in tall} | {(159, y) for y in (63, 64, 65)}
    for text, x in labels:
        exp |= glyph_pixels(text[0], x, 53) | glyph_pixels(text[1], x + 4, 53)
    assert L.pixels(13, 120, 200, 53, 65) == exp


def test_tape_worked_example(tmp_path):
    """heading 28.0: base label 03 at x 160, 02 at 140, ticks at 143..183 (note 06 'Cockpit frame, runway idle')."""
    L, _, _ = run(BASE, tmp=tmp_path)
    assert L.text([160, 164], 53, 13) == "03" and L.text([140, 144], 53, 13) == "02"
    assert {(x, 61) for x in (143, 153, 163, 173, 183)} <= L.pixels(13)
    assert {(x, 60) for x in (143, 163, 183)} <= L.pixels(13) and not {(153, 60), (173, 60)} & L.pixels(13)
    assert not L.pixels(13, 176, 183, 53, 57)


# ---- numeric readouts ------------------------------------------------------------------------------------------------

def readouts(L):
    speed = L.text([92, 98, 102, 106], 84, 13).strip()
    alt = L.text([204, 208, 212, 218, 222, 226], 84, 13).strip()
    return speed, alt


def g_text(L):
    sign = L.glyph(92, 121, 13)
    tens = L.glyph(96, 121, 13)
    units = L.glyph(102, 121, 13)
    assert L.pen(100, 125) == 13
    return (sign + tens + units).replace(" ", "")


@pytest.mark.parametrize("speed_w,y,rate,gcorr,gsign,exp", [
    (0, 1800, 0, 0, 0, ("0", "5", "10")),                      # runway idle: 1.0 (v = -10), blank sign
    (4726, 706604, 14, 0, 0, ("393", "3450", "9")),            # note 06 level state: speed 4726 / 12, 5 * (706604 >> 10), G 0.9
    (4422, 681120, 24, 0, 0, ("368", "3325", "7")),
    (4751, 711068, 8, -73, 0, ("395", "3470", "18")),
])
def test_numeric_readouts_worked_examples(tmp_path, speed_w, y, rate, gcorr, gsign, exp):
    spec = BASE.replace("y=1800", f"y={y}") + f",speed_w={speed_w},pitch_rate={rate},gcorr={gcorr},gsign={gsign}"
    L, _, _ = run(spec, tmp=tmp_path)
    speed, alt = readouts(L)
    assert (speed, alt) == exp[:2]
    assert L.text([102, 106], 91, 13, True) == "KT" and L.text([222, 226], 91, 13, True) == "FT"
    assert g_text(L) == exp[2].replace(" ", "")
    assert L.glyph(108, 121, 13) == "G"


def test_g_sign_and_wrap(tmp_path):
    # v >= 0 prints a minus sign at x 92 (note 06: "printed with a minus sign when v is at least 0")
    L, _, _ = run(BASE + ",gsign=1", tmp=tmp_path)             # v = +10 with a zero pitch rate
    assert L.glyph(92, 121, 13) == "-" and L.glyph(96, 121, 13) == "1" and L.glyph(102, 121, 13) == "0"
    L, _, _ = run(BASE + ",pitch_rate=800", tmp=tmp_path)      # (800 + 100) >> 3 = 112, minus 10 = 102: 10.2 wraps to 0.2
    assert L.glyph(96, 121, 13) == " " and L.glyph(102, 121, 13) == "2"


F16 = BASE.replace("class=17", "class=16")


def g(chars_xs, top):
    px = set()
    for ch, x in chars_xs:
        px |= glyph_pixels(ch, x, top)
    return px


def test_f16_scale_form_at_rest(tmp_path):
    """Class 0x10, speed 0 and altitude 0 (note 06 'F-16 scale form'): L = 0 gives N = 0, d = 0, R0 = 0, centre row 91 and label row 89; labels N + 5k above at
    89 - 15k while at least 70 (so one: 5 at row 74), none below a label of 0; ticks every 3 rows, the centre row and every fifth also light the second
    column; pointers; the altitude labels show three digits and a point four rows below the glyph top."""
    L, _, _ = run(F16.replace("y=1800", "y=0"), tmp=tmp_path)
    up = [91 - 3 * k for k in range(0, 7)]                       # 91, 88, ..., 73 (the next, 70, is above row 72)
    down = [91 + 3 * k for k in range(1, 7)]                     # 94 .. 109 (112 is below row 111)
    ticks = {(105, y) for y in up + down} | {(104, y) for y in (91, 76, 106)}
    atick = {(212, y) for y in up + down} | {(213, y) for y in (91, 76, 106)}
    pointers = {(x, 91) for x in range(107, 112)} | {(109, 86), (110, 86), (108, 87), (108, 88), (109, 89), (110, 89)}
    apointer = {(x, 91) for x in range(206, 211)}
    labels = g([("0", 100)], 89) | g([("5", 100)], 74)
    alabels = g([("0", 215), ("0", 219), ("0", 225)], 89) | {(223, 93)} | g([("0", 215), ("0", 219), ("5", 225)], 74) | {(223, 78)}
    gfig = g([("1", 110), ("0", 117)], 61) | {(114, 65)}          # the F-16 G figure: no sign for level flight (v = -10), no letter G
    assert L.pixels(13, 85, 125, 53, 125) == ticks | pointers | labels | gfig
    assert L.pixels(13, 200, 233, 53, 125) == atick | apointer | alabels


def test_f16_speed_scale_stacks_labels_and_ticks(tmp_path):
    """120 knots: L = 20 is case 1, N = 10, R0 = 6, centre row 97, label row 95; above: 15 at row 80 (row 65 is above 70); below: row 110 is beyond 109."""
    L, _, _ = run(F16.replace("y=1800", "y=0") + ",speed_w=1440", tmp=tmp_path)
    px = L.pixels(13, 90, 112, 60, 125)
    labels = g([("1", 96), ("0", 100)], 95) | g([("1", 96), ("5", 100)], 80)
    up = [97 - 3 * k for k in range(0, 9)]                       # 97 .. 73
    down = [97 + 3 * k for k in range(1, 5)]                     # 100 .. 109
    ticks = {(105, y) for y in up + down} | {(104, y) for y in (97, 82)}
    pointer = {(x, 91) for x in range(107, 112)} | {(109, 86), (110, 86), (108, 87), (108, 88), (109, 89), (110, 89)}
    gfig = g([("1", 110), ("0", 117)], 61) | {(114, 65)}
    assert px - gfig == labels | ticks | pointer, (sorted(px - gfig - (labels | ticks | pointer)), sorted((labels | ticks | pointer) - px))


def test_f16_altitude_scale_label_with_point(tmp_path):
    """2,500 feet: T = 250 tens of feet, L = 50 is case 2, N = 25, d = -50, R0 = 0: the centre label reads '02.5', 30 above at row 74 and 20 below at row 104."""
    L, _, _ = run(F16 + ",y=512000", tmp=tmp_path)             # 5 * (512000 >> 10) = 2500 feet
    px = L.pixels(13, 205, 232, 60, 125)
    labels = (g([("0", 215), ("2", 219), ("5", 225)], 89) | {(223, 93)} | g([("0", 215), ("3", 219), ("0", 225)], 74) | {(223, 78)} |
              g([("0", 215), ("2", 219), ("0", 225)], 104) | {(223, 108)})
    ys = [91 - 3 * k for k in range(0, 7)] + [91 + 3 * k for k in range(1, 7)]
    ticks = {(212, y) for y in ys} | {(213, y) for y in (91, 76, 106)}
    pointer = {(x, 91) for x in range(206, 211)}
    assert px == labels | ticks | pointer


def test_class_0x12_gets_the_numeric_readouts_with_the_f16_g_figure(tmp_path):
    L, _, _ = run(BASE.replace("class=17", "class=18"), tmp=tmp_path)
    assert readouts(L)[0] == "0" and L.text([102, 106], 91, 13, True) == "KT"
    assert L.glyph(110, 61, 13) == "1" and L.glyph(117, 61, 13) == "0" and L.pen(114, 65) == 13
    assert L.pixels(10, 93, 224, 37, 37)                            # the outline is drawn for 0x10, 0x11 and 0x12


def test_g_sign_slot_of_the_f16_form(tmp_path):
    L, _, _ = run(F16 + ",gsign=1", tmp=tmp_path)                  # v = +10: a minus sign at x 106
    assert L.glyph(106, 61, 13) == "-" and L.glyph(110, 61, 13) == "1" and L.glyph(117, 61, 13) == "0"


def test_hit_flag_zeroes_the_speed(tmp_path):
    L, _, _ = run(BASE.replace("w00=4552", f"w00={4552 | 0x8000}") + ",speed_w=4726", tmp=tmp_path)
    assert readouts(L)[0] == "0"


def test_weapon_readout(tmp_path):
    gun, _, _ = run(BASE.replace("sel=13", "sel=29"), tmp=tmp_path)
    assert gun.text([92, 96, 100], 129, 13, True) == "GUN" and gun.text([108, 112, 116], 129, 13) == "500"
    amr, _, _ = run(BASE.replace("sel=13", "sel=61"), tmp=tmp_path)          # 0x3D: AMRAAM, low nibble of the stock (4)
    assert amr.text([96, 100], 129, 13, True) == "AM" and amr.glyph(108, 129, 13) == "4"
    sw, _, _ = run(BASE.replace("sel=13", "sel=45"), tmp=tmp_path)           # 0x2D: Sidewinder, high nibble (2)
    assert sw.text([96, 100], 129, 13, True) == "SW" and sw.glyph(108, 129, 13) == "2"
    none, _, _ = run(BASE, tmp=tmp_path)
    assert not none.pixels(13, 90, 120, 129, 133)


@pytest.mark.parametrize("rounds,exp", [(0, "  0"), (5, "  5"), (50, " 50"), (500, "500"), (1234, "234"), (0xFFFF, "535")])
def test_gun_rounds_are_modulo_1000_with_leading_zeros_blank(tmp_path, rounds, exp):
    """Note 06 'Leading digits': the unsigned word modulo 1000, leading zeros blank, the last digit always drawn."""
    L, _, _ = run(BASE.replace("sel=13", "sel=29").replace("rounds=500", f"rounds={rounds}"), tmp=tmp_path)
    assert L.text([108, 112, 116], 129, 13) == exp
    assert L.text([92, 96, 100], 129, 13, True) == "GUN"


def test_missile_stock_zero_draws_no_digit_but_keeps_the_tag(tmp_path):
    L, _, _ = run(BASE.replace("sel=13", "sel=61").replace("stock=36", "stock=32"), tmp=tmp_path)   # AMRAAM, low nibble 0
    assert L.text([96, 100], 129, 13, True) == "AM" and not L.pixels(13, 106, 120, 129, 133)
    L, _, _ = run(BASE.replace("sel=13", "sel=45").replace("stock=36", "stock=4"), tmp=tmp_path)    # Sidewinder, high nibble 0
    assert L.text([96, 100], 129, 13, True) == "SW" and not L.pixels(13, 106, 120, 129, 133)


@pytest.mark.parametrize("rate,exp", [(0, ("", "1", "0")), (50, ("", "", "3")), (14, ("", "", "9"))])
def test_g_below_one_shows_the_point_and_one_digit(tmp_path, rate, exp):
    """v = ((w + w/8) >> 3) - 10; |v| in tenths; level flight prints 1.0, a figure below 1.0 prints '.4' / '.9' with no leading zero."""
    L, _, _ = run(BASE + f",pitch_rate={rate}", tmp=tmp_path)
    assert L.glyph(96, 121, 13) == (exp[1] or " ") and L.glyph(102, 121, 13) == exp[2]
    assert L.pen(100, 125) == 13


SHOOT = BASE.replace("sel=13", "sel=29") + ",target=159:91,designated=1,range=100,pipper_x=159,pipper_y=91,prompt=1"
PROMPT_MISSILE, PROMPT_GUN = b"AB CDE", b" FGHJK"      # invented strings: the frontend draws what tables/prompts.bin holds


def assets_with_prompts(tmp_path, missile=PROMPT_MISSILE, gun=PROMPT_GUN):
    """A copy of the assets directory (symlinks) whose tables/prompts.bin holds the given six-character strings."""
    root = Path(tmp_path) / "assets-prompts"
    if not root.exists():
        root.mkdir()
        for e in ASSETS.iterdir():
            if e.name != "tables":
                (root / e.name).symlink_to(e)
        (root / "tables").mkdir()
        for e in (ASSETS / "tables").iterdir():
            if e.name != "prompts.bin":
                (root / "tables" / e.name).symlink_to(e)
    (root / "tables" / "prompts.bin").write_bytes(struct.pack("<2H", 2, 6) + missile + gun)
    return root


def test_the_fire_prompt_is_drawn_whenever_the_core_says_v4a96c_is_not_zero(tmp_path):
    """Note 07 12.7: the prompt 0x36930 is drawn whenever V:4A96C is non-zero after the update; the condition is the core's (the frontend has no rule of its own for it). The strings are the game's
    (tables/prompts.bin, six characters from x 204, a blank is not drawn); the gun form for the selection nibble 1, the missile form otherwise."""
    a = assets_with_prompts(tmp_path)
    xs = [204, 208, 212, 216, 220, 224]
    L, _, _ = run(SHOOT, tmp=tmp_path, assets=a)
    assert L.text(xs, 129, 13, True) == " FGHJK"                                      # the gun
    L, _, _ = run(SHOOT.replace("sel=29", "sel=45"), tmp=tmp_path, assets=a)
    assert L.text(xs, 129, 13, True) == "AB CDE" and not L.pixels(13, 212, 214, 129, 133)   # a missile: the blank slot at 212 is not drawn
    L, _, _ = run(SHOOT.replace("sel=29", "sel=45").replace("prompt=1", "prompt=2"), tmp=tmp_path, assets=a)   # state 2 (fast closing) draws it too
    assert L.text(xs, 129, 13, True) == "AB CDE"
    for bad in (SHOOT.replace("prompt=1", "prompt=0"),                             # V:4A96C is 0: nothing, whatever the geometry
                SHOOT.replace("range=100", "range=2305").replace("prompt=1", "prompt=0")):
        L, _, _ = run(bad, tmp=tmp_path, assets=a)
        assert not L.pixels(13, 200, 230, 129, 133)
    L, _, _ = run(SHOOT.replace("pipper_x=159", "pipper_x=200").replace("range=100", "range=9999"), tmp=tmp_path, assets=a)   # the core said so: the frontend does not second-guess it
    assert L.text(xs, 129, 13, True) == " FGHJK"
    other = assets_with_prompts(tmp_path, b"LM NOP", b"QRTUVW")                      # other strings: the text follows the table
    L, _, _ = run(SHOOT, tmp=tmp_path, assets=other)
    assert L.text(xs, 129, 13, True) == "QRTUVW"


def test_target_box_is_19_by_17_with_68_pixels(tmp_path):
    """Corners (x-9,y-8) .. (x+9,y+8) in pen 10, lit columns x-9..x+9 and rows y-7..y+9 (note 06 'Target box')."""
    L, _, _ = run(BASE + ",target=120:100", frames=1, tmp=tmp_path)
    box = L.pixels(10, 105, 135, 90, 112)
    exp = {(x, y) for x in range(111, 130) for y in range(93, 110) if x in (111, 129) or y in (93, 109)}
    assert box == exp and len(box) == 68


def test_target_box_is_clamped_to_the_hud_window_and_skipped_when_wholly_outside(tmp_path):
    L, _, _ = run(BASE + ",target=90:50", frames=1, tmp=tmp_path)
    # top side (y 42) wholly above row 45 and left side (x 81) wholly left of 86 are skipped; the right side x 99 starts at the clamped row 45
    # (lit from row 46) and the bottom side y 58 is drawn one row lower, clamped on the left to x 86
    assert L.pixels(10, 80, 110, 40, 70) == {(x, 59) for x in range(86, 100)} | {(99, y) for y in range(46, 59)}
    L, _, _ = run(BASE + ",target=60:100", frames=1, tmp=tmp_path)      # both vertical sides at x 51 and 69: wholly left of 86
    assert not L.pixels(10, 40, 85, 85, 115)


def test_marker_diamond_when_far_and_lock_ring_when_close(tmp_path):
    """The core decides the kind (note 07 12.7: 1 the diamond, 2 the ring of the lock box) and the place (the slew); the frontend draws the shapes of note 06."""
    w = BASE.replace("sel=13", "sel=29")
    L, _, _ = run(w + ",target=100:60,marker=1:155:87", frames=1, tmp=tmp_path)
    assert marker_points(0, 12, 155, 87) <= L.pixels(10)
    assert len(L.pixels(10, 150, 160, 83, 92)) == 12                      # radius 3: 12 points
    ring7, _, _ = run(w + ",target=159:91,marker=2:159:91", frames=7, tmp=tmp_path)        # frame counter 7: mod 4 is not zero, the ring is drawn
    ring8, _, _ = run(w + ",target=159:91,marker=2:159:91", frames=8, tmp=tmp_path)        # mod 4 is zero: the ring blinks off
    appeared = ring7.pixels(10) - ring8.pixels(10)
    exp = marker_points(12, 44, 159, 91)
    assert exp - ring8.pixels(10) == appeared and len(appeared) >= 30      # the 44 point list, minus the points other pen 10 marks cover anyway
    assert min(x for x, _ in exp) == 148 and max(x for x, _ in exp) == 169 and min(y for _, y in exp) == 81 and max(y for _, y in exp) == 100
    none, _, _ = run(w + ",target=100:60", frames=1, tmp=tmp_path)         # the core says no marker (kind 0)
    assert not (marker_points(0, 12, 155, 87) <= none.pixels(10))
    f16, _, _ = run(w.replace("class=17", "class=16") + ",target=100:60,marker=1:155:87", frames=1, tmp=tmp_path)   # not drawn for the F-16
    assert not (marker_points(0, 12, 155, 87) <= f16.pixels(10))


def test_the_jitter_moves_the_target_box_not_the_marker(tmp_path):
    """Note 06 'Target box': with the pitch rate nonzero a jitter pair (index (E+0x14 and 0x1C) >> 2) is added to the target point the box is taken from; the marker is not jittered."""
    w = BASE.replace("sel=13", "sel=29") + ",target=120:100,marker=1:155:87"
    still, _, _ = run(w, frames=1, tmp=tmp_path)
    moved, _, _ = run(w + ",pitch_rate=5,lon=12", frames=1, tmp=tmp_path)          # (12 and 0x1C) >> 2 = 3: the pair (1, 1)
    box = lambda L: L.pixels(10, 105, 135, 90, 112)
    assert {(x + 1, y + 1) for x, y in box(still)} == box(moved)
    assert marker_points(0, 12, 155, 87) <= moved.pixels(10)


def test_lock_circle_and_range_dot(tmp_path):
    """The core's circle (note 07 12.7: gun or missile) is 74 distinct pixels (note 06 'Lock circle point list'); the range dot sits on the second arc at the core's index k; the pipper pixel is its own flag."""
    circle = lock_circle_points(159, 91)
    assert len(circle) == 74
    spec = BASE.replace("sel=13", "sel=29") + ",pipper_x=159,pipper_y=91"
    base, _, _ = run(spec, frames=1, tmp=tmp_path)                           # nothing from the core: no circle, no dot, no pipper pixel (the base shares pixels with the fixed marks)
    assert len(base.pixels(10) & circle) < 20
    L, _, _ = run(spec + ",circle=159:91,pip=1", frames=1, tmp=tmp_path)
    assert L.pixels(10) - base.pixels(10) == circle - base.pixels(10)       # the circle is pen 10
    assert L.pen(159, 91) == 13 and base.pen(159, 91) != 13                 # the pipper pixel is pen 13 (note 07 12.7: the HUD draw leaves the pen word at 13)
    assert min(x for x, _ in circle) == 145 and max(x for x, _ in circle) == 173 and min(y for _, y in circle) == 79 and max(y for _, y in circle) == 102
    L, _, _ = run(spec + ",circle=159:91,dot=0", frames=1, tmp=tmp_path)
    assert L.pixels(13, 140, 180, 75, 107) - base.pixels(13) == {(158, 80), (159, 80), (158, 81), (159, 81)}   # k = 0: x -1..0, rows -11..-10
    L, _, _ = run(spec + ",circle=159:91,dot=40", frames=1, tmp=tmp_path)    # k = 40: the bottom, around k = 38 (x -1..0, rows 10..11)
    dot = L.pixels(13, 140, 180, 75, 107) - base.pixels(13)
    assert len(dot) == 4 and {y - 91 for _, y in dot} == {10, 11}
    L, _, _ = run(spec + ",circle=159:91,dot=76", frames=1, tmp=tmp_path)    # k = 76 and more: nothing
    assert not (L.pixels(13, 140, 180, 75, 107) - base.pixels(13))
    L, _, _ = run(spec + ",circle=159:91", frames=1, tmp=tmp_path)           # the core's dot is -1 (no target): no dot
    assert not (L.pixels(13, 140, 180, 75, 107) - base.pixels(13))
    # outside the full-arc window (centre x 99 or 219) the arc goes through the dot's clip: 67 pixels (note 06, measured in the harness)
    L, _, _ = run(spec + ",circle=99:91", frames=1, tmp=tmp_path)
    assert len((L.pixels(10) - base.pixels(10))) in (66, 67, 68)            # circle 67, less overlaps


# ---- views and head offsets (note 06 "Head views") ---------------------------------------------------------------

@pytest.mark.parametrize("head,step", [(0, 0), (1, -7), (2, -14), (11, 7), (10, 14)])
def test_head_view_offsets_the_overlay(tmp_path, head, step):
    L, _, _ = run(BASE + f",head={head}", tmp=tmp_path)
    off = 16 * step
    assert all(L.pen(159 + off + dx, 71) == 10 for dx in (0,)) if 0 <= 159 + off < 320 else True
    # the whole panel moves with the HUD: its top-left corner pixel is where the offset puts it
    if off < 0:
        assert L.pen(0, 150) != NONE and L.pen(319 + off, 150) != NONE and L.pen(319 + off + 1, 150) == NONE
    elif off > 0:
        assert L.pen(319, 150) != NONE and L.pen(off, 150) != NONE and L.pen(off - 1, 150) == NONE
    else:
        assert L.pen(0, 150) != NONE and L.pen(319, 150) != NONE


def black_below(px, rows):
    return all(p == (0, 0, 0) for p in px[(rows + 1) * 320:])


@pytest.mark.parametrize("head,rows", [(3, 167), (5, 179), (7, 179), (12, 167), (13, 167), (4, 167), (8, 167), (9, 167)])
def test_head_views_without_panel_draw_nothing_and_extend_the_window(tmp_path, head, rows):
    L, ppm, _ = run(BASE + f",head={head},cam_pitch=0,cam_roll=0,cam_y={HIGH}", tmp=tmp_path)
    assert all(p == NONE for p in L.d)                                # HUD and panel are off the screen: nothing is drawn
    w, h, px = read_ppm(ppm)
    assert black_below(px, rows)                                      # below the window's last row is black
    assert any(p != (0, 0, 0) for p in px[rows * 320:(rows + 1) * 320])   # and the window reaches that row


@pytest.mark.parametrize("head,col", [(0, 160), (1, 300), (2, 300), (10, 10), (11, 10)])
def test_cockpit_window_rows(tmp_path, head, col):
    """The 3D window ends at row 144 in the cockpit; the panel and cowl cover the rows below except where a head
    view moved them away (then it is black)."""
    L, ppm, _ = run(BASE + f",head={head},cam_pitch=0,cam_roll=0,cam_y={HIGH}", tmp=tmp_path)
    w, h, px = read_ppm(ppm)
    if head:
        assert px[150 * 320 + col] == (0, 0, 0) and px[160 * 320 + col] == (0, 0, 0)    # uncovered: black
        assert px[143 * 320 + col] != (0, 0, 0) and px[144 * 320 + col] != (0, 0, 0)    # the world reaches row 144
    assert L.pen(160 if not head else 160 + 16 * {1: -7, 2: -14, 10: 14, 11: 7}[head], 150) != NONE or head


# ---- horizon alignment at two window scales (Review Focus 4) ----------------------------------------------------

def horizon_row(px, w, col, scale):
    """First ground row (window pixels) in a column, divided by the scale."""
    pal = palette()
    for y in range(0, 150 * scale):
        if px[y * w + col] == pal[4]:   # the ground base is the land pen 4 (note 04)
            return y
    return None


@pytest.mark.parametrize("scale", [1, 2, 3, 4])
@pytest.mark.parametrize("pitch", [0, 600, -900])
def test_hud_stays_aligned_with_the_horizon_at_any_window_scale(tmp_path, scale, pitch):
    """The level marks (original row 91) cover the same world rows at every scale: one original pixel = scale x scale
    window pixels, and the world's horizon is at scale times its 1x row."""
    L1, _, w1 = run(BASE + f",cam_pitch={pitch},cam_roll=0", scale=1, tmp=tmp_path)
    Ls, _, ws = run(BASE + f",cam_pitch={pitch},cam_roll=0", scale=scale, tmp=tmp_path)
    assert L1.d == Ls.d                                               # the overlay logic is scale independent
    W1, H1, px1 = read_ppm(w1)
    Ws, Hs, pxs = read_ppm(ws)
    assert (W1, H1, Ws, Hs) == (320, 200, 320 * scale, 200 * scale)
    pal = palette()
    r1 = horizon_row(px1, W1, 100, 1)
    rs = horizon_row(pxs, Ws, 100 * scale, scale)
    assert r1 is not None and rs == r1 * scale                        # the world horizon scales exactly
    # the level marks: original row 91, x 141..145, are scale-by-scale cells of pen 10 in the window
    for x in range(141, 146):
        for dy in range(scale):
            for dx in range(scale):
                assert pxs[(91 * scale + dy) * Ws + x * scale + dx] == pal[10]
    if pitch == 0:
        assert r1 in (90, 91)                                         # the horizon of level flight is at the marks (row 91)
        assert pxs[(r1 * scale - 1) * Ws + 100 * scale] == pal[5]     # sky just above it, at every scale
    # the marks do not move with the pitch: they are at row 91 whatever the horizon does
    assert {(x, 91) for x in range(141, 146)} <= Ls.pixels(10)


WINDOWS = [("640x400", 2, 0, 0), ("1000x700", 3, 20, 50), ("800x450", 2, 80, 25), ("320x200", 1, 0, 0), ("1366x768", 3, 203, 84)]


@pytest.mark.parametrize("size,k,ox,oy", WINDOWS)
def test_window_scale_is_the_largest_integer_factor_letterboxed(tmp_path, size, k, ox, oy):
    """The window is resizable: the 320x200 frame is scaled by the integer factor that fits and centred in black bars. Review focus 4: at two
    or more window sizes the overlay and the world stay aligned, because every original pixel is one k x k cell of the same frame."""
    spec = BASE + f",cam_pitch=600,cam_roll=0,cam_y={HIGH}"
    _, ppm, win = run(spec, tmp=tmp_path, window=size)
    w, h = map(int, size.split("x"))
    W, H, wpx = read_ppm(win)
    assert (W, H) == (w, h)
    _, f1, _ = run(spec, tmp=tmp_path)
    _, _, fpx = read_ppm(f1)
    # the 1x frame is the reference; every cell of the window frame has its colour (sampled rows keep the test quick)
    for y in range(0, 200, 3):
        for x in range(320):
            c = fpx[y * 320 + x]
            for (dx, dy) in ((0, 0), (k - 1, k - 1)):
                assert wpx[(oy + y * k + dy) * W + ox + x * k + dx] == c, (x, y, dx, dy)
    black = (0, 0, 0)
    if ox:
        assert all(wpx[y * W + x] == black for y in range(H) for x in list(range(ox)) + list(range(W - ox, W)))     # the bars at the sides
    if oy:
        assert all(wpx[y * W + x] == black for y in list(range(oy)) + list(range(H - oy, H)) for x in range(W))    # and at top and bottom


def test_resizing_the_window_while_running_changes_the_scale(tmp_path):
    out = tmp_path / "r"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    cmd = [str(BIN), "--assets", str(ASSETS), "--scale", "1", "--flight", "--cockpit", "--frames", "6", "--resize-at", "3", "960x600",
           "--hud-test", BASE, "--dump", f"{out}.ppm", "--dump-window", f"{out}.win.ppm"]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    assert r.returncode == 0, r.stderr
    W, H, wpx = read_ppm(f"{out}.win.ppm")
    _, _, fpx = read_ppm(f"{out}.ppm")
    assert (W, H) == (960, 600)                                         # the buffer follows the new size, scale 3
    assert all(wpx[(3 * y + 1) * W + 3 * x + 1] == fpx[y * 320 + x] for y in range(0, 200, 5) for x in range(0, 320, 3))


def test_window_resolution_overlay_has_no_gaps(tmp_path):
    """At scale 3 every crosshair pixel is a full 3x3 cell (no interpolation, no half cells)."""
    L, _, ws = run(BASE, scale=3, tmp=tmp_path)
    W, H, px = read_ppm(ws)
    pal = palette()
    for (x, y) in L.pixels(10, 150, 170, 70, 76):
        assert all(px[(3 * y + dy) * W + 3 * x + dx] == pal[10] for dy in range(3) for dx in range(3))


# ---- panel ----------------------------------------------------------------------------------------------------------

def test_panel_picture_is_below_row_145_and_cowl_above(tmp_path):
    L, _, _ = run(BASE, tmp=tmp_path)
    assert all(L.pen(x, y) != NONE for y in (145, 170, 199) for x in (0, 100, 319))
    assert L.pixels(8, 16, 303, 133, 144)                              # the cowl, cookie-cut: world shows where it has pen 0
    assert any(L.pen(x, 140) == NONE for x in range(16, 304))


def panel_digits(L, xs, y, fg=4):
    return L.text(xs, y, fg).strip()


@pytest.mark.parametrize("y,speed_w,thr,exp", [
    (706604, 4726, 120, ("3450", "393", "100")),
    (1800, 0, 9, ("5", "0", "7")),
])
def test_panel_digits(tmp_path, y, speed_w, thr, exp):
    spec = BASE.replace("y=1800", f"y={y}").replace("throttle=9", f"throttle={thr}") + f",speed_w={speed_w}"
    L, _, _ = run(spec, frames=12, tmp=tmp_path)
    assert panel_digits(L, [240, 244, 248, 254, 258, 262], 158) == exp[0]
    assert panel_digits(L, [242, 248, 252, 256], 166) == exp[1]
    assert panel_digits(L, [240, 244, 248], 175) == exp[2]


def test_panel_magnification_range_fuel_latlon(tmp_path):
    L, _, _ = run(BASE + ",lat=279184624,lon=273963296", tmp=tmp_path)
    assert L.text([242, 248], 184, 4) == "10"
    assert L.text([148, 152], 184, 4) == "40"                          # low nibble 13: anything else is 40
    assert L.text([104, 108, 112, 116, 120], 183, 0).strip() == "24999"
    assert L.text([295, 299, 305], 180, 0) == "376"                    # 37.6 N (the picture has the point)
    assert L.text([292, 295, 299, 305], 188, 0) == "1223"              # 122.3 W
    L, _, _ = run(BASE.replace("zoom=128", "zoom=64").replace("sel=13", "sel=9"), tmp=tmp_path)
    assert L.text([242, 248], 184, 4) == "20" and L.text([148, 152], 184, 4).strip() == "2"
    L, _, _ = run(BASE.replace("sel=13", "sel=11").replace("zoom=128", "zoom=32"), tmp=tmp_path)
    assert L.text([242, 248], 184, 4) == "40" and L.text([148, 152], 184, 4) == "10"


def test_lat_lon_leading_zeros_are_blank_and_the_digit_before_the_point_is_always_drawn(tmp_path):
    """Latitude 'NN.N' glyphs at 295, 299, 305; longitude 'NNN.N' at 292, 295, 299, 305 (note 06 'Leading digits')."""
    lat = 0x10000000 - 325 * 0x7000 * 256            # tenths: 375 - 325 = 50 -> " 5" "0": the tens digit blank
    lon = 0x0F000000 + (900 - 1220) * 0x5999 * 256   # tenths: 900 -> the first digit blank, then "900"
    L, _, _ = run(BASE + f",lat={lat},lon={lon}", tmp=tmp_path)
    assert L.text([295, 299, 305], 180, 0) == " 50"
    assert L.text([292, 295, 299, 305], 188, 0) == " 900"
    assert all(L.pen(x, y) == 5 for x in (295, 296, 297) for y in range(180, 185))        # the blank cell is repainted with the box pen
    assert all(L.pen(x, y) == 5 for x in (292, 293, 294) for y in range(188, 193))


@pytest.mark.parametrize("fuel_pounds,rows", [
    (24999, 22), (23552, 22), (22528, 21), (5000, 3), (2048, 1), (1024, 0), (1000, 0), (0, 0)])
def test_fuel_bar(tmp_path, fuel_pounds, rows):
    """Lit rows = min(22, max(0, level - 1)) with level = (digits & 0x7FFF) >> 10: a full tank (level 24) lights the 22 rows 159..180,
    level 23 already does, level 22 lights 21 (note 06 'fuel bar')."""
    L, _, _ = run(BASE.replace("fuel=6399880", f"fuel={fuel_pounds * 256}"), tmp=tmp_path)
    lit = {y for y in range(159, 181) if L.pen(112, y) == 4}
    assert lit == set(range(181 - rows, 181))
    assert all(L.pen(x, y) == 4 for y in lit for x in (111, 112, 113, 114))
    assert all(L.pen(x, y) == 0 for y in range(159, 181) if y not in lit for x in (111, 112, 113, 114))


@pytest.mark.parametrize("head,off", [(1, -112), (2, -224)])
def test_fuel_bar_is_not_drawn_when_the_view_moves_it_off(tmp_path, head, off):
    """The routine returns when 112 plus the view offset is at most 0: the clean picture's own bar (19 lit rows, 162..180) shows, shifted."""
    L, _, _ = run(BASE + f",head={head}", tmp=tmp_path)
    pic = (ASSETS / "pix" / "inst5.idx").read_bytes()
    def picture(x, y):
        p = pic[(y - 145) * 320 + x] & 15
        return 11 if p == 9 else 9 if p == 11 else p
    for y in range(159, 181):
        for x in range(111, 115):
            if 0 <= x + off < 320:
                assert L.pen(x + off, y) == picture(x, y)
    if off + 112 > 0:
        assert {y for y in range(159, 181) if L.pen(112 + off, y) == 4} == set(range(162, 181))


def compass_model(deg):
    d = deg
    scroll = ((d // 2) * 135 >> 8) - 4
    if scroll < 0:
        scroll += 96
    strip = MASKS[0x100:0x100 + 84]
    px = {}
    for j in range(32):
        col = (j + scroll // 2) % 96
        for r in range(7):
            bit = (strip[r * 12 + col // 8] >> (7 - col % 8)) & 1
            px[(192 + j, 151 + r)] = 2 if bit else 3
    for r in range(7):
        px[(208, 151 + r)] = 1
    return px, scroll


@pytest.mark.parametrize("deg,scroll", [(0, 92), (45, 7), (90, 19), (180, 43), (270, 67), (359, 90), (28, 3)])
def test_compass(tmp_path, deg, scroll):
    L, _, _ = run(BASE.replace("heading80=2240", f"heading80={deg * 80 + 40}"), tmp=tmp_path)
    model, s = compass_model(deg)
    assert s == scroll                                                 # the note's measured scrolls
    assert all(L.pen(x, y) == pen for (x, y), pen in model.items())


BALL_SPANS = [(206, 212), (204, 214), (202, 216), (201, 217), (200, 218), (199, 219), (199, 219), (198, 220), (198, 220), (198, 220),
              (198, 220), (198, 220), (198, 220), (199, 219), (199, 219), (200, 218), (200, 218), (201, 217), (202, 216), (204, 214), (206, 212)]


def ball_rows(L):
    """For rows 163..183: the set of x with the ball's two pens (sky 5, ground 1) or the overlays drawn on it."""
    return {y: {x for x in range(196, 223) if L.pen(x, y) in (1, 5, 2, 13)} for y in range(163, 184)}


def test_attitude_ball_tiny_polygon_draws_pen_3_outside_the_ellipse_mask(tmp_path):
    """Note 04, 'The attitude ball's tiny cases': a ball polygon of one point goes through the filler's ball entry (A = 199) and draws pen 3 straight into the page, outside the ellipse
    mask: the scaled point (100, 0) -> (202, 162) puts a dot at (202, 163), where row 163's ellipse spans only 206..212."""
    L, _, _ = run(BASE + ",cam_pitch=0,cam_roll=0,ball=100:0:100:0:100:0", tmp=tmp_path)
    assert L.pen(202, 163) == 3
    assert {(x, 163) for x in range(196, 223) if L.pen(x, 163) in (1, 5)} == {(x, 163) for x in range(206, 213)}


def test_attitude_ball_level_sky_down_to_row_172(tmp_path):
    """Level flight on the ground: the sky polygon is the rectangle (0,89)..(319,0), scaled 89 -> row 172: rows 163..172 are sky and rows
    173..183 ground, over the 21 rows of the ball mask (note 06 'Attitude ball')."""
    L, _, _ = run(BASE + ",cam_pitch=0,cam_roll=0", tmp=tmp_path)
    rows = ball_rows(L)
    for i, (a, b) in enumerate(BALL_SPANS):
        y = 163 + i
        assert rows[y] == set(range(a, b + 1)), y
        for x in range(a, b + 1):
            if L.pen(x, y) in (1, 5):
                assert L.pen(x, y) == (5 if y <= 172 else 1)
    assert {(x, 173) for x in range(204, 215)} <= L.pixels(2)          # the fixed aircraft symbol
    assert {(209, 171), (209, 172)} <= L.pixels(2)
    for (x, y) in [(206, 165), (204, 166), (203, 167), (201, 170), (218, 171), (218, 172), (218, 173), (217, 175)]:
        assert L.pen(x, y) == 13                                       # bank dots


def test_attitude_ball_polygon_scaling_rule(tmp_path):
    """A horizon whose last sky row is 120 (pitch 1520 puts the edge at 120.996) scales to row 176 (note 06: 'a polygon down to y 120 gives sky down to row 176')."""
    L, _, _ = run(BASE + ",cam_pitch=1520,cam_roll=0", tmp=tmp_path)
    assert L.pen(210, 176) == 5 and L.pen(210, 177) == 1
    # all sky, all ground
    L, _, _ = run(BASE + ",cam_pitch=6400,cam_roll=0", tmp=tmp_path)    # looking far up: the horizon is far below the window
    assert {L.pen(210, y) for y in range(163, 173)} == {5} and L.pen(210, 183) == 5
    L, _, _ = run(BASE + ",cam_pitch=-6400,cam_roll=0", tmp=tmp_path)
    assert {L.pen(210, y) for y in range(163, 173)} == {1}


def test_attitude_ball_inverted_and_banked(tmp_path):
    inv, _, _ = run(BASE + ",cam_pitch=0,cam_roll=14400", tmp=tmp_path)
    assert inv.pen(210, 164) == 1 and inv.pen(210, 182) == 5          # sky below when inverted
    # banked: the horizon is the world's horizon scaled into the ball, so the sky/ground boundary rises on one side
    bank, _, win = run(BASE + ",cam_pitch=0,cam_roll=2400", tmp=tmp_path)
    left = [y for y in range(163, 184) if bank.pen(204, y) == 5]
    right = [y for y in range(163, 184) if bank.pen(216, y) == 5]
    assert left and right and len(left) != len(right)                  # not level
    lvl, _, _ = run(BASE + ",cam_pitch=0,cam_roll=0", tmp=tmp_path)
    assert sum(1 for y in range(163, 184) for x in range(190, 226) if bank.pen(x, y) == 5) > 20
    # the ball and the world agree in level flight: the boundary row in the ball matches the world's horizon scaled by 31/256 (rows) and 24/256 (columns), within a
    # row. (Banked, they do not: the ball's matrix is the attitude matrix without the zoom row scale, the world's carries it, so the world's horizon is steeper by
    # 252 / 168; the ball against the captured frames is the test below.)
    L1, _, w1 = run(BASE + f",cam_pitch=0,cam_roll=0,cam_y={HIGH}", scale=1, tmp=tmp_path)
    W, H, px = read_ppm(w1)
    pal = palette()
    def world_horizon(col):
        for y in range(1, 145):
            if px[y * W + col] == pal[4]:   # the land pen 4
                return y
        return None
    for xb in (200, 206, 212, 218):
        X = (xb - 193) * 256 // 24 + 5
        ys = [y for y in range(163, 184) if L1.pen(xb, y) == 5]
        if not ys:
            continue
        edge = max(ys)                                                 # last sky row of the ball in this column
        hw = world_horizon(X)
        assert hw is not None
        assert abs(edge - (162 + ((hw - 1) * 31 >> 8))) <= 2, (xb, edge, hw)


ATT = CAPTURES / "logs" / "att.txt"


def att_attitude(label):
    for line in ATT.read_text().splitlines():
        p = line.split()
        if p and p[0] == f"rec0_{label}":
            rec = bytes.fromhex("".join(p[2:]))
            return struct.unpack_from(">h", rec, 0x66)[0], struct.unpack_from(">h", rec, 0x6A)[0]


@pytest.mark.skipif(not (ATT.exists() and (CAPTURES / "openers" / "att-p0.ppm").exists()), reason="reference captures not present")
@pytest.mark.parametrize("label,limit", [("p20up", 0), ("p10up", 0), ("p0", 0), ("p10dn", 0), ("r10", 10), ("r20", 10), ("r30", 10), ("r45", 10), ("l30", 10)])
def test_attitude_ball_matches_the_reference_frames(tmp_path, label, limit):
    """The sky/ground of the ball against the captured att-* frames (attitudes logged exactly: pitch and roll words of the record; the scene carries the roll word, the ball negates it): pitch-only
    horizons are exact, banked ones differ in a few edge pixels (the blitter's edge pixel of a sloped line)."""
    pw, rw = att_attitude(label)
    L, _, _ = run(BASE + f",cam_pitch={-pw},cam_roll={rw},cam_y={HIGH}", tmp=tmp_path)
    w, px = ref_pixels(CAPTURES / "openers" / f"att-{label}.ppm")
    bad = cells = 0
    for y in range(163, 184):
        for x in range(192, 224):
            pen = L.pen(x, y)
            if pen not in (1, 5):
                continue
            o = 3 * ((42 + y) * w + 186 + 2 * x)
            cap = tuple(px[o:o + 3])
            if cap not in ((43, 43, 89), (106, 0, 0)):
                continue                                           # the panel's own pixels
            cells += 1
            bad += (pen == 5) != (cap == (43, 43, 89))
    assert cells > 300 and bad <= limit, (label, bad, cells)


def lamp_pen(L, x, y):
    return L.pen(x, y)


def test_lamps(tmp_path):
    off, _, _ = run(BASE, frames=12, tmp=tmp_path)
    brake, _, _ = run(BASE.replace("w00=4552", f"w00={4552 | 0x800}"), frames=12, tmp=tmp_path)
    ecm, _, _ = run(BASE + ",ecm=1", frames=12, tmp=tmp_path)
    for (x, y) in [(12, 172), (20, 173), (30, 175)]:
        assert brake.pen(x, y) == off.pen(x, y) | 1                    # BRAKE box x 9..32, rows 169..175: pen OR 1
    assert brake.pen(12, 160) == off.pen(12, 160)                      # outside the box: unchanged
    for (x, y) in [(295, 168), (300, 170), (309, 172)]:
        assert ecm.pen(x, y) == off.pen(x, y) | 1                      # ECM box x 292..309, rows 166..172
    # afterburner line: bit 3 of E+0x03 gives pen 9, else pen 0 (row 181, x 241..250)
    ab, _, _ = run(BASE + ",b03=8", tmp=tmp_path)
    assert all(ab.pen(x, 181) == 9 for x in range(241, 251)) and all(off.pen(x, 181) == 0 for x in range(241, 251))


@pytest.mark.parametrize("frames", [10, 11, 12, 13, 14, 15])
def test_gear_lamp_flashes_with_bit_1_of_the_frame_counter(tmp_path, frames):
    off, _, _ = run(BASE, frames=frames, tmp=tmp_path)
    on, _, _ = run(BASE + ",gear_lamp=1", frames=frames, tmp=tmp_path)
    lit = (frames & 2) != 0                                            # scene.tick equals the frame count here
    for (x, y) in [(12, 184), (20, 186), (30, 189)]:
        assert on.pen(x, y) == (off.pen(x, y) | 1 if lit else off.pen(x, y))


@pytest.mark.parametrize("variant,count,first,last", [(0, 25, 190, 195), (1, 24, 190, 194), (2, 25, 190, 193), (3, 27, 189, 192)])
def test_gear_symbol_variants(tmp_path, variant, count, first, last):
    """Variant v = (E+0x7C without bit 7) >> 5 draws the mask at 0x42B1C - 0x1C v (index 3 - v) from row 189: its pen 0 pixels number 25, 24, 25, 27
    and fall in rows 190..195, 190..194, 190..193, 189..192 (note 06 gear symbol and 'Addendum rules')."""
    L, _, _ = run(BASE + f",b7c={variant << 5}", tmp=tmp_path)
    clean, _, _ = run(BASE + ",b7c=0", tmp=tmp_path)
    pic = (ASSETS / "pix" / "inst5.idx").read_bytes()
    sym = {(x, y) for y in range(189, 197) for x in range(10, 31)
           if L.pen(x, y) == 0 and (pic[(y - 145) * 320 + x] & 15) not in (0,)}      # pen 0 where the picture has the lamp
    assert len(sym) == count
    assert min(y for _, y in sym) == first and max(y for _, y in sym) == last
    assert all(14 <= x <= 28 for x, _ in sym)


def test_threat_and_stall_lamps(tmp_path):
    L, _, _ = run(BASE + ",threat=4", tmp=tmp_path)                    # bit 2: hostile in range, lamp A pen 1
    assert L.pen(293, 156) == 1 and L.pen(298, 156) == 3
    L, _, _ = run(BASE + ",threat=2", tmp=tmp_path)                    # bit 1 friendly: lamp B pen 4
    assert L.pen(298, 157) == 4 and L.pen(293, 156) == 3
    L, _, _ = run(BASE + ",threat=48", tmp=tmp_path)                   # bits 4 and 5: lamps C and D pen 2
    assert L.pen(298, 160) == 2 and L.pen(293, 160) == 2
    st2, _, _ = run(BASE + ",b20=4", frames=10, tmp=tmp_path)          # stall: lamp E pen 1 on frames with bit 1 set
    st0, _, _ = run(BASE + ",b20=4", frames=9, tmp=tmp_path)
    assert (st2.pen(303, 160), st0.pen(303, 160)) == (1, 3)


def test_radar_own_ship_plus_and_cone_lines(tmp_path):
    L, _, _ = run(BASE, tmp=tmp_path)
    assert L.pen(158, 167) == 12 and all(L.pen(x, 168) == 12 for x in (157, 158, 159))      # a plus of four pixels in pen 12
    cone_l = {(154 - i, 163 - i) for i in range(16)}          # (154,163) .. (139,148)
    cone_r = {(162 + i, 163 - i) for i in range(16)}          # (162,163) .. (177,148)
    assert cone_l | cone_r <= L.pixels(3)
    assert (138, 147) not in cone_l and L.pen(139, 148) == 3 and L.pen(177, 148) == 3


# ---- external strip and map ---------------------------------------------------------------------------------------

def test_external_strip(tmp_path):
    L, ppm, _ = run(BASE + ",speed_w=4284,y=648192,view=1", view_args=("--flight",), tmp=tmp_path)
    assert L.text([84, 88, 92], 184, 10, True) == "HDG" and L.text([100, 104, 108], 184, 10) == "028"
    assert L.text([150, 154, 158], 184, 10).strip() == "357" and L.text([167, 171, 175], 184, 10, True) == "KTS"
    assert L.text([208, 212, 216, 222, 226, 230], 184, 10).strip() == "3165" and L.text([239, 243], 184, 10, True) == "FT"
    assert not L.pixels(13)                                            # no HUD in the outside view
    L, _, _ = run(BASE + ",speed_w=4284,y=648192,view=1,crashed=1", view_args=("--flight",), tmp=tmp_path)
    assert all(p == NONE for p in L.d)                                 # the strip disappears after the crash hand-over


def test_the_message_line_is_hidden_in_the_external_views_unless_the_mode_byte_is_2(tmp_path):
    """Note 08 8.7 (0x11426): training part 1 shows the lesson names in the external view; every other mode hides the line there."""
    spec = BASE + ",speed_w=4284,y=648192,view=1,msg_entry=23"
    L, _, _ = run(spec, view_args=("--flight",), tmp=tmp_path)
    assert shown_text(L)[0].strip() == ""
    L, _, _ = run(spec + ",msg_ext=1", view_args=("--flight",), tmp=tmp_path)
    t, pen = shown_text(L)
    assert t.strip() == entry_text(23).strip() and pen == (1 if WARN[28 * 23] >> 6 == 2 else 4)
    assert L.text([84, 88, 92], 184, 10, True) == "HDG"                # the strip is drawn as before


TOWER = BASE + ",view=2,cam_x=273963296,cam_z=279184624"


def test_tower_map_grid_and_labels_at_0x7000000(tmp_path):
    """Lines at the columns and rows the note's projection gives (verified there against the capture); labels leading zeros printed."""
    L, _, _ = run(TOWER + ",cam_y=117440512", view_args=("--flight",), tmp=tmp_path)           # altitude 0x7000000
    cols = [x for x in range(320) if sum(1 for y in range(1, 170) if L.pen(x, y) == 8) > 120]
    rows = [y for y in range(1, 180) if sum(1 for x in range(320) if L.pen(x, y) == 8) > 200]
    assert cols == [42, 94, 147, 199, 251, 304] and rows == [47, 101, 156]
    assert L.text([140, 144, 148], 174, 2) == "122" and L.text([36, 40, 44], 174, 2) == "123"
    assert L.text([8, 12], 154, 2) == "37" and L.text([8, 12], 45, 2) == "38"


def map_columns(L):
    return [x for x in range(320) if sum(1 for y in range(1, 170) if L.pen(x, y) == 8) > 120]


def test_tower_map_line_rule_for_other_positions(tmp_path):
    """The camera is the entity's position: lines are 448 high-word units apart in x (24 of them), 560 in z (16), the labelled ones the odd ones.
    Moving the camera by two line spacings moves nothing on screen except the labels' numbers."""
    a, _, _ = run(TOWER + ",cam_y=117440512", view_args=("--flight",), tmp=tmp_path)
    b, _, _ = run(BASE + ",view=2,cam_x=%d,cam_z=279184624,cam_y=117440512" % (273963296 + 2 * 448 * 65536), view_args=("--flight",), tmp=tmp_path)
    assert map_columns(a) == map_columns(b)
    assert b.text([140, 144, 148], 174, 2) == "123"                  # the same screen column now carries the next line


def test_tower_map_labels_snap_to_four_pixels_and_skip_the_left_edge(tmp_path):
    L, _, _ = run(TOWER + ",cam_y=117440512", view_args=("--flight",), tmp=tmp_path)
    for x in (42, 147, 251):                                         # the odd lines carry a label
        d0 = min(x - 12, 300)
        x0 = 4 * (d0 >> 2) + 8
        assert L.pixels(2, x0, x0 + 11, 174, 178), x
        assert L.text([x0, x0 + 4, x0 + 8], 174, 2).strip().isdigit()
    for x in (94, 199, 304):                                         # the even ones do not
        x0 = 4 * (min(x - 12, 300) >> 2) + 8
        assert not L.pixels(2, x0, x0 + 11, 174, 178), x
    # a labelled line left of column 20 (line x - 12 below 8) is not labelled: move the camera 200 high-word units so the line at 42 is at about 19
    L, _, _ = run(BASE + ",view=2,cam_x=%d,cam_z=279184624,cam_y=117440512" % (273963296 + 200 * 65536), view_args=("--flight",), tmp=tmp_path)
    cols = map_columns(L)
    assert cols and min(cols) < 20
    assert not L.pixels(2, 0, 30, 174, 178)


@pytest.mark.parametrize("name,alt", [("tower-1", 0x3000000), ("tower-2", 0x1000000), ("tower-low", 0x7000000)])
@pytest.mark.skipif(not (CAPTURES / "views" / "tower-1.ppm").exists(), reason="reference captures not present")
def test_tower_frames_match_the_captures(tmp_path, name, alt):
    """Grid, labels and the ship mark of the three captured tower frames (one aircraft position): every overlay pixel of pens 8, 2 and 3
    equals the capture (the carrier's black ship shape is the 21 pixels at column 71, rows 107..109 in tower-1)."""
    L, _, _ = run(TOWER + f",cam_y={alt}", view_args=("--flight",), tmp=tmp_path)
    w, px = ref_pixels(CAPTURES / "views" / f"{name}.ppm")
    colors = {8: (56, 56, 56), 2: (106, 106, 106)}
    n = 0
    for y in range(180):
        for x in range(320):
            pen = L.pen(x, y)
            if pen in colors:
                o = 3 * ((42 + y) * w + 186 + 2 * x)
                assert tuple(px[o:o + 3]) == colors[pen], (x, y)
                n += 1
    assert n > 400


MAPSHAPES = (ASSETS / "cockpit" / "mapshapes.bin").read_bytes() if needed[8].exists() else b""


def shape_triples(k):
    """The triples (dy, dx, length) of shape k of the extracted table (17 offset words, then the shapes in order, each ended by 0x80); read here by the test, apart from the frontend's reader."""
    p = 34
    for _ in range(k):
        while MAPSHAPES[p] != 0x80:
            p += 3
        p += 1
    out = []
    while MAPSHAPES[p] != 0x80:
        out.append((s8(MAPSHAPES[p]), s8(MAPSHAPES[p + 1]), MAPSHAPES[p + 2]))
        p += 3
    return out


def shape_pixels(k, x, y):
    """Where shape k lands for the point (x, y) the core hands over: a run on row y + dy + 1 (the line routine draws a run one row below its y) from x + dx for `length` pixels;
    a run is dropped when y + dy is outside 0 to 179, x + dx is negative or its last pixel is beyond 319 (0x3019C)."""
    px = set()
    for dy, dx, ln in shape_triples(k):
        if y + dy < 0 or y + dy > 179 or x + dx < 0 or x + dx + ln - 1 > 319:
            continue
        px |= {(x + dx + i, y + dy + 1) for i in range(ln)}
    return px


def mark(rec, x, y, shape, pen):
    return f",mark={rec}:{x}:{y}:{shape}:{pen}"


def tower(marks, tmp_path):
    return run(TOWER + ",cam_y=117440512" + "".join(marks), frames=1, view_args=("--flight",), tmp=tmp_path)[0]


def test_tower_draws_the_cores_marks_with_the_shape_the_point_and_the_pen(tmp_path):
    """The marks are the core's decision (src/core/mapmarks.c, tests/test_mapmarks.c): the frontend plots shape k of the extracted table at the point in the pen the core gives."""
    for shape in (0, 1, 8, 15):
        L = tower([mark(0, 159, 90, shape, 13)], tmp_path)
        assert shape_pixels(shape, 159, 90) and shape_pixels(shape, 159, 90) <= L.pixels(13)
    assert shape_pixels(0, 159, 90) != shape_pixels(8, 159, 90)                              # the table has distinct shapes
    L = tower([mark(5, 100, 60, 3, 1), mark(7, 220, 120, 11, 3)], tmp_path)                  # several marks, other pens
    assert shape_pixels(3, 100, 60) <= L.pixels(1) and shape_pixels(11, 220, 120) <= L.pixels(3)
    assert len(shape_triples(0)) == 5                                                        # sector 0 has five rows (note 06 "Marks"; the rows themselves are disk data)


def test_tower_ship_shape_is_the_seventeenth(tmp_path):
    # the carrier's ship shape (three rows, note 06 "Marks") in pen 3 around column 71, row 107 (the tower-1 frame)
    assert len(shape_triples(16)) == 3
    L = tower([mark(14, 71, 107, 16, 3)], tmp_path)
    assert shape_pixels(16, 71, 107) and shape_pixels(16, 71, 107) <= L.pixels(3)


def test_tower_runs_are_dropped_by_the_shape_routines_clip_and_the_last_pixel_319_is_inside(tmp_path):
    """0x3019C: a run whose last pixel x + dx + length - 1 is beyond 319 is dropped, one ending at 319 is drawn; a negative x + dx or a row outside 0 to 179 is dropped."""
    for x in (316, 317, 318, 319):
        L = tower([mark(0, x, 90, 0, 13)], tmp_path)
        assert shape_pixels(0, x, 90) <= L.pixels(13)
    assert (319, 92) in shape_pixels(0, 317, 90) and (319, 92) not in shape_pixels(0, 318, 90)    # the five-pixel run x - 2 .. x + 2 ends at 319 for x = 317
    L = tower([mark(0, 1, 90, 0, 13)], tmp_path)
    assert (1, 91) in shape_pixels(0, 1, 90) and not shape_pixels(0, 1, 90) & {(x, 92) for x in range(0, 5)}   # the run starting at x - 2 = -1 is dropped
    assert shape_pixels(0, 1, 90) <= L.pixels(13) and not L.pixels(13, 0, 6, 92, 92)


# ---- the gating cadence (hud_driver) ------------------------------------------------------------------------------

def drive(lines):
    assert DRIVER.exists(), "build/hud_driver not built"
    r = subprocess.run([str(DRIVER), str(ASSETS)], input="\n".join(lines) + "\n", capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return [tuple(int(v) for v in ln.split()) for ln in r.stdout.splitlines()]


TICK, SPEED, ALT, THR, FUEL, BAR, MSG, CODE, EJ, SDRAW, ADRAW, FDRAW = range(12)


def test_speed_cache_rules_one_to_four():
    """Rules of 'Gating a replica must keep' for the speed (window n and 3 of 2 or 3): (1) R > 0 draws every frame, (2) a set flag is cleared and
    the cached figure drawn once more (not the current one), (3) an equal value draws nothing, (4) a new figure only when bit 0 of n is clear."""
    rows = drive([f"{n} {BASE},speed_w={12 * (10 + n)}" for n in range(1, 21)])
    by = {r[TICK]: r for r in rows}
    assert [by[n][SPEED] for n in (1, 2, 3)] == [11, 12, 13] and by[3][SDRAW] == 3        # rule 1: R = 3 on the first three frames
    assert by[4][SDRAW] == 3 and by[5][SDRAW] == 3                                        # n and 3 of 0 and 1: the routine is not called
    assert by[6][SDRAW] == 4 and by[6][SPEED] == 13                                       # rule 2: the flagged repeat draws the cached 13, not the current 16
    assert by[7][SDRAW] == 4 and by[7][SPEED] == 13                                       # rule 4: n odd, a new figure is not taken
    assert by[10][SDRAW] == 5 and by[10][SPEED] == 20                                     # a new figure at n and 3 = 2
    assert by[11][SDRAW] == 6 and by[11][SPEED] == 20                                     # rule 2 again: the repeat shows the cached figure, not 21
    assert by[14][SPEED] == 24 and by[15][SPEED] == 24


def test_speed_equal_value_draws_nothing():
    rows = drive([f"{n} {BASE},speed_w=4726" for n in range(1, 41)])
    assert rows[2][SDRAW] == 3                         # R > 0 on the first three frames
    assert rows[5][SDRAW] == 4                         # n = 6: the flagged repeat
    assert rows[-1][SDRAW] == 4                        # rule 3: the value equals the cache, nothing is drawn for the next 34 frames


def test_altitude_cache_rules_and_window():
    rows = drive([f"{n} {BASE},y={(1000 + 40 * n) << 5},cam_y=200000" for n in range(1, 25)])
    by = {r[TICK]: r for r in rows}
    assert by[3][ADRAW] == 3
    assert by[4][ADRAW] == 4 and by[4][ALT] == by[3][ALT]          # n and 3 = 0: the flagged repeat of the cached figure
    assert by[5][ADRAW] == 4 and by[6][ADRAW] == 4 and by[7][ADRAW] == 4      # n and 3 of 1 draws nothing new, 2 and 3 are outside the window
    assert by[8][ADRAW] == 5 and by[8][ALT] != by[7][ALT]          # a new figure at n and 3 = 0
    assert by[9][ADRAW] == 6 and by[9][ALT] == by[8][ALT]          # the repeat on n and 3 = 1


def test_panel_speed_and_altitude_lag_at_most_the_cadence():
    rows = drive([f"{n} {BASE},speed_w={12 * (10 + n)},y={(1000 + 40 * n) << 5},cam_y=200000" for n in range(1, 41)])
    speeds = {r[TICK]: r[SPEED] for r in rows}
    changes = [n for n in range(6, 41) if speeds[n] != speeds[n - 1]]
    assert changes and all(n & 3 in (2, 3) for n in changes)           # the new figure on 2, its repeat on 3
    for n in range(8, 41):
        assert 10 + n - speeds[n] <= 5 + 1


def test_altitude_is_called_on_every_frame_near_the_ground():
    """V:4AB1E above -0x8000 (camera below 32768 units): the altitude routine runs on every frame, and rule 4 still
    only takes a new figure on frames with bit 0 of n clear."""
    rows = drive([f"{n} {BASE},y={(1000 + 40 * n) << 5},cam_y=1000" for n in range(1, 41)])
    alts = {r[TICK]: r[ALT] for r in rows}
    changes = [n for n in range(6, 41) if alts[n] != alts[n - 1]]
    assert changes and all(n % 2 == 0 for n in changes) and any(n & 3 == 2 for n in changes)


def test_external_views_call_the_digits_on_every_pass_and_only_rule_4_limits_new_figures():
    """The external schedule is 0x36A10, 0x36ADE, 0x3697A without the (n and 3) windows (note 06 'Draw schedule' and 'Display model'):
    new figures are taken on every frame with bit 0 of n clear, so changes fall on both n and 3 = 0 and 2."""
    ext = ",view=1,cam_y=200000,redraw=0"
    rows = drive([f"{n} {BASE},speed_w={12 * (10 + n)},y={(1000 + 40 * n) << 5}{ext}" for n in range(1, 41)])
    for col in (SPEED, ALT):
        vals = {r[TICK]: r[col] for r in rows}
        ch = [n for n in range(6, 41) if vals[n] != vals[n - 1]]
        assert ch and all(n % 2 == 0 for n in ch)                                # rule 4: a new figure only when bit 0 of n is clear
        assert {n & 3 for n in ch} == {0, 2}                                     # in the cockpit the speed would only change at n and 3 = 2


def test_redraw_burst_after_a_writer_of_the_redraw_counter():
    """R = 3 on the first pass and after a writer of the helper 0x1EFF4 (note 07 12.1): for three frames the figures are drawn every frame (rule 1); the harness plays the core's rule."""
    rows = drive([f"{n} {BASE},speed_w={12 * (10 + n)}" for n in range(1, 10)])
    assert [r[SPEED] for r in rows[:3]] == [11, 12, 13]                    # R > 0: a new value on every frame (frames 1, 2, 3)
    rows = drive([f"{n} {BASE},speed_w={12 * (10 + n)}" + (",rreq=1" if n == 6 else "") for n in range(1, 11)])
    assert rows[6][SPEED] == 17 and rows[7][SPEED] == 18                   # a writer at frame 6 reloads R (frames 6, 7, 8)


def test_a_repeated_pass_does_not_advance_the_caches():
    """The frontend presents several frames per core step: the same n changes nothing (and a pause leaves n alone)."""
    rows = drive([f"{n} {BASE},speed_w={12 * (10 + n)}" for n in (1, 2, 3, 4)] + [f"4 {BASE},speed_w=999"] * 3)
    assert rows[4][SDRAW] == rows[3][SDRAW] and rows[6][SPEED] == rows[3][SPEED]


def test_fuel_digits_use_the_four_cache_rules_and_the_bar_follows_the_cache():
    """Window n and 7 of 0 or 1 with the speed rules: (1) R > 0 draws, (2) n and 7 = 0 draws the flagged cached figure, not the current one,
    (4) a new figure only on a frame with bit 0 clear, so n and 7 = 1 shows the flagged repeat of the figure taken at n and 7 = 0.
    The bar (window n and 15 of 0 or 1) takes its level from the cache."""
    fuel_at = lambda n: (24999 - 1000 * n) << 8
    rows = drive([f"{n} {BASE},fuel={fuel_at(n)}" for n in range(1, 35)])
    by = {r[TICK]: r for r in rows}
    assert by[3][FUEL] == 24999 - 3000 and by[3][FDRAW] == 3          # rule 1 while R > 0
    assert by[7][FDRAW] == 3                                          # outside the window
    assert by[8][FUEL] == 24999 - 3000 and by[8][FDRAW] == 4          # rule 2: the cached figure of n = 3, not the current one
    assert by[9][FUEL] == 24999 - 3000 and by[9][FDRAW] == 4          # rule 4: bit 0 of n set, no new figure
    assert by[16][FUEL] == 24999 - 16000 and by[16][FDRAW] == 5       # a new figure at n and 15 = 0 (flag was clear)
    assert by[17][FUEL] == 24999 - 16000 and by[17][FDRAW] == 6       # n and 7 = 1: the flagged repeat of the 16 figure, not the 17 one
    assert by[25][FUEL] == 24999 - 24000 and by[25][FDRAW] == 8 and by[24][FDRAW] == 7   # taken at 24, repeated at 25
    assert by[24][FUEL] == 24999 - 24000
    assert by[8][BAR] == by[3][BAR] == (24999 - 3000) >> 10                  # outside the bar window (n and 15 = 8) the bar keeps its level
    assert by[16][BAR] == (24999 - 16000) >> 10


def test_throttle_and_fuel_windows():
    lines = [f"{n} {BASE.replace('throttle=9', f'throttle={9 + n}')},fuel={(24999 - 40 * n) << 8}" for n in range(1, 50)]
    rows = drive(lines)
    thr = {r[TICK]: r[THR] for r in rows}
    ch = [n for n in range(6, 50) if thr[n] != thr[n - 1]]
    assert ch and all((n & 3) < 2 for n in ch)                         # throttle: window n & 3 of 0 or 1
    fuel = {r[TICK]: r[FUEL] for r in rows}
    fch = [n for n in range(6, 50) if fuel[n] != fuel[n - 1]]
    assert fch and all((n & 7) < 2 for n in fch)                       # fuel digits: n & 7 of 0 or 1
    bar = {r[TICK]: r[BAR] for r in rows}
    assert all(bar[n] == bar[n - 1] or (n & 15) < 2 for n in range(6, 50))


def test_eject_lamp_blinks_four_times():
    lines = [f"{n} {BASE},eject_at=5" for n in range(1, 20)]
    rows = drive(lines)
    lit = [r[TICK] for r in rows if r[EJ]]
    # note 07 6.5 / 06 (V: followup-combat-eject-frames.txt): the key sets the timer to 8 before its own pass's draw, which counts it to 7: four lit pictures, those of the key's pass and every second one after
    # (offsets 0, 2, 4, 6; the timer 7, 5, 3, 1), unlit at 1, 3, 5
    assert lit == [5, 7, 9, 11]


def test_eject_countdown_decrements_per_cockpit_draw_only():
    """V:4A8FA is decremented by the lamp routine, which runs only in the cockpit draw: frames in an external view do not count."""
    lines = [f"{n} {BASE},eject_at=5" + (",view=1" if 5 <= n <= 8 else "") for n in range(1, 20)]
    rows = drive(lines)
    assert [r[TICK] for r in rows if r[EJ]] == [9, 11, 13, 15]         # the countdown starts with the first cockpit draw after the key (the key pass itself is an external-view draw and does not count)


# ---- the warning line: drawn from the entry the core publishes (the arbiter is tested in tests/test_cockpit.c) ---------------

def entry_text(i):
    return WARN[28 * i + 1:28 * i + 27].decode("ascii")


def shown_text(L):
    xs = [108 + 4 * i for i in range(26)]
    for pen in (1, 4):
        t = L.text(xs, 192, pen, True)
        if t.strip():
            return t, pen
    return "", None


@pytest.mark.parametrize("entry", [16, 17, 2, 9, 13, 5, 42, 53])
def test_message_line_draws_the_published_entry_in_its_class_pen(tmp_path, entry):
    """Class 2 (bits 7 and 6 of the entry's flag byte) is pen 1, class 1 pen 4; 26 characters from x 108 at a pitch of 4."""
    L, _, _ = run(BASE + f",msg_entry={entry}", tmp=tmp_path)
    t, pen = shown_text(L)
    assert t.strip() == entry_text(entry).strip()
    assert pen == (1 if WARN[28 * entry] >> 6 == 2 else 4)


def test_blank_entry_draws_no_text(tmp_path):
    L, _, _ = run(BASE + ",msg_entry=0", tmp=tmp_path)
    assert shown_text(L)[0].strip() == ""


def test_the_shown_entry_does_not_lag_the_core():
    """The text follows the published entry in the same frame (note 06 'Text versus code word': the lag seen against the logged state is the
    page flip, not a rule)."""
    rows = drive([f"{n} {BASE},msg_entry={e}" for n, e in enumerate([0, 0, 16, 16, 17, 17, 0], 1)])
    assert [r[MSG] for r in rows] == [0, 0, 16, 16, 17, 17, 0]


def test_the_hud_switch_comes_from_the_core(tmp_path):
    """H off (cp_hud_on = 0): only the outline, crosshair, level marks, nose datum, weapon readout and target symbols remain."""
    on, _, _ = run(BASE.replace("sel=13", "sel=29"), tmp=tmp_path)
    off, _, _ = run(BASE.replace("sel=13", "sel=29") + ",hud_on=0", tmp=tmp_path)
    assert on.pixels(13, 120, 200, 53, 65) and not off.pixels(13, 120, 200, 53, 65)               # tape gone
    assert not off.pixels(13, 90, 110, 84, 95) and not off.pixels(13, 200, 230, 84, 95)            # speed and altitude gone
    assert off.pixels(10, 93, 224, 37, 37) and off.pixels(8, 158, 161, 132, 132)                    # outline and nose datum stay
    assert off.text([92, 96, 100], 129, 13, True) == "GUN"                                            # weapon readout stays


def test_external_strip_blank_for_one_pass_after_an_entity_switch(tmp_path):
    L, _, _ = run(BASE + ",speed_w=4284,y=648192,view=1,strip_off=1", view_args=("--flight",), tmp=tmp_path)
    assert all(p == NONE for p in L.d)


# ---- reference frames (extracted/captures, git-ignored) ------------------------------------------------------------

LOG = CAPTURES / "logs" / "hud.txt"
EMU = {0: 0, 1: 0, 2: 0, 3: 28, 4: 43, 5: 56, 6: 72, 7: 89, 8: 106}


def logged_state(label):
    rec = s71 = None
    for line in LOG.read_text().splitlines():
        p = line.split()
        if p and p[0] == f"rec0_{label}":
            rec = bytes.fromhex("".join(p[2:]))
        if p and p[0] == f"s71_{label}":
            s71 = bytes.fromhex("".join(p[2:]))
    u16 = lambda b, o: struct.unpack_from(">H", b, o)[0]
    s16 = lambda b, o: struct.unpack_from(">h", b, o)[0]
    s32 = lambda b, o: struct.unpack_from(">i", b, o)[0]
    f = dict(**{"class": rec[0x62]}, sel=rec[0x63], stock=rec[0x5F], b03=rec[3], b20=rec[0x20], b7c=rec[0x7C],
             throttle=rec[0x2B], w00=u16(rec, 0), rounds=u16(rec, 0x60), pitch_rate=s16(rec, 0x56),
             gcorr=s16(s71, 0x2FE), heading80=s16(rec, 0x68), speed_w=s16(rec, 0x6E), y=s32(rec, 0x18),
             lat=s32(rec, 0x1C), lon=s32(rec, 0x14), fuel=s32(rec, 0x72), gsign=(u16(s71, 0x282) >> 1) & 1,
             zoom=s71[0x3FB], threat=s71[0x226])
    return ",".join(f"{k}={v}" for k, v in f.items()), struct.unpack_from(">H", s71, 0x292)[0]


def ref_pixels(path):
    data = Path(path).read_bytes()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, parts[3]


@pytest.mark.skipif(not (LOG.exists() and (CAPTURES / "cockpit-takeoff.ppm").exists()), reason="reference captures not present")
def test_runway_idle_frame_matches_the_capture(tmp_path):
    """The whole panel and HUD of the note's 'runway idle' frame: every overlay pixel equals the capture's, except the one
    radar blip of the distant carrier (the scene's radar list is empty here)."""
    spec, n = logged_state("runway-idle")
    L, _, _ = run(spec + ",cam_pitch=0,cam_roll=0", frames=n, tmp=tmp_path)
    w, px = ref_pixels(CAPTURES / "cockpit-takeoff.ppm")
    pal = {}
    for line in LOG.read_text().splitlines():
        p = line.split()
        if p and p[0] == "pal_runway-idle":
            pal = [tuple(int(c[i], 16) for i in range(3)) for c in p[1:17]]
    bad = []
    for y in range(200):
        for x in range(320):
            pen = L.pen(x, y)
            if pen == NONE:
                continue
            mine = tuple(EMU.get(v, 16 * v) if v < 9 else None for v in pal[pen])
            if None in mine:
                continue
            o = 3 * ((42 + y) * w + 186 + 2 * x)
            if tuple(px[o:o + 3]) != mine:
                bad.append((x, y))
    assert len(bad) <= 1, bad[:10]                                     # the carrier's radar blip at (150, 174)


def test_paused_passes_draw_the_old_cached_figure_and_keep_the_cache():
    """Rule 4 pause variant: with the pause byte nonzero a figure that would be taken (bit 0 of n clear) is not taken; the old cached value is drawn
    and the cache keeps it. The speed window is n and 3 of 2 or 3, so n = 10 is a taking frame."""
    lines = [f"{n} {BASE},speed_w=1200" for n in range(1, 8)]
    # passes 8 and 9 paused with n standing still at 10: a new figure (200 knots) is offered, the old cached 100 stays
    lines += ["8 %s,speed_w=2400,n=10,paused=1" % BASE, "9 %s,speed_w=2400,n=10,paused=1" % BASE]
    # the pause ends: the same figure is taken on the next unpaused pass with n = 10
    lines += ["10 %s,speed_w=2400,n=10" % BASE]
    rows = drive(lines)
    by = {r[TICK]: r for r in rows}
    assert by[7][SPEED] == 100
    assert by[8][SPEED] == 100 and by[9][SPEED] == 100        # the old figure is drawn, not the offered 200
    assert by[9][SDRAW] > by[7][SDRAW]                        # the paused passes did run the draw
    assert by[10][SPEED] == 200                               # unpaused: taken (the cache had kept 100)


def test_the_dedupe_is_on_the_pass_not_on_n():
    """A paused pass is a pass (n stands still, the draws run again): the per-pass state advances; the same pass presented twice does not."""
    base = f"{BASE},speed_w=1200,n=10"
    rows = drive([f"1 {base},paused=1,pass=1", f"2 {base},paused=1,pass=1", f"3 {base},paused=1,pass=2", f"4 {base},paused=1,pass=3"])
    d = [r[SDRAW] for r in rows]
    assert d[1] == d[0]                    # the same pass again: nothing advances
    assert d[2] > d[1] and d[3] > d[2]     # a further pass with the same n (paused): R counts down and the draw runs
