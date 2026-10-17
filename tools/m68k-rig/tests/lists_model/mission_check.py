"""The mission-script dispatcher and the six scripts (docs/re/08-game-flow.md, section 12).

    python3 mission_check.py [SEED] [N]

The end-of-update routine 0x2852E (stale-lock drop 0x26F28, then the script dispatcher 0x28540 for game modes 3 to 9, 0x7D and the default
script) is run in the rig on N scripted states per mode and compared with a model written from the note's rules: the end code V:4A850, the
request V:4A8E2 and its countdown V:4A8E4, the delay V:4A978, the keys-locked byte V:4A965, the pause byte V:4A866 (used as the scripts'
countdown), the forced-return timer V:4A83A, the camera hand-over bytes, the destinations of the stolen aircraft, and whether the tracking
camera routine 0x1EC14 was called (V:4A994).  Also the landing test 0x28B12 (code 0xFF or 1 becomes 0xFC).
Seventy percent of the cases are biased toward each script's trigger (focus()); the summary lists how many cases took each branch (code stored, delay, tracking camera).
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
Run under: ulimit -v 2500000; timeout 600.
"""
import os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH

R0 = 0x4B23C


def rec(n): return R0 + 0x200 * n


def s8(v): v &= 0xFF; return v - 256 if v & 0x80 else v
def s16(v): v &= 0xFFFF; return v - 65536 if v & 0x8000 else v
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v


class St:
    """A flat model of the bytes the scripts touch: dict address -> value with sizes."""
    def __init__(self, r): self.r = r; self.o = {}
    def g8(self, a): return self.o.get(("b", a), self.r.u8(a))
    def g16(self, a): return self.o.get(("w", a), self.r.u16(a))
    def g32(self, a): return self.o.get(("l", a), self.r.u32(a))
    def p8(self, a, v): self.o[("b", a)] = v & 0xFF
    def p16(self, a, v): self.o[("w", a)] = v & 0xFFFF
    def p32(self, a, v): self.o[("l", a)] = v & 0xFFFFFFFF


def cam_call(s, n):
    """0x1EC14(n): the tracking camera on record n (the part this note needs)."""
    s.p8(0x4A910, 0xFF); s.p16(0x4A9F2, 0xFFFF); s.p8(0x4A968, 0xFE); s.p16(0x4A994, n)
    if s.g8(0x4A83D) != 0:
        s.p8(0x4A83D, s.g8(0x4A8EB))
    else:
        s.p16(0x4A9E2, s.g16(rec(n) + 0x68))
    s.p8(0x4A8EE, 3)


def valid(s, n): return bool(s.g8(rec(n) + 1) & 0x40)


def finish(s, d0):
    s.p16(0x4A978, d0); s.p8(0x4A8E2, 3); s.p8(0x4A8E4, 4); s.p8(0x4A965, 1)


def landing_ok(s):
    a = R0
    if not (s.g8(a + 0x21) & 1): return False
    if not (s.g8(a + 3) & 0x80): return False
    if s.g8(0x4A900) == 3:
        if not (s.g8(a + 4) & 0xC0): return False
    else:
        if not (s.g8(a + 4) & 0x04): return False
    return s.g16(a + 0x6E) == 0


def tail(s):
    c = s.g8(0x4A850)
    if c == 0xFF or c == 1:
        if landing_ok(s):
            s.p8(0x4A850, 0xFC); finish(s, 0)


def countdown(s, start):
    """The scripts' use of the pause byte: returns the byte after the decrement."""
    p = s.g8(0x4A866)
    if p == 0: p = start
    p = (p - 1) & 0xFF
    s.p8(0x4A866, p)
    return p


def cam_block(s, a, mode_byte, need_valid_for_set=True):
    """The p == 1 pass of modes 4 and 7: the external-view hand-over (once)."""
    if s.g8(0x4A876) & 0x80: return
    s.p8(0x4A876, s.g8(0x4A96A) | 0x80)
    s.p8(0x4A8EC, s.g8(0x4A83D))
    if not valid(s, 4): return
    s.p8(0x4A96A, 7); s.p8(0x4A86D, 1); s.p8(0x4A86C, 1); s.p8(0x4A865, 0)
    if s.g8(0x4A83D) != 0: return
    s.p16(0x4A9E2, s.g16(rec(4) + 0x68)); s.p16(0x4A97E, s.g16(0x4A97E) | 2); s.p8(0x4A8EB, mode_byte)


