"""Training mode (docs/re/08-game-flow.md, section 8): the note's rules against the original's code, in the Musashi rig.

    python3 training_check.py [SEED] [N]

Three parts, all over random states (no emulator log is needed):

  1. The instructor's manoeuvre states (the AI state routine 0x30A7E called with a scripted record, W2 bit 0 set): the note's
     transition table for the states 0x0A, 6, 8, 2, 7, 0x0B-0x28, written here in Python from the note's prose and table values,
     against the original's routine: the whole 0x200-byte record and the two variables the routine reads (V:4A852, V:4A84B) must agree.
  2. The lesson sequencer of the instructor record (0x27222, with its helper 0x273F0): the note's rules for the lesson number
     V:4A851, the flag V:4A852, the script cursor V:54E42, the state byte, the trigger bit, the prompts and the re-placement calls (the
     placement 0x2CE36 and the post routine 0x29BC8 are replaced by logging stubs, so a call and its argument are compared).
  3. The player's re-position (0x270A0): lesson advance, the prompt, the flag, the copy of the instructor and the formation offset.

The two tables at 0x2749A and 0x274E2 are checked structurally (8 rows each).  Inputs (git-ignored): extracted/interceptor.bin and
tools/m68k-rig/librig.so; the check skips with exit 0 when one is missing.
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH

I_REC, P_REC = 0x4BA3C, 0x4B23C
SENT = 0x7F0100          # stub log area: +0 posts, +2 last post, +4 placements, +6 calls of 0x37C32/0x37C4A
# ---------------------------------------------------------------- tables read from the image (the note's constants)
img = None


def s16(x):
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def tbl(addr, n=11):
    o = addr - 0x10000
    return [struct.unpack(">h", img[o + 2 * i:o + 2 * i + 2])[0] for i in range(n)]


class Rec:
    """The record bytes as the note's field table names them (offsets from the record base)."""

    def __init__(self, data):
        self.b = bytearray(data)

    def u8(self, o): return self.b[o]
    def w8(self, o, v): self.b[o] = v & 0xFF
    def u16(self, o): return struct.unpack_from(">H", self.b, o)[0]
    def s16(self, o): return struct.unpack_from(">h", self.b, o)[0]
    def w16(self, o, v): struct.pack_into(">H", self.b, o, v & 0xFFFF)
    def s32(self, o): return struct.unpack_from(">i", self.b, o)[0]


