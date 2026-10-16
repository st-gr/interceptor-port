"""Return rule of every drawing operation handler (docs/re/04a-display-lists.md section 8c), checked in the rig by direct calls.

    python3 prim_ops.py [SEED] [N] [op ...]

For each operation the handler of the operation table (0x23408) is called directly with random operands and a random camera-space
vertex array (0x4D43C: some vertices behind the camera, some outside the view pyramid, some inside), with the polygon filler, the line
routine and the point sprites replaced by logging stubs (primlog.py).  The word D0 and the condition codes the handler returns are compared
with the rule stated in the note.  Prints the number of calls per operation and per outcome; exit 1 on any difference.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
"""
import collections, os, random, struct, sys
from common import Rig, IMGPATH, LIBPATH
import primlog as P
import visibility as vis

SLOTS = 0x4D43C
ARGS = 0x7E0000
FRAME = 0x7F0200                 # the drawer's frame (A6): the handlers use negative offsets from it
FILLER, LINE, SPR_A, SPR_B, SPR_C = 1, 2, 3, 4, 5
CLIP_A, CLIP_B, LINE_CLIP, LINE_WHOLE, POINT_N, POINT_F1, POINT_F2 = 10, 11, 12, 13, 14, 15, 16


class Unit:
    def __init__(self):
        self.r = Rig()
        self.r.reset()
        self.orig = P.op_table(self.r)
        P.install(self.r, ops=False)
        self.n = collections.Counter()
        self.bad = collections.Counter()
        self.shown = 0

    def sr(self):
        return self.r.L.rig_get_reg(17)

    def set_state(self, rng, sd=None, lod=None, cam_h=None):
        r = self.r
        r.w16(0x4AB70, rng.randint(0, 5) if lod is None else lod)
        r.w16(0x4ABE2, rng.randint(-300, 300)); r.w16(0x4ABE4, 0); r.w16(0x4ABE6, rng.randint(-300, 300))
        r.w32(0x4AB30, -(cam_h if cam_h is not None else rng.choice((0x10, 0x60, 0x90, 0xC0, 0x100, 0x300, 0x2000))))
        r.w16(0x4AB2A, rng.randint(-300, 300)); r.w16(0x4AB2E, rng.randint(-300, 300))
        r.w16(0x4AA3C, 0x7FFF)
        r.w16(0x4A79E, 0xF); r.w16(0x4A7A0, 0xFFFF)
        r.w16(FRAME - 0x28, rng.randint(0, 600) if sd is None else sd)

    def vertices(self, rng, n=24, mode=None):
        """a random camera-space vertex array: modes 'inside', 'behind', 'mixed', 'edge'"""
        mode = mode or rng.choice(('inside', 'inside', 'behind', 'mixed', 'mixed', 'edge'))
        vs = []
        for i in range(n):
            if mode == 'inside':
                z = rng.randint(20, 3000); x = rng.randint(-z, z) * 9 // 10; y = rng.randint(-z, z) * 9 // 10
            elif mode == 'behind':
                z = rng.randint(-3000, -1); x = rng.randint(-3000, 3000); y = rng.randint(-3000, 3000)
            elif mode == 'edge':
                z = rng.randint(1, 300); x = rng.choice((-z, z, z - 1, 1 - z, rng.randint(-z, z))); y = rng.choice((-z, z, z + 1, rng.randint(-z, z)))
            else:
                z = rng.randint(-200, 2000); x = rng.randint(-2 * abs(z) - 5, 2 * abs(z) + 5); y = rng.randint(-2 * abs(z) - 5, 2 * abs(z) + 5)
            vs.append((x, y, z))
        self.r.poke(SLOTS, b"".join(struct.pack(">3h", *v) for v in vs))
        return vs

    def call(self, op, words, extra=None):
        r = self.r
        r.poke(ARGS, b"".join(struct.pack(">H", w & 0xFFFF) for w in words) + bytes(32))
        P.clear_log(r)
        r.L.rig_set_max_instructions(2000000)
        r.run(self.orig[op], a2=ARGS, a6=FRAME)
        log = P.read_log(r)
        d0 = P.s16(r.reg('d0') & 0xFFFF)
        sr = self.sr()
        lt = ((sr >> 3) & 1) ^ ((sr >> 1) & 1)
        prims = collections.Counter(rec[1] for rec in log if rec[0] == 4)
        calls = [(rec[1], P.s16(rec[2]), (rec[3] >> 3 & 1) ^ (rec[3] >> 1 & 1)) for rec in log if rec[0] == 5]
        return d0, lt, prims, calls, r.reg('a2') - ARGS

    def check(self, op, name, ok, info):
        self.n[(op, name)] += 1
        if not ok:
            self.bad[(op, name)] += 1
            if self.shown < 12:
                self.shown += 1
                print('DIFF op', op, name, info)

