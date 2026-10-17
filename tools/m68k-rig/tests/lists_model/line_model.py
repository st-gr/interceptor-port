"""Word-exact model of the clipped line routine 0x33936 (docs/re/03-3d-pipeline.md, "The clipped line routine") and its check against the rig.

    python3 line_model.py [SEED] [N]

`clip_line(p1, p2)` returns None (nothing drawn, the routine returns 0) or (x1, y1, x2, y2), the four screen words handed to the line routine 0x3455E.
The check puts N random segments at 0x5163E, runs the original routine with 0x3455E replaced by a stub that logs D0-D3, and compares the return and
the four words.  Segments: random words of mixed size, camera-space points in all regions of the pyramid, points on the planes, and large words.
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
import primlog as P


def w16(x):
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def tdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def mul_div(a, b, c):
    """T(a * b / c): the product of two signed words (32 bits) divided by the signed word c, truncated toward zero; when the quotient does not fit
    a signed word, the result is the low word of the 32-bit product instead (taken as a signed word by the caller's w16)"""
    p = w16(a) * w16(b)
    c = w16(c)
    if c == 0: raise ZeroDivisionError      # the original tests the word difference for zero and stays in a one-instruction loop (see den_zero_check)
    q = tdiv(p, c)
    return q if -32768 <= q <= 32767 else p


# plane index: 0 = +x (z = x), 1 = -x (z = -x), 2 = +y, 3 = -y; the four intersection routines 0x33BB2, 0x33BE0, 0x33C14, 0x33C42
def intersect(plane, p1, p2):
    """hit point (x, y, z) of the segment with the plane, computed from the end point p2 toward p1, and whether it is OUTSIDE the closed pyramid (z < 0 or any |x|, |y| > z)"""
    ax = 0 if plane < 2 else 1          # the coordinate that lies on the plane (x for planes 0, 1)
    s = 1 if plane in (0, 2) else -1    # sign of the plane
    m1 = w16(p1[2] - s * p1[ax]) if s == 1 else w16(p1[2] + p1[ax])
    m2 = w16(p2[2] - p2[ax]) if s == 1 else w16(p2[2] + p2[ax])
    den = w16(m2 - m1)
    other = 1 - ax
    t_o = mul_div(m2, w16(p2[other] - p1[other]), den)
    t_z = mul_div(m2, w16(p2[2] - p1[2]), den)
    o = w16(p2[other] - t_o); z = w16(p2[2] - t_z)
    a = z if s == 1 else w16(-z)
    hit = [0, 0, z]
    hit[ax] = a; hit[other] = o
    x, y = hit[0], hit[1]
    outside = z < 0 or x > z or w16(-x) > z or y > z or w16(-y) > z
    return tuple(hit), outside


intersect_ref = [intersect]


def project(p):
    x, y, z = p
    if z <= 0: return None
    sx = w16(mul_div(x, 0xA0, z) + 0xA0)
    sx = 0 if sx < 0 else 0x13F if sx >= 0x140 else sx
    sy = w16(mul_div(y, 0x5A, z) + 0x5A)
    sy = 0 if sy < 0 else 0xB3 if sy >= 0xB4 else sy
    return w16(0x13F - sx), w16(0xB3 - sy)


def clip_end(p1, p2):
    """one end point against the other raw end: returns the point to project (p1 itself, a hit) or None (reject)"""
    x1, y1, z1 = p1; x2, y2, z2 = p2
    XP, XM, YP, YM = 0, 1, 2, 3
    hit = [None]

    def tryp(plane):
        h, out = intersect(plane, p1, p2); hit[0] = h
        return not out

    def zh(): return hit[0][2]
    USE, REJ = 'use', 'rej'
    # --- tree; each block follows the original's order
    def y_tests_after_x():       # 0x339B6
        if y1 < 0:
            if w16(-y1) < z1: return REJ
            if z2 <= w16(-y2): return REJ
            if tryp(YM): return USE
            if zh() >= 0: return REJ
            if y1 < z1: return REJ
            if z2 <= y2: return REJ
            return USE if tryp(YP) else REJ
        if y1 < z1: return REJ
        if z2 <= y2: return REJ
        if tryp(YP): return USE
        if zh() >= 0: return REJ
        if w16(-y1) < z1: return REJ
        if z2 <= w16(-y2): return REJ
        return USE if tryp(YM) else REJ
    if x1 >= z1:
        if z2 <= x2: return None
        if tryp(XP): return hit[0]
        if zh() < 0 and not z2 <= w16(-x2):
            if tryp(XM): return hit[0]
        r = y_tests_after_x()
    elif w16(-x1) >= z1:
        if z2 <= w16(-x2): return None
        if tryp(XM): return hit[0]
        if zh() < 0 and not z2 <= x2:
            if tryp(XP): return hit[0]
        r = y_tests_after_x()
    else:
        if y1 >= z1:                                    # 0x33A48
            if z2 <= y2: return None
            if tryp(YP): return hit[0]
            if zh() < 0 and not z2 <= w16(-y2):
                if tryp(YM): return hit[0]
            r = 'x_after_y'
        elif w16(-y1) >= z1:                            # 0x33A7E
            if z2 <= w16(-y2): return None
            if tryp(YM): return hit[0]
            if zh() < 0 and not z2 <= y2:
                if tryp(YP): return hit[0]
            r = 'x_after_y'
        else:
            return p1 if z1 >= 0 else None              # 0x33B1C
        if x1 < 0:                                      # 0x33AAA
            if w16(-x1) < z1: return None
            if z2 <= w16(-x2): return None
            if tryp(XM): return hit[0]
            if zh() >= 0: return None
            if x1 < z1: return None
            if z2 <= x2: return None
            return hit[0] if tryp(XP) else None
        if x1 < z1: return None
        if z2 <= x2: return None
        if tryp(XP): return hit[0]
        if zh() >= 0: return None
        if w16(-x1) < z1: return None
        if z2 <= w16(-x2): return None
        return hit[0] if tryp(XM) else None
    return hit[0] if r == USE else None


DEPTH_ERROR = [False]      # set by clip_line when a used point had a depth that is not positive (the original stores 0x16 in V:4AA56)


def clip_line(p1, p2):
    DEPTH_ERROR[0] = False
    pts = []
    for a, b in ((p1, p2), (p2, p1)):
        h = clip_end(a, b)
        if h is None: return None
        pr = project(h)
        if pr is None: DEPTH_ERROR[0] = True; return None
        pts.append(pr)
    return pts[0] + pts[1]


def stub_line_routine():
    c = P.w(0x40E7, 0x2F08)
    c += P.append_header(9, None, [(2, ("reg", 0)), (4, ("reg", 1)), (6, ("reg", 2)), (8, ("reg", 3))])
    c += P.w(0x205F, 0x44DF, 0x4E75)
    return c


def gen(rng):
    kind = rng.choice(('mixed', 'edge', 'inside', 'behind', 'big', 'plane', 'small'))
    def pt():
        if kind == 'inside':
            z = rng.randint(20, 3000); return (rng.randint(-z, z), rng.randint(-z, z), z)
        if kind == 'behind':
            return (rng.randint(-3000, 3000), rng.randint(-3000, 3000), rng.randint(-3000, 3000))
        if kind == 'big':
            return tuple(rng.randint(-32768, 32767) for _ in range(3))
        if kind == 'plane':
            z = rng.randint(-100, 3000); return (rng.choice((z, -z, rng.randint(-z, z) if z > 0 else 0)), rng.choice((z, -z, rng.randint(-abs(z), abs(z)))), z)
        if kind == 'small':
            return (rng.randint(-6, 6), rng.randint(-6, 6), rng.randint(-6, 6))
        z = rng.randint(-200, 2000); return (rng.randint(-2 * abs(z) - 5, 2 * abs(z) + 5), rng.randint(-2 * abs(z) - 5, 2 * abs(z) + 5), z)
    return pt(), pt()


def den_zero_cases(rng, n):
    """segments whose intersection difference den (a word) is zero for the plane that the model reaches: end points of full-range words, the second end
    point's margin forced congruent (mod 65536) to the first's for a random plane"""
    for _ in range(n):
        e = tuple(rng.randint(-32768, 32767) for _ in range(3)); f = list(rng.randint(-32768, 32767) for _ in range(3))
        plane = rng.randint(0, 3); ax = 0 if plane < 2 else 1; sgn = 1 if plane in (0, 2) else -1
        f[ax] = w16(sgn * (f[2] - w16(e[2] - sgn * e[ax]))); yield e, tuple(f)


def den_zero_check(r, rng, n):
    """the original at den = 0: the model says (ZeroDivisionError) which segments reach a zero den; the rig must stay in a loop at the test (the run hits the
    instruction cap, RIG_ERR_TIMEOUT) with no line drawn; the segments the model draws instead are drawn by the rig as well"""
    import collections, ctypes
    cls = collections.Counter(); bad = 0
    orig = intersect_ref[0]; seen = []
    def spy(plane, p1, p2):
        seen.append(plane); return orig(plane, p1, p2)
    globals()['intersect'] = spy
    try:
        for e, f in den_zero_cases(rng, n):
            seen.clear()
            try:
                exp = clip_line(e, f); hang = False
            except ZeroDivisionError:
                hang = True
            if not hang: continue
            where = 'first plane tried' if len(seen) == 1 else 'a later plane (the opposite plane or the y stage)'
            r.poke(0x5163E, b"".join(struct.pack(">3h", *p) for p in (e, f)))
            P.clear_log(r); r.L.rig_set_max_instructions(20000)
            rc = r.run(0x33936, a6=0x7F0200, a7=0x7F8000)
            log = [x for x in P.read_log(r) if x[0] == 9]
            ok = rc == -1 and ctypes.c_int.in_dll(r.L, 'rig_last_error').value == 1 and not log
            cls[where, ok] += 1; bad += not ok
    finally:
        globals()['intersect'] = orig
    return cls, bad


def den_zero_radius(rng, per_plane):
    """how large the coordinates must be for den = 0 (model only; the rig agrees on the full-range segments above): end points with every coordinate within +-R, the second
    end point's margin for a random plane set congruent (mod 65536) to the first's; counts of segments the model sends into the zero divide, by R, for the first plane tried
    and for a later plane"""
    import collections
    res = {}
    orig = intersect_ref[0]; seen = []
    def spy(plane, p1, p2):
        seen.append(plane); return orig(plane, p1, p2)
    globals()['intersect'] = spy
    try:
        for R in (8000, 11000, 12000, 16383, 16384, 32767):
            c = collections.Counter()
            for plane in range(4):
                ax = 0 if plane < 2 else 1; sgn = 1 if plane in (0, 2) else -1
                for _ in range(per_plane):
                    e = tuple(rng.randint(-R, R) for _ in range(3)); f = [rng.randint(-R, R) for _ in range(3)]
                    f[ax] = w16(sgn * (f[2] - w16(e[2] - sgn * e[ax])))
                    if abs(f[ax]) > R: continue
                    seen.clear()
                    try: clip_end(e, tuple(f))
                    except ZeroDivisionError: c['first' if len(seen) == 1 else 'later'] += 1
            res[R] = dict(c)
    finally:
        globals()['intersect'] = orig
    return res


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 20000
    r = Rig(); r.reset(); rng = random.Random(seed)
    P.clear_log(r)
    stub = 0x8F0000
    r.poke(stub, stub_line_routine()); r.poke(0x3455E, P.w(0x4EF9) + P.l(stub))
    bad = drawn = hangs = 0; cnt_err = {True: 0, False: 0}
    for _ in range(n):
        p1, p2 = gen(rng)
        r.poke(0x5163E, b"".join(struct.pack(">3h", *p) for p in (p1, p2)))
        P.clear_log(r)
        r.L.rig_set_max_instructions(20000)
        r.w16(0x4AA56, 0xABCD)
        r.run(0x33936, a6=0x7F0200, a7=0x7F8000)
        errw = r.u16(0x4AA56)
        log = [x for x in P.read_log(r) if x[0] == 9]
        d0 = r.reg('d0') & 0xFFFF
        got = tuple(P.s16(v) for v in log[0][1:5]) if log else None
        try:
            exp = clip_line(p1, p2)
        except ZeroDivisionError:
            hangs += 1; continue
        drawn += got is not None
        cnt_err[DEPTH_ERROR[0]] += 1
        if errw != (0x16 if DEPTH_ERROR[0] else 0xABCD):
            bad += 1
            if bad < 8: print('DIFF error word', p1, p2, hex(errw), DEPTH_ERROR[0])
        if got != exp or d0 != (1 if got else 0):
            bad += 1
            if bad < 8: print('DIFF', p1, p2, 'rig', got, d0, 'model', exp)
    print('error word V:4AA56: %d segments rejected by a non-positive depth (word 0x16 stored), %d others (word untouched)' % (cnt_err[True], cnt_err[False]))
    print('line_model: %d segments, %d drawn, %d model-divide-by-zero skipped, %s' % (n, drawn, hangs, 'FAILED %d differences' % bad if bad else 'ok'))
    cls, hbad = den_zero_check(r, random.Random(seed + 1), max(n, 2000))
    for (where, ok), k in sorted(cls.items()):
        print('den = 0, %s: %d segments, the rig %s' % (where, k, 'stays in the loop and draws nothing' if ok else 'DOES NOT HANG'))
    rad = den_zero_radius(random.Random(seed + 2), max(n, 20000) * 4)
    for R, c in rad.items(): print('den = 0 survey, coordinates within +-%d: %s' % (R, c or 'none'))
    print('den_zero_check: %d segments with den = 0, %s' % (sum(cls.values()), 'FAILED %d' % hbad if hbad else 'ok'))
    return 1 if bad or hbad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
