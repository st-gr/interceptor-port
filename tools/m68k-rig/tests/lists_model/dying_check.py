"""The non-player dying path (docs/re/07-ai-and-weapons.md, section 5): the expiry rules of the step 0x2A2A2 and the whole timeline of a wreck, run in the
Musashi rig.

    python3 dying_check.py [n] [seed]      (run under ulimit -v 2500000 and timeout)

  1. the expiry (W0 bit 0x0400 set, the counter at or below 0): 2,000 random combinations of class, W0, W2 and counter against the rule of section 5.3
     (compared: the valid bit, the dying bit, the counter, the kill counter V:4A963);
  2. the timeline of a shot-down aircraft in the whole world update: crash flag at step 5, the kill counted 15 steps later while the wreck is still in
     the air, the record freed 15 steps after the touchdown (printed and checked).
Prints the counts; exit 1 on a difference.  Skips with exit 0 when extracted/interceptor.bin or librig.so is missing.
"""
import os, sys, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped (missing extracted/interceptor.bin or librig.so)"); sys.exit(0)
N = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
P = 0x4B23C
rig = Rig(); rig.reset()
STUBS = (0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x1F1D6, 0x1A0DC, 0x1A042)

def base():
    rig.reset()
    for a in STUBS: rig.poke(a, b'\x4e\x75')
    rig.w8(0x4A95E, 3); rig.w16(0x4A984, 0x0840); rig.w8(0x4A83D, 0); rig.w16(0x4A994, 0); rig.w16(0x4A996, 0)
    rig.w8(0x4A844, 1); rig.w32(0x4A7B2, 0x800000)
    for a in range(0x4A83C, 0x4A848):
        if a not in (0x4A83D, 0x4A83E, 0x4A840): rig.w8(a, 1)
    rig.w32(0x1D024, 0x700000)
    rig.w16(P, 0x11C8); rig.w8(P + 0x62, 0x11); rig.w32(P + 0x18, 0x1000000); rig.w32(P + 0x10, 0x10000)
    rig.w16(P + 0x6C, 0x1200); rig.w16(P + 0x6E, 0x1200)

# ---- 1. the expiry rule
bad = n1 = freed = counted = 0
for it in range(N):
    base()
    idx = rng.choice((4, 6, 8, 10, 12))
    H = P + 0x200 * idx
    cls = rng.choice((0x12, 0x13, 0x10, 0x16, 0x15, 0x14, 0x00, 0x01, 0x21))
    w0 = 0x0400 | 0x0040 | rng.choice((0, 0x1000)) | rng.choice((0, 0x0008)) | rng.choice((0, 0x8000)) | rng.choice((0, 0x2000)) | 0x0080
    w2 = rng.choice((0, 0x0080, 0x0100))
    cnt = rng.choice((-3, -1, 0, 0, 1, 5))
    rig.w16(H, w0); rig.w16(H + 2, w2); rig.w8(H + 0x62, cls); rig.w16(H + 0x4C, cnt)
    rig.w8(H + 0x5E, idx)
    rig.w16(0x4A992, 1); rig.w16(0x4AA6C, idx); rig.w16(0x4AA6E, 0x200 * idx)
    rig.w8(0x4A963, 7)
    rig.run(0x2A2A2, a1=H, a6=0x710000)
    got = (rig.u16(H) & 0x0440, rig.s16(H + 0x4C) if cnt <= 0 else None, rig.u8(0x4A963))
    # the model of section 5.3
    nib = cls & 0xF0
    exp_w = w0 & 0x0440; exp_k = 7
    if cnt <= 0:
        exp_w &= ~0x0400
        free = False
        if cls == 0x15 or nib == 0: free = True
        else:
            if nib == 0x10 and not (w0 & 8) and not (w2 & 0x80): exp_k += 1
            if w2 & 0x80: free = True
            elif w0 & 0x8000: free = True
        if free: exp_w &= ~0x40
        exp_c = 0x14 if free else None
        # a record that is not freed goes on with the step: only the bits above are compared, the counter is left to the step
        got = (rig.u16(H) & 0x0440, rig.s16(H + 0x4C) if free else None, rig.u8(0x4A963))
        exp = (exp_w, exp_c, exp_k)
        n1 += 1; freed += free; counted += (exp_k != 7)
    else:
        exp = (exp_w, None, 7)
    if got != exp:
        bad += 1
        if bad < 5: print("  differs:", hex(cls), hex(w0), hex(w2), cnt, got, exp)