R = lambda rng, a, b: rng.randint(a, b)
V = lambda i: 6 * i                       # byte offset of slot i


def run_op(u, op, rng):
    """one random call of operation `op` and the check of its rule"""
    ch = u.check
    u.set_state(rng)
    vs = u.vertices(rng)
    pen = R(rng, 0, 15)
    fill = lambda prims: prims[FILLER] > 0
    lines = lambda prims: prims[LINE] > 0
    if op == 0:
        d0, lt, pr, cl, a2 = u.call(op, [])
        ch(op, 'returns 0, no primitive', d0 == 0 and not lt and not pr and a2 == 0, (d0, lt, dict(pr)))
    elif op in (1, 58):
        i = R(rng, 0, 23)
        d0, lt, pr, cl, a2 = u.call(op, [V(i), pen])
        x, y, z = vs[i]
        sprite = pr[SPR_A] + pr[SPR_B] + pr[SPR_C] > 0
        inside = z > 0 and -z < x < z and -z < y < z
        ch(op, 'inside the open pyramid: returns 1; outside or behind: returns 0 with negative condition codes (the plain drawer abandons the program)',
           d0 == (1 if inside else 0) and lt == (0 if inside else 1) and sprite == inside, (vs[i], d0, lt, dict(pr)))
    elif op in (2, 74, 48):
        i, j = R(rng, 0, 23), R(rng, 0, 23)
        near_gate = u.r.s32(0x4AB30) >= -0xC0
        d0, lt, pr, cl, a2 = u.call(op, [V(i), V(j), pen])
        (x1, y1, z1), (x2, y2, z2) = vs[i], vs[j]
        if op == 74 and near_gate:
            ch(op, 'height gate: V:4AB30 at or above -0xC0 skips the item, returns 0', d0 == 0 and not lt and not pr and a2 == 6, (d0, a2))
        elif op == 48:
            inb = lambda x, y, z: z > 0 and -z <= x <= z and -z <= y <= z
            ch(op, 'returns 1 iff both endpoints inside the closed pyramid (no clipping)', d0 == (1 if inb(*vs[i]) and inb(*vs[j]) else 0) and not lt and lines(pr) == (d0 == 1), (vs[i], vs[j], d0, dict(pr)))
        else:
            ch(op, 'returns 1 iff the clipped line is drawn', d0 == (1 if lines(pr) else 0) and not lt, (vs[i], vs[j], d0, dict(pr)))
    elif op in (13, 75):
        k = R(rng, 1, 5); idx = [R(rng, 0, 23) for _ in range(2 * k)]
        words = [pen] + [V(x) for x in idx]; words[-1] |= 0x8000
        gate = op == 75 and u.r.s32(0x4AB30) >= -0xC0
        d0, lt, pr, cl, a2 = u.call(op, words)
        if gate:
            ch(op, 'height gate skips the list, returns 0', d0 == 0 and not pr and a2 == 2 * len(words), (d0, a2))
        else:
            ch(op, 'returns 1 iff any line of the list is drawn (OR)', d0 == (1 if lines(pr) else 0) and not lt and a2 == 2 * len(words), (d0, dict(pr), a2))
    elif op == 14:
        k = R(rng, 1, 4); idx = [R(rng, 0, 20) for _ in range(2 * k)]
        words = [pen, V(R(rng, 0, 20))] + [V(x) for x in idx]; words[-1] |= 0x8000
        d0, lt, pr, cl, a2 = u.call(op, words)
        ch(op, 'returns 1 iff any shifted line is drawn (OR)', d0 == (1 if lines(pr) else 0) and not lt, (d0, dict(pr)))
    elif op == 11:
        n = R(rng, 1, 6)
        d0, lt, pr, cl, a2 = u.call(op, [(n << 8) | R(rng, 0, 63), V(R(rng, 0, 10))])
        ch(op, 'returns 1 iff any segment is drawn (OR)', d0 == (1 if lines(pr) else 0) and not lt, (d0, dict(pr)))
    elif op == 12:
        n = R(rng, 1, 6)
        d0, lt, pr, cl, a2 = u.call(op, [(n << 8) | R(rng, 0, 63), V(R(rng, 0, 10)), V(R(rng, 0, 10))])
        ch(op, 'returns 1 iff any shifted segment is drawn (OR)', d0 == (1 if lines(pr) else 0) and not lt, (d0, dict(pr)))
    elif op == 6:
        n = R(rng, 2, 6)
        d0, lt, pr, cl, a2 = u.call(op, [n, V(R(rng, 0, 20)), V(R(rng, 0, 20)), V(R(rng, 0, 20)), V(R(rng, 0, 20)), pen])
        ch(op, 'returns 1 iff any rung is drawn (OR)', d0 == (1 if lines(pr) else 0) and not lt, (d0, dict(pr)))
    elif op in (3, 69):
        k = R(rng, 3, 6) if op == 3 else 3; idx = [R(rng, 0, 20) for _ in range(k)]
        W = rng.choice((0x8000 | pen, pen, pen | 0x4000, (R(rng, 0, 7) << 7) | pen, (R(rng, 0, 7) << 7) | pen | 0x4000))
        words = [V(x) for x in idx]
        if op == 3: words[-1] |= 0x8000
        words.append(W)
        if W & 0x4000 and not W & 0x8000: words.append(R(rng, 0, 15))
        d0, lt, pr, cl, a2 = u.call(op, words)
        pts = [vs[x] for x in idx]
        zs = [p[2] for p in pts]
        if op == 69:
            z4 = (zs[0] - zs[1] + zs[2]) & 0xFFFF
            zs = zs + [z4 - 0x10000 if z4 & 0x8000 else z4]
        allbehind = all(z < 0 for z in zs)
        face_away = None
        if not allbehind and not W & 0x8000:
            face_away = vis.Draw.plane_test(None, pts[:3], W)
        if allbehind:
            ok = d0 == -1 and lt and not pr
        elif W & 0x8000 or face_away == 0:
            ok = d0 == (1 if fill(pr) else 0) and not lt
        elif W & 0x4000:
            ok = d0 == (1 if fill(pr) else 0) and not lt
        else:
            ok = d0 == -1 and lt and not pr
        ch(op, 'polygon (op 69: quad A, B, C, A-B+C with the same word W): -1 when all vertices are behind or the face is away without bit 14, else 1 iff the filler is reached', ok, (pts, hex(W), face_away, d0, dict(pr)))
    elif op in (24, 25, 26):
        k = R(rng, 3, 7)
        if op == 24:
            idx = [R(rng, 0, 23) for _ in range(k)]
            words = [k] + [V(x) for x in idx] + [pen]
        elif op == 25:
            words = [k, pen]; idx = list(range(k))
        else:
            s = R(rng, 0, 12); words = [k, V(s)]; idx = list(range(s, s + k))
        d0, lt, pr, cl, a2 = u.call(op, words)
        allbehind = all(vs[x][2] < 0 for x in idx)
        if op == 26:
            ok = (d0 == 0 and not lt and not pr) if allbehind else (d0 == (1 if fill(pr) else 0) and lt == 1)
            ch(op, 'every listed depth behind: returns 0 with non-negative condition codes; otherwise 1 iff the filler is reached and the condition codes are negative (the plain drawer would abandon the program; the type B drawer tests the word)', ok, (d0, lt, dict(pr), allbehind))
        else:
            ch(op, 'returns 1 iff the polygon reaches the filler', d0 == (1 if fill(pr) else 0) and not lt, (d0, dict(pr)))
    elif op in (9, 10, 15, 16, 27, 30, 7, 8):
        a, b, c, e = [R(rng, 0, 20) for _ in range(4)]
        words = {9: [pen, V(a)], 10: [pen, V(a)], 15: [pen, V(a), V(b), V(c), V(e)], 16: [pen, V(a), V(b), V(c)], 27: [pen, V(a)], 30: [pen, V(a)],
                 7: [pen, V(a), V(b), V(c), V(e)], 8: [pen, V(a), V(b), V(c), V(e), V(R(rng, 0, 20))]}[op]
        near = u.r.s32(0x4AB30) >= -0x80
        d0, lt, pr, cl, a2 = u.call(op, words)
        if op in (9, 10) and not near:
            ch(op, 'height gate: V:4AB30 below -0x80 returns 0 without drawing', d0 == 0 and not pr, (d0, dict(pr)))
        else:
            ch(op, 'returns 1 iff the polygon reaches the filler (0 when culled)', d0 == (1 if fill(pr) else 0) and not lt, (d0, lt, dict(pr)))
    elif op == 17:
        q = R(rng, 1, 3); words = []
        for _ in range(q): words += [V(R(rng, 0, 20)) for _ in range(4)]
        words.append(0xFFFF)
        d0, lt, pr, cl, a2 = u.call(op, words)
        ch(op, 'returns 1 iff any quad of the list reaches the filler (OR)', d0 == (1 if fill(pr) else 0) and not lt, (d0, dict(pr)))
    elif op in (39, 40):
        dat = [R(rng, 0, 15), R(rng, 0, 15)] if op == 39 else [R(rng, 0, 15), R(rng, 0, 15)]
        words = dat + ([R(rng, 0, 15)] if op == 39 else [])
        # slots 0..3 (and 4 for op 40) are the absolute slots
        x, y, z = vs[1]
        outside = not (z >= x and z >= -x and z >= y and z >= -y)
        d0, lt, pr, cl, a2 = u.call(op, words)
        if outside:
            ch(op, 'slot 1 outside the closed pyramid (or behind): returns -1', d0 == -1 and lt and not pr, (vs[1], d0, dict(pr)))
        else:
            ch(op, 'slot 1 inside: returns 1 iff any polygon reaches the filler (OR)', d0 == (1 if fill(pr) else 0) and not lt, (d0, dict(pr)))
    elif op in (19, 22):
        n = R(rng, 1, 4)
        if op == 19:
            d0, lt, pr, cl, a2 = u.call(op, [pen, V(R(rng, 0, 10)), n])
        else:
            n2 = [R(rng, 1, 3) for _ in range(n)]
            d0, lt, pr, cl, a2 = u.call(op, [pen, V(R(rng, 0, 10)), n] + n2)
        ch(op, 'returns 1 iff any stroke is drawn (OR over all strokes)', d0 == (1 if lines(pr) else 0) and not lt, (d0, dict(pr)))
    elif op in (20, 32):
        n = R(rng, 1, 4)
        words = ([pen] if op == 32 else []) + [V(R(rng, 0, 10)), n]
        d0, lt, pr, cl, a2 = u.call(op, words)
        res = [c[1] for c in cl if c[0] == CLIP_B]
        first = 1 if any(res[:n]) else 0
        ch(op, 'returns the OR of the first N strips only; the second N strips do not count', d0 == first and not lt and len(res) == 2 * n, (n, res, d0))
    elif op in (21, 57):
        n1 = R(rng, 1, 3); n2 = [R(rng, 1, 3) for _ in range(n1)]
        words = ([pen] if op == 57 else []) + [V(R(rng, 0, 10)), n1] + n2
        d0, lt, pr, cl, a2 = u.call(op, words)
        res = [c[1] for c in cl if c[0] == CLIP_B]
        ch(op, 'returns the OR of the clip results of all stripes (one clipper call per stripe)', d0 == (1 if any(res) else 0) and not lt and len(res) == sum(n2), (res, d0, n2))
        ch(op, 'returns 1 iff any stripe reaches the filler', d0 == (1 if fill(pr) else 0) and not lt, (d0, dict(pr)))
    elif op == 23:
        n = R(rng, 1, 4); n2 = R(rng, 1, 3)
        d0, lt, pr, cl, a2 = u.call(op, [V(R(rng, 0, 10)), n])
        ch(op, 'returns 1 iff any strip reaches the filler', d0 == (1 if fill(pr) else 0) and not lt, (d0, dict(pr)))

    elif op == 31:
        # early-out against the first two vertices of the block at the operand: the pre-test of rule 6 as a program item
        blk = 0x78000
        xs = [R(rng, -9000, 9000) for _ in range(4)]
        if rng.random() < .3: xs[2:] = [xs[0] + R(rng, -3, 3), xs[1] + R(rng, -3, 3)]
        opd = rng.choice((0, 4, 12, 20, 36))         # the operand: a byte offset into the vertex list of the drawn block (V:4AAEA); the shipped values are 12 and 20
        u.r.poke(blk + 10 + opd, b"".join(struct.pack(">h", v) for v in xs) + bytes(16))
        u.r.w32(0x4AAEA, blk)
        u.r.w8(0x4A924, rng.choice((0, 0, 0, 1)))
        cam = rng.choice((-0x40, -0x100, -0x13F, -0x140, -0x141, -0x300, 0x0, 0x20))
        u.r.w32(0x4AB30, cam)
        sd = R(rng, 0, 300) if rng.random() < .8 else R(rng, 0, 12000)
        u.r.w16(FRAME - 0x28, sd)
        ncx, ncz = R(rng, -4000, 4000), R(rng, -4000, 4000)
        u.r.w16(FRAME - 0x26, ncx); u.r.w16(FRAME - 0x24, 0); u.r.w16(FRAME - 0x22, ncz)
        wx, wz = R(rng, -400, 400), R(rng, -400, 400)
        u.r.w16(0x4ABE2, wx); u.r.w16(0x4ABE6, wz)
        d0, lt, pr, cl, a2 = u.call(op, [opd])
        if cam <= -0x140 or u.r.u8(0x4A924):
            ch(op, 'no test (V:4AB30 at or below -0x140, or V:4A924 not zero): returns 0', d0 == 0 and not lt and a2 == 2, (d0, lt, cam))
        else:
            m = vis.Mem(u.r.peek(0, 0x80000))
            st = type('S', (), {})(); st.m = m; st.sd = sd; st.wx = wx; st.wz = wz; st.ncw = [ncx, 0, ncz]
            want = vis.Draw.pretest(st, blk + 10 + opd)
            ch(op, 'pre-test rejects (dot measure below the table entry): returns -1, the program is abandoned' if want < 0 else 'pre-test passes: returns 0', d0 == want and (lt == (1 if want < 0 else 0)), (d0, want, sd, cam))
    elif op == 67:
        a, b, c = [R(rng, 0, 20) for _ in range(3)]
        W = rng.choice((0, 0x80, 0x100, 0x180, 0x200, 0x300, 0x280))
        d0, lt, pr, cl, a2 = u.call(op, [V(a), V(b), V(c), W] + [0] * 9)
        pts = [vs[a], vs[b], vs[c]]
        away = vis.Draw.plane_test(None, pts, W)
        ch(op, 'face selector: skips the next 18 bytes when the face is away; never negative, draws nothing', not lt and not pr and a2 == (8 + (18 if away else 0)), (a2, away, d0, lt))
        # D0 is no result: the low word of the first normal component times x of the first vertex (the leftover of the face test, 0x22ACC)
        k = (W >> 7) & 7
        sh = lambda t: (t << k) & 0xFFFF
        e1 = [sh(pts[1][i] - pts[0][i]) for i in range(3)]; e2 = [sh(pts[2][i] - pts[0][i]) for i in range(3)]
        e1, e2 = [[t - 0x10000 if t & 0x8000 else t for t in e] for e in (e1, e2)]
        nx = ((e1[1] * e2[2] - e1[2] * e2[1]) >> 8) & 0xFFFF
        nx = nx - 0x10000 if nx & 0x8000 else nx
        ch(op, 'D0 on return = low word of (first normal component * x of the first vertex)', d0 == P.s16((nx * pts[0][0]) & 0xFFFF), (d0, nx, pts[0], hex(W)))
    elif op == 49:
        a, b, c = [R(rng, 0, 20) for _ in range(3)]
        W = rng.choice((0x180, 0x189, 0x80, 0x0))
        d0, lt, pr, cl, a2 = u.call(op, [V(a), V(b), V(c), W])
        away = vis.Draw.plane_test(None, [vs[a], vs[b], vs[c]], W)
        x, y, z = vs[a]
        inside = z > 0 and -z < x < z and -z < y < z
        if away:
            ok = d0 == 0 and not lt and not pr
        else:
            ok = d0 == (1 if inside else 0) and lt == (0 if inside else 1)
        ch(op, 'point at vertex 0 when the face test passes: 0 (not negative) when the face is away, else 1 drawn or 0 with negative codes rejected', ok, (d0, lt, away, inside))
    elif op in (33, 34, 35, 36, 38, 41, 42, 43, 44, 45, 46, 47, 50, 51, 52, 53, 54, 56, 59, 63, 64, 65, 66, 68, 70, 72, 73, 76, 77):
        words = [R(rng, 0, 40) * 6 for _ in range(4)] + [0] * 12
        d0, lt, pr, cl, a2 = u.call(op, words)
        ch(op, 'returns 0 (no primitive, never negative)', d0 == 0 and not lt and not pr, (d0, lt, dict(pr)))
    else:
        raise KeyError(op)


