"""The polygon filler's tiny cases (docs/re/04-scene-composition.md "Filling the polygon", "The attitude ball's tiny cases") against the original, as pixel sets.

    python3 filler_check.py tiny [SEED] [N]     the filler 0x34CD6 and its limit-199 entry 0x34CD0: random boxes of at most 2 rows or 1 column (and some larger), random stale colour and
                                                mask, random base pen, every row limit: the changed pixels against the rule (fmodel.tiny of tools/vamiga-ref/sky-ground)
    python3 filler_check.py ball [SEED] [N]     the ball's fill 0x34C7C: the sky polygon at V:504DE (window coordinates) scaled by 24/256 in x and 31/256 in y, offset (193 + V:4AA40,
                                                162 + V:4A990), through the tiny cases at the limit 199 (not V:4AA3C): pixel sets against the rule
    python3 filler_check.py state               the colour word V:4AA0C and the mask block as the ball finds them: the panel routines run in the frame's order on the logged scenes
Needs extracted/interceptor.bin and build/sky-ground/librigblit.so (tiny, ball: bash tools/vamiga-ref/sky-ground/build.sh); state needs librig.so and the lists log. Skips with exit 0 otherwise."""
import os, random, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
SG = os.path.join(ROOT, "tools", "vamiga-ref", "sky-ground")


def tiny_poly(rnd, big):
    x0 = rnd.choice([0, 1, 2, 15, 16, 17, 100, 190, 200, 317, 318, 319]); y0 = rnd.choice([0, 1, 2, 60, 150, 162, 176, 178, 197, 198, 199])
    w = rnd.randint(0, 4 if not big else 8); h = rnd.randint(0, 5 if not big else 8)
    return [(min(319, x0 + rnd.randint(0, w)), min(199, y0 + rnd.randint(0, h))) for _ in range(rnd.choice([3, 4, 5]))]


def run_one(r, rigw, entry, pts, base, colour, mask, limit):
    for k in range(4):
        if (base >> k) & 1: r.poke(rigw.P0 + rigw.PS * k, b"\xff" * 8000)
    r.w16(0x4AA0C, colour); r.w16(0x4A79E, mask); r.w16(0x4A7A0, mask); r.w32(0x4A7A2, 0); r.w8(0x4A85A, 0)
    r.w16(0x5043C, len(pts))
    for i, (x, y) in enumerate(pts): r.w16(0x5043E + 4 * i, x); r.w16(0x50440 + 4 * i, y)
    r.run(entry)
    return rigw.pens(r)


def model(fm, pts, a4, lwin, colour, mask):
    """The rule: the filler's own limit is A4 (V:4AA3C for the entry 0x34CD6, 199 for the ball's entry 0x34CD0); the dot cases test miny + 1 against A4; the block case exits on miny + 1 > A4 but takes the one-row pair from miny + 1 >= V:4AA3C; the two line cases (3455E)
    cut at the window's last row V:4AA3C whatever A4 is."""
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    h, w = max(ys) - min(ys), max(xs) - min(xs)
    if min(ys) > a4: return {}
    if (h >= 3 and w >= 2) or (h == 2 and w >= 3): return None
    if h >= 3 or (h <= 1 and w >= 2): return fm.tiny(pts, lwin, colour, mask)
    if h == 2:                                                      # the block: the exit tests miny + 1 against A4, the choice of the pair (one row, two dots) against V:4AA3C
        y1 = min(ys) + 1
        if y1 > a4: return {}
        return fm.tiny(pts, y1 if y1 >= lwin else 10 ** 6, colour, mask)
    return fm.tiny(pts, a4, colour, mask)


def expect(t, base, colour, mask):
    e = {}
    for (x, y), k in t.items():
        if 0 <= x < 320: e[(x, y)] = colour if k == "set" else ((base & ~mask) | (colour & mask))
    return {k: v for k, v in e.items() if v != base}


