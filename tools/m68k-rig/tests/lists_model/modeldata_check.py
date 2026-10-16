"""The model data is never written (docs/re/03-3d-pipeline.md, type B models: the zero "placeholder" corners are literal data).

    python3 modeldata_check.py [SEED] [N]       (skips with exit 0 without extracted/interceptor.bin or librig.so)

N random frames (builder 0x1F770 inside the harness, walkers A, B and C with the real drawers) and three constructed one-entry walks per frame of the type B and
plain items, each followed by a byte comparison of the image area 0x10000 up to the BSS start 0x4A6E8 with the file.  The only bytes allowed to differ
are the BSS hunk 2 (0x1A6B8-0x1D0BC), the filler stub at 0x34A28 and the run-time descriptor slots 0 to 16 of 0x2600C that the harness and the game write; the rest of the area holds the code, the tables and every model."""
import os, random, sys
from common import Rig, IMGPATH, LIBPATH
import walk_check as W
import prim_walk as PW


def main(seed=1, n=150):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    f = open(IMGPATH, 'rb').read()
    lo, hi = 0x10000, 0x4A6E8
    allowed = lambda a: 0x1A6B8 <= a < 0x1D0BC or 0x34A28 <= a < 0x34A2A or 0x2600C <= a < 0x2600C + 17 * 20      # hunk 2 is BSS (zeros in the image)
    r = Rig(); r.L.rig_set_max_instructions(3000000); rng = random.Random(seed)
    bad = 0; frames = 0; walks = 0
    def compare(what):
        nonlocal bad
        cur = r.peek(lo, hi - lo)
        diff = [lo + i for i in range(hi - lo) if cur[i] != f[i] and not allowed(lo + i)]
        if diff:
            bad += 1
            if bad < 5: print('WRITE into the image area after', what, [hex(a) for a in diff[:8]], len(diff))
    r.reset(); items = PW.items_of(r)
    for k in range(n):
        W.random_frame(r, rng, camr=rng.choice((300, 3000)), tag=rng.choice(('air', 'deck')), active=rng.random() < .5, extra=rng.randint(0, 2))
        for t in 'ABC': W.walk(r, t)
        frames += 1; compare('random frame %d' % k)
        for _ in range(3):
            c = PW.construct(r, rng, items, dist=rng.choice((None, 300, 914, 2500)))
            if c is None: continue
            W.walk(r, c[1]); walks += 1; compare('constructed walk %d (item %d)' % (k, c[2]))
    print('modeldata: %d random frames (3 walks each), %d constructed one-entry walks, %d with writes into 0x10000-0x4A6E8' % (frames, walks, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(int(sys.argv[1]) if len(sys.argv) > 1 else 1, int(sys.argv[2]) if len(sys.argv) > 2 else 150))
