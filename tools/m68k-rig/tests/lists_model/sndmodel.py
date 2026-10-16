"""A Python model of the original's sound library and its callers (docs/re/10-sound.md); sound_check.py compares it with the 68000 code.

Everything here is the note's rule, written as executable text: the 37 objects, the voice slots, the player, the per-blank driver (script step, register
write, deltas, timers), the audio interrupt handler, the wrappers (engine, wind, tone, noise, gun, ground, gear, launch), the per-pass routine's pitch and
volume arithmetic.  No sample data is held: an object has a data id and a length.
"""

# ---- the objects (note 10, section 2): n -> (file, byte offset, length, period, count, reload, next)  [file None: synthesised]
MUSIC_A = [(0x0D, "texti0a", 2044, 13, 0, 0x0F), (0x0F, "texti0b", 2012, 1, 0, 0x11), (0x11, "texti1", 32770, 2, 2, 0x13), (0x13, "texti2", 32786, 0, 0, 0x15),
           (0x15, "texti1", 32770, 2, 2, 0x17), (0x17, "texti2", 32786, 0, 0, 0x19), (0x19, "texti3", 32914, 2, 2, 0x1B), (0x1B, "texti4", 32720, 0, 0, 0x1D),
           (0x1D, "texti3", 32914, 2, 2, 0x1F), (0x1F, "texti4", 32720, 0, 0, 0x21), (0x21, "texti5", 65590, 3, 3, 0x11)]


def objects():
    """object number -> dict(file, off, len, period, count, reload, next, script); voice pairs share data (even number: voice 0, odd: voice 1)"""
    T = {}
    def o(n, file, off, ln, period=358, count=1, reload=0, nxt=0, script=None):
        T[n] = dict(file=file, off=off, len=ln, period=period, count=count, reload=reload, next=nxt, script=script)
    o(0, "textcpt", 0, 13202, count=-1); o(1, "textcpt", 0, 5580, count=-1)
    o(2, "textegn", 0, 13818, count=-1); o(3, "textegn", 0x370, 13818 - 0x370, count=-1)
    o(4, None, 0, 32, count=-1, script="tone"); o(5, "texttre", 0, 3462)
    o(6, None, 0, 2048, count=-1, script="noise")
    o(8, "textwnd", 0, 20000, count=-1); o(9, "textwnd", 0x792, 13818 - 0x370 - 0x792, count=-1)
    o(0xA, None, 0, 2048, period=150, count=-1, script="gun"); o(0xB, "textger", 0, 4124, count=-1)
    o(0xC, "textegn", 0, 13818, count=-1, script="launch")
    for n, f, ln, c, r, nx in MUSIC_A:
        o(n, f, 0, ln, count=c, reload=r, nxt=nx); o(n + 1, f, 0, ln, count=c, reload=r, nxt=nx + 1)
    o(0x23, "text201", 0, 568, period=179, count=-1); o(0x24, "text201", 0, 568, period=181, count=-1)
    return T


# ---- the scripts (section 3): (field, value) pairs; field 0x2C wait (0: end), 0x40/0x44 loop on counter 0/1 to a byte offset
F_PER, F_VOL, F_PD, F_VD, F_C0, F_C1, F_WAIT, F_LOOP0, F_LOOP1 = 8, 0xC, 0x18, 0x1C, 0x24, 0x28, 0x2C, 0x40, 0x44
SCRIPTS = {
    "tone": [[0x24, 5], [0x28, 3], [8, 0x8C0000], [0xC, 0x280000], [0x2C, 5], [8, 0x8C0000], [0xC, 0], [0x2C, 1], [0x44, 0x10], [0x2C, 5], [0x40, 8], [0x2C, 0]],
    "noise": [[0xC, 0x3C0000], [0x1C, -65536], [0x2C, 0x3C], [0xC, 0], [0x2C, 0]],
    "gun": [[0x24, 1], [0xC, 0], [0x1C, 0], [0x2C, 2], [0xC, 0], [0x2C, 1], [0x40, 8], [8, 0x1180000], [0xC, 0x140000], [0x1C, -61440], [0x2C, 0xA], [0x2C, 0]],
    "launch": [[0xC, 0xB0000], [0x1C, 0x20000], [0x2C, 8], [0xC, 0x3E0000], [0x1C, -65536], [0x2C, 0x3C], [0xC, 0], [0x2C, 0]],
    "gear": [[8, 0x2BC0000], [0x18, -458752], [0x2C, 0xF], [0x18, 0], [0x2C, -1]],
}


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def idiv(a, b):
    """signed division truncating toward zero, as the CPU's division and the runtime's long division"""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


