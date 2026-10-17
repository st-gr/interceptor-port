"""Probes of the view rules that note 03 ("External camera aim", "Camera attitude") and note 06 ("Head views", "View kinds, settled") state from the note-fix round.

    python3 view_probe.py [SEED] [N]          (about a minute; `python3 view_probe.py 1 600 sqrt` adds the exhaustive square-root range check, about two minutes)

Each probe runs the original code in the rig (extracted/interceptor.bin, librig.so; skipped with exit 0 when one is missing) and compares with the rule written in the notes:

  point   the aim target point of 0x32092: the viewed record's position plus the offset (0, 0, 1) (or (0, 0, 5) with the camera mode V:4A966 = 6, or (0, 0, -4) when the high nibble of the
          class byte is 3) rotated by the nine words at +0x92 and shifted right by 4 (0x1D478/0x1D488), minus the camera longs V:4ACF6.., turned into the pair by the target rule of note 03;
          the same vector with the shift 6 gives a different pair in a counted share of the states (the discriminating power of the test);
  atan    the index range of the heading table (0..511, 1,471 of 300,000 random vectors above 260), no divide overflow in the rounded quotient over them, the example vector (-1000, 0, 1000);
  sqrt    no divide overflow in the Newton iteration over the whole range its three branches feed (with the argument `sqrt`);
  snap    0x1454A sets V:4A85E to 1 on every snap path (a negative limit, or the first frame);
  paused  the key routine 0x1DAA0 with the pause byte V:4A866 set against clear: the bytes each key changes (memory difference of the whole variable area) are the same except for E and P;
  map     M in the cockpit, in an external view and in the map (which of V:4A9E2, V:4A8EB, V:4A97E bit 1 are written), the altitude clamps of [ and ] in the map;
  extlongs 0x1F45E in an external view: the camera longs and words from the ring position and the negated masked copies;
  sites   the request sites 0x1431A, 0x143C4 and 0x1EC14 (the state routine for a class 0x30 viewed record, the reset of a dead viewed record, the viewed-entity switch);
  step    the HUD step byte V:4AA3E after every head key from every code with each present window row V:4AA3C (the equal-row rule), and the cockpit zoom reset of 0x1F060.
"""
import ctypes, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH, ROOT
import view_check as vc

E = vc.E


def key_call(r, k):
    L = r.L; L.rig_call.restype = ctypes.c_uint32; L.rig_call.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]
    arr = (ctypes.c_uint32 * 1)(k); L.rig_call(0x1DAA0, arr, 1)


def call(r, addr, *args):
    L = r.L; L.rig_call.restype = ctypes.c_uint32; L.rig_call.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]
    arr = (ctypes.c_uint32 * max(1, len(args)))(*[a & 0xFFFFFFFF for a in args]); return L.rig_call(addr, arr, len(args))


def probe_point(r, rng, n):
    tab = struct.unpack('>1024h', (r.reset(), r.peek(0x42C00, 2048))[1])
    bad = diff6 = 0
    for _ in range(n):
        r.reset()
        cls = rng.choice((0x11, 0x10, 0x14, 0x30, 0x31, 0x3F, 0x20, 0x12))
        vc.attitude(r, cls, rng.randrange(28800), rng.randrange(28800), rng.choice((0, rng.randrange(28800))))
        for off in (0x14, 0x18, 0x1C): r.w32(E + off, rng.randint(-(1 << 28), 1 << 28))
        mode = rng.choice((0, 6)); r.w8(0x4A966, mode)
        r.w8(0x4A83D, 0xFF); r.w8(0x4A86C, 0xFF); r.w8(0x4A86D, 1); r.w8(0x4A866, 0); r.w16(0x4A996, 0)
        pos = [r.s32(E + 0x14 + 4 * i) for i in range(3)]
        sc = rng.choice((1 << 17, 1 << 17, 1 << 25))
        cam = [vc.s32(pos[i] + rng.randint(-sc, sc)) for i in range(3)]
        for i in range(3): r.w32(0x4ACF6 + 4 * i, cam[i])
        r.run(0x32074)
        got = (r.u16(0x4AB4C), r.u16(0x4AB4E))
        w = (0, 0, -4) if cls & 0xF0 == 0x30 else (0, 0, 5 if mode == 6 else 1)
        m = vc.words(r, E + 0x92, 9)
        def pt(shift):
            return [vc.s32(pos[i] + (sum(m[3 * i + j] * w[j] for j in range(3)) >> shift)) for i in range(3)]
        def pair(shift):
            p = pt(shift); d = [vc.s32(p[i] - cam[i]) for i in range(3)]
            return tuple(v % 65536 for v in vc.aim_target(tab, *d))
        if got != pair(4): bad += 1
        if pair(4) != pair(6): diff6 += 1
    print('point  : %d states, %d differences from the shift-4 rule; the shift-6 rule would give another pair in %d' % (n, bad, diff6)); return bad


