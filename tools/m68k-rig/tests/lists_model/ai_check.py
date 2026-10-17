"""The generic AI state routine 0x30A7E (docs/re/07-ai-and-weapons.md, sections 3 and 4): random records through a model written from the note
against the original in the Musashi rig.

    python3 ai_check.py [n] [seed]      (run under ulimit -v 2500000 and timeout)

The model covers the states 0 to 9 and 0x0A and the general steering (the states 2, 5, 7 and 9 included; the training manoeuvre states 0x0B-0x28 are checked
by training_step_check.py and note 08 section 8.5).  Compared after the call: the whole 0x200-byte record and the notice word V:4AA56.
Prints the counts; exit 1 on a difference.  Skips with exit 0 when extracted/interceptor.bin or librig.so is missing.
"""
import os, sys, random, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped (missing extracted/interceptor.bin or librig.so)"); sys.exit(0)
MODE = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].isdigit() else None   # "dives": the dive x drift run (docs/re/07 4.12)
_ARGS = [a for a in sys.argv[1:] if a.isdigit()]
N = int(_ARGS[0]) if _ARGS else 3000
SEED = int(_ARGS[1]) if len(_ARGS) > 1 else 1
img = open(IMGPATH, 'rb').read()
def iw(a): return struct.unpack('>H', img[a-0x10000:a-0x10000+2])[0]
def isw(a): return struct.unpack('>h', img[a-0x10000:a-0x10000+2])[0]
T_31316 = [isw(0x31316 + 2*i) for i in range(11)]
T_3132C = [isw(0x3132C + 2*i) for i in range(11)]
T_31342 = [isw(0x31342 + 2*i) for i in range(11)]
T_31358 = [isw(0x31358 + 2*i) for i in range(11)]
T_3136E = [isw(0x3136E + 2*i) for i in range(11)]

def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v
def s8(v): v &= 0xFF; return v - 256 if v & 0x80 else v

class Rec:
    def __init__(s, b): s.b = bytearray(b)
    def u8(s, o): return s.b[o]
    def w8(s, o, v): s.b[o] = v & 0xFF
    def u16(s, o): return (s.b[o] << 8) | s.b[o+1]
    def s16(s, o): return s16(s.u16(o))
    def w16(s, o, v): s.b[o] = (v >> 8) & 0xFF; s.b[o+1] = v & 0xFF
    def s32(s, o): return s32(struct.unpack('>I', bytes(s.b[o:o+4]))[0])
    def w32(s, o, v): s.b[o:o+4] = struct.pack('>I', v & 0xFFFFFFFF)

def norm(rig, x, y, z, length=0xC0):
    """the normaliser 0x29C0E on the vector (the rig runs the original; verified separately in note 04a P4)"""
    rig.run(0x29C0E, d0=length, d5=x & 0xFFFFFFFF, d6=y & 0xFFFFFFFF, d7=z & 0xFFFFFFFF)
    return s32(rig.reg('d5')), s32(rig.reg('d6')), s32(rig.reg('d7'))