def mode4(s):
    if s.g8(0x4A850): return tail(s)
    a = rec(4); s.p16(0x4AA6C, 4)
    if (s.g16(a + 6) | s.g16(a + 0xC)) == 0: return
    if valid(s, 4):
        if not (s.g8(a + 3) & 0x80): return
        if s16(s.g16(a + 0x6C)) > 0x320: return
        if valid(s, 8) or valid(s, 10): return
    p = countdown(s, 5)
    if p == 0:
        if valid(s, 4):
            cam_call(s, 4)
            if s.g8(0x4A8E2) == 3: return
            s.p8(0x4A850, 0xFF); finish(s, 4)
        else:
            if s.g8(0x4A8E2) == 3: return
            s.p8(0x4A850, 0xFD if s.g8(a + 0x20) & 0x80 else 0xFE); finish(s, 0)
        return
    if s8(p) > 1: return
    cam_block(s, a, 0xFF)


def mode7(s):
    if s.g8(0x4A850): return tail(s)
    a = rec(4); s.p16(0x4AA6C, 4)
    if (s.g16(a + 6) | s.g16(a + 0xC)) == 0: return
    if valid(s, 4) and (s.g8(a + 1) & 0x80): return
    p = countdown(s, 2)
    if p == 0:
        if not (s.g8(a + 0x20) & 2):
            cam_call(s, 4)
            if s.g8(0x4A8E2) == 3: return
            s.p8(0x4A850, 0xFE); finish(s, 7)
        else:
            if s.g8(0x4A8E2) == 3: return
            s.p8(0x4A850, 0xFF); finish(s, 0)
        return
    if s8(p) > 1: return
    cam_block(s, a, 3)


def reroute(s, n):
    a = rec(n)
    idx = s.g8(a + 0x3A) * 2
    off = s16(s.r.u16(0x2DCE8 + idx))
    b = 0x2DCE8 + off
    w = [s16(s.r.u16(b + 2 * i)) for i in range(5)]
    s.p16(a + 0x2C, w[0]); s.p16(a + 0x2E, w[1]); s.p16(a + 0x30, w[2]); s.p16(a + 0x32, w[3]); s.p32(a + 0x34, w[4])


def success_check(s):
    cap = s8(s.g8(0x4A962)); kills = s8(s.g8(0x4A963))
    if cap - kills > 0: return
    p = countdown(s, 2)
    if p != 0: return
    if s.g8(0x4A8E2) == 3: return
    s.p8(0x4A850, 0xFF); finish(s, 0)


def mode5(s):
    if s.g8(0x4A850): return tail(s)
    a2, a3 = 4, 6
    if not valid(s, a2):
        if not valid(s, a3): return success_check(s)
        if not (s.g8(rec(a3) + 1) & 8): return f6(s)
        a2, a3 = a3, a2
    else:
        if not (s.g8(rec(a2) + 1) & 8): return f6(s)
    A = rec(a2)
    if s16(s.g16(A + 6)) >= 0x70:
        if s.g8(0x4A8E2) == 3: return
        s.p8(0x4A850, 0xFE); finish(s, 0); return
    prog = s8(s.g8(0x4A95F))
    d6 = 0x18000 if prog >= 3 else (0x24000 if prog >= 2 else 0x30000)
    far = False
    for off in (0x14, 0x18, 0x1C):
        d = s32(s.g32(R0 + off) - s.g32(A + off))
        d = -d if d < 0 else d
        if s32(d) > d6: far = True; break
    if not far:
        t = s16(s.g16(0x4A83A))
        if t >= 0:
            s.p16(0x4A83A, t - 1); return f6(s)
        reroute(s, a2); reroute(s, a3)
    if s16(s.g16(0x4A83A)) < 0: return f6(s)
    s.p16(0x4A83A, 200)
    return f6(s)


def f6(s):
    if s16(s.g16(0x4A83A)) >= 0: return
    success_check(s)


def mode6(s):
    if s.g8(0x4A850): return tail(s)
    off = s.g16(0x4A97A)
    if off == 0: return
    a = (R0 + s16(off)) & 0xFFFFFFFF
    if not (s.g8(a) & 0x80): return
    if s16(s.g16(a + 0x4C)) > 0: return
    d0 = s32(s.g32(rec(11) + 0x14) - s.g32(a + 0x14)); d0 = -d0 if d0 < 0 else d0
    d2 = s32(s.g32(rec(11) + 0x1C) - s.g32(a + 0x1C)); d2 = -d2 if d2 < 0 else d2
    if s32(d0) > 0x10000 or s32(d2) > 0x10000:
        if s.g8(0x4A8E2) == 3: return
        s.p8(0x4A850, 0xFE); finish(s, 0); return
    if s.g8(0x4A8E2) == 3: return
    s.p8(0x4A850, 0xFF); finish(s, 0)


