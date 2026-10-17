"""View cameras of the original (docs/re/03-3d-pipeline.md "Camera matrix", docs/re/06-cockpit-hud.md "Head views" and "View kinds, settled").

    python3 view_check.py [SEED] [N]

Four checks, each on random states of the player record (the table at 0x4B23C, record 0), run on the original code in the rig:

  head   0x32074 in the cockpit (V:4A83D = 0): for the head codes V:4A85F 0 to 13 and the classes 0x10, 0x11, 0x14, 0x20, 0x30 the camera words V:4AB42, V:4AB46, V:4AB4A
         equal the output of the composition 0x325B8 fed with the head angle triple of the rule (code 0: the record's own angles; codes 1 to 11: yaw T[code] of the table 0x3239A
         (class 0x30: the triples of the table 0x323DC and a twelfth for code 0); codes 12 and 13: pitch 0x5690/0x19F0 within 7 degrees of level (record pitch below 560 or at
         least 28240), else 0x5460/0x1C20; class 0x30: 0x3610/0x3840 and 0x230/0 by the vertical test) and the attitude matrix at record +0x80;
  ring   0x2D756 in an external view (V:4A83D = -1, 1 to 4): the camera longs V:4ACF6/FA/FE equal the record position plus ((matrix times the ring words) >> 4), the words being the ring
         table of the class at V:4A96A scaled by the level (1: w + (w >> 1); 2, 3, 4: w << 1, 2, 3; -1: unchanged), the matrix the nine words at +0x92 for the ring directions 0
         and 4 of a non-0x30 class and the heading-only matrix otherwise, and the height raised to (deck + 7) * 256 (deck = word +0x4E, read only when byte +4 has bit 6 or 7);
         plus the ring rotation by 4 when the heading word moves by 7200 to 21600 in one frame (V:4A9E2);
  aim    0x1454a (the aim routine): the new pitch and heading words equal the exact target when the limit is negative, else the previous angle plus the step
         min(|difference| >> 2, limit) toward the target along the shorter way round 28800 (pitch and heading separately);
  target 0x1454A with a negative limit: the exact target angles of the rule in note 03 ("External camera aim"): the sign flags and magnitudes of the camera-to-target vector, the scale pair
         (S, T) by the largest magnitude, the heading through the ratio table at 0x42C00 (index = rounded quotient >> 6) and the pitch through the same table (index = rounded quotient)
         of the vector's horizontal length (the Newton square root 0x29B12), the quadrant fixes;
  caller 0x32074 external branch: the limit is 2000 (V:4A966 zero, the ring key V:4A996 | V:4A96A equal to V:4AB18, V:4A85E nonzero), 560 (V:4A966 nonzero), the aim snaps when V:4A85E
         and V:4A966 are zero (first frame), when the ring key changed, or when V:4A86D is zero; nothing happens while V:4A866 is nonzero.

  keys   the key handler 0x1DAA0 in the cockpit: the head code after KP8, KP9, KP2, KP7, KP1, KP4 and KP6 from every code (KP4: code - 1, with 11 from 0 and 0 from 1 or from 12 and 13; KP6: code + 1, 0 from
         11, 12 and 13), the hold byte V:4A949 = 0xFF on every head key, the list request V:4A910 = 0xFF on every head key except KP5, the cockpit zoom words by `[` and `]`, KP-Enter (request bit
         V:4A97E bit 1 set once however often it is pressed, V:4A96A = 4, V:4A8EB = 0xFF, V:4A865 = 0) and M (map flag, camera 7200 and 0, altitude V:4A71C).

Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
"""
import ctypes, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH

E = 0x4B23C
TAB = (2256, 4800, 7200, 9600, 12000, 14400, 16800, 19200, 21600, 24000, 26544)
T30 = [(24000, 7200, 21600), (26400, 7200, 21600), (0, 7200, 21600), (2400, 7200, 21600), (4800, 7200, 21600), (7200, 7200, 21600), (9600, 7200, 21600),
       (12000, 7200, 21600), (14400, 7200, 21600), (16800, 7200, 21600), (19200, 7200, 21600), (21600, 0, 0)]
RING = {0x11: 0x2D6A0, 0x14: 0x2D6DC, 0x30: 0x2D718}


def w16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def words(r, a, n):
    return [r.s16(a + 2 * i) for i in range(n)]


