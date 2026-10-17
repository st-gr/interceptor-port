#!/usr/bin/env python3
"""Differential checks of the models in this directory against the game's code run in the rig (needs extracted/interceptor.bin and
build/sky-ground/librigblit.so, see build.sh):

    python3 tools/vamiga-ref/sky-ground/check_rig.py polygon 1 1500    # ground-polygon model (gmodel.py) vs 0x32E5A, random camera states
    python3 tools/vamiga-ref/sky-ground/check_rig.py fill 1 300        # fill model (fmodel.py) vs the sky fill 0x3486C, pen map of the four planes
    python3 tools/vamiga-ref/sky-ground/check_rig.py zones 1 4000      # sea-zone lookup model (zones.py) vs 0x230F8
    python3 tools/vamiga-ref/sky-ground/check_rig.py tiny 1 400        # point and line cases of the filler (fmodel.tiny) vs 0x34CD6
    python3 tools/vamiga-ref/sky-ground/check_rig.py flag 1 3000       # side flag (bit 1 of V:4A982) rule of note 04 vs 0x143F2

Arguments after the mode: the random seed and the number of states. Prints the mismatches and a summary line; exit status 1 on any mismatch."""
import os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rigw, gmodel as gm, fmodel as fm, zones


def random_state(rnd):
    pitch = rnd.choice([rnd.randint(0, 28799), rnd.randint(-1500, 1500) % 28800, rnd.randint(13000, 16000) % 28800])
    roll = rnd.choice([0, rnd.randint(0, 28799), rnd.randint(-3000, 3000) % 28800])
    alt = -rnd.choice([rnd.randint(0, 1 << 20), rnd.randint(0, 1 << 23), rnd.randint(0, 1 << 27)])
    return dict(pitch=pitch, heading=rnd.randint(0, 28799), roll=roll, alt_long=alt, f982=rnd.randint(0, 1) * 2)


def mode_polygon(rnd, n):
    bad = 0
    kinds = {}
    for it in range(n):
        s = random_state(rnd)
        view = rnd.choice([0, 0, 1])                       # V:4A83D: 0 cockpit, non-zero an external camera
        cpitch = rnd.randint(0, 28799)                     # V:4AB4C, the pitch word the whole-window test reads in the other views
        r = rigw.stage(view=view, **s)
        r.w16(0x4AB4A, s["roll"])
        r.w16(0x4AB4C, cpitch)
        M = [r.s16(0x4AC90 + 2 * i) for i in range(9)]
        r.run(0x32E5A)
        got = rigw.polygon(r) if r.reg("d0") == 0 else None
        try:
            mp = gm.polygon(gm.corners(M, s["alt_long"]), s["f982"] >> 1, s["roll"], view == 0, s["pitch"] if view == 0 else cpitch)
        except OverflowError:
            kinds["overflow"] = kinds.get("overflow", 0) + 1
            continue
        if mp == "FULL":
            mp = [(0, 0), (319, 0), (319, 179), (0, 179)]
        kinds[len(mp) if mp else "none"] = kinds.get(len(mp) if mp else "none", 0) + 1
        if got != mp:
            bad += 1
            if bad <= 5: print("MISMATCH", s, view, cpitch, "rig", got, "model", mp)
    return bad, kinds


def mode_fill(rnd, n):
    bad = 0
    kinds = {}
    for it in range(n):
        s = random_state(rnd)
        win = rnd.choice([144, 167, 179])
        f953 = rnd.choice([0, 1])                                   # the base-colour flag: 0 land (pen 4), 1 sea zone (pen 6 and plane 1 cleared under the sky)
        r = rigw.stage(win=win, f953=f953, **s)
        r.w16(0x4AB4A, s["roll"])
        M = [r.s16(0x4AC90 + 2 * i) for i in range(9)]
        r.run(0x3486C)
        pn = rigw.pens(r)
        try:
            mp = gm.polygon(gm.corners(M, s["alt_long"]), s["f982"] >> 1, s["roll"], True, s["pitch"])
        except OverflowError:
            continue
        if mp == "FULL":
            mp = [(0, 0), (319, 0), (319, 179), (0, 179)]
        if mp is None:
            st, plane = "none", [[0] * 320 for _ in range(200)]
        else:
            st, plane = fm.fillpoly(mp, win)
        kinds[st] = kinds.get(st, 0) + 1
        if st == "tiny":
            continue
        for y in range(win + 2):
            for x in range(320):
                exp = ((4 | plane[y][x]) if not f953 else (6 if not plane[y][x] else 5)) if 1 <= y <= win else 0     # land: plane 2 set, plane 0 by the polygon; sea: planes 2 and 1 set, the polygon sets plane 0 and clears plane 1
                if pn[y][x] != exp:
                    bad += 1
                    print("MISMATCH", s, win, mp, "at", x, y, pn[y][x], exp)
                    break
            else:
                continue
            break
    return bad, kinds


def side_flag(pitch, roll):
    """Rule of note 04 for bit 1 of V:4A982: pitch and roll words 0..28799 (28800 units per circle), divided by 8."""
    p, r = pitch // 8, roll // 8
    if 900 <= p < 2700:
        return int(not (900 <= r < 2700))
    return int(900 <= r <= 2700)


