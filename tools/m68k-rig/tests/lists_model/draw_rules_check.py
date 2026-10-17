"""Rules of the draw operations that the handler-level differential of the port (tests/rig/test_draw_diff.c) found missing from note 03, run in the rig
(docs/re/03-3d-pipeline.md, "Draw operations", "Projection", "The clipped line routine", "Line operations 6, 11, 12, 14").

    python3 draw_rules_check.py [SEED] [N]

Sections (each prints calls and differences):
  rows     the screen row of each projection entry: points 180 - sy' (ops 1, 49, 58, 62), the unclipped line op 48, the clipped line op 2 and the
           polygon path (op 3) 179 - sy'
  pen      when each handler writes the current pen V:4AA0C: at entry, after its early-out, or never (the table of note 03)
  op26     the early-out reads N + 1 slots; the pen, the mask and the pattern are written only when it does not exit; the solid defaults come back after the draw
  op27     ops 27 and 30: the early-out takes four depths (A, B, C and A - B + C as words); the stipple and the pen are written before it
  op6      N in {-32768, -1, 0, 1, 2, 3}: rungs drawn, trap, extent of the buffer written at 0x4FE3C, the corruption of rungs from the 257th on
  census   models that use ops 52 and 53, and the shape of the stream their skips jump over
  neg      word negation: a coordinate of -32768 (the negation of -32768 is -32768) in the clipped line routine; the rig against the word model and against
           a model with a 32-bit negation
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.  Exit 1 on any difference.
"""
import collections, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
import primlog as P
import line_model as L
import prim_ops as PO

SLOTS, ARGS, FRAME = 0x4D43C, 0x7E0000, 0x7F0200
STUB_FILL, STUB_LINE, STUB_TRAP = 0x8F1000, 0x8F1100, 0x8F1200
TRAP = 0x8FF110
BUF = 0x4FE3C
SENT = {0x4A79E: 0x1357, 0x4A7A0: 0x2468, 0x4A7A2: 0x369C, 0x4A7A4: 0x47AD, 0x4AA0C: 0x1234}


def tdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def log_stub(ident):
    """a stub for an output routine: records (kind 9: id, D0-D3, V:4AA0C, V:4A79E) and (kind 10: id, V:4A7A0, V:4A7A2, V:4A7A4) and returns"""
    c = P.w(0x40E7, 0x2F08)
    c += P.append_header(9, None, [(2, ident), (4, ("reg", 0)), (6, ("reg", 1)), (8, ("reg", 2)), (10, ("reg", 3)), (12, ("mem", 0x4AA0C)), (14, ("mem", 0x4A79E))])
    c += P.append_header(10, None, [(2, ident), (4, ("mem", 0x4A7A0)), (6, ("mem", 0x4A7A2)), (8, ("mem", 0x4A7A4))])
    c += P.w(0x205F, 0x44DF, 0x4E75)
    return c


class Unit(PO.Unit):
    def __init__(self):
        super().__init__()          # one Rig, the handler table read, the logging stubs of primlog installed (the machine is global: one Unit only)
        self.r.poke(STUB_FILL, log_stub(1)); self.r.poke(STUB_LINE, log_stub(2))
        self.r.poke(0x34A28, P.w(0x4EF9) + P.l(STUB_FILL))
        self.r.poke(0x3455E, P.w(0x4EF9) + P.l(STUB_LINE))
        self.r.poke(0x33CAC, P.w(0x4E75))          # the disc (op 62) writes the screen bitmap: out
        self.r.poke(STUB_TRAP, P.w(0x33FC, 0x0001) + P.l(TRAP) + P.w(0x4EF9) + P.l(0x100))
        self.r.poke(0x14, P.l(STUB_TRAP))
        self.cap = 2000000

    def state(self, rng, cam=None):
        r = self.r
        r.w16(0x4AB70, rng.randint(0, 5)); r.w32(0x4AB30, -(cam if cam is not None else rng.choice((0x10, 0x60, 0xC0, 0x100, 0x2000))))
        r.w16(0x4AA3C, 0x7FFF); r.w16(FRAME - 0x28, rng.randint(0, 600))
        r.w16(0x4ABE2, 0); r.w16(0x4ABE4, 0); r.w16(0x4ABE6, 0)

    def put(self, vs):
        self.r.poke(SLOTS, b"".join(struct.pack(">3h", *v) for v in vs))

    def xcall(self, op, words, sentinels=True):
        r = self.r
        r.poke(ARGS, b"".join(struct.pack(">H", w & 0xFFFF) for w in words) + bytes(32))
        if sentinels:
            for a, v in SENT.items(): r.w16(a, v)
        r.w16(TRAP, 0)
        P.clear_log(r)
        r.L.rig_set_max_instructions(self.cap)
        r.run(self.orig[op], a2=ARGS, a6=FRAME)
        log = P.read_log(r)
        d0 = P.s16(r.reg('d0') & 0xFFFF)
        sr = r.L.rig_get_reg(17)
        lt = ((sr >> 3) & 1) ^ ((sr >> 1) & 1)
        out = []
        for i, rec in enumerate(log):
            if rec[0] == 9:
                nxt = log[i + 1]
                out.append({'id': rec[1], 'd': tuple(P.s16(x) for x in rec[2:6]), 'pen': rec[6], 'mask': rec[7], 'pat': nxt[2], 'tog': nxt[3], 'w4': nxt[4]})
        calls = [(rec[1], P.s16(rec[2])) for rec in log if rec[0] == 5]
        sprites = [(rec[1], P.s16(rec[2]), P.s16(rec[3])) for rec in log if rec[0] == 4 and rec[1] in (3, 4, 5)]
        st = {a: r.u16(a) for a in SENT}
        return d0, lt, out, calls, sprites, st, r.reg('a2') - ARGS

    def n_check(self, sect, name, ok, info):
        self.n[(sect, name)] += 1
        if not ok:
            self.bad[(sect, name)] += 1
            if self.shown < 12:
                self.shown += 1; print('DIFF', sect, name, info)