# ================================================================== part 1: the manoeuvre states (note 08, section 8.5)
class Ai:
    def __init__(self, rec, g852, g84b, T1, T2):
        self.r, self.g852, self.g84b, self.T1, self.T2 = rec, g852, g84b, T1, T2   # g852 as a signed byte

    # --- the helpers (note: "roll command", "pitch command")
    def roll_cmd(self, d3):            # roll bits 3 and 2 of the key byte from the sign of d3; the other bits keep bits 1-0 only
        d2 = 0 if d3 == 0 else (4 if d3 < 0 else 8)
        self.r.w8(0x65, (self.r.u8(0x65) & 3) | d2)

    def roll_zero(self):
        self.r.w8(0x65, (self.r.u8(0x65) & 3))

    def pitch_cmd(self, d3, keep=False):   # stick bits 5-4 from the target rate d3 against the pitch rate +0x56
        rate = self.r.s16(0x56)
        if d3 >= 0:
            d2 = 0 if d3 <= rate else 0x10
        else:
            d2 = 0 if d3 >= rate else 0x20
        k = self.r.u8(0x65)
        k = (k & 0xCF) if keep else (k & 3)
        self.r.w8(0x65, k | d2)

    def epi(self):
        self.r.w8(0x64, self.r.u8(0x64) | 0x10)

    def f(self, bit): return bool(self.r.u8(0x20) & bit)
    def fset(self, bit): self.r.w8(0x20, self.r.u8(0x20) | bit)
    def fclr(self, mask): self.r.w8(0x20, self.r.u8(0x20) & ~mask)
    def recentre(self): self.r.w8(0x03, self.r.u8(0x03) | 0x10)
    def st(self, v): self.r.w8(0x05, v)
    def c4c(self, v): self.r.w16(0x4C, v)

    # --- the end of a manoeuvre and the waiting states
    def end(self):
        if self.g852 >= 0:
            self.st(0x0A)
            self.c4c(0x24 if self.g84b else 0x3C)
            return                                  # no epilogue
        self.c4c(10)
        self.st(6)
        self.s06()

    def clr(self):
        self.st(0); self.r.w8(0x65, self.r.u8(0x65) & 3)

    def s0a(self):
        if self.r.s16(0x4C) < 0: self.clr()

    def s06(self):
        if self.r.u8(0x20) & 2: return
        if self.g852 < 0: return
        if self.r.s16(0x4C) < 0:
            self.st(8); self.c4c(0x1E); self.idle()

    def idle(self):                    # 0x30B76 for a scripted record with the target cell negative: state 8 and state 0
        if self.r.u8(0x20) & 2: return self.idle_ooc()
        if self.r.u8(0x05) != 8: return self.r.w8(0x64, 0) if False else self.clear64()
        if self.g852 >= 0: return
        if self.r.s16(0x4C) < 0:
            self.c4c(0x0B); self.st(6); return self.s06()
        p = self.r.s16(0x66)
        if 0x0E10 <= p < 0x6270: return self.clear64()
        ro = self.r.s16(0x6A)
        if ro < 0x1C20: return self.clear64()
        if ro < 0x5460:
            self.c4c(0x25); self.st(6); return self.s06()
        return self.clear64()

    def idle_ooc(self):                # state 8 or 0 with +0x20 bit 1 set: not exercised (constrained away by the generator)
        raise NotImplementedError

    def clear64(self): self.r.w8(0x64, 0)

    # --- one state
    def run(self):
        r = self.r
        r.w16(0x7E, 0x1400)
        s = r.u8(0x05)
        if s == 0: return self.idle()
        h = {0x0A: self.s0a, 6: self.s06, 8: self.idle, 0x0B: self.s0b, 0x0C: self.s0c, 0x0D: self.s0d, 0x0E: self.s0e,
             0x0F: self.s0f, 0x10: self.s10, 0x11: self.s11, 0x12: self.s12, 0x13: self.s13, 0x14: self.s14, 0x15: self.s15,
             0x16: self.s16, 0x17: self.s17, 0x18: self.s18, 0x19: self.s19, 0x1A: self.s1a, 0x1B: self.s1b, 0x1C: self.s1c,
             0x1E: self.s1e, 0x1F: self.s1f, 0x21: self.s21, 0x22: self.s22, 0x23: self.s23, 0x24: self.s24, 0x25: self.s25,
             0x26: self.s26, 0x27: self.s27, 0x28: self.s28, 2: self.s02, 7: self.s07}[s]
        h()

    # the aileron roll 0x0B-0x0D
    def s0b(self): self.fclr(0x18); self.st(0x0C); self.c4c(15); self.s0c()
    def s0c(self):
        if self.r.s16(0x4C) > 0: self.recentre(); return self.epi()
        self.st(0x0D); self.s0d()
    def s0d(self):
        d3 = self.T2[8]; d4 = self.r.s16(0x6A)
        if self.f(0x10): pass
        else:
            if not self.f(0x08):
                if d4 >= 0x3840: return self.roll_epi(d3)
                self.fset(0x08)
            if d4 < 0x3840: return self.roll_epi(d3)
            self.fset(0x10)
        if d4 < 0x320: return self.end()
        if d4 >= 0x6E00: d3 = 0
        self.roll_epi(d3)
    def roll_epi(self, d3): self.roll_cmd(d3); self.epi()
    def pitch_epi(self, d3): self.pitch_cmd(d3); self.epi()

    # the inside loop 0x0E-0x10
    def s0e(self): self.fclr(0x18); self.st(0x0F); self.c4c(15); self.s0f()
    def s0f(self):
        if self.r.s16(0x4C) > 0: self.recentre(); return self.epi()
        self.st(0x10); self.s10()
    def s10(self):
        d3 = -self.T1[6]; d4 = self.r.s16(0x66)
        if not self.f(0x10):
            if not self.f(0x08):
                if d4 <= 0x3840: return self.pitch_epi(d3)
                self.fset(0x08)
            if d4 > 0x3840: return self.pitch_epi(d3)
            self.fset(0x10)
        if d4 > 0x3840: return self.end()
        self.pitch_epi(d3)

    # the split-S 0x11-0x13
    def s11(self):
        if self.r.s32(0x18) <= 0x100000: return self.end()
        self.fclr(0x18); self.st(0x12); self.c4c(15); self.s12()
    def s12(self):
        if self.r.s16(0x4C) > 0: self.recentre(); return self.epi()
        self.st(0x13); self.s13()
    def s13(self):
        d3 = self.T2[8]; d4 = self.r.s16(0x6A)
        if not self.f(0x08):
            if d4 >= 0x3840: return self.roll_epi(d3)
            self.fset(0x08)
        if d4 == 0x3840:
            self.st(0x0F); self.c4c(2); return self.s0f()
        if d4 >= 0x35C0 or True:
            if d4 >= 0x35C0:
                self.recentre(); d3 = 0
        self.roll_epi(d3)

    # the vertical half-loop 0x14-0x16 (then the roll 0x0D)
    def s14(self): self.fclr(0x08); self.st(0x15); self.c4c(15); self.s15()
    def s15(self):
        if self.r.s16(0x4C) > 0: self.recentre(); return self.epi()
        self.st(0x16); self.s16()
    def s16(self):
        d3 = -self.T1[6]; d4 = self.r.s16(0x66)
        if not self.f(0x08):
            if d4 <= 0x3840: return self.pitch_epi(d3)
            self.fset(0x08)
        if d4 > 0x3840: return self.pitch_epi(d3)
        self.st(0x0D); self.s0d()

    # the barrel roll 0x17-0x19
    def s17(self): self.fclr(0x18); self.st(0x18); self.c4c(15); self.s18()
    def s18(self):
        if self.r.s16(0x4C) > 0: self.recentre(); return self.epi()
        self.st(0x19); self.s19()
    def s19(self):
        d3 = self.T2[7]; d4 = self.r.s16(0x6A)
        if not self.f(0x10):
            if not self.f(0x08):
                if d4 >= 0x3840: return self.s19_cmds(d3)
                self.fset(0x08)
            if d4 < 0x3840: return self.s19_cmds(d3)
            self.fset(0x10)
        if d4 < 0x3840: return self.end()
        self.s19_cmds(d3)
    def s19_cmds(self, d3):
        self.roll_cmd(d3)
        self.pitch_cmd(-self.T1[7], keep=True)
        self.epi()

    # inverted flight 0x1A-0x22
    def s1a(self): self.fclr(0x18); self.st(0x1B); self.c4c(15); self.s1b()
    def s1b(self):
        if self.r.s16(0x4C) > 0: self.recentre(); return self.epi()
        self.st(0x1C); self.s1c()
    def s1c(self):
        d3 = self.T2[8]; d4 = self.r.s16(0x6A)
        if not self.f(0x08):
            if d4 >= 0x3840: return self.roll_epi(d3)
            self.fset(0x08)
        if d4 == 0x3840:
            self.fclr(0x10); self.st(0x1E); self.c4c(10); return self.s1e()
        if d4 >= 0x35C0:
            self.recentre(); d3 = 0
        self.roll_epi(d3)
    def s1e(self):
        if self.r.s16(0x4C) > 0: self.recentre(); return self.epi()
        self.st(0x1F); self.s1f()
    def s1f(self):
        d3 = -self.T1[4]; d4 = self.r.s16(0x66); d2 = self.r.s16(0x6A)
        if 0x1C20 <= d2 <= 0x5460:
            if not self.f(0x10):
                if d4 > 0x3840: return self.pitch_epi(d3)
                self.fset(0x10)
            if d4 < 0x960: return self.pitch_epi(d3)
            return self.s1f_done()
        if not self.f(0x10):
            if d4 < 0x3840: return self.pitch_epi(d3)
            self.fset(0x10)
        if d4 < 0x6720: return self.s1f_done()
        self.pitch_epi(d3)
    def s1f_done(self):
        self.pitch_cmd(0)
        self.fclr(0x10); self.st(0x21); self.c4c(0x19); self.s21()
    def s21(self):
        if self.r.s16(0x4C) > 0: self.recentre(); return self.epi()
        self.st(0x22); self.s22()
    def s22(self):
        d3 = self.T1[4]; d4 = self.r.s16(0x66); d2 = self.r.s16(0x6A)
        if 0x1C20 <= d2 <= 0x5460:
            if not self.f(0x10):
                if d4 < 0x3840: return self.pitch_epi(d3)
                self.fset(0x10)
            if d4 > 0x6720: return self.pitch_epi(d3)
            return self.s22_done()
        if not self.f(0x10):
            if d4 > 0x3840: return self.pitch_epi(d3)
            self.fset(0x10)
        if d4 > 0x960: return self.s22_done()
        self.pitch_epi(d3)
    def s22_done(self):
        self.pitch_cmd(0)
        self.fset(0x08); self.st(0x16); self.s16()

    # take-off 0x23-0x26
    def s23(self):
        t = self.r.u8(0x2B)
        if (t - 256 if t > 127 else t) >= 0x60:
            self.r.w8(0x65, self.r.u8(0x65) & 0xFC)
        else:
            self.r.w8(0x65, self.r.u8(0x65) | 1)
        if self.r.s16(0x6E) < 0x840: return self.epi()
        self.st(0x24); self.c4c(0x3C); self.epi()
    def s24(self):
        if self.r.s16(0x4C) >= 0: return self.epi()
        self.fclr(0x10); self.st(0x25); self.s25()
    def s25(self):
        d3 = -0x28; d4 = self.r.s16(0x66)
        if not self.f(0x10):
            if d4 < 0x3840: return self.pitch_epi(d3)
            self.fset(0x10)
        if d4 >= 0x68B0: return self.pitch_epi(d3)
        self.pitch_cmd(0)
        self.r.w8(0x65, 0)
        self.st(0x26); self.s26()
    def s26(self):
        if self.r.s32(0x18) > 0x240000: return self.end()
        self.epi()

    # the break turn 0x27, 0x28, 2, 7
    def s27(self): self.st(0x28); self.c4c(15); self.s28()
    def s28(self):
        if self.r.s16(0x4C) > 0: self.recentre(); return self.epi()
        self.st(2); self.s02()
    def s02(self):
        d2 = 0; d3 = self.T2[8]; d0 = 0x1C20; d1 = 0x5460; d4 = self.r.s16(0x6A)
        if d4 >= 0x3840: d0 = d1
        if d4 >= d0: d2 += 1
        t = d4 - d0                                 # sub.w then bge: the branch follows the true sign of the difference
        d4 = s16(t)
        if t < 0: d4 = s16(-d4)
        if d4 >= 0x4B0:
            if d2: d3 = -d3
            return self.roll_epi(d3)
        self.st(7); self.c4c(0x20); self.r.w8(0x65, self.r.u8(0x65) & 3); self.epi()
    def s07(self):
        if self.r.s16(0x4C) < 0: return self.end()
        self.pitch_cmd(-self.T1[4]); self.epi()


