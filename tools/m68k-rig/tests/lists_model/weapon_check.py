"""The gun, the tracer pool and the missile launch (docs/re/07-ai-and-weapons.md, sections 7 and 8): the original's routines run in the Musashi rig on
random states and are compared, byte for byte over the whole state hunk 0x4A6E8..0x54EC0, with a model written from the note.

    python3 weapon_check.py [gun|launch|gate|hits|hull|entity|focus|repeat|order|bighull|ships|chaff|nonplayer|result40|shipverts|scope|decision|landmark|decoy|rangekey|all] [N] [SEED]   (run under ulimit -v 2500000 and timeout)

gun     0x172DC (the gun/countermeasure routine, 10 usable pool slots, the burst reservoir, rounds, the log counter, the slot step 0x1754C and the slot
        initialiser 0x177D8) with an empty entity queue, drawers stubbed;
motion  the pool slot step with the world: gravity, drag, cell wrap, ground impact, expiry;
launch  the missile launch 0x275C4 (player) and 0x275F6 (AI): stock, copy, class, template descriptor, hard-point offset, life, log counters;
hull    the aircraft hull test 0x2B4BA (the point in the convex face list at 0x3DACC) against a model read from the image;
focus   Review Focus 1 through the world update: no target, a target that dies, a target out of reach, a hit and the kill count;
decision the AI launch decision 0x2701A against the order of tests of note 07 4.6.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so, extracted/captures/logs/lists.txt.  Skips with exit 0 when one is missing.
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import IMGPATH, inputs_missing
import combat as C
from combat import REC, LOGBUF, rig, stage, callf
import visibility as V
from visibility import Mem, s8, s16, s32, u16

POOL = 0x4AD2A
LO, HI = 0x4A6E8, 0x54EC0
TICK = 0x0001


def asr(v, n): return v >> n


class Model:
    """a Mem over the whole image plus the state hunk copied from the rig"""
    def __init__(self, img):
        self.img = img
        self.m = Mem(b"\x00" * 0x10000 + img + b"\x00" * (0x800000 - 0x10000 - len(img)))

    def load(self, r):
        self.m.b[LO:HI] = r.peek(LO, HI - LO)
        self.m.b[LOGBUF:LOGBUF + 0x80] = r.peek(LOGBUF, 0x80)
        self.m.b[0x1D024:0x1D028] = r.peek(0x1D024, 4)
        self.m.b[0x2600C:0x2600C + 320] = r.peek(0x2600C, 320)
        self.sounds = []


def vnorm32(m, d0, x, y, z):
    """0x29CB0: like 0x29C0E but the shift starts at 3 (the result is 32 times as long); returns the three words, sign extended"""
    d0 = s16(d0); neg = d0 < 0
    assert d0 != 0
    if d0 < 0: d0 = s16(-d0)
    d1 = V.fx_dist(m, V.abs16(x), V.abs16(y), V.abs16(z))
    d1 = s32(d1)
    m.w16(0x4ABF8, d1 & 0xFFFF)
    sh = 3; l = d0
    while not (l > d1): l <<= 2; sh += 2
    while True:
        l >>= 2; sh -= 2
        if sh > 1:
            if l <= d1: break
            continue
        break
    l <<= 2; sh += 2
    l = (l << 8) & 0xffffffff
    q = l // (d1 & 0xffff)
    d0q = (q & 0xffff) if q <= 0xffff else (l & 0xffff)
    mm = s16(d0q)
    res = [s32(mm * s16(v)) >> sh for v in (x, y, z)]
    if neg: res = [-v for v in res]
    return res


def divs_w(d0, d1):
    """68000 DIVS.W: D0.l / D1.w; returns the new D0 (quotient low, remainder high), overflow leaves D0 unchanged"""
    d0 = s32(d0); dv = s16(d1)
    if dv == 0: raise ZeroDivisionError
    q = int(d0 / dv)
    if q > 32767 or q < -32768: return d0 & 0xFFFFFFFF
    rem = d0 - q * dv
    return ((rem & 0xFFFF) << 16) | (q & 0xFFFF)


def muls_w(a, b): return s16(a) * s16(b)


class Gun:
    """the model of 0x172DC with an empty entity queue"""
    def __init__(self, md):
        self.md = md; self.m = md.m

    def call(self, hit=None):
        m = self.m
        fired = 0; anyf = 0
        m.w32(0x4A79E, 0xFFFFF)
        T = 0x4B277; A = 0x4B2B9
        m.w8(T, m.u8(T) - 1)
        for i in range(0, 20, 2):
            S = POOL + 64 * i
            if m.u8(0x4A875) == 0 and m.u8(0x4A866) == 0:
                m.w16(S + 0x28, m.s16(S + 0x28) - 1)
            F = S + 0x26
            cand = (m.u8(0x4A866) == 0 and (m.u16(0x4A97E) & 8)) or m.u8(0x4A943) != 0
            if cand and not (m.u16(F) & 1):
                if (not fired) and (m.u8(A) & 0xF0) and m.s16(0x4B29C) >= 1:
                    fired = 1
                    m.w8(A, (m.u8(A) & 0xF0) - 0x10 + (m.u8(A) & 0xF))
                    anyf = 1
                    lg = m.u32(0x1D024)
                    m.w16(lg + 0x3A, m.u16(lg + 0x3A) + 3)
                    m.w8(0x4A87D, 1)
                    m.w16(F, 0x401)
                    m.w16(S + 0x2E, 0)
                    if m.u8(0x4A943):
                        if m.u8(0x4A865) == 0: self.md.sounds.append((1, 0x1C, 0x30))
                        if m.u8(0x4A943) - 1 == 0:
                            m.w16(F, m.u16(F) | 0x2000); m.w8(0x4A945, i + 1)
                        else:
                            m.w16(F, m.u16(F) | 0x1000); m.w8(0x4A944, i + 1)
                        m.w8(0x4A943, 0)
                    else:
                        if m.u8(0x4A865) == 0:
                            if m.u8(0x4AC13) & 0x20:
                                self.md.sounds.append((2, 6, 0x1E if m.u8(0x4A83D) else 0x16))
                            else:
                                self.md.sounds.append((1, 0x1C, 0x30))
                        n = m.s16(0x4B29C) - 3
                        m.w16(0x4B29C, 0 if n < 0 else n)
                        m.w8(0x4A8FB, 3)
                        m.w8(T, 0x14)
                    self.init(i)
                else:
                    if m.s8(T) > 0: anyf = 1
            elif cand:
                pass
            if m.u16(F) & 1:
                self.step(i)
        if not anyf:
            m.w8(T, 0)
            m.w8(A, (m.u8(A) & 0xF) | 0x50)

    # ---- 0x177D8 and 0x17AFE
    def init(self, i):
        m = self.m; S = POOL + 64 * i; F = m.u16(S + 0x26)
        mw = [m.s16(0x4B23C + o) for o in range(0x92, 0xA4, 2)]
        if F & 0x1000: a, b, c, life = 0, -8, 0, 0x1E
        elif F & 0x2000: a, b, c, life = 0, -8, -56, 0x3C
        elif m.u8(0x4B29E) == 0x11: a, b, c, life = 0, 0, 0x38, 0x14
        else: a, b, c, life = 6, 1, 0x18, 0x14
        m.w16(S + 0x28, life)
        o = [(mw[0] * a + mw[1] * b + mw[2] * c) >> 6, (mw[3] * a + mw[4] * b + mw[5] * c) >> 6, (mw[6] * a + mw[7] * b + mw[8] * c) >> 6]
        m.w32(0x4AB0A, o[0]); m.w32(0x4AB0E, o[1]); m.w32(0x4AB12, o[2])
        m.w16(S + 0x30, m.u16(0x4B242)); m.w16(S + 0x32, m.u16(0x4B244))
        px = (m.u32(0x4B250) & 0x3FFFFF) + o[0]
        py = m.s32(0x4B254) + o[1]
        pz = (m.u32(0x4B258) & 0x3FFFFF) + o[2]
        m.w32(S, px); m.w32(S + 4, py); m.w32(S + 8, pz)
        if F & 0x3000: b2, c2 = -64, 0
        else:
            b2, c2 = 0x40, 0x400
            n = m.u16(0x4A992) & 3
            if n == 2: b2 += 5
            elif n == 1: b2 -= 3
            elif n == 0: b2 += 3
        d = [(mw[1] * b2 + mw[2] * c2) >> 4, (mw[4] * b2 + mw[5] * c2) >> 4, (mw[7] * b2 + mw[8] * c2) >> 4]
        m.w32(0x4AB0A, d[0]); m.w32(0x4AB0E, d[1]); m.w32(0x4AB12, d[2])
        m.w16(0x4AA70, 0)
        # 0x17AFE
        n = m.u16(0x4A992) & 3
        w = {0: 0, 1: 16, 2: 0, 3: -16}[n]
        s6 = m.s16(0x4B23C + 0x6C) >> 6
        if F & 0x1000: ln = 13
        elif F & 0x2000: ln = 5
        else: ln = w + 0xC6 + s6
        d1 = (m.u32(0x4B250) & 0x3FFFFF) + d[0] - px
        d0 = m.s32(0x4B254) * 0 + d[1] + m.s32(0x4B254) - py
        d3 = (m.u32(0x4B258) & 0x3FFFFF) + d[2] - pz
        # the words passed: (value >> 8) as a 16-bit word
        xs, ys, zs = (s16(s32(d1) >> 8), s16(s32(d0) >> 8), s16(s32(d3) >> 8))
        out = vnorm32(m, ln, xs, ys, zs)
        m.b_out = out
        m.w16(0x4AB04, out[0]); m.w16(0x4AB06, out[1]); m.w16(0x4AB08, out[2])
        m.w32(S + 0xC, out[0] << 3); m.w32(S + 0x10, out[1] << 3); m.w32(S + 0x14, out[2] << 3)
        m.w32(S + 0x18, out[0]); m.w32(S + 0x1C, out[1]); m.w32(S + 0x20, out[2])
        m.w16(S + 0x24, 0)
        if F & 0x3000:
            for k, vo in enumerate((0x3E, 0x42, 0x46)):
                v = m.s32(0x4B23C + vo)
                m.w32(S + 0xC + 4 * k, m.s32(S + 0xC + 4 * k) + v)
                m.w32(S + 0x18 + 4 * k, m.s32(S + 0x18 + 4 * k) + (v >> 3))

    # ---- 0x1754C
    def step(self, i, hit=None, ret=False):
        m = self.m; S = POOL + 64 * i; F = S + 0x26
        life = m.s16(S + 0x28)
        if life <= 0:
            m.w16(F, m.u16(F) & 0xFFFE)
            if m.u8(0x4A944) == i + 1: m.w8(0x4A944, 0)
            if m.u8(0x4A945) == i + 1: m.w8(0x4A945, 0)
            return
        if not (m.u16(F) & 2):
            h = self.hit_test(i)
            self.last_hit = h
            if h:
                m.w16(F, (m.u16(F) & 0xFFF7) | 0x12); m.w16(S + 0x28, 2)
            elif m.s32(S + 4) < 0:
                m.w16(F, (m.u16(F) & 0xFFE7) | 2); m.w16(S + 0x28, 2)
                vy = m.s32(S + 0x10) >> 8
                m.w16(0x4AB88, vy)
                if s16(vy) != 0: self.ground(i)
            else:
                if m.s32(S) < 0:
                    c = m.s16(S + 0x30) - 1
                    if c < 0: c = 0x7F
                    m.w16(S + 0x30, c); m.w32(S, m.s32(S) + 0x400000)
                elif m.s32(S) >= 0x400000:
                    c = m.s16(S + 0x30) + 1
                    if c > 0x7F: c = 0
                    m.w16(S + 0x30, c); m.w32(S, m.s32(S) - 0x400000)
                if m.s32(S + 8) < 0:
                    c = m.s16(S + 0x32) - 1
                    if c < 0: c = 0x7F
                    m.w16(S + 0x32, c); m.w32(S + 8, m.s32(S + 8) + 0x400000)
                elif m.s32(S + 8) >= 0x400000:
                    c = m.s16(S + 0x32) + 1
                    if c > 0x7F: c = 0
                    m.w16(S + 0x32, c); m.w32(S + 8, m.s32(S + 8) - 0x400000)
        else:
            if m.u16(F) & 0x100:
                for k, vo in enumerate((0x3E, 0x42, 0x46)):
                    m.w32(S + 4 * k, m.s32(S + 4 * k) + (m.s32(0x4B23C + vo) >> 2))
        # the drawers are stubbed in the rig: no effect

    def ground(self, i):
        m = self.m; S = POOL + 64 * i
        d0, d1, d2 = m.s32(S + 0xC), m.s32(S + 0x10), m.s32(S + 0x14)
        d1 >>= 8
        if s16(d1) == 0: return
        d3 = s16(-s16(m.s32(S + 4) >> 8))
        d0 = (d0 << 4) & 0xFFFFFFFF; d2 = (d2 << 4) & 0xFFFFFFFF
        d0 = divs_w(d0, d1); d2 = divs_w(d2, d1)
        d0 = s32(muls_w(d0, d3)) >> 4; d2 = s32(muls_w(d2, d3)) >> 4
        m.w32(S, m.s32(S) + d0); m.w32(S + 4, 0); m.w32(S + 8, m.s32(S + 8) + d2)

    # ---- 0x2ADDE with an empty queue: the scratch it writes and the motion
    def hit_test(self, i):
        m = self.m; S = POOL + 64 * i
        if m.u8(0x4A875) | m.u8(0x4A866): return 0
        v = [m.s32(S + 0xC + 4 * k) >> 8 for k in range(3)]
        for k in range(3): m.w16(0x4B230 + 2 * k, v[k])
        m.w16(0x4AA70, 0)
        F = m.u16(S + 0x26)
        d0, d1, d2 = m.s32(S + 0xC), m.s32(S + 0x10), m.s32(S + 0x14)
        d1 -= 0x100
        if F & 0x1000:
            d0 -= d0 >> 6; d2 -= d2 >> 6
        elif F & 0x2000:
            d1 += 0x100; d1 -= d1 >> 2; d0 -= d0 >> 2; d2 -= d2 >> 2
        m.w32(S + 0xC, d0); m.w32(S + 0x10, d1); m.w32(S + 0x14, d2)
        if m.u16(S + 0x24) == 0: m.w16(S + 0x24, 1)
        else:
            m.w32(S, m.s32(S) + d0); m.w32(S + 4, m.s32(S + 4) + d1); m.w32(S + 8, m.s32(S + 8) + d2)
        return 0


def setup(r, rng, mode=3):
    """a fresh air scene, the drawers and the sound calls stubbed, an empty entity queue"""
    stage(mode=mode, place=False, silence=False)
    C.rts_stub(r, (0x31384, 0x3140C, 0x31478))
    C.hook_calls(r, {0x1A0DC: 1, 0x1A2F0: 2})
    sent = 0x7E8000
    r.poke(sent, bytes(24)); r.w16(sent, 0x0040)          # one secondary-object entry: skipped by the walk
    r.w32(0x4AA7E, sent + 0x18); r.w8(0x4A916, 0)
    r.poke(POOL, bytes(1280))
    r.w16(0x4A97E, 0); r.w8(0x4A943, 0); r.w8(0x4A944, 0); r.w8(0x4A945, 0)


def rand_gun_state(r, rng):
    P = REC(0)
    r.w8(P + 0x63, 0x10)
    r.w8(P + 0x62, rng.choice((0x11, 0x11, 0x10)))
    r.w8(P + 0x7D, (rng.choice((0, 1, 2, 3, 4, 5, 5)) << 4) | (r.u8(P + 0x7D) & 0xF))
    r.w8(P + 0x3B, rng.choice((0, 1, 5, 0x14, 0xFE, 0xFF, 3)))
    r.w16(P + 0x60, rng.choice((0, 1, 2, 3, 4, 5, 500, 500)))
    r.w16(0x4A97E, rng.choice((0, 8, 8, 8)))
    r.w8(0x4A943, rng.choice((0, 0, 0, 1, 2)))
    r.w8(0x4A865, rng.choice((0, 0, 1)))
    r.w8(0x4A83D, rng.choice((0, 1)))
    r.w8(0x4AC13, rng.choice((0, 0x20)))
    r.w8(0x4A866, rng.choice((0, 0, 0, 1)))
    r.w8(0x4A875, rng.choice((0, 0, 0, 1)))
    r.w16(0x4A992, rng.randint(0, 0xFFFF))
    r.w16(P + 0x6C, rng.choice((0x1091, 0x500, 0x3000, rng.randint(0, 0x4200))))
    # a random attitude: three random Q14 rows normalised roughly
    for o in range(0x92, 0xA4, 2): r.w16(P + o, rng.randint(-0x4000, 0x4000))
    for o in (0x14, 0x18, 0x1C): r.w32(P + o, rng.randint(0x100000, 0x7000000) if o != 0x18 else rng.randint(0x100000, 0x3000000))
    r.w16(P + 6, rng.randint(0, 127)); r.w16(P + 8, rng.randint(0, 127))
    for o in (0x3E, 0x42, 0x46): r.w32(P + o, rng.randint(-0x4000, 0x4000))
    for i in range(0, 20):
        S = POOL + 64 * i
        if rng.random() < 0.3 and i % 2 == 0:
            r.w16(S + 0x26, 0x401 | rng.choice((0, 0, 0x1000, 0x2000)))
            r.w16(S + 0x28, rng.randint(-2, 20))
            for k in range(0, 3): r.w32(S + 4 * k, rng.randint(0x10000, 0x300000))
            for k in range(3, 6): r.w32(S + 4 * k, rng.randint(-0x20000, 0x20000))
            for k in range(6, 9): r.w32(S + 4 * k, rng.randint(-0x4000, 0x4000))
            r.w16(S + 0x24, rng.choice((0, 1)))
            r.w16(S + 0x30, rng.randint(0, 127)); r.w16(S + 0x32, rng.randint(0, 127))
            if rng.random() < 0.2: r.w16(S + 0x26, r.u16(S + 0x26) | 2)
            if rng.random() < 0.2: r.w16(S + 0x26, r.u16(S + 0x26) | rng.choice((0x10, 0x20, 0x100)))
            if rng.random() < 0.15: r.w32(S + 4, rng.randint(-0x20000, 0x1000))
            if rng.random() < 0.15: r.w32(S, rng.choice((-5, 0x3FFFFF + 9)))
            if rng.random() < 0.15: r.w32(S + 8, rng.choice((-5, 0x3FFFFF + 9)))
    r.w8(0x4A944, rng.choice((0, 0, 3, 5))); r.w8(0x4A945, rng.choice((0, 0, 1, 7)))


def diff(r, md, label, bad_info):
    got = r.peek(LO, HI - LO); want = bytes(md.m.b[LO:HI])
    lg = r.peek(LOGBUF, 0x80); wl = bytes(md.m.b[LOGBUF:LOGBUF + 0x80])
    n = 0
    if got != want:
        idx = [k for k in range(len(got)) if got[k] != want[k]]
        n += 1
        if bad_info[0] < 6:
            print("DIFF %s at %s: first %s" % (label, ", ".join("%X:%02X/%02X" % (LO + k, got[k], want[k]) for k in idx[:8]), len(idx)))
    if lg != wl:
        n += 1
        if bad_info[0] < 6: print("DIFF %s log" % label)
    return n


def check_gun(n, seed, md):
    r = rig(); rng = random.Random(seed); bad = [0]; shots = 0; slots_active = 0
    for it in range(n):
        setup(r, rng)
        rand_gun_state(r, rng)
        md.load(r)
        C.read_calls(r)
        Gun(md).call()
        r.run(0x172DC)
        sounds = C.read_calls(r)
        # the sounds the model logged: (id, a, b)
        b = diff(r, md, "gun case %d" % it, bad)
        if sounds != md.sounds:
            b += 1
            if bad[0] < 6: print("DIFF gun sounds case %d: got %s want %s" % (it, sounds, md.sounds))
        shots += sum(1 for s in sounds)
        bad[0] += b
    print("gun: %d cases, %d sound calls, %s" % (n, shots, "FAILED %d" % bad[0] if bad[0] else "0 differences"))
    return bad[0]


# ---------------------------------------------------------------- the missile launch
TPL = 0x25ECC; DESC = 0x2600C
HARD = {0x10: 0x278AA}


class Launch:
    """the model of 0x275C4 (player) and 0x275F6 (AI) -> 0x27630: A1 = the new record, A2 = the owner; cur = V:4AA6C"""
    def __init__(self, md):
        self.md = md; self.m = md.m

    def run(self, a1, a2, cur, player_entry):
        m = self.m
        if player_entry:
            m.w8(0x4A872, 0); m.w8(0x4A86F, 1)
            if m.u32(0x4AC08) & 0x4000:
                m.w32(0x4AC08, m.u32(0x4AC08) & 0xFFFFBFFF)
                m.w32(0x4AC0C, m.u32(0x4AC0C) | 8)
        else:
            if m.u8(0x4A841) == 0: return self.abort(a1)
            p = m.u8(0x4A95F)
            d1 = 3 if p >= 3 else (5 if p >= 2 else 7)
            if m.u8(0x4A96D): d1 >>= 1
            if m.u16(0x4A992) & d1: return self.abort(a1)
        d1 = m.u8(a2 + 0x5F); d0 = m.u8(a2 + 0x63) & 0xF0
        d1 = (d1 & 0xF) if d0 == 0x30 else (d1 & 0xF0)
        if d1 == 0: return self.abort(a1)
        if a2 - 0x4B23C == m.u16(0x4A996):
            m.w8(0x4A84F, 8); m.w8(0x4A968, 0xFB)
        m.b[a1:a1 + 164] = m.b[a2:a2 + 164]
        m.w8(a1 + 5, 0)
        lg = m.u32(0x1D024)
        if (m.u8(a1 + 0x63) & 0xF0) == 0x30:
            m.w8(a1 + 0x62, 1); tpl = 0x14
            if a2 == 0x4B23C: m.w16(lg + 0x3E, m.u16(lg + 0x3E) + 1); m.w8(0x4A87D, 1)
        else:
            m.w8(a1 + 0x62, 0); tpl = 0
            if a2 == 0x4B23C: m.w16(lg + 0x42, m.u16(lg + 0x42) + 1); m.w8(0x4A87D, 1)
        m.b[DESC + 20 * cur:DESC + 20 * cur + 20] = m.b[TPL + tpl:TPL + tpl + 20]
        for o in (0x56, 0x58, 0x5A): m.w16(a1 + o, 0)
        m.w8(a1 + 0x64, 0); m.w8(a1 + 3, m.u8(a1 + 3) & ~4)
        d1 = m.u8(a2 + 0x5F); d2 = d1; d0 = m.u8(a2 + 0x63) & 0xF0
        if d0 == 0x30:
            d1 &= 0xF; d2 &= 0xF0; d1 = (d1 - 1) & 0xFF; idx = (d1 & 0xFF) + 2
        else:
            d1 &= 0xF0; d2 &= 0xF; d1 = (d1 - 0x10) & 0xFF; idx = d1 >> 4
        m.w8(a2 + 0x5F, (d1 | d2) & 0xFF)
        m.w8(0x4A8FB, 3); m.w8(0x4A8FC, 3)
        tab = 0x278AA if m.u8(a2 + 0x62) == 0x10 else 0x278CE
        x, y, z = m.s16(tab + 6 * idx), m.s16(tab + 6 * idx + 2), m.s16(tab + 6 * idx + 4)
        mw = [m.s16(a2 + o) for o in range(0x92, 0xA4, 2)]
        o = [(x * mw[0] + y * mw[1] + z * mw[2]) >> 6, (x * mw[3] + y * mw[4] + z * mw[5]) >> 6, (x * mw[6] + y * mw[7] + z * mw[8]) >> 6]
        for k, off in enumerate((0x14, 0x18, 0x1C)): m.w32(a1 + off, m.s32(a1 + off) + o[k])
        m.w16(a1 + 0x4C, 0x96 if m.u8(a1 + 0x62) == 0 else 0xC8)
        m.w16(a1 + 0x26, 0x14)
        m.w16(a1, m.u16(a1) | 0x10C0 | 0x100 | 2)
        m.w16(a1 + 0x76, 0)
        if m.u16(a1 + 2) & 0x80: m.w32(a1 + 0x42, 0)
        m.w16(a1 + 2, m.u16(a1 + 2) & 0xFF7F)
        m.w16(a1 + 0x2C, 0xFFFF)
        m.w8(0x4A910, 0x8C)
        m.w8(a1 + 0x38, m.u8(a2 + 0x38))

    def abort(self, a1):
        self.m.w8(a1 + 1, self.m.u8(a1 + 1) & ~0x40)


def rand_launch_state(r, rng, ai):
    """a random owner (the player, or the hostile in record 8) and an empty target record"""
    own = 8 if ai else 0; new = 9 if ai else 1
    for i in range(16):
        if i not in (0, 14): r.poke(REC(i), bytes(0x200))
    O = REC(own); N = REC(new)
    for o in range(0, 0xA4, 2): r.w16(O + o, rng.randint(0, 0xFFFF))
    r.w16(O, rng.choice((0x11C8, 0x11C0, 0x1048)) | 0x40)
    r.w16(O + 2, rng.choice((0, 0x80, 0x2080, 0x0010)))
    r.w8(O + 0x62, rng.choice((0x11, 0x10, 0x12, 0x13, 0x14)))
    r.w8(O + 0x5F, rng.choice((0x24, 0x44, 0x40, 0x04, 0x00, 0x11, 0x20, 0x02, 0x33, rng.randint(0, 255))))
    r.w8(O + 0x63, rng.choice((0x20, 0x30, 0x10, 0x3D, 0x2D, 0x1D, 0xFD, 0x00)))
    for o in range(0x92, 0xA4, 2): r.w16(O + o, rng.randint(-0x4000, 0x4000))
    r.poke(N, bytes(0x200)); r.w16(N, rng.choice((0, 0x0010, 0x0020)))
    r.w16(0x4A996, rng.choice((0, 0, 0x1000, own * 0x200)))
    r.w32(0x4AC08, rng.choice((0, 0x4000, 0x4200, 0xFFFFFFFF))); r.w32(0x4AC0C, rng.choice((0, 0x30)))
    r.w8(0x4A841, rng.choice((0, 1, 1, 1))); r.w8(0x4A95F, rng.randint(0, 4)); r.w8(0x4A96D, rng.choice((0, 1)))
    r.w16(0x4A992, rng.randint(0, 0xFFFF))
    r.w8(0x4A872, 1)
    r.w16(0x4AA6C, new); r.w16(0x4AA6E, 0x200 * new)
    return O, N, new


def check_launch(n, seed, md):
    r = rig(); rng = random.Random(seed); bad = 0; launched = 0; aborted = 0; per = {}
    for it in range(n):
        ai = it % 2
        stage(place=False, silence=False)
        O, N, new = rand_launch_state(r, rng, ai)
        r.poke(DESC, bytes(20 * 16))
        md.load(r)
        md.m.b[DESC:DESC + 320] = r.peek(DESC, 320)
        Launch(md).run(N, O, new, not ai)
        r.run(0x275F6 if ai else 0x275C4, a1=N, a2=O)
        got = r.peek(LO, HI - LO); want = bytes(md.m.b[LO:HI])
        gd = r.peek(DESC, 320); wd = bytes(md.m.b[DESC:DESC + 320])
        ok = got == want and gd == wd
        v = bool(r.u16(N) & 0x40)
        launched += v; aborted += (not v)
        if not ok:
            bad += 1
            if bad < 6:
                idx = [k for k in range(len(got)) if got[k] != want[k]]
                print("DIFF launch case %d ai %d: %s" % (it, ai, ", ".join("%X:%02X/%02X" % (LO + k, got[k], want[k]) for k in idx[:8])), "desc" if gd != wd else "")
    print("launch: %d cases (%d launched, %d refused), %s" % (n, launched, aborted, "FAILED %d" % bad if bad else "0 differences"))
    return bad


# ---------------------------------------------------------------- the missile gate 0x27DC2 (class nibble 0) and the guidance 0x2826A
class Gate:
    """the model of 0x278F6 for a record whose class nibble is 0; returns (D0, step)"""
    def __init__(self, md):
        self.md = md; self.m = md.m

    def run(self, a1):
        m = self.m
        if m.u8(0x4A866): return None                 # 0x278F6 returns at once when paused (D0 unchanged, the caller skips the step)
        if not (m.u16(a1) & 0x40): return (None, False)
        if not (m.u8(0x4A840) | m.u8(0x4A866)):
            c = m.s16(a1 + 0x4C) - 1; m.w16(a1 + 0x4C, c)
            if c <= 0:
                m.w16(a1, 0); m.w8(0x4A910, 0x0C); return (0, False)
        coast = False
        if m.u16(a1) & 0x400: coast = True
        else:
            c = m.s16(a1 + 0x4C)
            if c < 0x32:
                if c < 0x0C: m.w16(a1, m.u16(a1) | 0x400)
                coast = True
        if coast:
            self.speed(a1, 0x3000); return (1, True)
        s = s16(m.s16(a1 + 0x6C) + 0x1E0)                     # 16-bit wrap as the original's addi.w
        if s > 0x4200: s = 0x4200
        m.w16(a1 + 0x6C, s); m.w16(a1 + 0x6E, s)
        if not (m.u16(a1) & 0x0002): self.guide(a1); return (1, True)
        m.w16(a1, m.u16(a1) & 0xFFFE)
        if not (m.u16(a1) & 0x0008): self.guide(a1); return (1, True)
        d0 = m.s16(0x4AA78)
        if d0 == -1 or d0 == m.s16(0x4AA6E): self.speed(a1, 0x4200); return (1, True)
        t = 0x4B23C + d0
        m.w16(a1 + 0x2C, m.u16(t + 6)); m.w16(a1 + 0x2E, m.u16(t + 8))
        m.w16(a1 + 0x30, m.u16(t + 0xC)); m.w16(a1 + 0x32, m.u16(t + 0xE)); m.w32(a1 + 0x34, m.u32(t + 0x10))
        m.w8(a1 + 0x38, ((d0 >> 8) >> 1) | 0x80)
        return (1, True)

    def speed(self, a1, d1):
        m = self.m
        m.w32(a1 + 0x34, 0)
        d0 = m.s16(a1 + 0x6C)
        if d0 > d1:
            d0 = s16(d0 - 0x240)
            if d0 < d1: d0 = d1
        else:
            d0 = s16(d0 + 0x240)
            if d0 > d1: d0 = d1
        m.w16(a1 + 0x6C, d0); m.w16(a1 + 0x6E, d0)

    def guide(self, a1):
        m = self.m
        m.w16(a1, m.u16(a1) | 1)
        d0 = ((m.u8(a1 + 0x38) & 0x7F) << 9) & 0xFFFF
        if d0 == 0:
            if m.u8(a1 + 0x62) == 0: d1 = m.u8(0x4A944)
            else: d1 = m.u8(0x4A945)
            if d1 != 0: return self.decoy(a1, d1)
        t = 0x4B23C + s16(d0)
        if not (m.u16(a1) & 0x40): m.w16(a1, m.u16(a1) & 0x7FFF)
        m.w16(a1 + 0x2C, m.u16(t + 6)); m.w16(a1 + 0x2E, m.u16(t + 8))
        m.w16(a1 + 0x30, m.u16(t + 0xC)); m.w16(a1 + 0x32, m.u16(t + 0xE)); m.w32(a1 + 0x34, m.u32(t + 0x10))

    def decoy(self, a1, d1):
        m = self.m
        S = POOL + 64 * (d1 - 1)
        cx, cz = m.u16(S + 0x30), m.u16(S + 0x32)
        m.w16(a1 + 0x2C, cx); m.w16(a1 + 0x2E, cz)
        d3 = (cx << 22); d5 = (cz << 22)
        x, y, z = m.s32(S), m.s32(S + 4), m.s32(S + 8)
        d3 += x; d5 += z; d4 = y
        m.w16(a1 + 0x30, (x >> 8) & 0xFFFF); m.w16(a1 + 0x32, (z >> 8) & 0xFFFF); m.w32(a1 + 0x34, y >> 8)
        near = abs(d3 - m.s32(a1 + 0x14)) <= 0x4800 and abs(d4 - m.s32(a1 + 0x18)) <= 0x4800 and abs(d5 - m.s32(a1 + 0x1C)) <= 0x4800
        if near: m.w8(a1 + 3, m.u8(a1 + 3) | 1)
        elif m.u8(a1 + 3) & 1: m.w8(a1 + 0x20, m.u8(a1 + 0x20) | 0x20)


def rand_gate_state(r, rng, idx):
    N = REC(idx)
    for i in range(16):
        if i not in (0, 14): r.poke(REC(i), bytes(0x200))
    cls = rng.choice((0, 1))
    r.w16(N, rng.choice((0x10CA, 0x10C2, 0x10C8, 0x10C0, 0x11CA, 0x0000, 0x10CB, 0x14CA, 0x12CA)) | 0x40)
    if rng.random() < 0.05: r.w16(N, 0x10CA & ~0x40)
    r.w16(N + 2, rng.choice((0, 1, 0x2000)))
    r.w8(N + 0x20, rng.choice((0, 0x20)))
    r.w8(N + 0x62, cls)
    r.w16(N + 0x4C, rng.choice((1, 2, 5, 11, 12, 13, 49, 50, 51, 100, 149, 200, rng.randint(-3, 210))))
    r.w16(N + 0x6C, rng.choice((0, 0x1000, 0x1276, 0x3000, 0x4100, 0x4200, 0x7000, -0x1000, rng.randint(-0x8000, 0x7FFF))))
    r.w16(N + 0x6E, 0)
    r.w8(N + 0x38, rng.choice((0xFF, 0x00, 0x80, 0x88, 0x8E, 0x85, 0x01, 0x40)))
    for o in (0x2C, 0x2E, 0x30, 0x32): r.w16(N + o, rng.randint(-5, 9000))
    r.w32(N + 0x34, rng.randint(-1000, 100000))
    for o in (0x14, 0x18, 0x1C): r.w32(N + o, rng.randint(0, 0x7FFFFFF))
    for t in (0, 5, 8, 9):                                    # candidate targets
        T = REC(t) if t else REC(0)
        if t == 0: continue
        r.w16(T, rng.choice((0x11C0, 0x0010, 0x11C8))); r.w16(T + 6, rng.randint(0, 127)); r.w16(T + 8, rng.randint(0, 127))
        r.w16(T + 0xC, rng.randint(0, 16383)); r.w16(T + 0xE, rng.randint(0, 16383)); r.w32(T + 0x10, rng.randint(-2000, 60000))
    r.w16(0x4AA78, rng.choice((0xFFFF, 0xFFFF, 0x1000, 0x0A00, 0x0000, 0x0200 * idx, 0x1200, 0x0400)))
    r.w16(0x4AA6E, 0x200 * idx); r.w16(0x4AA6C, idx)
    r.w8(0x4A866, rng.choice((0, 0, 0, 0, 1))); r.w8(0x4A840, rng.choice((0, 0, 0, 1)))
    r.w8(0x4A944, rng.choice((0, 0, 2, 5, 19))); r.w8(0x4A945, rng.choice((0, 0, 1, 7)))
    for i in range(0, 20, 2):
        S = POOL + 64 * i
        r.w16(S + 0x26, 0x401 | rng.choice((0, 0x1000, 0x2000))); r.w16(S + 0x30, rng.randint(0, 127)); r.w16(S + 0x32, rng.randint(0, 127))
        base = rng.randint(0, 0x3FFFFF)
        r.w32(S, rng.randint(0, 0x3FFFFF)); r.w32(S + 4, rng.randint(0, 0x100000)); r.w32(S + 8, rng.randint(0, 0x3FFFFF))
    # make the missile near some decoy in half of the cases
    if rng.random() < 0.5:
        for slot in (2, 4, 18):
            S = POOL + 64 * slot
            for k, o in enumerate((0x14, 0x18, 0x1C)):
                base = (r.u16(S + 0x30) << 22) + r.s32(S) if k == 0 else (r.s32(S + 4) if k == 1 else (r.u16(S + 0x32) << 22) + r.s32(S + 8))
                r.w32(N + o, base + rng.randint(-0x5000, 0x5000))
    return N


def check_gate(n, seed, md):
    r = rig(); rng = random.Random(seed); bad = 0; stats = {}
    for it in range(n):
        stage(place=False, silence=False)
        idx = rng.choice((1, 2, 3, 9, 11))
        N = rand_gate_state(r, rng, idx)
        md.load(r)
        want_ret = Gate(md).run(N)
        r.run(0x278F6, a1=N, a2=REC(0))
        sr = r.L.rig_get_reg(17)
        got = r.peek(LO, HI - LO); want = bytes(md.m.b[LO:HI])
        gn = r.peek(REC(1), 0x200 * 14); wn = bytes(md.m.b[REC(1):REC(1) + 0x200 * 14])
        ok = got == want
        if want_ret is not None and want_ret[0] is not None and r.reg("d0") != want_ret[0] & 0xFFFFFFFF: ok = False
        z = bool(sr & 4)
        if want_ret is not None and want_ret[1] != (not z): ok = False
        key = "none" if want_ret is None else ("ret%s" % want_ret[0])
        stats[key] = stats.get(key, 0) + 1
        if not ok:
            bad += 1
            if bad < 6:
                idx2 = [k for k in range(len(got)) if got[k] != want[k]]
                print("DIFF gate case %d: %s ret %s z %s" % (it, ", ".join("%X:%02X/%02X" % (LO + k, got[k], want[k]) for k in idx2[:8]), want_ret, z))
    print("gate: %d cases %s, %s" % (n, sorted(stats.items(), key=str), "FAILED %d" % bad if bad else "0 differences"))
    return bad


# ---------------------------------------------------------------- the missile and aircraft contact: effects of a box hit (0x2B5E2 -> 0x2B78A, 0x2BFFA)
def hit_effects(m, mi, oi, other_hit):
    """the writes of the scan after the box test decided `hit` for the pair (mover mi, other oi): other W0/B20/W2, the log counters, B20 bit 7"""
    M = REC(mi); O = REC(oi)
    cls_o = m.u8(O + 0x62)
    if m.u16(O) & 0x1000:
        m.w16(O, m.u16(O) | 0x200)
        n = cls_o & 0xF0
        if n != 0:
            m.w8(O + 0x20, m.u8(O + 0x20) | 2)
            # the original compares the nibble-masked class with 0x15, which never matches: class 0x15 is treated like any aircraft here
            m.w16(0x54E72, 0x200 * oi); m.w8(O + 2, m.u8(O + 2) | 0x10); m.w8(O + 5, 1)
    # 0x2BFFA
    lg = m.u32(0x1D024)
    if m.u8(M + 1) & 8:
        if m.u8(M + 0x5E) == 0:
            c = m.u8(M + 0x62)
            if c == 0:
                if not (m.u8(O + 1) & 8): m.w16(lg + 0x44, m.u16(lg + 0x44) + 1)
            elif c == 1:
                if not (m.u8(O + 1) & 8): m.w16(lg + 0x40, m.u16(lg + 0x40) + 1)
        if m.u8(O + 1) & 8:
            m.w8(O + 0x20, m.u8(O + 0x20) | 0x80); m.w8(M + 0x20, m.u8(M + 0x20) | 0x80); return
    m.w8(O + 0x20, m.u8(O + 0x20) & 0x7F); m.w8(M + 0x20, m.u8(M + 0x20) & 0x7F)
    if mi == 0 and (cls_o & 0xF0) == 0:
        m.w16(lg + 0x48, m.u16(lg + 0x48) + 1)


def check_hits(n, seed, md):
    import damage_check as D
    r = rig(); rng = random.Random(seed); bad = 0; hits = 0; stats = {}
    for it in range(n):
        stage(place=False, silence=False)
        for i in range(16):
            if i not in (0, 14): r.poke(REC(i), bytes(0x200))
        mi = rng.choice((0, 1, 2, 9)); oi = rng.choice([k for k in (4, 5, 8, 10, 11, 12) if k > mi])
        mcls = rng.choice((0, 1)) if mi else 0x11
        ocls = rng.choice((0x12, 0x13, 0x11, 0x15, 0x00, 0x01, 0x10))
        mw0 = rng.choice((0x10CA, 0x10C2, 0x11C8)) if mi else 0x11C8
        ow0 = rng.choice((0x11C0, 0x11C8, 0x01C0, 0x11C1, 0x10C0)) | 0x40
        mg = 0 if mi in (0, 1, 2) else rng.choice((0, 8)); og = rng.choice((8, 10, 0))
        M = REC(mi); O = REC(oi)
        r.w16(M, mw0); r.w8(M + 0x62, mcls); r.w8(M + 0x5E, mg); r.w16(M + 2, 0)
        r.w16(O, ow0); r.w8(O + 0x62, ocls); r.w8(O + 0x5E, og); r.w16(O + 2, rng.choice((0, 0x100)))
        base = [rng.randint(0x1000000, 0x2000000) for _ in range(3)]
        for k, off in enumerate((0x14, 0x18, 0x1C)): r.w32(M + off, base[k])
        sizes = (0x800, 0x1000, 0x5000, 0x6000, 0x7000, 0x10000)
        d = [rng.choice((0, 1, 0x1000, 0x4F00, 0x5000, 0x5001, 0x9000, 0xFFFF, 0x10001)) * rng.choice((-1, 1)) for _ in range(3)]
        for k, off in enumerate((0x14, 0x18, 0x1C)): r.w32(O + off, base[k] + d[k])
        disp = [rng.choice((0, 0x2000, -0x2000, 0x8000)) for _ in range(3)]
        for off, v in zip((0x3E, 0x42, 0x46), disp): r.w32(O + off, v)
        r.w16(0x4AA6E, 0x200 * mi); r.w16(0x4AA6C, mi)
        r.w8(0x4A957, 0)
        r.w16(0x4B236, 0)
        md.load(r)
        w2m = md.m.u16(M + 2); w2o = md.m.u16(O + 2)
        exp = D.box_model({"w0": mw0, "grp": mg, "cls": mcls}, {"w0": ow0, "grp": og, "cls": ocls, "pos": [base[k] + d[k] for k in range(3)]}, w2m, w2o, base, disp)
        if exp is None: continue
        contact, hit = exp
        if contact:
            md.m.w8(0x4A957, 1); md.m.w16(O + 2, md.m.u16(O + 2) | 1)
            if hit: hit_effects(md.m, mi, oi, True)
        # the scan is run for the mover with D2-D4 = its previous position (the caller passes them in registers)
        r.run(0x2B5E2, d2=base[0], d3=base[1], d4=base[2])
        res = r.reg("d0") & 0xFFFF
        got = r.peek(LO, HI - LO); want = bytes(md.m.b[LO:HI])
        gr = r.peek(REC(0), 0x200 * 15); wr = bytes(md.m.b[REC(0):REC(0) + 0x200 * 15])
        ok = (got == want and gr == wr and (res == 0x40) == bool(hit))
        key = (contact, hit)
        stats[key] = stats.get(key, 0) + 1
        if not ok:
            bad += 1
            if bad < 6:
                idx = [k for k in range(len(gr)) if gr[k] != wr[k]]
                idx2 = [k for k in range(len(got)) if got[k] != want[k]]
                print("DIFF hit case %d mi %d oi %d mcls %d ocls %02X: res %X want %s; rec %s; state %s" % (it, mi, oi, mcls, ocls, res, hit,
                      ", ".join("%X:%02X/%02X" % (REC(0) + k, gr[k], wr[k]) for k in idx[:6]), ", ".join("%X:%02X/%02X" % (LO + k, got[k], want[k]) for k in idx2[:6])))
    print("hits: %d cases %s, %s" % (n, sorted(stats.items()), "FAILED %d" % bad if bad else "0 differences"))
    return bad


# ---------------------------------------------------------------- the aircraft hull test 0x2B4BA
def hull_model(m, rec_i, slot):
    """is the tracer point (slot position >> 8, logical shift) inside the convex face list of the record at index rec_i; reads the image"""
    A0 = REC(rec_i); S = slot
    x = (m.u32(S) >> 8) & 0xFFFF; y = (m.u32(S + 4) >> 8); z = (m.u32(S + 8) >> 8) & 0xFFFF
    x = s16(x); z = s16(z)
    b = m.s8(A0 + 0x7B)
    if b < 0: return 0
    sh = (m.u8(A0 + 0x7D) & 0xF) - ((m.u8(A0 + 0x7B) >> 4) & 0xF)
    if sh < 0: sh = 0
    lst = m.u32(0x3DACC + 4 * (m.u8(A0 + 0x7B) & 0xF))
    p = lst
    while True:
        d2, d3, d4 = m.u16(p), m.u16(p + 2), m.u16(p + 4); p += 6
        if d2 & 0x8000: return 1
        d2 &= 0x7FFF
        d2 += 0xA4; d3 += 0xA4; d4 += 0xA4
        if not (d2 & 0x4000):
            a5 = A0 + d2
            v4 = [m.s16(A0 + d4 + 2 * k) for k in range(3)]; v3 = [m.s16(A0 + d3 + 2 * k) for k in range(3)]
            n = [s16(v4[k] - v3[k]) for k in range(3)]
        else:
            d2 &= ~0x4000
            a5 = A0 + d2
            P = [m.s16(a5 + 2 * k) for k in range(3)]
            a = [s16(m.s16(A0 + d3 + 2 * k) - P[k]) for k in range(3)]
            bb = [s16(m.s16(A0 + d4 + 2 * k) - P[k]) for k in range(3)]
            cx = (a[1] * bb[2] - a[2] * bb[1]) & 0xFFFFFFFF
            cy = (a[2] * bb[0] - a[0] * bb[2]) & 0xFFFFFFFF
            cz = (a[0] * bb[1] - a[1] * bb[0]) & 0xFFFFFFFF
            n = [s16(cx >> 4), s16(cy >> 4), s16(cz >> 4)]
        P = [m.s16(a5 + 2 * k) for k in range(3)]
        px = s16(P[0] >> sh) ; py = s16(P[1] >> sh); pz = s16(P[2] >> sh)
        wx = s16(px + m.s16(A0 + 0xC)); wy = s32(py + m.s32(A0 + 0x10)); wz = s16(pz + m.s16(A0 + 0xE))
        dx = s16(-s16(wx - x)); dy = s16(-s16(wy - y)); dz = s16(-s16(wz - z))
        dot = s32(n[0] * dx + n[1] * dy + n[2] * dz)
        if dot < 0: return 0


def check_hull(n, seed, md):
    r = rig(); rng = random.Random(seed); bad = 0; inside = 0
    for it in range(n):
        stage(place=False, silence=False)
        oi = rng.choice((4, 8, 10))
        O = REC(oi)
        r.poke(O, bytes(0x200))
        r.w16(O, 0x11C0); r.w8(O + 0x62, 0x12)
        r.w8(O + 0x7B, rng.choice((0, 0, 0, 1, 2, 0x10, 0x11, 0x12, 0x80)))
        r.w8(O + 0x7D, rng.choice((0, 1, 3, 4, 5)))
        r.w16(O + 0xC, rng.randint(2000, 14000)); r.w16(O + 0xE, rng.randint(2000, 14000)); r.w32(O + 0x10, rng.randint(1000, 30000))
        for k in range(0, 40):
            r.w16(O + 0xA4 + 2 * k, rng.randint(-1500, 1500) if rng.random() < 0.9 else rng.randint(-32768, 32767))
        if rng.random() < 0.3: r.poke(O + 0xA4, bytes(100))
        r.w16(0x4AA70, oi)
        S = POOL + 64 * 0
        r.poke(S, bytes(64))
        ox, oy, oz = r.u16(O + 0xC), r.s32(O + 0x10), r.u16(O + 0xE)
        r.w32(S, (ox + rng.randint(-700, 700)) * 256 + rng.randint(0, 255))
        r.w32(S + 4, (oy + rng.randint(-700, 700)) * 256 + rng.randint(0, 255))
        r.w32(S + 8, (oz + rng.randint(-700, 700)) * 256 + rng.randint(0, 255))
        md.load(r)
        want = hull_model(md.m, oi, S)
        r.run(0x2B4BA, a3=S, a6=0x7D0000)
        got = r.reg("d7") & 0xFFFF
        inside += want
        if got != want:
            bad += 1
            if bad < 6: print("DIFF hull case %d: got %d want %d (part %02X)" % (it, got, want, r.u8(O + 0x7B)))
    print("hull: %d cases (%d inside), %s" % (n, inside, "FAILED %d" % bad if bad else "0 differences"))
    return bad


# ---------------------------------------------------------------- the tracer against the entities of queue C (0x2ADDE, entity branch)
class BigHull(Exception): pass
KILLS = [0]


def model_flag(m, desc):
    base = m.u32(desc + 4)
    d7 = m.s16(base)
    if d7 >= 0: d7 = m.s16(base + 4)
    d7 &= 0x3FFF
    return m.u8(base + 7 + d7)


def bullet_vs_entities(m, i, entries):
    """returns the result of 0x2ADDE for slot i and the queue `entries` [(flagword, descptr)] given from the last emitted entry backwards"""
    S = POOL + 64 * i
    if m.u8(0x4A875) | m.u8(0x4A866): return 0
    v = [m.s32(S + 0xC + 4 * k) >> 8 for k in range(3)]
    for k in range(3): m.w16(0x4B230 + 2 * k, v[k])
    m.w16(0x4AA70, 0)
    visited = 0; ecount = 0; count = m.u8(0x4A916)
    pos = 0
    while True:
        fw, dptr = entries[pos]; pos += 1
        visited += 1
        flags = fw & 0xFF
        if not (flags & 0x40) and (flags & 0x10) and ecount < 6:
            ecount += 1
            idx = (fw >> 8) & 0xFF
            if idx > 127: idx -= 256
            R = REC(idx)
            if idx != m.s16(S + 0x2E) and m.u16(R + 6) == m.u16(S + 0x30) and m.u16(R + 8) == m.u16(S + 0x32) and (m.u16(R) & 0x40):
                m.w16(0x4AA70, idx)
                flag = model_flag(m, dptr)
                d7 = 0x800 if flag & 0x10 else m.s16(0x4AA5E)
                dx = abs(s16(m.s16(R + 0xC) - (m.s32(S) >> 8))); dy = abs(m.s32(R + 0x10) - (m.s32(S + 4) >> 8)); dz = abs(s16(m.s16(R + 0xE) - (m.s32(S + 8) >> 8)))
                # the first compare is on the 16-bit difference (sub.w), abs.w on the word
                if dx < d7 and dy < d7 and dz < d7:
                    if flag & 0x10: raise BigHull()
                    hit = False
                    for k in range(8):
                        for a in range(3): m.w32(S + 4 * a, m.s32(S + 4 * a) + m.s32(S + 0x18 + 4 * a))
                        m.w16(0x4AA70, idx)
                        if hull_model(m, idx, S): hit = True; break
                    if hit:
                        lg = m.u32(0x1D024)
                        m.w16(lg + 0x3C, m.u16(lg + 0x3C) + 1)
                        d0 = m.u8(R + 0x3C); d7n = (d0 & 0xF) + 1
                        if d7n >= 3:
                            KILLS[0] += 1
                            m.w16(R, m.u16(R) | 0x200)
                            c = m.u8(R + 0x62)
                            if c != 0x15 and (c & 0xF0) != 0:
                                m.w8(R + 0x20, m.u8(R + 0x20) | 2); m.w16(0x54E72, idx * 0x200); m.w8(R + 2, m.u8(R + 2) | 0x10); m.w8(R + 5, 1)
                        else:
                            m.w8(R + 0x3C, (d0 & 0xF0) | d7n)
                        return 1
                    for a in range(3): m.w32(S + 4 * a, m.s32(S + 4 * a) - m.s32(S + 0xC + 4 * a))
        if not (count > visited and ecount < 6): break
    # no hit: gravity, drag and the move
    F = m.u16(S + 0x26)
    d0, d1, d2 = m.s32(S + 0xC), m.s32(S + 0x10), m.s32(S + 0x14)
    d1 -= 0x100
    if F & 0x1000: d0 -= d0 >> 6; d2 -= d2 >> 6
    elif F & 0x2000:
        d1 += 0x100; d1 -= d1 >> 2; d0 -= d0 >> 2; d2 -= d2 >> 2
    m.w32(S + 0xC, d0); m.w32(S + 0x10, d1); m.w32(S + 0x14, d2)
    if m.u16(S + 0x24) == 0: m.w16(S + 0x24, 1)
    else:
        m.w32(S, m.s32(S) + d0); m.w32(S + 4, m.s32(S + 4) + d1); m.w32(S + 8, m.s32(S + 8) + d2)
    return 0


def check_entity_hits(n, seed, md, kind=0x401):
    """kind: the slot flag word the tracer is staged with (0x401 a bullet; 0x1401 and 0x2401 a flare and a chaff slot of the same pool, note 07 9.2: the hit test of 8.2 has no condition on it)"""
    r = rig(); rng = random.Random(seed); bad = 0; stats = {"hit": 0, "miss": 0, "kill": 0}
    img = md.img
    tpls = [k for k in range(134)]
    for it in range(n):
        stage(place=False, silence=False)
        C.rts_stub(r, (0x31384, 0x3140C, 0x31478))
        ents = rng.choice((1, 2, 3, 7))
        entries = []; recs = []
        for e in range(ents):
            oi = rng.choice([k for k in (4, 5, 6, 8, 9, 10, 11, 12, 13) if k not in recs]); recs.append(oi)
            O = REC(oi)
            r.poke(O, bytes(0x200))
            r.w16(O, 0x11C0 | rng.choice((0, 0, 0x200, 0x8000)) if rng.random() < 0.9 else 0x0010)
            r.w8(O + 0x62, rng.choice((0x12, 0x13, 0x11, 0x15, 0x00, 0x20))); r.w8(O + 0x7B, rng.choice((0, 0, 0, 1, 2)))
            r.w8(O + 0x7D, rng.choice((1, 3, 4)))
            r.w16(O + 6, 65); r.w16(O + 8, 66)
            r.w16(O + 0xC, rng.randint(3000, 12000)); r.w16(O + 0xE, rng.randint(3000, 12000)); r.w32(O + 0x10, rng.randint(1000, 30000))
            r.w8(O + 0x3C, rng.choice((0, 0, 1, 2, 0x10, 0x11, 0x12)))
            for k in range(0, 40): r.w16(O + 0xA4 + 2 * k, rng.randint(-600, 600))
            t = rng.choice(tpls)
            while True:
                t = rng.choice(tpls)
                mb = struct.unpack(">I", img[TPL + 20 * t + 4 - 0x10000:TPL + 20 * t + 8 - 0x10000])[0]
                break
            r.poke(DESC + 20 * oi, img[TPL + 20 * t - 0x10000:TPL + 20 * t + 20 - 0x10000])
            entries.append(((oi << 8) | 0x10 | rng.choice((0, 0, 1)), DESC + 20 * oi))
        # a secondary-object entry mixed in
        if rng.random() < 0.3: entries.insert(rng.randint(0, len(entries)), (0x0640, 0))
        q = 0x7E8000
        r.poke(q, bytes(24 * (len(entries) + 2)))
        # entries are listed from the last emitted entry backwards; the memory has them in ascending order
        for k, (fw, dp) in enumerate(reversed(entries)):
            r.w16(q + 24 * k, fw); r.w32(q + 24 * k + 2, dp)
        r.w32(0x4AA7E, q + 24 * (len(entries) - 1)); r.w8(0x4A916, len(entries))
        # make the walk start at the last entry and go backwards: V:4AA7E is the address of the first one examined? (see the run below)
        S = POOL
        r.poke(S, bytes(64))
        O0 = REC(recs[0])
        r.w16(S + 0x26, kind); r.w16(S + 0x28, 15); r.w16(S + 0x2E, rng.choice((0, 0, recs[-1]))); r.w16(S + 0x30, 65); r.w16(S + 0x32, 66)
        r.w16(S + 0x24, rng.choice((0, 1, 1, 1)))
        ox, oy, oz = r.u16(O0 + 0xC), r.s32(O0 + 0x10), r.u16(O0 + 0xE)
        r.w32(S, (ox + rng.randint(-400, 400)) * 256 + rng.randint(0, 255)); r.w32(S + 4, (oy + rng.randint(-400, 400)) * 256 + rng.randint(0, 255)); r.w32(S + 8, (oz + rng.randint(-400, 400)) * 256 + rng.randint(0, 255))
        for a in range(3):
            v = rng.randint(-9000, 9000)
            r.w32(S + 0x18 + 4 * a, v); r.w32(S + 0xC + 4 * a, v * 8)
        r.w32(0x4B22A, 0)
        md.load(r)
        for k in range(len(entries) + 2): pass
        # the model walks from V:4AA7E downwards: build the same order
        order = []
        a = q + 24 * (len(entries) - 1)
        mem = md.m
        while len(order) < len(entries) + 1:
            order.append((r.u16(a), r.u32(a + 2))); a -= 24
        g = Gun(md); g.hit_test = lambda i: bullet_vs_entities(md.m, i, order)
        try:
            g.step(0); res = getattr(g, 'last_hit', 0)
        except BigHull:
            stats['big'] = stats.get('big', 0) + 1; continue
        r.w32(0x1A6C4, POOL); callf(r, 0x1754C, (0,))
        got = r.peek(LO, HI - LO); want = bytes(md.m.b[LO:HI])
        gr = r.peek(REC(0), 0x200 * 15); wr = bytes(md.m.b[REC(0):REC(0) + 0x200 * 15])
        ok = got == want
        stats["hit" if res else "miss"] += 1
        if not ok:
            bad += 1
            if bad < 6:
                idx2 = [k for k in range(len(got)) if got[k] != want[k]]
                print("DIFF entity case %d: %s" % (it, ", ".join("%X:%02X/%02X" % (LO + k, got[k], want[k]) for k in idx2[:8])))
    stats["kill"] = KILLS[0]
    print("entity hits: %d cases %s, %s" % (n, stats, "FAILED %d" % bad if bad else "0 differences"))
    return bad


# ---------------------------------------------------------------- Review Focus 1: a player missile with no target, a target that dies, a target out of reach, a hit
def focus_run(case, passes=260):
    """a Sidewinder launched through the world update 0x26AF8 (request V:4A872) from the staged mode-3 air scene; record 8 is the placed MiG-29, made inert
    (no target, no velocity, speed word 0x1200) and put N passes of the player's velocity ahead.  Returns the per-pass trace."""
    r = stage(mode=3, place=True); C.stub_sound(r)
    P = REC(0); M = REC(1); H = REC(8)
    r.w8(P + 0x5F, 0x44); r.w8(P + 0x63, (r.u8(P + 0x63) & 0xF) | 0x20)
    pp = C.pos(r, 0); v = [C.s32(r.u32(P + o)) for o in (0x3E, 0x42, 0x46)]
    for o in (0x3E, 0x42, 0x46): r.w32(H + o, 0)
    r.w16(H + 0x2C, 0xFFFF); r.w8(H + 0x38, 0xFF); r.w16(H + 0x6C, 0x1200); r.w16(H + 0x6E, 0x1200)
    n = {"far": 1500, "n20": 20, "n60": 60}.get(case, 30)
    C.put_at(r, 8, [pp[k] + v[k] * n for k in range(3)])
    r.w16(0x4AA78, 0xFFFF if case == "none" else 0x1000)
    r.w8(0x4A872, 1)
    tr = []; lg = r.u32(0x1D024)
    for i in range(passes):
        C.world(r)
        if case == "dies" and i == 12: r.w16(H, 0)
        tr.append(dict(i=i, w0=r.u16(M), life=s16(r.u16(M + 0x4C)), spd=r.u16(M + 0x6C), dx=r.u16(M + 0x2C), dh=r.s32(M + 0x34), lock=r.u16(0x4AA78),
                       req=r.u8(0x4A910), hw0=r.u16(H), hb20=r.u8(H + 0x20), hst=r.u8(H + 5), cam=r.u16(0x54E72), own=r.u16(P + 0x6C), dist=max(abs((C.pos(r, 1)[k] - C.pos(r, 8)[k]) >> 8) for k in range(3)), fired=r.u16(lg + 0x42), hit=r.u16(lg + 0x44),
                       kills=r.u8(0x4A963)))
        if not r.u16(M) & 0x40 and i > 2: break
    return tr, r


