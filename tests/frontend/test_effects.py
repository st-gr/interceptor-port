# tests/frontend/test_effects.py - the combat effects (src/frontend/sdl/effects.c, docs/re/07-ai-and-weapons.md sections 7.7, 11, 13), the jolt (13.3) and the radar scope's erase lists (12.3, note 06).
# The effects run through build/effects_driver with invented scenes and invented burst tables (no game data); the expected pixels are computed here from the note's formulas (camera at the origin
# looking along +z: the matrix rows are the zoom triple 168, 252, 128 over the identity, x' = (168 x) >> 8, y' = (252 y) >> 8, z' = (128 z) >> 8). The jolt and the scope tests drive the built frontend
# with --hud-test and need the extracted cockpit assets (they skip without them).
import os
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "build" / "effects_driver"
BIN = ROOT / "build" / "interceptor"
ASSETS = ROOT / "assets"

driver = pytest.mark.skipif(not DRIVER.exists(), reason="build/effects_driver not built")
frontend = pytest.mark.skipif(not BIN.exists() or not (ASSETS / "cockpit" / "masks.bin").exists(), reason="build/interceptor or the cockpit assets absent")

FX_SHELL, FX_MISSILE, FX_EXPLOSION, FX_SMOKE, FX_FLARE, FX_CHAFF, FX_POD, FX_BURST = range(8)


def tdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def w16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def view(dx, dy, dz):
    """Position difference in words to camera space (identity attitude, zoom row scale)."""
    return (168 * dx) >> 8, (252 * dy) >> 8, (128 * dz) >> 8


def project(X, Y, Z, limit=143):
    """note 03: x' = 0x13F - (160 + 160 X / Z), y' = 180 - (90 + 90 Y / Z); None when rejected."""
    if Z <= 0 or abs(X) >= Z or abs(Y) >= Z:
        return None
    px = min(max(tdiv(X * 160, Z) + 160, 0), 319)
    py = min(max(tdiv(Y * 90, Z) + 90, 0), 179)
    sx, sy = 319 - px, 180 - py
    return (sx, sy) if sy <= limit else None