def model_ai(rec, g852, g84b, T1, T2):
    r = rec
    if (r.u8(0x62) & 0xF0) == 0x30: r.w8(0x64, 0); return
    if not (r.u16(0) & 0x80): r.w8(0x64, 0); return r
    Ai(r, g852, g84b, T1, T2).run()


# ================================================================== part 2 and 3: the sequencer
ROW_A = 0x2749A      # 8 rows x 8 bytes: +0 prompt id, +2 name id
ROW_B = 0x274E2      # 8 rows x 8 bytes: +0 long (view << 8 | direction), +4 long (0x80000000 | manoeuvre state)


class Seq:
    """The lesson sequencer as the note states it (8.3 and 8.4): globals g, instructor record i, player record p."""

    def __init__(self, g, ins, ply):
        self.g, self.i, self.p = g, ins, ply
        self.posts, self.places = [], 0
        self.rowA = [struct.unpack_from(">hhl", img, ROW_A - 0x10000 + 8 * k) for k in range(8)]
        self.rowB = [struct.unpack_from(">ll", img, ROW_B - 0x10000 + 8 * k) for k in range(8)]

    def post(self, v): self.posts.append(v & 0xFFFF)

    def place(self):                              # 0x2CE36, logged
        self.places += 1

    def advance(self):                            # 0x273F0
        g, i = self.g, self.i
        L = (g["L"] + 1) & 0xFF
        if (L - 256 if L > 127 else L) > 7: L = 1
        g["L"] = L; g["85e"] = 0; g["cur"] = 0; i.w8(0x65, 0)
        d1 = L if (L - 256 if L > 127 else L) > 0 else 1
        d0 = g["6c"]
        if g["994"] == g["6c"]:                    # the viewed entity is the instructor: the direction row of the entity index
            d0 = self.rowB[g["6c"] & 7][0]
            g["96a"] = d0 & 0xFF
        if g["mode"] == 2 and g["83d"] != 0:
            v = (d0 & 0xFFFF) >> 8 & 0xFF
            g["83d"] = v if 0 < v < 128 else 0xFF
        g["910"] = 0xFF
        if d1 == 4 and i.s32(0x18) <= 0x100000: self.post(0x35)
        else: self.post(self.rowA[d1][1])

    def start7d(self):
        g, i, p = self.g, self.i, self.p
        if g["mode"] == 0x7D and not (i.u16(2) & 0x100):
            i.w8(5, 0)
            if p.u16(0x6E) == 0: return False
            i.w16(2, i.u16(2) | 0x100); g["cur"] = 0
        return True

    def instructor(self):                          # 0x27222
        g, i, p = self.g, self.i, self.p
        if not self.start7d(): return
        if g["866"]: return
        if not (i.u16(2) & 0x100): return
        if g["mode"] == 2:
            if not g["84b"]: return
            if p.u16(2) & 0x0800:                  # 0x27276: RETURN seen by the player
                p.w16(2, p.u16(2) & ~0x0800)
                i.w16(0, 0); self.place(); i.w16(2, i.u16(2) | 0x100); i.w8(0x2B, 0x60)
                self.advance()
                return self.walk(after_return=True)
        return self.walk(after_return=False)

    def walk(self, after_return):
        g, i = self.g, self.i
        while True:
            L = g["L"] - 256 if g["L"] > 127 else g["L"]
            if not after_return and L <= 0 and g["mode"] == 2:
                i.w8(0x2B, 0x60); g["968"] = 0xFE
                d0 = self.rowB[L][1] if 0 <= L < 8 else 0
                if d0 == 0: return
                if d0 > 0: raise NotImplementedError
                r = self.end_of_manoeuvre()
            else:
                after_return = False
                d0 = self.rowB[L][1] if 0 <= L < 8 else 0
                if d0 == 0: return
                if d0 > 0: raise NotImplementedError
                if g["cur"] == 0:                  # 0x273DC: start the manoeuvre
                    i.w16(2, i.u16(2) & ~0x0800); i.w8(5, d0 & 0xFF); g["cur"] = 1; return
                if i.u8(5) != 0: return
                i.w8(0x65, i.u8(0x65) & 0xC3)
                r = self.end_of_manoeuvre()
            if r != "again": return

    def end_of_manoeuvre(self):                    # 0x27314
        g, i = self.g, self.i
        go = g["mode"] == 2
        if not go:
            go = bool(i.u16(2) & 0x0800)
            i.w16(2, i.u16(2) & ~0x0800)
        if not go:
            if g["852"] & 0x80: return "done"
            g["852"] = 0xFF
            L = g["L"] - 256 if g["L"] > 127 else g["L"]
            n = L + 1 if L + 1 <= 7 else 1
            self.post(self.rowA[n][0])
            return "done"
        g["852"] = 0                               # 0x27362
        L = g["L"] - 256 if g["L"] > 127 else g["L"]
        if L >= 7:
            i.w16(0, 0); self.place(); i.w16(2, i.u16(2) | 0x100); i.w8(0x2B, 0x60)
        self.advance()
        return "again"

    def player(self):                              # 0x270A0, the training part (the first block, the pitch-rate sound, is kept inert)
        g, i, p = self.g, self.i, self.p
        if g["mode"] != 0x7D: return
        if not (p.u16(2) & 0x0800): return
        p.w16(2, p.u16(2) & ~0x0800)
        L = g["L"]
        if g["852"] != 0xFF:
            L = L + 1
            if (L - 256 if L > 127 else L) > 7: L = 1
        g["L"] = L & 0xFF
        n = L + 1
        if n > 7: n = 1
        self.post(self.rowA[n][0])
        g["852"] = 0xFE
        keep = p.u8(0x63)
        p.b[0:0xA4] = i.b[0:0xA4]                  # 41 longs
        p.w8(0x63, keep); p.w8(0x5E, 0); p.w8(0x62, 0x11)
        p.w16(2, p.u16(2) & ~0x0100); p.w8(5, 0); p.w8(0x65, 0)
        m = [i.s16(0x92 + 2 * k) for k in range(9)]
        d3, d4, d5 = 9, 1, -97
        o = [(d3 * m[3 * r] + d4 * m[3 * r + 1] + d5 * m[3 * r + 2]) for r in range(3)]
        def asr4(v):
            v &= 0xFFFFFFFF
            v = v - (1 << 32) if v & 0x80000000 else v
            return v >> 4
        px = [asr4(o[k]) + i.s32(0x14 + 4 * k) for k in range(3)]
        px = [((v + 2 ** 31) % 2 ** 32) - 2 ** 31 for v in px]
        for k in range(3): struct.pack_into(">i", p.b, 0x14 + 4 * k, px[k])
        d0, d1, d2 = px
        fx = (d0 >> 8) & 0x3FFF; fz = (d2 >> 8) & 0x3FFF
        cx = s16(((d0 >> 16) & 0xFFFF)) >> 6; cz = s16(((d2 >> 16) & 0xFFFF)) >> 6
        p.w16(6, cx); p.w16(8, cz); p.w16(0x0C, fx); p.w16(0x0E, fz)
        struct.pack_into(">i", p.b, 0x10, d1 >> 8)
        g["910"] = 0xFF