def model(R, G, rig):
    """returns the notice posted (V:4AA56 value) or None"""
    notice = [None]
    cls = R.u8(0x62) & 0xF0
    if cls == 0x30:
        R.w8(0x64, 0); return None
    R.w16(0x7E, 0x1400)
    if not (R.u16(0) & 0x80):
        R.w8(0x64, 0); return None
    if G['pause']: return None
    scripted = R.u8(2) & 1
    oc = (R.u8(0x20) >> 1) & 1
    def y(): return R.s32(0x18)
    def sp6(): return R.s16(0x6C)
    def floor_alt():
        d4 = sp6() << 6
        return d4
    # ---- state entry
    def to_state1():          # 30b6a
        R.w8(5, 1); R.w16(0x4C, 0x1E)
        return L30b76()
    def to_state6():          # 309f6
        R.w8(5, 6)
        return L309fc()
    def end_routine():        # 30984
        if scripted:
            if s8(G['v4a852']) >= 0:        # 3095c
                R.w8(5, 0xA); R.w16(0x4C, 0x24 if G['v4a84b'] else 0x3C)
                return L30978()
            R.w16(0x4C, 10); return to_state6()
        t = s8(R.u8(0x38))
        if t >= 0 or R.u8(0x38) == 0xFF:
            R.w16(0x4C, 10); return to_state6()
        p = s8(G['v4a95f'])
        if p > 2 and (R.u8(0x64) & 0x60) != 0x60:
            R.w16(0x4C, 10); return to_state6()
        if p <= 0: c = 0x4B
        elif p == 1: c = 0x25
        elif p == 2: c = 0x12
        else: c = 4
        R.w16(0x4C, c); return to_state6()
    def L30978():
        if R.s16(0x4C) < 0:                  # 30a68
            R.w8(5, 0); R.w8(0x65, R.u8(0x65) & 3)
        return
    def L309fc():             # state 6
        if oc: return
        if scripted:
            if s8(G['v4a852']) < 0: return
            if R.s16(0x4C) < 0:
                R.w8(5, 8); R.w16(0x4C, 0x1E); return L30b76()
            return
        if R.s16(0x4C) < 0: return to_state1()
        d4 = floor_alt()
        if R.s32(0x42) < 0:
            if d4 < y(): return
            notice[0] = 0x3C
            return to_state1()
        d4 = 0x800000 - d4
        if d4 < y(): return to_state1()
        return
    def L30b76():
        if oc:
            p = R.s16(0x66)
            if p > 0x3840 or p < 0xAF0: return steer()
            return to_state6()
        if R.u8(5) != 8:
            if scripted: return steer()
            if R.s16(0x4C) < 0: return end_routine()
            return steer()
        if scripted:
            if s8(G['v4a852']) >= 0: return
            if R.s16(0x4C) < 0:
                R.w16(0x4C, 0xB); return to_state6()
        p = R.s16(0x66)
        if p >= 0xE10 and p < 0x6270: return steer()
        r = R.s16(0x6A)
        if r < 0x1C20: return steer()
        if r < 0x5460:
            R.w16(0x4C, 0x25); return to_state6()
        return steer()
    def steer():              # 30be8
        d0 = R.s16(0x2C)
        if d0 < 0: R.w8(0x64, 0); return
        if cls == 0:
            R.w16(0x26, R.u16(0x26) - 1)
            if R.s16(0x26) >= 0xA: R.w8(0x64, 0); return
        state = R.u8(5)
        X = s16(d0 - R.s16(6)) << 14
        Z = s16(R.s16(0x2E) - R.s16(8)) << 14
        fx = R.s16(0xC); fz = R.s16(0xE)
        tx = R.s16(0x30); tz = R.s16(0x32)
        c = R.u8(0x62)
        if oc or cls == 0 or c == 0x14 or c == 0x15:
            ty = R.s32(0x34)
        else:
            ty = sp6() << (6 if state == 8 else 7)
            if R.s32(0x42) < 0:
                if ty < y(): ty = R.s32(0x34)
                elif ty < R.s32(0x34): ty = R.s32(0x34)
            else:
                ty = 0x800000 - ty
                if ty > y(): ty = R.s32(0x34)
                elif ty < R.s32(0x34): pass
                else: ty = R.s32(0x34)
        if s8(R.u8(0x38)) >= 0:
            sh = s8(R.u8(0x38))
            tx = s32(tx << sh) ; ty = s32(ty << sh); tz = s32(tz << sh)
            tx &= 0x3FFF; tz &= 0x3FFF
        tx = s32(tx - fx); tz = s32(tz - fz)
        tx = s32(tx + X); tz = s32(tz + Z)
        ty = s32(ty - R.s32(0x10))
        vx, vy, vz = tx, ty, tz
        m = max(abs(vx), abs(vy), abs(vz))
        while m >= 0x4800:
            vx >>= 2; vy >>= 2; vz >>= 2; m >>= 2
        vx, vy, vz = norm(rig, vx, vy, vz)
        if state == 8:
            Lg = (0xC000, 0x10000, 0x18000, -0xC000, -0x10000, -0x18000, 0x30000, 0x80000)
        elif cls != 0x10:
            Lg = (0x10000, 0x10000, 0x20000, -0x10000, -0x10000, -0x20000, 0x20000, 0x20000)
        else:
            Lg = (0x30000, 0x40000, 0x50000, -0x30000, -0x40000, -0x50000, 0x80000, 0x80000)
        L4, L8, Lc, L14, L10, L18, L1c, L20 = Lg
        R.w8(0x64, R.u8(0x64) & 0x79)
        def col(a, b, cc): return s32(R.s16(a)*vx + R.s16(b)*vy + R.s16(cc)*vz)
        d2 = col(0x92, 0x98, 0x9E); d4 = abs(d2)
        d3 = col(0x94, 0x9A, 0xA0); d0 = abs(d3)
        if d4 > d0: R.w8(0x64, R.u8(0x64) & 0xF7)
        else:
            R.w8(0x64, R.u8(0x64) | 8); d2 = d3
        d3 = d2
        b3 = (R.u8(0x64) >> 3) & 1
        if d2 < 0:
            R.w8(0x64, R.u8(0x64) & 0xFE)
            lim = L14 if b3 else L10
            if d2 >= lim: return aligned(d2, d3, vx, vy, vz, Lg)
        else:
            R.w8(0x64, R.u8(0x64) | 1)
            lim = L4 if b3 else L8
            if d2 <= lim: return aligned(d2, d3, vx, vy, vz, Lg)
        if b3: return pitchpath(d2, vx, vy, vz, Lg)
        return rollpath(d2, vx, vy, vz, Lg)
    def tail():               # 3130c: the epilogue flag
        R.w8(0x64, R.u8(0x64) | 0x10)
    def aligned(d2, d3, vx, vy, vz, Lg):     # 312aa
        if (R.u8(0x62) & 0xF0) != 0x10:
            R.w16(0x50, 0); R.w16(0x52, 0); R.w16(0x54, 0)
        if not (R.u8(0x64) & 8):
            R.w8(0x64, R.u8(0x64) | 2); R.w8(0x64, R.u8(0x64) | 0x20)
        else:
            dd = s32(R.s16(0x96)*vx + R.s16(0x9C)*vy + R.s16(0xA2)*vz)
            if dd < 0:
                return pitchtail(d3, Lg, d3)    # 311e2 with d2 = d3 (d3 stays the signed value)
            R.w8(0x64, R.u8(0x64) | 4); R.w8(0x64, R.u8(0x64) | 0x40)
        R.w8(0x64, R.u8(0x64) ^ 1)
        tail()
    def keyroll(d3):          # 31112 and 3117e/3118c
        d2 = 0
        if (R.u8(0x64) & 0x60) == 0x60:
            if d3 == 0: d2 = 0
            else:
                if d3 > 0:
                    d2 = 0x80 if d3 > R.s16(0x58) else 0
                else:
                    d2 = 0x40 if d3 < R.s16(0x58) else 0
                if d2:
                    r = R.s16(0x6A)
                    if r > 0x3840:
                        if r <= 0x7030: d2 |= 8
                    else:
                        if r > 0x50: d2 |= 4
        elif R.u8(0x64) & 0x80:
            r = R.s16(0x6A)
            if r > 0x3840:
                if r <= 0x6EF0: d2 = 8
                else: d2 = 8 if d3 > 0 else (4 if d3 < 0 else 0)
            else:
                if r >= 0x190: d2 = 4
                else: d2 = 8 if d3 > 0 else (4 if d3 < 0 else 0)
        else:
            d2 = 8 if d3 > 0 else (4 if d3 < 0 else 0)
        R.w8(0x65, (R.u8(0x65) & 3) | d2)
    def keypitch(d3, v):      # 31272 / 3126e
        if d3 >= 0: d2 = 0x10 if d3 > R.s16(0x56) else 0
        else: d2 = 0x20 if d3 < R.s16(0x56) else 0
        k = R.u8(0x65)
        k = (k & 0xCF) if v else (k & 3)
        R.w8(0x65, k | d2)
    def rudder(d3):           # 30f4a
        if d3 == 0: d2 = 0
        elif d3 > 0: d2 = 0x80 if d3 > R.s16(0x58) else 0
        else: d2 = 0x40 if d3 < R.s16(0x58) else 0
        R.w8(0x65, (R.u8(0x65) & 3) | d2)
        if (R.u8(0x62) & 0xF0) == 0x10: return
        R.w16(0x52, d3); R.w16(0x54, 0); R.w16(0x50, 0)
    def rollpath(d2, vx, vy, vz, Lg):        # 30ebe with bit 3 clear
        L4, L8, Lc, L14, L10, L18, L1c, L20 = Lg
        d3 = abs(d2)
        if R.u8(0x64) & 2:
            if d3 < Lc: return
        if d3 < L20: R.w8(0x64, R.u8(0x64) | 0x80)
        if d3 >= L1c: R.w8(0x64, R.u8(0x64) & 0xDF)
        else:
            d4 = col3(vx, vy, vz)
            if d4 < 0: R.w8(0x64, R.u8(0x64) & 0xDF)
            else: R.w8(0x64, R.u8(0x64) | 0x20)
        R.w8(0x64, R.u8(0x64) & 0xFD)
        idx = min((d3 >> 16) & 0xFFFF, 10)
        if (R.u8(0x62) & 0xF0) == 0x10:
            return acpath(d2, idx, vx, vy, vz)
        t = T_31316[idx]
        if d2 >= 0: t = -t
        rudder(t)
        return
    def col3(vx, vy, vz): return s32(R.s16(0x96)*vx + R.s16(0x9C)*vy + R.s16(0xA2)*vz)
    def acpath(d2, idx, vx, vy, vz):         # 30f98
        c = R.u8(0x62)
        tbl = T_3136E if (c == 0x14 or R.u8(5) == 8) else T_31342
        d3 = tbl[idx]
        if sp6() >= 0x1800: d3 >>= 1
        d0 = d2
        if d2 < 0: d3 = -d3; d0 = -d2
        d4 = R.s16(0x6A)
        # the two tests in order: roll in 6000..22800 -> rudder tail; else pitch in 1601..27200 -> rudder tail
        if d4 >= 0x1770 and d4 <= 0x5910:
            return rudder(d3)
        p = R.s16(0x66)
        if p <= 0x6A40 and p > 0x640:
            return rudder(d3)
        d7 = col3(vx, vy, vz)
        if d7 < 0:
            d0 = (0x300000 - d0) + 0x300000
        d0 >>= 8
        w = s16(d0)
        if w > 0x3000: w = 0x3000
        elif w <= 0x20: w = 0x1000
        elif w < 0x1800: w = 0x1800
        d0 = w
        if R.u8(0x7A) in (3, 4):
            if d3 < 0:
                if (R.u8(0x64) & 0x60) == 0x60 and d4 >= 0x3840 and d4 < 0x6D60: d3 = 0xF0
                elif d4 <= 0x3840: pass
                elif d4 > 0x68B0: pass
                elif d4 > 0x6720: d3 = 0
                else: d3 = 0xF0
            else:
                if (R.u8(0x64) & 0x60) == 0x60 and d4 <= 0x3840 and d4 > 0x320: d3 = -240
                elif d4 >= 0x3840: pass
                elif d4 < 0x7D0: pass
                elif d4 < 0x960: d3 = 0
                else: d3 = -240
        s = R.u8(5)
        if s not in (0, 1):
            if s != 8: return
            if R.u16(2) & 0x80: return
        keyroll(d3)
        if (R.u8(0x62) & 0xF0) != 0x10:
            R.w16(0x54, d3); R.w16(0x52, 0); R.w16(0x50, 0)
        if R.u8(5) == 8: d0 >>= 1
        R.w16(0x7E, d0)
    def pitchpath(d2, vx, vy, vz, Lg):       # 3119e
        L4, L8, Lc, L14, L10, L18, L1c, L20 = Lg
        d3 = abs(d2)
        if R.u8(0x64) & 4:
            if d3 <= Lc: return
        return pitchtail2(d2, d3, vx, vy, vz, Lg, True)
    def pitchtail2(d2, d3, vx, vy, vz, Lg, check):
        L4, L8, Lc, L14, L10, L18, L1c, L20 = Lg
        if d3 >= L1c: R.w8(0x64, R.u8(0x64) & 0xBF)
        else:
            d4 = col3(vx, vy, vz)
            if d4 < 0: R.w8(0x64, R.u8(0x64) & 0xBF)
            else: R.w8(0x64, R.u8(0x64) | 0x40)
        return pitchtail(d2, Lg, d3)
    def pitchtail(d2, Lg, d3=None):          # 311e2: d3 is the register as the caller left it (|d2|, or the signed d2 from the aligned path)
        R.w8(0x64, R.u8(0x64) & 0xFB)
        if d3 is None: d3 = abs(d2)
        idx = s16(d3 >> 16)
        if idx > 10: idx = 10
        c = R.u8(0x62)
        if (c & 0xF0) != 0x10: base = 0x31316
        elif c == 0x14: base = 0x31358
        else: base = 0x3132C
        t = isw(base + 2 * idx)
        if d2 >= 0: t = -t
        s = R.u8(5)
        if s not in (0, 1):
            if s != 8: return
            if R.u16(2) & 0x80: return
        keypitch(t, False)
        if (c & 0xF0) != 0x10:
            R.w16(0x50, t); R.w16(0x52, 0); R.w16(0x54, 0)
    def simple_roll(d3):                     # 3117e
        d2 = 0 if d3 == 0 else (4 if d3 < 0 else 8)
        R.w8(0x65, (R.u8(0x65) & 3) | d2)
    def bankhold(d0, d5):                    # states 5 and 9
        d1 = 0x7080 - d0
        if R.s16(0x4C) < 0:
            R.w8(5, 0); R.w8(0x65, R.u8(0x65) & 3); return
        d2 = 0; d3 = T_31342[2]; d4 = R.s16(0x6A)
        if d4 >= 0x3840: d0 = d1
        if d4 >= d0: d2 = 1
        d4 = s16(d4 - d0)
        if d4 < 0: d4 = s16(-d4)
        if d4 >= d5:
            if d2: d3 = -d3
            simple_roll(d3)
        else:
            R.w8(0x65, R.u8(0x65) & 3)
        tail()
    def breakturn():                         # state 2
        d2 = 0; d3 = T_31342[8]; d0 = 0x1C20; d1 = 0x5460; d4 = R.s16(0x6A)
        if d4 >= 0x3840: d0 = d1
        if d4 >= d0: d2 = 1
        d4 = s16(d4 - d0)
        if d4 < 0: d4 = s16(-d4)
        if d4 >= 0x4B0:
            if d2: d3 = -d3
            simple_roll(d3)
        else:
            R.w8(5, 7); R.w16(0x4C, 0x20); R.w8(0x65, R.u8(0x65) & 3)
        tail()
    def breakpull():                         # state 7
        if R.s16(0x4C) < 0: return end_routine()
        keypitch(-T_3132C[4], False)
        tail()
    # ---- the main entry (30aae)
    def entry():
        s = R.u8(5)
        if s == 0: return L30b76()
        if not (s == 8 or s == 1 or oc or scripted):
            d4 = sp6() << 6
            if R.s32(0x42) < 0:
                if not (d4 < y()): return to_state1()
            else:
                d4 = 0x800000 - d4
                if d4 < y(): return to_state1()
        if s != 8 and G['mode'] == 5 and (R.u8(1) & 8) and R.s16(0x4C) <= 0:
            R.w8(5, 8)
            blk = 0x2DCE8 + iw(0x2DCE8 + 2*R.u8(0x3A))
            if isw(blk) >= 0:
                R.b[0x2C:0x34] = img[blk-0x10000:blk-0x10000+8]
                R.w32(0x34, isw(blk + 8))
                return entry()
            R.w8(5, s) if False else None
        # dispatch
        if s in (1, 3, 4, 8): return L30b76()
        if s == 6: return L309fc()
        if s == 0xA: return L30978()
        if s == 5: return bankhold(0x460, 0x1E0)
        if s == 9: return bankhold(0x280, 0x140)
        if s == 2: return breakturn()
        if s == 7: return breakpull()
        raise KeyError(s)
    entry()
    return notice[0]

