"""The second vertex section of flat blocks with flag bit 2 (docs/re/03-3d-pipeline.md, "Block set-up of the terrain models"), checked on constructed blocks.

    python3 sec2_check.py [SEED] [N]        (skips with exit 0 without extracted/interceptor.bin or librig.so)

Item 30 (model 46) is drawn by the original walker with its block replaced, in a copy of the model at 0x780000 (the descriptor's base pointers are
redirected; every offset of the model is relative to its base).  Each case is run twice in the same constructed state:
  reference  the same explicit points as b8 plain vertices (flag bit 2 set, the section a lone negative count word): the rig's transformed point T[i] of
             every explicit point i of the case, in the order the block stores them;
  case       the block under test: b8 plain vertices followed by the section.
The slots the case must have written are computed from T alone by the rules stated in the note and compared with the rig's table 0x4D43C:
  * the section starts at slot b8 (no header word), and is skipped when b8 is 0 or 1 (the plain loop then still transforms exactly one vertex);
  * a group is a count word n (negative: the section ends) and then a do-while: n = 0 yields one pair like n = 1, n yields n pairs; each pair is two slots,
    the point and the point plus the vector D = (last slot written) - (slot before it), taken once at the start of the group, word by word;
  * a word after the pairs, only its sign tested (negative: the section ends; zero and positive: any value), then two points, one slot each;
  * the next group begins at the next block word; its D comes from the last two slots written (the two single points when present).
Slots above the last written one keep the sentinel the table was filled with."""
import collections, os, random, struct, sys
from common import Rig, IMGPATH, LIBPATH
import walk_check as W
import prim_walk as PW

ITEM = 30
NEW = 0x780000
TAB = 0x4D43C
SENT = 0x5A5A
_G = {}


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def words(*v):
    return b"".join(struct.pack(">h", s16(x)) for x in v)


def state(r, seed):
    rng = random.Random(seed)
    c = PW.construct(r, rng, [], tier='A', item=ITEM, lod=3, dist=401, h=rng.randint(0x4000, 0x20000), spread=100)
    return c


def block_offset():
    """offset of the drawn block from the model base, found by one unmodified walk"""
    if 'off' not in _G:
        r = Rig(); r.reset(); r.L.rig_set_max_instructions(3000000)
        rng = random.Random(3)
        for seed in range(1, 40):
            c = state(r, seed)
            if c is None: continue
            W.walk(r, 'A')
            blk = r.u32(0x4AAEA)
            base = r.u32(0x2600C + 20 * ITEM + 4)
            if base < blk < base + 0x400:
                _G['off'] = blk - base; _G['hdr'] = r.peek(blk, 10); break
    return _G['off'], _G['hdr']


def run_block(r, seed, b8, body):
    """walk the constructed state with the block (b8, body bytes after the header); returns the slot words list (0x5A5A = untouched) or None when not drawn"""
    off, hdr = block_offset()
    c = state(r, seed)
    if c is None: return None
    desc = 0x2600C + 20 * ITEM
    base = r.u32(desc + 4)
    r.poke(NEW, r.peek(base, off))
    h = bytearray(hdr); h[8] = b8 & 0xFF; h[7] = 0x06; h[9] = 0
    r.poke(NEW + off, bytes(h) + body + bytes(40))
    for k in (4, 8, 12): r.w32(desc + k, NEW)
    r.poke(TAB, struct.pack(">H", SENT) * 300)
    W.walk(r, 'A')
    if r.u32(0x4AAEA) != NEW + off: return None
    return [r.s16(TAB + 2 * i) if r.u16(TAB + 2 * i) != SENT else None for i in range(300)]


