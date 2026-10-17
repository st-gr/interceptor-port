"""The numbers 1 to 4 on the map overview of the location screen (docs/re/04-scene-composition.md, "The start marks of the overview map"): the mark pass 0x2FAB6 against the rule.

    python3 mapcam_check.py [SEED] [N]

The rule (a model written from the note, not from the code): for each entry of the start table at 0x47ADE (16 bytes; a first word below 0 names a ship record, 0xFFFF ends) the world point is the
runway position of note 05 17.14 (cell words, the direction pair, the offsets) or the ship record's x and z; it is turned into the camera frame by the camera longs V:4ACF6.. and the nine
words V:4AC90 and projected with the window's 160 / 90 scale; the dot (pen 2) is at (319 - X, 180 - Y) and the digit (pen 9) has its top-left pixel at ((x and not 3) + 8, y). The original runs in the
rig with the sky-ground rig (build/sky-ground/librigblit.so); N random camera states (the overview's own matrix and general ones), compared as the pixel sets of pens 2 and 9.
Needs extracted/interceptor.bin and the rig library; skips with exit 0 otherwise."""
import os, random, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "vamiga-ref", "sky-ground"))
import rigw

IMG = open(os.path.join(ROOT, "extracted", "interceptor.bin"), "rb").read()
def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v
def img16(a): return struct.unpack(">h", IMG[a - 0x10000:a - 0x10000 + 2])[0]
def img8(a): return IMG[a - 0x10000]
def sdiv(a, b): q = abs(a) // abs(b); return q if (a < 0) == (b < 0) else -q


def project(cam, M, wx, wz):
    dx = s32(wx - cam[0]); dy = s32(-cam[1]); dz = s32(wz - cam[2])
    dx, dy, dz = dx >> 14, dy >> 14, dz >> 14
    X = s16((s32(s16(dx) * M[0] + s16(dy) * M[1] + s16(dz) * M[2])) >> 8)
    Y = s16((s32(s16(dx) * M[3] + s16(dy) * M[4] + s16(dz) * M[5])) >> 8)
    Z = s16((s32(s16(dx) * M[6] + s16(dy) * M[7] + s16(dz) * M[8])) >> 8)
    if X >= Z or Y >= Z or s16(-X) >= Z or s16(-Y) >= Z or Z <= 0: return None
    px = sdiv(X * 160, Z) + 160; py = sdiv(Y * 90, Z) + 90
    px = 0 if px < 0 else px; px = 0x13F if px >= 0x140 else px; py = 0 if py < 0 else py; py = 0xB3 if py >= 0xB4 else py
    sx = 319 - px; sy = 179 - py + 1
    return sx, sy


_GLYPH = {}
def glyph(d):
    """The pixel set of digit d relative to its top-left (taken from the original's digit drawer 0x37508 drawn alone, once)."""
    if d not in _GLYPH:
        r = rigw.stage()
        r.w16(0x4AA0C, 9); r.poke(0x4A79E, struct.pack(">4H", 0xF, 0xFFFF, 0, 0))
        r.run(0x37508, d0=0x40, d1=0x40, d2=d, d3=0)
        pn = rigw.pens(r)
        pts = [(x, y) for y in range(200) for x in range(320) if pn[y][x]]
        _GLYPH[d] = [(x - 0x48, y - 0x40) for x, y in pts]     # relative to the glyph origin ((x and not 3) + 8, y) of a call at (0x40, 0x40)
    return _GLYPH[d]


def entries():
    a = 0x47ADE; out = []
    while True:
        w0 = img16(a)
        if w0 == -1: break
        out.append(a); a += 16
    return out


