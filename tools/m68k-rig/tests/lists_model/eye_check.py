"""Camera position and the cockpit eye offset (docs/re/03-3d-pipeline.md, "Camera position and the eye offset").

    python3 eye_check.py [SEED] [N]

The camera set-up 0x1F45E (cockpit and chase views: V:4A83D zero) is run in the rig on N random records of the viewed entry (class byte +0x62, position longs +0x14 / +0x18 / +0x1C, the nine
Q14 words +0x92 .. +0xA2) and compared with the stated rule: the body-frame eye offset of the class, rotated by the nine words and shifted right by 6, added to the
position (x and z masked to 22 bits for the camera longs), negated, stored as V:4AB1A / 4AB1E / 4AB22; V:4AB34 receives the unmasked sum; V:4AB2A / 4AB2C / 4AB2E are those longs
shifted right by 8 as words and V:4AB30 the long of the height.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH

REC = 0x4B23C + 0x200 * 3


def w32(x):
    x &= 0xFFFFFFFF
    return x - (1 << 32) if x & 0x80000000 else x


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 300
    r = Rig(); r.reset(); rng = random.Random(seed); bad = 0
    EYE = {0x30: (0, 1, -5), 0x11: (0, 5, 20)}
    for _ in range(n):
        cls = rng.choice((0x30, 0x11, 0x10, 0x14, 0x20, 0x12))
        eye = EYE.get(cls, (0, 4, 18))
        m = [rng.randint(-16384, 16384) for _ in range(9)]
        px, py, pz = rng.randint(-(1 << 28), 1 << 28), rng.randint(-(1 << 24), 1 << 24), rng.randint(-(1 << 28), 1 << 28)
        r.w8(0x4A83D, 0); r.w16(0x4A996, 0x200 * 3); r.w8(REC + 0x62, cls)
        r.w32(REC + 0x14, px); r.w32(REC + 0x18, py); r.w32(REC + 0x1C, pz)
        r.poke(REC + 0x92, struct.pack('>9h', *m))
        r.run(0x1F45E)
        o = [(eye[0] * m[3 * k] + eye[1] * m[3 * k + 1] + eye[2] * m[3 * k + 2]) >> 6 for k in range(3)]
        full = (w32(px + o[0]), w32(py + o[1]), w32(pz + o[2]))
        cam = (-w32((px & 0x3FFFFF) + o[0]), -w32(py + o[1]), -w32((pz & 0x3FFFFF) + o[2]))
        want = {0x4AB34: full, 0x4AB1A: cam}
        got = {a: tuple(r.s32(a + 4 * k) for k in range(3)) for a in want}
        words = tuple(r.s16(0x4AB2A + 2 * k) for k in range(3)); lng = r.s32(0x4AB30)
        wwords = tuple(w16 for w16 in ((c >> 8) & 0xFFFF) if False) if False else tuple((((c >> 8) & 0xFFFF) ^ 0x8000) - 0x8000 for c in cam)
        ok = got == want and words == wwords and lng == (cam[1] >> 8)
        if not ok:
            bad += 1
            if bad < 5: print('DIFF', hex(cls), got, want, words, wwords, lng, cam[1] >> 8)
    print('eye_check: %d records, %s' % (n, 'FAILED %d' % bad if bad else 'ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