def mode_flag(rnd, n):
    bad = 0
    r = rigw.stage()
    for it in range(n):
        pitch = rnd.choice([rnd.randint(0, 28799), rnd.choice([900, 2700, 7200, 21600, 7199, 21599, 21601, 7201]) ])
        roll = rnd.choice([rnd.randint(0, 28799), rnd.choice([900, 2700, 7200, 21600, 7199, 21599, 21601, 7201])])
        ext = rnd.random() < 0.3
        r.w8(0x4A83D, 1 if ext else 0)
        r.w32(0x4AB40, pitch); r.w32(0x4AB48, roll); r.w16(0x4AB4C, pitch)
        r.w16(0x4A982, rnd.choice([0, 2, 0x100, 0x102]))
        r.run(0x143F2)
        got = (r.u16(0x4A982) >> 1) & 1
        exp = side_flag(pitch, 0 if ext else roll)
        if got != exp:
            bad += 1
            if bad <= 5: print("MISMATCH", pitch, roll, ext, got, exp)
    return bad, {}


def mode_zones(rnd, n):
    bad = 0
    r = rigw.stage()
    # all (row, column) keys of the table
    a = 0x23188
    rows = []
    while zones.w(a) >= 0:
        rows.append(zones.w(a)); a += 2
    keys = []
    for i, rz in enumerate(rows):
        b = 0x23188 + zones.w(a + 2 + 2 * i)
        while zones.w(b) >= 0:
            keys.append((rz, zones.w(b))); b += 2
    hits = 0
    for it in range(n):
        cz, cx = rnd.choice(keys) if rnd.random() < 0.7 else (rnd.randint(0, 31), rnd.randint(0, 31))
        alt = rnd.choice([rnd.randint(-100, 5000), rnd.randint(0, 200000), rnd.randint(0, 40000)])
        ph, oc = rnd.randint(0, 15), rnd.randint(0, 7)
        r.w16(0x4AA02, cz); r.w16(0x4AA00, cx); r.w32(0x4AB30, -alt); r.w8(0x4A908, ph); r.w8(0x4A90C, oc)
        r.run(0x230F8, a0=0x23188)
        got = r.reg("d0") & 0xFF
        exp = zones.lookup(0x23188, cz, cx, alt, ph, oc)
        hits += exp
        if got != exp:
            bad += 1
            if bad <= 5: print("MISMATCH", cz, cx, alt, ph, oc, got, exp)
    return bad, {"hits": hits}


def mode_tiny(rnd, n):
    """fmodel.tiny (the point and line cases of 0x34CD6) vs the rig: random small boxes at corners and edges, random stale colour V:4AA0C and mask V:4A79E, random base pen."""
    bad = cnt = 0
    for it in range(n):
        L = rnd.choice([144, 167, 179]); b = rnd.choice([4, 6, 5, 0, 9]); c = rnd.randint(0, 15); m = rnd.choice([1, 2, 4, 8, 15, 3, 5, 6, 7, 9, 13])
        x0 = rnd.choice([0, 1, 2, 15, 16, 17, 100, 317, 318, 319]); y0 = rnd.choice([0, 1, 2, L - 4, L - 3, L - 2, L - 1, L, L + 1])
        w = rnd.randint(0, 4); h = rnd.randint(0, 5)
        pts = [(min(319, x0 + rnd.randint(0, w)), y0 + rnd.randint(0, h)) for _ in range(rnd.choice([3, 4]))]
        t = fm.tiny(pts, L, c, m)
        if t is None:
            continue
        r = rigw.stage(win=L)
        for k in range(4):
            if (b >> k) & 1: r.poke(rigw.P0 + rigw.PS * k, b"\xff" * 8000)
        r.w16(0x4AA0C, c); r.w16(0x4A79E, m); r.w16(0x4A7A0, m); r.w32(0x4A7A2, 0)
        r.w16(0x5043C, len(pts))
        for i, (x, y) in enumerate(pts): r.w16(0x5043E + 4 * i, x); r.w16(0x50440 + 4 * i, y)
        r.run(0x34CD6); pn = rigw.pens(r)
        exp = {}
        for (x, y), k in t.items():
            if 0 <= x < 320: exp[(x, y)] = c if k == "set" else ((b & ~m) | (c & m))
        got = {(x, y): pn[y][x] for y in range(200) for x in range(320) if pn[y][x] != b}
        exp = {k: v for k, v in exp.items() if v != b}
        cnt += 1
        if got != exp:
            bad += 1
            if bad <= 5: print("MISMATCH", pts, L, b, c, m, sorted(got.items())[:8], sorted(exp.items())[:8])
    return bad, {"compared": cnt}


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "polygon"
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1            # default seed 1, 500 states (a few hundred per process: each rig takes 16 MiB)
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 500
    bad, info = {"polygon": mode_polygon, "fill": mode_fill, "zones": mode_zones, "flag": mode_flag, "tiny": mode_tiny}[mode](random.Random(seed), n)
    print(mode, "bad", bad, "of", n, info)
    sys.exit(1 if bad else 0)
