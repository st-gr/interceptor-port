"""The toggle word V:4A7A2 across objects (docs/re/03-3d-pipeline.md, "Fill state: what each operation writes and what it inherits").

    python3 typeb_toggle_check.py [SEED] [N]        (skips with exit 0 without extracted/interceptor.bin, the lists log or librig.so)

Rule under test: the entry of the common drawer (0x225F0) clears the toggle word and V:4A7A4; the entry of the type B drawer (0x22D1A) sets mask 0xF and
pattern 0xFFFF only, so the toggle word and V:4A7A4 of the previous object survive into a type B object.  Part 1: N one-entry walks of random items
(plain and type B) with sentinels in all four words before the walk; the words after the walk are compared with the rule applied to the handler calls the walk
made (the operations 19, 22, 30, 17, 18 write toggle 0, 20, 21, 23, 27 write 2, 26 and all the others leave it).  Part 2: each hatch model of type B
(models 36, 39-42, 99, 100, items 20, 23-26, 83, 84) is walked, then a second type B item and, separately, a plain item are walked with the toggle the hatch
object left: the type B walk keeps it unless an operation of its own writes it, the plain walk clears it.
Exit 1 on any difference."""
import collections, random, sys
from common import Rig, inputs_missing
import walk_check as W
import prim_walk as PW
import primlog as P

SENT = {0x4A79E: 0x2222, 0x4A7A0: 0x1111, 0x4A7A2: 0x0DEF, 0x4A7A4: 0x3333}
T0 = (19, 22, 30, 17, 18); T2 = (20, 21, 23, 27)


def state_stubs(r):
    """the output routines log (mask, pattern, toggle, pen) as kind 10 at the moment of the call (as stipple_check.py)"""
    a = 0x8E0000
    for pid, tgt in P.PRIMS.items():
        code = P.w(0x40E7, 0x2F08) + P.append_header(10, None, [(2, pid)] + [(4 + 2 * i, ("mem", m)) for i, m in enumerate((0x4A79E, 0x4A7A0, 0x4A7A2, 0x4AA0C))]) + P.w(0x205F, 0x44DF, 0x4E75)
        r.poke(a, code); r.poke(tgt, P.w(0x4EF9) + P.l(a)); a += (len(code) + 3) & ~3


def walk_one(r, rng, item, state, tier='C', dist=0):
    """state: words poked before the walk, or None to leave the words as the previous walk left them"""
    c = PW.construct(r, rng, [], tier=tier, item=item, lod=rng.randint(0, 2), dist=dist)
    if c is None: return None
    e = c[0]
    P.install(r, exits=True); state_stubs(r)
    for a, v in (state or {}).items(): r.w16(a, v)
    before = r.u16(0x4A7A2)
    P.clear_log(r); rc = W.walk(r, c[1]); log = P.read_log(r)
    if rc != 0 or not any(x[0] == 6 and x[1] in (1, 4) for x in log): return None
    typeb = r.u32(r.u32(e + 2)) in PW.TYPE_B
    ops = [x[1] for x in log if x[0] == 3]
    seen = [tuple(x[2:6]) for x in log if x[0] == 10]
    return typeb, ops, {a: r.u16(a) for a in SENT}, before, seen


def expect_toggle(start, ops):
    t = start
    for o in ops:
        if o in T0: t = 0
        elif o in T2: t = 2
    return t