def mode_tiny(rnd, n, ball):
    sys.path.insert(0, SG)
    import rigw, fmodel as fm
    bad = cnt = lines = dots = 0
    for it in range(n):
        L = rnd.choice([144, 167, 179]); base = rnd.choice([4, 6, 5, 0, 9, 3]); c = rnd.randint(0, 15); m = rnd.choice([1, 2, 4, 8, 15, 3, 5, 6, 7, 9, 13, 15])
        pts = tiny_poly(rnd, it % 5 == 0)
        if ball:
            # window coordinates 0..319 x 0..179 in; the ball scales them (mulu on words: non-negative), offset by the view step
            xo, yo = rnd.choice([0, 0, 16, -16, 32]), rnd.choice([0, 0, 0, 3, -2])
            win = [(rnd.randint(0, 319) if rnd.random() < .3 else rnd.randint(0, 40) + rnd.choice([0, 100, 300]), rnd.randint(0, 179)) for _ in range(rnd.choice([3, 4, 5]))]
            if rnd.random() < .7:
                bx, by = rnd.randint(0, 300), rnd.randint(0, 160)
                win = [(min(319, bx + rnd.randint(0, rnd.choice([3, 8, 14]))), min(179, by + rnd.randint(0, rnd.choice([3, 8, 20])))) for _ in range(len(win))]
            pts = [((x * 24 >> 8) + 193 + xo, (y * 31 >> 8) + 162 + yo) for x, y in win]
            limit = 199
        else:
            limit = 199
        t = model(fm, pts, limit, L, c, m & 15)
        if t is None: continue
        r = rigw.stage(win=L)
        if ball:
            r.w16(0x4AA40, xo); r.w16(0x4A990, yo)
            r.w16(0x504DE, len(win))
            for i, (x, y) in enumerate(win): r.w16(0x504E0 + 4 * i, x); r.w16(0x504E2 + 4 * i, y)
            r.w16(0x4AA0C, c); r.w16(0x4A79E, m); r.w16(0x4A7A0, m); r.w32(0x4A7A2, 0); r.w8(0x4A85A, 0)
            for k in range(4):
                if (base >> k) & 1: r.poke(rigw.P0 + rigw.PS * k, b"\xff" * 8000)
            r.run(0x34C7C); pn = rigw.pens(r)
        else:
            entry = 0x34CD0 if it % 2 else 0x34CD6
            lim = 199 if entry == 0x34CD0 else L
            t = model(fm, pts, lim, L, c, m & 15)
            if t is None: continue
            pn = run_one(r, rigw, entry, pts, base, c, m, lim)
        got = {(x, y): pn[y][x] for y in range(200) for x in range(320) if pn[y][x] != base}
        exp = expect(t, base, c, m & 15)
        cnt += 1
        if any(v == "set" for v in t.values()): lines += 1
        else: dots += 1
        if got != exp:
            bad += 1
            if bad <= 5: print("MISMATCH", pts, "base", base, "colour", c, "mask", m, "got", sorted(got.items())[:8], "exp", sorted(exp.items())[:8])
    print("filler_check %s: %d compared (%d with line pixels, %d dots only), %d mismatches" % ("ball" if ball else "tiny", cnt, lines, dots, bad))
    return 1 if bad else 0


def mode_state():
    from common import Rig, load_scene, inputs_missing
    if inputs_missing(): print("skipped: missing", inputs_missing()); return 0
    r = Rig()
    chain = [0x35238, 0x3548A, 0x35CF2, 0x37D78, 0x35BF6, 0x35A4C, 0x3697A, 0x36A10, 0x36C3C, 0x36ADE, 0x36BEE, 0x353EC]
    seen = {}
    for tag in ("air", "deck"):
        for ph in range(32):
            load_scene(r, tag)
            r.w16(0x4A992, ph); r.w8(0x4A8EF, 0)
            r.w16(0x4AA0C, 0x1234); r.poke(0x4A79E, struct.pack(">4H", 0x77, 0x7777, 0x66, 0x6666))
            for a in chain: r.run(a)
            key = (r.u16(0x4AA0C), r.u16(0x4A79E), r.u16(0x4A7A0))
            seen[key] = seen.get(key, 0) + 1
    print("state at the ball call (colour, mask, second word) over 2 scenes x 32 frame counters:", seen)
    return 0 if list(seen) == [(3, 15, 0xFFFF)] else 1


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "tiny"
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 300
    if mode == "state": sys.exit(mode_state())
    img = os.path.join(ROOT, "extracted", "interceptor.bin")
    if not (os.path.exists(img) and os.path.exists(os.path.join(ROOT, "build", "sky-ground", "librigblit.so"))):
        print("skipped: needs extracted/interceptor.bin and build/sky-ground/librigblit.so"); sys.exit(0)
    sys.exit(mode_tiny(random.Random(seed), n, mode == "ball"))
