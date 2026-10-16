"""Pixel effect of the near-ground pass (stage [90], docs/re/04-scene-composition.md "Stage [90]"): the events of nearground_model.py applied to a map of pens.

apply(events, pens, limit, cquirk=True) -> new pen map (200 rows of 320 pens).  The rules:
  * a point sprite (1 by 1 at (x, y), 2 by 1 at (x - 1, x), at x = 0 the pixels (0, y) and (319, y - 1)) with y <= 0 is rejected; every pixel loses plane 2 (the pen is 3, the mask 4);
  * a triangle goes to the filler 0x34A28 and takes one of its cases (fmodel.tiny: dot, block, line): every pixel loses plane 2 as well (a line because V:4A85A is set, so the mask is not forced to 0xF);
  * a triangle of the general fill (bounding box 3 rows or more and 2 columns or more, or 2 rows and 3 columns or more) takes the alternate blit 0x34EEC (V:4A7A4 is 0xFFFF and the pattern word 0 is not negative):
    a descending minterm blit (BLTCON0 0x0FCA: D = A ? B : C; A the filled area, B plane 3, C = D plane 2) over the rectangle of whole words from the word of max(minx - 1, 0) to the word of maxx and the
    rows miny to min(maxy, L).  The code never sets BLTCMOD, which holds 40 from the last edge line, while the A, B and D modulus is 40 - 2 W; so the C word of row r (r = 0 the bottom row) and word i
    from the right is read at the bottom row's plane-2 address minus r (40 + 2 W) minus 2 i, W words further to the left for every row up (below the start of plane 2 it is the end of plane 1, the
    planes of a page being contiguous).  Under the filled area plane 2 := plane 3; elsewhere in the rectangle plane 2 := that C bit (cquirk True, the hardware rule and the emulator's: note 04,
    "Stage [90]", the C-modulus probe; nearground_blit_check.py).  cquirk False reads C in place (plane 2 unchanged outside the area): the rule the note stated before the probe, kept for comparison.
fmodel.py is the polygon fill model of tools/vamiga-ref/sky-ground (edge lines, inclusive right-to-left fill)."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "vamiga-ref", "sky-ground"))
import fmodel


def apply(events, pens, limit, cquirk=True, cmod=None, alt=True):
    out = [row[:] for row in pens]
    if cmod is None and cquirk: cmod = 40                      # the modulus the last edge line left in BLTCMOD

    def clr(x, y):
        if 0 <= x < 320 and 0 <= y < 200:
            out[y][x] &= ~4
    for e in events:
        if e[0] == "s1":
            if e[2] > 0: clr(e[1], e[2])
        elif e[0] == "s2":
            x, y = e[1], e[2]
            if y > 0:
                if x: clr(x - 1, y); clr(x, y)
                else: clr(0, y); clr(319, y - 1)
        else:
            poly = list(e[1:])
            t = fmodel.tiny(poly, limit, 3, 4)
            if t is not None:
                for (x, y) in t: clr(x, y)
                continue
            status, pl = fmodel.fillpoly(poly, limit)
            if status == "skip": continue
            if not alt:                                        # the plain per-plane path (not taken by the pass): clear plane 2 under the area
                for y in range(200):
                    for x in range(320):
                        if pl[y][x]: out[y][x] &= ~4
                continue
            xs = [p[0] for p in poly]; ys = [p[1] for p in poly]
            minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
            wl = max(minx - 1, 0) >> 4; wr = maxx >> 4; W = wr - wl + 1
            ybot = min(maxy, limit)
            p1 = bytearray(8000); p2 = bytearray(8000); p3 = bytearray(8000)
            for y in range(200):
                for x in range(320):
                    if out[y][x] & 2: p1[y * 40 + x // 8] |= 0x80 >> (x % 8)
                    if out[y][x] & 4: p2[y * 40 + x // 8] |= 0x80 >> (x % 8)
                    if out[y][x] & 8: p3[y * 40 + x // 8] |= 0x80 >> (x % 8)
            new = bytearray(p2)
            for r in range(ybot - miny + 1):
                y = ybot - r
                for i in range(W):
                    w = wr - i
                    A = 0
                    for b in range(16):
                        if pl[y][w * 16 + b]: A |= 0x8000 >> b
                    B = (p3[y * 40 + 2 * w] << 8) | p3[y * 40 + 2 * w + 1]
                    co = ybot * 40 + 2 * wr - r * (cmod + 2 * W) - 2 * i if cmod is not None else y * 40 + 2 * w
                    C = ((p2[co] << 8) | p2[co + 1]) if co >= 0 else ((p1[8000 + co] << 8) | p1[8000 + co + 1])   # before plane 2: the end of plane 1
                    D = (A & B) | (~A & C & 0xFFFF)
                    new[y * 40 + 2 * w] = D >> 8; new[y * 40 + 2 * w + 1] = D & 255
            for y in range(200):
                for x in range(320):
                    out[y][x] = (out[y][x] & ~4) | (((new[y * 40 + x // 8] >> (7 - x % 8)) & 1) << 2)
    return out
