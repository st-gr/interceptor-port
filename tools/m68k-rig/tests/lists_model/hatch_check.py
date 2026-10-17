"""Hatch and stripe family (ops 19, 20, 32, 21, 57, 22, 23) and line ops 6, 12, 14: the geometry each handler hands to the clipped line
routine 0x33936 or the polygon clipper 0x28B6C, the fill-mode words it writes, and which calls count in its return
(docs/re/03-3d-pipeline.md, "Hatch and stripe geometry").

    python3 hatch_check.py [SEED] [N] [op ...]

The two clipper entries are replaced by stubs that log their arguments (the 6 words at 0x5163e, or the 12 words at 0x51040) and return
1 or 0 by a mask over the call index, so the check compares the exact vertex lists (word arithmetic, wrapping) and the exact set of calls that
count.  The mode words 4AA0C, 4A79E, 4A7A0, 4A7A2 are preset to sentinels to see which the handler writes.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
import primlog as P

SLOTS, ARGS, FRAME = 0x4D43C, 0x7E0000, 0x7F0200
MASK, IDX, TRAP = 0x8FF100, 0x8FF104, 0x8FF110
STUB_TRAP = 0x8F0200
STUB_LINE, STUB_POLY = 0x8F0000, 0x8F0100
sentinel = {0x4AA0C: 0x1234, 0x4A79E: 0x5678, 0x4A7A0: 0x9ABC, 0x4A7A2: 0x0DEF}


def w16(x):
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def h(x):
    return w16(x) >> 1


def vsub(a, b): return tuple(w16(p - q) for p, q in zip(a, b))
def vadd(a, b): return tuple(w16(p + q) for p, q in zip(a, b))
def vh(a): return tuple(h(p) for p in a)


def capture_stub(kind_id, srcs):
    recs = b""
    chunks = [srcs[i:i + 6] for i in range(0, len(srcs), 6)]
    c = b""
    for n, ch in enumerate(chunks):
        c += P.w(0x40E7, 0x2F08)
        c += P.append_header(8, None, [(2, kind_id + n)] + [(4 + 2 * i, ("mem", a)) for i, a in enumerate(ch)])
        c += P.w(0x205F, 0x44DF)
    c += P.w(0x2F01) + P.w(0x2039) + P.l(MASK) + P.w(0x3239) + P.l(IDX) + P.w(0x5279) + P.l(IDX)
    c += P.w(0xE2A8) + P.w(0x0280) + P.l(1) + P.w(0x221F, 0x4E75)
    return c


class Unit:
    def __init__(self):
        self.r = Rig(); self.r.reset()
        self.orig = P.op_table(self.r)
        P.install(self.r, ops=False)
        self.r.poke(STUB_LINE, capture_stub(12, [0x5163E + 2 * i for i in range(6)]))
        self.r.poke(STUB_POLY, capture_stub(20, [0x51040 + 2 * i for i in range(12)]))
        self.r.poke(0x33936, P.w(0x4EF9) + P.l(STUB_LINE))
        self.r.poke(0x28B6C, P.w(0x4EF9) + P.l(STUB_POLY))
        # processor exception 5 (divide by zero): vector at 0x14 -> a stub that sets TRAP and returns to the rig's sentinel
        self.r.poke(STUB_TRAP, P.w(0x33FC, 0x0001) + P.l(TRAP) + P.w(0x4EF9) + P.l(0x100))
        self.r.poke(0x14, P.l(STUB_TRAP))
        self.n = {}; self.bad = {}; self.shown = 0

    def run(self, op, words, slots, mask):
        r = self.r
        r.poke(SLOTS, b"".join(struct.pack(">3h", *v) for v in slots))
        r.poke(ARGS, b"".join(struct.pack(">H", w & 0xFFFF) for w in words) + bytes(32))
        for a, v in sentinel.items(): r.w16(a, v)
        r.w16(0x5103C, 0x7777); r.w16(0x5103E, 0x7777)
        r.w32(MASK, mask); r.w16(IDX, 0); r.w16(TRAP, 0)
        P.clear_log(r)
        r.L.rig_set_max_instructions(2000000)
        r.run(self.orig[op], a2=ARGS, a6=FRAME)
        log = P.read_log(r)
        d0 = P.s16(r.reg('d0') & 0xFFFF)
        recs = [rec for rec in log if rec[0] == 8]
        calls, i = [], 0
        while i < len(recs):
            if recs[i][1] == 12:
                calls.append(('line', tuple(P.s16(x) for x in recs[i][2:8]))); i += 1
            else:
                v = [P.s16(x) for x in recs[i][2:8]] + [P.s16(x) for x in recs[i + 1][2:8]]
                calls.append(('poly', tuple(tuple(v[3 * k:3 * k + 3]) for k in range(4)))); i += 2
        modes = {a: r.u16(a) for a in sentinel}
        return d0, calls, modes, r.reg('a2') - ARGS, (r.u16(0x5103C), r.u16(0x5103E))

    def check(self, op, name, ok, info):
        self.n[(op, name)] = self.n.get((op, name), 0) + 1
        if not ok:
            self.bad[(op, name)] = self.bad.get((op, name), 0) + 1
            if self.shown < 12:
                self.shown += 1; print('DIFF op', op, name, info)


def slots_for(rng):
    big = rng.random() < 0.5
    lim = 32767 if big else 1500
    return [tuple(rng.randint(-lim, lim) for _ in range(3)) for _ in range(24)]


EDGE, CUR_OP = {}, [0]      # calls per op that used a count word of 0 or less


def cnt(rng, lo, hi):
    """a count word: mostly lo..hi (the shipped range), one time in four an edge value the data never uses (0, negative, the most negative word)"""
    if rng.random() < 0.25:
        EDGE[CUR_OP[0]] = EDGE.get(CUR_OP[0], 0) + 1
        return rng.choice((0, 0, -1, -2, -7, -32768))
    return rng.randint(lo, hi)


def accum(q, n):
    """the stroke offsets h(acc_k), acc_0 = 0, acc_{k+1} = acc_k + q as words; n do-while iterations (at least one)"""
    out, acc = [], (0, 0, 0)
    for k in range(max(n, 1)):
        out.append(vh(acc)); acc = vadd(acc, q)
    return out


def line_ops(S, op, n):
    """19: exact strokes; returns list of ('line', 6 words)"""
    a, b, c = S[0], S[1], S[2]
    p, q = vsub(a, c), vsub(b, c); hq = vh(q)
    out = []
    for off in accum(q, n):
        p0 = vadd(c, off); out.append(('line', p0 + vadd(p0, p)))
    sc = vadd(vsub(c, hq), p)
    for off in accum(q, n):
        p0 = vsub(sc, off); out.append(('line', p0 + vsub(p0, p)))
    return out


def strips(S, n, with_second=True):
    a, b, c, d = S[0], S[1], S[2], S[3]
    p, q = vsub(a, c), vsub(b, c); hq = vh(q)
    sc, sd = vadd(vsub(c, hq), p), vadd(vsub(d, hq), p)
    out = []
    for off in accum(q, n):
        out.append(('poly', (vadd(c, off), vadd(vadd(c, off), p), vadd(vadd(d, off), p), vadd(d, off))))
    for off in accum(q, n):
        out.append(('poly', (vsub(sc, off), vsub(vsub(sc, off), p), vsub(vsub(sd, off), p), vsub(sd, off))))
    return out


def stripes(S, rows):
    """ops 21, 57: rows = list of N2 per row"""
    p0, q0, r0, s3 = vsub(S[0], S[3]), vsub(S[1], S[3]), vsub(S[2], S[3]), S[3]
    out = []
    for i, n2 in enumerate(rows):
        base = S[3 + i]
        for off in accum(q0, n2):
            b = vadd(base, off); d = vadd(vadd(base, r0), off)
            out.append(('poly', (b, vadd(b, p0), vadd(d, p0), d)))
    return out


def rowlines(S, rows):
    """op 22"""
    step, vec = vsub(S[0], S[2]), vsub(S[1], S[2])
    out = []
    for i, n2 in enumerate(rows):
        base = S[2 + i]
        for off in accum(step, n2):
            b = vadd(base, off); out.append(('line', b + vadd(vadd(base, vec), off)))
    return out


def steps(S, n):
    """op 23"""
    p, q, r = vsub(S[0], S[3]), vsub(S[1], S[3]), vsub(S[2], S[3])
    t3 = tuple(w16(w16(c) + w16(2 * c)) for c in q)
    t = vadd(p, vh(t3))
    out = []
    for i in range(max(n, 1)):
        b = S[3 + i]; d = vadd(b, r)
        out.append(('poly', (b, vadd(b, t), vadd(d, t), d)))
    return out


def rungs(S, n, a1, a2, b1, b2):
    """op 6: points in Q4 longs; step = truncated 16-bit quotient (low word of the long difference when the divide overflows); the rungs
    k = 1 .. N-1 for N of 2 or more, ONE rung (k = 1) for N of 1 and for N negative (N = 0 is the divide-by-zero trap, handled by the caller)"""
    def step(p1, p2):
        out = []
        for u, v in zip(p1, p2):
            d = (v << 4) - (u << 4)
            q = abs(d) // abs(n) * (1 if (d >= 0) == (n > 0) else -1)
            if not -32768 <= q <= 32767: q = w16(d)
            out.append(q)
        return out
    sa, sb = step(S[a1], S[a2]), step(S[b1], S[b2])
    res = []
    def rnd(x):                       # (x + 8) shifted right 4 arithmetically: half rounds up
        return w16((x >> 4) + ((x >> 3) & 1))
    for k in range(1, max(w16(n - 1), 1) + 1):
        pa = [(S[a1][i] << 4) + k * sa[i] for i in range(3)]
        pb = [(S[b1][i] << 4) + k * sb[i] for i in range(3)]
        res.append(('line', tuple(rnd(x) for x in pa) + tuple(rnd(x) for x in pb)))
    return res


def expect_return(calls_exp, mask, counted):
    """counted(i): whether call i counts; the return is the OR of the counted calls' results"""
    res = [(mask >> i) & 1 for i in range(len(calls_exp))]
    return 1 if any(res[i] for i in range(len(res)) if counted(i)) else 0