def main(seed=1, n=300):
    if inputs_missing():
        print('skipped (missing: %s)' % ', '.join(inputs_missing())); return 0
    r = Rig(); r.reset(); r.L.rig_set_max_instructions(3000000)
    rng = random.Random(seed); items = PW.items_of(r)
    plain = [i for i in items if r.u32(0x2600C + 20 * i) not in PW.TYPE_B]
    cnt = collections.Counter(); bad = 0
    for _ in range(n * 3):
        if cnt['part 1 walks'] >= n: break
        it = rng.choice(items)
        res = walk_one(r, rng, it, SENT)
        if res is None: continue
        typeb, ops, after = res[:3]
        start = SENT[0x4A7A2] if typeb else 0
        ok = after[0x4A7A2] == expect_toggle(start, ops)
        if not any(o in (T0 + T2) for o in ops):
            cnt['type B, no toggle writer: toggle carried' if typeb else 'plain, no toggle writer: toggle cleared'] += 1
            ok = ok and after[0x4A7A2] == start and after[0x4A7A4] == (SENT[0x4A7A4] if typeb else 0)
        cnt['part 1 walks'] += 1; cnt['  type B' if typeb else '  plain'] += 1
        if not ok:
            bad += 1
            if bad < 6: print('DIFF item', it, 'type B' if typeb else 'plain', 'ops', sorted(set(ops)), {hex(a): hex(v) for a, v in after.items()})
    # part 2: a plain object that ends with toggle 2 (the models 47, 60, 66, 69-71, ... with op 20, 21, 23 or 27) then a type B object with op 26 (the models
    # 36, 39-42, 99, 100: items 20, 23-26, 83, 84), then a plain one.  The harness rebuilds the frame between two walks (which clears the words), so the toggle
    # the writer left is written back before the next walk, as the words of the object drawn before
    writers = [m - 16 for m in (47, 59, 60, 66, 69, 70, 71, 74, 75, 76, 77, 79, 82, 83, 84, 85, 93, 94, 96, 97)]
    op26 = [20, 23, 24, 25, 26, 83, 84]
    for w in writers:
        for _ in range(max(n // 5, 20)):
            r1 = walk_one(r, rng, w, {0x4A7A2: 0, 0x4A7A4: 0}, tier=rng.choice('ABC'), dist=rng.choice((0, 300, 914)))
            if r1 is None or r1[0] or not any(o in T2 for o in r1[1]): continue
            t1 = r1[2][0x4A7A2]
            if t1 != 2: cnt['walks that ran an op 20/21/23/27 but ended with toggle 0 (a later op 19 or 22): not used'] += 1; continue
            cnt['writer walks ending with toggle 2 (plain object, last toggle op 20, 21, 23 or 27)'] += 1
            if t1 != expect_toggle(0, r1[1]): bad += 1; print('DIFF writer item', w, 'toggle', hex(t1), sorted(set(r1[1])))
            it2 = rng.choice(op26)
            r2 = None
            for _try in range(8):
                r2 = walk_one(r, rng, it2, {0x4A7A2: t1, 0x4A7A4: 0x3333}, tier=rng.choice('ABC'), dist=rng.choice((0, 300, 914)))
                if r2 is not None and r2[0] and any(t[:2] == (3, 3) for t in r2[4]): break
            if r2 is None or not r2[0]: continue
            cnt['  then a type B op-26 object walked'] += 1
            seen26 = [t for t in r2[4] if t[:2] == (3, 3)]       # filler calls with mask 3 and pattern 3: the op 26 polygons
            cnt['    op 26 filler calls seen (mask 3, pattern 3)'] += len(seen26)
            ok = r2[3] == t1 and all(t[2] == t1 for t in seen26) and r2[2][0x4A7A2] == expect_toggle(t1, r2[1])
            cnt['    toggle at the type B entry equal to the writer toggle (%d) and seen by op 26' % t1] += ok
            if not ok: bad += 1; print('DIFF writer', w, 'then type B', it2, 'toggle before', hex(r2[3]), 'wrote', hex(t1), 'seen', seen26[:3])
            r3 = walk_one(r, rng, rng.choice(plain), {0x4A7A2: t1}, tier=rng.choice('ABC'), dist=rng.choice((0, 300, 914)))
            if r3 is not None and not r3[0]:
                cnt['  then a plain object: its own walk ends with the toggle'] += 1
                if r3[2][0x4A7A2] != expect_toggle(0, r3[1]): bad += 1; print('DIFF plain after', r3[1], hex(r3[2][0x4A7A2]))
    for k in sorted(cnt): print('   %-72s %5d' % (k, cnt[k]))
    print('typeb_toggle_check: %s' % ('FAILED, %d differences' % bad if bad else 'ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(int(sys.argv[1]) if len(sys.argv) > 1 else 1, int(sys.argv[2]) if len(sys.argv) > 2 else 300))