def edge_39_40(u, rng, n):
    """Ops 39 and 40: slot 1 with coordinates at the ends of the word range.  The test "outside the closed pyramid" is made of word negations (x > z, then
    the word -x > z, likewise y), so a coordinate of -32,768 is never beyond a minus plane, and the true rule |x| > z would reject it.
    A slot that passes goes on to the quads' clipper with whatever the other slots hold; with extreme words the clipper can write far outside its buffers (it
    corrupted the rig's log pointer in one call of seed 5), so every call that passes runs on a fresh rig and the rig is rebuilt after it, the log pointer is checked before it is read, and
    such a call is counted as 'ran on' (not -1) without reading the log."""
    ch = u.check
    ext = (-32768, -32767, -1, 0, 1, 32766, 32767)
    w16 = lambda v: ((v + 0x8000) & 0xFFFF) - 0x8000
    for op in (39, 40):
        for _ in range(n):
            x, y, z = (rng.choice(ext) if rng.random() < .6 else rng.randint(-40, 40) for _ in range(3))
            if rng.random() < .5: x = -32768
            if rng.random() < .3: y = -32768
            outside_word = x > z or w16(-x) > z or y > z or w16(-y) > z
            outside_int = x > z or -x > z or y > z or -y > z
            if not outside_word:
                u.r.reset(); P.install(u.r, ops=False)
            u.set_state(rng)
            vs = u.vertices(rng)
            vs[1] = (x, y, z); u.r.poke(SLOTS + 6, struct.pack(">3h", x, y, z))
            words = [R(rng, 0, 15), R(rng, 0, 15)] + ([R(rng, 0, 15)] if op == 39 else [])
            r = u.r
            r.poke(ARGS, b"".join(struct.pack(">H", w & 0xFFFF) for w in words) + bytes(32))
            P.clear_log(r); r.L.rig_set_max_instructions(2000000)
            r.run(u.orig[op], a2=ARGS, a6=FRAME)
            d0 = P.s16(r.reg('d0') & 0xFFFF); sr = u.sr(); lt = ((sr >> 3) & 1) ^ ((sr >> 1) & 1)
            end = r.u32(P.LOGPTR)
            sane = P.LOGBASE <= end <= P.LOGBASE + 16 * 4000
            empty = sane and end == P.LOGBASE
            rejected = d0 == -1 and lt and empty
            ch(op, 'word negation edge: slot 1 outside iff x > z or w16(-x) > z or y > z or w16(-y) > z (return -1, nothing logged)', rejected == outside_word, ((x, y, z), d0, sane, empty))
            if outside_word != outside_int:
                ch(op, 'word and 32-bit negation differ here: the original follows the word rule', rejected == outside_word, ((x, y, z), d0))
            if not outside_word and not sane:
                ch(op, 'passing slot with a clipper that overran its buffers (counted, log not read)', True, None)
            if not outside_word:                                    # the clipper ran: leave a clean rig for the next call, whatever it overran
                u.r.reset(); P.install(u.r, ops=False)