def make_case(rng):
    b8 = rng.choice((0, 1, 2, 2, 2, 3, 4))
    nplain = max(b8, 1)
    pts = lambda n: [(rng.randint(-300, 300), rng.randint(-300, 300)) for _ in range(n)]
    plain = pts(nplain)
    groups = []
    for _ in range(rng.choice((0, 1, 1, 2, 3))):
        cnt = rng.choice((0, 1, 2, 3, 5))
        post = rng.choice((-1, -32768, 0, 1, 7, 0x100, 0x7FFF, 0, 1))
        groups.append(dict(cnt=cnt, pairs=pts(max(cnt, 1)), post=post, singles=pts(2) if post >= 0 else []))
    end = rng.choice(('neg', 'neg', 'cut'))        # how the last group list ends: a negative count word, or the post word negative
    return b8, plain, groups, end


def encode(b8, plain, groups, end):
    body = b"".join(words(x, z) for x, z in plain)
    for g in groups:
        body += words(g['cnt']) + b"".join(words(x, z) for x, z in g['pairs']) + words(g['post'])
        if g['post'] >= 0: body += b"".join(words(x, z) for x, z in g['singles'])
        if g['post'] < 0: break
    else:
        body += words(-1)
    return body


def explicit(b8, plain, groups):
    pts = list(plain)
    for g in groups:
        pts += g['pairs']
        if g['post'] < 0: break
        pts += g['singles']
    return pts


def expected(T, b8, plain, groups):
    S = list(T[:len(plain)])
    if b8 <= 1: return S
    i = len(plain)
    for g in groups:
        D = tuple(s16(S[-1][k] - S[-2][k]) for k in range(3))
        for _ in range(max(g['cnt'], 1)):
            v = T[i]; i += 1
            S += [v, tuple(s16(v[k] + D[k]) for k in range(3))]
        if g['post'] < 0: break
        S += [T[i], T[i + 1]]; i += 2
    return S


def check(seed=1, n=300):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print('skipped (missing: extracted/interceptor.bin or librig.so)'); return 0
    block_offset()
    r = Rig(); r.reset(); r.L.rig_set_max_instructions(3000000)
    rng = random.Random(seed); bad = 0; cnt = collections.Counter()
    done = 0; tries = 0
    while done < n and tries < 40 * n:
        tries += 1
        b8, plain, groups, end = make_case(rng)
        pts = explicit(b8, plain, groups)
        if len(pts) < 2 or len(pts) > 60: continue
        st = rng.randint(1, 1 << 30)
        ref = run_block(r, st, len(pts), b"".join(words(x, z) for x, z in pts) + words(-1))
        if ref is None or ref[0] is None or ref[3 * (len(pts) - 1)] is None:
            cnt['reference not drawn (first-vertex test or level)'] += 1; continue
        T = [tuple(ref[3 * i:3 * i + 3]) for i in range(len(pts))]
        got = run_block(r, st, b8, encode(b8, plain, groups, end))
        if got is None or got[0] is None:
            cnt['case not drawn'] += 1; continue
        done += 1
        S = expected(T, b8, plain, groups)
        want = [c for v in S for c in v]
        want += [None] * (300 - len(want))
        cnt['cases'] += 1
        cnt['b8 <= 1 with a section present (skipped)'] += (b8 <= 1 and len(groups) > 0)
        cnt['groups with count 0 (b8 >= 2)'] += sum(1 for g in groups if g['cnt'] == 0 and b8 >= 2)
        cnt['groups with a non-negative word after the pairs (b8 >= 2)'] += sum(1 for g in groups if g['post'] >= 0 and b8 >= 2)
        cnt['groups in cases with b8 >= 2'] += len(groups) if b8 >= 2 else 0
        cnt['cases with two or more groups'] += (b8 >= 2 and len(groups) >= 2)
        if got != want:
            bad += 1
            if bad < 6: print('DIFF b8', b8, 'groups', groups, '\n rig ', got[:3 * len(S)], '\n want', want[:3 * len(S)])
    print('sec2: %d constructed blocks (%d tries), %d differences' % (cnt['cases'], tries, bad))
    for k in sorted(cnt):
        if k != 'cases': print('   %-60s %5d' % (k, cnt[k]))
    return bad


if __name__ == '__main__':
    sys.exit(1 if check(int(sys.argv[1]) if len(sys.argv) > 1 else 1, int(sys.argv[2]) if len(sys.argv) > 2 else 300) else 0)