def mode3(s):
    if s.g8(0x4A850): return tail(s)
    if s8(s.g8(0x4A927)) >= 0: return
    if s.g8(0x4A8E2) == 3: return
    s.p8(0x4A850, 0xFF); finish(s, 0)


def mode9(s):
    if s.g8(0x4A850): return
    if not valid(s, 0): return
    if (s.g16(R0 + 2) & 0xC080) != 0xC080: return
    if s.g16(R0 + 0x6E) != 0: return
    if s.g8(0x4A8E2) == 3: return
    s.p8(0x4A850, 0xFF); finish(s, 0)


def mode7d(s):
    if s.g8(0x4A850): return
    if valid(s, 4): return
    if s.g8(0x4A8E2) == 3: return
    s.p8(0x4A850, 0xFE); finish(s, 0)


def default(s, d0hi):
    if s.g8(0x4A850): return tail(s)
    cap = s.g8(0x4A962)
    if s8(cap) - s8(s.g8(0x4A963)) > 0: return
    p = countdown(s, 2)
    if p != 0: return
    if s.g8(0x4A8E2) == 3: return
    s.p8(0x4A850, 0xFF); finish(s, (d0hi << 8) | cap)


def model(s, lock0):
    """0x2852E: clears, stale-lock drop, dispatch."""
    s.p8(0x4A872, 0); s.p8(0x4A873, 0); s.p8(0x4A874, 0)
    lock = s16(s.g16(0x4AA78))
    if lock >= 0:
        a = R0 + lock
        if (s.g8(a + 1) & 0x40) and not (s.g8(a + 0x20) & 2):
            pass
        else:
            s.p16(0x4AA78, 0xFFFF); s.p8(0x4A920, 0); s.p16(0x4A9F2, 0xFFFF)
    if not (s.g8(0x4A985) & 0x40): return
    if s.g8(0x4A848): return
    mode = s8(s.g8(0x4A95E))
    if mode <= 2: return
    d0hi = (lock0 >> 8) & 0xFF
    {3: mode3, 4: mode4, 5: mode5, 6: mode6, 7: mode7, 9: mode9, 0x7D: mode7d}.get(mode, lambda s: default(s, d0hi))(s)


WATCH_B = [0x4A850, 0x4A8E2, 0x4A8E4, 0x4A965, 0x4A866, 0x4A876, 0x4A96A, 0x4A86D, 0x4A86C, 0x4A865, 0x4A8EB, 0x4A8EC, 0x4A872, 0x4A920, 0x4A83D, 0x4A8EE, 0x4A968, 0x4A910]
WATCH_W = [0x4A978, 0x4A83A, 0x4A9E2, 0x4A97E, 0x4AA78, 0x4A9F2, 0x4AA6C]


