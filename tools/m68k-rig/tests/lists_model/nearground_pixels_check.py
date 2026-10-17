"""Near-ground pass 0x2C0EC run for real with the blitter model on random pen maps against nearground_model.py + nearground_pixels.py (docs/re/04-scene-composition.md "Stage [90]").

    python3 nearground_pixels_check.py [SEED [N]]       (needs extracted/interceptor.bin and build/sky-ground/librigblit.so: bash tools/vamiga-ref/sky-ground/build.sh)

Each state: a random camera (the matrix of a random rotation, a random height 0 to 2,048 u, band and offsets), a random pen map in the four planes (half of the states with pens 0, 4, 5, 6, 7, 9, 13, 15 only,
half with all 16), the original pass; the planes after the run must equal the model's pens, pixel for pixel (sprites, the line and dot cases, the general fill, with the rig blitter's C modulus behaviour)."""
import math, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nearground_model as M, nearground_pixels as NP


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    N = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    try:
        import rigw
    except SystemExit:
        print("skipped (needs librigblit.so)"); return 0
    img = open(rigw.IMG, "rb").read()
    rng = random.Random(seed); bad = 0; kinds = {}; general = 0
    for it in range(N):
        band = rng.choice((0, 0, 1, 2, 3))
        hgt = rng.choice((rng.randint(0, 128), rng.randint(100, 260), rng.randint(200, 700), rng.randint(0, 2048)))
        a, b, c = [rng.uniform(0, 2 * math.pi) for _ in range(3)]
        if rng.random() < .5: a = rng.choice((0, math.pi / 2, math.pi)); b = rng.uniform(-.5, .5); c = rng.uniform(-.3, .3)
        ca, sa, cb, sb, cc, sc = math.cos(a), math.sin(a), math.cos(b), math.sin(b), math.cos(c), math.sin(c)
        R = [[ca * cc - sa * cb * sc, -ca * sc - sa * cb * cc, sa * sb], [sa * cc + ca * cb * sc, -sa * sc + ca * cb * cc, -ca * sb], [sb * sc, sb * cc, cb]]
        st = dict(cam_x=rng.randint(-16384, 16383), cam_z=rng.randint(-16384, 16383), height_long=-hgt, band=band,
                  matrix=[int(round(R[i][j] * 256)) for i in range(3) for j in range(3)], limit=rng.choice((143, 166, 178)))
        pool = (4, 5, 6, 7, 0, 9, 13, 15) if rng.random() < .5 else tuple(range(16))
        bg = [[rng.choice(pool) for x in range(320)] for y in range(200)]
        r = rigw.Rig()
        for i in range(4): r.w32(rigw.TAB + 4 * i, rigw.P0 + rigw.PS * (3 - i))
        r.w32(0x4A76E, rigw.TAB); r.w32(0x4A772, rigw.TAB); r.w32(0x4A79A, 0x500000)
        planes = [bytearray(8000) for _ in range(4)]
        for y in range(200):
            for x in range(320):
                for k in range(4):
                    if bg[y][x] >> k & 1: planes[k][y * 40 + x // 8] |= 0x80 >> (x % 8)
        for k in range(4): r.poke(rigw.P0 + rigw.PS * k, bytes(planes[k]))
        r.w16(0x4AB2A, st["cam_x"]); r.w16(0x4AB2E, st["cam_z"]); r.poke(0x4AB30, struct.pack(">i", st["height_long"])); r.w8(0x4A923, band)
        r.poke(0x4AC90, struct.pack(">9h", *st["matrix"])); r.w16(0x4AA3C, st["limit"]); r.w8(0x4A85A, 0); r.w16(0x4AA0C, 0x55)
        r.run(0x2C0EC)
        got = rigw.pens(r)
        events, _ = M.model(st, img)
        exp = NP.apply(events, bg, st["limit"], cquirk=True)       # the rig's blitter model reads C with the stale modulus (see nearground_pixels.py)
        for e in events:
            kinds[e[0]] = kinds.get(e[0], 0) + 1
            if e[0] == "tri" and NP.fmodel.tiny(list(e[1:]), st["limit"], 3, 4) is None: general += 1
        if got != exp:
            bad += 1
            if bad < 4: print("DIFF state", it, events[:4])
    print("near-ground pixels: %d random states, differences %d; events %s, general fills %d" % (N, bad, kinds, general))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
