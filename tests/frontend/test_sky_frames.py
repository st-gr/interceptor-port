# tests/frontend/test_sky_frames.py - the sky fill and the sea mesh stage for logged camera states, through build/sky_driver (the core's pure functions and the
# frontend's filler), against the worked examples of docs/re/04-scene-composition.md and, when the reference captures are present, against the reference
# emulator's frames pixel for pixel (the 73 sky states, the 8 tiny-polygon states and the 137 sea mesh states of extracted/captures).
import re
import struct
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "sky_driver"
CAP = ROOT / "extracted" / "captures"
ASSETS = ROOT / "assets"
pytestmark = pytest.mark.skipif(not BIN.exists(), reason="build/sky_driver not built")
W = 320


def draw(tmp_path, matrix, camy, words, side=0, cockpit=1, sea=0, limit=144, tiny=(13, 1), mesh=None):
    out = tmp_path / "px.bin"
    txt = (f"matrix {' '.join(map(str, matrix))}\ncamy {camy}\nwords {words[0]} {words[1]}\n"
           f"flags {side} {cockpit} {sea} {limit} {tiny[0]} {tiny[1]}\ntables {ASSETS / 'tables'}\nout {out}\n")
    if mesh:
        txt += "".join(f"{k} {' '.join(map(str, v)) if isinstance(v, (list, tuple)) else v}\n" for k, v in mesh.items())
    r = subprocess.run([str(BIN)], input=txt.encode(), capture_output=True, timeout=30, check=True)
    return out.read_bytes(), r.stdout.decode().strip()


def sky_span(px, x):
    ys = [y for y in range(1, 145) if px[y * W + x] == 5]
    return (ys[0], ys[-1]) if ys else None


# ---- the worked examples of note 04 (rig, confirmed on the frame): matrix, roll word, sky rows at columns 0, 80, 160, 240, 319 -------------------------------------
EXAMPLES = [
    ("level",    [107, 0, 128, 0, 252, 0, -98, 0, 82], 0, [(1, 89)] * 5),
    ("roll 1600", [101, -58, 120, 55, 236, 65, -98, 0, 82], 1600, [(1, 41), (1, 65), (1, 90), (1, 114), (1, 138)]),
    ("inverted", [-109, 0, -130, 0, -252, 0, -98, 0, 82], 14400, None),
    ("roll 3600", [76, -119, 90, 114, 178, 135, -98, 0, 82], 3600, [None, (1, 24), (1, 91), (1, 144), (1, 144)]),
]


@pytest.mark.parametrize("name,m,roll,rows", EXAMPLES)
def test_worked_examples_sky_rows(tmp_path, name, m, roll, rows):
    side = 1 if roll == 14400 else 0
    px, msg = draw(tmp_path, m, -566501, (0, roll), side=side)
    if rows is None:     # inverted: ground above, sky from row 90 to the window's last row in every column
        for x in (0, 80, 160, 240, 319):
            assert sky_span(px, x) == (90, 144)
        return
    for x, want in zip((0, 80, 160, 240, 319), rows):
        got = sky_span(px, x)
        if want is not None and want[1] > 144:
            want = (want[0], 144)
        assert got == want, (name, x, got, want)


@pytest.mark.parametrize("height", [30472, 150000, 600000, 2000000, 8388608])
def test_pitch_0_last_sky_row_89_first_ground_row_90_at_every_height(tmp_path, height):
    # the horizon does not depend on the height (note 04: no altitude band): the matrix of level flight, four heights from the deck to the ceiling
    px, _ = draw(tmp_path, [107, 0, 128, 0, 252, 0, -98, 0, 82], -height, (0, 0))
    for x in range(0, 320, 7):
        assert sky_span(px, x) == (1, 89) and px[90 * W + x] == 4 and px[145 * W + x] == 0


def test_the_sea_zone_base_is_pen_6_and_the_mesh_is_off_there(tmp_path):
    px, msg = draw(tmp_path, [107, 0, 128, 0, 252, 0, -98, 0, 82], -566501, (0, 0), sea=1)
    assert px[100 * W + 100] == 6 and px[50 * W + 100] == 5 and "0 mesh polygons" in msg


# ---- the reference captures ------------------------------------------------------------------------------------------------------------------------------------
LOGS = CAP / "logs"
needs_caps = pytest.mark.skipif(not (CAP / "sky").is_dir() or not (LOGS / "sky-ground-attitude.txt").exists() or not (ASSETS / "tables" / "mesh_cells.bin").exists(),
                                reason="reference captures or assets absent")