def setup(r, rng, mode):
    r.reset()
    r.w8(0x4A95E, mode & 0xFF); r.w8(0x4A985, 0x40 if rng.random() < 0.97 else 0); r.w8(0x4A848, 0 if rng.random() < 0.97 else 1)
    r.w8(0x4A850, rng.choice((0, 0, 0, 0, 0, 0xFF, 1, 0xFE, 4)))
    r.w8(0x4A8E2, rng.choice((0, 0, 0, 3, 2, 1)))
    r.w8(0x4A866, rng.choice((0, 0, 0, 0, 1, 2, 3, 5, 0xFF, 0xFE)))
    r.w8(0x4A876, rng.choice((0, 0, 0x80, 3))); r.w8(0x4A96A, rng.randint(0, 8)); r.w8(0x4A83D, rng.choice((0, 0, 1, 2)))
    r.w8(0x4A927, rng.choice((0, 0, 1, 0x1E, 0xFF, 0x80)))
    r.w8(0x4A961, rng.randint(0, 4)); cap = rng.choice((2, 2, 4)); r.w8(0x4A962, cap); r.w8(0x4A963, rng.choice((0, 1, cap - 1, cap, cap, cap + 1)))
    r.w16(0x4A83A, rng.choice((0, 0, 200, 0xFFFF, 5, 1)))
    r.w8(0x4A95F, rng.randint(0, 3)); r.w8(0x4A900, rng.choice((0, 3, 3)))
    r.w16(0x4A994, 0x7777); r.w16(0x4A9F2, 0x1234); r.w8(0x4A920, 9)
    r.w16(0x4AA78, rng.choice((0xFFFF, 0xFFFF, 0, 0x800, 0x1000, 0x1E00, 0x400)))
    r.w16(0x4A97A, rng.choice((0, 0, 0xC00 - 0x200 * 0, 0xA00, 0x1400, 0x1800)))
    for n in range(0, 16):
        a = rec(n)
        w0 = (rng.choice((0x40, 0, 0x40)) | rng.choice((0, 0x8, 0x80)) | (rng.choice((0, 0x80)) << 8) | 0x1000)
        r.w16(a, w0)
        r.w16(a + 2, rng.choice((0xC080, 0x0080, 0, 0x8000, rng.randint(0, 0xFFFF))))
        r.w8(a + 4, rng.choice((0, 0x04, 0x80, 0x40, 0xC0, 0x08)))
        r.w16(a + 6, rng.choice((0, 65, 100, 111, 112, 113, 200, 0xFFFF)))
        r.w16(a + 0xC, rng.choice((0, 5, 1)))
        r.w8(a + 0x20, rng.choice((0, 1, 2, 0x80, 3, 0x82)))
        r.w8(a + 0x21, rng.choice((0, 1)))
        r.w8(a + 0x3A, rng.randint(0, 40))
        r.w16(a + 0x4C, rng.choice((0, 0, 5, 0xFFF0, 20)))
        r.w16(a + 0x68, rng.randint(0, 28799)); r.w16(a + 0x6C, rng.choice((0, 0x100, 0x320, 0x321, 0x1200)))
        r.w16(a + 0x6E, rng.choice((0, 0, 0, 5)))
    # near-by positions so the proximity rules see both outcomes
    base = rng.randint(0x10000000, 0x14000000)
    for n in range(0, 16):
        a = rec(n)
        near = rng.random() < 0.5
        for off in (0x14, 0x18, 0x1C):
            r.w32(a + off, base + (rng.randint(-0x20000, 0x20000) if near else rng.randint(-0x400000, 0x400000)) if n != 0 else base)
    if mode == 6:
        # make the pod and the capsule near each other half of the time
        pass


def focus(r, rng, mode):
    """Bias a random state toward each script's trigger so that every branch gets many cases (the branches are listed in the summary)."""
    r.w8(0x4A850, rng.choice((0, 0, 0, 0, 4, 0xFF, 1, 0xFE)))
    if mode in (4, 7):
        a = rec(4); r.w16(a + 6, 65); r.w16(a + 0xC, 5)
        r.w16(a, rng.choice((0x50D8, 0x40D8, 0x5058, 0x5018)))
        r.w16(a + 2, rng.choice((0x0080, 0, 0x0080)))
        r.w16(a + 0x6C, rng.choice((0, 0x100, 0x320, 0x321)))
        r.w8(a + 0x20, rng.choice((0, 2, 0x80, 0x82)))
        for n in (8, 10): r.w16(rec(n), rng.choice((0x3091, 0x30D1)))
        r.w8(0x4A866, rng.choice((0, 0, 0, 1, 2, 3, 4, 5, 0xFF)))
        r.w8(0x4A8E2, rng.choice((0, 0, 0, 3)))
    elif mode == 5:
        for n in (4, 6):
            a = rec(n); r.w16(a, rng.choice((0x10D9, 0x10D1, 0x1099, 0x10D9))); r.w16(a + 6, rng.choice((65, 65, 112, 120)))
            for off in (0x14, 0x18, 0x1C): r.w32(a + off, 0x12000000 + (rng.randint(-0x20000, 0x20000) if off != 0x18 else 0))
        for off in (0x14, 0x18, 0x1C): r.w32(R0 + off, 0x12000000 + rng.choice((0, 0x10000, 0x40000, 0x300000)) * (off != 0x18))
        r.w16(0x4A83A, rng.choice((0, 200, 0xFFFF, 0xFFFF, 1, 2)))
        r.w8(0x4A962, rng.choice((2, 2, 3, 4))); r.w8(0x4A963, rng.choice((0, 2, 3, 4)))
        r.w8(0x4A866, rng.choice((0, 0, 0, 1, 2, 0xFE)))
        r.w8(0x4A8E2, rng.choice((0, 0, 3)))
    elif mode == 6:
        off = rng.choice((0x200, 0x400, 0x600)); r.w16(0x4A97A, off); a = R0 + off
        r.w16(a, rng.choice((0x90DA, 0x10DA, 0x90CA))); r.w16(a + 0x4C, rng.choice((0xFFF0, 0, 5, 1)))
        for o2 in (0x14, 0x1C):
            r.w32(a + o2, 0x12700000 + rng.choice((0, 0x8000, 0x10000, 0x10001, -0x20000 & 0xFFFFFFFF, 0x400000)))
            r.w32(rec(11) + o2, 0x12700000)
        r.w8(0x4A8E2, rng.choice((0, 0, 3)))
    elif mode == 8:
        r.w8(0x4A962, rng.choice((2, 4, 4))); r.w8(0x4A963, rng.choice((0, 1, 2, 3, 4, 5)))
        r.w8(0x4A866, rng.choice((0, 0, 1, 2)))
        r.w16(0x4AA78, rng.choice((0xFFFF, 0xFFFF, 0x1000, 0x1400, 0x1E00, 0x800)))
        for n in (4, 8, 10, 15): r.w16(rec(n), 0x10C0)
    elif mode == 3:
        r.w8(0x4A927, rng.choice((0xFF, 0xFE, 0, 1, 0x80, 0x7F)))
        r.w8(0x4A8E2, rng.choice((0, 3)))
    if rng.random() < 0.3:
        r.w8(0x4A850, rng.choice((0xFF, 1)))
        a = R0; r.w8(a + 0x21, 1); r.w8(a + 3, 0x80); r.w8(a + 4, rng.choice((0x04, 0xC0, 0x80, 0x40, 0))); r.w16(a + 0x6E, rng.choice((0, 0, 3))); r.w8(0x4A900, rng.choice((0, 3)))


