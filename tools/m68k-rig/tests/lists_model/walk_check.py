"""Walker and drawer-result checks (docs/re/04a-display-lists.md section 8 and 8b, Verified by).

    python3 walk_check.py [snapshots|random|entities|forced|norm|ground|c34|primitives|all] [SEED] [N]

Compares the rig's walkers (0x1FA24, 0x1FA36 for queues A and B, 0x1FBCC for the direct entities and queue C) with
  snapshots  the emulator's logged result words (+0x14) of the deck and airborne queues, and with the model
  random     the port-free model visibility.walk_queue on random frames (N frames, default 400)
  entities   the model's distance refresh of entity and terrain entries of queue C (N walks, default 900; forced N frames, default 3000)
  forced     constructed entries (low camera, flat blocks) against the model
  norm       the vector normaliser 0x29C0E against the model, bit for bit
  ground     the near-ground pass 0x2C0EC at both sides of each height limit
  c34        the 0x21C34 descriptors in the three queues (refresh and countdown rules of step 4 and 5)
  primitives the primitive layer (section 8c): prim_suite.py runs the handler returns of every operation (prim_ops.py), the stored result against the
             combination of the handler results (prim_walk.py), the vertex table, the writers of the gate bytes, the sprites, op 4, the entity path, the
             zoom words and the x handedness; N is the scale (1000 is the size cited in the notes, the default of `all` is 60)
Inputs (all git-ignored, read at run time): extracted/interceptor.bin, extracted/captures/logs/lists.txt (written by
tools/vamiga-ref/scripts/openers-lists.txt), tools/m68k-rig/librig.so (make -C tools/m68k-rig librig.so).  Skips with
exit 0 when one is missing.  Exit 1 on any difference.  The model decides only the entries whose drawer path it implements;
the others are counted as undecided with the reason, never as equal.
"""
import collections, math, random, struct, sys
from common import Rig, load_scene, load_log, inputs_missing, run_builder
import visibility as vis

STUB = 0x34A28                                   # the polygon filler: replaced by a return so no pixels are needed
WALKER = {'A': 0x1FA24, 'B': 0x1FA36, 'C': 0x1FBCC}
QUEUE = {'A': 0x53A4A, 'B': 0x540DA, 'C': 0x5476A}
OFFSET = {'A': 0x4AA64, 'B': 0x4AA66, 'C': 0x4AA68}
# exit name of the model -> rule number of section 8b
RULE = {'cut34': 2, 'nolevel': 4, 'subst': 5, 'pretest': 6, 'flatcull': 7, 'fv': 8, 'end': 9, 'ffff': 9, 'alt': 9}


def scene(r, tag):
    load_scene(r, tag)
    r.poke(STUB, b"\x4e\x75")
    for i in range(17):                          # run-time descriptors 0-16 (spawner copies): a routine that returns at once
        r.poke(0x2600C + 20 * i, struct.pack(">5I", STUB, 0, 0, 0, 0))
    run_builder(r)


def window(r, tier):
    """addresses of the entries the walker visits (from the walked-window offset to the terminator)"""
    a = QUEUE[tier] + r.u16(OFFSET[tier]); out = []
    while r.u16(a) != 0xFFFF and len(out) < 70:
        out.append(a); a += 24
    return out


def walk(r, tier):
    return r.run(WALKER[tier], a6=0x70000)


def snap(r): return vis.Mem(r.peek(0, 1 << 20))


def rule_of(d):
    e = d.exit
    return RULE.get(e, 0) if e else 0


def cmp_model(r, tier, m, pred, stats, show):
    """compare the model's result and walker fields with the rig after a walk; returns the number of differences"""
    bad = 0
    for ea, res, d in pred:
        stats['walked'] += 1
        decided = res is not None
        if not decided:
            stats['undecided: ' + ('type B' if 'routine 22' in d.exit else d.exit)] += 1
        diff = []
        if r.u16(ea + 0x10) != m.u16(ea + 0x10): diff.append('dist')
        if r.u8(ea + 0x13) != m.u8(ea + 0x13): diff.append('+13')
        if r.u8(ea + 0x12) != m.u8(ea + 0x12): diff.append('+12')
        if decided:
            stats['decided'] += 1
            stats['rule %d' % rule_of(d)] += 1
            if r.s16(ea + 0x14) != res: diff.append('result')
        if diff:
            bad += 1
            if show[0] < 6:
                show[0] += 1
                print('DIFF', tier, hex(ea), diff, 'model', res, d.exit, 'rig', r.s16(ea + 0x14))
    return bad