def parse_log(path):
    states = {}
    for line in path.read_text().splitlines():
        if line.startswith("#") or line.startswith("seg") or not line.strip():
            continue
        t = line.split()
        k, _, name = t[0].partition("_")
        if k in ("s71", "rec0"):
            states.setdefault(name, {})[k] = bytes.fromhex("".join(t[2:]))
    return states


def W16(b, addr, base=0x4A700, signed=True):
    return struct.unpack_from(">h" if signed else ">H", b, addr - base)[0]


def L32(b, addr, base=0x4A700):
    return struct.unpack_from(">i", b, addr - base)[0]


def state_spec(s71, rec0):
    cockpit = s71[0x4A83D - 0x4A700] == 0
    pos = [L32(rec0, 0x4B23C + o, 0x4B23C) for o in (0x14, 0x18, 0x1C)] if cockpit else [L32(s71, a) for a in (0x4ACF6, 0x4ACFA, 0x4ACFE)]
    return dict(
        matrix=[W16(s71, 0x4AC90 + 2 * i) for i in range(9)], camy=L32(s71, 0x4AB1E), cockpit=int(cockpit),
        words=(W16(s71, 0x4AB42 if cockpit else 0x4AB4C, signed=False), W16(s71, 0x4AB4A, signed=False)),
        side=(W16(s71, 0x4A982) >> 1) & 1, sea=int(s71[0x4A953 - 0x4A700] != 0), limit=W16(s71, 0x4AA3C),
        mesh=dict(pos=pos, alt=-L32(s71, 0x4AB30), zoom=W16(s71, 0x4AAFA), default=int(s71[0x4A895 - 0x4A700] != 0), map=int(s71[0x4A865 - 0x4A700] != 0),
                  ownalt=int(s71[0x4A954 - 0x4A700] != 0), phase=s71[0x4A908 - 0x4A700], octant=s71[0x4A90C - 0x4A700]))


def torn(m, zoom=128):
    """A camera matrix caught between the build and the row scale (note 04, Verified by): a row norm far from 168, 252 and the zoom word (128 at the start)."""
    import math
    want = (168, 252, zoom)
    return any(abs(math.hypot(*m[3 * r:3 * r + 3]) - want[r]) > 3 for r in range(3))


def ppm(path):
    d = path.read_bytes()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", d)
    return int(m[1]), int(m[2]), d[m.end():]


# the frame colours of pens 4 to 7 and 13 as the reference emulator shows them (note 04: land (0,56,0), sky (43,43,89), sea (0,28,72), pen 7 (43,43,43))
CLS = {(0, 56, 0): 4, (43, 43, 89): 5, (0, 28, 72): 6, (43, 43, 43): 7}


def compare(tmp_path, spec, frame):
    px, msg = draw(tmp_path, spec["matrix"], spec["camy"], spec["words"], side=spec["side"], cockpit=spec["cockpit"], sea=spec["sea"], limit=spec["limit"],
                   tiny=(13, 1) if spec["cockpit"] else (0, 15), mesh=spec["mesh"])
    w, h, body = ppm(frame)
    same, diff, skipped, bad = 0, 0, 0, []
    for y in range(1, spec["limit"] + 1):
        for x in range(320):
            o = ((42 + y) * w + 186 + 2 * x) * 3
            cls = CLS.get(tuple(body[o:o + 3]))
            if cls is None:
                skipped += 1                 # a pixel of another stage (HUD, panel, objects)
                continue
            mine = px[y * W + x]
            if cls == 7 and mine != 7:
                skipped += 1                 # the HUD uses pen 7 too: the pen 7 check is one-sided (note 04)
                continue
            if mine == cls:
                same += 1
            else:
                diff += 1
                bad.append((x, y, cls, mine))
    return same, diff, skipped, bad


def run_log(tmp_path, log):
    totals = [0, 0, 0]
    problems = {}
    states = parse_log(LOGS / log)
    for name, st in sorted(states.items()):
        frame = CAP / "sky" / f"{name}.ppm"
        if not frame.exists() or "s71" not in st or "rec0" not in st:
            continue
        spec = state_spec(st["s71"], st["rec0"])
        if spec["limit"] not in (144, 167, 179) or torn(spec["matrix"], spec["mesh"]["zoom"]):
            continue
        same, diff, skipped, bad = compare(tmp_path, spec, frame)
        totals[0] += same; totals[1] += diff; totals[2] += 1
        if diff:
            problems[name] = (diff, bad[:3])
    return totals, problems


