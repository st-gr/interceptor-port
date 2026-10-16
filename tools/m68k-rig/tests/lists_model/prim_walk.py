"""The primitive layer inside the walkers (docs/re/04a-display-lists.md section 8c, Verified by): rule 9 and the stream-end exits.

    python3 prim_walk.py [primitives|vertices|divergences|typeb|all] [SEED] [N]

primitives  A one-entry queue (any descriptor of the image: terrain, flat blocks, type B patches, bridges, landmarks), random camera
            height, level of detail, distance and position, is walked by the original walker.  Handler calls, stream-end exits and
            the drawer's tree walk are logged (primlog.py).  The stored result word (+0x14) is compared with the combination rule: the
            word OR of the handler results that did not return negative condition codes, forced to 0 by the 0xFFFF stream end of the
            plain drawer; in the type B drawer a negative handler word aborts with 0.
vertices    The camera-space vertex table 0x4D43C after the draw of a constructed flat or solid block against the model's vertex
            pipeline visibility.Draw.terrain_vertices (first vertices, the flat second section of flag bit 2, strips of flag bit 3).
Needs the user's git-ignored inputs of common.py (the emulator log is not read); skips with exit 0 when one is missing.
"""
import collections, random, struct, sys
from common import Rig, IMGPATH, LIBPATH, load_scene
import walk_check as W
import primlog as P
import visibility as vis
import os

TYPE_B = (0x22CFC, 0x22D0A)


def items_of(r):
    r.reset()
    out = []
    for i in range(20, 128):                    # items 17-19 (models 33-35) are drawn through the secondary-object table, never from the queues
        a = 0x2600C + 20 * i
        if r.u32(a + 4) > 0x10000 and r.u32(a) in (0x21C40, 0x21C34, 0x21D0C, 0x22CFC, 0x22D0A):
            out.append(i)
    return out


def construct(r, rng, items, tier=None, item=None, lod=None, dist=None, h=None, spread=None, extra=0):
    """random state with a one-entry queue; returns (entry address, tier, item, lod)"""
    W.random_frame(r, rng, camr=300)
    h = h if h is not None else (rng.randint(0x400, 0x14000) if rng.random() < .75 else rng.randint(0x14000, 0x300000))
    r.w32(0x4AB1E, -h); r.w32(0x4AB30, (-h) >> 8)
    tier = tier or rng.choice('ABC')
    ents = [e for e in W.window(r, tier) if not r.u8(e + 1) & 0x50]
    if not ents:
        return None
    e = ents[0]
    it = item if item is not None else rng.choice(items)
    lod = lod if lod is not None else rng.randint(0, 6)
    sp = spread or rng.choice((100, 400, 1500))
    r.w16(e, (it << 8) | (r.u8(e + 1) & 0xF0) | lod); r.w32(e + 2, 0x2600C + 20 * it)
    cx = struct.unpack('>h', r.peek(0x4AB2A, 2))[0]; cz = struct.unpack('>h', r.peek(0x4AB2E, 2))[0]
    r.w16(e + 6, ((-cx) >> lod) + rng.randint(-sp, sp)); r.w16(e + 8, 0); r.w16(e + 10, ((-cz) >> lod) + rng.randint(-sp, sp))
    r.w16(e + 0x10, dist if dist is not None else 0); r.w16(e + 0x14, 0); r.w8(e + 0x12, 0); r.w16(e + 24, 0xFFFF)
    r.w8(0x4A974, 3); r.w16(0x4A992, rng.randint(0, 255))
    return e, tier, it, lod


def combine(r, e, log):
    """the stored result the combination rule predicts, or None when the drawer did not reach its tree walk"""
    typeb = r.u32(r.u32(e + 2)) in TYPE_B
    kinds = [rec for rec in log if rec[0] == 6]
    start = [k for k in kinds if k[1] in (1, 4)]
    if not start:
        return None, 'no tree walk'
    calls = [(P.s16(rec[2]), ((rec[3] >> 3) & 1) ^ ((rec[3] >> 1) & 1), rec[1]) for rec in log if rec[0] == 3]
    if typeb:
        if any(k[1] == 3 for k in kinds):
            return -1, 'type B abort'
        word = 0
        for d0, lt, op in calls: word |= d0 & 0xFFFF
    else:
        if any(k[1] == 2 for k in kinds):
            return -1, '0xFFFF stream end'
        word = 0
        for d0, lt, op in calls:
            if not lt: word |= d0 & 0xFFFF
    word = P.s16(word)
    return (word if word > 0 else -1), ('type B' if typeb else 'plain')


