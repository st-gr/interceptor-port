"""The heading of the free-flight start for the location keys (docs/re/05-flight-model.md 17.14, "The heading of the out-of-table starts"): the player start 0x1D4E2 for V:4A900 = 0 to 9.

    python3 keyheading_check.py

Runs the original's player start (A0 = the start table 0x47ADE as the caller passes it is set by the routine itself) with the aircraft V:4A901 = 0x11 for every selector value and prints the stored heading
word of record 0 (+0x68) and the camera-free matrix words; asserts the rule: heading = (degrees word of the entry x 10, shifted left by 3 as a 16-bit word) for the runway entries, and for the selector 9 (key 0)
the word 0xE800 (59,392), which the matrix builder turns into a non-rotation (the over-read of 17.9). One Rig; skips with exit 0 without the git-ignored image or librig.so.
"""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from common import Rig, IMGPATH, LIBPATH
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped: needs extracted/interceptor.bin and tools/m68k-rig/librig.so"); raise SystemExit(0)
img = open(IMGPATH, "rb").read()
def w(a): return struct.unpack(">H", img[a - 0x10000:a - 0x10000 + 2])[0]

r = Rig()
bad = 0
for sel in range(0, 10):
    r.reset()
    r.w8(0x4A900, sel); r.w8(0x4A901, 0x11)
    # the carrier record must be active for selector 3 (the start would fall back to block 0); the other selectors do not look at it
    r.w8(0x4B23C + 14 * 512 + 1, 0x40)
    r.run(0x1D4E2)
    head = r.u16(0x4B23C + 0x68)
    deg = w(0x47ADE + 16 * sel + 14)
    exp = (deg * 80) & 0xFFFF
    m = [r.s16(0x4B23C + 0x80 + 2 * i) for i in range(9)]
    kind = "runway" if w(0x47ADE + 16 * sel) < 0x8000 else "carrier"
    print("selector %d: %s entry, degrees word %d, heading word %d (rule %d), matrix A %s" % (sel, kind, deg, head, exp, m))
    if kind == "runway" and head != exp: bad += 1
print("keyheading_check: %d differences" % bad)
sys.exit(1 if bad else 0)
