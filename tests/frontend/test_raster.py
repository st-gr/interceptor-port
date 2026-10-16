# tests/frontend/test_raster.py - drives build/raster_driver; framebuffer comes back as PPM (grey = index*8).
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "raster_driver"
W, H = 320, 200
pytestmark = pytest.mark.skipif(not BIN.exists(), reason="build/raster_driver not built")


def run(tmp_path, cmds):
    out = tmp_path / "f.ppm"
    script = "\n".join(cmds + [f"dump {out}"]) + "\n"
    subprocess.run([str(BIN)], input=script.encode(), check=True, timeout=30)
    data = out.read_bytes()
    hdr = b"P6\n320 200\n255\n"
    assert data.startswith(hdr)
    body = data[len(hdr):]
    return [body[3 * i] // 8 for i in range(W * H)]


def pix(fb, x, y):
    return fb[y * W + x]


def where(fb, c):
    return [(i % W, i // W) for i, v in enumerate(fb) if v == c]


def test_triangle_count_and_bbox(tmp_path):
    fb = run(tmp_path, ["poly 3 20 20 120 20 20 120"])
    pts = where(fb, 3)
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    assert (min(xs), min(ys)) == (20, 21)   # the blitter filler of note 04: the top row of the polygon holds no edge pixel and is not set
    assert max(xs) <= 120 and max(ys) == 119   # the apex row holds two edge pixels on the same column: they cancel in the XOR plane
    assert abs(len(pts) - 5000) <= 250      # area 100*100/2


def test_rectangle_exact(tmp_path):
    fb = run(tmp_path, ["poly 2 10 10 30 10 30 20 10 20"])
    pts = where(fb, 2)
    assert len(pts) == 21 * 10 and {p[1] for p in pts} == set(range(11, 21))   # x inclusive, rows ymin + 1 .. ymax


def test_crop_left_right(tmp_path):
    fb = run(tmp_path, ["poly 4 0 50 319 50 319 60 0 60"])
    pts = where(fb, 4)
    assert len(pts) == 320 * 10
    assert {p[1] for p in pts} == set(range(51, 61))


def test_crop_row_limit(tmp_path):
    fb = run(tmp_path, ["limit 144", "poly 5 0 100 319 100 319 179 0 179"])
    pts = where(fb, 5)
    assert max(p[1] for p in pts) == 144
    assert len(pts) == 320 * 44             # rows 101..144


def test_row_limit_leaves_rest_untouched(tmp_path):
    fb = run(tmp_path, ["clear 1", "limit 144", "ground 4", "skyfill 0 13 1 0 0 319 0 319 89 0 89",
                        "poly 7 0 0 319 0 319 179 0 179", "line 7 0 199 319 150"])
    assert all(pix(fb, x, y) == 1 for y in range(145, 200) for x in range(W))
    assert all(pix(fb, x, 0) == 1 for x in range(W))   # row 0 not part of the window


def test_outside_is_noop(tmp_path):
    # the clipper keeps every vertex in 0..319 x 0..179; a polygon outside that space is not drawn (the filler returns "none")
    fb = run(tmp_path, ["poly 3 400 10 500 10 450 50", "poly 3 10 -80 50 -80 30 -5",
                        "poly 3 10 300 50 300 30 400", "poly 3 -200 10 -100 10 -150 50"])
    assert not where(fb, 3)


@pytest.mark.parametrize("cmd", [
    "poly 3 10 10 50 50",                       # n = 2
    "poly 3 10 10",                             # n = 1
    "poly 3",                                   # n = 0
])
def test_degenerate_noop(tmp_path, cmd):
    fb = run(tmp_path, [cmd])
    assert not where(fb, 3)


def test_extreme_coordinates_do_not_crash(tmp_path):
    big = 32767
    cmds = [f"poly 3 {-big-1} {-big-1} {big} {-big-1} {big} {big}",
            f"poly 3 {-big-1} 100 {big} 100 0 {big}",
            f"poly 3 {big} {big} {big} {big-1} {big-2} {big}",
            f"poly 3 {-big-1} {big} {big} {-big-1} 0 0",
            f"line 3 {-big-1} {-big-1} {big} {big}"]
    fb = run(tmp_path, cmds)
    assert len(fb) == W * H
    assert not where(fb, 3)                 # all outside the projection space: nothing is drawn


def test_line(tmp_path):
    fb = run(tmp_path, ["line 9 10 10 20 10", "line 9 -50 5 400 5"])
    # the blitter line of note 04 draws rows y0 + 1 to y1 like a polygon edge, a horizontal line included (the carrier-deck frame)
    assert len([p for p in where(fb, 9) if p[1] == 11]) == 11 and not any(p[1] == 10 for p in where(fb, 9))
    assert len([p for p in where(fb, 9) if p[1] == 6]) == 320


# --- plane-aware primitives (note 03, "Primitive output") ---
SQ = "10 10 30 10 30 20 10 20"   # 20x10 rectangle area rows 10..19, columns 10..30


def test_planes_solid_full_mask_equals_plain_fill(tmp_path):
    a = run(tmp_path, ["clear 16", f"poly 5 {SQ}"])
    b = run(tmp_path, ["clear 16", f"pply 15 5 -1 0 {SQ}"])
    assert a == b


def test_planes_solid_partial_mask(tmp_path):
    # mask 6 pen 2: plane 1 set, plane 2 cleared, planes 0 and 3 untouched
    fb = run(tmp_path, ["clear 13", f"pply 6 2 -1 0 {SQ}"])
    assert pix(fb, 15, 15) == (13 & ~4) | 2 and pix(fb, 15, 15) == 11
    assert pix(fb, 5, 15) == 13


def test_planes_pattern_and_toggle(tmp_path):
    # pattern 8 mask 8: plane 3 set only
    fb = run(tmp_path, ["clear 5", f"pply 8 0 8 0 {SQ}"])
    assert pix(fb, 15, 15) == 13
    # mask 3, pattern 1: plane 0 set, plane 1 cleared
    fb = run(tmp_path, ["clear 2", f"pply 3 0 1 0 {SQ}"])
    assert pix(fb, 15, 15) == 1
    # mask 2 pattern 0 toggle 2: plane 1 toggles (twice = original)
    fb = run(tmp_path, ["clear 4", f"pply 2 13 0 2 {SQ}"])
    assert pix(fb, 15, 15) == 6
    fb = run(tmp_path, ["clear 4", f"pply 2 13 0 2 {SQ}", f"pply 2 13 0 2 {SQ}"])
    assert pix(fb, 15, 15) == 4


def test_planes_pattern_row_limit(tmp_path):
    fb = run(tmp_path, ["clear 0", "limit 14", f"pply 15 0 15 0 {SQ}"])
    assert pix(fb, 15, 14) == 15 and pix(fb, 15, 15) == 0


def test_line_planes(tmp_path):
    fb = run(tmp_path, ["clear 3", "linep 4 4 10 50 40 50"])
    assert where(fb, 7) == [(x, 51) for x in range(10, 41)]
    fb = run(tmp_path, ["clear 3", "limit 50", "linep 15 9 0 0 5 60", "linep 15 9 330 50 -5 50"])
    assert all(y >= 1 for _, y in where(fb, 9)) and max(y for _, y in where(fb, 9)) == 50
    assert not any(pix(fb, x, 51) == 9 for x in range(320))


def test_sprite_1x1_and_2x1(tmp_path):
    fb = run(tmp_path, ["clear 0", "sprite 0 15 7 100 50", "sprite 1 15 8 200 60"])
    assert where(fb, 7) == [(100, 50)]
    assert where(fb, 8) == [(199, 60), (200, 60)]


def test_sprite_2x1_at_x0_wraps(tmp_path):
    fb = run(tmp_path, ["clear 0", "sprite 1 15 8 0 60"])
    assert where(fb, 8) == [(319, 59), (0, 60)] or sorted(where(fb, 8)) == [(0, 60), (319, 59)]


def test_sprite_2x2(tmp_path):
    fb = run(tmp_path, ["clear 0", "sprite 2 15 7 101 50", "sprite 2 15 8 32 70", "sprite 2 15 9 0 80"])
    assert where(fb, 7) == [(100, 50), (101, 50), (100, 51), (101, 51)]
    assert where(fb, 8) == [(32, 70), (33, 70), (32, 71), (33, 71)]
    assert where(fb, 9) == [(0, 80), (1, 80), (0, 81), (1, 81)]


def test_sprite_2x2_falls_back_at_row_limit(tmp_path):
    fb = run(tmp_path, ["clear 0", "limit 100", "sprite 2 15 7 51 99", "sprite 2 15 8 71 100", "sprite 2 15 9 20 101"])
    assert where(fb, 7) == [(50, 99), (51, 99), (50, 100), (51, 100)]
    assert where(fb, 8) == [(70, 100), (71, 100)]
    assert where(fb, 9) == []


def test_sprite_rejects_y_le_0_and_planes(tmp_path):
    fb = run(tmp_path, ["clear 0", "sprite 0 15 7 10 0", "sprite 2 15 7 10 -3"])
    assert where(fb, 7) == []
    fb = run(tmp_path, ["clear 5", "sprite 0 2 15 10 10"])   # only plane 1
    assert pix(fb, 10, 10) == 7


def disc_widths(fb, y0, y1):
    return [sum(1 for x in range(W) if fb[y * W + x] == 9) for y in range(y0, y1)]


@pytest.mark.parametrize("R,expect", [
    (1, [3, 3]),
    (2, [3, 5, 5, 3]),
    (3, [3, 5, 7, 7, 5, 3]),
    (4, [3, 7, 7, 9, 9, 7, 7, 3]),
])
def test_disc_rows(tmp_path, R, expect):
    fb = run(tmp_path, ["clear 0", f"disc {R} 15 9 100 50"])
    assert disc_widths(fb, 50 - R + 1, 50 + R + 1) == expect
    assert sum(disc_widths(fb, 1, 120)) == sum(expect)


def test_disc_r5_begins(tmp_path):
    fb = run(tmp_path, ["clear 0", "disc 5 15 9 100 50"])
    w = disc_widths(fb, 46, 56)
    assert [(v - 1) // 2 for v in w] == [2, 3, 4, 5, 5, 5, 5, 4, 3, 2]


def test_disc_r0_is_2x1_and_clipping(tmp_path):
    fb = run(tmp_path, ["clear 0", "disc 0 15 9 100 50"])
    assert where(fb, 9) == [(99, 50), (100, 50)]
    fb = run(tmp_path, ["clear 0", "limit 52", "disc 4 15 9 2 50"])
    assert max(y for _, y in where(fb, 9)) == 52 and min(x for x, _ in where(fb, 9)) == 0


# --- the window base and the sky polygon filler (note 04, "The window clear and the pens", "Filling the polygon") ---

def test_ground_base_rows_1_to_limit(tmp_path):
    fb = run(tmp_path, ["clear 16", "limit 144", "ground 4"])
    assert all(pix(fb, x, y) == 4 for y in range(1, 145) for x in (0, 159, 319))
    assert all(pix(fb, x, 0) == 16 and pix(fb, x, 145) == 16 for x in (0, 159, 319))


def sky_rows(fb, x):
    return [y for y in range(0, H) if pix(fb, x, y) == 5]


def test_level_sky_polygon_last_sky_row_89_first_ground_row_90(tmp_path):
    # the polygon of level flight: vertices at y = 89 (note 04: sky rows 1 to 89, ground from 90)
    fb = run(tmp_path, ["clear 16", "limit 144", "ground 4", "skyfill 0 13 1  0 89 319 89 319 0 0 0"])
    for x in (0, 80, 160, 240, 319):
        assert sky_rows(fb, x) == list(range(1, 90)) and pix(fb, x, 90) == 4


def test_sky_polygon_over_the_sea_clears_plane_1(tmp_path):
    fb = run(tmp_path, ["clear 16", "limit 144", "ground 6", "skyfill 1 13 1  0 89 319 89 319 0 0 0"])
    assert pix(fb, 100, 50) == 5 and pix(fb, 100, 100) == 6          # sky pen 5 on either base


def test_inverted_whole_window_and_limit(tmp_path):
    fb = run(tmp_path, ["clear 16", "limit 144", "ground 4", "skyfill 0 13 1  0 0 319 0 319 179 0 179"])
    assert sky_rows(fb, 10) == list(range(1, 145))                    # the whole window, nothing in row 0, nothing below the limit
    assert pix(fb, 10, 145) == 16 and pix(fb, 10, 0) == 16


def test_sky_edges_cover_rows_ymin_plus_1_to_ymax_inclusive(tmp_path):
    # a triangle (10,20) (50,20) (30,60): the edges cover rows 21..60 (the top row 20 is not set); the two edges meet in one pixel of row 60 and the XOR
    # of the blitter cancels it (a vertex shared at the bottom is drawn twice), so the last row with sky is 59
    out = run(tmp_path, ["clear 16", "limit 179", "ground 4", "skyfill 0 13 1 10 20 50 20 30 60"])
    assert sky_rows(out, 30)[0] == 21 and sky_rows(out, 30)[-1] == 59 and pix(out, 30, 60) == 4


def test_shallow_line_one_dot_per_row_and_fill_between_edge_pixels(tmp_path):
    # a shallow edge from (0, 40) to (300, 50) with the right side vertical: each row has one edge pixel on the shallow edge, the fill spans to the vertical edge
    out = run(tmp_path, ["clear 16", "limit 179", "ground 4", "skyfill 0 13 1 0 40 300 50 300 0 0 0"])
    for y in range(1, 41):
        assert pix(out, 5, y) == 5 and pix(out, 300, y) == 5
    assert pix(out, 5, 45) == 4          # the shallow edge has left the polygon's rows below: ground
    assert pix(out, 100, 44) in (4, 5)


def test_edge_below_the_limit_is_not_drawn_and_rows_are_cut(tmp_path):
    out = run(tmp_path, ["clear 16", "limit 100", "ground 4", "skyfill 0 13 1 0 150 319 150 319 0 0 0"])
    assert pix(out, 100, 100) == 5 and pix(out, 100, 101) == 16      # the sky runs to the limit and stops there
    out = run(tmp_path, ["clear 16", "limit 100", "ground 4", "skyfill 0 13 1 0 200 100 200 50 120"])
    assert not [1 for y in range(H) for x in range(W) if pix(out, x, y) == 5]       # miny above the limit: nothing


# the tiny polygons: a box of at most two rows or one column (cockpit view: pen 13, mask 1)
def test_tiny_dot_sets_plane_0_only(tmp_path):
    out = run(tmp_path, ["clear 16", "limit 144", "ground 4", "skyfill 0 13 1  50 20 50 21 51 20"])      # h 1, w 1: dots at (50,21) and (51,21)
    assert [(x, y) for y in range(H) for x in range(W) if pix(out, x, y) == 5] == [(50, 21), (51, 21)]
    out = run(tmp_path, ["clear 16", "limit 144", "ground 6", "skyfill 0 13 1  50 20 50 20 50 20"])
    assert [(x, y) for y in range(H) for x in range(W) if pix(out, x, y) == 7] == [(50, 21)]               # sea 6 becomes 7 by the dot


def test_tiny_line_replaces_the_pen(tmp_path):
    out = run(tmp_path, ["clear 16", "limit 144", "ground 4", "skyfill 0 13 1  0 0 319 0 319 1 0 1"])    # h 1, w 319: pen 13 across row 1
    assert [pix(out, x, 1) for x in (0, 100, 319)] == [13, 13, 13] and pix(out, 100, 0) == 16 and pix(out, 100, 2) == 4
    out = run(tmp_path, ["clear 16", "limit 144", "ground 4", "skyfill 0 13 1  100 10 100 15 101 15"])  # h 5, w 1: the diagonal of the box, pen 13
    pts = [(x, y) for y in range(H) for x in range(W) if pix(out, x, y) == 13]
    assert [y for _, y in pts] == list(range(11, 16)) and {x for x, _ in pts} <= {100, 101}           # one pixel per row, rows miny + 1 to maxy


def test_tiny_block_and_the_multiple_of_16_rule(tmp_path):
    out = run(tmp_path, ["clear 16", "limit 144", "ground 4", "skyfill 0 13 1  50 20 51 22 50 22"])     # h 2, w 1: block at (maxx-1, maxx) = (50, 51), rows 21 and 22
    assert sorted((x, y) for y in range(H) for x in range(W) if pix(out, x, y) == 5) == [(50, 21), (50, 22), (51, 21), (51, 22)]
    out = run(tmp_path, ["clear 16", "limit 144", "ground 4", "skyfill 0 13 1  63 20 64 22 64 22"])     # maxx 64 is a multiple of 16: columns (64, 65)
    assert sorted((x, y) for y in range(H) for x in range(W) if pix(out, x, y) == 5) == [(64, 21), (64, 22), (65, 21), (65, 22)]
    out = run(tmp_path, ["clear 16", "limit 21", "ground 4", "skyfill 0 13 1  50 20 51 22 50 22"])      # miny + 1 = limit: two dots at maxx and maxx - 1
    assert sorted((x, y) for y in range(H) for x in range(W) if pix(out, x, y) == 5) == [(50, 21), (51, 21)]


def test_mesh_line_case_replaces_the_pen_with_6_on_all_planes(tmp_path):
    # a degenerate mesh polygon (one row high, at least two columns wide: 76 hits in 47 of the 210 states of note 04) is a line that REPLACES the pen with 6 on all four
    # planes (mask forced to 0xF); the general mesh fill ORs plane 1 only (pen 5 sky becomes 7)
    out = run(tmp_path, ["clear 16", "limit 144", "ground 5", "pply 2 6 -1 0  20 30 60 30 60 31 20 31", "pply 2 6 -1 0  100 40 140 40 140 70 100 70"])
    assert [pix(out, x, 31) for x in (20, 40, 60)] == [6, 6, 6] and pix(out, 40, 30) == 5 and pix(out, 40, 32) == 5
    assert pix(out, 120, 55) == 7


def test_tiny_external_view_pen_0(tmp_path):
    out = run(tmp_path, ["clear 16", "limit 167", "ground 4", "skyfill 0 0 15  0 0 319 0 319 1 0 1"])
    assert pix(out, 100, 1) == 0 and pix(out, 100, 2) == 4


# --- the palette fade (note 04, "Palette in flight") ---

def fade_levels(target, n, start=15):
    script = "\n".join([f"fade {target} 40"] if start == 15 else ["fade 2 40", f"fade {target} {n}"])
    r = subprocess.run([str(BIN)], input=(script + "\n").encode(), check=True, capture_output=True, timeout=30)
    return [int(v) for v in r.stdout.split()]


def test_fade_goes_down_one_level_per_two_blanks_and_up_one_per_three():
    down = fade_levels(2, 40)
    changes = [i for i in range(1, len(down)) if down[i] != down[i - 1]]
    assert down[0] == 14 and down[-1] == 2                              # the first blank already steps (the game frame's latency is the core's)
    assert len(changes) == 12 and all(b - a == 2 for a, b in zip(changes, changes[1:]))
    both = fade_levels(15, 60, start=2)                                 # down for 40 blanks, then back up
    up = both[40:]
    ch = [i for i in range(1, len(up)) if up[i] != up[i - 1]]
    assert up[-1] == 15 and len(ch) >= 11 and all(b - a == 3 for a, b in zip(ch, ch[1:]))


def test_fade_first_step_comes_counter_plus_one_blanks_after_the_target_is_seen():
    for cnt, first in ((0, 1), (1, 2), (2, 3)):   # the snapshot's 2: level 14 at blank 3 (note 04)
        r = subprocess.run([str(BIN)], input=f"fadecnt {cnt}\nfade 2 8\n".encode(), check=True, capture_output=True, timeout=30)
        lv = [int(v) for v in r.stdout.split()]
        assert lv.index(14) + 1 == first, (cnt, lv)
        assert lv[first - 1] == 14 and (first == 1 or lv[first - 2] == 15)
    # after a descent the counter stands at 1: the first step up (target seen at blank 1) comes at blank 2; the next 3 blanks later
    r = subprocess.run([str(BIN)], input=b"fadelevel 2\nfadecnt 1\nfade 15 6\n", check=True, capture_output=True, timeout=30)
    assert [int(v) for v in r.stdout.split()] == [2, 3, 3, 3, 4, 4]


def test_fade_stays_put_when_the_levels_are_equal():
    assert set(fade_levels(15, 20)) == {15}


def test_object_polygons_use_the_blitter_filler_for_every_fill_state(tmp_path):
    # the same coverage whatever the plane operation: solid, partial mask, pattern and toggle (note 04 filler; the top row of the box is not set)
    tri = "20 20 120 20 20 120"
    cov = where(run(tmp_path, ["clear 16", f"pply 15 5 -1 0 {tri}"]), 5)
    assert cov and min(p[1] for p in cov) == 21 and max(p[1] for p in cov) == 119
    pat = run(tmp_path, ["clear 5", f"pply 8 0 8 0 {tri}"])
    assert where(pat, 13) == cov            # plane 3 set where the polygon is, pen 5 elsewhere
    tog = run(tmp_path, ["clear 4", f"pply 2 13 0 2 {tri}"])
    assert where(tog, 6) == cov


def test_a_horizontal_line_is_one_row_below_its_end_points_and_a_slope_follows_the_edge_rule(tmp_path):
    fb = run(tmp_path, ["clear 0", "linep 15 7 100 93 120 93"])
    assert where(fb, 7) == [(x, 94) for x in range(100, 121)]
    # the same line from either end
    a = run(tmp_path, ["clear 0", "linep 15 7 10 20 50 33"])
    b = run(tmp_path, ["clear 0", "linep 15 7 50 33 10 20"])
    assert a == b and where(a, 7)[0][1] == 21 and where(a, 7)[-1][1] == 33


# --- the near-ground pass's pixels (stage [90], docs/re/04-scene-composition.md): a speck clears plane 2 of the pixels it covers (sea pen 6 -> 2, land 4 -> 0, sky 5 -> 1, 7 -> 3; a pen without plane 2
# stays), the general-fill triangle is the alternate blit (plane 2 := plane 3 under the filled area) ---
def row_of_pens(tmp_path, cmds, y):
    """Sets pen p at (p, y) for p = 0..15, runs the commands, returns the 16 pens of the row."""
    fb = run(tmp_path, ["clear 0"] + [f"pen {p} {y} {p}" for p in range(16)] + cmds)
    return [pix(fb, p, y) for p in range(16)]


def test_a_speck_clears_plane_2_of_every_pen(tmp_path):
    expect = [p & ~4 for p in range(16)]                    # 6 -> 2, 4 -> 0, 5 -> 1, 7 -> 3, 12 -> 8, 14 -> 10
    cmds = [f"sprite 0 4 3 {p} 50" for p in range(16)]      # the 1 by 1 sprite with the pass's block: mask 4, colour 3
    assert row_of_pens(tmp_path, cmds, 50) == expect


def test_the_2x1_speck_covers_x_minus_1_and_x_and_wraps_at_column_0(tmp_path):
    fb = run(tmp_path, ["clear 6", "sprite 1 4 3 100 50", "sprite 1 4 3 0 60", "sprite 1 4 3 200 0", "sprite 0 4 3 150 0"])
    assert where(fb, 2) == [(99, 50), (100, 50), (319, 59), (0, 60)]   # (0, Y) and (319, Y - 1) at X = 0; a sprite with row 0 is rejected


def test_the_line_dot_and_block_cases_clear_plane_2_only(tmp_path):
    # a box of one row and four columns: the line (minx .. maxx, miny + 1)
    fb = run(tmp_path, ["clear 6", "gtri 0 10 20 14 20 12 21"])
    assert where(fb, 2) == [(x, 21) for x in range(10, 15)] and not where(fb, 0)
    # three rows and one column: the diagonal of the box from (minx, miny) to (maxx, maxy), rows miny + 1 .. maxy
    fb = run(tmp_path, ["clear 4", "gtri 0 30 20 31 23 30 22"])
    assert sorted(set(p[1] for p in where(fb, 0))) == [21, 22, 23] and len(where(fb, 0)) == 3
    # a box of at most one row and one column: a dot at (minx, miny + 1), a second at (maxx, miny + 1) when w = 1
    fb = run(tmp_path, ["clear 6", "gtri 0 40 20 41 21 40 21", "gtri 1 60 30 60 30 60 31"])
    assert where(fb, 2) == [(40, 21), (41, 21), (60, 31)]
    # two rows and at most two columns: the 2 by 2 block of dots at rows miny + 1 and miny + 2, columns (maxx - 1, maxx)
    fb = run(tmp_path, ["clear 6", "gtri 0 80 20 82 22 81 21"])
    assert where(fb, 2) == [(81, 21), (82, 21), (81, 22), (82, 22)]
    # the planes other than 2 stay: pen 5 (sky) -> 1, pen 7 -> 3, a pen with plane 2 clear stays
    fb = run(tmp_path, ["clear 5", "gtri 0 10 20 14 20 12 21", "pen 100 40 7", "pen 101 40 0", "gtri 0 100 39 101 39 100 39"])
    assert pix(fb, 12, 21) == 1 and pix(fb, 100, 40) == 3 and pix(fb, 101, 40) == 0


def lcg_pens(seed):
    st, out = seed, []
    for _ in range(W * H):
        st = (st * 1103515245 + 12345) & 0xFFFFFFFF
        out.append((st >> 16) & 15)
    return out


def lcg_pens_below(seed):
    st, out = seed ^ 0x5A5A, []
    for _ in range(W * H):
        st = (st * 1103515245 + 12345) & 0xFFFFFFFF
        out.append((st >> 16) & 15)
    return out


def note_alt_blit(px, tri, limit=179, below=None):
    """The alternate blit as note 04 ("The blit's C channel") states it: the rectangle is the polygon's rows (miny .. min(maxy, L)) and the words from the one of max(minx - 1, 0) to the one of maxx (W words);
    descending, so C of the row r (up from the bottom row) and the word i (from the right) is read at the bottom-right word's plane-2 address minus r (40 + 2 W) minus 2 i, from the planes before the blit, the
    planes lying 8,000 bytes apart with 40 bytes per row (below plane 2 the read is plane 1's end); a pixel under the filled area gets plane 2 := plane 3, any other pixel of the rectangle the C bit at its position."""
    px = list(px)
    for i in range(W):
        px[i] = 0                                                # row 0 of every plane is zero
    for i in range((limit + 1) * W, W * H):
        px[i] = below[i] if below else 0                        # the rows below the window: the page's panel
    mem = bytearray(4 * 8000)
    for y in range(H):
        for x in range(W):
            for k in range(4):
                if px[y * W + x] >> k & 1:
                    mem[k * 8000 + y * 40 + x // 8] |= 0x80 >> (x % 8)
    xs, ys = tri[0::2], tri[1::2]
    wl, wr = max(min(xs) - 1, 0) // 16, max(xs) // 16
    nw, ylast = wr - wl + 1, min(max(ys), limit)
    out = list(px)
    for y in range(min(ys), ylast + 1):
        r = ylast - y
        for x in range(wl * 16, wr * 16 + 16):
            if (x, y) in AREA:
                bit = px[y * W + x] >> 3 & 1
            else:
                addr = 2 * 8000 + ylast * 40 + 2 * wr - r * (40 + 2 * nw) - 2 * (wr - x // 16)
                if addr < 0:
                    bit = 0
                else:
                    byte = addr + (x % 16) // 8                       # the byte of the C word that holds the pixel's position
                    bit = mem[byte] >> (7 - x % 8) & 1
                    if addr < 2 * 8000 and bit != (px[y * W + x] >> 2 & 1):
                        if 8000 <= addr < 16000 and (addr - 8000) // 40 > limit:
                            PANEL[0] += 1                            # the wrapped read lands in plane 1's panel rows
                        WRAPPED[0] += 1                          # a C read below plane 2's start (plane 1's end) that differs from the in-place read
            out[y * W + x] = (px[y * W + x] & ~4) | bit << 2
    return out


AREA = set()
WRAPPED = [0]
PANEL = [0]


def test_the_alternate_blit_follows_the_c_channel_rule_of_the_note_on_random_rectangles(tmp_path):
    import random
    rnd = random.Random(11)
    global AREA
    done = 0
    for case in range(24):
        seed = rnd.randrange(1, 1 << 30)
        if case < 3:       # near the top of the window, so that C reads wrap into plane 1
            x0, y0 = rnd.randint(0, 200), rnd.randint(1, 8)
        else:
            x0, y0 = rnd.randint(0, 230), rnd.randint(1, 120)
        tri = [x0 + rnd.randint(2, 80), y0 + rnd.randint(0, 5), x0, y0 + rnd.randint(4, 60), x0 + rnd.randint(0, 80), y0 + rnd.randint(6, 70)]
        tri = [min(v, 319) if i % 2 == 0 else min(v, 179) for i, v in enumerate(tri)]
        xs, ys = tri[0::2], tri[1::2]
        if not ((max(ys) - min(ys) >= 3 and max(xs) - min(xs) >= 2) or (max(ys) - min(ys) == 2 and max(xs) - min(xs) >= 3)):
            continue                                                  # a small case: not the alternate blit
        base = lcg_pens(seed)
        limit = rnd.choice([179, 144, 100])
        mine = run(tmp_path, [f"limit {limit}", f"rand {seed}", f"below {seed}", "falt 15 0 " + " ".join(map(str, tri))])
        # the area comes from the filler with the same row limit
        cover = run(tmp_path, ["clear 0", f"limit {limit}", "poly 15 " + " ".join(map(str, tri))])
        AREA = {(i % W, i // W) for i, v in enumerate(cover) if v == 15}
        want = note_alt_blit(base, tri, limit, lcg_pens_below(seed))
        y0, y1 = min(tri[1::2]), min(max(tri[1::2]), limit)
        want = [want[i] if y0 <= i // W <= y1 else base[i] for i in range(W * H)]   # (the rows outside the rectangle are the page as it was)
        done += 1
        bad = [(i % W, i // W) for i in range(W * H) if mine[i] != want[i]]
        assert not bad, (case, tri, bad[:5])
        assert any(mine[i] != base[i] for i in range(W * H))
    assert done >= 10
    assert PANEL[0] > 0                      # and some read lands in the panel rows below the window, giving the panel's bits
    assert WRAPPED[0] > 0                    # some case reads C from plane 1 with a result that differs from the in-place reading


def test_the_filler_alternate_path_equals_the_ground_triangle_with_alt(tmp_path):
    tri = "100 50 160 50 100 110"
    a = run(tmp_path, ["rand 5", f"falt 3 5 {tri}"])
    b = run(tmp_path, ["rand 5", f"gtri 1 {tri}"])
    c = run(tmp_path, ["rand 5", f"gtri 0 {tri}"])
    assert a == b and a != c
    assert all(v & 4 == 0 for v in [c[y * W + x] for y in range(60, 90) for x in range(102, 108)])   # the plain triangle clears plane 2 under the area


# --- the raster line (docs/re/03-3d-pipeline.md "The raster line"): the rule written out from the note and compared with the driver on random lines ---
def note_line(x0, y0, x1, y1, last_row):
    """Pixels of the line routine: the start is the end with the smaller y (equal y: the first end), drawn from (x_start, y_start + 1); dy1 = |dy| - 1 (0 for equal y), dx signed from the start end to the other
    end; major x when |dx| >= dy1; the error word 4 minor - 2 major (16 bits) with its sign flag; major + 1 pixels, cut at last_row."""
    if y1 < y0:
        x0, y0, x1, y1 = x1, y1, x0, y0
    dx, dy1 = x1 - x0, max(abs(y1 - y0) - 1, 0)
    major, minor = max(abs(dx), dy1), min(abs(dx), dy1)
    x, y = x0, y0 + 1
    if y > last_row:
        return []
    a = last_row - y
    if abs(dx) >= dy1:
        n = major + 1 if dy1 <= a else (2 * major * a // dy1 + 1) // 2 + 1
    else:
        n = min(major, a) + 1
    err = ((4 * minor - 2 * major) + 0x8000) % 0x10000 - 0x8000
    out = []
    for _ in range(n):
        out.append((x, y))
        neg = err < 0
        if abs(dx) >= dy1:
            x += 1 if dx >= 0 else -1
            if not neg:
                y += 1
        else:
            y += 1
            if not neg:
                x += 1 if dx >= 0 else -1
        err = ((err + (4 * minor if neg else 4 * (minor - major))) + 0x8000) % 0x10000 - 0x8000
    return out


def test_the_line_routine_follows_the_note_on_random_lines(tmp_path):
    import random
    rnd = random.Random(7)
    cmds, wanted = ["clear 0"], set()
    for i in range(36):
        # each line in its own 60 by 60 cell so that the lines do not touch; the cell's rows start at 1
        cx, cy = 10 + 70 * (i % 4), 1 + 63 * (i // 4 % 3)   # 12 cells, three lines per cell (the pen is the same, so overlaps do not matter)
        x0, y0 = cx + rnd.randint(0, 50), cy + rnd.randint(0, 50)
        x1 = x0 + rnd.choice([0, 0, rnd.randint(-30, 30)]) if rnd.random() < 0.3 else cx + rnd.randint(0, 50)
        y1 = y0 if rnd.random() < 0.2 else cy + rnd.randint(0, 50)
        cmds.append(f"linep 15 7 {x0} {y0} {x1} {y1}")
        wanted.update(p for p in note_line(x0, y0, x1, y1, 179) if 0 <= p[0] < 320)
    fb = run(tmp_path, cmds)
    assert set(where(fb, 7)) == wanted


def test_the_line_cut_at_the_last_row_and_the_equal_y_start(tmp_path):
    for line in [(10, 10, 60, 100, 40), (10, 10, 20, 100, 50), (100, 30, 10, 31, 179), (5, 50, 5, 50, 179), (200, 20, 100, 20, 179), (100, 20, 200, 20, 179), (30, 60, 31, 40, 45)]:
        x0, y0, x1, y1, lim = line
        fb = run(tmp_path, ["clear 0", f"limit {lim}", f"linep 15 7 {x0} {y0} {x1} {y1}"])
        assert set(where(fb, 7)) == set(note_line(x0, y0, x1, y1, lim)), line


def test_the_alternate_path_draws_a_tiny_box_with_the_objects_own_pen_and_mask(tmp_path):
    # only the general fill takes 0x34EEC: a box of one row and five columns is the filler's line case, drawn in the object's pen (the plane mask forced to 0xF while V:4A85A is 0)
    fb = run(tmp_path, ["clear 6", "falt 3 10 10 20 14 20 12 21"])
    assert where(fb, 10) == [(x, 21) for x in range(10, 15)]
    # a dot takes the planes of the mask from the pen
    fb = run(tmp_path, ["clear 6", "falt 3 9 40 20 41 21 40 21"])
    assert where(fb, 5) == [(40, 21), (41, 21)]       # pen 9 = 1001 on mask 0011: plane 0 set, plane 1 cleared: 6 -> 5


# --- the filler's ball entry 0x34CD0 (note 04, "The two entries of the filler and the limit of the tiny cases" and "The attitude ball's tiny cases") ---
def ball_run(tmp_path, limit, poly):
    out = tmp_path / "f.ppm"
    script = f"clear 16\nlimit {limit}\nball {poly}\ndump {out}\n"
    r = subprocess.run([str(BIN)], input=script.encode(), check=True, capture_output=True, timeout=30)
    data = out.read_bytes()[len(b"P6\n320 200\n255\n"):]
    fb = [data[3 * i] // 8 for i in range(W * H)]
    return int(r.stdout.split()[1]), fb


def test_ball_entry_dots_draw_pen_3_below_the_window_limit(tmp_path):
    # A = 199 decides the dot cases, so a dot at row 177 shows in the 144-row window; the pen is 3 on all four planes (the stale colour 3, mask 0xF)
    ret, fb = ball_run(tmp_path, 144, "50 176 50 176 51 176")
    assert ret == 1 and where(fb, 3) == [(50, 177), (51, 177)]
    ret, fb = ball_run(tmp_path, 144, "50 176 50 176 50 176")
    assert where(fb, 3) == [(50, 177)]
    ret, fb = ball_run(tmp_path, 144, "50 199 50 199 50 199")                  # miny + 1 = 200 > A: nothing
    assert ret == 1 and where(fb, 3) == []


def test_ball_entry_lines_cut_at_the_window_limit_not_at_199(tmp_path):
    # the two line cases read V:4AA3C themselves: in the 144-row window a line at row 177 is cut away entirely, in a 179-row window it is drawn
    for poly in ("100 170 100 175 101 175", "50 176 60 176 60 176"):             # the diagonal (h 5, w 1) and a horizontal line (h 0, w 10)
        ret, fb = ball_run(tmp_path, 144, poly)
        assert ret == 1 and where(fb, 3) == []
    ret, fb = ball_run(tmp_path, 179, "100 175 100 185 101 185")
    assert where(fb, 3) != []
    ret, fb = ball_run(tmp_path, 179, "50 176 60 176 60 176")
    assert where(fb, 3) == [(x, 177) for x in range(50, 61)]
    ret, fb = ball_run(tmp_path, 179, "100 170 100 185 101 185")                   # cut at the window's last row 179, not at the polygon's end
    assert max(y for _, y in where(fb, 3)) == 179


def test_ball_entry_block_takes_its_one_row_pair_from_the_window_limit(tmp_path):
    # h 2, w 1: the pair of two dots in one row when miny + 1 >= V:4AA3C (even with A = 199), the 2 by 2 block below that
    ret, fb = ball_run(tmp_path, 144, "50 176 51 178 50 178")
    assert ret == 1 and sorted(where(fb, 3)) == [(50, 177), (51, 177)]
    ret, fb = ball_run(tmp_path, 179, "50 176 51 178 50 178")
    assert sorted(where(fb, 3)) == [(50, 177), (50, 178), (51, 177), (51, 178)]


def test_ball_entry_general_fill_is_cut_at_199(tmp_path):
    ret, fb = ball_run(tmp_path, 144, "192 150 220 150 192 170")
    assert ret == 0 and max(y for _, y in where(fb, 5)) > 160      # rows below the 144-row window are filled (A = 199)
