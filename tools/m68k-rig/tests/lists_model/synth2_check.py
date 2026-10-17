"""Vertex synthesis ops 38, 46, 45, 47, 56, 63, 64, 65 (no operand): the slots each writes, against the rig.

    python3 synth2_check.py [SEED] [N]

For each operation the handler (table 0x23408) is run directly on random 16-bit slot arrays at 0x4D43C.  The expected words are the word-exact expressions that the
symbolic executor symx.py derives from the straight-line fragments (ops 38 and 46 share 0x2551C, ops 45 and 47 share 0x2560C; ops 56, 63, 64 and 65 run their fragment
first on a slot array inside the object record, +0xA4 of the viewed entry, then on the model array).  Every word of the array outside the written slots must be unchanged;
D0 = 0 with the Z flag set.  Prints the number of arrays per operation; exit 1 on any difference.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
import symx

TABLE = 0x23408
FRAG = {38: (0x25522, 0x25608), 46: (0x25522, 0x25608), 45: (0x25612, 0x256B6), 47: (0x25612, 0x256B6), 56: (0x25CF8, 0x25DD2), 63: (0x256D4, 0x2574C),
        64: (0x25DF0, 0x25E7C), 65: (0x25E98, 0x25EC2)}


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 200
    rng = random.Random(seed); r = Rig(); r.reset()
    ARR = 0x4D43C; NW = 3 * 160
    tot = {}; bad = {}
    for op, (a, b) in FRAG.items():
        m = symx.M(); symx.run(a, b, m)
        written = sorted(m.mem)
        handler = r.u32(TABLE + 4 * op)
        tot[op] = bad[op] = 0
        for _ in range(n):
            vals = [rng.randint(-3000, 3000) if rng.random() < .7 else rng.randint(-32768, 32767) for _ in range(NW)]
            r.poke(ARR, struct.pack('>%dh' % NW, *vals))
            r.poke(0x700000, bytes(64)); r.w16(0x4AA6E, 0)
            r.run(handler, a2=0x700000, a6=0x710000)
            after = struct.unpack('>%dh' % NW, r.peek(ARR, 2 * NW))
            env = {'s%d.%d' % (k, ax): vals[3 * k + ax] for k in range(NW // 3) for ax in range(3)}
            want = list(vals)
            for off, e in m.mem.items(): want[off // 2] = symx.ev(e, env)
            ok = list(after) == want and (r.reg('d0') & 0xFFFF) == 0 and (r.L.rig_get_reg(17) >> 2) & 1 == 1
            tot[op] += 1
            if not ok:
                bad[op] += 1
                if bad[op] < 3: print('DIFF op', op, [(i, after[i], want[i]) for i in range(NW) if after[i] != want[i]][:5])
        print('op %2d: %3d slots written (b+%d .. b+%d), %d random arrays, %d differences' % (op, len({o // 6 for o in written}), min(o // 6 for o in written), max(o // 6 for o in written), tot[op], bad[op]))
    # ops with operands: 34, 35, 36 (parallelepiped completions), 52 and 53 (control-surface and gear clusters), against references written from the stated formulas
    def w(x): x &= 0xFFFF; return x - 0x10000 if x & 0x8000 else x
    def shl(x, s): return w(w(x) << s) if s >= 0 else w(x) >> (-s)
    for op in (34, 35, 36, 52, 53):
        handler = r.u32(TABLE + 4 * op); tot[op] = bad[op] = 0
        for _ in range(n):
            vals = [rng.randint(-3000, 3000) if rng.random() < .7 else rng.randint(-32768, 32767) for _ in range(NW)]
            r.poke(ARR, struct.pack('>%dh' % NW, *vals)); r.w16(0x4AA6E, 0)
            v = 6 * rng.randint(0, 3); s = rng.randint(-4, 5); want = list(vals); S = lambda k, a, b=v // 6: vals[3 * (b + k) + a]
            if op in (34, 35, 36):
                words = [v, s] if op != 35 else [v]
                for a in range(3):
                    def put(k, val, a=a, b=v // 6): want[3 * (b + k) + a] = w(val)
                    if op in (34, 35):
                        put(4, S(0, a) - S(1, a) + S(2, a)); put(5, S(0, a) - S(1, a) + S(3, a)); put(6, S(3, a) + S(2, a) - S(1, a)); put(7, S(0, a) - 2 * S(1, a) + S(2, a) + S(3, a))
                    if op == 34:
                        d = shl(S(2, a) - S(3, a), s); put(8, S(3, a) - d); put(9, S(3, a) - d - S(1, a) + S(2, a))
                    if op == 36:
                        put(6, S(0, a) - S(1, a) + S(2, a)); put(7, S(0, a) - S(1, a) + S(3, a)); put(8, S(0, a) - S(1, a) + S(4, a)); put(9, S(0, a) - S(1, a) + S(5, a))
                        put(10, S(2, a) + S(5, a) - S(1, a)); put(11, S(0, a) - 2 * S(1, a) + S(2, a) + S(5, a))
                        d = shl(S(3, a) - S(4, a), s); put(12, S(4, a) - d); put(13, S(4, a) - d - S(2, a) + S(3, a))
            elif op == 52:
                ia, ib, it = rng.randint(0, 20), rng.randint(0, 20), rng.randint(0, 20); words = [6 * ia, 6 * ib, 6 * it]
                for a in range(3):
                    d = w(vals[3 * ib + a] - vals[3 * ia + a]); h = d >> 1
                    want[3 * (it + 3) + a] = w(vals[3 * it + a] + d); want[3 * (it + 4) + a] = w(vals[3 * (it + 1) + a] + h); want[3 * (it + 5) + a] = w(vals[3 * (it + 2) + a] + h)
            else:
                ia, ib = rng.randint(0, 6), rng.randint(0, 6); words = [6 * ia, 6 * ib, 0]
                for a in range(3):
                    for j in range(3):
                        d = w(vals[3 * (ib + j) + a] - vals[3 * ia + a])
                        for k in range(1, 6): want[3 * (ia + 30 + 5 * j + k - 1) + a] = w(vals[3 * (ia + k) + a] + d)
            r.poke(0x700000, struct.pack('>%dh' % len(words), *words) + bytes(64)); r.w8(0x4B23C + 0x7C, 0)
            r.run(handler, a2=0x700000, a6=0x710000)
            after = struct.unpack('>%dh' % NW, r.peek(ARR, 2 * NW))
            ok = list(after) == want and (r.reg('d0') & 0xFFFF) == 0
            tot[op] += 1
            if not ok:
                bad[op] += 1
                if bad[op] < 3: print('DIFF op', op, [(i, after[i], want[i]) for i in range(NW) if after[i] != want[i]][:5])
        print('op %2d: operands, %d random arrays, %d differences' % (op, tot[op], bad[op]))
    return 1 if any(bad.values()) else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
