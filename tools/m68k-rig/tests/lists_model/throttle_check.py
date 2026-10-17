"""The throttle step of a non-player record (docs/re/07-ai-and-weapons.md, section 4.11): the key bits 0 and 1 of +0x65 turned into the throttle byte +0x2B by the
speed step 0x15ED4 for a scripted record in the original's world update, against a model written from the code.

    python3 throttle_check.py [n] [seed]      (run under ulimit -v 2500000 and timeout)

Compared per step: +0x2B, the ramp byte +0x39, the key byte +0x65 and W2 (bits 0x2000, 0x0400, 0x0008).  Skips with exit 0 when the inputs are missing.
"""
import os, sys, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped (missing extracted/interceptor.bin or librig.so)"); sys.exit(0)
N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
P = 0x4B23C
def sb(v): v &= 0xFF; return v - 256 if v & 0x80 else v

def model(thr, r39, key, w2, b4, fuel):
    """one step; returns thr, r39, key, w2"""
    if fuel == 0:
        return 0, r39, key, w2 & ~8
    keys = key & 3
    if keys == 0:
        return thr, (r39 & 0xF0) | 1, key, w2
    cnt = (r39 & 0xF) + 1
    if cnt > 8: step = 8
    else: step = cnt; r39 = (r39 & 0xF0) | cnt
    step >>= 1
    if keys == 1:
        w2 &= ~0x0400
        s = sb(thr + step)
        thr = s & 0xFF
        if s >= 0x78:
            if not (w2 & 0x2000):
                w2 |= 0x2000; key &= 0xFC
            else:
                if not (w2 & 8): w2 |= 8
            thr = 0x78
        return sb(thr), r39, key, w2
    if keys == 2:
        w2 &= ~0x2000
        if w2 & 8:
            w2 &= ~8; thr = sb(thr - 1)
        elif sb(thr) < 0:
            thr = sb(thr) - step
            if thr < -32: thr = -32
        else:
            thr = sb(thr - 4)
            if thr < 0:
                if b4 & 8: thr = 0
                elif not (w2 & 0x0400):
                    key &= 0xFC; thr = 0; w2 |= 0x0400
        return sb(thr), r39, key, w2
    return sb(thr), r39, key, w2