def attitude(r, cls, pitch, head, roll):
    r.w8(E + 0x62, cls)
    r.run(0x3202C, a1=E, d4=pitch, d5=head, d6=roll)       # stores +0x66..+0x6A, the matrices at +0x80 and +0x92


def head_triple(code, cls, pitch):
    if cls & 0xF0 == 0x30:
        if code <= 11:
            return T30[(code - 1) if code else 11]
        near = 6640 < pitch < 7760
        return ((0x3610 if near else 0x3840) if code == 12 else (0x230 if near else 0), 0, 0)
    if code == 0:
        return None
    if code <= 11:
        return (0, TAB[code - 1], 0)
    level = pitch < 560 or pitch >= 28240
    return ((0x5690 if level else 0x5460) if code == 12 else (0x19F0 if level else 0x1C20), 0, 0)


def check_head(r, rng, n):
    bad = 0
    for _ in range(n):
        r.reset()
        cls = rng.choice((0x11, 0x11, 0x10, 0x14, 0x30, 0x20))
        pitch = rng.choice((rng.randrange(28800), rng.randrange(0, 40), 559, 560, 28239, 28240, 6640, 6641, 7759, 7760, rng.randrange(6000, 8000)))
        head, roll = rng.randrange(28800), rng.choice((0, rng.randrange(28800)))
        code = rng.randrange(14)
        attitude(r, cls, pitch, head, roll)
        r.w8(0x4A83D, 0); r.w8(0x4A85F, code); r.w8(0x4A869, 0)
        r.run(0x32074)
        got = (r.s16(0x4AB42), r.s16(0x4AB46), r.s16(0x4AB4A))
        t = head_triple(code, cls, pitch)
        if t is None:
            exp = (pitch, head, roll)
        else:
            r.run(0x325B8, d0=t[0], d2=t[1], d4=t[2], a4=E + 0x80)
            exp = tuple(w16(r.reg(x)) for x in ('d4', 'd5', 'd6'))
        if got != exp:
            bad += 1
            if bad < 6: print('head DIFF', hex(cls), pitch, head, roll, code, got, exp)
    print('head   : %d states, %d differences' % (n, bad)); return bad


def check_ring(r, rng, n):
    bad = 0
    for _ in range(n):
        r.reset()
        cls = rng.choice((0x11, 0x11, 0x10, 0x14, 0x30, 0x20, 0x12))
        head = rng.randrange(28800)
        attitude(r, cls, rng.randrange(28800), head, rng.choice((0, rng.randrange(28800))))
        for off in (0x14, 0x18, 0x1C): r.w32(E + off, rng.randint(-(1 << 29), 1 << 29))
        flag4 = rng.choice((0, 0x40, 0x80, 0xC0)); r.w8(E + 4, flag4); r.w16(E + 0x4E, rng.randrange(0, 300))
        view = rng.choice((0xFF, 1, 2, 3, 4)); d = rng.randrange(10)
        r.w8(0x4A83D, view); r.w8(0x4A86D, 1); r.w8(0x4A866, 0); r.w8(0x4A865, 0); r.w8(0x4A966, rng.choice((0, 0, 5))); r.w8(0x4A96A, d)
        r.w16(0x4A996, 0); r.w16(0x4A9E2, head)
        r.run(0x2D756)
        got = [s32(r.s32(0x4ACF6 + 4 * i)) for i in range(3)]
        base = 0x2D718 if cls & 0xF0 == 0x30 else RING.get(cls, 0x2D664)
        w = [r.s16(base + 6 * d + 2 * j) for j in range(3)]
        if view < 0x80:
            w = [w16(x + (x >> 1)) for x in w] if view == 1 else [w16(x << (view - 1)) for x in w] if view > 1 else w
        if cls & 0xF0 == 0x30 or d not in (0, 4):
            r.run(0x32A48, a1=0x4AD40, d4=((28800 - head) if head else 0)); m = words(r, 0x4AD40, 9)
        else:
            m = words(r, E + 0x92, 9)
        out = [sum(m[3 * i + j] * w[j] for j in range(3)) >> 4 for i in range(3)]
        pos = [r.s32(E + 0x14 + 4 * i) for i in range(3)]
        exp = [s32(pos[i] + out[i]) for i in range(3)]
        low = ((r.s16(E + 0x4E) if flag4 & 0xC0 else 0) + 7) << 8
        if exp[1] < low: exp[1] = low
        if got != exp:
            bad += 1
            if bad < 6: print('ring DIFF', hex(cls), d, view, got, exp)
    # the ring rotation on a heading jump
    jump = 0
    for old, new, d, want in ((0, 7200, 1, 5), (0, 7199, 1, 1), (0, 21600, 2, 6), (0, 21601, 2, 2), (0, 14400, 4, 4), (0, 14400, 0, 0), (0, 14400, 5, 1),
                              (0, 14400, 7, 3), (0, 14400, 8, 8), (0, 14400, 9, 9), (5000, 26000, 6, 2), (26000, 5000, 3, 7)):
        r.reset(); attitude(r, 0x11, 0, new, 0)
        r.w8(0x4A83D, 0xFF); r.w8(0x4A86D, 1); r.w8(0x4A96A, d); r.w16(0x4A996, 0); r.w16(0x4A9E2, old)
        r.run(0x2D756)
        if r.u8(0x4A96A) != want or r.u16(0x4A9E2) != new: jump += 1
    print('ring   : %d states, %d differences; ring rotation on a heading jump: 12 cases, %d differences' % (n, bad, jump)); return bad + jump