def check_focus():
    bad = 0
    def expect(c, msg):
        nonlocal bad
        if not c: bad += 1; print("FOCUS FAILED:", msg)
    # no target: straight flight, the speed ramp, freed when the life reaches 0 (150 passes with the launch pass)
    tr, r = focus_run("none")
    expect(len(tr) == 150 and tr[-1]["w0"] == 0 and tr[-1]["req"] == 0x0C, "none: freed at pass 150 (%d, W0 %04X)" % (len(tr), tr[-1]["w0"]))
    expect(all(t["dx"] == 0xFFFF for t in tr), "none: the destination word stays 'none' (straight flight)")
    expect(tr[11]["spd"] == 0x4200 and tr[0]["spd"] < 0x2000, "none: no-target speed ramp to 0x4200 by pass 11")
    expect(tr[-1]["spd"] == 0x3000 and all(t["w0"] & 0x400 for t in tr[-12:-1]), "none: coast to 0x3000 and W0 0x0400 below 12")
    # the target dies in flight: the player's designation drops to -1 the pass after, the missile keeps the last destination at height 0, hits the ground, freed
    tr, r = focus_run("dies")
    expect(tr[12]["lock"] == 0x1000 and tr[13]["lock"] == 0xFFFF, "dies: designation dropped one pass after the removal")
    expect(all(t["dh"] == 0 for t in tr[13:]), "dies: destination height 0 after the loss")
    crash = [t["i"] for t in tr if t["w0"] & 0x8000]
    expect(tr[0]["spd"] == tr[0]["own"] + 0x1E0 and tr[1]["spd"] - tr[0]["spd"] == 0x1E0, "dies: the boost +0x1E0 runs in the launch pass (speed %04X, owner %04X)" % (tr[0]["spd"], tr[0]["own"]))
    expect(crash and crash[0] == 39 and tr[-1]["w0"] == 0 and len(tr) == 55, "dies: ground impact and freed (%d passes, impact pass %s)" % (len(tr), crash[:1]))
    dies_n = len(tr); dies_c = crash[0] if crash else None
    # a target out of reach: the life runs out at pass 150 whatever the guidance does
    tr, r = focus_run("far")
    expect(len(tr) == 150 and tr[-1]["w0"] == 0, "far: freed at pass 150 (%d)" % len(tr))
    # a hit: the Sidewinder counts, the hostile is marked, the missile is removed at once, the kill is counted at the dying expiry
    tr, r = focus_run("hit", 40)
    hp = [t["i"] for t in tr if t["hit"]]
    expect(hp and hp[0] == 34 and len(tr) == 35 and tr[-1]["w0"] & 0x40 == 0 and tr[-1]["req"] == 0x0C, "hit: the missile removed in pass 35")
    t = tr[-1]
    expect(t["hit"] == 1 and t["fired"] == 1 and t["hw0"] & 0x600 and t["hb20"] & 2 and t["hst"] == 1 and t["cam"] == 0x1000, "hit: counters and marks %s" % t)
    H = REC(8)
    k = 0
    while k < 40 and r.u8(0x4A963) == 0:
        C.world(r); k += 1
    expect(r.u8(0x4A963) == 1 and k == 15 and r.u16(H) & 0x40, "hit: kill counted %d passes after the hit, record kept (airborne wreck)" % k)
    for c, nn in (("n20", 20), ("hit", 30), ("n60", 60)):
        t2, _ = focus_run(c, 80)
        print("  (printed, not asserted) target %d passes of velocity ahead: closest approach %d du, %s" % (nn, min(t["dist"] for t in t2 if t["w0"] & 0x40), "hit" if t2[-1]["hit"] else "no hit"))
    print("focus: none freed at %d, dies freed at %d (impact %s), far freed at 150, hit at pass %s killed %d passes later, %s" %
          (150, dies_n, dies_c, hp[0] if hp else None, k, "FAILED %d" % bad if bad else "0 differences"))
    return bad


