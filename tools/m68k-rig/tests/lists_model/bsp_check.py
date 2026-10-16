"""The three side tests of the block tree walk (docs/re/03-3d-pipeline.md, "The side tests of the block tree"; docs/re/04a-display-lists.md section 8b).

    python3 bsp_check.py [SEED] [N]

Direct calls in the rig of the three routines the tree walk (0x225F0) uses to choose a child, with random operands, compared with word-exact models:
the point-and-normal test (entry 0x22A94, flag bit 13), the axis test (0x22B3A, flag bits 10-11) and the three-vertex face test (0x22A84 -> 0x22ACC).
Result of each: Z clear (the second child; D7 = 1 for the plane and face tests) or Z set (the first child); the walk tests Z only.  Prints the number of calls per test; exit 1 on any difference.
The eye words of the point-and-normal test are not poked from the formula: the original instructions that write them (0x21E26-0x21E5A, the head of the block header loader
0x21E02: camera longs V:4AB1A.. shifted right by the LOD, then by 8, negated low words stored at the frame offsets -0x26, -0x24, -0x22) are copied into the scratch
memory and run on the random camera longs and LOD; the words they leave are compared with the formula ("frame" in the output) and then used by the test.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH

FRAME = 0x7F0200


def w16(x):
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def w32(x):
    x &= 0xFFFFFFFF
    return x - (1 << 32) if x & 0x80000000 else x


def sr_z(r): return (r.L.rig_get_reg(17) >> 2) & 1


def eye_words(cam):
    """the drawer frame words -0x26, -0x24, -0x22: minus the low word of ((camera long >> LOD) >> 8); cam = (V:4AB1A, V:4AB1E, V:4AB22), lod"""
    longs, lod = cam
    return [w16(-w16(((c >> lod) >> 8))) for c in longs]


def plane_model(p, n, lod, posx, posz, eye):
    d0 = w16(w16(p[0]) >> lod) ; d1 = w16(w16(p[1]) >> lod); d2 = w16(w16(p[2]) >> lod)
    d0 = w16(d0 + posx); d2 = w16(d2 + posz)
    d0 = w16(d0 - eye[0]); d1 = w16(d1 - eye[1]); d2 = w16(d2 - eye[2])
    s = w32(w32(d0 * n[0] + d2 * n[2]) + d1 * n[1])
    return 1 if s >= 0 else 0


def axis_model(axis, flags, const, lod, shl, posw, cam_off_x, cam_off_z, cam_h_long):
    c = w16(const) >> shl
    if axis == 1:
        e, pc = w32(-cam_h_long), c
    elif axis == 2:
        e, pc = w16(-cam_off_x), w16(c + w16(posw << lod))
    else:
        e, pc = w16(-cam_off_z), w16(c + w16(posw << lod))
    below = e < pc
    return (1 if below else 0) if not flags & 0x1000 else (0 if below else 1)


def face_model(v0, v1, v2, flags):
    s = (flags >> 7) & 7
    e1 = [w16((a - b) << s) for a, b in zip(v1, v0)]
    e2 = [w16((a - b) << s) for a, b in zip(v2, v0)]
    nx = w16(w32(e1[1] * e2[2] - e1[2] * e2[1]) >> 8)
    ny = w16(w32(e1[2] * e2[0] - e2[2] * e1[0]) >> 8)
    nz = w16(w32(e1[0] * e2[1] - e2[0] * e1[1]) >> 8)
    t = w32(w32(nx * v0[0] + ny * v0[1]) + nz * v0[2])
    return 1 if t >= 0 else 0


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 1500
    r = Rig(); r.reset(); rng = random.Random(seed)
    R = rng.randint
    cnt = {'plane': 0, 'axis': 0, 'face': 0, 'frame': 0}; bad = {'plane': 0, 'axis': 0, 'face': 0, 'frame': 0}
    WRITER = 0x8F0300
    r.poke(WRITER, r.peek(0x21E26, 0x5A - 0x26) + bytes([0x4E, 0x75]))     # the original bytes of the writer, plus a return
    def chk(kind, ok, info):
        cnt[kind] += 1
        if not ok:
            bad[kind] += 1
            if sum(bad.values()) < 10: print('DIFF', kind, info)
    big = lambda: R(-32768, 32767) if rng.random() < 0.3 else R(-3000, 3000)
    for _ in range(n):
        # point and normal
        lod = R(0, 6); longs = [R(-(1 << 26), 1 << 26) for _ in range(3)]
        posx, posz = big(), big()
        r.w16(0x4AB70, lod); r.w16(0x4ABE2, posx); r.w16(0x4ABE6, posz)
        for i, v in enumerate(longs): r.w32(0x4AB1A + 4 * i, v & 0xFFFFFFFF)
        for i in range(3): r.w16(FRAME - 0x26 + 2 * i, 0x5A5A)
        r.run(WRITER, a6=FRAME)
        eye = [r.s16(FRAME - 0x26 + 2 * i) for i in range(3)]
        chk('frame', eye == eye_words((longs, lod)), (longs, lod, eye, eye_words((longs, lod))))
        p = [big() for _ in range(3)]; nn = [big() for _ in range(3)]
        r.run(0x22A94, d0=p[0] & 0xFFFF, d1=p[1] & 0xFFFF, d2=p[2] & 0xFFFF, d3=nn[0] & 0xFFFF, d4=nn[1] & 0xFFFF, d5=nn[2] & 0xFFFF, a6=FRAME)
        d7 = r.reg('d7') & 0xFFFF
        got = d7 if (d7 in (0, 1) and sr_z(r) == (1 if d7 == 0 else 0)) else None
        chk('plane', got == plane_model(p, nn, lod, posx, posz, eye), (p, nn, lod, posx, posz, eye, d7))
        # axis
        blk = 0x78000; axis = R(1, 3); flags = (axis << 10) | (rng.choice((0, 0x1000))) | R(0, 0x3FF) & 0x2FF
        flags &= ~0x2000
        off = 6 * R(0, 5)
        const = big(); shl = R(0, 15)
        words = [big() for _ in range(3)]; words[{2: 0, 1: 1, 3: 2}[axis]] = const
        r.poke(blk + 10 + off, struct.pack(">3h", *words))
        r.w8(blk + 6, (R(0, 15) << 4) | shl); r.w32(0x4AAEA, blk)
        lod = R(0, 6); r.w16(0x4AB70, lod); px_, pz_ = big(), big(); posw = px_ if axis == 2 else pz_; r.w16(0x4ABE2, px_); r.w16(0x4ABE6, pz_)
        cx, cz = big(), big(); ch = R(-(1 << 24), 1 << 24)
        r.w16(0x4AB2A, cx); r.w16(0x4AB2E, cz); r.w32(0x4AB30, ch & 0xFFFFFFFF)
        r.run(0x22B3A, d0=off, d7=flags, a6=FRAME)
        d7 = r.reg('d7') & 0xFFFF
        got = 0 if sr_z(r) else 1                          # the walk tests the Z flag only (D7 holds the compared value when the test falls through)
        chk('axis', got == axis_model(axis, flags, const, lod, shl, posw, cx, cz, ch), (axis, hex(flags), const, shl, lod, posw, cx, cz, ch, d7))
        # three-vertex face test
        vs = [tuple(big() if rng.random() < .5 else R(-400, 400) for _ in range(3)) for _ in range(3)]
        r.poke(0x51040, b"".join(struct.pack(">3h", *v) for v in vs))
        flags = (R(0, 7) << 7) | R(0, 0x7F)
        r.run(0x22A84, d7=flags, a3=flags, a6=FRAME)
        d7 = r.reg('d7') & 0xFFFF
        got = d7 if (d7 in (0, 1) and sr_z(r) == (1 if d7 == 0 else 0)) else None
        chk('face', got == face_model(*vs, flags), (vs, hex(flags), d7))
    for k in cnt: print('bsp test %-6s %5d calls  %d differences' % (k, cnt[k], bad[k]))
    tot = sum(bad.values())
    print('bsp_check: %s' % ('FAILED, %d differences' % tot if tot else 'ok'))
    return 1 if tot else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