rig = Rig()
bad = steps = 0
for run in range(max(4, N // 500)):
    rig.reset()
    for a in (0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x1F1D6, 0x1A0DC, 0x1A042): rig.poke(a, b'\x4e\x75')
    rig.w8(0x4A95E, 3); rig.w16(0x4A984, 0x0840); rig.w8(0x4A844, 1); rig.w32(0x1D024, 0x700000)
    for a in range(0x4A83C, 0x4A848):
        if a not in (0x4A83D, 0x4A83E, 0x4A840): rig.w8(a, 1)
    rig.w16(P, 0x11C8); rig.w8(P + 0x62, 0x11)
    H = P + 0x200 * 8
    rig.w16(H, 0x11C0); rig.w16(H + 2, 0x0100); rig.w8(H + 0x62, 0x12); rig.w8(H + 0x5E, 8); rig.w8(H + 0x38, 0xFF); rig.w8(H + 0x5D, 0xFF)
    rig.w16(H + 0x2C, 0xFFFF); rig.w16(H + 6, 70); rig.w16(H + 8, 70); rig.w32(H + 0x14, 70 << 22); rig.w32(H + 0x1C, 70 << 22); rig.w32(H + 0x18, 0x800000)
    rig.w32(H + 0x72, 0x61A800); rig.w16(H + 0x6C, 0x1800); rig.w16(H + 0x6E, 0x1800); rig.w16(H + 0x4C, 30000); rig.w8(H + 0x2B, rng.choice((0, 9, 60, 100, 118, -20 & 255)))
    for o in (0x92, 0x9A, 0xA2, 0x80, 0x88, 0x90): rig.w16(H + o, 0x4000)
    mode = 0; hold = 0
    for k in range(500):
        if hold == 0:
            hold = rng.choice((1, 2, 5, 20, 60)); mode = rng.choice((0, 1, 1, 2, 2, 3))
        hold -= 1
        rig.w8(H + 0x65, (rig.u8(H + 0x65) & 0xFC) | mode)
        pre = (sb(rig.u8(H + 0x2B)), rig.u8(H + 0x39), rig.u8(H + 0x65), rig.u16(H + 2), rig.u8(H + 4), rig.u32(H + 0x72))
        rig.run(0x26AF8, a6=0x710000)
        got = (sb(rig.u8(H + 0x2B)), rig.u8(H + 0x39), rig.u8(H + 0x65) & 3, rig.u16(H + 2) & 0x2408)
        thr, r39, key, w2 = model(pre[0], pre[1], pre[2], pre[3], pre[4], pre[5])
        exp = (thr, r39, key & 3, w2 & 0x2408)
        steps += 1
        if got != exp:
            bad += 1
            if bad < 6: print("  step", k, "pre", pre[:4], "got", got, "exp", exp)
            break

# ---- part 2: an unscripted AI record flying 1,500 steps against the player: the key byte the AI leaves is read by a stub in front of the speed step
import struct
STUB = (bytes.fromhex('13E90065007F0300') + bytes.fromhex('13E90039007F0301') + bytes.fromhex('13E9002B007F0302') + bytes.fromhex('33E90002007F0304')
        + bytes.fromhex('13E90004007F0306') + bytes.fromhex('23E90072007F0308') + bytes.fromhex('11FC0001') + bytes.fromhex('4EF900015ED4'))
STUB = STUB.replace(bytes.fromhex('11FC0001'), bytes.fromhex('13FC0001007F030F'))
bad2 = steps2 = 0
for run in range(2):
    rig.reset()
    for a in (0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x1F1D6, 0x1A0DC, 0x1A042): rig.poke(a, b'\x4e\x75')
    rig.poke(0x8F0000, STUB); rig.poke(0x2A4BC, struct.pack('>I', 0x8F0000))
    rig.w8(0x4A95E, 3); rig.w16(0x4A984, 0x0840); rig.w8(0x4A844, 1); rig.w32(0x1D024, 0x700000); rig.w8(0x4A95F, run)
    for a in range(0x4A83C, 0x4A848):
        if a not in (0x4A83D, 0x4A83E, 0x4A840): rig.w8(a, 1)
    rig.w16(P, 0x11C8); rig.w8(P + 0x62, 0x11); rig.w32(P + 0x14, 70 << 22); rig.w32(P + 0x1C, 70 << 22); rig.w32(P + 0x18, 0x600000)
    rig.w16(P + 0x6, 70); rig.w16(P + 0x8, 70)
    for o in (0x92, 0x9A, 0xA2, 0x80, 0x88, 0x90): rig.w16(P + o, 0x4000)
    H = P + 0x200 * 8
    rig.w16(H, 0x11C0); rig.w8(H + 0x62, 0x12); rig.w8(H + 0x5E, 8); rig.w8(H + 0x38, 0x80); rig.w8(H + 0x5D, 0xFF); rig.w8(H + 0x7A, 3)
    rig.w16(H + 0x2C, 0xFFFF); rig.w16(H + 6, 70); rig.w16(H + 8, 70); rig.w32(H + 0x14, 70 << 22); rig.w32(H + 0x1C, (70 << 22) + 0x300000); rig.w32(H + 0x18, 0x600000)
    rig.w32(H + 0x72, 0x61A800); rig.w16(H + 0x6C, 0x1800); rig.w16(H + 0x6E, 0x1800); rig.w8(H + 0x5F, 0)
    for o in (0x92, 0x9A, 0xA2, 0x80, 0x88, 0x90): rig.w16(H + o, 0x4000)
    for k in range(1500):
        rig.w8(0x7F030F, 0)
        rig.run(0x26AF8, a6=0x710000)
        if not rig.u8(0x7F030F): continue
        m = rig.peek(0x7F0300, 16)
        pre = (sb(m[2]), m[1], m[0], (m[4] << 8) | m[5], m[6], struct.unpack('>I', m[8:12])[0])
        thr, r39, key, w2 = model(*pre)
        got = (sb(rig.u8(H + 0x2B)), rig.u8(H + 0x39), rig.u8(H + 0x65) & 3, rig.u16(H + 2) & 0x2408)
        exp = (thr, r39, key & 3, w2 & 0x2408)
        steps2 += 1
        if got[0] != exp[0] or got[3] != exp[3]:
            bad2 += 1
            if bad2 < 4: print("  AI step", k, "pre", pre[:5], "got", got, "exp", exp)
            break
print("throttle step of an unscripted AI record: %d steps compared, %d differences" % (steps2, bad2))
print("throttle step of a scripted record: %d steps compared, %d runs ended at a difference" % (steps, bad))
sys.exit(1 if bad or bad2 else 0)
