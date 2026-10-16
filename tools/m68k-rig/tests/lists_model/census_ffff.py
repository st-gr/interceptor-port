"""Census of the streams that end on the word 0xFFFF (docs/re/04a-display-lists.md section 8c, "Combination"; docs/re/02-assets-and-tables.md section 3).

    python3 census_ffff.py        (reads only the user's git-ignored extracted/interceptor.bin; skips with exit 0 without it)

For every descriptor of the table 0x2600C and every model record of 0x25ECC (the entity records, whose descriptors are written at run time) whose routine is a drawer of terrain or landmark models (0x21C40, 0x21D0C, 0x21C34) or of the
entities (0x26944 and the other wrappers, 0x21C44), every leaf of the detail lists (both lists, fields +8 and +12; every threshold on both sides)
is taken, and the tree of the leaf's stream is walked along BOTH children of every node (not along the path a camera would take), following the
jumps and the draw items that continue (bit 14 and bit 13 clear).  A draw item whose word is 0xFFFF is a stream end.
Prints the number of descriptors, leaves and streams, and the leaves whose tree reaches the word, by routine class."""
import collections, os, struct, sys
from common import IMGPATH

BASE = 0x10000


def main():
    if not os.path.exists(IMGPATH):
        print('skipped (missing: extracted/interceptor.bin)'); return 0
    d = open(IMGPATH, 'rb').read()
    u16 = lambda a: struct.unpack('>H', d[a - BASE:a - BASE + 2])[0]
    s16 = lambda a: struct.unpack('>h', d[a - BASE:a - BASE + 2])[0]
    u32 = lambda a: struct.unpack('>I', d[a - BASE:a - BASE + 4])[0]
    cls = {0x21C40: 'terrain/landmark', 0x21D0C: 'terrain/landmark', 0x21C34: 'terrain/landmark', 0x21C44: 'entity'}
    res = collections.Counter(); hits = []
    seen_streams = set()
    for tab, it in [(0x2600C, i) for i in range(134)] + [(0x25ECC, i) for i in range(134)]:
        dsc = tab + 20 * it
        rt = u32(dsc)
        if rt in (0x22CFC, 0x22D0A): continue
        base = u32(dsc + 4)
        if not (0x10000 < base < 0x80000) or s16(dsc) < 0: continue
        if rt not in cls and not (0x26900 < rt < 0x26B00): continue
        kind = cls.get(rt, 'entity')
        if tab == 0x25ECC and kind != 'entity': continue          # the model table: only the entity records are new
        res[kind + ' descriptors'] += 1
        leaves = set()
        for fld in (8, 12):
            lst = u32(dsc + fld)
            if not (0x10000 < lst < 0x80000): continue
            # every (leaf, stream) a detail list can give: the walk of the list for each threshold boundary
            for sd in range(-1, 0x4001):
                a2 = lst
                while True:
                    d0 = s16(a2); a2 += 2
                    if d0 < 0:                          # a negative word is itself a leaf (its offset is the block, the stream follows)
                        leaves.add((d0 & 0xFFFF, a2, base)); break
                    t = d0 & 0x3FFF
                    if t > sd:
                        if not d0 & 0x4000: a2 += 2
                        leaves.add((s16(a2) & 0xFFFF, a2 + 2, base)); break
                    if d0 & 0x4000: a2 += 4; continue
                    j = s16(a2)
                    if j < 0: break
                    a2 = base + j
                if sd > 0x3FFF: break
                # only the boundaries matter: jump over the range between thresholds
        for leaf, stream, b in leaves:
            if leaf == 0xFFFF: res[kind + ' leaves that are 0xFFFF'] += 1; continue
            if (stream, b) in seen_streams: continue
            seen_streams.add((stream, b))
            res[kind + ' streams'] += 1
            # walk both children
            todo = [stream]; vis = set(); end = False
            while todo:
                a = todo.pop()
                if a in vis: continue
                vis.add(a)
                w = s16(a); a += 2
                if w >= 0:
                    d7 = u16(a); a += 2
                    if (d7 & 0xFC00) == 0xFC00: todo.append(b + s16(a)); continue
                    if d7 & 0xC00: pass
                    elif d7 & 0x2000: a += 12
                    else: a += 4
                    todo += [b + s16(a), b + s16(a + 2)]
                    continue
                if w == -1: end = True; continue
                if w & 0x2000: continue            # bit 13: the stream ends after the item
                if w & 0x4000: continue            # bit 14: the stream ends after the item
                a += 4 if w & 0x1000 else 0
                todo.append(a)
            if end: res[kind + ' streams whose tree reaches 0xFFFF'] += 1; hits.append((it, hex(stream)))
    # positive control: the cockpit block of the aircraft.  The alternate list of the models whose field +12 is the base minus 8 is a leaf (near block),
    # a draw item, a leaf (the cockpit block, part id 0xFFFF) and the stream of that block, the single word 0xFFFF
    for mid in range(134):
        rec = 0x25ECC + 20 * mid
        alt, base = u32(rec + 12), u32(rec + 4)
        if alt == base - 8 and s16(alt) < 0 and s16(alt + 4) < 0 and u16(alt + 6) == 0xFFFF and u16(base + (u16(alt + 4) & 0xFFF)) == 0xFFFF:
            res['positive control: models whose cockpit block (part id 0xFFFF) has the stream 0xFFFF'] += 1
    for k in sorted(res): print('   %-60s %5d' % (k, res[k]))
    print('streams ending on 0xFFFF:', hits)
    return 0


if __name__ == '__main__':
    sys.exit(main())