REC = 0x4B23C + 0x200 * 4
def rand_rec(rng):
    b = bytearray(0x200)
    R = Rec(b)
    cls = rng.choice((0x10, 0x10, 0x12, 0x13, 0x14, 0x15, 0x11, 0x16, 0x20))
    R.w8(0x62, cls)
    R.w16(0, rng.choice((0x11C0, 0x11C8, 0x0140, 0x11C0 | 0x2000, 0x1140)))
    R.w8(2, rng.choice((0, 0, 0, 1)))
    R.w8(3, rng.choice((0, 0x80, 0x10)))
    R.w8(5, rng.choice((0, 0, 1, 2, 3, 4, 5, 6, 6, 7, 8, 8, 9, 0xA)))
    R.w8(0x20, rng.choice((0, 0, 0, 2, 0x10)))
    cx = rng.randint(60, 80); cz = rng.randint(60, 80)
    R.w16(6, cx); R.w16(8, cz); R.w16(0xC, rng.randint(0, 16383)); R.w16(0xE, rng.randint(0, 16383))
    R.w16(0x2C, rng.choice((-1, cx + rng.randint(-3, 3), cx + rng.randint(-40, 40))))
    R.w16(0x2E, cz + rng.randint(-3, 3) if rng.random() < .8 else cz + rng.randint(-40, 40))
    R.w16(0x30, rng.randint(0, 16383)); R.w16(0x32, rng.randint(0, 16383))
    R.w32(0x34, rng.choice((rng.randint(0, 0x8000), rng.randint(0, 0x3000), rng.randint(0, 0x300))))
    R.w8(0x38, rng.choice((0xFF, 0x80, 0x8E, 0x81, 0, 3, 0x85)))
    R.w8(0x3A, rng.randint(0, 6))
    R.w32(0x10, rng.randint(0, 0x8000)); R.w32(0x18, R.s32(0x10) << 8)
    R.w32(0x42, rng.choice((0, rng.randint(-3000, 3000), -rng.randint(0, 100000), rng.randint(0, 100000))))
    R.w16(0x4C, rng.randint(-5, 40))
    R.w16(0x6C, rng.randint(0, 0x3000)); R.w16(0x6E, R.u16(0x6C))
    R.w16(0x66, rng.choice((rng.randint(0, 28799), rng.randint(0, 600), rng.randint(28000, 28799), rng.randint(0, 28799))))
    R.w16(0x6A, rng.choice((rng.randint(0, 28799), rng.randint(0, 600), rng.randint(28000, 28799), rng.randint(5000, 24000))))
    R.w16(0x68, rng.randint(0, 28799))
    R.w16(0x56, rng.randint(-700, 700)); R.w16(0x58, rng.randint(-1500, 1500)); R.w16(0x5A, rng.randint(-1500, 1500))
    R.w8(0x64, rng.randint(0, 255))
    R.w8(0x65, rng.randint(0, 255))
    R.w8(0x7A, rng.choice((0, 3, 4, 5)))
    for o in (0x92, 0x94, 0x96, 0x98, 0x9A, 0x9C, 0x9E, 0xA0, 0xA2):
        R.w16(o, rng.randint(-16384, 16384))
    return bytes(R.b)

