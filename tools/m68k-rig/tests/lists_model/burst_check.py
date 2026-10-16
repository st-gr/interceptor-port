"""The explosion burst of a dying entity (0x31766 and 0x31850; docs/re/07-ai-and-weapons.md, section 11.4), original against a model written from the note.

    python3 burst_check.py [SEED] [N]

The routine draws, for an entity whose dying bit is set, twelve small triangles (kinds 1, 2: land/air and water) or nine discs plus two layers of triangles (kind 3: the cruise
missile) that fly outwards with the age 0 to 15 (15 minus the counter +0x4C).  The polygon filler 0x34A28 and the disc routine 0x33CAC are replaced by loggers (pen V:4AA0C,
the three screen points of the filler, or the centre and radius of the disc).  Random camera-relative positions, ages, kinds and LOD shifts are run; the model takes the
distance estimate 0x20884 from the original (only its own call, after the run) and everything else (the offsets by age, the shape cycle, the camera rotation of V:4AC90, the
pyramid test, the mapping to the 320 x 180 window, the pens) from the note.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so, extracted/captures/logs/lists.txt.  Skips with exit 0 when one is missing.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, load_scene, inputs_missing, image
import primlog as P

STUB = 0x8A0000


def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v
def asr16(v, c): c &= 63; return s16(v) >> min(c, 15) if c < 16 else (-1 if s16(v) < 0 else 0)


class Tables:
    def __init__(self):
        d = image()
        self.d = d

    def b(self, a, n): return self.d[a - 0x10000:a - 0x10000 + n]

    def sb(self, a): v = self.d[a - 0x10000]; return v - 256 if v >= 128 else v

    def w(self, a): return struct.unpack(">H", self.b(a, 2))[0]

    def l(self, a): return struct.unpack(">I", self.b(a, 4))[0]


def model(T, r, cen, age, kind, sft):
    """returns the list of ('tri', pen, [(sx, sy)] * 3) and ('disc', pen, sx, sy, radius) in drawing order; cen: three longs D0..D2; sft: the LOD shift (low 6 bits)"""
    out = []
    D0, D1, D2 = cen
    h = s32(r.u32(0x4AB30))
    D5 = abs(s32(D1 + h))
    if D5 > 0x8000: return out
    cam_x = s16(r.u16(0x4AB2A)); cam_z = s16(r.u16(0x4AB2E))
    rad = 0
    if kind == 3:
        # the distance estimate of the centre (0x31A88): coordinates plus camera offsets, absolute, shifted by 0 / 4 / 8 by size, then 0x20884
        a0 = abs(s32(D0 + cam_x)); a1 = abs(s32(D1 + h)); a2 = abs(s32(D2 + cam_z))
        sh = 0
        if max(a0, a1, a2) > 0x4000:
            sh = 8
            if max(a0, a1, a2) <= 0x40000: sh = 4
        a0 >>= sh; a1 >>= sh; a2 >>= sh
        r.run(0x20884, d2=a0 & 0xFFFF, d3=a1 & 0xFFFF, d4=a2 & 0xFFFF)
        dist = s16(r.reg("d1") & 0xFFFF)
        size = (0x2800 * age) // 6 + 0x2800
        # word arithmetic: addi.w then ext.l; an unsigned long divide by the word distance, overflow leaves the value
        size_w = s16(size)
        if dist > 0:
            q = (size_w & 0xFFFFFFFF) // (dist & 0xFFFF)
            rad = q if q <= 0xFFFF else (size_w & 0xFFFF)
            rad = s16(rad)
            if rad > 0x7F: rad = 0x7F
        else:
            rad = 0x7F
    x = s32(D0 + cam_x) >> sft; y = s32(D1 + h) >> sft; z = s32(D2 + cam_z) >> sft
    mat = [[s16(r.u16(0x4AC90 + 2 * (3 * i + j))) for j in range(3)] for i in range(3)]

    def layer(k, age_l, cx, cy, cz):
        if k == 1: pens, pos, n = 0x316e5, 0x31652, 12
        elif k == 2: pens, pos, n = 0x316f1, 0x31652, 12
        elif k == 3: pens, pos, n = 0x316fd, 0x316c7, 9
        elif k == 4: pens, pos, n = 0x31709, 0x316a0, 12
        else: pens, pos, n = 0x31715, 0x31679, 12
        for j in range(n - 1, -1, -1):
            dx, dy, dz = [T.sb(pos + 3 * j + i) for i in range(3)]
            dx, dy, dz = [s16((v * age_l) & 0xFFFF) for v in (dx, dy, dz)]
            sidx = T.w(0x31722 + 2 * (j + age_l))
            dx, dy, dz = asr16(dx, sft), asr16(dy, sft), asr16(dz, sft)
            ax = s16(cx + dx); ay = s16(cy + dy); az = s16(cz + dz)
            pen = T.b(pens, 12)[j]
            ptr = T.l(0x315A2 + sidx)
            pts = []; ok = True
            nv = 1 if k == 3 else 3
            for v in range(nv):
                if k == 3: o = (0, 0, 0)
                else: o = tuple(s16(T.w(ptr + 6 * v + 2 * i)) for i in range(3))
                p = [asr16(s16(ax + o[0]), sft), asr16(s16(ay + o[1]), sft), asr16(s16(az + o[2]), sft)]
                xr = s16(s32(mat[0][0] * p[0] + mat[0][1] * p[1] + mat[0][2] * p[2]) >> 8)
                yr = s16(s32(mat[1][0] * p[0] + mat[1][1] * p[1] + mat[1][2] * p[2]) >> 8)
                zl = s32(mat[2][0] * p[0] + mat[2][1] * p[1] + mat[2][2] * p[2]) >> 8
                zr = s16(zl)
                if zl <= 0 or xr > zr or s16(-xr) > zr or yr > zr or s16(-yr) > zr: ok = False; break
                sx = (xr * 0xA0) // zr if (xr * 0xA0) >= 0 else -((-xr * 0xA0) // zr)       # divs truncates toward zero
                sx += 0xA0; sx = 0 if sx < 0 else (0x13F if sx >= 0x140 else sx); sx = 0x13F - sx
                sy = (yr * 0x5A) // zr if (yr * 0x5A) >= 0 else -((-yr * 0x5A) // zr)
                sy += 0x5A; sy = 0 if sy < 0 else (0xB3 if sy >= 0xB4 else sy); sy = 0xB3 - sy
                pts.append((sx, sy))
            if not ok: continue
            if k == 3: out.append(("disc", pen, pts[0][0], pts[0][1], rad))
            else: out.append(("tri", pen, pts))

    x, y, z = s16(x), s16(y), s16(z)
    layer(kind, age, x, y, z)
    if kind >= 3:
        layer(4, max(age - 1, 0), x, y, z)
        if kind >= 4:
            pass
        layer(5, max(age - 2, 0), s16(x + 0x14), s16(y + 0x0A), s16(z + 0x14))
    return out


def main(argv):
    miss = inputs_missing()
    if miss: print("skipped (missing: %s)" % ", ".join(miss)); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 400
    T = Tables(); rng = random.Random(seed); r = Rig(); bad = 0; frags = 0; calls = 0
    filler = P.w(0x40E7, 0x2F08) + P.append_header(9, None, [(2, ("mem", 0x4AA0C)), (4, ("mem", 0x5043E)), (6, ("mem", 0x50440)), (8, ("mem", 0x50442)), (10, ("mem", 0x50444)),
                                                           (12, ("mem", 0x50446)), (14, ("mem", 0x50448))]) + P.w(0x205F, 0x44DF, 0x7001, 0x4E75)
    disc = P.w(0x40E7, 0x2F08) + P.append_header(10, None, [(2, ("mem", 0x4AA0C)), (4, ("reg", 0)), (6, ("reg", 1)), (8, ("reg", 6))]) + P.w(0x205F, 0x44DF, 0x7001, 0x4E75)
    for it in range(n):
        load_scene(r, "air")
        r.poke(STUB, filler); r.poke(STUB + 0x100, disc)
        r.poke(0x34A28, P.w(0x4EF9) + P.l(STUB)); r.poke(0x33CAC, P.w(0x4EF9) + P.l(STUB + 0x100))
        r.w32(P.LOGPTR, P.LOGBASE)
        kind = rng.choice((1, 1, 2, 3, 3)); age = rng.randint(0, 15); sft = rng.choice((0, 0, 1, 2, 3, 4, 5, 6))
        h = s32(r.u32(0x4AB30))
        cen = [rng.randint(-1500, 1500) << rng.choice((0, 2, 4)), (-h) + rng.randint(-3000, 3000) if rng.random() < 0.9 else rng.randint(0x9000, 0x20000), rng.randint(500, 6000) << rng.choice((0, 2, 3))]
        # the centre is camera relative; put it in front of the camera (the rotation of V:4AC90 decides) by trying both signs of the offsets
        if rng.random() < 0.5: cen[0] = -cen[0]
        if rng.random() < 0.75:                  # a camera-space point in front of the camera, turned back to world offsets by the transposed rotation
            mat = [[s16(r.u16(0x4AC90 + 2 * (3 * i + j))) for j in range(3)] for i in range(3)]
            cz = rng.randint(200, 4000); cx = rng.randint(-cz, cz) * 8 // 10; cy = rng.randint(-cz, cz) * 8 // 10
            c = (cx << sft, cy << sft, cz << sft)
            w = [sum(mat[i][j] * c[i] for i in range(3)) >> 8 for j in range(3)]
            cen = [w[0] - s16(r.u16(0x4AB2A)), w[1] - h, w[2] - s16(r.u16(0x4AB2E))]
        d7 = (rng.randint(0, 0xFFFF) << 16) | sft
        r.run(0x31766, d0=cen[0] & 0xFFFFFFFF, d1=cen[1] & 0xFFFFFFFF, d2=cen[2] & 0xFFFFFFFF, d3=age, d4=kind, d7=d7)
        end = r.u32(P.LOGPTR); got = []; a = P.LOGBASE
        while a < end:
            k = r.u16(a)
            if k == 9: got.append(("tri", r.u16(a + 2), [(s16(r.u16(a + 4)), s16(r.u16(a + 6))), (s16(r.u16(a + 8)), s16(r.u16(a + 10))), (s16(r.u16(a + 12)), s16(r.u16(a + 14)))]))
            else: got.append(("disc", r.u16(a + 2), s16(r.u16(a + 4)), s16(r.u16(a + 6)), s16(r.u16(a + 8))))
            a += 16
        r.poke(0x34A28, b"\x4e\x75"); r.poke(0x33CAC, b"\x4e\x75")
        exp = model(T, r, cen, age, kind, sft)
        calls += 1; frags += len(got)
        if got != exp:
            bad += 1
            if bad < 4: print("DIFF it %d kind %d age %d shift %d cen %s: got %d %s want %d %s" % (it, kind, age, sft, cen, len(got), got[:2], len(exp), exp[:2]))
    print("burst_check: %d calls, %d fragments, %s" % (calls, frags, "FAILED %d" % bad if bad else "0 differences"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