def check_primitives(r, seed=1, n=1500):
    rng = random.Random(seed)
    r.L.rig_set_max_instructions(3000000)
    items = items_of(r)
    bad = 0; shown = 0
    cnt = collections.Counter(); opseen = collections.Counter(); oppos = collections.Counter(); abandon = collections.Counter()
    for _ in range(n):
        c = construct(r, rng, items, dist=rng.choice((None, None, 300, 914, 2500)))
        if c is None: continue
        e, tier, it, lod = c
        P.install(r, exits=True)
        P.clear_log(r); rc = W.walk(r, tier); log = P.read_log(r)
        if rc != 0:
            # the last detail entry of the type B models 39-42, 99, 100 names itself as the next entry (threshold 0x7FFF): a scaled
            # distance above 0x7FFF >> LOD never ends the search (an endless loop in the original; unreachable in play, 04a section 8c)
            ok = r.u32(r.u32(e + 2)) in TYPE_B and r.s16(e + 0x10) > (0x7FFF >> lod)
            cnt['endless loop of the type B level search (distance above 0x7FFF >> LOD)' if ok else 'TIMEOUT'] += 1
            bad += 0 if ok else 1
            continue
        res = r.s16(e + 0x14)
        pred, why = combine(r, e, log)
        cnt['walked'] += 1
        for rec in log:
            if rec[0] == 3:
                opseen[rec[1]] += 1
                if P.s16(rec[2]) > 0 and not ((rec[3] >> 3) & 1) ^ ((rec[3] >> 1) & 1): oppos[rec[1]] += 1
                if ((rec[3] >> 3) & 1) ^ ((rec[3] >> 1) & 1): abandon[rec[1]] += 1
        if pred is None:
            cnt['rule 1-8 (no tree walk)'] += 1; continue
        cnt['tree walk reached: ' + why] += 1
        if any(k[0] == 6 and k[1] == 2 for k in log): cnt['  ended by the 0xFFFF stream end (result forced to 0)'] += 1
        if any(k[0] == 6 and k[1] == 3 for k in log): cnt['  type B drawer aborted by a negative handler word'] += 1
        if why == 'plain' and any(k[0] == 3 and ((k[3] >> 3) & 1) ^ ((k[3] >> 1) & 1) for k in log): cnt['  with a handler that returned negative condition codes'] += 1
        cnt['  of them positive' if pred > 0 else '  of them not positive'] += 1
        if pred != res:
            bad += 1
            if shown < 8:
                shown += 1; print('DIFF item', it, 'tier', tier, 'lod', lod, 'rig', res, 'combination rule', pred, why, [(x[1], P.s16(x[2])) for x in log if x[0] == 3])
    print('primitives: %d one-entry walks, %d differences' % (cnt['walked'], bad))
    for k in sorted(cnt):
        if k != 'walked': print('   %-44s %5d' % (k, cnt[k]))
    print('   handler calls by op (calls / positive and kept / negative condition codes):', ' '.join('%d:%d/%d/%d' % (o, opseen[o], oppos[o], abandon[o]) for o in sorted(opseen)))
    return bad