def main():
    rng = random.Random(SEED)
    rig = Rig(); rig.reset()
    ok = bad = skipped = 0; first = []
    seen = {}
    for n in range(N):
        b0 = rand_rec(rng)
        G = {'pause': 0, 'v4a852': rng.choice((0, 0xFF, 0xFE, 0x80)), 'v4a95f': rng.choice((0, 1, 2, 3, 4, 5)), 'mode': rng.choice((3, 5, 5, 6)), 'v4a84b': rng.randint(0, 1)}
        rig.reset()
        rig.poke(0x1F1D6, b'\x4e\x75')
        rig.poke(REC, bytes(b0))
        rig.w8(0x4A852, G['v4a852']); rig.w8(0x4A95F, G['v4a95f']); rig.w8(0x4A95E, G['mode']); rig.w8(0x4A84B, G['v4a84b'])
        rig.w16(0x4AA56, 0)
        st = b0[5]
        if st not in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA): skipped += 1; continue
        rc = rig.run(0x30A7E, a1=REC)
        got = rig.peek(REC, 0x200); gn = rig.u16(0x4AA56)
        R = Rec(b0)
        notice = model(R, G, rig)
        exp = bytes(R.b); en = notice if notice is not None else 0
        key = (st, ) 
        ch = (bytes(b0) != got); seen[(st, got[5], 'chg' if ch else 'same', (got[0x62]&0xF0))] = seen.get((st, got[5], 'chg' if ch else 'same', (got[0x62]&0xF0)), 0) + 1
        if got == exp and gn == en: ok += 1
        else:
            bad += 1
            if len(first) < 6:
                dif = [(hex(i), got[i], exp[i]) for i in range(0x200) if got[i] != exp[i]][:6]
                first.append((n, st, b0[0x62], hex(gn), hex(en), dif, rc))
    print("records:", N, "run:", ok + bad, "identical:", ok, "different:", bad, "state not modelled:", skipped)
    print('coverage (state before, state after, changed, class nibble): count'); print(sorted(seen.items()))
    for f in first: print("  ", f)
    sys.exit(1 if bad else 0)