def V(i): return 6 * i


def front(rng, lo=20, hi=3000, frac=0.9):
    z = rng.randint(lo, hi)
    return (int(rng.randint(-z, z) * frac), int(rng.randint(-z, z) * frac), z)


def behind(rng):
    return (rng.randint(-3000, 3000), rng.randint(-3000, 3000), rng.randint(-3000, -1))


def proj_rows(x, y, z):
    """sx', sy' of the line and polygon paths (clamped), screen (319 - sx', 179 - sy'); the point path has 180 - sy'"""
    sx = tdiv(160 * x, z) + 160; sy = tdiv(90 * y, z) + 90
    sx = min(max(sx, 0), 319); sy = min(max(sy, 0), 179)
    return 319 - sx, sy


# ------------------------------------------------------------------------------------------------------------------------------------ rows
def sec_rows(u, rng, n):
    for _ in range(n):
        u.state(rng)
        # op 48: both end points in the closed pyramid (z > 0, |x|, |y| <= z), boundary included
        vs = []
        for _ in range(12):
            z = rng.randint(1, 3000)
            vs.append((rng.choice((-z, z, rng.randint(-z, z))), rng.choice((-z, z, rng.randint(-z, z))), z))
        u.put(vs)
        i, j = rng.randint(0, 11), rng.randint(0, 11)
        d0, lt, out, calls, spr, st, a2 = u.xcall(48, [V(i), V(j), rng.randint(0, 15)])
        (x1, y1, z1), (x2, y2, z2) = vs[i], vs[j]
        e1, e2 = proj_rows(x1, y1, z1), proj_rows(x2, y2, z2)
        want = (e1[0], 179 - e1[1], e2[0], 179 - e2[1])
        u.n_check('rows', 'op 48 (unclipped line): screen (319 - sx\', 179 - sy\') for both end points, clamped; 180 - sy\' would differ on every call',
                d0 == 1 and len(out) == 1 and out[0]['d'] == want, (vs[i], vs[j], d0, out, want))
        # op 2: in the open pyramid, the clipped line routine projects the same way
        vs = [front(rng) for _ in range(12)]
        u.put(vs)
        i, j = rng.randint(0, 11), rng.randint(0, 11)
        d0, lt, out, calls, spr, st, a2 = u.xcall(2, [V(i), V(j), rng.randint(0, 15)])
        e1, e2 = proj_rows(*vs[i]), proj_rows(*vs[j])
        want = (e1[0], 179 - e1[1], e2[0], 179 - e2[1])
        u.n_check('rows', 'op 2 (clipped line): the same rows 179 - sy\' for end points inside the pyramid', d0 == 1 and len(out) == 1 and out[0]['d'] == want, (vs[i], vs[j], out, want))
        # op 3: a triangle inside the pyramid; the polygon clipper projects with 179 - sy'; compare the set of vertices handed to the filler
        tri = [front(rng, 200, 3000, 0.8) for _ in range(3)]
        vs = tri + [front(rng) for _ in range(9)]
        u.put(vs)
        d0, lt, out, calls, spr, st, a2 = u.xcall(3, [V(0), V(1), V(2) | 0x8000, 0x8000 | 5])
        want = sorted((proj_rows(*p)[0], 179 - proj_rows(*p)[1]) for p in tri)
        # the filler stub does not log the list: read it from the clipper's output area
        # (count word, then x, y word pairs)
        lst = u.r.peek(0x5043C, 2 + 4 * 3)
        c = struct.unpack(">H", lst[:2])[0]
        pts = sorted(struct.unpack(">2h", lst[2 + 4 * k:6 + 4 * k]) for k in range(min(c, 3)))
        u.n_check('rows', 'op 3 (polygon path): vertex rows 179 - sy\'', c == 3 and d0 == 1 and pts == want, (tri, c, pts, want))
        # point sprites: row 180 - sy' (the point path)
        vs = [front(rng) for _ in range(12)]
        u.put(vs)
        i = rng.randint(0, 11)
        d0, lt, out, calls, spr, st, a2 = u.xcall(1, [V(i), rng.randint(0, 15)])
        sx, sy = proj_rows(*vs[i])
        u.n_check('rows', 'op 1 (point sprite): screen (319 - sx\', 180 - sy\'), one row lower than the line and polygon paths', d0 == 1 and len(spr) == 1 and spr[0][1] == sx and spr[0][2] == 180 - sy, (vs[i], spr, sx, sy))


