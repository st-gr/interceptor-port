"""The vertex-synthesis tables of docs/re/03-3d-pipeline.md ("Vertex synthesis") and of its fixed-formula rows, transcribed as written and run against the rig.

    python3 synth_note_check.py [SEED] [N]

Ops 41, 42, 43, 44, 59 (operand: slot base b and, for 41, the shift s) are run on random 16-bit slot arrays; the expected arrays apply the formulas of the note table
word by word (h = arithmetic shift right 1, q = right 2, every sum a 16-bit word).  Every word of the array must equal the expectation (slots the table does not name stay
unchanged), so the table is complete and exact for the extractor.  Prints the number of arrays per op; exit 1 on any difference.
The operand-free rows (ops 38 and 46, 45 and 47, 56, 63, 64, 65) are not retyped: `parse_note_rows` reads the table "| op | new slots |" out of the note
itself and evaluates its text ("11 = S0 + S1 - S2; ...", with M = h(S1 + S2), h and q the halve and quarter, "3 (b+7)" the product with the new slot 7), so a
slip in the note's text fails the check.  The state skips of ops 52 and 53 are run with every value of the record byte +0x7C and with random skip operands.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
"""
import os, random, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH

TABLE = 0x23408


def w(x):
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def h(x): return w(x) >> 1
def q(x): return w(x) >> 2


NOTE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'docs', 're', '03-3d-pipeline.md')
TOK = re.compile(r'\s*(\(b\+7\)|[hq]\(|S\d+|M|\d+|[()+\-])')


def parse_note_rows():
    """{op: [(new slot, expression text)]} from the table that follows 'Sk is slot k, M = h(S1 + S2)' in the note (rows keyed by their op cell)"""
    lines = open(NOTE).read().split('\n')
    j = next(k for k, l in enumerate(lines) if l.startswith('**Fixed-formula synthesis without an operand'))
    i = next(k for k, l in enumerate(lines) if k > j and l.startswith('| op | new slots |'))
    rows = {}
    for l in lines[i + 2:]:
        if not l.startswith('|'): break
        cells = [c.strip() for c in l.strip().strip('|').split('|')]
        ops = [int(x) for x in re.findall(r'\d+', re.sub(r'\(`[^)]*`\)', '', cells[0]))]
        items = []
        for part in cells[1].split(';'):
            slot, expr = part.split('=', 1)
            expr = re.sub(r',.*$', '', expr)                       # "..., with b+7 the new slot 7"
            items.append((int(slot), expr.strip()))
        for o in ops: rows[o] = items
    return rows


def eval_expr(text, S, new):
    """sum of products over words: S[k] the old slot, M = h(S1 + S2), (b+7) the new slot 7; implicit product 'n X'; every operation wraps to a word"""
    toks = TOK.findall(text); pos = [0]
    assert ''.join(t for t in toks).replace(' ', '') == text.replace(' ', ''), text
    def peek(): return toks[pos[0]] if pos[0] < len(toks) else None
    def take(): pos[0] += 1; return toks[pos[0] - 1]
    def factor():
        t = take()
        if t == '(': v = expr(); assert take() == ')'; return v
        if t in ('h(', 'q('):
            v = expr(); assert take() == ')'; return h(v) if t == 'h(' else q(v)
        if t == 'M': return h(S[1] + S[2])
        if t == '(b+7)': return new[7]
        if t[0] == 'S': return S[int(t[1:])]
        return int(t)
    def term():
        v = factor()
        while peek() is not None and peek() not in ('+', '-', ')'): v = w(v * factor())
        return v
    def expr():
        v = term()
        while peek() in ('+', '-'):
            o = take(); t = term(); v = w(v + t if o == '+' else v - t)
        return v
    v = expr(); assert pos[0] == len(toks), text
    return v


def op41(V, s, new):
    d = w(w(V[2] - V[3]) << s) if s >= 0 else w(V[2] - V[3]) >> (-s)
    new[9] = V[3] + V[2] - V[1]; new[10] = V[3] - d; new[11] = V[5] - V[6] + V[7]; new[12] = new[11] + V[8] - V[6]
    new[13] = V[5] + (V[4] - V[6]); new[14] = V[8] + (V[4] - V[6]); new[15] = V[7] + (V[4] - V[6])
    new[16] = new[11] + (V[4] - V[6]); new[17] = new[12] + (V[4] - V[6])