def snapshot(r):
    s = St(r)
    return s


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print("skipped (missing: extracted/interceptor.bin or librig.so)"); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 300
    rng = random.Random(seed); r = Rig(); bad = 0; total = 0
    per = {}; hist = {}
    for mode in (3, 4, 5, 6, 7, 8, 9, 0x7D, 0x7F):
        cases = 0; changed = 0
        for _ in range(n):
            setup(r, rng, mode)
            if rng.random() < 0.7: focus(r, rng, mode)
            lock0 = r.u16(0x4AA78)
            ms = St(r)
            ms_before = {a: r.u8(a) for a in WATCH_B}; wb = {a: r.u16(a) for a in WATCH_W}
            model(ms, lock0)
            r.run(0x2852E)
            cases += 1; total += 1
            k = (hex(mode), 'code %02X->%02X' % (ms_before[0x4A850], r.u8(0x4A850)) if r.u8(0x4A850) != ms_before[0x4A850] else 'no code', 'delay %d' % r.u16(0x4A978) if r.u8(0x4A8E2) == 3 and ms_before[0x4A8E2] != 3 else '', 'cam' if r.u16(0x4A994) == 4 else '', 'reroute' if ms.o.get(('w', 0x4BA3C + 0x2C)) is not None or ms.o.get(('w', 0x4BE3C + 0x2C)) is not None else '')
            hist[k] = hist.get(k, 0) + 1
            diffs = []
            for a in WATCH_B:
                want = ms.o.get(("b", a), ms_before[a]); got = r.u8(a)
                if got != want: diffs.append(("b", hex(a), got, want))
            for a in WATCH_W:
                want = ms.o.get(("w", a), wb[a]); got = r.u16(a)
                if got != want: diffs.append(("w", hex(a), got, want))
            for (k, a), v in ms.o.items():
                if k == "w" and a not in WATCH_W:
                    if r.u16(a) != v: diffs.append(("w", hex(a), r.u16(a), v))
                if k == "l" and r.u32(a) != v: diffs.append(("l", hex(a), r.u32(a), v))
            if ms.o.get(("b", 0x4A850), ms_before[0x4A850]) != ms_before[0x4A850]: changed += 1
            if diffs:
                bad += 1
                if bad < 8: print("DIFF mode %02x seed %d: %r" % (mode, seed, diffs[:6]))
        per[mode] = (cases, changed)
    for k in sorted(hist): print("  branch", " ".join(x for x in k if x), hist[k])
    print("mission_check: %d states, per mode (cases, with an effect): %s, %s" % (total, {hex(k): v for k, v in per.items()}, "FAILED %d" % bad if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