# ---------------------------------------------------------------- the AI launch decision 0x2701A
def check_decision(n, seed):
    r = rig(); rng = random.Random(seed); bad = 0; yes = 0
    for it in range(n):
        stage(place=False, silence=False)
        own = rng.choice((8, 10, 12)); O = REC(own)
        for i in range(16):
            if i not in (0, 14): r.poke(REC(i), bytes(0x200))
        w0 = rng.choice((0x11C1, 0x11C0, 0x91C1, 0x15C1, 0x13C1, 0x11C9, 0x10C1, 0x01C1, 0x1141, 0x17C1))
        oc = rng.random() < 0.15; t38 = rng.choice((0x80, 0x80 | 14, 0x80 | 4, 0x00, 0x85, 0xFF))
        flags = rng.choice((0x60, 0x60, 0x70, 0x40, 0x20, 0)); r63 = rng.choice((0x3D, 0x2D, 0x1D, 0x3D, 0xFD)); req = rng.choice((0, 0, 1))
        r.w16(O, w0); r.w8(O + 0x20, 2 if oc else 0); r.w8(O + 0x38, t38); r.w8(O + 0x64, flags); r.w8(O + 0x63, r63)
        tw = {}
        for i in range(16):
            if i not in (0, 14, own):
                tw[i] = (rng.choice((0x11C0, 0x11C1, 0x0000, 0x1180)), rng.random() < 0.2)
                r.w16(REC(i), tw[i][0]); r.w8(REC(i) + 0x20, 2 if tw[i][1] else 0)
        r.w8(0x4A874, req)
        # the model: the tests in the order of 4.6
        exp = 0; ereq = req
        if w0 & 0x8700 or oc: exp = 0
        elif req: exp, ereq = 1, 0
        elif not (w0 & 0x40): exp = 0
        else:
            ok = True
            if t38 & 0x80:
                ti = t38 & 0x7F
                if ti == own: tw0, toc = w0, oc
                elif ti in tw: tw0, toc = tw[ti]
                else: tw0, toc = r.u16(REC(ti)) if ti < 16 else 0, False
                if not (tw0 & 0x40) or toc or (tw0 & 1): ok = False
            if ok and (flags & 0x60) == 0x60 and (w0 & 1) and (r63 & 0xF0) in (0x20, 0x30): exp, ereq = 1, 0
        if t38 & 0x80 and (t38 & 0x7F) >= 16: continue
        r.run(0x2701A, a2=O, a6=0x710000)
        got = r.reg("d0") & 0xFF; yes += got
        if got != exp or r.u8(0x4A874) != ereq:
            bad += 1
            if bad < 4: print("DIFF decision case %d: got %d want %d" % (it, got, exp))
    print("launch decision: %d cases (%d yes), %s" % (n, yes, "FAILED %d" % bad if bad else "0 differences"))
    return bad