def check_snapshots(r):
    """rig walker vs the emulator's +0x14 of the logged queues, and vs the model"""
    log = load_log(); bad = 0; tot = 0
    for tag, name in (('air', 'airborne'), ('deck', 'deck')):
        for tier in 'AB':
            scene(r, tag)
            emu = log['q_' + tag].data; qb = {'A': 0, 'B': 0x690, 'C': 0xD20}[tier]
            m = snap(r); ents = window(r, tier); first = (ents[0] - QUEUE[tier]) // 24 if ents else 0
            pred = vis.walk_queue(m, tier); walk(r, tier)
            stats = collections.Counter(); show = [0]
            nd = cmp_model(r, tier, m, pred, stats, show); bad += nd
            equal = 0
            for ea in ents:
                i = (ea - QUEUE[tier]) // 24
                e = emu[qb + 24 * i + 0x14: qb + 24 * i + 0x16]
                tot += 1
                if struct.unpack('>h', e)[0] == r.s16(ea + 0x14): equal += 1
                else: bad += 1; print('EMULATOR DIFF', name, tier, 'entry', i)
            print('%-8s tier %s: %2d walked, rig result equals the emulator for %2d, positive %d; model decided %d, differences %d  %s'
                  % (name, tier, len(ents), equal, sum(1 for ea in ents if r.s16(ea + 0x14) > 0), stats['decided'], nd,
                     dict(sorted((k, v) for k, v in stats.items() if k.startswith(('rule', 'undec'))))))
    print('snapshots: %d walked entries compared with the emulator' % tot)
    return bad


def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def unit(a): n = math.sqrt(sum(x * x for x in a)); return tuple(x / n for x in a)


def rot_matrix(yaw, pitch, roll, zoom):
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    f = (sy * cp, sp, cy * cp)
    xr = unit(cross((0, 1, 0), f)); yu = cross(f, xr)
    cr, sr = math.cos(roll), math.sin(roll)
    x2 = tuple(cr * a + sr * b for a, b in zip(xr, yu)); y2 = tuple(-sr * a + cr * b for a, b in zip(xr, yu))
    return [int(round(v * z)) for row, z in zip((x2, y2, f), zoom) for v in row]


def random_frame(r, rng, tag='air', camr=3000, gate=0, active=False, extra=0):
    """a random viewer cell, sub-cell, camera height, offsets, octant and rotation matrix, built with request 0x0B"""
    load_scene(r, tag)
    r.poke(STUB, b"\x4e\x75")
    for i in range(17): r.poke(0x2600C + 20 * i, struct.pack(">5I", STUB, 0, 0, 0, 0))
    if not active:
        for k in range(16): r.w8(0x4B23C + 0x200 * k + 1, r.u8(0x4B23C + 0x200 * k + 1) & ~0x40)
    xc = rng.randint(40, 74); zc = rng.randint(38, 76); lay = rng.randint(0, 15)
    r.w16(0x4B23C + 6, xc); r.w16(0x4B23C + 8, zc); r.w8(0x4B23C + 0xA, lay)
    for k in rng.sample(range(1, 14), extra):    # further active records in the viewer's cell (entity entries of queue C)
        R = 0x4B23C + 0x200 * k
        r.w8(R + 1, r.u8(R + 1) | 0x40); r.w16(R + 6, xc); r.w16(R + 8, zc); r.w8(R + 0xA, rng.randint(0, 15))
    r.w16(0x4AA04, xc); r.w16(0x4AA06, zc); r.w8(0x4A908, 4 * (3 - (zc & 3)) + (3 - (xc & 3))); r.w8(0x4A909, lay)
    h = rng.randint(0x10000, 0x400000) if rng.random() < .5 else rng.randint(0x400, 0x9000)
    cx = rng.randint(-camr, camr) * 256; cz = rng.randint(-camr, camr) * 256
    r.w32(0x4AB1A, cx); r.w32(0x4AB1E, -h); r.w32(0x4AB22, cz)
    r.w16(0x4AB2A, (cx >> 8) & 0xFFFF); r.w16(0x4AB2E, (cz >> 8) & 0xFFFF); r.w32(0x4AB30, (-h) >> 8)
    octv = rng.randint(0, 7); r.w8(0x4A90C, octv); r.w8(0x4A83E, gate)
    zoom = struct.unpack('>3h', r.peek(0x4AAF6, 6))
    M = rot_matrix(octv * math.pi / 4 + rng.uniform(-.4, .4), rng.uniform(-.5, .15), rng.uniform(-.3, .3), zoom)
    r.poke(0x4AC90, struct.pack('>9h', *M))
    run_builder(r)


