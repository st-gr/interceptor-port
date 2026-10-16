"""Persistence of the fill state between the operations of one object (docs/re/03-3d-pipeline.md, "Fill state and its persistence").

    python3 stipple_check.py [SEED] [N]

Sequences of N handler calls (random operations from the table below, random operands, random camera-space vertices) are run one after the
other in the rig, with no reset between them, as the items of one object.  The output routines (filler, line routine, the three point
sprites) are replaced by stubs that log the words V:4A79E (plane mask), V:4A7A0 (pattern), V:4A7A2 (toggle) and V:4AA0C (pen colour) at the moment of
the call.  The expectation is a model of the stated rule: the operations of the table write the stated words, every other operation leaves them, and
the output sees the state left by the earlier operation.  Prints the number of sequences and output calls; exit 1 on any difference.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
import primlog as P

SLOTS, ARGS, FRAME = 0x4D43C, 0x7E0000, 0x7F0200
STUBS = 0x8E0000
MODE = (0x4A79E, 0x4A7A0, 0x4A7A2)
# operation -> words written (mask, pattern, toggle); None leaves the word.  A write of the pair (mask, pattern) leaves the toggle word.
WRITES = {2: (0xFFFF, 0xFFFF, None), 48: (0xFFFF, 0xFFFF, None), 74: (0xFFFF, 0xFFFF, None), 13: (0xFFFF, 0xFFFF, None), 75: (0xFFFF, 0xFFFF, None),
          19: (0xF, 0xFFFF, 0), 22: (0xF, 0xFFFF, 0), 32: (0xF, 0xFFFF, None), 57: (0xF, 0xFFFF, None),
          20: (2, 0, 2), 21: (2, 0, 2), 23: (2, 0, 2), 27: (2, 0, 2), 30: (8, 8, 0), 17: (8, 8, 0)}
UNTOUCHED = (1, 3, 6, 9, 10, 11, 12, 14, 15, 16, 24, 25, 39, 40, 49, 58, 62, 69, 7, 8)
OPS = list(WRITES) + [26] + list(UNTOUCHED)


def out_stub(pid):
    c = P.w(0x40E7, 0x2F08)
    c += P.append_header(10, None, [(2, pid)] + [(4 + 2 * i, ("mem", a)) for i, a in enumerate(MODE + (0x4AA0C,))])
    c += P.w(0x205F, 0x44DF, 0x4E75)
    return c


class Unit:
    def __init__(self):
        self.r = Rig(); self.r.reset()
        self.orig = P.op_table(self.r)
        P.install(self.r, ops=False)
        a = STUBS
        for pid, tgt in P.PRIMS.items():
            code = out_stub(pid); self.r.poke(a, code); self.r.poke(tgt, P.w(0x4EF9) + P.l(a)); a += (len(code) + 3) & ~3

    def call(self, op, words):
        r = self.r
        r.poke(ARGS, b"".join(struct.pack(">H", w & 0xFFFF) for w in words) + bytes(32))
        P.clear_log(r)
        r.L.rig_set_max_instructions(2000000)
        r.run(self.orig[op], a2=ARGS, a6=FRAME)
        outs = [rec[1:6] for rec in P.read_log(r) if rec[0] == 10]
        return outs, tuple(r.u16(a) for a in MODE)


def operands(rng, op, pen):
    R = rng.randint
    V = lambda i: 6 * i
    if op in (1, 58): return [V(R(0, 23)), pen]
    if op == 62: return [V(R(0, 23)), pen, R(1, 400)]
    if op == 49: return [V(R(0, 20)), V(R(0, 20)), V(R(0, 20)), rng.choice((0x180, 0x189, 0x80, 0))]
    if op in (2, 48, 74): return [V(R(0, 23)), V(R(0, 23)), pen]
    if op in (13, 75):
        k = R(1, 3); w = [pen]
        for i in range(k): w += [V(R(0, 23)), V(R(0, 23)) | (0x8000 if i == k - 1 else 0)]
        return w
    if op == 3: return [V(R(0, 20)), V(R(0, 20)), V(R(0, 20)) | 0x8000, 0x8000 | pen]
    if op == 69: return [V(R(0, 20)), V(R(0, 20)), V(R(0, 20)), 0x8000 | pen]
    if op in (24,): return [3, V(R(0, 20)), V(R(0, 20)), V(R(0, 20)), pen]
    if op == 25: return [3, pen]
    if op == 26: return [3, V(R(0, 20))]
    if op in (19,): return [pen, V(R(0, 10)), R(1, 2)]
    if op == 22: return [pen, V(R(0, 10)), 1, R(1, 2)]
    if op in (20,): return [V(R(0, 10)), R(1, 2)]
    if op == 32: return [pen, V(R(0, 10)), R(1, 2)]
    if op == 21: return [V(R(0, 10)), 1, R(1, 2)]
    if op == 57: return [pen, V(R(0, 10)), 1, R(1, 2)]
    if op == 23: return [V(R(0, 10)), R(1, 2)]
    if op in (27, 30, 9, 10): return [pen, V(R(0, 20))]
    if op in (17,): return [V(R(0, 20)), V(R(0, 20)), V(R(0, 20)), V(R(0, 20)), 0xFFFF]
    if op in (11,): return [(R(1, 3) << 8) | pen, V(R(0, 10))]
    if op == 12: return [(R(1, 3) << 8) | pen, V(R(0, 10)), V(R(0, 10))]
    if op == 14: return [pen, V(R(0, 10)), V(R(0, 20)), V(R(0, 20)) | 0x8000]
    if op == 6: return [3, V(R(0, 20)), V(R(0, 20)), V(R(0, 20)), V(R(0, 20)), pen]
    if op in (15,): return [pen, V(R(0, 20)), V(R(0, 20)), V(R(0, 20)), V(R(0, 20))]
    if op == 16: return [pen, V(R(0, 20)), V(R(0, 20)), V(R(0, 20))]
    if op in (7, 8): return [pen] + [V(R(0, 20)) for _ in range(5 if op == 7 else 6)]
    if op == 39: return [R(0, 15), R(0, 15), R(0, 15)]
    if op == 40: return [R(0, 15), R(0, 15)]
    raise KeyError(op)


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    nseq = int(argv[2]) if len(argv) > 2 else 200
    u = Unit(); rng = random.Random(seed); bad = outs_n = calls = 0
    r = u.r
    for _ in range(nseq):
        # object start: the tree-walk entry's reset (mask 0xF, pattern solid, toggle 0), pen anything
        r.w16(0x4A79E, 0xF); r.w16(0x4A7A0, 0xFFFF); r.w16(0x4A7A2, 0)
        state = [0xF, 0xFFFF, 0]
        for step in range(rng.randint(3, 8)):
            op = rng.choice(OPS)
            r.w16(0x4AB70, rng.randint(0, 3)); r.w32(0x4AB30, -rng.choice((0x10, 0x60, 0x400, 0x2000)))
            r.w16(0x4ABE2, rng.randint(-300, 300)); r.w16(0x4ABE4, 0); r.w16(0x4ABE6, rng.randint(-300, 300))
            r.w16(0x4AB2A, rng.randint(-300, 300)); r.w16(0x4AB2E, rng.randint(-300, 300)); r.w16(0x4AA3C, 0x7FFF)
            r.w16(FRAME - 0x28, rng.randint(0, 600))
            z = lambda: rng.randint(20, 3000)
            vs = []
            for i in range(24):
                zz = z(); vs.append((rng.randint(-zz, zz) * 9 // 10, rng.randint(-zz, zz) * 9 // 10, zz))
            r.poke(SLOTS, b"".join(struct.pack(">3h", *v) for v in vs))
            # the height gates of 74, 75: the near state below the gate would skip the op; the model below follows the gate
            gate = op in (74, 75) and r.s32(0x4AB30) >= -0xC0
            pen = rng.randint(0, 15)
            before = tuple(state)
            outs, after = u.call(op, operands(rng, op, pen))
            calls += 1
            if op in WRITES and not gate:
                new = WRITES[op]
                for i in range(3):
                    if new[i] is not None: state[i] = new[i]
                seen = {tuple(o[1:4]) for o in outs}
                # the output routines of these operations see the state the operation itself wrote
                ok_out = all(s == tuple(state) for s in seen)
            elif op == 26:
                # drawn: the output sees mask 3, pattern 3 (toggle as held); afterwards mask 0xF, pattern solid; all behind: nothing written
                seen = {tuple(o[1:4]) for o in outs}
                if outs:
                    state[0], state[1] = 0xF, 0xFFFF
                    ok_out = all(s == (3, 3, before[2]) for s in seen)
                else:
                    ok_out = True
            else:
                seen = {tuple(o[1:4]) for o in outs}
                ok_out = all(s == before for s in seen)
            outs_n += len(outs)
            if not ok_out or after != tuple(state):
                bad += 1
                if bad < 8: print('DIFF op', op, 'before', before, 'after', after, 'expected', tuple(state), 'seen', sorted(seen))
                state = list(after)
    print('stipple_check: %d sequences, %d handler calls, %d output calls, %s' % (nseq, calls, outs_n, 'FAILED, %d differences' % bad if bad else 'ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