def op9_residue(u, rng, n):
    """Ops 9 and 10 (docs/re/03-3d-pipeline.md "Op 9 reads past its quad").  Buffer layout: V:5103C shift word, V:5103E count word, then vertices from 0x51040 of three words (x, y, z), 6 bytes
    each, so the depth word of vertex k (0 based) is at 0x51044 + 6 k: V3 at 0x51056, V4 at 0x5105C, V5 at 0x51062, V6 at 0x51068.
    Op 9 builds V0 = slot a, V1 = slot a + 1, V2 = slot a + 2, V3 = V2 - (V1 - V0) and drops the quad (return 0, clipper not called) iff depth(V3) AND the words at 0x5105C, 0x51062, 0x51068 is negative.
    Op 10 builds V0 = slot a + 3, V1 = V0 + (slot a + 1 - slot a), V2 = V1 + (slot a + 2 - slot a + 1), V3 = V2 - (V1 - V0) and drops it iff the AND of its own four depths is negative; the stale words play no part."""
    ch = u.check
    import ctypes
    w16 = lambda v: ((v + 0x8000) & 0xFFFF) - 0x8000
    arr = (ctypes.c_uint32 * 1)(0x28B6A)
    for op in (9, 10):
        for _ in range(n):
            u.set_state(rng, cam_h=rng.choice((0x10, 0x40, 0x7F, 0x80)))
            vs = u.vertices(rng, mode='mixed')
            a = R(rng, 0, 20)
            stale = [rng.choice((-1, -5, -3000, 0, 7, 3000, rng.randint(-3000, 3000))) for _ in range(3)]
            for addr, z in zip((0x5105C, 0x51062, 0x51068), stale):
                u.r.w16(addr, z)
            words = [R(rng, 0, 15), V(a)]
            if op == 9:
                z0, z1, z2 = vs[a][2], vs[a + 1][2], vs[a + 2][2]
                z3 = w16(z2 - (z1 - z0))
                reject = (z3 & stale[0] & stale[1] & stale[2]) < 0
                name = 'quad rejected iff (z3 AND the stale words at 0x5105C, 0x51062, 0x51068) < 0 (then returns 0, no clipper call)'
            else:
                zp = vs[a + 3][2]; dz = w16(vs[a + 1][2] - vs[a][2]); dz2 = w16(vs[a + 2][2] - vs[a + 1][2])
                zs = [zp, w16(zp + dz), w16(zp + dz + dz2)]; zs.append(w16(zs[2] - dz))
                reject = (zs[0] & zs[1] & zs[2] & zs[3]) < 0
                name = 'quad rejected iff the AND of its own four depths is negative (stale words play no part)'
            u.r.L.rig_watch_set(arr, 1); u.r.L.rig_trace_clear()
            d0, lt, pr, cl, a2 = u.call(op, words)
            called = u.r.L.rig_trace_get(None, 0) > 0
            u.r.L.rig_watch_set(arr, 0)
            ch(op, name, called == (not reject) and (not reject or d0 == 0), (vs[a:a + 4], stale, d0, dict(pr)))
            u.n[(op, 'calls rejected (the rest reach the clipper)')] += 1 if reject else 0