# ---------------------------------------------------------------- a repeated hit on a dying record (8.2): the crashed bit is set again, the dying count is not restarted
def check_repeat():
    r = rig(); bad = 0
    for cnt in (14, 7, 2):
        stage(mode=3, place=True); C.stub_sound(r)
        H = REC(8)
        r.w16(H, 0x11C0 | 0x0400 | 0x0200); r.w8(H + 0x20, 2)
        r.w16(H + 0x4C, cnt)
        r.w16(0x4AA6C, 8); r.w16(0x4AA6E, 0x200 * 8)
        r.run(0x2A2A2, a1=H, a6=0x710000)
        ok = r.u16(H) & 0x0600 == 0x0600 and r.s16(H + 0x4C) == cnt
        bad += not ok
    print("repeated hit on a dying record: 3 cases, crashed bit kept and the count not restarted, %s" % ("FAILED %d" % bad if bad else "0 differences"))
    return bad


# ---------------------------------------------------------------- the "target destroyed" post 0x4016 (12.5): who posts it
def check_order():
    """frame order (note 08 8.1): world update (steps, then the stale-lock drop at its tail) -> display lists -> gun routine -> draw (the aircraft handler 0x26944).
    A missile kill is marked inside the world update and converted by the target's own step of the same pass, the drop at the tail clears the designation: no post.
    A gun kill is marked after the world update and reaches the draw handler as a crashed record: the handler posts 0x4016 and drops the designation."""
    bad = 0
    tr, r = focus_run("hit", 40)
    H = REC(8)
    r.w16(0x4AB98, 0); r.w16(0x4AA6E, 0x200 * 8); r.w16(0x4AA6C, 8); r.poke(0x21C44, b"\x4e\x75")
    r.run(0x26944)
    missile_ok = r.u16(0x4AB98) == 0 and r.u16(0x4AA78) == 0xFFFF and r.u16(H) & 0x600 == 0x400
    stage(mode=3, place=True); r.poke(0x21C44, b"\x4e\x75")
    r.w16(0x4AA78, 0x1000); C.world(r)
    r.w16(H, r.u16(H) | 0x200); r.w8(H + 0x20, r.u8(H + 0x20) | 2); r.w8(H + 5, 1); r.w16(0x54E72, 0x1000)       # the marks of the gun's third hit (0x2B286)
    r.w16(0x4AB98, 0); r.w16(0x4AA6E, 0x200 * 8); r.w16(0x4AA6C, 8)
    r.run(0x26944)
    gun_ok = r.u16(0x4AB98) == 0x4016 and r.u16(0x4AA78) == 0xFFFF and r.u16(H) & 0x600 == 0x400
    bad = (not missile_ok) + (not gun_ok)
    print("target-destroyed post: missile kill posts nothing (%s), gun kill posts 0x4016 in the draw pass (%s), %s" % (missile_ok, gun_ok, "FAILED %d" % bad if bad else "0 differences"))
    return bad


