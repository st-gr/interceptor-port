"""The endless search of six type B models is unreachable (docs/re/03-3d-pipeline.md "Type B models", docs/re/04a-display-lists.md rule 4B).

    python3 typeb_loop_check.py [SEED] [N]        (skips with exit 0 without extracted/interceptor.bin, the lists log or librig.so)

Static part (image only): the chains of detail entries of every type B descriptor: how many entries, how many have bit 15 of the threshold set, which
models' last entry names itself as the next entry (threshold 0x7FFF).
Dynamic part: N random frames (default 150; heights, cells, camera offsets and both logged states as in walk_check.py), the three queues are built by the
original builder and walked; for every terrain entry the distance word times 2^LOD (the true distance in units of 16 u) and, for each LOD, the largest
scaled distance word seen are compared with the threshold 0x7FFF shifted right by the LOD: the search of such a model loops forever only when the scaled
distance word is above that value.  The zoom factor V:4AAFA is at most 0x80 in play (keys 0x20 to 0x80), so the scaled word never exceeds the word.
Exit 1 when any scaled word exceeds the shifted threshold."""
import collections, os, random, sys
from common import Rig, inputs_missing
import walk_check as W
import prim_walk as PW


def main(seed=1, n=150):
    if inputs_missing():
        print('skipped (missing: %s)' % ', '.join(inputs_missing())); return 0
    r = Rig(); r.reset(); r.L.rig_set_max_instructions(3000000)
    entries = 0; bit15 = []; selfref = []
    for it in range(20, 128):
        d = 0x2600C + 20 * it
        if r.u32(d) in PW.TYPE_B and r.u32(d + 4) > 0x10000:
            a = r.u32(d + 4); seen = set()
            while a not in seen:
                seen.add(a); thr = r.u16(a); nxt = r.u32(a + 2); entries += 1
                if thr & 0x8000: bit15.append((it + 16, hex(thr))); break
                if nxt == a: selfref.append((it + 16, hex(thr))); break
                if nxt & 0x80000000: break
                a = nxt
    print('static: %d detail entries of the type B models; threshold with bit 15 set: %s; last entry naming itself: %s' % (entries, bit15, selfref))
    rng = random.Random(seed); worst = collections.defaultdict(int); bad = 0; walked = 0
    for k in range(n):
        W.random_frame(r, rng, camr=rng.choice((300, 3000)), tag=rng.choice(('air', 'deck')), active=rng.random() < .5, extra=rng.randint(0, 2))
        for tier in 'ABC':
            W.walk(r, tier)
            for e in W.window(r, tier):
                if r.u8(e + 1) & 0x50: continue
                lod = r.u8(e + 1) & 15; word = r.u16(e + 0x10); walked += 1
                worst[lod] = max(worst[lod], word)
                zoom = 0x80
                if (word * 2 * zoom // 256) > (0x7FFF >> lod): bad += 1
    print('dynamic: %d terrain entries in %d frames; largest distance word by LOD (word, threshold 0x7FFF >> LOD, true distance word << LOD):' % (walked, n))
    for lod in sorted(worst):
        print('   LOD %2d  word %5d  limit %5d  true %6d  (%.1f percent of 0x7FFF)' % (lod, worst[lod], 0x7FFF >> lod, worst[lod] << lod, 100.0 * (worst[lod] << lod) / 0x7FFF))
    print('typeb loop: %d entries beyond the limit' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(int(sys.argv[1]) if len(sys.argv) > 1 else 1, int(sys.argv[2]) if len(sys.argv) > 2 else 150))
