"""Sign of the camera roll (docs/re/03-3d-pipeline.md, "Camera matrix", roll sign).

    python3 roll_check.py [SEED] [N]

The cockpit-view camera set-up 0x321F0 (called by 0x32074 when the external-view flag V:4A83D is zero) is run in the rig on N random attitude records
(words +0x66, +0x68, +0x6A random in 0..28799).  Checked: (1) the words V:4AB42, V:4AB46, V:4AB4A (the angles A, B, C handed to fx_cam_ypr, 0x32AB6) are the
record words unchanged; (2) the nine words at V:4AC90 equal fx_cam_ypr(A, B, C) called directly with those words (zoom words 0x100) and differ, for roll words that
are not 0 and not 14400, from fx_cam_ypr with the roll negated; (3) with pitch and heading zero, the camera-space image of the world's up vector is
(-sin C, cos C, 0) (Q8): a positive roll word turns the up vector toward negative camera x.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
"""
import math, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH

REC = 0x4B23C


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 200
    r = Rig(); r.reset(); rng = random.Random(seed)
    s16 = lambda v: v - 0x10000 if v & 0x8000 else v
    bad = flipped_differs = flipped_cases = 0
    for _ in range(n):
        a, b, c = [rng.randint(0, 28799) for _ in range(3)]
        if rng.random() < 0.2: a = b = 0
        r.w16(0x4A996, 0); r.w8(0x4A83D, 0); r.w8(0x4A85F, 0); r.w8(0x4A869, 0)
        for i in range(3): r.w16(0x4AAF6 + 2 * i, 0x100)
        r.w8(REC + 0x62, 0x10)
        r.w16(REC + 0x66, a); r.w16(REC + 0x68, b); r.w16(REC + 0x6A, c)
        r.run(0x321F0)
        words = [s16(r.u16(0x4AC90 + 2 * k)) for k in range(9)]
        got_angles = (r.u16(0x4AB42), r.u16(0x4AB46), r.u16(0x4AB4A))
        def direct(a_, b_, c_):
            r.run(0x32AB6, d0=a_, d2=b_, d4=c_, a1=0x700000)
            return [s16(r.u16(0x700000 + 2 * k)) for k in range(9)]
        ref = direct(a, b, c)
        if got_angles != (a, b, c) or words != ref:
            bad += 1
            if bad < 6: print('DIFF', (a, b, c), got_angles, words, ref)
        if c not in (0, 14400):
            flipped_cases += 1
            if direct(a, b, (28800 - c) % 28800) != words: flipped_differs += 1
    # geometry with pitch = heading = 0
    geo_bad = 0
    for c in (0, 1200, 3600, 7200, 21600, 27600):
        r.w16(REC + 0x66, 0); r.w16(REC + 0x68, 0); r.w16(REC + 0x6A, c)
        r.run(0x321F0)
        m = [s16(r.u16(0x4AC90 + 2 * k)) for k in range(9)]
        ang = 2 * math.pi * c / 28800
        want = (-math.sin(ang) * 256, math.cos(ang) * 256, 0)
        up = (m[1], m[4], m[7])
        if any(abs(u - w) > 2 for u, w in zip(up, want)): geo_bad += 1; print('DIFF up vector', c, up, want)
    print('roll_check: %d states; angles and matrix equal fx_cam_ypr(+0x66, +0x68, +0x6A): %s; matrix differs from the negated-roll matrix in %d of %d states; up-vector geometry: %s' % (
        n, 'yes' if not bad else 'NO (%d)' % bad, flipped_differs, flipped_cases, 'ok' if not geo_bad else 'FAILED'))
    return 1 if bad or geo_bad or flipped_differs != flipped_cases else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