def step(cur, tgt, lim):
    d = tgt - cur
    if d < 0:
        if d >= -14400:
            return cur - min((-d) >> 2, lim)
        nn = cur + min((d + 28800) >> 2, lim)
        return nn - 28800 if nn >= 28800 else nn
    if d > 14400:
        nn = cur - min((28800 - d) >> 2, lim)
        return nn + 28800 if nn < 0 else nn
    return cur + min(d >> 2, lim)


def check_aim(r, rng, n):
    L = r.L; L.rig_call.restype = ctypes.c_uint32; L.rig_call.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]

    def aim(cp, cy, dx, dy, dz, lim, h85e=0, k966=0):
        r.w8(0x4A85E, h85e); r.w8(0x4A966, k966)
        arr = (ctypes.c_uint32 * 6)(*[x & 0xFFFFFFFF for x in (cp, cy, dx, dy, dz, lim)])
        L.rig_call(0x1454A, arr, 6)
        return (r.u16(0x4AB78), r.u16(0x4AB7A))
    r.reset(); bad = 0
    for _ in range(n):
        mag = rng.choice((1 << 12, 1 << 16, 1 << 20, 1 << 22, 1 << 24, 1 << 26, 1 << 28, 1 << 29))
        dx, dy, dz = [rng.randint(-mag, mag) for _ in range(3)]
        cp, cy = rng.randrange(28800), rng.randrange(28800)
        lim = rng.choice((2000, 560, 100, 3000))
        tp, ty = aim(cp, cy, dx, dy, dz, -1)
        got = aim(cp, cy, dx, dy, dz, lim, h85e=1)
        if got != (step(cp, tp, lim) % 65536, step(cy, ty, lim) % 65536): bad += 1
    snap = 0
    for _ in range(n // 6):
        dx, dy, dz = [rng.randint(-(1 << 24), 1 << 24) for _ in range(3)]
        cp, cy = rng.randrange(28800), rng.randrange(28800)
        t = aim(cp, cy, dx, dy, dz, -1)
        if aim(cp, cy, dx, dy, dz, 2000, h85e=0, k966=0) != t or r.u8(0x4A85E) != 1: snap += 1
    print('aim    : %d states, %d differences; first-frame snap (V:4A85E = 0): %d cases, %d differences' % (n, bad, n // 6, snap)); return bad + snap


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def divs(d0, d1):
    """68000 DIVS.W of the long d0 by the word d1: the new d0 (quotient low word, remainder high word); an overflow leaves d0."""
    a, b = s32(d0), s16(d1)
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0): q = -q
    if q > 32767 or q < -32768: return d0 & 0xFFFFFFFF
    return (((a - q * b) & 0xFFFF) << 16) | (q & 0xFFFF)


def div_round(d0, d1):
    """0x29E44: the word quotient of the long by the word, moved one away from 0 when the remainder reaches half the divisor."""
    half = abs(s16(d1)) >> 1
    x = divs(d0, d1)
    if half > abs(s16(x >> 16)): return s16(x)
    q = s16(x)
    return s16(q - 1) if q < 0 else s16(q + 1)


def isqrt(v):
    """0x29B12: Newton iteration from v / 200 + 2, stopping when the quotient is within 1 of the guess; large values are scaled down by 16 or 256 and the root up by 4 or 16."""
    def loop(v):
        g = ((v // 200) + 2) & 0xFFFF
        while True:
            q = v // g
            d1 = q if q <= 0xFFFF else v & 0xFFFF
            if ((d1 - g) & 0xFFFF) in (0, 1, 0xFFFF): return d1
            g = ((d1 + g) & 0xFFFF) >> 1
    if v <= 0x63F000: return loop(v) & 0xFFFF
    if v <= 0x63F0000: return (loop(v >> 4) << 2) & 0xFFFF
    return (loop(v >> 8) << 4) & 0xFFFF


def aim_target(tab, dx, dy, dz):
    f = 0; ax, ay, az = dx, dy, dz
    if ax < 0: ax = -ax; f |= 1
    if ay < 0: ay = -ay; f |= 2
    if az < 0: az = -az; f |= 4
    m = max(ax, ay, az)
    if m > 0x10000000: ax >>= 3; ay >>= 3; az >>= 3; S, T = 14, 0
    elif m > 0x2000000: S, T = 14, 0
    elif m > 0x800000: S, T = 12, 2
    elif m > 0x200000: S, T = 10, 4
    else: S, T = 8, 6
    xs, zs = s16(ax >> S), s16(az >> S)
    if xs <= zs:
        h = 3600 if zs == 0 else tab[div_round((ax << T) & 0xFFFFFFFF, zs) >> 6] * 8
    else:
        h = (900 - tab[div_round((az << T) & 0xFFFFFFFF, xs) >> 6]) * 8
    k = f & 5
    h = 0x3840 - h if k == 5 else h + 0x3840 if k == 4 else 0x7080 - h if k == 0 else h
    if h >= 0x7080: h = 0
    hor = isqrt(((zs * zs) + (s16(ax >> S) ** 2)) & 0xFFFFFFFF); dyy = s16(ay >> S)
    if dyy <= s16(hor):
        p = 3600 if hor == 0 else tab[div_round((dyy << 8) & 0xFFFFFFFF, hor)] * 8
    else:
        p = (900 - tab[div_round((hor << 8) & 0xFFFFFFFF, dyy)]) * 8
    if not f & 2: p = 0x7080 - p
    if p >= 0x7080: p = 0
    return p, h


def check_target(r, rng, n):
    L = r.L; L.rig_call.restype = ctypes.c_uint32; L.rig_call.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]
    r.reset(); tab = struct.unpack('>1024h', r.peek(0x42C00, 2048)); bad = 0
    for _ in range(n):
        mag = rng.choice((1 << 8, 1 << 12, 1 << 16, 1 << 20, 1 << 22, 1 << 24, 1 << 26, 1 << 28, 1 << 29, 1 << 30))
        dx, dy, dz = [rng.randint(-mag, mag) for _ in range(3)]
        r.w8(0x4A85E, 0); r.w8(0x4A966, 0)
        arr = (ctypes.c_uint32 * 6)(*[x & 0xFFFFFFFF for x in (0, 0, dx, dy, dz, -1)])
        L.rig_call(0x1454A, arr, 6)
        if (r.u16(0x4AB78), r.u16(0x4AB7A)) != tuple(v % 65536 for v in aim_target(tab, dx, dy, dz)): bad += 1
    print('target : %d vectors, %d differences' % (n, bad)); return bad


def check_caller(r, rng, n):
    bad = {}
    for k966 in (0, 5, 6, 2):
        for same in (True, False):
            for h85e in (0, 1):
                for paused in (0, 255):
                    for a86d in (1, 0):
                        for _ in range(n):
                            r.reset()
                            r.w8(0x4A83D, 0xFF); r.w8(0x4A86C, 1); r.w8(0x4A86D, a86d); r.w8(0x4A966, k966); r.w8(0x4A85E, h85e); r.w8(0x4A866, paused)
                            r.w16(0x4A996, 0); r.w8(0x4A96A, rng.randrange(10)); r.w8(E + 0x62, 0x11)
                            attitude(r, 0x11, rng.randrange(28800), rng.randrange(28800), 0)
                            pos = [r.s32(E + 0x14 + 4 * i) for i in range(3)]
                            for i, v in enumerate((pos[0] + rng.randint(-(1 << 24), 1 << 24), pos[1] + rng.randint(0, 1 << 22), pos[2] + rng.randint(-(1 << 24), 1 << 24))):
                                r.w32(0x4ACF6 + 4 * i, v)
                            r.w16(0x4AB4C, rng.randrange(28800)); r.w16(0x4AB4E, rng.randrange(28800))
                            key = r.u8(0x4A96A); r.w16(0x4AB18, key if same else key ^ 0x40)
                            cur = (r.u16(0x4AB4C), r.u16(0x4AB4E))
                            saved = r.peek(0x4A6E8, 0x54EC0 - 0x4A6E8)
                            r.w8(0x4A86C, 0xFF); r.run(0x32074); t = (r.u16(0x4AB4C), r.u16(0x4AB4E))     # negative: snap, the target
                            r.poke(0x4A6E8, saved); r.run(0x32074); g = (r.u16(0x4AB4C), r.u16(0x4AB4E))
                            if paused: exp = cur
                            elif a86d == 0: exp = t
                            elif k966: exp = (step(cur[0], t[0], 560), step(cur[1], t[1], 560))
                            elif same and h85e: exp = (step(cur[0], t[0], 2000), step(cur[1], t[1], 2000))
                            else: exp = t
                            if g != (exp[0] % 65536, exp[1] % 65536): bad[(k966, same, h85e, paused, a86d)] = bad.get((k966, same, h85e, paused, a86d), 0) + 1
    print('caller : %d states, %d differences' % (n * 64, sum(bad.values()))); return sum(bad.values())


def check_keys(r, rng, n):
    L = r.L; L.rig_call.restype = ctypes.c_uint32; L.rig_call.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]
    bad = 0; cases = 0

    def key(k):
        arr = (ctypes.c_uint32 * 1)(k); L.rig_call(0x1DAA0, arr, 1)

    def stage(code):
        r.reset()
        for a, v in ((0x4A83F, 1), (0x4A95E, 1), (0x4A88D, 1), (0x4A895, 0x80), (E + 0x62, 0x11)): r.w8(a, v)
        r.w16(0x4AAFA, 0x80); r.w16(0x4AA3C, 144); r.w8(0x4A85F, code); r.w8(0x4A949, 0)
    for code in range(14):
        for k, name in ((0x3E, 'KP8'), (0x3F, 'KP9'), (0x1E, 'KP2'), (0x3D, 'KP7'), (0x1D, 'KP1'), (0x2D, 'KP4'), (0x2F, 'KP6'), (0x2E, 'KP5')):
            stage(code); key(k); cases += 1
            if name in ('KP8', 'KP9'): want = 0
            elif name == 'KP2': want = 6
            elif name == 'KP7': want = 12
            elif name == 'KP1': want = 13
            elif name == 'KP5': want = code
            elif name == 'KP4':
                c = code - 1
                want = 11 if c < 0 else (0 if c == 0 or c >= 11 else c)
            else:
                c = code + 1
                want = c if c <= 11 else 0
            req = 0 if name == 'KP5' else 0xFF
            if (r.u8(0x4A85F), r.u8(0x4A949), r.u8(0x4A910)) != (want, 0xFF, req):
                bad += 1
                if bad < 6: print('keys DIFF', name, code, r.u8(0x4A85F), want)
    stage(0)
    for k, want in ((0x1B, 0x40), (0x1B, 0x20), (0x1B, 0x20), (0x1A, 0x40), (0x1A, 0x80), (0x1A, 0x80)):
        key(k); cases += 1
        if r.u16(0x4AAFA) != want or r.u8(0x4A910) != 0xFF or r.u8(0x4A949) != 0xFF: bad += 1
    stage(0); r.w16(E + 0x68, 2240)
    key(0x43); key(0x43); cases += 1
    if (r.u16(0x4A97E), r.u8(0x4A96A), r.u8(0x4A8EB), r.u8(0x4A865), r.u16(0x4A9E2)) != (2, 4, 0xFF, 0, 2240): bad += 1
    stage(0); key(0x37); cases += 1
    if (r.u8(0x4A865), r.u8(0x4A86C), r.u32(0x4ACFA), r.u16(0x4AB4C), r.u16(0x4AB4E), r.u16(0x4A97E)) != (1, 0, 0x3000000 if False else r.u32(0x4A71C), 7200, 0, 2): bad += 1
    print('keys   : %d key presses, %d differences' % (cases, bad)); return bad


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 600
    r = Rig(); rng = random.Random(seed)
    bad = check_head(r, rng, n) + check_ring(r, rng, n) + check_aim(r, rng, n * 5) + check_target(r, rng, n * 10) + check_caller(r, rng, max(2, n // 40)) + check_keys(r, rng, n)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