def probe_atan(r, rng, n):
    r.reset(); tab = struct.unpack('>1024h', r.peek(0x42C00, 2048))
    L = r.L
    r.w8(0x4A85E, 0); r.w8(0x4A966, 0); call(r, 0x1454A, 0, 0, -1000, 0, 1000, -1)
    ex = (r.u16(0x4AB78), r.u16(0x4AB7A))
    idx = vc.div_round((1000 << 6), 3) >> 6
    bad = 0 if (idx, ex, tab[idx]) == (333, (0, 72), 9) else 1
    ovf = mx = gt = 0
    for _ in range(n * 100):
        mag = rng.choice((1 << 8, 1 << 12, 1 << 16, 1 << 20, 1 << 22, 1 << 24, 1 << 26, 1 << 28, 1 << 29, 1 << 30))
        dx, dy, dz = [rng.randint(-mag, mag) for _ in range(3)]
        ax, az = abs(dx), abs(dz); m = max(ax, abs(dy), az)
        if m > 0x10000000: ax >>= 3; az >>= 3; S, T = 14, 0
        elif m > 0x2000000: S, T = 14, 0
        elif m > 0x800000: S, T = 12, 2
        elif m > 0x200000: S, T = 10, 4
        else: S, T = 8, 6
        xs, zs = vc.s16(ax >> S), vc.s16(az >> S)
        if xs <= zs and zs: a, b = (ax << T), zs
        elif xs > zs: a, b = (az << T), xs
        else: continue
        q = a // b
        if q > 32767: ovf += 1
        i = vc.div_round(a & 0xFFFFFFFF, b) >> 6
        mx = max(mx, i); gt += i > 260
    print('atan   : (-1000, 0, 1000): index %d, Tab word %d, pair (pitch, heading) %s; %d random vectors: largest index %d, %d above 260, %d divide overflows' % (idx, tab[idx], ex, n * 100, mx, gt, ovf))
    return bad + ovf


