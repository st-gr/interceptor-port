"""The cue player 0x14B5A (docs/re/07-ai-and-weapons.md, section 13.4): which tone parameters each bit of V:4AC0C plays, in which order, and what the player clears.

    python3 cue_check.py

The player is entered at 0x14B5A with a prepared frame (the saved frame pointer and the return address are the rig's sentinel) and the tone routine 0x1A042 replaced by a logger of its
nine stack arguments.  Compared with the table of the note: for each single bit the nine parameters; for random combinations of bits the first bit in the player's order
(0x40, 0x80, 0x01, 0x04, 0x100, 0x10, 0x02, 0x20, 0x800, then 0x08 = silence) is the one played and the whole long V:4AC0C is cleared afterwards; the cool-down V:4A942
above 0 plays nothing and keeps the bits; a non-zero ejection nibble (record byte +0x7C, low nibble) plays only the bit 0x40; the cue 0x01 sets the cool-down to 3.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
import primlog as P

STUB = 0x8A0000
BUF = 0x8F2000
CNT = 0x8F1FFC
FRAME = 0x7F0200

# (bit of the long V:4AC0C, p1 .. p9) in the order of the player's own checks
CUES = [
    (0x40, (0xA0, 0x10, 8, 0xC8, 0x10, 5, 3, 4, 4)),
    (0x80, (0x144, 0x0A, 8, 0xC8, 0x0A, 5, 3, 1, 4)),
    (0x01, (0x7C, 0x10, 8, 0xB4, 0x10, 5, 3, 3, 4)),
    (0x04, (0xE5, 3, 5, 0xE5, 3, 5, 1, 0, 1)),
    (0x100, (0xC0, 4, 9, 0xC8, 0, 3, 1, 0, 1)),
    (0x10, (0x1F4, 0x0F, 5, 0x1F4, 0x0F, 5, 3, 1, 5)),
    (0x02, (0x1F4, 0x0A, 5, 0x82, 0, 1, 3, 3, 5)),
    (0x20, (0x190, 8, 5, 0x14A, 0, 1, 3, 2, 5)),
    (0x800, (0x1F4, 0x0A, 5, 0x82, 0, 1, 3, 1, 5)),
    (0x08, None),
]


def main():
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print("skipped (missing: extracted/interceptor.bin or librig.so)"); return 0
    r = Rig(); rng = random.Random(1); bad = 0; runs = 0
    code = (P.w(0x2F08, 0x2F09, 0x41EF, 0x000C) + P.w(0x43F9) + P.l(BUF) + P.w(*([0x22D8] * 9)) + P.w(0x52B9) + P.l(CNT) + P.w(0x225F, 0x205F, 0x4E75))
    stop = P.w(0x2F08, 0x2F09, 0x4E75)           # unused placeholder to keep the stub area tidy
    def run(bits, cool, nib):
        r.reset()
        r.poke(STUB, code); r.poke(0x1A042, P.w(0x4EF9) + P.l(STUB))
        r.poke(0x19C58, P.w(0x4E75))                      # the voice stop of the silence cue
        r.w32(CNT, 0); r.poke(BUF, bytes(36))
        r.w32(0x1A6C0, 0x4B23C)                            # the viewed record
        r.w8(0x4B23C + 0x7C, nib)
        r.w32(0x4AC0C, bits); r.w8(0x4A942, cool)
        r.w32(FRAME, 0); r.w32(FRAME + 4, 0x100)
        r.run(0x14B5A, a6=FRAME)
        n = r.u32(CNT)
        args = tuple(r.u32(BUF + 4 * i) for i in range(9)) if n else None
        return n, args
    order = [c[0] for c in CUES]
    for bit, par in CUES:
        n, args = run(bit, 0, 0); runs += 1
        want = (1, par) if par else (0, None)
        ok = (n, args) == want and r.u32(0x4AC0C) == 0
        if bit == 0x01: ok &= r.u8(0x4A942) == 3
        if not ok:
            bad += 1; print("DIFF single bit %X: got %s %s want %s" % (bit, n, args, want))
    for it in range(300):
        bits = 0
        for b in order:
            if rng.random() < 0.3: bits |= b
        cool = rng.choice((0, 0, 1)); nib = rng.choice((0, 0, 0, 3))
        n, args = run(bits, cool, nib); runs += 1
        first = next((c for c in CUES if c[0] & bits), None)
        if bits == 0 or cool > 0:
            want = (0, None); keep = bits if bits else 0
            ok = (n, args) == want and r.u32(0x4AC0C) == keep
        elif nib != 0:
            if bits & 0x40: want = (1, CUES[0][1]); ok = (n, args) == want and r.u32(0x4AC0C) == 0
            else: ok = n == 0 and r.u32(0x4AC0C) == 0
        else:
            want = (1, first[1]) if first[1] else (0, None)
            ok = (n, args) == want and r.u32(0x4AC0C) == 0
        if not ok:
            bad += 1
            if bad < 6: print("DIFF bits %X cool %d nib %d: got %s %s" % (bits, cool, nib, n, args))
    print("cue_check: %d runs, %s" % (runs, "FAILED %d" % bad if bad else "0 differences"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