class Obj:
    def __init__(self, n, spec):
        self.n = n; self.spec = spec
        self.period = spec["period"] << 16; self.vol = 63 << 16
        self.count = spec["count"]; self.reload = spec["reload"]; self.next = spec["next"]
        self.pdelta = self.vdelta = 0; self.wait = 0; self.pos = 0; self.ptimer = self.vtimer = 0
        self.c0 = self.c1 = 0
        self.script = [list(p) for p in SCRIPTS[spec["script"]]] if spec["script"] else None


class Lib:
    """The sound library: objects, four voice slots, the master level, the hardware registers it writes (per voice: period, volume, dma, data id, length)."""

    def __init__(self):
        self.spec = objects()
        self.o = {n: Obj(n, s) for n, s in self.spec.items()}
        self.slot = [None] * 4
        self.master = 0x3F
        self.hw = [dict(per=0, vol=0, dma=False) for _ in range(4)]
        self.writes = []            # (voice, 'lc'|'len'|'dma'|'per'|'vol', value) in order: the register writes of the library
        self.loads = []             # (voice, object number) for every block the handler loaded
        self.addr = {}              # object number -> the data address the rig's loader gave it (optional: the 'lc' writes carry it)
        self.en12 = self.en13 = 0   # V:4AC12, V:4AC13: the enable bits the loader sets

    # ---- hardware ---------------------------------------------------------------------------------------------------------
    def _w(self, v, k, x):
        self.hw[v][k] = x; self.writes.append((v, k, x))

    def regs(self, v, o):                       # 0x55260
        per = (o.period >> 16) & 0xFFFF
        if per & 0x8000 or per < 0x7C: per = 0x7C
        vol = (o.vol >> 16) & 0x3F
        if vol > self.master: vol = self.master
        self._w(v, "per", per); self._w(v, "vol", vol)

    def irq(self, v, depth=0):                  # 0x55158, the audio interrupt of voice v
        o = self.slot[v]
        if o is None:
            self._w(v, "vol", 0); self._w(v, "per", 0x7C); self._w(v, "dma", False)
            return
        self.loads.append((v, o.n))
        self._w(v, "lc", self.addr.get(o.n))
        self._w(v, "len", o.spec["len"] // 2)
        was = self.hw[v]["dma"]
        self._w(v, "dma", True)
        self.regs(v, o)
        if o.count != -1:
            o.count -= 1
            if o.count < 0:
                self.slot[v] = self.o[o.next] if o.next else None
                o.count = o.reload
        if not was and depth == 0:               # DMA start raises the interrupt again at once
            self.irq(v, 1)

    def stop(self, v):                          # 0x19C58
        self.slot[v] = None; self.irq(v)

    def start(self, n, v, vol):                 # 0x19C7C
        o = self.o.get(n)
        if o is None: return
        self.stop(v)
        o.vol = s32(vol << 16); self.slot[v] = o; self.irq(v)

    def step_script(self, v, o):                # 0x55292
        if o.script is None or o.wait == 0: return
        o.wait = s32(o.wait - 1)
        if o.wait != 0: return
        while True:
            f, val = o.script[o.pos // 8]; o.pos += 8
            if f < 0x2C:
                if f == 8: o.period = s32(val)
                elif f == 0xC: o.vol = s32(val)
                elif f == 0x18: o.pdelta = s32(val)
                elif f == 0x1C: o.vdelta = s32(val)
                elif f == 0x24: o.c0 = val
                elif f == 0x28: o.c1 = val
                else: raise ValueError("script field %X" % f)
                continue
            if f == 0x2C:
                o.wait = s32(val)
                if val == 0:
                    self.slot[v] = None; self.irq(v)
                return
            c = "c0" if f == 0x40 else "c1"
            n = getattr(o, c)
            if n == 0:
                o.pos = val; continue
            n -= 1; setattr(o, c, n)
            if n == 0: continue
            o.pos = val

    def vbl(self):                              # 0x551D8
        for v in range(4):
            o = self.slot[v]
            if o is None: continue
            self.step_script(v, o)
            self.regs(v, o)
            o.period = s32(o.period + o.pdelta); o.vol = s32(o.vol + o.vdelta)
            if o.ptimer:
                o.ptimer -= 1
                if o.ptimer == 0: o.pdelta = 0
            if o.vtimer:
                o.vtimer -= 1
                if o.vtimer == 0: o.vdelta = 0

    # ---- wrappers (each tests what the original tests: the enable bits V:4AC12, V:4AC13 and whether the object exists) -------------------------------------
    side = None

    def _side(self, k, v):
        if self.side is not None: self.side[k] = v

    def tone(self, p):                          # 0x1A042 (nine parameters)
        o = self.o.get(4)
        if o is None: return
        self.stop(3)
        sc = o.script
        sc[0][1] = p[7]; sc[1][1] = p[6]
        sc[2][1] = s32(p[0] << 16); sc[3][1] = s32(p[1] << 16); sc[4][1] = p[2]
        sc[5][1] = s32(p[3] << 16); sc[6][1] = s32(p[4] << 16); sc[7][1] = p[5]; sc[9][1] = p[8]
        o.pos = 0; o.wait = 1
        self.start(4, 3, 0)

    def noise(self, a, b, rnd11):               # 0x1A0DC (a amplitude, b duration; rnd11 is drawn only when the sound runs)
        o = self.o.get(6)
        if not (self.en12 & 1):
            self._side("notice", 0xFA); self._side("v4a84f", 2); return False
        if o is None: return False
        self.stop(2)
        sc = o.script
        sc[0][1] = s32(a << 16); sc[1][1] = idiv(-(a << 16), b); sc[2][1] = b
        o.pos = 0
        o.period = s32((rnd11() * 4 + 0x231E) << 16)
        o.wait = 1
        self.start(6, 2, 0)
        return True

    def gun(self, a, b):                        # 0x1A2F0
        o = self.o.get(0xA)
        if o is None: return
        self.stop(2)
        o.wait = 0
        o.script[0][1] = a; o.script[1][1] = s32(b << 16)
        o.pos = 0; o.wait = 1
        self.start(0xA, 2, 0)

    def ground(self, vol, cam=True):            # 0x1A18C
        o = self.o.get(5)
        if not (self.en13 & 4) or not cam or o is None: return
        o.period = s32(0x136 << 16); o.pdelta = s32(-2 << 16); o.count = 1
        self.start(5, 2, vol)

    def gear(self, vol, cam=True):              # 0x1A1E6
        o = self.o.get(0xB)
        if not (self.en13 & 0x40) or not cam or o is None: return
        self.stop(2)
        o.script = [list(p) for p in SCRIPTS["gear"]]
        o.pos = 0; o.wait = 1
        self.start(0xB, 2, vol)

    def stop2(self):                            # 0x1A24C
        self.stop(2)

    def launch(self, v, rnd3):                  # 0x1A258 (the caller tests the enable bit and the object)
        o = self.o[0xC]
        self.stop(2)
        o.script[3][1] = s32(v << 16); o.script[4][1] = idiv(-((v - 1) << 16), 0x3C)
        o.pos = 0
        o.period = s32((rnd3() + 0x8C) << 16)
        o.wait = 1
        self.start(0xC, 2, 0)

    def f152c6(self, pitch, vol, rnd3):         # 0x152C6: the launch sound, or the external engine pair a little higher
        if (self.en12 & 2) and 0xC in self.o:
            self.launch(vol, rnd3); self._side("v4a84f", 0)
        elif self.en12 & 2:
            self._side("v4a84f", 0)
        else:
            self.engine_ext(pitch + 0x16, vol)

    def phrase(self, v):                        # 0x19CE6: the start-up sound (the caller's flag V:4A88F is self.flag)
        if self.flag != 0: return
        if self.en13 & 0x80:
            for i in range(4): self.stop(i)
            self.start(0xD, 0, 0x3F); self.start(0xE, 1, 0x3F); self.flag = 2
        elif self.en12 & 4:
            self.start(0x23, 0, v); self.start(0x24, 1, v); self.flag = 2
        else:
            for i in range(4): self.stop(i)

    def mission_tone(self, v):                  # 0x19D7A
        if self.en12 & 4:
            self.start(0x23, 0, v); self.start(0x24, 1, v)

    flag = 0

    def engine_cockpit(self, pitch, vol):       # 0x19DB2
        if self.en13 & 1:
            if 1 not in self.o: return
            self._pair(0, 1, pitch, vol)
        else:
            self.engine_ext(pitch, vol >> 2)

    def engine_ext(self, pitch, vol):           # 0x19E46
        if (self.en13 & 2) and 3 in self.o:
            self._pair(2, 3, pitch, vol)

    def _pair(self, a, b, pitch, vol):
        oa, ob = self.o[a], self.o[b]
        ob.vdelta = oa.vdelta = ob.pdelta = oa.pdelta = 0
        oa.period = s32(pitch << 16); ob.period = s32((pitch + 2) << 16)
        self.start(a, 0, vol); self.start(b, 1, vol)

    def glide(self, pitch, vol, ticks):         # 0x19EFA: voices 0 and 1 slide to (pitch, vol) over `ticks` blanks
        if not (self.en13 & 2): return
        s0, s1 = self.slot[0], self.slot[1]
        if s1 is None or s0 is None: return
        pd = idiv(s32(pitch << 16) - s0.period, ticks); vd = idiv(s32(vol << 16) - s0.vol, ticks)
        for o in (s1, s0):
            o.pdelta = pd; o.ptimer = ticks; o.vdelta = vd; o.vtimer = ticks

    def glide_b(self, pitch, vol, ticks):       # 0x19EBE
        self.glide(pitch, vol if self.en13 & 1 else vol >> 2, ticks)

    def wind(self, vol, rnd6):                  # 0x19F9A
        if not (self.en13 & 0x10):
            self.stop(0); self.stop(1); return
        if 9 not in self.o: return
        self.o[8].period = s32((rnd6() + 0x168) << 16); self.o[9].period = s32((rnd6() + 0x168) << 16)
        self.start(8, 0, vol); self.start(9, 1, vol)


class Lfsr:
    """The random source 0x55BE4 / 0x55C32 (a 32-bit shift register, taps at bits 0 and 3, seed 0x47091247 in the image)"""

    def __init__(self, seed=0x47091247):
        self.s = seed

    def bit(self):
        s = self.s
        b = (s & 1) ^ ((s >> 3) & 1)
        s = (s >> 1) & 0x7FFFFFFF
        if b: s = (s + 0x80000000) & 0xFFFFFFFF
        self.s = s
        return b

    def bits(self, n):                           # 0x55C32: n bits, the first drawn is the most significant
        acc = 0
        for _ in range(n): acc = (acc << 1) + self.bit()
        return acc

    def fill(self, n):                           # 0x55C66: n bytes of 8 bits each
        return bytes(self.bits(8) for _ in range(n))


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def sar(v, n):
    return v >> n                                # Python's shift on negative ints is arithmetic


def tone_buffer():
    """object 4's sample (0x55B88): 16 bytes of +127 then 16 bytes of -126"""
    return bytes([0x7F] * 16 + [0x82] * 16)


# ---- the per-pass routine 0x14AA0 (section 5): state -> list of (wrapper, args) ------------------------------------------
def f154e6(x):
    return s16(sar(x, 1) + sar(x, 3))


def engine_volume(S, a):                         # 0x1530E
    if a == 0:
        return 0xC if (S["w2"] & 0x80) and S["w6e"] != 0 else 0
    if S["v4a84f"] != 0: a = 0x3F
    elif a > 8: a = 0x3F - sar(0x78 - a, 2)
    else: a = 0x3C - sar(0x78 - a, 1)
    if S["view"] != 0:
        if S["v4a86d"] != 0:
            s = S["v4a96a"] if S["v4a96a"] < 128 else S["v4a96a"] - 256
            if s > 4:
                s = 8 - s
                if s < 0: s = 0
            if s in (0, 1): a = f154e6(a)
            elif s == 2: a -= sar(a, 2)
            elif s == 3: a -= sar(a, 3)
            k = S["view"] - 1
            if k == 0: a -= sar(a, 3)
            elif k == 1: a -= sar(a, 2)
            elif k == 2: a = f154e6(a)
            elif k == 3: a = sar(a, 1)
        elif S["v4abfa"] != 0:
            a = idiv(a * 32, S["v4abfa"])
            if a > 0x54: a = 0x54
        if (S["w2"] & 8) == 0: a -= sar(a, 2)
        if a <= 0: a = 1
        return a
    b941 = S["v4a941"] - 256 if S["v4a941"] >= 128 else S["v4a941"]       # a signed byte: -1 when idle
    if b941 > 5: a = sar(a, 1)
    return a


def wind_volume(S):                              # 0x15502
    if (S["cls"] & 0xF0) == 0x30: return 0x10
    w = abs(s16(S["w6e"])); w = sar(w, 9)
    if w > 0x3F: w = 0x3F
    if S["view"] == 0: w = sar(w, 1)
    if w == 0: w = 1
    return w


def pass_calls(S):
    """What 0x14AA0 asks of the library in one pass: a list of (name, args), the new notice byte, the new gear byte and V:4A84F.
    S keys: notice, v4a966, v4a865, v4a866, v4a84d, gear, view, v4a86d, v4a879, cls, w2, t2b, w56, w5a, w6e, v4a96a, v4abfa, v4a941, v4a84f, en13, en12"""
    C = []
    out = dict(notice=S["notice"], gear=S["gear"], v4a84f=S["v4a84f"], v4a870=S.get("v4a870", 0))
    if S["v4a966"] or S["v4a865"] or S["v4a866"] or not S["v4a84d"]:
        return C, out
    G = S["gear"]
    if G & 0x80:
        if S["view"] == 0: C.append(("gear", (10,)))
        elif S["v4a86d"] != 0: C.append(("gear", (5,)))
        out["gear"] = G & 0x7F
    else:
        G = (G - 1) & 0xFF
        if G == 0:
            C.append(("stop2", ())); out["gear"] = 0
    # the explosion and ejection bang: V:4A870 counts down in every ungated pass and fires at 0 or below (signed byte)
    if S.get("v4a870", 0) != 0:
        c = (S["v4a870"] - 1) & 0xFF
        if (c - 256 if c >= 128 else c) <= 0:
            amp = S["v4a922"] - 256 if S["v4a922"] >= 128 else S["v4a922"]
            if S["view"] == 0: amp = sar(amp, 1)
            C.append(("noise", (amp, S["v4a921"])))
            out["v4a870"] = 0
            if not (S["en12"] & 1):                    # the failed noise wrapper posts its notice and a hold of 2, which this pass's engine block already sees
                out["notice"] = 0xFA; out["v4a84f"] = 2; S = dict(S, v4a84f=2)
        else:
            out["v4a870"] = c
    N = S["notice"]
    if N == 0 and (S["v4a879"] & 3) != 0:
        return finish(S, C, out)
    ext = S["view"] != 0
    if (S["cls"] & 0xF0) == 0x30:
        C.append(("wind", (wind_volume(S),)))
        return finish(S, C, out, clear=True)
    t = S["t2b"] if S["t2b"] < 128 else S["t2b"] - 256
    a = abs(t)
    v6 = 0x3D
    P = engine_volume(S, a)
    if P == 0:
        C.append(("wind", (wind_volume(S),)))
        return finish(S, C, out, clear=True)
    c = abs(sar(s16(S["w5a"]), 4)); d = abs(sar(s16(S["w56"]), 4))
    if c < d: c += sar(s16(S["w56"]), 4)
    if a >= 0x78: a -= c
    else:
        a += c
        if a > 0x78: a = 0x78
    a = s16(a)
    en0 = S["en13"] & 1
    if ext:
        P -= sar(P, 2)
        if S["v4a86d"] == 0 and S["v4abfa"] != 0:
            v6 = idiv(v6 * 32, S["v4abfa"])
            if v6 > 0x3F: v6 = 0x3F
        b = 0x320 - 2 * a - sar(a, 1) - sar(a, 2)
        if N == 0xFE: C.append(("eng_ext", (b, P)))
        elif N == 0xFD: C.append(("eng_ext", (b + 0x20, v6)))
        elif N == 0xFC: C.append(("eng_ext", (b + 0x20, P)))
        elif N == 0xFB: C.append(("launch", (b, v6))); out["v4a84f"] = 0 if S["en12"] & 2 else out["v4a84f"]
        elif N == 0xFA: C.append(("eng_ext", (b + 0x16, min(2 * v6, 0x3F))))
        else: C.append(("glide", (b, P, 0x20)))
    else:
        if P < 0x28: P = 0x28
        b = 0x310 - 2 * a - sar(a, 2)
        if N == 0xFE:
            C.append(("eng_cockpit", (b, P)) if (S["w2"] & 8) == 0 else ("eng_ext", (b, sar(P, 2))))
        elif N == 0xFD: C.append(("eng_ext", (b + 0x23, sar(v6, 1))))
        elif N == 0xFC: C.append(("eng_cockpit", (b + 0x20, P)))
        elif N == 0xFB: C.append(("launch", (b, v6))); out["v4a84f"] = 0 if S["en12"] & 2 else out["v4a84f"]
        elif N == 0xFA: C.append(("eng_ext", (b + 0x16, min(2 * v6, 0x28))))
        elif S["v4a84f"] != 0: C.append(("glide_b", (b, 0xA if en0 else 0x28, 0x20)))
        elif (S["w2"] & 8) == 0 or not en0: C.append(("glide_b", (b, P, 0x20)))
        else: C.append(("glide_b", (b, sar(P, 2) + sar(P, 3), 0x20)))
    return finish(S, C, out, clear=True)


def finish(S, C, out, clear=False):
    if clear: out["notice"] = 0
    f = out["v4a84f"]                            # the launch wrapper has cleared it when it ran
    if f > 0 and f < 128:
        f -= 1; out["v4a84f"] = f
        if f == 0 and S["view"] == 0 and (S["w2"] & 8) == 0: out["notice"] = 0xFE
    return C, out