OPS = [31, 49, 67, 0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 19, 20, 21, 22, 23, 24, 25, 26, 27, 30, 32, 39, 40, 48, 57, 58, 69, 74, 75,
       33, 34, 35, 36, 38, 41, 42, 43, 44, 45, 46, 47, 50, 51, 52, 53, 54, 56, 59, 63, 64, 65, 66, 68, 70, 72, 73, 76, 77]


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 400
    u = Unit(); rng = random.Random(seed)
    if argv[3:4] == ['op9']:
        op9_residue(u, rng, n)
        for key in sorted(u.n):
            print('op %2d  %-110s %5d calls  %d differences' % (key[0], key[1], u.n[key], u.bad[key]))
        bad = sum(u.bad.values())
        print('prim_ops op9: %s' % ('FAILED, %d differences' % bad if bad else 'ok'))
        return 1 if bad else 0
    if argv[3:4] == ['edge']:
        edge_39_40(u, rng, n)
        for key in sorted(u.n):
            print('op %2d  %-110s %5d calls  %d differences' % (key[0], key[1], u.n[key], u.bad[key]))
        bad = sum(u.bad.values())
        print('prim_ops edge: %s' % ('FAILED, %d differences' % bad if bad else 'ok'))
        return 1 if bad else 0
    ops = [int(a) for a in argv[3:]] or OPS
    for op in ops:
        for _ in range(n):
            run_op(u, op, rng)
    for key in sorted(u.n):
        print('op %2d  %-110s %5d calls  %d differences' % (key[0], key[1], u.n[key], u.bad[key]))
    bad = sum(u.bad.values())
    print('prim_ops: %d handler calls over %d operations, %d checks (ops 21, 57 and 67 have two per call), %s' % (n * len(ops), len(ops), sum(u.n.values()), 'FAILED, %d differences' % bad if bad else 'ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