def check_vertices(r, seed=1, n=600):
    r.L.rig_set_max_instructions(3000000)
    """vertex pipeline of the model against the rig's 0x4D43C after the draw (entries that pass the first-vertex test)"""
    rng = random.Random(seed)
    items = [i for i in items_of(r) if r.u32(0x2600C + 20 * i) in (0x21C40, 0x21D0C)]
    bad = 0; cmpv = 0; walks = 0; kinds = collections.Counter()
    for _ in range(n):
        c = construct(r, rng, items, h=rng.randint(0x400, 0x20000), spread=rng.choice((100, 400)))
        if c is None: continue
        e, tier, it, lod = c
        m = W.snap(r)
        pred = vis.walk_queue(m, tier)
        W.walk(r, tier)
        for ea, res, d in pred:
            if ea != e or not getattr(d, 'verts', None) or d.exit in ('fv', 'nolevel', 'subst', 'cut34', 'pretest'): continue
            walks += 1
            flags = d.flags
            kinds['flat' if flags & 2 else 'solid'] += 1
            if flags & 4: kinds['second section'] += 1
            if flags & 8: kinds['strips'] += 1
            for slot, v in sorted(d.verts.items()):
                got = struct.unpack('>3h', r.peek(0x4D43C + 6 * slot, 6))
                cmpv += 1
                if got != tuple(v):
                    bad += 1
                    if bad < 6: print('VERTEX DIFF item', it, 'slot', slot, 'rig', got, 'model', v, 'flags', hex(flags))
                    break
    print('vertices: %d drawn blocks (%s), %d slots compared, %d differences' % (walks, dict(kinds), cmpv, bad))
    return bad


def typeb_chain(r, it):
    """the detail entries of a type B item: [(threshold, vertex block address)] (02 section 7: distance word, next entry long, vertex block long)"""
    a = r.u32(0x2600C + 20 * it + 4); out = []; seen = set()
    while a not in seen:                          # the last entry of models 39-42, 99, 100 names itself as the next one
        seen.add(a)
        thr = r.u16(a); nxt = r.u32(a + 2)
        out.append((thr, r.u32(a + 6)))
        if thr & 0x8000 or nxt & 0x80000000: break
        a = nxt
    return out


def check_typeb_levels(r, seed=1, n=400):
    """Rule 4B: the type B drawer takes the first detail entry whose threshold, shifted right by the LOD (unmasked), is at least the scaled distance
    (strictly greater is the rule of the plain drawer); no entry fits: the drawer returns 0 and no tree walk is reached.  The distance word is set on both
    sides of every threshold (a word with shifted value at or above 0x400 is not refreshed by the walker)."""
    rng = random.Random(seed); bad = 0; cnt = collections.Counter()
    r.L.rig_set_max_instructions(3000000)
    r.reset()
    tb = [i for i in range(20, 128) if r.u32(0x2600C + 20 * i) in TYPE_B and r.u32(0x2600C + 20 * i + 4) > 0x10000]
    done = 0
    for _ in range(40 * n):
        if done >= n: break
        it = rng.choice(tb); lod = rng.randint(0, 6)
        chain = typeb_chain(r, it)
        ths = [t for t, _ in chain if t < 0x7FFF]
        if not ths: continue
        thr = rng.choice(ths)
        d = (thr >> lod) + rng.choice((-2, -1, 0, 1, 2))
        if d < 0 or (d << lod) < 0x400: continue
        c = construct(r, rng, [], tier='B', item=it, lod=lod, dist=d, h=rng.randint(0x1000, 0x80000))
        if c is None: continue
        e = c[0]
        P.install(r, exits=True); P.clear_log(r); rc = W.walk(r, 'B'); log = P.read_log(r)
        if rc != 0: continue
        done += 1
        want = next((vb for t, vb in chain if (t >> lod) >= d), None)
        reached = any(x[0] == 6 and x[1] == 4 for x in log)
        got = r.u32(0x4AAEA) if reached else None
        cnt['fits (a detail entry chosen)' if want else 'no entry fits (returns 0)'] += 1
        cnt['at the threshold exactly (shifted threshold = distance)'] += (d == (thr >> lod))
        if got != want:
            bad += 1
            if bad < 6: print('DIFF typeB item', it, 'lod', lod, 'dist', d, 'chain', [(hex(t), hex(v)) for t, v in chain], 'got', got and hex(got), 'want', want and hex(want))
    print('typeb levels: %d sweeps, %d differences  %s' % (sum(cnt[k] for k in cnt if k.startswith(('fits', 'no entry'))), bad, dict(cnt)))
    return bad