def run(script, *args):
    r = subprocess.run([str(DRIVER), *args], input=script, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return r.stdout


def pixels(out):
    return {(int(a), int(b)): int(c) for _, a, b, c in (l.split() for l in out.splitlines() if l.startswith("px "))}


def draws(out):
    return [int(l.split()[1]) for l in out.splitlines() if l.startswith("draw ")]


CAM = "cam 0 0 0 0 0 0\n"


def fx(kind, x, y, z, age=0, rec=0, owner=0, model=0, phase=0):
    return f"fx {kind} {x} {y} {z} {age} {rec} {owner} {model} {phase}\n"


# ---- the tracer pool (7.7) -----------------------------------------------------------------------------------------------------------

@driver
def test_a_tracer_is_one_pixel_in_pen_9_at_the_projected_point():
    out = run(CAM + fx(FX_SHELL, 25600, 12800, 102400, model=0x0001, phase=20) + "pool\ndump\n")
    assert pixels(out) == {project(*view(100, 50, 400)): 9} == {(107, 68): 9}


@driver
@pytest.mark.parametrize("pos", [(0, 0, -102400), (51200, 0, 25600), (0, 80000, 102400)])
def test_a_tracer_behind_beside_or_above_the_view_is_not_drawn(pos):
    out = run(CAM + fx(FX_SHELL, *pos, model=1, phase=20) + "pool\ndump\n")
    assert pixels(out) == {}


@driver
def test_a_tracer_below_the_windows_last_row_is_not_drawn():
    base = CAM + fx(FX_SHELL, 0, -25600, 102400, model=1, phase=20)
    on = project(*view(0, -100, 400))
    assert on is not None and on[1] > 100
    assert pixels(run(f"limit {on[1]}\n" + base + "pool\ndump\n")) == {on: 9}
    assert pixels(run(f"limit {on[1] - 1}\n" + base + "pool\ndump\n")) == {}      # y' not below V:4AA3C


@driver
def test_the_cell_difference_wraps_at_two_to_the_29():
    out = run("cam 536870000 0 0 0 0 0\n" + fx(FX_SHELL, 600, 0, 102400, model=1, phase=20) + "pool\ndump\n")   # 2^29 - 912 .. 600: the difference is 1512 units, 5 words
    assert len(pixels(out)) == 1


# INVENTED star offsets (the real table is tables/star.bin; the driver takes these through its "star" command)
STAR = [((-4, 2, -3), (5, -3, 4)), ((1, 3, -2), (-2, -2, 3)), ((-3, -1, -4), (2, 4, 3))]
STAR_CMD = "star " + " ".join(str(v) for seg in STAR for pt in seg for v in pt) + "\n"


def star_segments(dx, dy, dz):
    segs = STAR
    out = []
    for a, b in segs:
        ends = []
        for o in (a, b):
            X, Y, Z = view(dx + o[0], dy + o[1], dz + o[2])
            if Z <= 0 or abs(X) > Z or abs(Y) > Z:
                break
            sx = min(max(tdiv(X * 160, Z) + 160, 0), 319)
            sy = min(max(tdiv(Y * 90, Z) + 90, 0), 179)
            ends.append((319 - sx, 179 - sy))
        else:
            out.append(ends)
    return out


@driver
def test_a_flare_is_a_three_stroke_star_in_pen_9():
    out = run(STAR_CMD + CAM + fx(FX_FLARE, 25600, 12800, 102400, model=0x1001, phase=30) + "pool\ndump\n")
    px = pixels(out)
    assert set(px.values()) == {9}
    segs = star_segments(100, 50, 400)
    assert len(segs) == 3
    xs = [p[0] for s in segs for p in s]
    ys = [p[1] for s in segs for p in s]
    assert all(min(xs) <= x <= max(xs) and min(ys) <= y <= max(ys) + 1 for x, y in px)
    for s in segs:                                                           # every stroke starts at its upper end on the row below it and ends on its lower end
        top, bot = sorted(s, key=lambda p: p[1])
        assert (top[0], top[1] + 1) in px or top[1] == bot[1]
        assert bot in px or (bot[0], bot[1] + 1) in px


@driver
def test_the_star_is_drawn_only_when_both_ends_are_inside_the_pyramid():
    near_edge = 25600 * 4 + 40000      # x' beyond z at the window's edge: the star's ends fall outside
    out = run(STAR_CMD + CAM + fx(FX_FLARE, near_edge, 0, 102400, model=0x1001, phase=30) + "pool\ndump\n")
    assert pixels(out) == {}


@driver
def test_a_tracer_that_hit_something_turns_into_the_same_star():
    a = run(STAR_CMD + CAM + fx(FX_FLARE, 25600, 12800, 102400, model=0x1001, phase=30) + "pool\ndump\n")
    b = run(STAR_CMD + CAM + fx(FX_SHELL, 25600, 12800, 102400, model=0x0003, phase=2) + "pool\ndump\n")        # flags 0x0002: stopped
    assert pixels(a) == pixels(b) and pixels(a)


# ---- the place of the pool in the frame (7.3, note 04 stage 9) -------------------------------------------------------------------------

TERRAIN, ENTITY, SECONDARY, SPECK = 0, 1, 2, 3
TIER_A, TIER_B, TIER_C, TIER_DIRECT = range(4)
SCENE = f"entry {TERRAIN} {TIER_A}\nentry {TERRAIN} {TIER_B}\nentry {SPECK} {TIER_DIRECT}\nentry {ENTITY} {TIER_DIRECT}\nentry {TERRAIN} {TIER_C}\nentry {ENTITY} {TIER_C}\n"


@driver
def test_the_pool_is_drawn_after_the_last_entry_when_the_gun_follows_the_entity_pass():
    assert [int(l.split()[1]) for l in run(SCENE + "gunfirst 0\npoolpos\n").splitlines()] == [6]


@driver
def test_the_pool_is_drawn_before_the_entity_pass_but_after_the_near_ground_specks_when_the_gun_comes_first():
    assert [int(l.split()[1]) for l in run(SCENE + "gunfirst 1\npoolpos\n").splitlines()] == [3]        # the entry after the speck: the direct entity
    only_c = f"entry {TERRAIN} {TIER_A}\nentry {TERRAIN} {TIER_C}\n"
    assert [int(l.split()[1]) for l in run(only_c + "gunfirst 1\npoolpos\n").splitlines()] == [1]       # queue C is walked by the entity pass
    assert [int(l.split()[1]) for l in run(f"entry {TERRAIN} {TIER_A}\ngunfirst 1\npoolpos\n").splitlines()] == [1]   # no entity pass entry: after the last


# ---- the burst routine (11.4) ------------------------------------------------------------------------------------------------------

DEFAULT_PENS = [[9] * 12, [2] * 12, [13, 10] * 6, [9] * 12, [9] * 12]
DEFAULT_CYCLE = [4 * (i % 8) for i in range(32)]


def tables(pens=None, cycle=None):
    """Invented tables: eight big triangles, the three offset tables, nine disc offsets, the five pen tables and the shape cycle (the shipped image's values unless given)."""
    shape = []
    for i in range(8):
        base = 200 + 30 * i
        shape += [-base, -base // 2, 0, base, -base // 2, 0, 0, base, 0]
    off = []
    for g, mul in enumerate((1, 2, 3)):
        for j in range(12):
            off += [mul * ((j * 7) % 15 - 7), mul * ((j * 5) % 11 - 5), 0]
    disc = []
    for j in range(9):
        disc += [(j * 9) % 17 - 8, (j * 4) % 9 - 4, 0]
    pen = [p for t in (pens or DEFAULT_PENS) for p in t]
    return "tab\n" + " ".join(map(str, shape + off + disc + pen + (cycle or DEFAULT_CYCLE))) + "\n"


T = {"shape": None}


def parse_tables():
    toks = tables().split("\n")[1].split()
    v = list(map(int, toks))
    shape = [[v[i * 9 + k * 3: i * 9 + k * 3 + 3] for k in range(3)] for i in range(8)]
    o = v[72:72 + 108]
    off = [[o[g * 36 + j * 3: g * 36 + j * 3 + 3] for j in range(12)] for g in range(3)]
    d = v[180:207]
    disc = [d[j * 3: j * 3 + 3] for j in range(9)]
    return shape, off, disc


def burst_triangles(centre, s, age, off, shape, add=(0, 0, 0), n=12, cycle=None):
    tris = []
    for j in range(n - 1, -1, -1):
        fc = [w16((centre[a] >> s) + (w16(off[j][a] * age) >> s) + add[a]) for a in range(3)]
        sh = shape[((cycle or DEFAULT_CYCLE)[j + age] >> 2) & 7]
        pts = []
        for k in range(3):
            v = [w16((fc[a] + sh[k][a]) >> s) for a in range(3)]
            X, Y, Z = view(*v)
            if Z <= 0 or abs(X) > Z or abs(Y) > Z:
                pts = None
                break
            x = 0x13F - min(max(160 + tdiv(160 * X, Z), 0), 0x13F)
            y = 0xB3 - min(max(160 // 160 * 90 + tdiv(90 * Y, Z), 0), 0xB4)
            pts.append((x, y))
        if pts:
            tris.append(pts)
    return tris


def centroid(t):
    return (sum(p[0] for p in t) // 3, sum(p[1] for p in t) // 3)


BURST_POS = (0, 0, 2000 * 256)          # 2000 words ahead


@driver
@pytest.mark.parametrize("kind,pen", [(1, 9), (2, 2)])
@pytest.mark.parametrize("age", [0, 7, 15])
def test_the_dying_burst_of_kind_1_and_2_is_twelve_triangles_in_pen_9_or_2(kind, pen, age):
    shape, off, _ = parse_tables()
    out = run(CAM + tables() + "limit 179\n" + fx(FX_BURST, *BURST_POS, age=age, rec=5, owner=0xFF, model=kind, phase=15 - age) + "tail 5 0\ndump\n")
    px = pixels(out)
    tris = burst_triangles((0, 0, 2000), 0, age, off[0], shape)
    assert tris and draws(out) == [len(tris)]
    assert set(px.values()) == {pen}
    for t in tris:
        assert px.get(centroid(t)) == pen, (t, centroid(t))
    xs = [p[0] for t in tris for p in t]
    ys = [p[1] for t in tris for p in t]
    assert all(min(xs) <= x <= max(xs) and min(ys) <= y <= max(ys) for x, y in px)


@driver
def test_the_pens_and_the_shape_cycle_are_the_tables_not_constants():
    """Invented pen tables (a different pen for every fragment) and a reversed cycle: the figure follows the tables the loader gave (extractor version 18)."""
    shape, off, _ = parse_tables()
    pens = [[3 + (j % 4) for j in range(12)], [14] * 12, [13, 10] * 6, [9] * 12, [9] * 12]
    cycle = [4 * (7 - (i % 8)) for i in range(32)]
    age = 6
    out = run(CAM + tables(pens, cycle) + "limit 179\n" + fx(FX_BURST, *BURST_POS, age=age, rec=5, owner=0xFF, model=1, phase=15 - age) + "tail 5 0\ndump\n")
    px = pixels(out)
    tris = burst_triangles((0, 0, 2000), 0, age, off[0], shape, cycle=cycle)
    assert tris and draws(out) == [len(tris)]
    assert set(px.values()) <= {3, 4, 5, 6} and len(set(px.values())) > 1
    plain = run(CAM + tables() + "limit 179\n" + fx(FX_BURST, *BURST_POS, age=age, rec=5, owner=0xFF, model=1, phase=15 - age) + "tail 5 0\ndump\n")
    assert pixels(plain) != px                                  # the reversed cycle draws other shapes
    kind2 = run(CAM + tables(pens, cycle) + "limit 179\n" + fx(FX_BURST, *BURST_POS, age=age, rec=5, owner=0xFF, model=2, phase=15 - age) + "tail 5 0\ndump\n")
    assert set(pixels(kind2).values()) == {14}                  # kind 2 reads the second table


@driver
def test_no_burst_is_drawn_without_the_tables_or_when_too_far_above_the_camera():
    one = fx(FX_BURST, *BURST_POS, age=3, rec=5, owner=0xFF, model=1, phase=12)
    assert pixels(run(CAM + "limit 179\n" + one + "tail 5 0\ndump\n")) == {}                  # no tables: nothing is drawn (effects.h)
    high = fx(FX_BURST, 0, 0x8000 * 256 + 256 * 4, 2000 * 256, age=3, rec=5, owner=0xFF, model=1, phase=12)
    assert pixels(run(CAM + tables() + "limit 179\n" + high + "tail 5 0\ndump\n")) == {}      # |y + V:4AB30| above 0x8000


@driver
def test_a_burst_belongs_to_its_record_only():
    out = run(CAM + tables() + "limit 179\n" + fx(FX_BURST, *BURST_POS, age=3, rec=5, owner=0xFF, model=1, phase=12) + "tail 6 0\ndump\n")
    assert pixels(out) == {} and draws(out) == [0]


@driver
def test_the_lod_shift_halves_the_centre_and_the_shape_twice():
    shape, off, _ = parse_tables()
    out = run(CAM + tables() + "limit 179\n" + fx(FX_BURST, 0, 0, 2000 * 256, age=4, rec=5, owner=0xFF, model=1, phase=11) + "tail 5 1\ndump\n")
    tris = burst_triangles((0, 0, 2000), 1, 4, off[0], shape)
    px = pixels(out)
    assert tris and draws(out) == [len(tris)]
    for t in tris:
        assert px.get(centroid(t)) == 9


@driver
def test_the_cruise_missiles_burst_has_discs_then_two_layers_of_triangles():
    shape, off, disc = parse_tables()
    age = 5
    out = run(CAM + tables() + "limit 179\n" + fx(FX_BURST, *BURST_POS, age=age, rec=5, owner=0xFF, model=3, phase=15 - age) + "tail 5 0\ndump\n")
    px = pixels(out)
    n = draws(out)[0]
    ntri = len(burst_triangles((0, 0, 2000), 0, age - 1, off[2], shape)) + len(burst_triangles((0, 0, 2000), 0, age - 2, off[1], shape, add=(0x14, 0x0A, 0x14)))
    ndisc = 0
    for j in range(9):
        c = [w16(0 + w16(disc[j][a] * age)) for a in range(3)]
        X, Y, Z = view(c[0], c[1], 2000 + c[2])
        ndisc += abs(X) <= Z and abs(Y) <= Z and Z > 0
    assert ndisc == 9 and n == ndisc + ntri                # nine discs, then the two layers of twelve triangles
    assert set(px.values()) <= {9, 10, 13}                 # pens 13 and 10 alternate on the discs, 9 on the triangles (which cover most of them here)


@driver
def test_the_discs_of_the_cruise_missile_burst_vanish_from_age_14():
    out = run(CAM + tables() + "limit 179\n" + fx(FX_BURST, *BURST_POS, age=14, rec=5, owner=0xFF, model=3, phase=1) + "tail 5 0\ndump\n")
    px = pixels(out)
    assert 13 not in px.values() and 10 not in px.values()   # (0x2800 x 14 / 6 + 0x2800) is above 0x7FFF: a negative radius draws nothing


# ---- chaff (7.7) -------------------------------------------------------------------------------------------------------------------

@driver
@pytest.mark.parametrize("life", [60, 0x3B])
def test_chaff_is_the_tracers_pixel_in_pen_13_for_its_first_two_passes(life):
    out = run(CAM + tables() + "limit 179\n" + fx(FX_CHAFF, *BURST_POS, model=0x2001, phase=life) + "pool\ndump\n")
    px = pixels(out)
    assert len(px) == 1 and set(px.values()) == {13}         # 0x3140C stores the pen word 13 and jumps into the tracer's pixel code after its own pen store (note 07 7.7)


@driver
@pytest.mark.parametrize("life,age", [(0x3A, 0), (0x30, 10), (0x20, 12), (1, 12)])
def test_chaff_then_bursts_as_twelve_lines_in_pen_13_with_the_age_capped_at_12(life, age):
    """Note 07 7.7 and 11.4 (kind 0): the table of kind 1, the shapes of the cycle, the pen forced to 13, and each fragment drawn as the line from its first to its second vertex."""
    shape, off, _ = parse_tables()
    out = run(CAM + tables() + "limit 179\n" + fx(FX_CHAFF, *BURST_POS, model=0x2001, phase=life) + "pool\ndump\n")
    px = pixels(out)
    tris = burst_triangles((0, 0, 2000), 0, age, off[0], shape)
    assert tris and set(px.values()) == {13}
    for t in tris:                                           # both end points of the line are lit (the line routine may start one row off)
        for e in (t[0], t[1]):
            assert any((e[0] + dx, e[1] + dy) in px for dx in (-1, 0, 1) for dy in (-1, 0, 1)), e
    xs = [p[0] for t in tris for p in t[:2]]
    ys = [p[1] for t in tris for p in t[:2]]
    assert all(min(xs) - 1 <= x <= max(xs) + 1 and min(ys) - 1 <= y <= max(ys) + 1 for x, y in px)   # lines, not triangles: nothing past the end points
    direct = run(CAM + tables() + "limit 179\n" + fx(FX_BURST, *BURST_POS, age=age, rec=5, owner=0xFF, model=0, phase=15 - age) + "tail 5 0\ndump\n")
    assert draws(direct) == [len(tris)] and pixels(direct) == px   # the same figure through the burst routine of kind 0


@driver
def test_the_chaffs_lines_differ_from_the_filled_triangles_of_kind_1():
    kind1 = run(CAM + tables() + "limit 179\n" + fx(FX_BURST, *BURST_POS, age=6, rec=5, owner=0xFF, model=1, phase=9) + "tail 5 0\ndump\n")
    kind0 = run(CAM + tables() + "limit 179\n" + fx(FX_BURST, *BURST_POS, age=6, rec=5, owner=0xFF, model=0, phase=9) + "tail 5 0\ndump\n")
    assert len(pixels(kind0)) < len(pixels(kind1)) and set(pixels(kind0).values()) == {13}


@driver
def test_a_cruise_missile_disc_of_radius_below_1_draws_nothing():
    """The disc routine 0x33CAC returns at once for a radius below 1 (11.4): the 2 by 1 sprite of a zero radius is op 62's rule. At a distance of about 12,000 words the radius (0x2800 / distance) is 0."""
    far = (0, 0, 12000 * 256)
    out = run(CAM + tables() + "limit 179\n" + fx(FX_BURST, *far, age=0, rec=5, owner=0xFF, model=3, phase=15) + "tail 5 0\ndump\n")
    assert not any(p in (10, 13) for p in pixels(out).values())


# ---- the trail (13.2) ------------------------------------------------------------------------------------------------------------------

def ring_cmd(count, index, length, ooc, pts, vel=(0, 0, 4096), speed=0x200, pos=None):
    """The ring, the followed record's velocity longs, position longs (default: the newest ring entry) and speed word (13.2 step 1)."""
    flat = [c for p in pts for c in p]
    flat += [0] * (18 - len(flat))
    if pos is None:
        pos = pts[(index + 5) % 6] if len(pts) > (index + 5) % 6 else (0, 0, 40000)
    return f"trail {count} {index} {length} {ooc} " + " ".join(map(str, flat + list(vel) + list(pos) + [speed])) + "\nfollowed 2\n"


def trail_centre(p):
    s = 0 if max(abs(c) for c in p) <= 0x4000 else 4 if max(abs(c) for c in p) <= 0x40000 else 8
    X, Y, Z = view(p[0] >> s, p[1] >> s, p[2] >> s)
    return project(X, Y, Z, 179)


AWAY = [(6000 * (k - 2), 0, 40000 + 5000 * k) for k in range(6)]       # a record moving away along +z, drifting across the view


@driver
def test_a_trail_of_length_one_going_away_draws_slot_1_then_slot_0():
    out = run(CAM + "limit 179\n" + ring_cmd(6, 0, 1, 0, AWAY) + "trail_draw\ndump\n")
    px = pixels(out)
    assert draws(out) == [2]
    c1, c0 = trail_centre(AWAY[1]), trail_centre(AWAY[0])
    assert px[c1] == 8 and px[c0] == 10                                 # slot 1 is odd (pen 8), slot 0 even (pen 10); the oldest end is kept


@driver
def test_a_trail_going_toward_the_camera_starts_at_the_write_index_and_steps_up():
    toward = [(6000 * (k - 2), 0, 70000 - 5000 * k) for k in range(6)]
    out = run(CAM + "limit 179\n" + ring_cmd(6, 0, 1, 0, toward, vel=(0, 0, -4096)) + "trail_draw\ndump\n")
    px = pixels(out)
    assert draws(out) == [2]
    c0, c1 = trail_centre(toward[0]), trail_centre(toward[1])
    assert px[c0] == 10 and px[c1] == 8                                 # slot 0 first, slot 1 drawn last (on top)


@driver
def test_the_direction_is_the_sign_of_the_velocity_dot_the_position_difference_in_words():
    """13.2 step 1: each factor is shifted right by 8 when the speed word is 0x100 or more and truncated to a word before the product. The last disc drawn has its radius reduced by 1/8, so the two
    directions (the order of the same entries) give different pictures."""
    def trail(vel, speed, pos):
        return pixels(run(CAM + "limit 179\n" + ring_cmd(6, 0, 2, 0, AWAY, vel=vel, speed=speed, pos=pos) + "trail_draw\ndump\n"))
    toward = trail((0, 0, -1), 0x80, (0, 0, 20000))
    away = trail((0, 0, 1), 0x80, (0, 0, 20000))
    assert toward and away and toward != away
    assert trail((0, 0, 1), 0x80, (0, 0, 0x8000)) == toward               # the difference 0x8000 is the word -32768: the product with +1 is negative
    assert trail((0, 0, -255), 0x200, (0, 0, 20000)) == toward            # shifted by 8 (arithmetic): -255 becomes -1
    assert trail((0, 0, 255), 0x200, (0, 0, 20000)) == away               # 255 becomes 0: the product 0 is not negative
    assert trail((0, 0, 255), 0xFF, (0, 0, 20000)) == away                # below 0x100 nothing is shifted: 255 * 40000 is positive


@driver
def test_a_wreck_trail_is_dark_pens_12_and_3():
    out = run(CAM + "limit 179\n" + ring_cmd(6, 0, 1, 1, AWAY) + "trail_draw\ndump\n")
    px = pixels(out)
    assert px[trail_centre(AWAY[1])] == 3 and px[trail_centre(AWAY[0])] == 12


@driver
def test_a_longer_trail_adds_three_interpolated_discs_per_link():
    n2 = draws(run(CAM + "limit 179\n" + ring_cmd(6, 0, 2, 0, AWAY) + "trail_draw\ndump\n"))[0]
    n1 = draws(run(CAM + "limit 179\n" + ring_cmd(6, 0, 1, 0, AWAY) + "trail_draw\ndump\n"))[0]
    n4 = draws(run(CAM + "limit 179\n" + ring_cmd(6, 0, 4, 0, AWAY) + "trail_draw\ndump\n"))[0]
    assert (n1, n2, n4) == (2, 3 + 6, 5 + 12)                           # n + 1 entries; three more before every entry after the first when the length is above 1


@driver
def test_a_ring_that_is_not_full_visits_one_entry_less():
    ring = [(6000 * (k - 2), 0, 40000 + 5000 * k) for k in range(3)]
    out = run(CAM + "limit 179\n" + ring_cmd(3, 3, 2, 0, ring) + "trail_draw\ndump\n")
    assert draws(out) == [2 + 3]                                         # n = length - 1 = 1: two entries, three interpolated discs


@driver
def test_no_trail_without_a_length_or_for_another_record():
    assert draws(run(CAM + "limit 179\n" + ring_cmd(6, 0, 0, 0, AWAY) + "trail_draw\ndump\n")) == [0]
    out = run(CAM + "limit 179\n" + tables() + ring_cmd(6, 0, 3, 0, AWAY) + "tail 5 0\ndump\n")        # followed record is 2
    assert pixels(out) == {}
    out = run(CAM + "limit 179\n" + tables() + ring_cmd(6, 0, 3, 0, AWAY) + "tail 2 0\ndump\n")
    assert pixels(out)


@driver
def test_the_trail_discs_shrink_with_the_distance_not_with_age():
    out = run(CAM + "limit 179\n" + ring_cmd(6, 0, 1, 0, AWAY) + "trail_draw\ndump\n")
    px = pixels(out)
    near, far = trail_centre(AWAY[0]), trail_centre(AWAY[1])
    count = lambda c, pen: sum(1 for (x, y), p in px.items() if p == pen and abs(x - c[0]) < 25 and abs(y - c[1]) < 25)
    assert count(near, 10) > 0 and count(far, 8) > 0


# ---- the jolt (13.3) ------------------------------------------------------------------------------------------------------------------

@driver
@pytest.mark.parametrize("view_,step,jolt,base,want", [(0, 0, -1, 144, 143), (0, 0, 1, 144, 145), (0, 0, 0, 144, 144), (0, -7, 1, 144, 145),
                                                       (0, 50, 1, 167, 167), (1, 0, 1, 144, 144), (1, 0, -1, 179, 179), (2, 0, 1, 144, 144)])
def test_the_jolt_moves_the_windows_last_row_in_the_cockpit_view_only(view_, step, jolt, base, want):
    out = run(f"cam 0 0 0 0 0 0 {view_} {step}\njolt {jolt}\nwindow {base}\n")
    assert out.split() == ["window", str(want)]


@driver
def test_the_window_row_limit_applies_to_the_effects():
    on = project(*view(0, -100, 400))
    out = run(CAM + f"limit {on[1]}\n" + fx(FX_SHELL, 0, -25600, 102400, model=1, phase=20) + "pool\ndump\n")
    assert pixels(out) == {on: 9}


JOLT_KINDS = {1: (-1, 1, 0, 0), 2: (1, -1, 0, 0), 3: (-1, 1, 0, 0), 4: (1, -1, 0, 0)}   # the core's table (13.3); the display moves the window one scanline for four passes


def hud_run(spec, frames, tmp_path, assets=None):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    out = tmp_path / f"h{abs(hash((spec, frames, str(assets))))}"
    cmd = [str(BIN), "--assets", str(assets or ASSETS), "--scale", "1", "--flight", "--cockpit", "--frames", str(frames), "--dump", f"{out}.ppm", "--dump-layer", f"{out}.layer", "--hud-test", spec]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    assert r.returncode == 0, r.stderr
    return Layer(Path(f"{out}.layer").read_bytes())


class Layer:
    def __init__(self, d):
        assert len(d) == 64000
        self.d = d

    def pen(self, x, y):
        return self.d[y * 320 + x]

    def pixels(self, pen, x0=0, x1=319, y0=0, y1=199):
        return {(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1) if self.d[y * 320 + x] == pen}


BASE = "class=17,sel=13,stock=36,throttle=9,w00=4552,rounds=500,heading80=2240,y=1800,fuel=6399880,zoom=128"
CROSS = {(159, 71), (159, 72), (159, 74), (159, 75)} | {(x, 73) for x in range(157, 162)}


@frontend
@pytest.mark.parametrize("d", [-1, 0, 1])
def test_the_hud_and_panel_rows_follow_the_jolt(tmp_path, d):
    L = hud_run(BASE + f",jolt_row={d}", 1, tmp_path)
    ref = hud_run(BASE, 1, tmp_path)
    shifted = {(x, y + d) for x, y in CROSS}
    assert shifted <= L.pixels(10)                                      # the crosshair is drawn d rows lower
    assert not ((CROSS - shifted) & L.pixels(10))
    for y in range(150, 187):                                           # the panel picture and its instruments follow: V:4A9D0 shifts the row addresses
        assert [L.pen(x, y + d) for x in range(10, 130)] == [ref.pen(x, y) for x in range(10, 130)], y
    assert hud_run(BASE + f",jolt_row={d}", 1, tmp_path).pen(158, 167 + d) == 12   # the own-ship plus too


@frontend
def test_the_full_width_views_cancel_the_jolt(tmp_path):
    a = hud_run(BASE + ",jolt_row=1,head=3", 1, tmp_path)
    b = hud_run(BASE + ",head=3", 1, tmp_path)
    assert a.d == b.d


# ---- the radar scope between walks (12.3, note 06 "Radar scope") ---------------------------------------------------------------------

CORE = BASE + ",walk=1"      # a scene in which the core's scope walk ran (scene.hud.scope_walked); without walk=1 the frontend draws no blip and no erase


@frontend
def test_the_scope_draws_the_cores_blips_at_their_own_coordinates_and_widths(tmp_path):
    L = hud_run(CORE + ",blip=150:160:0:1,blip=170:175:1:2,blip=140:150:0:4", 1, tmp_path)
    assert L.pen(150, 160) == 1 and L.pen(149, 160) != 1
    assert L.pen(170, 175) == 2 and L.pen(169, 175) == 2                # two pixels wide: x - 1 and x
    assert L.pen(140, 150) == 4


@frontend
def test_nothing_of_the_walk_is_drawn_in_a_pass_without_one(tmp_path):
    L = hud_run(BASE + ",blip=150:160:0:1,erase=158:167:0", 1, tmp_path)      # scope_walked is 0: the core says no walk ran (a paused pass, an external view, bit 1 of n)
    assert L.pen(150, 160) != 1 and L.pen(158, 167) == 12


@frontend
def test_the_blips_row_includes_the_jolt_and_is_not_shifted_again(tmp_path):
    L = hud_run(CORE + ",jolt_row=1,blip=150:161:0:1", 1, tmp_path)     # the core's blip row already holds V:4A990
    assert L.pen(150, 161) == 1 and L.pen(150, 162) != 1


@frontend
def test_the_own_ship_plus_is_drawn_while_the_redraw_counter_runs_and_stays(tmp_path):
    for frames in (1, 3, 7):
        L = hud_run(BASE, frames, tmp_path)
        assert L.pixels(12, 150, 170, 165, 170) == {(158, 167), (157, 168), (158, 168), (159, 168)}, frames


@frontend
def test_a_walks_pen_0_erase_removes_the_plus_of_an_r_equal_0_pass_but_not_the_plus_of_an_r_pass(tmp_path):
    """Note 07 12.1: with R = 0 the plus pass draws the plus and then walks (its erase can land on the plus); with R of 1 or more the order is the cone lines, the first walk (the erase of the old
    blips), the plus, the second walk: an old blip on the plus is erased before the plus is drawn."""
    spec = BASE + ",erase=158:167:0"
    assert hud_run(spec, 7, tmp_path).pen(158, 167) == 12               # no walk: the list is not applied
    assert hud_run(spec + ",walk=1", 1, tmp_path).pen(158, 167) == 12   # R pass: the erase came first, the plus after it
    assert hud_run(spec + ",walk=1", 8, tmp_path).pen(158, 167) == 0    # frame 8 (R = 0, (n and 14) = 8): the plus, then the walk's erase on it
    assert hud_run(spec + ",walk=1,redraw=2", 8, tmp_path).pen(158, 167) == 12   # an R pass at any n: the plus after the erase
    assert hud_run(spec + ",walk=1,blip=158:167:0:5", 1, tmp_path).pen(158, 167) == 5   # the new blip over the plus


@frontend
def test_the_erase_list_of_eleven_entries_is_applied_and_entries_off_the_bitmap_are_clipped(tmp_path):
    xs = list(range(150, 160))
    spec = CORE + "," + ",".join(f"erase={x}:150:0" for x in xs)
    L = hud_run(spec, 1, tmp_path)
    assert all(L.pen(x, 150) == 0 for x in xs)                           # ten entries, and the eleventh below
    L = hud_run(spec + ",erase=165:150:0", 1, tmp_path)                  # the eleventh entry: the erase reads one past a page of ten
    assert L.pen(165, 150) == 0
    stale = hud_run(CORE + ",erase=-5:300:1,erase=400:-2:1", 1, tmp_path)   # stale data of the other page: clipped to the bitmap, nothing drawn, no crash
    assert stale.pen(158, 167) == 12


@frontend
def test_the_erase_comes_before_the_blips_of_the_same_walk(tmp_path):
    L = hud_run(CORE + ",erase=150:160:0,blip=150:160:0:1", 1, tmp_path)
    assert L.pen(150, 160) == 1                                         # erased first, then drawn: the blip stays


@frontend
def test_a_wide_erase_removes_two_pixels(tmp_path):
    L = hud_run(CORE + ",erase=158:168:1", 8, tmp_path)
    assert L.pen(158, 168) == 0 and L.pen(157, 168) == 0 and L.pen(159, 168) == 12


@frontend
def test_the_cone_lines_are_drawn_with_the_redraw_counter_and_when_n_and_14_is_0(tmp_path):
    L = hud_run(CORE, 1, tmp_path)
    assert (154, 163) in L.pixels(3) and (177, 148) in L.pixels(3)


# ---- the message line's target data (note 07 12.8) --------------------------------------------------------------------------------------

def glyph_px(ch, x, y):
    import test_hud
    return test_hud.glyph_pixels(ch, x, y)


@frontend
@pytest.mark.parametrize("col,digits,colour,pen", [(18, "12345", 2, 1), (18, "12345", 3, 1), (19, "090", 1, 4), (18, " 450", 0, 8)])
def test_the_target_data_line_draws_its_digits_at_the_page_column(tmp_path, col, digits, colour, pen):
    """Note 07 12.8, the pen of the colour bits: 10 and 11 pen 1, 01 pen 4, 00 pen 8 (the digits here are invented)."""
    L = hud_run(BASE + f",tline=2:{colour}:{col}:{digits}", 1, tmp_path)
    want = set()
    for i, ch in enumerate(digits):
        want |= glyph_px(ch, 108 + 4 * (col + i), 192) if ch != " " else set()
    assert want and want <= L.pixels(pen, 100, 215, 190, 199)


@frontend
def test_the_blank_message_and_the_target_data_replace_the_posted_message(tmp_path):
    posted = hud_run(BASE + ",msg_entry=3", 1, tmp_path)
    assert posted.pixels(4, 100, 215, 190, 199) or posted.pixels(1, 100, 215, 190, 199)
    blank = hud_run(BASE + ",msg_entry=3,tline=1:0:0:", 1, tmp_path)
    assert not blank.pixels(4, 100, 215, 190, 199) and not blank.pixels(1, 100, 215, 190, 199)


def invented_assets(tmp_path, labels, names):
    """The real assets with tables/target_text.bin replaced by invented strings (3 labels of 4 characters, 8 names of 6): symlinks, nothing copied."""
    root = tmp_path / "assets-invented"
    root.mkdir()
    for e in ASSETS.iterdir():
        if e.name != "tables":
            (root / e.name).symlink_to(e)
    (root / "tables").mkdir()
    for e in (ASSETS / "tables").iterdir():
        if e.name != "target_text.bin":
            (root / "tables" / e.name).symlink_to(e)
    import struct
    (root / "tables" / "target_text.bin").write_bytes(struct.pack("<4H", 3, 4, 8, 6) + "".join(labels).encode() + "".join(names).encode())
    return root


LABELS = ["AB1:", "CD2:", "EF3:"]
NAMES = ["GH0123", "IJ4567", "KL89MN", "OPQRST", "UVWXYZ", "ZY9876", "54321A", "BCDEFG"]


def text_px(text, x0, y):
    return set().union(*[glyph_px(ch, x0 + 4 * i, y) for i, ch in enumerate(text) if ch != " "])


@frontend
@pytest.mark.parametrize("cls,detected,name", [(0x12, 1, 0), (0x13, 1, 1), (0x14, 1, 2), (0x10, 1, 3), (0x15, 1, 4), (0x12, 0, 5), (0x16, 1, 6), (0x17, 1, 7), (0x11, 1, None), (0x00, 1, None)])
def test_the_target_data_line_names_the_type_by_the_class_and_the_undetected_text_when_the_detected_bit_is_clear(tmp_path, cls, detected, name):
    a = invented_assets(tmp_path, LABELS, NAMES)
    L = hud_run(BASE + f",tline=2:2:18:12345:{cls}:{detected}:0", 1, tmp_path, a)
    box = L.pixels(1, 120, 152, 190, 199)                                # the name stands at character 4: x 124 to 147
    assert box == (text_px(NAMES[name], 124, 192) if name is not None else set())
    L4 = hud_run(BASE + f",tline=2:1:18:12345:{cls}:{detected}:0", 1, tmp_path, a)
    assert L4.pixels(4, 120, 152, 190, 199) == box and not L4.pixels(1, 120, 152, 190, 199)   # the other colour bits draw pen 4


@frontend
@pytest.mark.parametrize("page,col,digits", [(1, 18, "12345"), (2, 19, "090"), (3, 18, " 450")])
def test_the_target_data_line_draws_the_page_label_at_character_13_before_the_digits(tmp_path, page, col, digits):
    a = invented_assets(tmp_path, LABELS, NAMES)
    L = hud_run(BASE + f",tline=2:2:{col}:{digits}:18:1:{page}", 1, tmp_path, a)
    assert L.pixels(1, 156, 179, 190, 199) == text_px(LABELS[page - 1], 160, 192)
    assert text_px(digits, 108 + 4 * col, 192) <= L.pixels(1, 180, 215, 190, 199)
    none = hud_run(BASE + f",tline=2:2:{col}:{digits}:18:1:0", 1, tmp_path, a)             # no page: no label
    assert not none.pixels(1, 156, 179, 190, 199)


@frontend
def test_without_the_target_text_file_only_the_digits_are_drawn(tmp_path):
    a = invented_assets(tmp_path, LABELS, NAMES)
    (a / "tables" / "target_text.bin").unlink()
    L = hud_run(BASE + ",tline=2:2:18:12345:18:1:1", 1, tmp_path, a)
    assert not L.pixels(1, 120, 179, 190, 199) and text_px("12345", 108 + 72, 192) <= L.pixels(1, 180, 215, 190, 199)


# ---- the range number of the lock marker (note 07 12.7: five characters from x 188, top row 123, pen 13) ------------------------------------

GUN = BASE.replace("sel=13", "sel=0x1B")
MISSILE = BASE.replace("sel=13", "sel=0x3B")
NUMBOX = (186, 210, 121, 127)


@frontend
@pytest.mark.parametrize("sel", [GUN, MISSILE])
@pytest.mark.parametrize("text", ["-0042", " 0420", "-0000", " 9999"])
def test_the_range_number_is_drawn_in_pen_13_at_the_notes_position(tmp_path, sel, text):
    L = hud_run(sel + f",designated=1,range=1000,pipper_x=159,pipper_y=91,number={text}", 1, tmp_path)
    assert L.pixels(13, *NUMBOX) == text_px(text, 188, 123)             # a space (the closing target's sign) is not drawn


@frontend
def test_no_range_number_without_the_core_asking_for_it(tmp_path):
    base = GUN + ",designated=1,range=1000,pipper_x=159,pipper_y=91"
    assert not hud_run(base, 1, tmp_path).pixels(13, *NUMBOX)


# ---- the keys (main.c) ------------------------------------------------------------------------------------------------------------------

def flight_layer(keys, frames, tmp_path, ups=()):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    out = tmp_path / f"k{abs(hash((tuple(keys), tuple(ups), frames)))}"
    cmd = [str(BIN), "--assets", str(ASSETS), "--scale", "1", "--flight", "--cockpit", "--frames", str(frames), "--dump", f"{out}.ppm", "--dump-layer", f"{out}.layer"]
    for k in keys:
        cmd += ["--key", k]
    for k in ups:
        cmd += ["--keyup", k]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    assert r.returncode == 0, r.stderr
    return Layer(Path(f"{out}.layer").read_bytes())


@frontend
def test_the_w_key_replenishes_the_rounds_after_the_gun_has_fired(tmp_path):
    import test_hud
    sel = ["Return@2", "Return@3", "Return@4"]                       # AMRAAM, Sidewinder, gun
    fired = flight_layer(sel + ["Space@6"], 20, tmp_path, ups=["Space@12"])
    again = flight_layer(sel + ["Space@6", "W@14"], 20, tmp_path, ups=["Space@12"])

    def digits(L):
        h = test_hud.Layer(L.d)
        return h.text([108, 112, 116], 129, 13).strip()
    assert digits(fired) != "500" and digits(fired)
    assert digits(again) == "500"