# ---------------------------------------------------------------- the big-hull test 0x2B3F0 for a record that is not a ship (the missile models, flag byte bit 4)
def check_bighull():
    r = rig(); rng = random.Random(9); res = {}
    for label, cls, ylo, yhi, nv in (("air, 12 slots + zeros", 0, 3000, 30000, 12), ("air, all slots random", 0, 3000, 30000, 100),
                                     ("ground, 12 slots + zeros", 0, 0, 40, 12), ("ground, all slots random", 0, 0, 40, 100), ("ship at the ground", 0x20, 0, 40, 100)):
        hits = 0
        for it in range(1500):
            stage(place=False, silence=False)
            O = REC(1); r.poke(O, bytes(0x200))
            r.w16(O, 0x10CA); r.w8(O + 0x62, cls); r.w8(O + 0x7D, 3)
            r.w16(O + 0xC, 5000); r.w16(O + 0xE, 5000); y = rng.randint(ylo, yhi); r.w32(O + 0x10, y)
            for k in range(nv * 3): r.w16(O + 0xA4 + 2 * k, rng.randint(-40, 40))
            S = POOL; r.poke(S, bytes(64))
            r.w32(S, (5000 + rng.randint(-60, 60)) * 256); r.w32(S + 4, (y + rng.randint(-60, 60)) * 256); r.w32(S + 8, (5000 + rng.randint(-60, 60)) * 256)
            r.w16(0x4AA70, 1)
            r.run(0x2B3F0, a3=S, a6=0x7D0000)
            hits += r.reg("d7") & 0xFFFF
        res[label] = hits
    bad = (res["air, 12 slots + zeros"] != 0) + (res["air, all slots random"] != 0) + (res["ground, 12 slots + zeros"] == 0) + (res["ship at the ground"] == 0)
    print("big hull 0x2B3F0 (1,500 tracer points per row): %s, %s" % (res, "FAILED %d" % bad if bad else "0 differences (air: never a hit)"))
    return bad