print("1. expiry rule: %d random cases, %d past the counter (%d freed, %d counted), %d differences" % (N, n1, freed, counted, bad))

# ---- 2. the timeline
base()
for a in STUBS: rig.poke(a, b'\x4e\x75')
H = P + 0x200 * 8
rig.w16(H, 0x10C4); rig.w8(H + 0x62, 0x12); rig.w8(H + 0x5E, 8); rig.w8(H + 0x38, 0x80); rig.w8(H + 0x5D, 0xFF); rig.w8(H + 0x7A, 3)
rig.w16(H + 6, 70); rig.w16(H + 8, 70); rig.w32(H + 0x14, 70 << 22); rig.w32(H + 0x1C, 70 << 22); rig.w32(H + 0x18, 0x40000); rig.w32(H + 0x10, 0x400)
rig.w16(H + 0x6C, 0x1800); rig.w16(H + 0x6E, 0x1800); rig.w16(H + 0x2C, 0xFFFF); rig.w16(H + 0x4C, 40)
for o in (0x92, 0x9A, 0xA2, 0x80, 0x88, 0x90): rig.w16(H + o, 0x4000)
ev = {}
prev = None
for k in range(80):
    if k == 5:
        rig.w16(H, rig.u16(H) | 0x0200); rig.w8(H + 0x20, rig.u8(H + 0x20) | 2)
    rig.run(0x26AF8, a6=0x710000)
    s = (rig.u16(H) & 0x0440, rig.u8(0x4A963), rig.u16(H + 2) & 0x80)
    if s != prev: ev[k] = (hex(s[0]), s[1], hex(s[2]), rig.s16(H + 0x4C)); prev = s
print("2. timeline (step: W0 & 0x0440, V:4A963, W2 & 0x80, counter):", ev)
ks = sorted(ev)
ok2 = (ks == [0, 5, 20, 37, 52] and ev[5][0] == '0x440' and ev[20] == ('0x40', 1, '0x0', 0) and ev[37] == ('0x440', 1, '0x80', 15)
       and ev[52] == ('0x0', 1, '0x80', 20))
print("   crash at 5; kill counted at 20 (15 steps, wreck still airborne, record not freed); touchdown at 37 starts a second count-down; freed at 52 (counter 20, no second count):", "as stated" if ok2 else "DIFFERENT")

# ---- 3. a second impact after the first expiry (the state the result handler leaves for a hull or landmark hit, result 0x20, which does not set W2 0x0080)
base()
H = P + 0x200 * 8
rig.w16(H, 0x10C4); rig.w8(H + 0x62, 0x12); rig.w8(H + 0x5E, 8); rig.w8(H + 0x38, 0x80); rig.w8(H + 0x5D, 0xFF); rig.w8(H + 0x7A, 3)
rig.w16(H + 6, 70); rig.w16(H + 8, 70); rig.w32(H + 0x14, 70 << 22); rig.w32(H + 0x1C, 70 << 22); rig.w32(H + 0x18, 0x800000); rig.w32(H + 0x10, 0x8000)
rig.w16(H + 0x6C, 0x1800); rig.w16(H + 0x6E, 0x1800); rig.w16(H + 0x2C, 0xFFFF); rig.w16(H + 0x4C, 40)
for o in (0x92, 0x9A, 0xA2, 0x80, 0x88, 0x90): rig.w16(H + o, 0x4000)
log3 = []
for k in range(60):
    if k == 5: rig.w16(H, rig.u16(H) | 0x0200); rig.w8(H + 0x20, rig.u8(H + 0x20) | 2)
    if k == 25:        # the end state of the result handler for a hull hit: hit bit, out of control, inactive, dying, counter 15
        rig.w16(H, (rig.u16(H) | 0x8000 | 0x0400) & ~0x1000); rig.w8(H + 0x20, rig.u8(H + 0x20) | 2); rig.w16(H + 0x4C, 15)
    rig.run(0x26AF8, a6=0x710000)
    log3.append((rig.u8(0x4A963), (rig.u16(H) >> 6) & 1))