def op42(V, s, new):
    # b+100 reads b+10, b+13, b+14 written by op 43 earlier: they are the array's values here (untouched by this op)
    new[17] = V[3] + V[4] - V[8]; new[18] = V[2] - V[1] + V[6]; new[19] = h(V[6] - V[1]) + V[7]; new[20] = h(V[6] - V[1]) + V[16]
    new[21] = h(V[6] - V[1]) + h(V[3] + V[4]) + 2 * (V[6] - V[1]); new[22] = h(w(new[21]) + V[0])
    new[23] = V[4] + V[5] - V[8]; new[24] = h(V[8] + V[11]); new[25] = V[4] + V[5] - V[3]; new[26] = V[6] + V[9] - V[1]; new[27] = 2 * V[6] - V[1]
    new[28] = V[2] - 2 * V[1] + 2 * V[6]; new[29] = h(V[6] - V[1]) + V[0]
    new[100] = h(h(V[13] + V[14]) + V[10])


def op43(V, s, new):
    new[10] = V[3] + V[4] - V[6]; new[11] = h(V[3] + V[4]) + q(V[5] - V[3]); new[12] = h(w(new[11]) + V[0])
    new[13] = V[7] + q(V[9] - V[7]) + h(q(V[9] - V[7])); new[14] = V[4] + V[5] - V[3]; new[15] = V[3] + V[4] - V[7]


def op44(V, s, new):
    new[5] = V[0] + h(V[1] - V[0]); new[6] = V[0] + h(h(V[1] - V[0]))


def op59(V, s, new):
    t = h(V[3] + V[4])
    new[21] = V[6] - V[7] + V[10]; new[22] = t; new[23] = 2 * t - V[9]; new[24] = 2 * t - V[8]; new[25] = V[4] + V[5] - V[3]
    a = h(V[4] - V[3] + 2 * V[5])
    new[26] = h(a + V[5]); new[27] = h(a + V[4] - V[3] + V[5]); new[28] = h(4 * t - 2 * V[3] + 2 * V[5] - V[8] - V[9]); new[29] = h(V[1] + V[2])
    new[30] = V[6] - V[7] + V[12]; new[31] = V[6] - V[7] + V[14]
    u = h(V[6] - V[7])
    new[32] = u + V[1] + V[6] - V[7]; new[33] = u + V[2] + V[6] - V[7]; new[34] = u + V[11] + 2 * (V[6] - V[7]); new[35] = u + V[13] + 2 * (V[6] - V[7])
    new[36] = u + V[1] + 2 * (V[6] - V[7]); new[37] = u + V[2] + 2 * (V[6] - V[7]); new[38] = 2 * t - V[3] + V[5] - V[9]; new[39] = 2 * t - V[3] + V[5] - V[8]
    new[110] = h(h(V[18] + V[19]) + V[16])


MODEL = {41: op41, 42: op42, 43: op43, 44: op44, 59: op59}


