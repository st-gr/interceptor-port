"""The non-player step and the lesson cycle (docs/re/08-game-flow.md, 8.3, 8.4, 8.12): facts of the original's whole world update 0x26AF8
and of the pitch-rate routine 0x1557C, run in the Musashi rig.

    python3 training_step_check.py

  1. pitch rate without a stick command: index 0 decays by a quarter, any other record by half (0x1557C on 8 rates x 2 indices);
  2. the first pass of a placed record: no motion, W0 bit 0x0100 cleared, speed and state byte updated;
  3. the wait states 0x0A and 6 in the input conversion: wing levelling with a dead band of 280, W0 bit 0x2000 set (2 states x 10 rolls);
  4. the unscripted part 2 lead: nothing moves, state 6 with counter 10 every 11 passes (real placement 0x1D4E2 + 0x2CE36, mode 0x7D);
  5. the part 1 lesson cycle: the steps at which V:4A851 changes over 1100 passes;
  6. the gear byte +0x7C that the placement writes (0xE0) and its effect on the cycle (gear down: starts 385, 542, 702, 792, 974).
Prints the counts; exit 1 on a difference.  Skips with exit 0 when extracted/interceptor.bin or librig.so is missing.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped (missing extracted/interceptor.bin or librig.so)"); sys.exit(0)
I, P = 0x4BA3C, 0x4B23C
bad = 0


def base(mode, view=1):
    r = Rig(); r.reset()
    for a in (0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2): r.poke(a, b'\x4e\x75')      # clock, effect and AI-hook routines return at once
    r.w8(0x4A95E, mode); r.w16(0x4A984, 0x0840); r.w8(0x4A83D, view); r.w8(0x4A844, 1); r.w32(0x4A7B2, 0x800000)
    for a in range(0x4A83C, 0x4A848):
        if a not in (0x4A83D, 0x4A83E, 0x4A840): r.w8(a, 1)
    return r


def part1():
    r = base(2); r.w8(0x4A84B, 1); r.w16(0x4A994, 4); r.w16(0x4A996, 0x800)
    r.run(0x2CE36, a6=0x710000); r.w16(I + 2, 0x0100); r.w8(I + 0x65, 0)
    return r


# 1. pitch-rate decay
got = {}
for idx in (0, 4):
    base_ = P + idx * 0x200
    for v in (1000, -1000, 640, 333, -333, 7, -7, 3200):
        r = base(1); r.w32(0x4B2AE, 0x1000000); r.w16(0x4AA6C, idx)
        r.w16(base_, 0x11C8); r.w16(base_ + 2, 0x0100 if idx else 0); r.w16(base_ + 0x56, v); r.w16(base_ + 0x66, 0x1000)
        r.run(0x1557C, a6=0x710000); got[(idx, v)] = r.s16(base_ + 0x56)
def decay(v, sh):
    if -15 <= v <= 15: return v - 1 if v > 0 else v + 1      # within 15 units: one unit toward zero
    return v - (v >> sh) if v > 0 else -((-v) - ((-v) >> sh))
def quarter(v): return decay(v, 2)
def half(v): return decay(v, 1)
d1 = [(k, g) for k, g in got.items() if g != (quarter(k[1]) if k[0] == 0 else half(k[1]))]
print("1. pitch rate without a stick command: 16 cases (8 rates, index 0 and index 4), %d differences from 'quarter for the player, half for the others'" % len(d1)); bad += len(d1)

# 2. first pass
r = base(2); r.w8(0x4A84B, 1); r.w16(0x4A994, 4); r.w16(0x4A996, 0x800); r.run(0x2CE36, a6=0x710000)
w0 = r.u16(I); r.w16(I + 2, 0x0100); p0 = (r.s32(I + 0x14), r.s32(I + 0x18), r.s32(I + 0x1C))
r.run(0x26AF8, a6=0x710000); p1 = (r.s32(I + 0x14), r.s32(I + 0x18), r.s32(I + 0x1C)); w1 = r.u16(I)
r.run(0x26AF8, a6=0x710000); p2 = (r.s32(I + 0x14), r.s32(I + 0x18), r.s32(I + 0x1C))
ok2 = (w0 & 0x0100) and not (w1 & 0x0100) and p1 == p0 and p2 != p1
print("2. first pass of a placed record: W0 %04X -> %04X, no motion in pass 1, motion in pass 2: %s" % (w0, w1, "ok" if ok2 else "DIFFERENT")); bad += 0 if ok2 else 1

# 3. wing levelling
d3 = 0; n3 = 0
for st in (0x0A, 6):
    for roll, want in ((0, 0), (100, 0), (280, 0), (281, 4), (2000, 4), (14400, 4), (20000, 8), (28519, 8), (28520, 0), (28700, 0)):
        r = part1(); r.run(0x26AF8, a6=0x710000)
        r.w8(I + 5, st); r.w16(I + 0x4C, 30); r.w16(I + 0x6A, roll); r.w16(I + 0x66, 0); r.w16(I, r.u16(I) & ~0x2000)
        r.w8(0x4A851, 3); r.w8(0x4A852, 0); r.run(0x26AF8, a6=0x710000)
        n3 += 1
        if (r.u8(I + 0x65) & 0x0C) != want or not (r.u16(I) & 0x2000): d3 += 1
print("3. wait states 0x0A and 6, wing levelling: %d cases, %d differences" % (n3, d3)); bad += d3

# 4. unscripted lead
r = base(0x7D, 0); r.w8(0x4A900, 0); r.w8(0x4A901, 0x11); r.w8(0x4A94F, 1)
r.run(0x1D4E2, a6=0x710000); r.run(0x2CE36, a6=0x710000)
seq = []; pos0 = (r.s32(I + 0x14), r.s32(I + 0x18), r.s32(I + 0x1C))
for k in range(23):
    r.run(0x26AF8, a6=0x710000); seq.append((r.u8(I + 5), r.s16(I + 0x4C)))
moved = (r.s32(I + 0x14), r.s32(I + 0x18), r.s32(I + 0x1C)) != pos0
want = [(6, 10)] + [(0, 9 - i) for i in range(10)] + [(6, 10)] + [(0, 9 - i) for i in range(10)] + [(6, 10)][:0]
want = want[:22] + [(6, 10)] if len(want) >= 22 else want
ok4 = (not moved) and seq[:12] == [(6, 10)] + [(0, 9 - i) for i in range(10)] + [(6, 10)]
print("4. unscripted part 2 lead, 23 passes: moved=%s, state/counter %s...: %s" % (moved, seq[:3], "ok" if ok4 else "DIFFERENT %s" % seq)); bad += 0 if ok4 else 1

# 5. the part 1 cycle
r = part1(); prev = r.u8(0x4A851); ch = []
for k in range(1100):
    r.run(0x26AF8, a6=0x710000); L = r.u8(0x4A851)
    if L != prev: ch.append((k, L)); prev = L
want5 = [(0, 1), (93, 2), (196, 3), (384, 4), (541, 5), (690, 6), (780, 7), (962, 1), (1055, 2)]
print("5. part 1 lesson starts (step, lesson):", ch, "ok" if ch == want5 else "DIFFERENT"); bad += 0 if ch == want5 else 1

# 6. the gear byte of the placement and the cycle
r = base(2); r.w8(0x4A84B, 1); r.w16(0x4A994, 4); r.w16(0x4A996, 0x800); r.run(0x2CE36, a6=0x710000)
gear = r.u8(I + 0x7C)
def cycle(gb):
    r = part1()
    if gb is not None: r.w8(I + 0x7C, gb)
    prev = r.u8(0x4A851); ch = []
    for k in range(1100):
        r.run(0x26AF8, a6=0x710000); L = r.u8(0x4A851)
        if L != prev: ch.append((k, L)); prev = L
    return ch
late = [(0, 1), (93, 2), (196, 3), (385, 4), (542, 5), (702, 6), (792, 7), (974, 1), (1067, 2)]
c0, c60, ce0 = cycle(0x00), cycle(0x60), cycle(0xE0)
ok6 = gear == 0xE0 and ce0 == want5 and c0 == late and c60 == late
print("6. placement writes +0x7C = 0x%02X; cycle with gear 0xE0 %s, with gear 0x00 or 0x60: %s" % (gear, "as part 5" if ce0 == want5 else "DIFFERENT", c0)); bad += 0 if ok6 else 1
sys.exit(1 if bad else 0)