def sqrt_all():
    mx = 0; ovf = 0
    for v in range(0, 8400001):
        g = ((v // 200) + 2) & 0xFFFF
        while True:
            q = v // g
            if q > 0xFFFF: ovf += 1; q &= 0xFFFF
            mx = max(mx, q)
            if ((q - g) & 0xFFFF) in (0, 1, 0xFFFF): break
            g = ((q + g) & 0xFFFF) >> 1
    print('sqrt   : values 0..8,400,000 (the three branches of 0x29B12 feed at most 8.39 million into the iteration): largest quotient %d, %d divide overflows' % (mx, ovf))
    return ovf


def probe_snap(r):
    bad = 0
    for lim, h85e, k966 in ((-1, 0, 0), (-1, 0, 5), (-1, 1, 0), (2000, 0, 0), (560, 0, 5), (2000, 1, 0)):
        r.reset(); r.w8(0x4A85E, h85e); r.w8(0x4A966, k966)
        call(r, 0x1454A, 100, 200, 5000, 3000, 7000, lim)
        want = 1 if (lim < 0 or (k966 == 0 and h85e == 0)) else 0
        want = 1 if (want or h85e) else 0
        if r.u8(0x4A85E) != want: bad += 1
    print('snap   : V:4A85E after 0x1454A on the snap paths and the stepped paths: %d differences' % bad); return bad


def stage_flight(r, **kw):
    r.reset()
    vals = {0x4A83F: 1, 0x4A95E: 1, 0x4A88D: 1, 0x4A895: 0x80, 0x4A849: 0, 0x4A903: 0, 0x4A966: 0, 0x4A88B: 0, 0x4A965: 0, 0x4A83D: 0, E + 0x62: 0x11, 0x4B29E: 0x11, 0x4A85F: 0}
    for a, v in vals.items(): r.w8(a, v)
    r.w16(0x4AAFA, 0x80); r.w16(0x4AA3C, 144)


def probe_paused(r):
    names = {0x24: 'G gear', 0x20: 'A hook', 0x25: 'H hud', 0x41: 'BACKSPACE', 0x0B: '0', 0x0C: '-', 0x0D: '=', 0x4C: 'UP', 0x4D: 'DOWN', 0x4F: 'LEFT', 0x4E: 'RIGHT', 0x38: '0x38', 0x39: '0x39',
             0x23: 'F flare', 0x33: 'C chaff', 0x26: 'J', 0x13: 'R', 0x14: 'T', 0x15: 'Y', 0x40: 'SPACE', 0x12: 'E', 0x45: 'ESC', 0x44: 'RETURN', 0x19: 'P', 0x21: 'S'}
    lo, hi = 0x4A6E8, 0x54EC0
    same = []; differ = []
    for k, nm in names.items():
        res = []
        for paused in (0, 0xFF):
            stage_flight(r); r.w8(0x4A866, paused); r.w8(0x4A930, 4 if k == 0x12 else 0); r.w8(0x4A905, 5); r.w8(0x4A904, 5); r.w8(0x4A865, 1)
            before = r.peek(lo, hi - lo)
            key_call(r, k)
            after = r.peek(lo, hi - lo)
            res.append({lo + i for i in range(hi - lo) if before[i] != after[i]})
        a, b = res
        (same if a == b else differ).append((nm + ('(%d bytes)' % len(a)) if a == b else nm, sorted(a - b), sorted(b - a)))
    print('paused : %d keys with the same changed bytes paused and unpaused: %s' % (len(same), ', '.join(n for n, _, _ in same)))
    for nm, only_un, only_p in differ:
        print('         differs: %s: only unpaused %s, only paused %s' % (nm, [hex(x) for x in only_un], [hex(x) for x in only_p]))
    return 0


def probe_map(r):
    out = []
    # M in the cockpit
    stage_flight(r); r.w16(E + 0x68, 2240); key_call(r, 0x37)
    out.append(('M cockpit', r.u16(0x4A9E2), r.u8(0x4A8EB), r.u16(0x4A97E) & 2, r.u8(0x4A865)))
    # M in an external view (level 4)
    stage_flight(r); r.w8(0x4A83D, 4); r.w16(E + 0x68, 2240); r.w16(0x4A9E2, 1234); r.w8(0x4A8EB, 7); key_call(r, 0x37)
    out.append(('M external', r.u16(0x4A9E2), r.u8(0x4A8EB), r.u16(0x4A97E) & 2, r.u8(0x4A865), r.u8(0x4A83D), r.u16(0x4AA3C)))
    # [ and ] in the map: clamps
    res = []
    for alt, k in ((0x3000000, 0x1A), (0x5000000, 0x1A), (0x6000000, 0x1A), (0x7000000, 0x1A), (0x8000000, 0x1A), (0x3000000, 0x1B), (0x2000000, 0x1B), (0x1000000, 0x1B)):
        stage_flight(r); r.w8(0x4A83D, 0xFF); r.w8(0x4A86D, 1); r.w8(0x4A865, 1); r.w32(0x4ACFA, alt); key_call(r, k)
        res.append((hex(alt), 'bracket-' + ('farther' if k == 0x1A else 'closer'), hex(r.u32(0x4ACFA))))
    for o in out: print('map    :', o)
    print('map    : altitude after [ / ]:', res)
    return 0


def probe_step(r):
    keys = {0x3E: 'KP8', 0x1E: 'KP2', 0x2D: 'KP4', 0x2F: 'KP6', 0x3D: 'KP7', 0x1D: 'KP1'}
    rows = {c: (144 if c in (0, 1, 2, 10, 11) else 179 if c in (5, 6, 7) else 167) for c in range(14)}
    tab = list(struct.unpack('14b', open(os.path.join(ROOT, 'assets', 'tables', 'head_step.bin'), 'rb').read()))   # the extractor's table (note 06); the file name and hash are in the note
    cmp_codes = set(range(3, 10)) | {12, 13}
    bad = cases = 0
    for code in range(14):
        for k in keys:
            for cur in (144, 167, 179):
                stage_flight(r); r.w8(0x4A85F, code); r.w16(0x4AA3C, cur); r.w16(0x4AA3E, 1234); key_call(r, k)
                new = r.u8(0x4A85F); step = r.s16(0x4AA3E)
                want = 50 if (new in cmp_codes and rows[new] == cur) else tab[new]
                cases += 1
                if step != want or r.u16(0x4AA40) != (want * 16) & 0xFFFF or r.u16(0x4AAFA) != 0x80: bad += 1; print('step DIFF', keys[k], code, cur, new, step, want)
    # the zoom reset
    stage_flight(r); key_call(r, 0x1B); z1 = r.u16(0x4AAFA); key_call(r, 0x2F); z2 = r.u16(0x4AAFA)
    print('step   : %d (code, key, present row) cases, %d differences; zoom word after ] %#x, after a following head key %#x, V:4A895 bit 7 %d' % (cases, bad, z1, z2, r.u8(0x4A895) >> 7))
    return bad


def probe_sites(r):
    """The request sites 0x1431A (class 0x30 in 0x141E8), 0x143C4 (0x14392) and 0x1EC14 (the viewed-entity switch)."""
    out = []
    R4 = 0x4B23C + (4 << 9)
    # 0x1431A: the viewed record is of class 0x30 and the HUD step is 0
    stage_flight(r); r.w8(R4 + 0x62, 0x30); r.w16(0x4A994, 4); r.w16(0x4A996, 0x800); r.w16(0x4AA3E, 0); r.w16(0x4A97E, 0); r.w8(0x4A910, 0)
    r.run(0x141E8)
    out.append(('0x1431A', r.u8(0x4A910), r.u16(0x4AA3E), r.u16(0x4AA40), r.u16(0x4AA3C)))
    # 0x143C4: the viewed record 4 is not active (bit 6 of its first word clear): reset to the player
    stage_flight(r); r.w16(R4, 0); r.w16(0x4A994, 4); r.w16(0x4A996, 0x800); r.w8(0x4A910, 0); r.w16(0x4AA3E, 77)
    r.run(0x14392)
    out.append(('0x143C4', r.u8(0x4A910), r.u16(0x4A994), r.u16(0x4A996), r.u16(0x4AA3E)))
    # 0x1EC14: the switch to record 4 in the cockpit (class 0x11): the head code 0 path; class 0x30: the HUD step 50
    for cls in (0x11, 0x30):
        stage_flight(r); r.w8(R4 + 0x62, cls); r.w16(R4 + 0x68, 777); r.w8(0x4A910, 0); r.w16(0x4AAFA, 0x40)
        r.run(0x1EC14, d1=4, d0=0x80)
        out.append(('0x1EC14 class %#x' % cls, r.u8(0x4A910), r.u16(0x4A994), r.u16(0x4A996), r.u8(0x4A968), r.u16(0x4A9E2), r.u16(0x4AAFA), r.s16(0x4AA3E)))
    for o in out: print('sites  :', o)
    return 0


def probe_extlongs(r, rng, n):
    """0x1F45E in an external view (V:4A864 clear): the camera longs V:4AB34.. are the unmasked ring position, V:4AB1A.. the negated masked longs V:4ACEA.. copied, V:4AB2A.. their high words
    shifted by 8, V:4AB30 the height long >> 8 (note 03 "External camera longs")."""
    bad = 0
    for _ in range(n):
        r.reset()
        pos = [rng.randint(-(1 << 30), 1 << 30) for _ in range(3)]
        neg = [vc.s32(-(pos[0] & 0x3FFFFF)), vc.s32(-pos[1]), vc.s32(-(pos[2] & 0x3FFFFF))]
        for i in range(3): r.w32(0x4ACF6 + 4 * i, pos[i]); r.w32(0x4ACEA + 4 * i, neg[i])
        r.w8(0x4A83D, rng.choice((0xFF, 1, 2, 3, 4))); r.w8(0x4A864, 0)
        r.run(0x1F45E)
        got = ([r.s32(0x4AB34 + 4 * i) for i in range(3)], [r.s32(0x4AB1A + 4 * i) for i in range(3)], [r.s16(0x4AB2A + 2 * i) for i in range(3)], r.s32(0x4AB30))
        exp = (pos, neg, [vc.w16(neg[i] >> 8) for i in range(3)], neg[1] >> 8)
        if got != exp:
            bad += 1
            if bad < 4: print('extlongs DIFF', got, exp)
    print('extlongs: %d states, %d differences' % (n, bad)); return bad


def probe_followups(r):
    """Round-3 questions: A the offset order for a class 0x30 target in camera mode 6; B the hold-0 recentre resets the zoom; D the head keys of a class 0x30 viewed record leave the HUD step and window row alone."""
    out = []
    tab = struct.unpack('>1024h', (r.reset(), r.peek(0x42C00, 2048))[1])
    # A: class 0x30 with mode 6: which offset?
    got = {}
    for cls, mode in ((0x30, 6), (0x30, 0), (0x11, 6), (0x11, 0)):
        r.reset(); vc.attitude(r, cls, 1000, 5000, 0)
        for i, v in enumerate((100000, 20000, -300000)): r.w32(E + 0x14 + 4 * i, v)
        r.w8(0x4A966, mode); r.w8(0x4A83D, 0xFF); r.w8(0x4A86C, 0xFF); r.w8(0x4A86D, 1); r.w16(0x4A996, 0)
        for i, v in enumerate((100000 + 5000, 20000 + 3000, -300000 - 9000)): r.w32(0x4ACF6 + 4 * i, v)
        r.run(0x32074); pair = (r.u16(0x4AB4C), r.u16(0x4AB4E))
        m = vc.words(r, E + 0x92, 9); pos = [100000, 20000, -300000]; cam = [105000, 23000, -309000]
        want = {}
        for w in ((0, 0, -4), (0, 0, 5), (0, 0, 1)):
            pt = [pos[i] + (sum(m[3 * i + j] * w[j] for j in range(3)) >> 4) for i in range(3)]
            want[w] = tuple(v % 65536 for v in vc.aim_target(tab, *[pt[i] - cam[i] for i in range(3)]))
        got[(hex(cls), mode)] = [w for w, p in want.items() if p == pair]
    out.append(('A offsets matching the pair (class, mode)', got))
    # B: hold 0 in the cockpit through 0x141E8
    stage_flight(r); r.w8(0x4A949, 0); r.w8(0x4A85F, 5); r.w16(0x4AAFA, 0x40); r.w8(0x4A895, 0); r.w8(0x4A8F5, 0); r.w16(0x4A97E, 0)
    r.run(0x141E8)
    out.append(('B after 0x141E8 with hold 0, code 5, zoom 0x40', r.u8(0x4A85F), r.u8(0x4A949), hex(r.u16(0x4AAFA)), r.u8(0x4A895) >> 7, r.u8(0x4A8F5), r.u8(0x4A910)))
    # D: class 0x30 viewed record: head keys leave step and row alone
    res = []
    for k in (0x2F, 0x2D, 0x3E, 0x1E, 0x3D, 0x1D):
        for code in (0, 3, 4, 6, 11):
            stage_flight(r); r.w8(0x4B23C + (4 << 9) + 0x62, 0x30); r.w16(0x4A994, 4); r.w16(0x4A996, 0x800); r.w8(0x4A85F, code)
            r.w16(0x4AA3E, 50); r.w16(0x4AA40, 800); r.w16(0x4AA3C, 167); r.w16(0x4AAFA, 0x40)
            key_call(r, k)
            res.append((r.s16(0x4AA3E), r.s16(0x4AA40), r.u16(0x4AA3C), r.u16(0x4AAFA)))
    out.append(('D class 0x30 head keys: distinct (step, offset, row, zoom)', sorted(set(res))))
    for o in out: print('round3 :', o)
    return 0


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 600
    r = Rig(); rng = random.Random(seed)
    bad = probe_point(r, rng, n) + probe_atan(r, rng, n * 5 // 3) + probe_snap(r) + probe_paused(r) + probe_map(r) + probe_step(r) + probe_sites(r) + probe_extlongs(r, rng, n // 3) + probe_followups(r)
    if 'sqrt' in argv: bad += sqrt_all()
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