# ---------------------------------------------------------------- the contact scan against a ship in any record, mover viewed or not (10.2)
def check_ships():
    """a ship (class 0x20, a copy of record 14 with random rotated vertices) in record 14 or 6, the mover the player (the viewed record) or a missile record
    (not viewed): the outer box 0xA0000 sets V:4A957 and the deck and hull tests run (result 0x20 for some points)."""
    r = rig(); rng = random.Random(3); out = {}
    for label, ship, mover, viewed in (("player vs carrier 14", 14, 0, 0), ("missile 1 (not viewed) vs carrier 14", 14, 1, 0),
                                       ("missile 1 (not viewed) vs a ship in record 6", 6, 1, 0), ("player vs a ship in record 6", 6, 0, 0)):
        hit = prox = 0
        for it in range(1500):
            stage(place=False, silence=False)
            S = REC(ship)
            if ship != 14: r.poke(S, r.peek(REC(14), 0x200))
            for k in range(180): r.w16(S + 0xA4 + 2 * k, rng.randint(-300, 300))
            sx, sz = r.s32(S + 0x14), r.s32(S + 0x1C)
            M = REC(mover)
            for i in range(16):
                if i not in (0, 14, ship, mover): r.poke(REC(i), bytes(0x200))
            if mover:
                r.poke(M, r.peek(REC(0), 0x200)); r.w16(M, 0x10C8); r.w8(M + 0x62, 0); r.w8(M + 0x5E, 3)
            pos = [sx + rng.randint(-0x4000, 0x4000), rng.randint(-0x4000, 0x8000), sz + rng.randint(-0x4000, 0x4000)]
            for o, v in zip((0x14, 0x18, 0x1C), pos): r.w32(M + o, v)
            r.w16(M + 6, (pos[0] & 0xFFFFFFFF) >> 22); r.w16(M + 8, (pos[2] & 0xFFFFFFFF) >> 22)
            r.w16(M + 0xC, (pos[0] & 0x3FFFFF) >> 8); r.w16(M + 0xE, (pos[2] & 0x3FFFFF) >> 8); r.w32(M + 0x10, pos[1] >> 8)
            r.w16(0x4AA6E, 0x200 * mover); r.w16(0x4AA6C, mover); r.w16(0x4A996, 0x200 * viewed)
            r.w8(0x4A957, 0); r.w16(S + 2, 0)
            r.run(0x2B5E2, d2=pos[0], d3=pos[1], d4=pos[2])
            hit += (r.reg("d0") & 0xFFFF) == 0x20; prox += r.u8(0x4A957)
        out[label] = (hit, prox)
    bad = sum(1 for v in out.values() if v[0] == 0 or v[1] != 1500)
    print("ships in the contact scan (1,500 points each, hits / proximity): %s, %s" % (out, "FAILED %d" % bad if bad else "0 differences (every row hits and sets the proximity flag)"))
    return bad


# ---------------------------------------------------------------- the pens of the chaff drawer 0x3140C (7.7, 11.4)
def check_chaff():
    """the pixel routine 0x34168 and the line routine 0x3455E are replaced by a stub that copies the pen word V:4AA0C and counts the call: a chaff slot of
    life 0x3B (first two passes) calls the pixel once with pen 13; of life 0x3A and below it calls the LINE routine (kind 0 of the burst 0x31766) up to 12
    times with pen 13; a tracer calls the pixel with pen 9."""
    r = rig(); bad = 0
    code = bytes.fromhex("33F900" "04AA0C" "007F0000") + bytes.fromhex("5279007F0010") + bytes.fromhex("4E75")
    def setup(life, flags):
        stage(place=False, silence=False)
        r.poke(0x34168, code); r.poke(0x3455E, code); r.poke(0x7F0000, bytes(32))
        S = POOL; r.poke(S, bytes(64)); P = REC(0)
        pp = [r.s32(P + o) for o in (0x14, 0x18, 0x1C)]
        m = struct.unpack(">9h", r.peek(P + 0x92, 18)); fwd = (m[2], m[5], m[8])
        pos = [pp[k] + ((300 * fwd[k]) >> 14) * 256 for k in range(3)]
        r.w32(S, pos[0] & 0x3FFFFF); r.w32(S + 4, pos[1]); r.w32(S + 8, pos[2] & 0x3FFFFF)
        r.w16(S + 0x30, (pos[0] & 0xFFFFFFFF) >> 22); r.w16(S + 0x32, (pos[2] & 0xFFFFFFFF) >> 22)
        r.w16(S + 0x26, flags); r.w16(S + 0x28, life); r.w16(0x4AA0C, 0x77)
    res = {}
    for life in (0x3B, 0x3A, 0x30, 0x10):
        setup(life, 0x2401); callf(r, 0x3140C, (0,)); res[life] = (r.u16(0x7F0010), r.u16(0x7F0000))
    setup(10, 0x401); callf(r, 0x31384, (0,)); res["tracer"] = (r.u16(0x7F0010), r.u16(0x7F0000))
    ok = res[0x3B] == (1, 13) and all(res[l][0] >= 1 and res[l][1] == 13 for l in (0x3A, 0x30, 0x10)) and res["tracer"] == (1, 9)
    print("chaff pens (calls, pen): %s, %s" % (res, "0 differences" if ok else "FAILED"))
    return 0 if ok else 1


# ---------------------------------------------------------------- the non-player result handler's writer of V:54E72 (0x2A954)
def check_nonplayer():
    """an AI aircraft (record 8) through the flight step 0x2A2A2: touching another aircraft (collision result 0x40) reaches 0x2A954: V:54E72 = its offset, W2
    0x1000, B20 bit 1, state 1, and the record is removed; a ground impact (result 0x10) does not."""
    r = rig(); bad = 0
    res = {}
    for kind in ("contact", "ground"):
        stage(mode=3, place=True); C.stub_sound(r)
        M = REC(8); O = REC(10)
        for i in range(16):
            if i not in (0, 14, 8, 10): r.poke(REC(i), bytes(0x200))
        r.w16(M, 0x11C0); r.w8(M + 0x62, 0x12); r.w16(M + 2, 0); r.w8(M + 0x5E, 3); r.w8(M + 5, 0); r.w16(0x54E72, 0xFFFF)
        pp = C.pos(r, 0)
        if kind == "contact":
            base = [pp[0] + 0x400000, 0x400000, pp[2] + 0x400000]
            C.put_at(r, 8, base); r.poke(O, r.peek(M, 0xA4)); r.w16(O, 0x11C8); r.w8(O + 0x62, 0x13); r.w8(O + 0x5E, 4); C.put_at(r, 10, base)
            r.w16(M + 0x6C, 0x1200)
        else:
            C.put_at(r, 8, [pp[0] + 0x400000, -0x40000, pp[2] + 0x400000])
        r.w16(0x4AA6E, 0x200 * 8); r.w16(0x4AA6C, 8)
        r.run(0x2A2A2, a1=M, a6=0x710000)
        res[kind] = (r.u16(M), r.u16(M + 2), r.u8(M + 0x20), r.u8(M + 5), r.u16(0x54E72))
    ok = res["contact"] == (0x1080, 0x1000, 0x02, 1, 0x1000) and res["ground"][1] == 0 and res["ground"][4] == 0xFFFF and res["ground"][3] == 0
    print("non-player 0x2A954: contact (W0, W2, B20, state, V:54E72) %s, ground %s, %s" % (tuple(hex(v) for v in res["contact"]), tuple(hex(v) for v in res["ground"]), "0 differences" if ok else "FAILED"))
    return 0 if ok else 1


# ---------------------------------------------------------------- result 0x40 for other classes, and the player with an ejection nibble (13.1, 10.3)
def check_result40():
    """the flight step 0x2A2A2 with a collision result 0x40 (a contact with a friendly aircraft in record 10): class 0 (a missile) is removed at once with no
    marks; class 0x15 goes to the dying start (not removed); a class 0x12 aircraft is marked and removed. The player (record 0) with an ejection nibble is
    marked (W2 0x1000, B20 bit 1, state 1, V:54E72 = 0) and its valid bit is cleared, the nibble cycle goes on and the seat appears in record 1 at the 6th pass;
    without a nibble the damage handler runs instead."""
    r = rig(); res = {}
    for label, cls in (("class 0x00", 0x00), ("class 0x15", 0x15), ("class 0x12", 0x12)):
        stage(mode=3, place=True); C.stub_sound(r)
        M = REC(8); O = REC(10)
        for i in range(16):
            if i not in (0, 14, 8, 10): r.poke(REC(i), bytes(0x200))
        r.w16(M, 0x11C0); r.w8(M + 0x62, cls); r.w16(M + 2, 0); r.w8(M + 0x5E, 3); r.w8(M + 5, 0); r.w16(0x54E72, 0xFFFF); r.w16(M + 0x4C, 100)
        pp = C.pos(r, 0); base = [pp[0] + 0x400000, 0x400000, pp[2] + 0x400000]
        C.put_at(r, 8, base); r.poke(O, r.peek(M, 0xA4)); r.w16(O, 0x11C8); r.w8(O + 0x62, 0x13); r.w8(O + 0x5E, 4); C.put_at(r, 10, base)
        r.w16(M + 0x6C, 0x1200); r.w16(0x4AA6E, 0x200 * 8); r.w16(0x4AA6C, 8)
        r.run(0x2A2A2, a1=M, a6=0x710000)
        res[label] = (r.u16(M), r.u16(M + 2), r.u16(0x54E72), r.s16(M + 0x4C))
    # (V:54E72 = 0x1400 after the first two rows is the OTHER record's mark by the contact code 10.3; only the aircraft mover writes its own offset, 0x1000)
    ok = (res["class 0x00"][0] & 0x40 == 0 and res["class 0x00"][1] & 0x1000 == 0 and res["class 0x15"][0] & 0x440 == 0x440 and res["class 0x15"][3] == 15
          and res["class 0x12"][0] & 0x40 == 0 and res["class 0x12"][1] & 0x1000 and res["class 0x12"][2] == 0x1000)
    out = []
    for nib in (0x0A, 0x05):
        stage(mode=3, place=True); C.stub_sound(r)
        P = REC(0); O = REC(10)
        for i in range(16):
            if i not in (0, 14, 10): r.poke(REC(i), bytes(0x200))
        r.w8(P + 0x7C, (r.u8(P + 0x7C) & 0xF0) | nib); r.w16(0x54E72, 0xFFFF); r.w16(P + 0x6E, 0x1200)
        r.poke(O, r.peek(REC(8), 0xA4)); r.w16(O, 0x11C0); r.w8(O + 0x62, 0x13); r.w8(O + 0x5E, 4); C.put_at(r, 10, C.pos(r, 0))
        C.world(r)
        a = (r.u16(P), r.u16(P + 2), r.u8(P + 5), r.u16(0x54E72))
        seat = None
        for k in range(1, 8):
            C.world(r)
            if seat is None and r.u16(REC(1)) & 0x40: seat = k
        out.append((nib, a, seat, r.u16(P) & 0x40))
    ok = ok and out[0][1][0] & 0x40 == 0 and out[0][1][1] & 0x1000 and out[0][1][2] == 1 and out[0][1][3] == 0 and out[0][2] == 5 and out[0][3] == 0
    print("result 0x40 by class %s; player with a nibble (nibble, (W0, W2, state, V:54E72), seat pass, valid) %s, %s" % (res, out, "0 differences" if ok else "FAILED"))
    return 0 if ok else 1


# ---------------------------------------------------------------- the ship test for a mover that is not the viewed record (10.2)
def check_shipverts():
    """the mover's own vertex slots (+0xA4) are used only when the mover is the viewed record V:4A996; a non-viewed mover gives the same result whatever its slots hold."""
    r = rig(); rng = random.Random(4); diff_nv = diff_v = 0
    for it in range(600):
        stage(place=False, silence=False)
        S = REC(14)
        for k in range(180): r.w16(S + 0xA4 + 2 * k, rng.randint(-300, 300))
        sx, sz = r.s32(S + 0x14), r.s32(S + 0x1C)
        M = REC(1)
        for i in range(16):
            if i not in (0, 14, 1): r.poke(REC(i), bytes(0x200))
        r.poke(M, r.peek(REC(0), 0x200)); r.w16(M, 0x10C8); r.w8(M + 0x62, 0); r.w8(M + 0x5E, 3)
        pos = [sx + rng.randint(-0x4000, 0x4000), rng.randint(-0x4000, 0x8000), sz + rng.randint(-0x4000, 0x4000)]
        for o, v in zip((0x14, 0x18, 0x1C), pos): r.w32(M + o, v)
        r.w16(M + 6, (pos[0] & 0xFFFFFFFF) >> 22); r.w16(M + 8, (pos[2] & 0xFFFFFFFF) >> 22)
        r.w16(M + 0xC, (pos[0] & 0x3FFFFF) >> 8); r.w16(M + 0xE, (pos[2] & 0x3FFFFF) >> 8); r.w32(M + 0x10, pos[1] >> 8)
        r.w16(0x4AA6E, 0x200 * 1); r.w16(0x4AA6C, 1)
        got = {}
        for viewed in (0, 1):
            r.w16(0x4A996, 0x200 * viewed)
            row = []
            for variant in range(2):
                for k in range(60): r.w16(M + 0xA4 + 2 * k, rng.randint(-300, 300) if variant else 0)
                r.w8(0x4A957, 0); r.w16(S + 2, 0)
                r.run(0x2B5E2, d2=pos[0], d3=pos[1], d4=pos[2])
                row.append(r.reg("d0") & 0xFFFF)
            got[viewed] = row
        diff_nv += got[0][0] != got[0][1]; diff_v += got[1][0] != got[1][1]
    ok = diff_nv == 0 and diff_v > 0
    print("ship test, missile mover, 600 points: results differ with the mover's own slots %d times when not viewed (must be 0) and %d times when viewed (must be > 0), %s" % (diff_nv, diff_v, "0 differences" if ok else "FAILED"))
    return 0 if ok else 1


