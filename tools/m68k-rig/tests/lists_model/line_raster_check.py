"""The line routine 0x3455E drawn by the blitter model against the rule of docs/re/03-3d-pipeline.md "The line routine: start end and x step".

    python3 line_raster_check.py [SEED [N]]       (needs extracted/interceptor.bin and build/sky-ground/librigblit.so)

Random end points (some horizontal, some vertical, some short), a random last row L, mask, pen and solid or pattern mode, random pens under the line.  The rule: the start is the end with the smaller y
(equal y: the first end), at its row plus one; dy1 = |dy| - 1 (0 for equal y), dx the signed x difference from the start; major axis x when |dx| >= dy1, else y; error 4 minor - 2 major, a sixteen-bit word; every step moves the major axis
by one and the minor axis by one when the sign flag is clear; every pixel written, major + 1 of them, cut at the row L (shallow: round-half-up of major a / dy1; steep: min(major, a), a = L - start row)."""
import os, random, struct, sys


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def line_pixels(x0, y0, x1, y1, L):
    if y1 == y0:
        sy, xs, dy1, dx = y0 + 1, x0, 0, x1 - x0
    elif y1 > y0:
        sy, xs, dy1, dx = y0 + 1, x0, y1 - y0 - 1, x1 - x0
    else:
        sy, xs, dy1, dx = y1 + 1, x1, y0 - y1 - 1, x0 - x1
    if sy > L: return []
    adx = abs(dx); shallow = adx >= dy1
    major, minor = (adx, dy1) if shallow else (dy1, adx)
    a = L - sy
    length = (major if dy1 <= a else ((2 * major * a) // minor + 1) >> 1) if shallow else (a if major > a else major)
    apt = s16(4 * minor - 2 * major); sign = apt < 0
    x, y = xs, sy; out = []
    for _ in range(length + 1):
        out.append((x, y))
        if shallow:
            if not sign: y += 1
            x += -1 if dx < 0 else 1
        else:
            y += 1
            if not sign: x += -1 if dx < 0 else 1
        apt = s16(apt + (4 * minor if sign else 4 * (minor - major))); sign = apt < 0
    return out


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    N = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    try:
        import sys as _s
        _s.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "vamiga-ref", "sky-ground"))
        import rigw
    except SystemExit:
        print("skipped (needs librigblit.so)"); return 0
    rng = random.Random(seed); bad = 0
    for it in range(N):
        r = rigw.Rig()
        for i in range(4): r.w32(rigw.TAB + 4 * i, rigw.P0 + rigw.PS * (3 - i))
        r.w32(0x4A76E, rigw.TAB); r.w32(0x4A772, rigw.TAB); r.w32(0x4A79A, 0x500000)
        bg = [[rng.randint(0, 15) for x in range(320)] for y in range(200)]
        planes = [bytearray(8000) for _ in range(4)]
        for y in range(200):
            for x in range(320):
                for k in range(4):
                    if bg[y][x] >> k & 1: planes[k][y * 40 + x // 8] |= 0x80 >> (x % 8)
        for k in range(4): r.poke(rigw.P0 + rigw.PS * k, bytes(planes[k]))
        L = rng.choice((143, 166, 178, 199)); mode = rng.random()
        x0, x1 = rng.randint(0, 319), rng.randint(0, 319); y0, y1 = rng.randint(0, 198), rng.randint(0, 198)
        if mode < .15: y1 = y0
        elif mode < .3: x1 = x0
        elif mode < .5: x1 = min(max(x0 + rng.randint(-12, 12), 0), 319); y1 = min(max(y0 + rng.randint(-12, 12), 0), 198)
        mask = rng.randint(0, 15); pen = rng.randint(0, 15); solid = rng.random() < .6; pat = rng.randint(0, 255)
        r.w16(0x4AA3C, L); r.w16(0x4AA0C, pen | rng.choice((0, 0x10))); r.w16(0x4A79E, mask); r.w16(0x4A7A0, 0xFFFF if solid else pat); r.w16(0x4A7A2, 0)
        r.run(0x3455E, d0=x0, d1=y0, d2=x1, d3=y1)
        got = rigw.pens(r)
        col = pen if solid else (pat & 15)
        exp = [row[:] for row in bg]
        for (x, y) in line_pixels(x0, y0, x1, y1, L):
            if 0 <= x < 320 and 0 <= y < 200: exp[y][x] = (exp[y][x] & ~mask) | (col & mask)
        if got != exp:
            bad += 1
            if bad < 4: print("DIFF", it, (x0, y0, x1, y1), L, mask, pen, solid, pat)
    print("line raster: %d lines, differences %d" % (N, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