@needs_caps
def test_73_staged_sky_states_match_the_frames(tmp_path):
    """Attitudes, heights and sea-zone states (sky-ground-attitude, -height, -sea): sky, land, sea and mesh pen 7 pixel for pixel. The one difference of the
    note is the pixel (14, 135) of the pitch 135 degree attitude (a stroke of the HUD overlay, note 04); the torn-matrix state is skipped."""
    t1, p1 = run_log(tmp_path, "sky-ground-attitude.txt")
    t2, p2 = run_log(tmp_path, "sky-ground-height.txt")
    t3, p3 = run_log(tmp_path, "sky-ground-sea.txt")
    problems = {**p1, **p2, **p3}
    states = t1[2] + t2[2] + t3[2]
    pixels = t1[0] + t2[0] + t3[0]
    print(f"sky states compared: {states}, pixels equal: {pixels}, different: {sum(v[0] for v in problems.values())}: {problems}")
    assert states >= 70 and pixels > 3_000_000
    assert set(problems) <= {"a-p+10800-r+0"} and sum(v[0] for v in problems.values()) <= 1


@needs_caps
def test_eight_staged_tiny_polygon_states_match_rows_1_to_4(tmp_path):
    """The horizon within two rows of the window's top (sky-ground-tiny): the pen 13 line across row 1, the 69-pixel run, the general fill, none: rows 1 to 4."""
    states = parse_log(LOGS / "sky-ground-tiny.txt")
    assert len(states) == 8
    for name, st in states.items():
        spec = state_spec(st["s71"], st["rec0"])
        px, _ = draw(tmp_path, spec["matrix"], spec["camy"], spec["words"], side=spec["side"], cockpit=1, sea=spec["sea"], limit=spec["limit"], mesh=spec["mesh"])
        w, h, body = ppm(CAP / "sky" / f"{name}.ppm")
        for y in range(1, 5):
            for x in range(320):
                o = ((42 + y) * w + 186 + 2 * x) * 3
                cls = {(89, 89, 89): 13, **CLS}.get(tuple(body[o:o + 3]))
                assert cls == px[y * W + x], (name, x, y, cls, px[y * W + x])


@needs_caps
def test_137_staged_sea_mesh_states_match_the_frames(tmp_path):
    """The sea mesh pass (sky-ground-mesh-a to -e, -r): the core's polygons through the clipper and the filler against the frames. mb17 (5.6 ft above the ground) is
    inconsistent between its block and its frame at any settle time (note 04) and is excluded; mb17r replaces it."""
    states = same = diff = 0
    problems = {}
    for log in ("a", "b", "c", "d", "e", "r", "r2"):
        t, p = run_log(tmp_path, f"sky-ground-mesh-{log}.txt")
        states += t[2]; same += t[0]; diff += t[1]
        problems.update(p)
    print(f"mesh states compared: {states}, pixels equal: {same}, different: {diff}: {problems}")
    assert states >= 130 and same > 5_000_000
    assert set(problems) <= {"mb17"}


@needs_caps
def test_palette_registers_are_the_first_fade_step_at_every_height():
    """Entries 0 to 15 are fade step 0 (the normal palette) and 16 to 31 black at every height (sky-ground-height: the 14 heights, 'pal' lines)."""
    pal = ASSETS / "palette" / "fade.pal"
    if not pal.exists():
        pytest.skip("assets absent")
    step0 = pal.read_bytes()[:48]
    want = " ".join(f"{step0[3 * i] // 17:X}{step0[3 * i + 1] // 17:X}{step0[3 * i + 2] // 17:X}" for i in range(16))
    seen = set()
    for log in ("sky-ground-height.txt", "sky-ground-attitude.txt"):
        for line in (LOGS / log).read_text().splitlines():
            if line.startswith("pal_"):
                regs = line.split()[1:]
                assert " ".join(regs[:16]) == want and set(regs[16:]) == {"000"}
                seen.add(line.split()[0])
    assert len(seen) >= 14


def test_fade_table_step_13_is_black_and_step_0_converts_with_the_measured_curve(tmp_path):
    pal = ASSETS / "palette" / "fade.pal"
    if not pal.exists():
        pytest.skip("assets absent")
    drv = ROOT / "build" / "raster_driver"
    if not drv.exists():
        pytest.skip("raster_driver not built")
    def colours(step):
        r = subprocess.run([str(drv)], input=f"fadepal {step} {pal}\n".encode(), capture_output=True, check=True, timeout=30)
        return [tuple(int(v) for v in line.split()) for line in r.stdout.decode().splitlines()]
    assert set(colours(13)) == {(0, 0, 0)}                       # level 2: every entry 000, the screen goes black
    c0 = colours(0)
    assert c0[4] == (0, 56, 0) and c0[5] == (43, 43, 89) and c0[6] == (0, 28, 72) and c0[7] == (43, 43, 43)    # land, sky, sea, pen 7 of note 04
