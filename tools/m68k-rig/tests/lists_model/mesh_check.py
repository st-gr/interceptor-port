"""The sea mesh pass 0x2F19C against the rule of docs/re/04-scene-composition.md ("Stage [68]") and note 03's vertex layout ("Op 9 reads past its quad", the sea mesh paragraph).

    python3 mesh_check.py [SEED] [N]      (about 2 minutes for N = 300)

Each state: random camera cell and position (inside the world, a cell edge, outside it), a random rotation matrix, altitude, zoom, flags. The original pass runs in the rig with the clipper
0x28B6C replaced by the logging stub of tools/vamiga-ref/sky-ground/meshrig.py, with the polygon buffer V:5103C.. pre-filled with sentinels. Compares:
  1. the polygons handed to the clipper (count and the vertex words x, y, z in the buffer's order) with meshmodel.mesh();
  2. the buffer afterwards with a buffer model: the shift word cleared to 0 at entry, then for each polygon the count word and exactly n vertices of three words (positions 0 to n-1),
     nothing else written: the positions from n on keep the sentinels (or the earlier polygon's words), so the residue is that of the frame's last polygon.
Reads the git-ignored extracted/interceptor.bin; skipped with exit 0 when it or librig.so is missing."""
import math, os, random, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from common import Rig, IMGPATH, LIBPATH, ROOT
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped: needs extracted/interceptor.bin and tools/m68k-rig/librig.so"); raise SystemExit(0)
sys.path.insert(0, os.path.join(ROOT, "tools", "vamiga-ref", "sky-ground"))
import meshmodel as mm, meshrig as mr

BUF = 0x5103C
SENT = [0x5555, 0x4444] + [0x7000 + i for i in range(3 * 20)]      # shift word, count word, 20 vertex positions


def rand_params(rnd):
    cell = rnd.choice([1 << 24, 1 << 28])
    def coord():
        k = rnd.random()
        if k < .6: return rnd.randint(0, (1 << 29) - 1)
        if k < .8: return (rnd.randint(0, 31) << 24) + rnd.choice([0, 1, (1 << 24) - 1, 0x800000, 0x400000])
        return rnd.randint(-(1 << 30), 1 << 30)
    a, b, c = (rnd.randint(0, 28799) * 2 * math.pi / 28800 for _ in range(3))
    ca, sa, cb, sb = math.cos(a), math.sin(a), math.cos(b), math.sin(b)
    M = [int(round(256 * v)) for v in (ca, 0, sa, sa * sb, cb, -ca * sb, -sa * cb, sb, ca * cb)]
    alt = rnd.choice([rnd.randint(0, 0x600), rnd.randint(0, 0x5000), rnd.randint(0, 0x20000), rnd.randint(0, 1 << 20)])
    zoom = rnd.choice([0x80, 0x80, 0x40, 0x20, 0x10, 1])
    return mm.Params(x=coord(), y=rnd.randint(0, 1 << 26), z=coord(), M=M, a_long=alt, zoom3=zoom, f895=0x80 if zoom == 0x80 else 0,
                     f865=rnd.choice([0, 0, 0, 1]), f954=rnd.choice([0, 1]), phase=rnd.randint(0, 15), octant=rnd.randint(0, 7), f868=0,
                     external=rnd.choice([0, 1]))


def buffer_model(polys):
    w = list(SENT); w[0] = 0
    for n, verts in polys:
        w[1] = n & 0xFFFF
        for i, v in enumerate(verts):
            for k in range(3): w[2 + 3 * i + k] = v[k] & 0xFFFF
    return w


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    rnd = random.Random(seed)
    D = mm.Data()
    r = Rig()
    bad = tot_poly = tot_vert = nopoly = maxn = 0
    for it in range(n):
        P = rand_params(rnd)
        r.reset()
        mr.set_inputs(r, P)
        r.poke(BUF, struct.pack(">%dH" % len(SENT), *SENT))
        got = mr.run_logged(r)
        exp = mm.mesh(P, D)
        g = [(c, v) for c, v, s in got]
        e = [(c, [tuple(x) for x in v]) for c, v in exp]
        tot_poly += len(g); tot_vert += sum(len(v) for _, v in g); maxn = max([maxn] + [c for c, _ in g])
        if not g: nopoly += 1
        after = list(struct.unpack(">%dH" % len(SENT), r.peek(BUF, 2 * len(SENT))))
        ok = g == e and all(s == 0 for _, _, s in got) and after == buffer_model(e)
        if not ok:
            bad += 1
            if bad <= 5: print("MISMATCH", P, "polys rig", len(g), "model", len(e), "buffer ok", after == buffer_model(e))
    print("mesh_check seed %d: %d states, %d polygons, %d vertices, largest polygon %d, %d states without a polygon, %d mismatches" % (seed, n, tot_poly, tot_vert, maxn, nopoly, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