def check_random(r, seed=1, frames=80):
    rng = random.Random(seed); bad = 0; show = [0]
    stats = {t: collections.Counter() for t in 'ABC'}; skipped = collections.Counter()
    for _ in range(frames):
        random_frame(r, rng)
        r.w8(0x4A974, rng.choice((1, 3, 5))); r.w16(0x4A992, rng.randint(0, 255))
        for tier in 'ABC':
            for rep in range(3):                 # three walks of the same queue: the result of one is the countdown input of the next
                m = snap(r); ents = window(r, tier)
                pred = vis.walk_queue(m, tier); walk(r, tier)
                skipped[tier] += len(ents) - len(pred)
                bad += cmp_model(r, tier, m, pred, stats[tier], show)
                r.w16(0x4A992, (r.u16(0x4A992) + 1) & 0xFFFF)
    tot = sum(s['walked'] for s in stats.values()); dec = sum(s['decided'] for s in stats.values())
    print('random: %d frames x 3 walks per queue, %d entries drawn by the walkers (%d more skipped by the height limit or the countdown), %d decided by the model, %d differences'
          % (frames, tot, sum(skipped.values()), dec, bad))
    allc = collections.Counter()
    for s in stats.values(): allc.update(s)
    for k in sorted(allc):
        if k.startswith('rule') or k.startswith('undec'): print('   %-52s %4d' % (k, allc[k]))
    return bad


def check_entities(r, seed=1, walks=900):
    """900 walks of queue C with random record longs, old distance words and frame counters; entity entries are in the queue
    when records are active in the viewer's cell.  Compares +0x10, +0x12, +0x13 of every entry (and the result of decided terrain)."""
    rng = random.Random(seed); bad = 0; show = [0]; cnt = collections.Counter()
    for k in range(walks):
        random_frame(r, rng, tag=('deck', 'air')[k & 1], active=True, extra=rng.randint(0, 3), camr=rng.choice((200, 3000)))
        for i in range(16):
            R = 0x4B23C + 0x200 * i
            if r.u8(R + 1) & 0x40:
                r.w32(R + 0x14, rng.randint(0, 0xFFFFF) | (r.u32(R + 0x14) & 0xFFF00000))
                r.w32(R + 0x1C, rng.randint(0, 0xFFFFF) | (r.u32(R + 0x1C) & 0xFFF00000))
                r.w32(R + 0x18, rng.randint(-0x300000, 0x300000))
        ents = window(r, 'C')
        for e in ents: r.w16(e + 0x10, rng.choice([0, 0, 0x30, 0x90, 0x120, 0x1F0, 0x3F0, 0x500, 0x9000]))
        r.w16(0x4A992, rng.randint(0, 7))
        m = snap(r); pred = vis.walk_queue(m, 'C'); walk(r, 'C')
        for ea, res, d in pred:
            kind = 'entity' if r.u8(ea + 1) & 0x10 else 'secondary' if r.u8(ea + 1) & 0x40 else 'terrain'
            cnt[kind] += 1
        st = collections.Counter()
        bad += cmp_model(r, 'C', m, pred, st, show)
    print('entities: %d walks, %d queue C entries compared (entity %d, terrain %d, secondary %d), %d differences'
          % (walks, sum(cnt.values()), cnt['entity'], cnt['terrain'], cnt['secondary'], bad))
    return bad


