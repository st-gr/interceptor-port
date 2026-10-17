"""The ground contact of a non-player record (docs/re/07-ai-and-weapons.md, section 4.12): an aircraft record stepped by the original's world update into the floor,
for each class, gear state and speed: the floor height, the ground bit W2 0x0080, the active bit W0 0x1000 and the lift-off.

    python3 ground_check.py      (run under ulimit -v 2500000 and timeout)
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped (missing extracted/interceptor.bin or librig.so)"); sys.exit(0)
P = 0x4B23C
rig = Rig()
bad = 0
for cls, gear, S, floor in ((0x12, 0x00, 0x1800, 0x708), (0x12, 0xE0, 0x1800, 0x2EE), (0x14, 0x00, 0x1800, 0xA8C), (0x12, 0x00, 0x0300, 0x708), (0x12, 0xE0, 0x0300, 0x2EE)):
    rig.reset()
    for a in (0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x1F1D6, 0x1A0DC, 0x1A042): rig.poke(a, b'\x4e\x75')
    rig.w8(0x4A95E, 1); rig.w16(0x4A984, 0x0840); rig.w8(0x4A83D, 0); rig.w8(0x4A844, 1); rig.w32(0x1D024, 0x700000)
    for a in range(0x4A83C, 0x4A848):
        if a not in (0x4A83D, 0x4A83E, 0x4A840): rig.w8(a, 1)
    rig.w16(P, 0x11C8); rig.w8(P + 0x62, 0x11); rig.w32(P + 0x18, 0x2000000)
    H = P + 0x200 * 8
    rig.w16(H, 0x11C0); rig.w16(H + 2, 0x0100); rig.w8(H + 0x62, cls); rig.w8(H + 0x5E, 8); rig.w8(H + 0x38, 0xFF); rig.w8(H + 0x5D, 0xFF); rig.w16(H + 0x2C, 0xFFFF)
    rig.w32(H + 0x72, 0x61A800); rig.w16(H + 0x4C, 30000); rig.w8(H + 0x7D, 3); rig.w8(H + 0x7C, gear)
    rig.w16(H + 6, 100); rig.w16(H + 8, 100); rig.w32(H + 0x14, 100 << 22); rig.w32(H + 0x1C, 100 << 22); rig.w32(H + 0x18, floor + 6000); rig.w32(H + 0x10, (floor + 6000) >> 8)
    rig.w16(H + 0x66, 3000); rig.w16(H + 0x6C, S); rig.w16(H + 0x6E, S); rig.w8(H + 0x2B, 60)
    for o in (0x92, 0x9A, 0xA2, 0x80, 0x88, 0x90): rig.w16(H + o, 0x4000)
    rows = []
    for k in range(12):
        rig.run(0x26AF8, a6=0x710000)
        rows.append((rig.s32(H + 0x18) if hasattr(rig, 's32') else rig.u32(H + 0x18), hex(rig.u16(H + 2) & 0x80), hex(rig.u16(H) & 0x1000), rig.u16(H + 0x6E)))
    print("class %#x gear %#x S %#x floor %#x:" % (cls, gear, S, floor), rows[:3], "...", rows[-2:])

# the cruise missile (class 0x15): the height is forced to 0xB000 by the position update from the second step on, whatever it starts at
for y0 in (2000, 90000, 3000000):
    rig.reset()
    for a in (0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x1F1D6, 0x1A0DC, 0x1A042): rig.poke(a, b'\x4e\x75')
    rig.w8(0x4A95E, 7); rig.w16(0x4A984, 0x0840); rig.w8(0x4A83D, 0); rig.w8(0x4A844, 1); rig.w32(0x1D024, 0x700000)
    for a in range(0x4A83C, 0x4A848):
        if a not in (0x4A83D, 0x4A83E, 0x4A840): rig.w8(a, 1)
    rig.w16(P, 0x11C8); rig.w8(P + 0x62, 0x11); rig.w32(P + 0x18, 0x2000000)
    H = P + 0x200 * 4
    rig.w16(H, 0x11C0); rig.w16(H + 2, 0x0100); rig.w8(H + 0x62, 0x15); rig.w8(H + 0x5E, 4); rig.w8(H + 0x38, 0xFF); rig.w8(H + 0x5D, 0xFF); rig.w16(H + 0x2C, 0xFFFF)
    rig.w32(H + 0x72, 0x61A800); rig.w16(H + 0x4C, 30000); rig.w8(H + 0x7D, 3); rig.w8(H + 0x7C, 0xE0)
    rig.w16(H + 6, 100); rig.w16(H + 8, 100); rig.w32(H + 0x14, 100 << 22); rig.w32(H + 0x1C, 100 << 22); rig.w32(H + 0x18, y0); rig.w32(H + 0x10, y0 >> 8)
    rig.w16(H + 0x6C, 0x1800); rig.w16(H + 0x6E, 0x1800)
    for o in (0x92, 0x9A, 0xA2, 0x80, 0x88, 0x90): rig.w16(H + o, 0x4000)
    ys = []
    for k in range(4):
        rig.run(0x26AF8, a6=0x710000); ys.append(rig.u32(H + 0x18))
    print("cruise missile from height %d: height after each step %s" % (y0, ys))