def row_check(r, rng, n, rows):
    NW = 3 * 160; ARR = 0x4D43C; res = {}
    for ops in ((38, 46), (45, 47), (56,), (63,), (64,), (65,)):
        for op in ops:
            bad = 0
            for _ in range(n):
                vals = [rng.randint(-3000, 3000) if rng.random() < .7 else rng.randint(-32768, 32767) for _ in range(NW)]
                r.poke(ARR, struct.pack('>%dh' % NW, *vals)); r.poke(0x700000, bytes(64)); r.w16(0x4AA6E, 0)
                r.run(r.u32(TABLE + 4 * op), a2=0x700000, a6=0x710000)
                after = struct.unpack('>%dh' % NW, r.peek(ARR, 2 * NW)); want = list(vals)
                for a in range(3):
                    S = {k: vals[3 * k + a] for k in range(NW // 3)}; new = {}
                    for slot, text in rows[op]:
                        new[slot] = eval_expr(text, S, new)
                    for slot, v in new.items(): want[3 * slot + a] = w(v)
                if list(after) != want:
                    bad += 1
                    if bad < 3: print('DIFF op', op, [(i, after[i], want[i]) for i in range(NW) if after[i] != want[i]][:4])
            res[op] = bad
            print('op %d: operand-free row of the note, %d slots, %d random arrays, %d differences' % (op, len(rows[op]), n, bad))
    return res


def skip_check(r, rng, n):
    """ops 52 and 53: the stream advance. Op 52 skips 14 bytes times ((+0x7C with bit 7 cleared) >> 4); op 53 skips its third operand (a signed word of bytes);
    both after their three operand words.  Arrays are not compared here (synth2_check does)."""
    res = {}
    for op in (52, 53):
        bad = zero = 0
        for k in range(n):
            b7c = k % 256 if k < 256 else rng.randint(0, 255)
            skip = 0 if rng.random() < 0.2 else rng.choice((rng.randint(-60, 60), rng.randint(-32768, 32767)))
            words = [6 * rng.randint(0, 3), 6 * rng.randint(0, 3), 6 * rng.randint(0, 3) if op == 52 else skip]
            r.poke(0x4D43C, bytes(2000)); r.w16(0x4AA6E, 0)
            r.poke(0x700000, struct.pack('>3h', *words) + bytes(64)); r.w8(0x4B23C + 0x7C, b7c)
            r.run(r.u32(TABLE + 4 * op), a2=0x700000, a6=0x710000)
            adv = (r.reg('a2') - 0x700000)
            adv = adv - (1 << 32) if adv >= 1 << 31 else adv
            want = 6 + (14 * ((b7c & 0x7F) >> 4) if op == 52 else words[2])
            zero += (want == 6)
            if adv != want:
                bad += 1
                if bad < 3: print('DIFF skip op', op, hex(b7c), words, adv, want)
        res[op] = bad
        print('op %d: stream advance, %d runs (%d with no skip), %d differences' % (op, n, zero, bad))
    return res


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 200
    rng = random.Random(seed); r = Rig(); r.reset(); bad = {}
    NS = 130; ARR = 0x4D43C
    for op, fn in MODEL.items():
        bad[op] = 0
        for _ in range(n):
            vals = [rng.randint(-3000, 3000) if rng.random() < .7 else rng.randint(-32768, 32767) for _ in range(3 * NS)]
            b = rng.randint(0, 3); s = rng.randint(-4, 5)
            r.poke(ARR, struct.pack('>%dh' % (3 * NS), *vals)); r.w16(0x4AA6E, 0)
            words = [6 * b, s] if op == 41 else [6 * b]
            r.poke(0x700000, struct.pack('>%dh' % len(words), *words) + bytes(32))
            r.run(r.u32(TABLE + 4 * op), a2=0x700000, a6=0x710000)
            after = struct.unpack('>%dh' % (3 * NS), r.peek(ARR, 6 * NS))
            want = list(vals); ok = True
            for a in range(3):
                V = {k: vals[3 * (b + k) + a] for k in range(0, 115)} if b + 114 < NS else {k: vals[3 * (b + k) + a] for k in range(NS - b)}
                new = {}
                fn(V, s, new)
                for k, v in new.items():
                    if b + k < NS: want[3 * (b + k) + a] = w(v)
            if list(after) != want:
                ok = False; bad[op] += 1
                if bad[op] < 3: print('DIFF op', op, 'b', b, 's', s, [(i, after[i], want[i]) for i in range(3 * NS) if after[i] != want[i]][:4])
        print('op %d: %d random arrays, %d differences' % (op, n, bad[op]))
    rows = parse_note_rows()
    assert set(rows) >= {38, 46, 45, 47, 56, 63, 64, 65}, sorted(rows)
    bad.update({('row', k): v for k, v in row_check(r, rng, n, rows).items()})
    bad.update({('skip', k): v for k, v in skip_check(r, rng, max(n, 300)).items()})
    return 1 if any(bad.values()) else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