# ------------------------------------------------------------------------------------------------------------------------------------ pen
def sec_pen(u, rng, n):
    """V:4AA0C written (value != sentinel) per op and outcome, against the rule of note 03.  The operand streams are prim_ops.py's (run_op); its own return checks are switched off here"""
    ops = [1, 58, 2, 74, 48, 13, 75, 14, 11, 12, 6, 3, 69, 24, 25, 26, 27, 30, 9, 10, 15, 16, 7, 8, 17, 39, 40, 19, 20, 21, 22, 23, 32, 57, 49]
    where = {3: 'after the all-behind and face-away exits', 69: 'after the all-behind and face-away exits', 39: 'after the slot-1 exit', 40: 'after the slot-1 exit',
             26: 'after the early-out', 49: 'after the face-away exit', 74: 'after the height gate', 75: 'after the height gate', 23: 'never'}
    pcall, pcheck = u.call, u.check

    def pw(op, words, extra=None):
        u.r.w16(0x4AA0C, SENT[0x4AA0C])
        res = pcall(op, words, extra)
        d0, lt = res[0], res[1]
        written = u.r.u16(0x4AA0C) != SENT[0x4AA0C]
        cam = u.r.s32(0x4AB30)
        if op in (3, 69, 39, 40): want = d0 != -1            # the all-behind, face-away and slot-1 exits come before the write
        elif op in (26, 49): want = not (d0 == 0 and not lt)  # the early-out / face-away exit returns 0 with the codes clear
        elif op in (74, 75): want = cam < -0xC0               # the height gate skips the item before the write
        elif op == 23: want = False                           # never written
        else: want = True
        u.n_check('pen', 'op %d: V:4AA0C written %s' % (op, where.get(op, 'at entry (before any exit)')), written == want, (op, d0, lt, cam, written))
        return res
    u.call = pw
    u.check = lambda *a, **k: None          # the return rules are prim_ops.py's business
    try:
        for op in ops:
            for _ in range(n):
                PO.run_op(u, op, rng)
    finally:
        u.call, u.check = pcall, pcheck
    # op 62: v, pen, S; the pen is written first whatever the vertex
    for _ in range(n):
        u.set_state(rng); vs = u.vertices(rng)
        u.r.w16(0x4AA0C, SENT[0x4AA0C])
        i = rng.randint(0, 23)
        pcall(62, [V(i), rng.randint(0, 15), rng.randint(1, 30000)])
        u.n_check('pen', 'op 62: V:4AA0C written at entry (before the disc test)', u.r.u16(0x4AA0C) != SENT[0x4AA0C], vs[i])


