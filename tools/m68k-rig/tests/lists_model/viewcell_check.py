"""The viewed cell words V:4AA04/06 and the phases V:4A908/09 written by the frame hook 0x1F54E (docs/re/04a-display-lists.md, "The viewed cell").

    python3 viewcell_check.py [SEED] [N]

External view (V:4A83D non-zero): the hook takes the camera longs V:4ACF6 (x) and V:4ACFE (z), masks them with 0x1FFFFFFF and stores bits 22 to 28 (position >> 22) in V:4AA04 and V:4AA06; the
phase V:4A908 is qx + 4 qz with q = 3 - ((cell word) & 3) (bits 22 and 23), and V:4A909 the same from bits 20 and 21. Cockpit and chase view (V:4A83D zero): the words come from the viewed record
(+6, +8) and V:4A909 from +10, not from the camera. The camera mover 0x2D756 is replaced by a return, so the longs stay as poked. Random longs (negative and above the world included).
"""
import os, random, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from common import Rig, IMGPATH, LIBPATH
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped: needs extracted/interceptor.bin and tools/m68k-rig/librig.so"); raise SystemExit(0)


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    rnd = random.Random(seed)
    r = Rig()
    bad = ext = cock = differs = 0
    for it in range(n):
        r.reset()
        r.poke(0x2D756, b"\x4e\x75")
        x = rnd.choice([rnd.randint(-(1 << 31), (1 << 31) - 1), rnd.randint(0, (1 << 29) - 1), rnd.randint(0, 1 << 24) << 4])
        z = rnd.choice([rnd.randint(-(1 << 31), (1 << 31) - 1), rnd.randint(0, (1 << 29) - 1), rnd.randint(0, 1 << 24) << 4])
        ext_view = rnd.random() < .6
        r.w8(0x4A83D, 0xFF if ext_view else 0)
        r.w32(0x4ACF6, x); r.w32(0x4ACFE, z)
        rec = 0x4B23C
        r.w16(0x4A996, 0)
        cw = (rnd.randint(0, 127), rnd.randint(0, 127)); r.w16(rec + 6, cw[0]); r.w16(rec + 8, cw[1]); r.w8(rec + 10, rnd.randint(0, 15))
        r.w8(0x4A966, rnd.choice([0, 2, 5]))
        r.run(0x1F54E)
        got = (r.u16(0x4AA04), r.u16(0x4AA06), r.u8(0x4A908), r.u8(0x4A909))
        if ext_view:
            ext += 1
            mx, mz = x & 0x1FFFFFFF, z & 0x1FFFFFFF
            cx, cz = mx >> 22, mz >> 22
            q = lambda v: 3 - (v & 3)
            exp = (cx, cz, q(cx) + 4 * q(cz), q(mx >> 20) + 4 * q(mz >> 20))
            # what the port does today: the cell of the aircraft (the record), here the poked record words
            if (cw[0], cw[1]) != (cx, cz): differs += 1
        else:
            cock += 1
            q = lambda v: 3 - (v & 3)
            exp = (cw[0], cw[1], q(cw[0]) + 4 * q(cw[1]), r.u8(rec + 10))
            exp = (exp[0], exp[1], exp[2], exp[3])
        if got != exp:
            bad += 1
            if bad <= 5: print("MISMATCH", ext_view, hex(x & 0xFFFFFFFF), hex(z & 0xFFFFFFFF), "got", got, "exp", exp)
    print("viewcell_check seed %d: %d states (%d external, %d cockpit), %d mismatches; in %d external states the record's cell differs from the camera's" % (seed, n, ext, cock, bad, differs))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