first = [k for k in range(60) if log3[k][0] == 1][0]; second = [k for k in range(60) if log3[k][0] == 2]
freed = [k for k in range(26, 60) if log3[k][1] == 0]
ok3 = bool(second) and bool(freed) and second[0] == freed[0]
print("3. second impact (hull or landmark, W2 bit 0x0080 still clear) after the first expiry: count 1 at step %d, count 2 at step %s, freed at step %s: %s" % (first, second[:1], freed[:1], "COUNTED TWICE" if ok3 else "once"))
# ---- 4. other classes: a ship with the dying bit is never counted down by the gate; the seat classes are freed by their own step at once
base()
H = P + 0x200 * 14
rig.w16(H, 0x0440 | 0x0400); rig.w8(H + 0x62, 0x20); rig.w16(H + 0x4C, 3); rig.w8(H + 0x5E, 14)
for k in range(8): rig.run(0x26AF8, a6=0x710000)
ship = (hex(rig.u16(H) & 0x0440), rig.s16(H + 0x4C))
print("4. ship (class 0x20) with the dying bit, counter 3, after 8 passes:", ship, "(the gate never decrements a ship's counter)")

# ---- 5. the creator of secondary objects 0x2AA8E: first free of 16, nothing when none
OBJ = 0x4D23C
bad5 = n5 = 0
for it in range(300):
    base()
    pat = [rng.random() < (0.9 if it % 3 == 0 else 0.5) for _ in range(16)]
    if it % 5 == 0: pat = [True] * 16
    for i in range(16):
        rig.w16(OBJ + 0x20 * i, 0x0040 if pat[i] else 0)
        rig.w16(OBJ + 0x20 * i + 2, 0x55); rig.w16(OBJ + 0x20 * i + 4, 0x66)
    src = P + 0x200 * 8
    for o in range(0x20): rig.w8(src + o, rng.randint(0, 255))
    d1 = rng.choice((0, 0x11, 0x12)); d0 = rng.choice((0, 1, 0x20, 0x21))
    before = rig.peek(OBJ, 0x200); srcb = rig.peek(src, 0x20)
    rig.w8(0x4A910, 0)
    rig.run(0x2AA8E, a1=src, d0=d0, d1=d1)
    after = rig.peek(OBJ, 0x200)
    free = [i for i in range(16) if not pat[i]]
    if not free:
        ok = after == before and rig.u8(0x4A910) == 0
    else:
        k = free[0]
        exp = bytearray(before)
        exp[0x20 * k:0x20 * k + 0x20] = srcb
        exp[0x20 * k:0x20 * k + 2] = b'\x00\x40'; exp[0x20 * k + 4:0x20 * k + 6] = b'\x00\x7f'
        mid = d1 if d1 else (0x13 if (15 - k) & 1 else 0x12)      # d0 is the scan's loop counter by then: the parity of the slot index
        exp[0x20 * k + 2:0x20 * k + 4] = bytes([mid >> 8, mid & 255])
        ok = after == bytes(exp) and rig.u8(0x4A910) == 0xFF
    n5 += 1
    if not ok:
        bad5 += 1
        if bad5 < 3: print('  differs:', sum(pat), d0, d1, [(i, after[i], (before if not free else exp)[i]) for i in range(0x200) if after[i] != (before if not free else exp)[i]][:5], hex(rig.u8(0x4A910)))
print("5. secondary-object creator: %d cases (%d with all 16 in use), %d differences" % (n5, sum(1 for i in range(300) if i % 5 == 0), bad5))
sys.exit(1 if bad or bad5 or not ok2 else 0)