# ------------------------------------------------------------------------------------------------------------------------------------ op 26
def sec_op26(u, rng, n):
    for _ in range(n):
        u.state(rng)
        k = rng.randint(3, 7); base = rng.randint(0, 10)
        mode = rng.choice(('all', 'last_front', 'first_front', 'mixed', 'inside'))
        vs = [behind(rng) for _ in range(24)]
        if mode == 'last_front': vs[base + k] = front(rng)          # the (N+1)th slot in front: no early-out although N slots are behind
        elif mode == 'first_front': vs[base] = front(rng)
        elif mode == 'mixed':
            for i in range(24):
                if rng.random() < .5: vs[i] = front(rng)
        elif mode == 'inside':
            vs = [front(rng) for _ in range(24)]
        u.put(vs)
        d0, lt, out, calls, spr, st, a2 = u.xcall(26, [k, V(base)])
        early = all(vs[base + i][2] < 0 for i in range(k + 1))     # N + 1 slots
        early_n = all(vs[base + i][2] < 0 for i in range(k))       # the earlier (wrong) reading
        clip_called = any(c[0] == 10 for c in calls)
        u.n_check('op26', 'early-out iff every one of the N + 1 slots from v is behind (z < 0): no clipper call, returns 0 with the codes clear', clip_called == (not early) and ((d0 == 0 and not lt) if early else lt == 1), (k, base, mode, [vs[base + i][2] for i in range(k + 1)], calls, d0, lt))
        if early_n and not early:
            u.n_check('op26', 'N slots behind but the (N+1)th in front: NOT an early-out (the slot after the list counts)', clip_called, (k, base))
        if early:
            u.n_check('op26', 'early-out writes nothing: pen, mask, pattern, toggle keep their values', st == SENT, (st,))
        else:
            u.n_check('op26', 'no early-out: pen 7 written, solid defaults (mask 0xF, pattern 0xFFFF) restored after the draw, toggle untouched',
                      st[0x4AA0C] == 7 and st[0x4A79E] == 0xF and st[0x4A7A0] == 0xFFFF and st[0x4A7A2] == SENT[0x4A7A2], (st, out))
            fills = [o for o in out if o['id'] == 1]
            if fills:
                u.n_check('op26', 'the polygon reaches the filler under pen 7, mask 3, pattern 3', all(o['pen'] == 7 and o['mask'] == 3 and o['pat'] == 3 for o in fills), (out,))



# ------------------------------------------------------------------------------------------------------------------------------------ ops 27, 30
def sec_op27(u, rng, n):
    for op, want_state in ((27, (2, 0, 2, 0)), (30, (8, 8, 0, 0))):
        for _ in range(n):
            u.state(rng)
            base = rng.randint(0, 10)
            vs = [front(rng) for _ in range(24)]
            style = rng.choice(('abc_behind_d_front', 'abc_behind', 'one_front', 'inside', 'wrap'))
            for i in (base, base + 1, base + 2):
                vs[i] = behind(rng)
            if style == 'abc_behind_d_front':        # A.z - B.z + C.z > 0 with A, B, C behind: B far behind
                a, b, c = rng.randint(-50, -1), rng.randint(-3000, -200), rng.randint(-50, -1)
                vs[base] = (rng.randint(-99, 99), rng.randint(-99, 99), a); vs[base + 1] = (rng.randint(-99, 99), rng.randint(-99, 99), b); vs[base + 2] = (rng.randint(-99, 99), rng.randint(-99, 99), c)
            elif style == 'one_front': vs[base + rng.randint(0, 2)] = front(rng)
            elif style == 'inside':
                for i in (base, base + 1, base + 2): vs[i] = front(rng)
            elif style == 'wrap':                    # the fourth depth wraps as a word
                vs[base] = (1, 1, -32000); vs[base + 1] = (1, 1, 20000); vs[base + 2] = (1, 1, -30000)
            u.put(vs)
            pen = rng.randint(0, 15)
            d0, lt, out, calls, spr, st, a2 = u.xcall(op, [pen, V(base)])
            za, zb, zc = vs[base][2], vs[base + 1][2], vs[base + 2][2]
            z4 = (za - zb + zc) & 0xFFFF
            z4 = z4 - 0x10000 if z4 & 0x8000 else z4
            early = za < 0 and zb < 0 and zc < 0 and z4 < 0
            clip_called = any(c[0] == 11 for c in calls)
            early_abc = za < 0 and zb < 0 and zc < 0
            u.n_check('op27', 'op %d: early-out iff A, B, C and the derived fourth depth A - B + C (word arithmetic) are all behind: no clipper call, returns 0' % op, clip_called == (not early) and d0 == (1 if out and any(o['id'] == 1 for o in out) else 0), (op, [za, zb, zc, z4], calls, d0))
            if early_abc and not early:
                u.n_check('op27', 'op %d: A, B, C behind but the fourth depth not: no early-out (the clipper is called)' % op, clip_called, (za, zb, zc, z4))
            u.n_check('op27', 'op %d: pen and the stipple words (mask, pattern, toggle, fourth) %s written before the early-out and left as written' % (op, want_state),
                    st[0x4AA0C] == pen and (st[0x4A79E], st[0x4A7A0], st[0x4A7A2], st[0x4A7A4]) == want_state, (st, pen))