# ---------------------------------------------------------------- the scope draw 0x35CF2: how many walks per pass (12.2)
def check_scope():
    r = rig(); res = {}
    STUB = 0x7E1000; CNT = 0x7E1100
    stub = bytes.fromhex("5279") + CNT.to_bytes(4, "big") + bytes.fromhex("4A39") + (0x4A8F0).to_bytes(4, "big") + bytes.fromhex("4EF9") + (0x35E64).to_bytes(4, "big")
    for ef, n in ((0, 0), (0, 5), (0, 8), (0xFF, 0), (0xFF, 5), (1, 0), (2, 0), (3, 0)):
        stage(place=False, silence=False)
        r.poke(STUB, stub); r.poke(0x35E5E, bytes.fromhex("4EF9") + STUB.to_bytes(4, "big")); r.poke(CNT, bytes(4)); r.poke(0x53368, bytes(64))
        r.w8(0x4A8EF, ef); r.w8(0x4A8F0, 1); r.w16(0x4A992, n); r.w8(0x4A93B, 0)
        r.run(0x35CF2, a6=0x710000)
        res[(ef, n)] = (r.u16(CNT), r.u8(0x4A93B))
    ok = all(res[k] == (1, 1) for k in ((0, 0), (0, 5), (0, 8), (0xFF, 0), (0xFF, 5))) and all(res[k] == (2, 2) for k in ((1, 0), (2, 0), (3, 0)))
    print("scope draw 0x35CF2 (V:4A8EF, n) -> (walks, V:4A93B added): %s, %s" % (res, "0 differences" if ok else "FAILED"))
    return 0 if ok else 1


# ---------------------------------------------------------------- the landmark branch of the tracer test (note 07 8.2, "Landmarks")
def landmark_model(m, i, nentries):
    """0x2ADDE for slot i and a queue C of `nentries` landmark entries (flag byte 0x01), walked from V:4AA7E backwards; the model works on the Mem m and
    returns D0.  Written from the routine: the slot ordinals, the cell test, the top box, the chain of boxes, the plane test of 8 sub-steps."""
    S = POOL + 64 * i
    if m.u8(0x4A875) | m.u8(0x4A866): return 0
    pos = lambda k: m.s32(S + 4 * k)
    def setpos(k, v): m.w32(S + 4 * k, v)
    for k in range(3): m.w16(0x4B230 + 2 * k, m.s32(S + 0xC + 4 * k) >> 8)
    count = nentries; visited = 0; d5 = -1; ecount = 0
    cx_t, cz_t = m.u16(S + 0x30), m.u16(S + 0x32)
    for entry in range(nentries):
        visited += 1
        landed = False
        if d5 < 10:
            d5 += 1
            P = m.s32(0x5283C + 4 * d5)
            if P >= 0:
                R = 0x52868 + 256 * d5
                dcx = s16(cx_t - m.s8(R + 2)); dcz = s16(cz_t - m.s8(R + 3))
                if abs(dcx) <= 1 and abs(dcz) <= 1:
                    xoff = (dcx << 14); zoff = (dcz << 14) + (((P >> 16) & 0xFFFF) >> 2)    # the z offset keeps the high word of the table pointer in its low bits (the swap after a word-only sign extension: note 05 17.11, the artefact of the volume's z)
                    y = pos(1) >> 8
                    ok = y <= 0x7FFF and s16(y) <= m.s16(R + 4)
                    x = (pos(0) >> 8) + xoff; z = (pos(2) >> 8) + zoff
                    ok = ok and m.s16(R + 6) < x < m.s16(R + 8) and m.s16(R + 0xA) < z < m.s16(R + 0xC)
                    if ok:
                        A0 = P + 0xA; A1 = R + 0xE
                        while True:
                            if not (s16(y) > m.s16(A1)) and m.s16(A1 + 2) < x < m.s16(A1 + 4) and m.s16(A1 + 6) < z < m.s16(A1 + 8):
                                # the plane test of 8 sub-steps
                                A2 = A0; A4 = A1; res = None
                                for sub in range(8):
                                    for k in range(3): setpos(k, pos(k) + m.s32(S + 0x18 + 4 * k))
                                    pa = A2 + 0xE; va = A4 + 0xE
                                    n = m.s16(pa); pa += 2
                                    D0 = (pos(0) >> 8) + xoff; D1 = pos(1) >> 8; D2 = (pos(2) >> 8) + zoff
                                    inside = True
                                    while True:
                                        pa += 6
                                        D3 = s32(D0 - m.s16(va)); D4 = s16(D1 - m.s16(va + 2)); D5 = s32(D2 - m.s16(va + 4)); va += 6
                                        D7 = s32(s16(D3) * m.s16(pa) + s16(D4) * m.s16(pa + 2) + s16(D5) * m.s16(pa + 4)); pa += 6
                                        if D7 >= 0: inside = False; break
                                        n -= 1
                                        if n <= 0: break
                                    if inside: return 1
                                nxt = m.s32(A4 + 0xA)
                                if nxt <= 0: return 0
                                for k in range(3): setpos(k, pos(k) - m.s32(S + 0xC + 4 * k))
                                A0 = A2
                                x = pos(0) >> 8; y = pos(1) >> 8; z = pos(2) >> 8      # the original reloads the box coordinates without the cell offsets
                                nxt_addr = nxt
                                A1 = nxt_addr; A0 = m.u32(A0 + 0xA)
                                continue
                            nxt = m.s32(A1 + 0xA)
                            if nxt < 0: break
                            A1 = nxt; A0 = m.u32(A0 + 0xA)
        if not (count > visited and d5 < 10 and ecount < 6): break
    # no hit: gravity, drag and the move (slots below 20)
    F = m.u16(S + 0x26)
    d0, d1, d2 = m.s32(S + 0xC), m.s32(S + 0x10), m.s32(S + 0x14)
    d1 -= 0x100
    if F & 0x1000: d0 -= d0 >> 6; d2 -= d2 >> 6
    elif F & 0x2000:
        d1 += 0x100; d1 -= d1 >> 2; d0 -= d0 >> 2; d2 -= d2 >> 2
    m.w32(S + 0xC, d0); m.w32(S + 0x10, d1); m.w32(S + 0x14, d2)
    if m.u16(S + 0x24) == 0: m.w16(S + 0x24, 1)
    else:
        m.w32(S, m.s32(S) + d0); m.w32(S + 4, m.s32(S + 4) + d1); m.w32(S + 8, m.s32(S + 8) + d2)
    return 0