FLAT_ITEMS = [27, 31, 50, 66, 67, 68, 69, 80]       # descriptor indices (object id minus 16) of the flat blocks with the pre-test


def check_forced(r, seed=1, n=300):
    """Constructed entries: a low camera (height up to 0x14000 pu, so the pre-test and the flat cull can run) and a random terrain entry of the window of a
    random queue is replaced by a random plain or common-drawer item with a random LOD, a position near the camera and distance word 0.
    The descriptors are the image's; only the entry is constructed (the builder never makes these combinations on purpose)."""
    rng = random.Random(seed); bad = 0; show = [0]; stats = collections.Counter(); undec = collections.Counter()
    r.reset()
    items = [i for i in range(17, 128) if r.u32(0x2600C + 20 * i) in (0x21C40, 0x21C34, 0x21D0C) and r.u32(0x2600C + 20 * i + 4) > 0x10000]
    for _ in range(n):
        random_frame(r, rng, camr=300)
        h = rng.randint(0x400, 0x14000)
        r.w32(0x4AB1E, -h); r.w32(0x4AB30, (-h) >> 8)
        flat = rng.random() < .6                  # half of the entries: the flat blocks of objects 43, 47, 66, 82-85, 96 in queues A and B
        tier = rng.choice('AB' if flat else 'ABC'); ents = [e for e in window(r, tier) if not r.u8(e + 1) & 0x50]
        if not ents: continue
        e = rng.choice(ents); it = rng.choice(FLAT_ITEMS if flat else items); lod = rng.randint(0, 6); spread = rng.choice((200, 800, 2500))
        r.w16(e, (it << 8) | (r.u8(e + 1) & 0xF0) | lod); r.w32(e + 2, 0x2600C + 20 * it)
        cx = struct.unpack('>h', r.peek(0x4AB2A, 2))[0]; cz = struct.unpack('>h', r.peek(0x4AB2E, 2))[0]
        r.w16(e + 6, ((-cx) >> lod) + rng.randint(-spread, spread)); r.w16(e + 8, 0); r.w16(e + 10, ((-cz) >> lod) + rng.randint(-spread, spread))
        r.w16(e + 0x10, 0); r.w16(e + 0x14, rng.choice((0, 0xFFFF)))
        m = snap(r); pred = vis.walk_queue(m, tier); walk(r, tier)
        for p in pred:
            if p[0] == e:
                bad += cmp_model(r, tier, m, [p], stats, show)
    print('forced: %d constructed entries walked, %d decided by the model, %d differences' % (stats['walked'], stats['decided'], bad))
    for k in sorted(stats):
        if k.startswith(('rule', 'undec')): print('   %-52s %4d' % (k, stats[k]))
    return bad


def check_norm(r, seed=1, n=3000):
    """The vector normaliser 0x29C0E (length argument 0x100, as the pre-test calls it) against visibility.vnorm, bit for bit"""
    rng = random.Random(seed); bad = 0; zero = 0
    r.reset()
    m = snap(r)
    for _ in range(n):
        sc = rng.choice((3, 40, 700, 9000, 32767))
        v = [rng.randint(-sc, sc) for _ in range(3)]
        if rng.random() < .2: v[1] = 0
        if rng.random() < .05: v = [0, 0, 0]
        r.run(0x29C0E, d0=0x100, d5=v[0] & 0xFFFF, d6=v[1] & 0xFFFF, d7=v[2] & 0xFFFF)
        got = tuple(vis.s32(r.reg(x)) for x in ('d5', 'd6', 'd7'))
        want = vis.vnorm(m, 0x100, *v)
        zero += (got == (0, 0, 0))
        if got != want:
            bad += 1
            if bad < 5: print('NORM DIFF', v, got, want)
    print('norm: %d inputs (%d giving the zero vector), %d differences' % (n, zero, bad))
    return bad