# ------------------------------------------------------------------------------------------------------------------------------------ op 6
def w16(x):
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def sec_op6(u, rng, n):
    import hatch_check as H
    # the clipped line routine is replaced here by the hatch_check stub that logs its 6 input words
    r = u.r
    r.poke(H.STUB_LINE, H.capture_stub(12, [0x5163E + 2 * i for i in range(6)]))
    saved33936 = r.peek(0x33936, 6)
    r.poke(0x33936, P.w(0x4EF9) + P.l(H.STUB_LINE))
    u.cap = 60000000
    ids = [3, 4, 5, 6]
    S = [tuple(rng.randint(-2000, 2000) for _ in range(3)) for _ in range(24)]
    u.put(S)
    for N in (-32768, -32767, -2, -1, 0, 1, 2, 3, 4, 257, 258, 300, 32767):
        r.poke(BUF, b"\xEE" * (0x1D2000 - BUF))
        r.poke(H.ARGS, b"".join(struct.pack(">H", w & 0xFFFF) for w in [N] + [V(i) for i in ids] + [9]) + bytes(32))
        for a, v in H.sentinel.items(): r.w16(a, v)
        r.w32(H.MASK, 0xFFFFFFFF); r.w16(H.IDX, 0); r.w16(TRAP, 0)
        P.clear_log(r)
        r.L.rig_set_max_instructions(u.cap)
        rc = r.run(u.orig[6], a2=H.ARGS, a6=H.FRAME)
        log = P.read_log(r)
        d0 = P.s16(r.reg('d0') & 0xFFFF)
        calls = [tuple(P.s16(x) for x in rec[2:8]) for rec in log if rec[0] == 8 and rec[1] == 12]
        mem = r.peek(BUF, 0x1D2000 - BUF)
        # a record is written when it holds a byte other than the 0xEE sentinel; the rung writes at 0x5163E touch bytes 2 to 13 of record 256 only, those are not looked at
        def written(i):
            rec = mem[24 * i:24 * i + 24]
            if i == 256: rec = rec[:2] + rec[14:]
            return rec.count(0xEE) != len(rec)
        recs = 0
        for i in range(len(mem) // 24):
            if written(i): recs = i + 1
        exp_rungs = 0 if N == 0 else max(w16(N - 1), 1)
        exp_recs = 0 if N == 0 else ((w16(N - 2) + 1) if w16(N - 2) >= 0 else (w16(N - 2) + 0x10000 + 1))
        trap = r.u16(TRAP)
        print('op 6  N=%6d: rungs %6d (rule %6d), buffer records %6d (rule %6d, %d bytes from 0x4FE3C), trap %d, return %d, run code %d'
              % (N, len(calls), exp_rungs, recs, exp_recs, 24 * recs, trap, d0, rc))
        u.n_check('op6', 'N = 0: divide by zero (exception 5), no record and no rung', (N != 0) or (trap == 1 and not calls and recs == 0), (N, trap, len(calls), recs))
        if N == 0: continue
        u.n_check('op6', 'rungs drawn = the signed word N-1 with a minimum of one; buffer records = (N-2 as an unsigned word) + 1, 24 bytes each from 0x4FE3C', len(calls) == exp_rungs and recs == exp_recs, (N, len(calls), exp_rungs, recs, exp_recs))
        exall = H.rungs(S, N, *ids)
        badi = [i for i, c in enumerate(calls) if ('line', c) != exall[i]]
        u.n_check('op6', 'every rung equals the formula (rung k joins A1 + k stepA to B1 + k stepB, Q4 longs, rounded) except, for N of 258 or more, rung 257 alone', len(calls) == len(exall) and badi == ([256] if len(calls) > 256 else []), (N, badi[:5]))
        if len(calls) > 256:
            print('      N=%d: rungs that differ from the formula: %s' % (N, badi))
    u.cap = 2000000
    r.poke(0x33936, saved33936)


# ------------------------------------------------------------------------------------------------------------------------------------ word negation
def sec_neg(u, rng, n):
    src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'line_model.py')).read()
    for a, b in (("w16(-y1)", "(-y1)"), ("w16(-y2)", "(-y2)"), ("w16(-x1)", "(-x1)"), ("w16(-x2)", "(-x2)"),
                 ("a = z if s == 1 else w16(-z)", "a = z if s == 1 else -z"), ("w16(-x) > z", "(-x) > z"), ("w16(-y) > z", "(-y) > z")):
        assert a in src
        src = src.replace(a, b)
    ns = {'__name__': 'line_model_int32', '__file__': 'line_model.py'}
    exec(compile(src, 'line_model_int32', 'exec'), ns)
    diff_models = 0
    for _ in range(n):
        u.state(rng)
        vs = []
        for _ in range(12):
            m = rng.random()
            if m < .5: vs.append(tuple(rng.choice((-32768, rng.randint(-32768, 32767), rng.randint(-3000, 3000))) for _ in range(3)))
            else: vs.append(tuple(rng.randint(-32768, 32767) for _ in range(3)))
        if rng.random() < .5: vs[1] = (-32768, vs[1][1], vs[1][2]) if rng.random() < .5 else (vs[1][0], -32768, vs[1][2])
        u.put(vs)
        i, j = 0, 1
        try:
            want = L.clip_line(vs[i], vs[j])
        except ZeroDivisionError:
            continue
        try:
            alt = ns['clip_line'](vs[i], vs[j])
        except ZeroDivisionError:
            alt = 'zero'
        d0, lt, out, calls, spr, st, a2 = u.xcall(2, [V(i), V(j), 3])
        got = out[0]['d'] if out else None
        u.n_check('neg', 'op 2 with a coordinate of -32768: the rig equals the word model (negation of -32768 is -32768)', (got == want) and d0 == (1 if want else 0), (vs[i], vs[j], got, want))
        if alt != want:
            diff_models += 1
            u.n_check('neg', 'where a model with a 32-bit negation differs from the word model, the rig follows the word model', got == want, (vs[i], vs[j], got, want, alt))
    print('neg: segments on which a 32-bit negation gives another result than the rig: %d of %d' % (diff_models, n))