def check_typeb_cutoffs(r, seed=1, n=200):
    """Rule 3: the type B routine 0x22CFC returns 0 when V:4AB1E is below -0x380000 (camera above 17,920 ft), 0x22D0A when it is below -0x800000
    (40,960 ft); at the limit and one unit either side, and a near entry that would otherwise be drawn."""
    rng = random.Random(seed); bad = 0; cnt = collections.Counter()
    r.L.rig_set_max_instructions(3000000)
    r.reset()
    cut = {0x22CFC: 0x380000, 0x22D0A: 0x800000}
    tb = [i for i in range(20, 128) if r.u32(0x2600C + 20 * i) in cut and r.u32(0x2600C + 20 * i + 4) > 0x10000]
    for _ in range(n):
        it = rng.choice(tb); lim = cut[r.u32(0x2600C + 20 * it)]
        h = lim + rng.choice((-0x1000, -2, -1, 0, 1, 2, 0x1000))
        c = construct(r, rng, [], tier='B', item=it, lod=0, dist=0, h=h, spread=rng.choice((20, 60)))
        if c is None: continue
        P.install(r, exits=True); P.clear_log(r); rc = W.walk(r, 'B'); log = P.read_log(r)
        reached = any(x[0] == 6 and x[1] == 4 for x in log)
        stored = r.s16(c[0] + 0x14)
        inside = (-h) >= -lim
        cnt['camera at or below the limit' if inside else 'camera above the limit'] += 1
        if not inside and (reached or stored > 0):
            bad += 1; print('DIFF type B cut-off', it, hex(h), reached, stored)
        cnt['drawn (tree walk reached) at or below the limit'] += (inside and reached)
    print('typeb cut-offs: %d walks, %d differences  %s' % (sum(cnt[k] for k in cnt if k.startswith('camera')), bad, dict(cnt)))
    return bad


def seg_meets_open_pyramid(p, q):
    """exact rational test: the segment p-q meets the open view pyramid (z > |x| and z > |y|); the clipped line of the original agrees on random segments"""
    from fractions import Fraction as F
    P0 = [F(c) for c in p]; Q = [F(c) for c in q]; d = [Q[i] - P0[i] for i in range(3)]
    t0, t1 = F(0), F(1)
    for a, b, c in ((-1, 0, 1), (1, 0, 1), (0, -1, 1), (0, 1, 1)):
        f0 = a * P0[0] + b * P0[1] + c * P0[2]; fd = a * d[0] + b * d[1] + c * d[2]
        if fd == 0:
            if f0 <= 0: return False
        else:
            t = -f0 / fd
            if fd > 0: t0 = max(t0, t)
            else: t1 = min(t1, t)
        if t0 >= t1: return False
    return True