def check_ground(r):
    """The near-ground pass 0x2C0EC (stage [90]): it passes its height test (reaches 0x2C13A) when the camera height -V:4AB30 is at most
    2,048 u (pitch band 0), 3,456 u (band 1) or 4,608 u (bands 2 and 3).  The final clear of the pass flag at 0x2C360 is replaced by
    no-operations so that the flag set at 0x2C13A stays visible."""
    bad = 0
    for band, lim in ((0, 2048), (1, 3456), (2, 4608), (3, 4608)):
        seen = []; nb = 0
        for h in (lim // 2, lim - 1, lim, lim + 1, lim + 2, lim * 2):
            scene(r, 'air')
            r.poke(0x2C360, b"\x4e\x71" * 3)
            r.w8(0x4A923, band); r.w32(0x4AB30, -h); r.w8(0x4A85A, 0)
            r.run(0x2C0EC, a6=0x70000)
            ok = r.u8(0x4A85A) == (1 if h <= lim else 0)
            seen.append((h, r.u8(0x4A85A))); nb += 0 if ok else 1
        bad += nb
        print('ground: band %d limit %d: %s %s' % (band, lim, seen, 'ok' if not nb else 'DIFFERENT'))
    return bad


def check_c34(r):
    """A 0x21C34 descriptor given to the last walked entry of each queue (distance word 0, result -1, countdown 3):
    queues A and B neither refresh nor count down, queue C does both (04a section 8, steps 4 and 5)"""
    r.reset()
    desc = [0x2600C + 20 * i for i in range(17, 128) if r.u32(0x2600C + 20 * i) == 0x21C34]
    bad = 0
    print('c34: descriptors with routine 0x21C34 (item ids):', [(a - 0x2600C) // 20 for a in desc])
    for tier in 'ABC':
        scene(r, 'air')
        ents = [e for e in window(r, tier) if not r.u8(e + 1) & 0x50]
        if not ents: print('c34: tier', tier, 'no terrain entry'); bad += 1; continue
        e = ents[-1]
        r.w32(e + 2, desc[0]); r.w16(e + 0x10, 0); r.w16(e + 0x14, 0xFFFF); r.w8(e + 0x12, 3); r.w8(e + 0x13, 0)
        r.w32(0x4AB1E, -0x300000); r.w32(0x4AB30, -0x3000)       # height above the 0x21C34 cut-off: it draws
        m = snap(r); pred = vis.walk_queue(m, tier); walk(r, tier)
        got = (r.u16(e + 0x10), r.u8(e + 0x12), r.u8(e + 0x13))
        want = (m.u16(e + 0x10), m.u8(e + 0x12), m.u8(e + 0x13))
        expect = (0, 3, 0xFF) if tier in 'AB' else None
        ok = got == want and (expect is None or got == expect) and (tier != 'C' or got[0] != 0 and got[1] == 2)
        print('c34: tier %s entry %s: rig (dist %d, countdown %d, counter %d) model (%d, %d, %d) %s'
              % (tier, hex(e), *got, *want, 'ok' if ok else 'DIFFERENT'))
        bad += 0 if ok else 1
    return bad


def main(argv):
    mode = argv[1] if len(argv) > 1 else 'all'
    miss = inputs_missing()
    if miss:
        print('skipped (missing: %s)' % ', '.join(miss)); return 0
    r = Rig(); bad = 0
    seed = int(argv[2]) if len(argv) > 2 else 1
    n = int(argv[3]) if len(argv) > 3 else None
    if mode in ('snapshots', 'all'): bad += check_snapshots(r)
    if mode in ('random', 'all'): bad += check_random(r, seed, n or 400)
    if mode in ('entities', 'all'): bad += check_entities(r, seed, n or 900)
    if mode in ('forced', 'all'): bad += check_forced(r, seed, n or 3000)
    if mode in ('norm', 'all'): bad += check_norm(r, seed, n or 3000)
    if mode in ('ground', 'all'): bad += check_ground(r)
    if mode in ('c34', 'all'): bad += check_c34(r)
    if mode in ('primitives', 'all'):
        import prim_suite
        bad += prim_suite.run(seed, n or 60)
    print('walk_check:', 'FAILED, %d differences' % bad if bad else 'ok')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