# ------------------------------------------------------------------------------------------------------------------------------------ ops 52, 53
def sec_census(u, rng, n):
    """Which shipped models use ops 52 and 53, and what lies in the stream the skips jump over (the extractor's decode in assets/models, git-ignored)"""
    import glob
    root = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..'))
    files = sorted(glob.glob(os.path.join(root, 'assets', 'models', 'model_*.bin')))
    if not files:
        print('census: skipped (no assets/models)'); return
    sys.path.insert(0, os.path.join(root, 'tools', 'extract'))
    from extract.models import read_model
    users, shapes = [], 0
    for f in files:
        m = read_model(open(f, 'rb').read())
        for p in m.programs:
            items = m.items[p.item_first:p.item_first + p.item_count]
            for i, it in enumerate(items):
                if it.op not in (52, 53): continue
                users.append((m.id, it.op))
                if it.op == 52:
                    after = items[i + 1:i + 9]
                    skips = [m.operands[x.op_first + 2] for x in after[:6] if x.op == 53]
                    ok = [x.op for x in after] == [53] * 6 + [55, 13] and skips == [70, 56, 42, 28, 14, 0] and all(x.length == 14 for x in after[:7]) and it.length == 14
                    shapes += 1
                    u.n_check('census', 'every op 52 item is followed by six op 53 items (14 bytes each, skips 70, 56, 42, 28, 14, 0), the op 55 item and the op 13 item', ok, (m.id, [x.op for x in after], skips))
    ids = sorted({m for m, o in users})
    print('census: ops 52/53 are used by models %s (%d items; %d op 52 items)' % (ids, len(users), shapes))


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 200
    u = Unit(); rng = random.Random(seed)
    secs = argv[3:] or ['rows', 'pen', 'op26', 'op27', 'neg', 'op6', 'census']
    for s in secs:
        {'rows': sec_rows, 'pen': sec_pen, 'op26': sec_op26, 'op27': sec_op27, 'neg': sec_neg, 'op6': lambda u, rng, n: sec_op6(u, rng, n), 'census': sec_census}[s](u, rng, n)
    for key in sorted(u.n):
        print('%-5s %-170s %6d checks  %d differences' % (key[0], key[1][:170], u.n[key], u.bad[key]))
    bad = sum(u.bad.values())
    print('draw_rules_check: %d checks, %s' % (sum(u.n.values()), 'FAILED, %d differences' % bad if bad else 'ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
