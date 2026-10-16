"""The line rule of the clipped line (op 2, routine 0x33936): a segment counts as drawn when it meets the open view pyramid, up to the roundings of the integer
intersection routines (docs/re/03-3d-pipeline.md, "Line clipping").   python3 line_check.py [SEED] [N]

N random segments (default 6000; end points behind, outside, inside and on the edge of the pyramid, from prim_ops.Unit.vertices) are given to the handler of
op 2 (0x236D4) with the line routine replaced by a logging stub.  The handler's return is compared with the exact rational test "the segment meets the open
pyramid".  A disagreement is explained only when (1) the rig draws a segment that meets the pyramid only on its boundary (touching it or lying in a face), or
(2) the rig does not draw a segment whose part inside the pyramid is shorter than 1 percent of its length; anything else is a difference (exit 1)."""
import collections, os, random, sys
from fractions import Fraction as F
from common import IMGPATH, LIBPATH
import prim_ops as PO


def meets(p, q, closed):
    """exact rational test: the segment p-q meets the view pyramid (z > |x|, z > |y|; closed: z >= |x|, z >= |y|), as a part of positive length or a touching point"""
    P0 = [F(c) for c in p]; Q = [F(c) for c in q]; d = [Q[i] - P0[i] for i in range(3)]
    t0, t1 = F(0), F(1); s0 = s1 = False
    for a, b, c in ((-1, 0, 1), (1, 0, 1), (0, -1, 1), (0, 1, 1)):
        f0 = a * P0[0] + b * P0[1] + c * P0[2]; fd = a * d[0] + b * d[1] + c * d[2]
        if fd == 0:
            if f0 < 0 or (f0 == 0 and not closed): return None
        else:
            t = -f0 / fd
            if fd > 0:
                if t > t0: t0, s0 = t, not closed
                elif t == t0: s0 = s0 or not closed
            else:
                if t < t1: t1, s1 = t, not closed
                elif t == t1: s1 = s1 or not closed
        if t0 > t1 or (t0 == t1 and (s0 or s1)): return None
    return t0, t1


def main(seed=1, n=6000):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    u = PO.Unit(); rng = random.Random(seed)
    c = collections.Counter(); bad = 0; sliver = F(0)
    for k in range(n):
        u.set_state(rng)
        vs = u.vertices(rng, mode=rng.choice(('mixed', 'mixed', 'edge', 'inside', 'behind')))
        i, j = rng.randint(0, 23), rng.randint(0, 23)
        d0, lt, pr, cl, a2 = u.call(2, [PO.V(i), PO.V(j), 0])
        got = pr[PO.LINE] > 0
        op = meets(vs[i], vs[j], False)
        if got == (op is not None): c['equal to the open-pyramid rule'] += 1; continue
        if op is None:                          # the rig drew a segment that does not meet the open pyramid
            if meets(vs[i], vs[j], True) is not None: c['rig draws, the segment only touches or lies in the pyramid boundary'] += 1; continue
        elif op[1] - op[0] < F(1, 100):         # the rig does not draw a sliver of the segment inside the pyramid
            c['rig does not draw, the part inside is shorter than 1 percent of the segment'] += 1; sliver = max(sliver, op[1] - op[0]); continue
        bad += 1
        if bad < 6: print('DIFF', vs[i], vs[j], 'rig drawn', got, 'open-pyramid part', op)
    c['segments'] = n
    print('line rule: %s, longest sliver not drawn %.4f, %d unexplained differences' % (dict(c), float(sliver), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(int(sys.argv[1]) if len(sys.argv) > 1 else 1, int(sys.argv[2]) if len(sys.argv) > 2 else 6000))
