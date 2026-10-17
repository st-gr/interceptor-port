"""The carrier-qualification success test (docs/re/08-game-flow.md, section 9.3).

    python3 qual_check.py [SEED] [N]

The mission script of game mode 9, 0x28A18, is run in the rig on N random player states and compared with the stated rule: while the mission code V:4A850 is 0 the
qualification succeeds exactly when the player record is active (W0 bit 0x0040), W2 AND 0xC080 equals 0xC080 (hook down, hook engaged, wheels down), the indicated
speed word +0x6E is 0 and the leave request V:4A8E2 is not 3; success stores V:4A850 = 0xFF, V:4A978 = 0, V:4A8E2 = 3, V:4A8E4 = 4 and V:4A965 = 1, anything else
changes none of them.  A code other than 0 makes the routine return at once.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
"""
import os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH

REC = 0x4B23C


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print("skipped (missing: extracted/interceptor.bin or librig.so)"); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 3000
    r = Rig(); r.reset(); rng = random.Random(seed); bad = 0; ok_count = 0
    for _ in range(n):
        w0 = rng.choice((0x11C8, 0x1188, 0x11C8 & ~0x40, rng.randint(0, 0xFFFF)))
        w2 = rng.choice((0xC082, 0xC0 << 8 | 0x80, 0xE082, 0x8082, 0x4082, 0xC002, rng.randint(0, 0xFFFF)))
        speed = rng.choice((0, 0, 0, 1, -1, 12, rng.randint(-32768, 32767)))
        e2 = rng.choice((0, 0, 1, 2, 3, 0xFF))
        code = rng.choice((0, 0, 0, 0, 0xFF, 0xFE, 1))
        r.w16(REC, w0); r.w16(REC + 2, w2); r.w16(REC + 0x6E, speed)
        r.w8(0x4A850, code); r.w8(0x4A8E2, e2); r.w16(0x4A978, 0x1234); r.w8(0x4A8E4, 0x55); r.w8(0x4A965, 0)
        r.run(0x28A18)
        success = code == 0 and (w0 & 0x40) and (w2 & 0xC080) == 0xC080 and (speed & 0xFFFF) == 0 and e2 != 3
        got = (r.u8(0x4A850), r.u16(0x4A978), r.u8(0x4A8E2), r.u8(0x4A8E4), r.u8(0x4A965))
        want = (0xFF, 0, 3, 4, 1) if success else (code, 0x1234, e2, 0x55, 0)
        ok_count += 1 if success else 0
        if got != want:
            bad += 1
            if bad < 6: print("DIFF w0=%04x w2=%04x speed=%d e2=%d code=%d got %r want %r" % (w0, w2, speed, e2, code, got, want))
    print("qual_check: %d states (%d successes), %s" % (n, ok_count, "FAILED %d" % bad if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