def check_divergences(r, seed=1, n=2500):
    """The two first divergences of the code round (task-10-report.md, Code fix round 1), explained by the rig:
    (1) tier C, item 45 (model 61), LOD 1, distance word 914: the level is the last-resort leaf of model 61 whose only item is op 48, the line routine that does
        not clip (0x3385C): it counts only when both end points lie inside the closed view pyramid; a port that clips the line (as op 2 does) gives +1 for
        lines that cross the pyramid boundary while the original gives -1.
    (2) tier A, item 30 (model 46, flat block with flag bit 2), LOD 3, distance word 401: the program is the line list of op 13 over the slots 0, 2, 4, ... 14;
        slots 2 and above exist only in the second vertex section of the block, so without that section no line can be judged; with the section (the model's
        vertex pipeline, verified in `vertices`) the sign follows the line rule (segment meets the open pyramid)."""
    import prim_ops
    r.L.rig_set_max_instructions(3000000)
    rng = random.Random(seed); bad = 0
    orig = P.op_table(r)
    cnt = collections.Counter()
    for _ in range(n):
        c = construct(r, rng, [], tier='C', item=45, lod=1, dist=914, h=rng.randint(0x1000, 0x80000), spread=rng.choice((100, 400, 1500)))
        if c is None: continue
        e, tier, it, lod = c
        P.install(r, exits=True, pre=True); P.clear_log(r); W.walk(r, tier); log = P.read_log(r)
        cnt['(1) walks'] += 1
        pre = [x for x in log if x[0] == 7 and x[3] == 48]
        if not pre: continue
        a2 = (pre[0][1] << 16) | pre[0][2]
        v1, v2 = r.u16(a2), r.u16(a2 + 2)
        res = r.s16(e + 0x14)
        p1 = struct.unpack('>3h', r.peek(0x4D43C + v1, 6)); p2 = struct.unpack('>3h', r.peek(0x4D43C + v2, 6))
        inb = lambda p: p[2] > 0 and abs(p[0]) <= p[2] and abs(p[1]) <= p[2]
        want_whole = inb(p1) and inb(p2)
        cnt['(1) op 48 reached'] += 1
        if (res > 0) != want_whole:
            bad += 1; print('DIFF (1)', p1, p2, res)
        cnt['(1) op 48 positive in the rig'] += res > 0
        clipped = seg_meets_open_pyramid(p1, p2)
        cnt['(1) a clipped line would count but op 48 does not (the port would give +1, the original -1)'] += (clipped and res <= 0)
        cnt['(1) op 48 counts and the clipped line does not'] += (res > 0 and not clipped)
    rng = random.Random(seed + 1)
    for _ in range(n):
        c = construct(r, rng, [], tier='A', item=30, lod=3, dist=401, h=rng.randint(0x400, 0x20000), spread=rng.choice((100, 400)))
        if c is None: continue
        e, tier, it, lod = c
        m = W.snap(r); pred = vis.walk_queue(m, tier); P.install(r, exits=True, pre=True); P.clear_log(r); W.walk(r, tier); log = P.read_log(r)
        res = r.s16(e + 0x14)
        d = [x for ea, rr, x in pred if ea == e]
        if not d or not getattr(d[0], 'verts', None) or d[0].exit in ('fv', 'nolevel', 'subst', 'cut34', 'pretest', 'flatcull'): continue
        d = d[0]
        cnt['(2) walks that reach the program'] += 1
        lines13 = [x for x in log if x[0] == 7 and x[3] == 13]
        if not lines13: continue
        cnt['(2) reaches op 13'] += 1
        a2 = (lines13[0][1] << 16) | lines13[0][2]
        pen = r.u16(a2); k = 1; pairs = []
        while True:
            v1 = r.u16(a2 + 2 * k); v2 = r.u16(a2 + 2 * k + 2); k += 2
            pairs.append((v1, v2 & 0x7FFF))
            if v2 & 0x8000: break
        hit = any(seg_meets_open_pyramid(d.verts[a // 6], d.verts[b // 6]) for a, b in pairs if a // 6 in d.verts and b // 6 in d.verts)
        cnt['(2) slots used by the lines (2 and above are second-section slots)'] += 1 if max(max(pairs)) // 6 >= 2 else 0
        cnt['(2) rig positive'] += res > 0
        cnt['(2) model verts + line rule positive'] += hit
        if hit != (res > 0):
            bad += 1
            cnt['(2) differences'] += 1
            if bad < 6: print('DIFF (2)', 'item', it, 'rig', res, 'model + line rule', hit, pairs)
    print('divergences:')
    for k in sorted(cnt): print('   %-96s %6d' % (k, cnt[k]))
    return bad


def main(argv):
    mode = argv[1] if len(argv) > 1 else 'all'
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    r = Rig(); bad = 0
    seed = int(argv[2]) if len(argv) > 2 else 1
    n = int(argv[3]) if len(argv) > 3 else None
    if mode in ('primitives', 'all'): bad += check_primitives(r, seed, n or 1500)
    if mode in ('vertices', 'all'): bad += check_vertices(r, seed, n or 600)
    if mode in ('divergences', 'all'): bad += check_divergences(r, seed, n or 800)
    if mode in ('typeb', 'all'):
        bad += check_typeb_levels(r, seed, n or 400)
        bad += check_typeb_cutoffs(r, seed, n or 400)
    print('prim_walk:', 'FAILED, %d differences' % bad if bad else 'ok')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