def model(cam, M, limit, ships):
    dots, digits, paint = set(), set(), {}
    for i, a in enumerate(entries()):
        w = [img16(a + 2 * k) for k in range(8)]
        if w[0] < 0:
            rec = ships.get(w[0] & 0x7FFF)
            if not rec: continue
            wx, wz = rec
        else:
            dirx, dirz = img8(0x207E6 + 2 * (w[2] & 0xFFFF)) , img8(0x207E6 + 2 * w[2] + 1)  # unused: the direction pair is the word table 0x206F2
            pr = struct.unpack(">hh", IMG[0x206F2 - 0x10000 + 4 * w[2]:0x206F2 - 0x10000 + 4 * w[2] + 4])
            wx = s32(((w[0] << 16) << 8) + (pr[0] << 10) + (w[3] << 10) + (w[5] << 4))
            wz = s32(((w[1] << 16) << 8) + (pr[1] << 10) + (w[4] << 10) + (w[6] << 4))
        p = project(cam, M, wx, wz)
        if not p or p[1] > limit: continue
        dots.add(p)
        digits.add(((p[0] & ~3) + 8, p[1], i + 1))
        if p[0] < 320: paint[p] = 2                              # in the original's order: this entry's dot, then its digit; a later entry's dot covers an earlier digit
        gx0 = (p[0] & ~3) + 8
        for gx, gy in glyph(i + 1):
            if gx0 + gx < 320: paint[(gx0 + gx, p[1] + gy)] = 9
    return dots, digits, paint


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    rnd = random.Random(seed)
    bad = shown = nd = 0
    for it in range(n):
        if rnd.random() < .5:
            M = [168, 0, 0, 0, 0, 252, 0, -128, 0]
            cam = [0x10800000 + rnd.randint(-(1 << 26), 1 << 26), rnd.choice([0x3000000, 0x5000000, rnd.randint(0x1900000, 0x7000000)]), 0x10C00000 + rnd.randint(-(1 << 26), 1 << 26)]
        else:
            M = [rnd.randint(-256, 256) for _ in range(9)]
            cam = [rnd.randint(0, (1 << 29) - 1), rnd.randint(0x1900000, 0x7000000), rnd.randint(0, (1 << 29) - 1)]
        limit = rnd.choice([144, 167, 179])
        r = rigw.stage(win=limit)
        for i, v in enumerate(M): r.w16(0x4AC90 + 2 * i, v)
        r.w8(0x4A95E, 1); r.w8(0x4A966, 5); r.w8(0x4A83D, 1); r.w8(0x4A866, 0); r.w8(0x4A90F, 0)
        r.w32(0x4AB1E, -0x3000000)
        r.poke(0x4A79E, struct.pack(">4H", 0xF, 0xFFFF, 0, 0))
        for k, v in enumerate(cam): r.w32(0x4ACF6 + 4 * k, v)
        ships = {}
        if rnd.random() < .7:
            rec = 0x4B23C + 14 * 512
            sx_, sz_ = rnd.randint(0, (1 << 29) - 1), rnd.randint(0, (1 << 29) - 1)
            if rnd.random() < .5: sx_, sz_ = 295174144 + rnd.randint(-(1 << 22), 1 << 22), 274202624 + rnd.randint(-(1 << 22), 1 << 22)
            r.w8(rec + 1, 0x40); r.w32(rec + 0x14, sx_); r.w32(rec + 0x1C, sz_); ships[14] = (sx_, sz_)
        r.run(0x2FAB6)
        pn = rigw.pens(r)
        got2 = {(x, y) for y in range(200) for x in range(320) if pn[y][x] == 2}
        got9 = [(x, y) for y in range(200) for x in range(320) if pn[y][x] == 9]
        dots, digits, paint = model(cam, M, limit, ships)
        nd += len(dots)
        exp9 = {k for k, v in paint.items() if v == 9}; exp2 = {k for k, v in paint.items() if v == 2}
        ok2 = set(got9) == exp9
        ok = got2 == exp2
        if not (ok and ok2):
            bad += 1
            if bad <= 5: print("MISMATCH cam", [hex(c) for c in cam], M, limit, "rig dots", sorted(got2), "model", sorted(dots), "pen 9 only in rig", sorted(set(got9) - exp9), "only in model", sorted(exp9 - set(got9)), "dots only in rig", sorted(got2 - exp2), "only model", sorted(exp2 - got2))
    print("mapcam_check seed %d: %d states, %d dots, %d mismatches" % (seed, n, nd, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