def dives():
    """4.12: the same dive flown by a class 0x12 aircraft and by a class 0x15 cruise missile in the original's world update 0x26AF8 (the record is not AI valid, the
    way the trajectory harness stages dives); per step the three position longs and the velocity longs are logged; reported: the first step at which the x (and z)
    position of the two classes differ, whether they differ before the ground contact of the aircraft, and the x and z velocity longs at that step."""
    rng = random.Random(SEED)
    P = 0x4B23C
    rig = Rig()
    def fly(cls, y0, pitch, head, S, gear, steps):
        rig.reset()
        for a in (0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x1F1D6, 0x1A0DC, 0x1A042): rig.poke(a, b'\x4e\x75')
        rig.w8(0x4A95E, 1); rig.w16(0x4A984, 0x0840); rig.w8(0x4A83D, 0); rig.w8(0x4A844, 1); rig.w32(0x1D024, 0x700000)
        for a in range(0x4A83C, 0x4A848):
            if a not in (0x4A83D, 0x4A83E, 0x4A840): rig.w8(a, 1)
        rig.w16(P, 0x11C8); rig.w8(P + 0x62, 0x11); rig.w32(P + 0x18, 0x2000000)
        H = P + 0x200 * 8
        rig.w16(H, 0x11C0); rig.w16(H + 2, 0x0100); rig.w8(H + 0x62, cls); rig.w8(H + 0x5E, 8); rig.w8(H + 0x38, 0xFF); rig.w8(H + 0x5D, 0xFF); rig.w16(H + 0x2C, 0xFFFF)
        rig.w32(H + 0x72, 0x61A800); rig.w16(H + 0x4C, 30000); rig.w8(H + 0x7D, 3); rig.w8(H + 0x7C, gear)
        x0 = (100 << 22) + 1000000; z0 = (100 << 22) + 1000000
        rig.w16(H + 6, 100); rig.w16(H + 8, 100); rig.w32(H + 0x14, x0); rig.w32(H + 0x1C, z0); rig.w32(H + 0x18, y0); rig.w32(H + 0x10, y0 >> 8)
        rig.w16(H + 0xC, (x0 & 0x3FFFFF) >> 8); rig.w16(H + 0xE, (z0 & 0x3FFFFF) >> 8)
        rig.run(0x3202C, a1=H, d4=pitch, d5=head, d6=0)             # the angle words and the matrices at +0x80 and +0x92 from the angles
        rig.w16(H + 0x6C, S); rig.w16(H + 0x6E, S); rig.w8(H + 0x2B, 60)
        rows = []
        for k in range(steps):
            rig.run(0x26AF8, a6=0x710000)
            rows.append((rig.s32(H + 0x14), rig.s32(H + 0x18), rig.s32(H + 0x1C), rig.s32(H + 0x3E), rig.s32(H + 0x42), rig.s32(H + 0x46), rig.u16(H + 2), rig.u16(H) ))
        return rows
    n = int(os.environ.get("DIVES_N", "12")); steps = 40
    xdiff_air = xdiff_ground = same = 0
    for c in range(n):
        y0 = rng.randint(3000, 200000); pitch = rng.choice((rng.randint(300, 6000), rng.randint(22800, 28500))); head = rng.randint(0, 28799); S = rng.randint(500, 6000)
        a = fly(0x12, y0, pitch, head, S, 0xE0, steps); m = fly(0x15, y0, pitch, head, S, 0xE0, steps)
        g = next((i for i, r in enumerate(a) if r[6] & 0x80), None)           # the step of the aircraft's ground contact
        d = next((i for i in range(steps) if a[i][0] != m[i][0] or a[i][2] != m[i][2]), None)
        dy = next((i for i in range(steps) if a[i][1] != m[i][1]), None)
        print("case %2d y0 %6d pitch %5d head %5d S %4d: ground contact step %s, first x/z difference step %s, first y difference step %s%s" % (
            c, y0, pitch, head, S, g, d, dy, "" if d is None else "; dx %d dz %d vx %d/%d vz %d/%d" % (m[d][0] - a[d][0], m[d][2] - a[d][2], a[d][3], m[d][3], a[d][5], m[d][5])))
        if d is None: same += 1
        elif g is None or d < g: xdiff_air += 1
        else: xdiff_ground += 1
    print("dives: %d cases; x/z identical throughout %d; differ before the ground contact %d; differ at or after it %d" % (n, same, xdiff_air, xdiff_ground))
    sys.exit(0)

if MODE == "dives": dives()

main()
