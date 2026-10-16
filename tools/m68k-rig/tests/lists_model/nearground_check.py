"""Near-ground pass 0x2C0EC (stage [90]) against nearground_model.py on random camera states: the sprite and filler calls the original makes (the
callees are replaced by loggers) must equal the model's events, in order.  python3 nearground_check.py [SEED [N]]"""
import ctypes, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, image, inputs_missing
import nearground_model as M


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    N = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
    if not os.path.exists(os.path.join(os.path.dirname(__file__), "..", "..", "librig.so")) or inputs_missing()[:1] == ["extracted/interceptor.bin"]:
        print("skipped (missing inputs)"); return 0
    img = image()
    r = Rig(); log = []
    HOOK = ctypes.CFUNCTYPE(None)

    def mk(kind):
        def f():
            if kind == "tri":
                a = struct.unpack(">8h", r.peek(0x5043C, 16))
                log.append(("tri", a[0], (a[1], a[2]), (a[3], a[4]), (a[5], a[6])))
            else:
                log.append((kind, r.reg("d0") & 0xFFFF, r.reg("d1") & 0xFFFF))
        return HOOK(f)
    hs = [mk("s1"), mk("s2"), mk("tri")]
    rng = random.Random(seed); bad = 0; stats = {"s1": 0, "s2": 0, "tri": 0, "active": 0, "cand": 0}
    for it in range(N):
        r.reset()
        band = rng.choice((0, 0, 1, 2, 3))
        lim = {0: 2048, 1: 3456}.get(band, 4608)
        mode = rng.random()
        if mode < 0.3: hgt = rng.randint(-60, 200)
        elif mode < 0.6: hgt = rng.randint(100, 700)
        else: hgt = rng.randint(0, lim + 60)
        hl = -hgt - (0 if rng.random() < .8 else rng.randint(0, 255))
        if rng.random() < 0.3: hl = -hgt
        if rng.random() < .5:
            ang = [rng.randint(0, 28799) for _ in range(3)]
        mat = []
        for _ in range(9): mat.append(0)
        # random rotation scaled by 256 (the camera matrix is a rotation times 256 up to rounding), some states with free words
        import math
        a, b, c = [rng.uniform(0, 2 * math.pi) for _ in range(3)]
        if rng.random() < 0.3: a, b, c = rng.choice((0, math.pi / 2)), rng.uniform(-.3, .3) if rng.random() < .5 else 0, rng.choice((0, math.pi))
        ca, sa, cb, sb, cc, sc = math.cos(a), math.sin(a), math.cos(b), math.sin(b), math.cos(c), math.sin(c)
        R = [[ca * cc - sa * cb * sc, -ca * sc - sa * cb * cc, sa * sb],
             [sa * cc + ca * cb * sc, -sa * sc + ca * cb * cc, -ca * sb],
             [sb * sc, sb * cc, cb]]
        mat = [int(round(R[i][j] * 256)) for i in range(3) for j in range(3)]
        if rng.random() < 0.1: mat = [rng.randint(-300, 300) for _ in range(9)]
        st = dict(cam_x=rng.randint(-16384, 16383), cam_z=rng.randint(-16384, 16383), height_long=hl, band=band, matrix=mat,
                  limit=rng.choice((143, 166, 178, 179)))
        r.w16(0x4AB2A, st["cam_x"]); r.w16(0x4AB2E, st["cam_z"]); r.poke(0x4AB30, struct.pack(">i", hl)); r.poke(0x4A923, bytes([band]))
        r.poke(0x4AC90, struct.pack(">9h", *mat)); r.w16(0x4AA3C, st["limit"])
        r.w8(0x4A85A, 0); r.w16(0x4AA0C, 0x55)
        for a_, h in zip((0x340D4, 0x340EA, 0x34A28), hs): r.L.rig_hook_set(a_, h, 0)
        del log[:]
        r.run(0x2C0EC, a7=0xDFFF00) if False else r.run(0x2C0EC)
        r.L.rig_hooks_clear()
        got = [e if e[0] != "tri" else ("tri",) + e[2:] for e in log]
        exp, cand = M.model(st, img)
        exp = [(e[0], e[1] & 0xFFFF, e[2] & 0xFFFF) if e[0] != "tri" else e for e in exp]
        flag = r.u8(0x4A85A)
        if flag != 0: print("flag left set", it); bad += 1
        if got != exp:
            bad += 1
            if bad < 5: print("DIFF", it, st, "\n  rig", got[:6], "\n  model", exp[:6])
        else:
            if cand is not None: stats["active"] += 1; stats["cand"] += cand
            for e in got: stats[e[0]] += 1
    print("near-ground pass: %d random states, differences %d; active %d, candidates %d, sprites 1x1 %d, 2x1 %d, triangles %d" % (
        N, bad, stats["active"], stats["cand"], stats["s1"], stats["s2"], stats["tri"]))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