def main():
    global img
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    N = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
    if not os.path.exists(IMGPATH) or not os.path.exists(LIBPATH):
        print("skipped (missing extracted/interceptor.bin or librig.so)")
        return 0
    img = open(IMGPATH, "rb").read()
    rng = random.Random(seed)
    rig = Rig()
    T1, T2 = tbl(0x3132C), tbl(0x31342)
    bad = tot = 0

    # --- the two tables, structurally
    rowA = [struct.unpack_from(">hhl", img, ROW_A - 0x10000 + 8 * k) for k in range(8)]
    rowB = [struct.unpack_from(">ll", img, ROW_B - 0x10000 + 8 * k) for k in range(8)]
    ok = (rowA[0] == (0, 0, 0) and all(r[2] == 0 for r in rowA)
          and [r[0] for r in rowA[1:]] == [46, 49, 48, 50, 51, 47, 52] and [r[1] for r in rowA[1:]] == [23, 26, 25, 27, 43, 24, 28]
          and all(r[0] == 0x103 for r in rowB[:7]) and rowB[7][0] == 0x101
          and [r[1] & 0xFFFF for r in rowB] == [0x23, 0x0B, 0x27, 0x0E, 0x11, 0x14, 0x17, 0x1A] and all(r[1] < 0 for r in rowB))
    print("tables: 8 + 8 rows, structure %s" % ("as the note" if ok else "DIFFERENT"))
    bad += 0 if ok else 1

    # --- part 1: manoeuvre states
    states = [0x0A, 6, 8, 2, 7] + list(range(0x0B, 0x13 + 1)) + [0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1E, 0x1F, 0x21, 0x22,
                                                                  0x23, 0x24, 0x25, 0x26, 0x27, 0x28]
    near = [0x320, 0x35C0, 0x3840, 0x6E00, 0x1C20, 0x5460, 0x960, 0x6720, 0x68B0, 0x0E10, 0x6270, 0x4B0]
    diffs1 = 0
    per = {}
    for it in range(N):
        rig.reset()
        s = rng.choice(states)
        rec = bytearray(rng.getrandbits(8) for _ in range(0x200))
        r = Rec(rec); rec = r.b
        r.w16(0, 0x11C8 | rng.choice((0, 0x2000, 0x4000)))
        r.w16(2, 0x0100 | rng.getrandbits(16))
        r.w8(5, s); r.w8(0x62, rng.choice((0x10, 0x11, 0x14, 0x1F)))
        r.w8(0x20, rng.getrandbits(8) & 0xFD)                     # bit 1 (out of control) is not exercised
        r.w16(0x2C, rng.choice((0x8000, 0xFFFF, 0xFFFE)))        # no target cell: the steering part is not reached
        r.w16(0x4C, rng.choice((-3, -1, 0, 1, 2, 15, rng.randint(-40, 40))))
        for o in (0x66, 0x6A):
            r.w16(o, rng.choice((rng.choice(near) + rng.randint(-2, 2), rng.randint(0, 28799), rng.randint(0, 65535))))
        r.w16(0x56, rng.randint(-900, 900) if rng.random() < .8 else rng.randint(-32768, 32767))
        r.w16(0x6E, rng.choice((0x83F, 0x840, 0x841, rng.randint(0, 3000))))
        r.w8(0x2B, rng.choice((0x5F, 0x60, 0x61, rng.getrandbits(8))))
        y = rng.choice((0x100000, 0x100001, 0xFFFFF, 0x240000, 0x240001, 0x23FFFF, rng.randint(-2 ** 31, 2 ** 31 - 1)))
        struct.pack_into(">i", rec, 0x18, y if y < 2 ** 31 else y - 2 ** 32)
        g852 = rng.choice((0, 0xFE, 0xFF, 1)); g84b = rng.choice((0, 1))
        rig.poke(I_REC, bytes(rec))
        rig.w8(0x4A852, g852); rig.w8(0x4A84B, g84b); rig.w8(0x4A866, 0); rig.w8(0x4A867, 0)
        before = bytes(rec)
        rig.run(0x30A7E, a1=I_REC, a6=0x710000)
        got = rig.peek(I_REC, 0x200)
        m = Rec(before)
        model_ai(m, g852 - 256 if g852 > 127 else g852, g84b, T1, T2)
        tot += 1
        if got != bytes(m.b):
            diffs1 += 1
            per[s] = per.get(s, 0) + 1
            if diffs1 <= 5:
                d = [(hex(k), before[k], got[k], m.b[k]) for k in range(0x200) if got[k] != m.b[k]][:6]
                print("DIFF state %02X g852=%02X rol=%d: (offset, before, original, model) %s" % (s, g852, struct.unpack_from(">h",before,0x6A)[0], d))
    print("part 1 (manoeuvre states, 0x30A7E): %d random scripted records, %d differences %s" % (N, diffs1, per if per else ""))
    bad += diffs1

    # --- parts 2 and 3: the sequencer (0x27222) and the player's re-position (0x270A0), with logging stubs for 0x29BC8, 0x2CE36, 0x37C32
    GV = {"mode": (0x4A95E, 1), "L": (0x4A851, 1), "852": (0x4A852, 1), "cur": (0x54E42, 2), "84b": (0x4A84B, 1), "866": (0x4A866, 1),
          "6c": (0x4AA6C, 2), "994": (0x4A994, 2), "83d": (0x4A83D, 1), "96a": (0x4A96A, 1), "968": (0x4A968, 1), "910": (0x4A910, 1),
          "85e": (0x4A85E, 1), "854": (0x4A854, 1)}

    def install(rg):
        rg.poke(0x29BC8, bytes.fromhex("52790" + "07F0100" + "33C0007F0102" + "4E75"))
        rg.poke(0x2CE36, bytes.fromhex("5279007F0104" + "4E75"))
        rg.poke(0x37C32, bytes.fromhex("5279007F0106" + "4E75"))
        rg.poke(0x37C4A, bytes.fromhex("5279007F0106" + "4E75"))
        rg.poke(0x7F0100, bytes(16))

    diffs2 = diffs3 = 0
    act = {2: [0, 0, 0], 3: [0, 0]}      # part 2: states with a post, a placement, a state-byte start; part 3: states with a re-position
    n2 = max(N // 2, 1)
    for it in range(n2):
        rig.reset(); install(rig)
        for part in (2, 3):
            mode = rng.choice((2, 0x7D)) if part == 2 else 0x7D
            g = {"mode": mode, "L": rng.randint(0, 7), "852": rng.choice((0, 0xFE, 0xFF)), "cur": rng.choice((0, 0, 1, 2, 77)),
                 "84b": rng.choice((0, 1, 1)), "866": rng.choice((0, 0, 0, 0, 1)), "6c": 4, "994": rng.choice((0, 4)),
                 "83d": rng.choice((0, 1, 2, 0xFF)), "96a": rng.getrandbits(8), "968": rng.getrandbits(8), "910": rng.getrandbits(8),
                 "85e": rng.getrandbits(8), "854": 0}
            ib = bytearray(rng.getrandbits(8) for _ in range(0x200)); pb = bytearray(rng.getrandbits(8) for _ in range(0x200))
            ir, pr = Rec(ib), Rec(pb)
            ir.w16(0, rng.choice((0x11C8, 0x1000, 0x0148, 0))); ir.w16(2, (rng.getrandbits(16) & ~0x0900) | rng.choice((0, 0x100, 0x100, 0x800, 0x900)))
            ir.w8(5, rng.choice((0, 0, 0x0B, 0x23, 0x27)))
            struct.pack_into(">i", ir.b, 0x18, rng.choice((0x100000, 0x100001, 0xFFFFF, 0x300000, rng.randint(-2 ** 31, 2 ** 31 - 1))))
            pr.w16(2, (rng.getrandbits(16) & ~0x0900) | rng.choice((0, 0, 0x800)))
            pr.w16(0x6E, rng.choice((0, 0, 1, 500)))
            pr.w16(0x56, 0); pr.w16(0x58, 0)
            for k, (a, n) in GV.items():
                if n == 1: rig.w8(a, g[k])
                else: rig.w16(a, g[k])
            rig.w16(0x4A9FE, 0); rig.w8(0x4A940, 0)
            rig.poke(I_REC, bytes(ir.b)); rig.poke(P_REC, bytes(pr.b)); g_state0 = ir.u8(5)
            rig.poke(0x7F0100, bytes(16))
            m = Seq(dict(g), Rec(bytes(ir.b)), Rec(bytes(pr.b)))
            if part == 2:
                rig.run(0x27222, a1=I_REC, a2=P_REC, a6=0x710000)
                m.instructor()
            else:
                rig.run(0x270A0, a1=P_REC, a2=I_REC, a6=0x710000)
                m.player()
            gi, gm = {}, m.g
            for k, (a, n) in GV.items(): gi[k] = rig.u8(a) if n == 1 else rig.u16(a)
            ci, last, plc = rig.u16(0x7F0100), rig.u16(0x7F0102), rig.u16(0x7F0104)
            bad_here = []
            if part == 2: act[2][0] += ci > 0; act[2][1] += plc > 0; act[2][2] += rig.u8(I_REC + 5) != g_state0
            else: act[3][0] += ci > 0
            if gi != {k: gm[k] for k in gi}: bad_here.append(("globals", {k: (gi[k], gm[k]) for k in gi if gi[k] != gm[k]}))
            if ci != len(m.posts) or (m.posts and last != m.posts[-1]): bad_here.append(("posts", ci, last, m.posts))
            if plc != m.places: bad_here.append(("placements", plc, m.places))
            if rig.peek(I_REC, 0x200) != bytes(m.i.b): bad_here.append(("instructor record", [k for k in range(0x200) if rig.peek(I_REC, 0x200)[k] != m.i.b[k]][:6]))
            if rig.peek(P_REC, 0x200) != bytes(m.p.b): bad_here.append(("player record", [(hex(k), rig.peek(P_REC, 0x200)[k], m.p.b[k]) for k in range(0x200) if rig.peek(P_REC, 0x200)[k] != m.p.b[k]][:6]))
            if bad_here:
                if part == 2: diffs2 += 1
                else: diffs3 += 1
                if diffs2 + diffs3 <= 6: print("DIFF part %d mode %02X L=%d 852=%02X cur=%d:" % (part, mode, g["L"], g["852"], g["cur"]), bad_here)
    print("part 2 (sequencer 0x27222): %d random states, %d differences (%d with a post, %d with a placement, %d changing the state byte)" % (n2, diffs2, act[2][0], act[2][1], act[2][2]))
    print("part 3 (re-position 0x270A0): %d random states, %d differences (%d with a re-position)" % (n2, diffs3, act[3][0]))
    bad += diffs2 + diffs3
    print("training_check: %d cases in all (%d manoeuvre-state, %d sequencer, %d re-position), %d differences" % (N + 2 * n2, N, n2, n2, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