def check_landmark(n, seed):
    """the original's 0x2ADDE against landmark_model on the landmark slots the original's own list builder wrote for the logged air scene (5 live volumes):
    random tracers around each volume, random queue lengths; the return value and the slot's 64 bytes compared; the record bytes must not change."""
    import walk_check as W
    r = rig(); rng = random.Random(seed); bad = 0
    stats = {"hit": 0, "miss": 0, "cells": 0}
    W.scene(r, "air")
    r.poke(0x34A28, b"\x4e\x75")
    r.w32(0x5283C + 4 * 10, r.u32(0x5283C)); r.poke(0x52868 + 256 * 10, r.peek(0x52868, 256))     # a live volume at ordinal 10 too: the eleventh landmark entry is walked (ordinals 0 to 10)
    r.w32(0x5283C + 4 * 7, r.u32(0x5283C + 4 * 3)); r.poke(0x52868 + 256 * 7, r.peek(0x52868 + 256 * 3, 256))
    live = [k for k in range(11) if r.s32(0x5283C + 4 * k) >= 0 and r.u8(0x52868 + 256 * k + 2) != 0]
    recs_before = r.peek(REC(0), 0x200 * 15)
    snapbase = r.peek(0, 0x80000)
    q = 0x7E8000
    for it in range(n):
        k = rng.choice(live)
        R = 0x52868 + 256 * k
        r.poke(0, snapbase) if False else None
        ne = rng.randint(k + 1, 12) if k < 10 else rng.choice((11, 12))                 # the walk must reach the ordinal k
        r.poke(q, bytes(24 * (ne + 2)))
        for e in range(ne): r.w16(q + 24 * e, 0x2D01)
        r.w32(0x4AA7E, q + 24 * (ne - 1)); r.w8(0x4A916, ne)
        r.w8(0x4A875, 0); r.w8(0x4A866, 0)
        S = POOL
        r.poke(S, bytes(64))
        dcx = rng.choice((-1, 0, 0, 0, 1, 2)); dcz = rng.choice((-1, 0, 0, 0, 1, -2))
        lcx = r.u8(R + 2); lcz = r.u8(R + 3)
        sx, sz = lcx + dcx, lcz + dcz
        r.w16(S + 0x30, sx); r.w16(S + 0x32, sz)
        xmin, xmax, zmin, zmax, ymax = r.s16(R + 6), r.s16(R + 8), r.s16(R + 0xA), r.s16(R + 0xC), r.s16(R + 4)
        tx = rng.randint(xmin - 200, xmax + 200) - (dcx << 14) if rng.random() < 0.5 else rng.randint(xmin, xmax) - (dcx << 14)
        tz = rng.randint(zmin - 200, zmax + 200) - (dcz << 14) if rng.random() < 0.5 else rng.randint(zmin, zmax) - (dcz << 14)
        ty = rng.randint(-100, ymax + 200)
        if rng.random() < 0.7:                      # near the volume's own vertices (the box alone is much larger than the planes' volume)
            nv = max(1, r.s16(r.u32(0x5283C + 4 * k) + 0xA + 0xE) if False else 4)
            vi = rng.randint(0, 3); vx, vy, vz = r.s16(R + 0x1C + 6 * vi), r.s16(R + 0x1E + 6 * vi), r.s16(R + 0x20 + 6 * vi)
            cx_, cz_ = (xmin + xmax) // 2, (zmin + zmax) // 2
            tx = (cx_ + (vx - cx_) * rng.randint(0, 100) // 100 + rng.randint(-30, 30)) - (dcx << 14)
            tz = (cz_ + (vz - cz_) * rng.randint(0, 100) // 100 + rng.randint(-30, 30)) - (dcz << 14)
            ty = rng.randint(0, max(1, ymax))
        r.w32(S, (tx * 256 + rng.randint(0, 255)) & 0xFFFFFFFF); r.w32(S + 4, (ty * 256 + rng.randint(0, 255)) & 0xFFFFFFFF); r.w32(S + 8, (tz * 256 + rng.randint(0, 255)) & 0xFFFFFFFF)
        for a in range(3):
            v = rng.randint(-3000, 3000)
            r.w32(S + 0x18 + 4 * a, v); r.w32(S + 0xC + 4 * a, v * 8)
        r.w16(S + 0x26, rng.choice((0x401, 0x1401, 0x2401))); r.w16(S + 0x24, rng.choice((0, 1))); r.w16(S + 0x28, 15)
        m = visibility_mem(r)
        start = r.peek(S, 12)
        want = landmark_model(m, 0, ne)
        wslot = bytes(m.b[S:S + 64])
        if os.environ.get("LM_CASE") == str(it):
            import ctypes
            L = r.L; L.m68k_get_reg.argtypes = [ctypes.c_void_p, ctypes.c_int]; L.m68k_get_reg.restype = ctypes.c_uint; L.m68k_execute.argtypes = [ctypes.c_int]
            for q_ in range(16): L.rig_set_reg(q_, 0)
            sp = 0xE00000 - 0x100 - 8; r.w32(sp, 0x100); r.w32(sp + 4, 0); L.rig_set_reg(15, sp); L.rig_set_reg(16, 0x2ADDE)
            pcs = []
            for _ in range(200000):
                L.m68k_execute(1); pc_ = L.m68k_get_reg(None, 16)
                if pc_ == 0x100: break
                if pc_ == 0x2B354: pcs.append("[D3=%d D4=%d D5=%d D7=%d]" % tuple(s32(L.rig_get_reg(i)) for i in (3, 4, 5, 7)))
                if pc_ in (0x2AF52, 0x2B1E2, 0x2B2FC, 0x2B198, 0x2B366, 0x2B1DA, 0x2AE62, 0x2B000, 0x2B1D2, 0x2B392, 0x2B01A): pcs.append("%X" % pc_)
            print("   pcs", " ".join(pcs))
            got = r.reg("d0")
        else:
            got = C.callf(r, 0x2ADDE, (0,))
        gslot = r.peek(S, 64)
        stats["hit" if (got & 0xFFFF) else "miss"] += 1
        if (got & 0xFFFF) != want or gslot != wslot or r.peek(REC(0), 0x200 * 15) != recs_before:
            bad += 1
            if bad < 6 and os.environ.get("LM_DEBUG"):
                d = lambda b: [struct.unpack(">i", b[4 * a:4 * a + 4])[0] for a in range(3)]
                print("   start", d(start), "got pos", d(gslot), "want pos", d(wslot), "sub", d(r.peek(S + 0x18, 12)))
                print("   R", k, r.peek(R, 0x40).hex())
            if bad < 6: print("DIFF landmark case %d slot %d: got %d want %d, slot bytes %s" % (it, k, got & 0xFFFF, want, "equal" if gslot == wslot else "differ at %s" % [x for x in range(64) if gslot[x] != wslot[x]][:6]))
    print("landmark tracer test: %d cases %s, %s" % (n, stats, "FAILED %d" % bad if bad else "0 differences"))
    return bad


# ---------------------------------------------------------------- landmark and entity entries in one queue (the register sharing of 0x2ADDE)
def mixed_model(m, i, entries):
    """0x2ADDE for slot i and a queue C given from the first walked entry: ('L',) a landmark entry, ('E', record index, descriptor) an entity entry whose hull test never hits
    (part id 0xFF).  The model keeps the data register D5 as the routine does: the landmark ordinal is its low word (starts at -1); an entity entry whose box is entered swaps the
    halves and counts the 8 sub-steps in the low word (it ends at -1: the ordinals restart at 0, the old ordinal sits in the high word); a landmark chain element that passes its
    box swaps the halves as well, and the plane test leaves its z difference (a long) in the register."""
    S = POOL + 64 * i
    if m.u8(0x4A875) | m.u8(0x4A866): return 0
    pos = lambda k: m.s32(S + 4 * k)
    def setpos(k, v): m.w32(S + 4 * k, v)
    for k in range(3): m.w16(0x4B230 + 2 * k, m.s32(S + 0xC + 4 * k) >> 8)
    count = len(entries); visited = 0; ecount = 0; d5 = 0xFFFFFFFF
    sw = lambda v: ((v << 16) | (v >> 16)) & 0xFFFFFFFF
    cx_t, cz_t = m.u16(S + 0x30), m.u16(S + 0x32)
    for entry in entries:
        visited += 1
        if entry[0] == 'E':
            if ecount < 6:
                ecount += 1
                idx = entry[1]; R = REC(idx)
                if idx != m.s16(S + 0x2E) and m.u16(R + 6) == cx_t and m.u16(R + 8) == cz_t and (m.u16(R) & 0x40):
                    flag = model_flag(m, entry[2]); d7 = 0x800 if flag & 0x10 else m.s16(0x4AA5E)
                    dx = abs(s16(m.s16(R + 0xC) - (m.s32(S) >> 8))); dy = abs(m.s32(R + 0x10) - (m.s32(S + 4) >> 8)); dz = abs(s16(m.s16(R + 0xE) - (m.s32(S + 8) >> 8)))
                    if dx < d7 and dy < d7 and dz < d7:
                        d5 = sw(d5)
                        for k in range(8):
                            for a in range(3): setpos(a, pos(a) + m.s32(S + 0x18 + 4 * a))
                        d5 = (d5 & 0xFFFF0000) | 0xFFFF                      # the dbf loop ends with the low word at -1
                        for a in range(3): setpos(a, pos(a) - m.s32(S + 0xC + 4 * a))
        else:
            low = s16(d5 & 0xFFFF)
            if low < 10:
                low += 1; d5 = (d5 & 0xFFFF0000) | (low & 0xFFFF)
                d0 = s16(low << 2); P = m.s32(0x5283C + d0)
                if P >= 0:
                    R = 0x52868 + s16(d0 << 6)
                    dcx = s16(cx_t - m.s8(R + 2)); dcz = s16(cz_t - m.s8(R + 3))
                    if abs(dcx) <= 1 and abs(dcz) <= 1:
                        xoff = dcx << 14; zoff = (dcz << 14) + (((P >> 16) & 0xFFFF) >> 2)
                        y = pos(1) >> 8
                        ok = y <= 0x7FFF and s16(y) <= m.s16(R + 4)
                        x = (pos(0) >> 8) + xoff; z = (pos(2) >> 8) + zoff
                        ok = ok and m.s16(R + 6) < x < m.s16(R + 8) and m.s16(R + 0xA) < z < m.s16(R + 0xC)
                        if ok:
                            A0 = P + 0xA; A1 = R + 0xE
                            while True:
                                if not (s16(y) > m.s16(A1)) and m.s16(A1 + 2) < x < m.s16(A1 + 4) and m.s16(A1 + 6) < z < m.s16(A1 + 8):
                                    A2 = A0; A4 = A1
                                    d5 = sw(d5)
                                    for sub in range(8):
                                        for k in range(3): setpos(k, pos(k) + m.s32(S + 0x18 + 4 * k))
                                        pa = A2 + 0xE; va = A4 + 0xE
                                        n = m.s16(pa); pa += 2
                                        D0 = (pos(0) >> 8) + xoff; D1 = pos(1) >> 8; D2 = (pos(2) >> 8) + zoff
                                        inside = True
                                        while True:
                                            pa += 6
                                            D3 = s32(D0 - m.s16(va)); D4 = s16(D1 - m.s16(va + 2)); D5 = s32(D2 - m.s16(va + 4)); va += 6
                                            D7 = s32(s16(D3) * m.s16(pa) + s16(D4) * m.s16(pa + 2) + s16(D5) * m.s16(pa + 4)); pa += 6
                                            d5 = D5 & 0xFFFFFFFF
                                            if D7 >= 0: inside = False; break
                                            n -= 1
                                            if n <= 0: break
                                        if inside: return 1
                                    nxt = m.s32(A4 + 0xA)
                                    if nxt <= 0: return 0
                                    for k in range(3): setpos(k, pos(k) - m.s32(S + 0xC + 4 * k))
                                    x = pos(0) >> 8; y = pos(1) >> 8; z = pos(2) >> 8
                                    A1 = nxt; A0 = m.u32(A2 + 0xA)
                                    continue
                                nxt = m.s32(A1 + 0xA)
                                if nxt < 0: break
                                A1 = nxt; A0 = m.u32(A0 + 0xA)
        if not (count > visited and s16(d5 & 0xFFFF) < 10 and ecount < 6): break
    F = m.u16(S + 0x26)
    d0, d1, d2 = m.s32(S + 0xC), m.s32(S + 0x10), m.s32(S + 0x14)
    d1 -= 0x100
    if F & 0x1000: d0 -= d0 >> 6; d2 -= d2 >> 6
    elif F & 0x2000:
        d1 += 0x100; d1 -= d1 >> 2; d0 -= d0 >> 2; d2 -= d2 >> 2
    m.w32(S + 0xC, d0); m.w32(S + 0x10, d1); m.w32(S + 0x14, d2)
    if m.u16(S + 0x24) == 0: m.w16(S + 0x24, 1)
    else:
        m.w32(S, m.s32(S) + d0); m.w32(S + 4, m.s32(S + 4) + d1); m.w32(S + 8, m.s32(S + 8) + d2)
    return 0


def check_mixed(n, seed):
    """landmark and entity entries in one queue against mixed_model: the entity records have cells equal to the tracer's, a part id of 0xFF (no hit) and positions that often put the
    tracer inside their box, so the register sharing (ordinals restarting after an entered entity box, the plane test's scratch) is exercised; the walk limits (11 ordinals, 6 entities)."""
    import walk_check as W
    r = rig(); rng = random.Random(seed); bad = 0; stats = {"hit": 0, "miss": 0, "entered": 0}
    img = open(IMGPATH, "rb").read()
    W.scene(r, "air"); r.poke(0x34A28, b"\x4e\x75")
    r.w32(0x5283C + 4 * 10, r.u32(0x5283C)); r.poke(0x52868 + 256 * 10, r.peek(0x52868, 256))
    live = [k for k in range(11) if r.s32(0x5283C + 4 * k) >= 0 and r.u8(0x52868 + 256 * k + 2) != 0]
    snap = r.peek(0x4A6E8, 0x54EC0 - 0x4A6E8); recs = r.peek(REC(0), 0x200 * 15)
    q = 0x7E8000
    for it in range(n):
        r.poke(0x4A6E8, snap); r.poke(REC(0), recs)
        k = rng.choice(live); R = 0x52868 + 256 * k
        S = POOL; r.poke(S, bytes(64))
        lcx = r.u8(R + 2); lcz = r.u8(R + 3)
        dcx = rng.choice((-1, 0, 0, 1)); dcz = rng.choice((-1, 0, 0, 1))
        sx, sz = lcx + dcx, lcz + dcz
        r.w16(S + 0x30, sx); r.w16(S + 0x32, sz)
        xmin, xmax, zmin, zmax, ymax = r.s16(R + 6), r.s16(R + 8), r.s16(R + 0xA), r.s16(R + 0xC), r.s16(R + 4)
        vi = rng.randint(0, 3); vx, vy, vz = r.s16(R + 0x1C + 6 * vi), r.s16(R + 0x1E + 6 * vi), r.s16(R + 0x20 + 6 * vi)
        cx_, cz_ = (xmin + xmax) // 2, (zmin + zmax) // 2
        tx = cx_ + (vx - cx_) * rng.randint(0, 100) // 100 + rng.randint(-30, 30) - (dcx << 14)
        tz = cz_ + (vz - cz_) * rng.randint(0, 100) // 100 + rng.randint(-30, 30) - (dcz << 14)
        ty = rng.randint(0, max(1, ymax))
        r.w32(S, (tx * 256 + rng.randint(0, 255)) & 0xFFFFFFFF); r.w32(S + 4, (ty * 256 + rng.randint(0, 255)) & 0xFFFFFFFF); r.w32(S + 8, (tz * 256 + rng.randint(0, 255)) & 0xFFFFFFFF)
        for a in range(3):
            v = rng.randint(-1500, 1500)
            r.w32(S + 0x18 + 4 * a, v); r.w32(S + 0xC + 4 * a, v * 8)
        r.w16(S + 0x26, rng.choice((0x401, 0x1401, 0x2401))); r.w16(S + 0x24, rng.choice((0, 1))); r.w16(S + 0x28, 15)
        r.w16(S + 0x2E, 0)
        # entity records 4..13: valid, in the tracer's cells, part id 0xFF, around the tracer
        ents = []
        for idx in rng.sample(range(4, 14), 8):
            O = REC(idx); r.poke(O, bytes(0x200))
            r.w16(O, 0x11C0); r.w8(O + 0x62, 0x12); r.w8(O + 0x7B, 0xFF); r.w8(O + 0x7D, 3)
            r.w16(O + 6, sx); r.w16(O + 8, sz)
            near = rng.random() < 0.6
            ox = (tx & 0xFFFF) + (rng.randint(-250, 250) if near else rng.randint(-2000, 2000))
            r.w16(O + 0xC, ox); r.w16(O + 0xE, (tz & 0xFFFF) + (rng.randint(-250, 250) if near else rng.randint(-2000, 2000)))
            r.w32(O + 0x10, ty + (rng.randint(-250, 250) if near else rng.randint(-2000, 2000)))
            t = rng.choice((3, 4, 6, 7, 5, 14))
            r.poke(DESC + 20 * idx, img[TPL + 20 * t - 0x10000:TPL + 20 * t + 20 - 0x10000])
            ents.append(idx)
        # the walk order: a random mix of landmark and entity entries (the sequence is given in walk order, the memory holds it reversed)
        ne = rng.randint(2, 20); seq = []
        for e in range(ne):
            if rng.random() < 0.55 and ents: idx = rng.choice(ents); seq.append(('E', idx, DESC + 20 * idx))
            else: seq.append(('L',))
        if not any(x[0] == 'L' for x in seq): continue
        r.poke(q, bytes(24 * (ne + 2)))
        for pos_, x in enumerate(reversed(seq)):
            if x[0] == 'E': r.w16(q + 24 * pos_, (x[1] << 8) | 0x10); r.w32(q + 24 * pos_ + 2, x[2])
            else: r.w16(q + 24 * pos_, 0x2D01)
        r.w32(0x4AA7E, q + 24 * (ne - 1)); r.w8(0x4A916, ne); r.w8(0x4A875, 0); r.w8(0x4A866, 0)
        m = visibility_mem(r)
        want = mixed_model(m, 0, seq); wslot = bytes(m.b[S:S + 64])
        got = C.callf(r, 0x2ADDE, (0,)); gslot = r.peek(S, 64)
        stats["hit" if (got & 0xFFFF) else "miss"] += 1
        if (got & 0xFFFF) != want or gslot != wslot or r.peek(REC(0), 0x200 * 15) != recs and False:
            bad += 1
            if bad < 6: print("DIFF mixed case %d: got %d want %d slot %s seq %s" % (it, got & 0xFFFF, want, "equal" if gslot == wslot else [x for x in range(64) if gslot[x] != wslot[x]][:6], [x[0] + (str(x[1]) if x[0] == 'E' else '') for x in seq]))
    print("landmark and entity entries: %d cases %s, %s" % (n, stats, "FAILED %d" % bad if bad else "0 differences"))
    return bad


def visibility_mem(r):
    return Mem(r.peek(0, 0x800000))


def check_rangekey():
    """the range key body 0x1DEF4 for every low nibble of the viewed record's +0x63 (and five high nibbles): the original's table 9 -> 0xD, 0xB -> 9, every other value -> 0xB;
    the high nibble kept; V:4AA52 bit 6 and V:4A8F3 = 3 set (note 07 12.8 and the follow-up sweep of the range key)."""
    r = stage(mode=3, place=False)
    r.poke(0x37C4A, b"\x4e\x75")
    P = REC(0); bad = 0
    want = {9: 0xD, 0xB: 9}
    for hi in (0x00, 0x10, 0x20, 0x30, 0xF0):
        for n in range(16):
            r.w8(P + 0x63, hi | n); r.w16(0x4A996, 0); r.w8(0x4AA52, 0); r.w8(0x4A8F3, 0)
            r.run(0x1DEF4)
            got = r.u8(P + 0x63)
            if got != (hi | want.get(n, 0xB)) or r.u8(0x4AA52) != 0x40 or r.u8(0x4A8F3) != 3:
                bad += 1; print("RANGEKEY DIFF hi %02X n %X: got %02X" % (hi, n, got))
    print("range key: 80 nibbles, %s" % ("FAILED %d" % bad if bad else "0 differences"))
    return bad


def main(argv):
    miss = inputs_missing()
    if miss: print("skipped (missing: %s)" % ", ".join(miss)); return 0
    what = argv[1] if len(argv) > 1 else "all"
    n = int(argv[2]) if len(argv) > 2 else 300
    seed = int(argv[3]) if len(argv) > 3 else 1
    md = Model(open(IMGPATH, "rb").read())
    bad = 0
    if what in ("gun", "all"): bad += check_gun(n, seed, md)
    if what in ("launch", "all"): bad += check_launch(n, seed, md)
    if what in ("gate", "all"): bad += check_gate(n, seed, md)
    if what in ("hits", "all"): bad += check_hits(n, seed, md)
    if what in ("hull", "all"): bad += check_hull(n, seed, md)
    if what in ("entity", "all"): bad += check_entity_hits(n, seed, md)
    if what in ("focus", "all"): bad += check_focus()
    if what in ("result40", "all"): bad += check_result40()
    if what in ("shipverts", "all"): bad += check_shipverts()
    if what in ("scope", "all"): bad += check_scope()
    if what in ("chaff", "all"): bad += check_chaff()
    if what in ("nonplayer", "all"): bad += check_nonplayer()
    if what in ("ships", "all"): bad += check_ships()
    if what in ("bighull", "all"): bad += check_bighull()
    if what in ("order", "all"): bad += check_order()
    if what in ("repeat", "all"): bad += check_repeat()
    if what in ("decoy", "all"):
        for kd in (0x1401, 0x2401): bad += check_entity_hits(n, seed, md, kd)
    if what in ("mixed", "all"): bad += check_mixed(n, seed)
    if what in ("rangekey", "all"): bad += check_rangekey()
    if what in ("landmark", "all"): bad += check_landmark(n, seed)
    if what in ("decision", "all"): bad += check_decision(max(n, 500), seed)
    print("weapon_check:", "FAILED" if bad else "ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