def one(u, op, rng):
    R = rng.randint
    S = slots_for(rng)
    pen = R(0, 15); v = 6 * R(0, 6)
    mask = rng.getrandbits(32) if rng.random() < 0.5 else (1 << R(0, 30))
    SS = S[v // 6:]
    ck = u.check
    if op == 19:
        n = cnt(rng, 1, 5); d0, calls, modes, a2, shp = u.run(op, [pen, v, n], S, mask)
        exp = line_ops(SS, op, n)
        ck(op, 'strokes (first N: C + h(k Q) to A + h(k Q); second N: A - h(Q) - h(k Q) to C - h(Q) - h(k Q)), return = OR of all 2N', calls == exp and d0 == expect_return(exp, mask, lambda i: True) and a2 == 6,
           (n, calls[:2], exp[:2], d0))
        ck(op, 'modes: pen = operand, mask 0xF, pattern 0xFFFF, toggle 0', modes == {0x4AA0C: pen, 0x4A79E: 0xF, 0x4A7A0: 0xFFFF, 0x4A7A2: 0}, modes)
    elif op in (20, 32):
        n = cnt(rng, 1, 5); words = ([pen] if op == 32 else []) + [v, n]
        d0, calls, modes, a2, shp = u.run(op, words, S, mask)
        exp = strips(SS, n)
        ck(op, 'strips (4 vertices, shift word 0, count 4), return = OR of the first N only', calls == exp and shp == (0, 4) and d0 == expect_return(exp, mask, lambda i: i < max(n, 1)) and a2 == 2 * len(words), (n, calls[:1], exp[:1], d0, shp))
        if op == 20:
            ck(op, 'modes: pen 13, mask 2, pattern 0, toggle 2', modes == {0x4AA0C: 13, 0x4A79E: 2, 0x4A7A0: 0, 0x4A7A2: 2}, modes)
        else:
            ck(op, 'modes: pen = operand, mask 0xF, pattern 0xFFFF, toggle untouched', modes == {0x4AA0C: pen, 0x4A79E: 0xF, 0x4A7A0: 0xFFFF, 0x4A7A2: sentinel[0x4A7A2]}, modes)
    elif op in (21, 57):
        n1 = cnt(rng, 1, 4); rows = [cnt(rng, 1, 4) for _ in range(max(n1, 1))]
        words = ([pen] if op == 57 else []) + [v, n1] + rows
        d0, calls, modes, a2, shp = u.run(op, words, S, mask)
        exp = stripes(SS, rows)
        ck(op, 'stripes: row i from slot 3+i, stripe j shifted by h(j Q), Q = S1 - S3; return = OR of all', calls == exp and shp == (0, 4) and d0 == expect_return(exp, mask, lambda i: True) and a2 == 2 * len(words), (rows, calls[:1], exp[:1], d0))
        if op == 21:
            ck(op, 'modes: pen 13, mask 2, pattern 0, toggle 2', modes == {0x4AA0C: 13, 0x4A79E: 2, 0x4A7A0: 0, 0x4A7A2: 2}, modes)
        else:
            ck(op, 'modes: pen = operand, mask 0xF, pattern 0xFFFF, toggle untouched', modes == {0x4AA0C: pen, 0x4A79E: 0xF, 0x4A7A0: 0xFFFF, 0x4A7A2: sentinel[0x4A7A2]}, modes)
    elif op == 22:
        n1 = cnt(rng, 1, 4); rows = [cnt(rng, 1, 4) for _ in range(max(n1, 1))]
        d0, calls, modes, a2, shp = u.run(op, [pen, v, n1] + rows, S, mask)
        exp = rowlines(SS, rows)
        ck(op, 'row strokes: row i from slot 2+i, stroke j shifted by h(j (S0 - S2)), vector S1 - S2; return = OR of all', calls == exp and d0 == expect_return(exp, mask, lambda i: True) and a2 == 2 * (3 + len(rows)), (rows, calls[:1], exp[:1], d0))
        ck(op, 'modes: pen = operand, mask 0xF, pattern 0xFFFF, toggle 0', modes == {0x4AA0C: pen, 0x4A79E: 0xF, 0x4A7A0: 0xFFFF, 0x4A7A2: 0}, modes)
    elif op == 23:
        n = cnt(rng, 1, 5); d0, calls, modes, a2, shp = u.run(op, [v, n], S, mask)
        exp = steps(SS, n)
        ck(op, 'strips: slot 3+i, width vector (S0 - S3) + h(3 (S1 - S3)), length S2 - S3; return = OR of all', calls == exp and shp == (0, 4) and d0 == expect_return(exp, mask, lambda i: True) and a2 == 4, (n, calls[:1], exp[:1], d0))
        ck(op, 'modes: pen untouched, mask 2, pattern 0, toggle 2', modes == {0x4AA0C: sentinel[0x4AA0C], 0x4A79E: 2, 0x4A7A0: 0, 0x4A7A2: 2}, modes)
    elif op == 6:
        n = rng.choice((1, -1, -2, -7, 0)) if rng.random() < 0.25 else R(2, 7)
        EDGE[op] = EDGE.get(op, 0) + (n < 2)
        ids = [R(0, 20) for _ in range(4)]
        S2 = slots_for(rng)
        if rng.random() < 0.7: S2 = [tuple(R(-2000, 2000) for _ in range(3)) for _ in range(24)]
        d0, calls, modes, a2, shp = u.run(op, [n] + [6 * i for i in ids] + [pen], S2, mask)
        if n == 0:
            ck(op, 'N = 0: the divide by zero raises processor exception 5 (vector 0x14) before any rung is drawn (no stroke, A2 not advanced past the divide)', u.r.u16(TRAP) == 1 and calls == [], (calls, u.r.u16(TRAP)))
        elif n >= 2:
            exp = rungs(S2, n, *ids)
            ck(op, 'rungs k = 1..N-1 between A1 + k (A2 - A1)/N and B1 + k (B2 - B1)/N (Q4 longs, truncated divide, rounded back)', calls == exp and d0 == expect_return(exp, mask, lambda i: True) and modes[0x4AA0C] == pen, (n, ids, calls[:1], exp[:1]))
        else:
            exp = rungs(S2, n, *ids)
            ck(op, 'N of 1 or negative: exactly one rung (k = 1) with the step difference/N (a do-while, N-1 not positive)', calls == exp and len(exp) == 1 and d0 == expect_return(exp, mask, lambda i: True) and a2 == 12, (n, ids, calls[:1], exp[:1]))
    elif op in (11, 12, 14):
        k = R(1, 4)
        if op == 11:
            n = 0 if rng.random() < 0.25 else k; v1 = 6 * R(0, 12); EDGE[op] = EDGE.get(op, 0) + (n == 0)
            d0, calls, modes, a2, shp = u.run(op, [(n << 8) | pen, v1], S, mask)
            exp = [('line', S[v1 // 6 + 2 * i] + S[v1 // 6 + 2 * i + 1]) for i in range(max(n, 1))]
            ck(op, 'segments slot s to s+1 from v, two slots per segment, count = bits 8-15 (0 still draws one), return = OR, pen = bits 0-5', calls == exp and d0 == expect_return(exp, mask, lambda i: True) and a2 == 4, (n, calls[:1], exp[:1], d0))
            return
        if op == 12:
            vr, v1 = 6 * R(0, 18), 6 * R(0, 12)
            n = 0 if rng.random() < 0.25 else k; EDGE[op] = EDGE.get(op, 0) + (n == 0); d0, calls, modes, a2, shp = u.run(op, [(n << 8) | pen, vr, v1], S, mask)
            dl = vsub(S[vr // 6 + 1], S[vr // 6])
            exp = [('line', vsub(S[v1 // 6 + 2 * i], dl) + vsub(S[v1 // 6 + 2 * i + 1], dl)) for i in range(max(n, 1))]
        else:
            vr = 6 * R(0, 18); pairs = [(6 * R(0, 22), 6 * R(0, 22)) for _ in range(k)]
            words = [pen, vr]
            for i, (a, b) in enumerate(pairs): words += [a, b | (0x8000 if i == k - 1 else 0)]
            d0, calls, modes, a2, shp = u.run(op, words, S, mask)
            dl = vsub(S[vr // 6 + 1], S[vr // 6])
            exp = [('line', vsub(S[a // 6], dl) + vsub(S[b // 6], dl)) for a, b in pairs]
        ck(op, 'every line shifted by MINUS (V[vref+1] - V[vref]); return = OR', calls == exp and d0 == expect_return(exp, mask, lambda i: True), (calls[:1], exp[:1]))
    else:
        raise KeyError(op)


OPS = [19, 20, 32, 21, 57, 22, 23, 6, 11, 12, 14]


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 300
    ops = [int(a) for a in argv[3:]] or OPS
    u = Unit(); rng = random.Random(seed)
    for op in ops:
        CUR_OP[0] = op
        for _ in range(n): one(u, op, rng)
    for key in sorted(u.n):
        print('op %2d  %-120s %5d calls  %d differences' % (key[0], key[1], u.n[key], u.bad.get(key, 0)))
    print('count words of 0 or less (N, N1, N2; ops 11 and 12: N = 0), draws counted per op:', {o: EDGE.get(o, 0) for o in ops})
    bad = sum(u.bad.values())
    print('hatch_check: %d checks, %s' % (sum(u.n.values()), 'FAILED, %d differences' % bad if bad else 'ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
