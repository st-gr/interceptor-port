"""Damage rules of the player's aircraft-collision result (docs/re/07-ai-and-weapons.md, sections 9 and 10).

    python3 damage_check.py [SEED] [N]

Four checks (A, B as below; C: the marks and counters a hit leaves on the other record, 0x2B798 and 0x2BFFA; D: the scan order) against the original, both from a staged air scene (the logged 'air' state of extracted/captures/logs/lists.txt: the player in record 0 and the carrier
in record 14; the other records are written here):
  A. the aircraft box test of the collision scan 0x2B5E2 called directly for a mover and another record with random classes and flags: the half-size pairs
     (outer, inner) by class pair, the direct test and the swept (midpoint) test, the skip conditions, against the stated model;
  B. the result handler of the world update 0x26AF8 with a second aircraft record placed on the player: the speed gate (0x3C0), the immunity byte V:4A952,
     the collisions byte V:4A844, the cool-downs V:4A907 and V:4A906, the low bits of the frame counter V:4A992 and the three posts (0x9014, 0xD00B, 0xD00C),
     B20 bit 1, V:4A984 bit 8, the jolt byte V:4A878 and W0 bit 0x8000, against the stated model.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so, extracted/captures/logs/lists.txt.  Skips with exit 0 when one is missing.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, inputs_missing, load_scene

REC = lambda i: 0x4B23C + 0x200 * i


def sgn(v): return v - (1 << 32) if v & 0x80000000 else v


def s32(v): return sgn(v & 0xFFFFFFFF)


def box_model(m, o, w2m, w2o, dprev, disp_o):
    """m, o: dicts of the mover and the other record fields. Returns (contact, hit)."""
    if not (o["w0"] & 0x40) or (o["w0"] & 0x600) or o["grp"] == m["grp"]: return (False, False)
    if not ((m["w0"] & 0x08) or (o["w0"] & 0x08)): return (False, False)
    c = o["cls"]; n = c & 0xF0
    if c == 0x15: outer, inner = 0x5000, 0x1000
    elif n == 0x30: return (False, False)
    elif n == 0x20: return None
    else:
        dm = m["cls"] & 0xF0
        if dm == 0 or dm != n: outer, inner = 0x10000, 0x5000
        elif (w2m & 0x100) or (w2o & 0x100): outer, inner = 0x6000, 0x800     # W2 bit 0x0100: the byte at +2 (the high byte of W2), bit 0
        else: outer, inner = 0x7000, 0x1000
    d = [o["pos"][i] - dprev[i] for i in range(3)]
    if any(abs(x) > outer for x in d): return (False, False)
    if all(abs(x) <= inner for x in d): return (True, True)
    d2 = [o["pos"][i] - (disp_o[i] >> 1) - dprev[i] for i in range(3)]
    return (True, all(abs(x) <= inner for x in d2))


def check_boxes(r, rng, n):
    bad = 0; hits = 0; tot = 0
    for it in range(n):
        load_scene(r, "air")
        mi = rng.choice((0, 1, 2, 5, 9, 12)); oi = rng.choice([i for i in range(mi + 1, 16)])        # the scan visits only the records after the mover, up to record 15
        mcls = rng.choice((0x10, 0x11, 0x14, 0x00, 0x01, 0x15, 0x1F))
        ocls = rng.choice((0x10, 0x11, 0x14, 0x00, 0x01, 0x15, 0x30, 0x31, 0x40))
        mw0 = rng.choice((0x1048, 0x1040, 0x10C8, 0x1008)); ow0 = rng.choice((0x1048, 0x1040, 0x1008, 0x1448, 0x1240, 0x0048, 0x1000))
        mg = rng.choice((0, 1, 7)); og = rng.choice((0, 1, 7))
        w2m = rng.choice((0, 0x100)); w2o = rng.choice((0, 0x100))
        base = [rng.randint(0x1000000, 0x2000000) for _ in range(3)]
        dprev = [base[0], base[1], base[2]]
        sizes = (0x800, 0x1000, 0x5000, 0x6000, 0x7000, 0x10000)
        d = [rng.choice((0, 1)) * 0 + rng.choice(sizes) * rng.choice((-1, 1)) + rng.choice((-1, 0, 1)) if rng.random() < 0.8 else rng.randint(-0x20000, 0x20000) for _ in range(3)]
        if rng.random() < 0.5: d = [rng.choice((0, 1, -1, 5, -0x800, 0x800)) for _ in range(3)] if rng.random() < 0.5 else [x // 8 for x in d]
        disp = [rng.choice((0, 0x2000, -0x2000, 0x4000, 0x20000)) * (1 if rng.random() < .5 else 0) for _ in range(3)]
        m = REC(mi); o = REC(oi)
        for a, cls, w0, g, w2 in ((m, mcls, mw0, mg, w2m), (o, ocls, ow0, og, w2o)):
            r.poke(a, bytes(0x200)); r.w16(a, w0); r.w16(a + 2, w2); r.w8(a + 0x62, cls); r.w8(a + 0x5E, g)
        for i, off in enumerate((0x14, 0x18, 0x1C)):
            r.w32(m + off, dprev[i]); r.w32(o + off, dprev[i] + d[i])
        for off, v in zip((0x3E, 0x42, 0x46), disp): r.w32(o + off, v)
        r.w16(0x4AA6E, 0x200 * mi); r.w16(0x4AA6C, mi)
        r.w8(0x4A957, 0)
        for k in range(1, 16):
            if k not in (mi, oi): r.w16(REC(k), 0)
        # the player (record 0) must not be scanned as "other": group of record 0 equals the mover's, flags off
        if mi != 0: r.w16(REC(0), 0)
        exp = box_model({"w0": mw0, "grp": mg, "cls": mcls}, {"w0": ow0, "grp": og, "cls": ocls, "pos": [dprev[i] + d[i] for i in range(3)]}, w2m, w2o, dprev, disp)
        if exp is None: continue
        r.run(0x2B5E2, d2=dprev[0], d3=dprev[1], d4=dprev[2])
        res = r.reg("d0") & 0xFFFF
        got = (bool(r.u8(0x4A957)), res == 0x40)
        tot += 1; hits += got[1]
        if got != exp:
            bad += 1
            if bad < 8: print("DIFF box mi %d oi %d g %d %d mcls %02X ocls %02X mw0 %04X ow0 %04X w2 %d%d d %s disp %s got %s want %s res %X" % (mi, oi, mg, og, mcls, ocls, mw0, ow0, w2m, w2o, d, disp, got, exp, res))
    print("boxes: %d cases, %d hits, %s" % (tot, hits, "FAILED %d" % bad if bad else "0 differences"))
    return bad


def stage_hit(r, n, cd7, cd6, other_cls, immune, coll, view, nibble, speed):
    load_scene(r, "air")
    p = REC(0); q = REC(1)
    r.w16(p + 0x6E, speed); r.w8(0x4A952, immune); r.w8(0x4A844, coll); r.w8(0x4A83D, view)
    r.w16(0x4A992, n); r.w8(0x4A907, cd7); r.w8(0x4A906, cd6)
    r.w8(p + 0x7C, nibble)
    r.poke(q, r.peek(p, 0x200)); r.w16(q, 0x1048); r.w8(q + 0x62, other_cls); r.w8(q + 0x5E, 7); r.w16(q + 2, 0); r.w16(q + 0x6E, 0); r.w8(q + 0x20, 0)
    r.w8(0x4A878, 0); r.w16(0x4AB98, 0); r.w16(0x4A984, r.u16(0x4A984) & ~0x100)
    return p


def check_handler(r, rng, n):
    bad = 0; seen = {}
    for it in range(n):
        args = dict(n=rng.randint(0, 7), cd7=rng.choice((0, 0, 1, 30)), cd6=rng.choice((0, 0, 1, 30)), other_cls=rng.choice((0x10, 0x11, 0x00, 0x01)),
                    immune=rng.choice((0, 0, 0, 1)), coll=rng.choice((1, 1, 1, 0)), view=rng.choice((0, 1)), nibble=rng.choice((0, 0, 0, 0, 3)),
                    speed=rng.choice((0x100, 0x3C0, 0x3C1, 0x500, 0x1000, 0x2000)))
        p = stage_hit(r, **args)
        v984 = r.u16(0x4A984)
        r.run(0x26AF8)
        s = r.u16(p + 0x6E); w0 = r.u16(p); b20 = r.u8(p + 0x20)
        got = (r.u16(0x4AB98), bool(b20 & 2), bool(r.u16(0x4A984) & 0x100), r.u8(0x4A878) == 1, bool(w0 & 0x8000))
        # model: the handler of a result 0x40 for the player without an ejection nibble
        post = 0; b2 = False; f8 = False; jolt = False; w15 = False
        if args["nibble"] == 0 and args["coll"]:
            w15 = True
            if s > 0x3C0 and not args["immune"]:
                if args["n"] & 1 == 0:
                    if not (args["cd7"] > 1 or args["cd6"] > 1):      # the world update counts both down once before the step
                        b2 = True; f8 = True; post = 0x9014; jolt = True
                else:
                    post = 0xD00C if args["n"] & 2 else 0xD00B; jolt = True
        want = (post, b2, f8 or bool(v984 & 0x100), jolt, w15)
        if args["nibble"] != 0: continue                      # the dying path: note 05 17.12
        key = (post, b2)
        seen[key] = seen.get(key, 0) + 1
        if not args["coll"]:
            # with collisions off the handler ends before the bit: only the absence of any post/damage is stated
            want = (0, False, bool(v984 & 0x100), False, False)
        if got != want:
            bad += 1
            if bad < 8: print("DIFF handler %s S=%X got %s want %s" % (args, s, got, want))
    print("handler: %d cases, outcomes %s, %s" % (n, sorted((("%04X" % k[0], k[1]), v) for k, v in seen.items()), "FAILED %d" % bad if bad else "0 differences"))
    return bad


def check_effects(r, rng, n):
    """C. what the box test leaves behind (0x2B798 and the tail 0x2BFFA): the other record's crashed bit, out-of-control bit, camera follow, recentre bit and state
    byte, the friendly-fire marks (B20 bit 7), the log counters for hits on aircraft by the player's missiles and for missiles that hit the player."""
    bad = 0; seen = {}
    LOG = 0x700000
    for it in range(n):
        load_scene(r, "air")
        r.poke(LOG, bytes(0x80)); r.w32(0x1D024, LOG)
        mi = rng.choice((0, 1, 2, 3)); oi = rng.choice(range(max(mi + 1, 4), 14))
        mcls = rng.choice((0x00, 0x01, 0x11, 0x12)) if mi else rng.choice((0x11, 0x10))
        ocls = rng.choice((0x12, 0x13, 0x10, 0x15, 0x00, 0x01, 0x16))
        mw0 = rng.choice((0x1048, 0x1040, 0x11C8, 0x10C0)); ow0 = rng.choice((0x1048, 0x1040, 0x1000 | 0x40, 0x0048, 0x11C0, 0x11C8))
        mg = 0 if rng.random() < 0.6 else 1; og = rng.choice((5, 7))
        base = [rng.randint(0x1000000, 0x2000000) for _ in range(3)]
        m = REC(mi); o = REC(oi)
        for a, cls, w0, g in ((m, mcls, mw0, mg), (o, ocls, ow0, og)):
            r.poke(a, bytes(0x200)); r.w16(a, w0); r.w8(a + 0x62, cls); r.w8(a + 0x5E, g)
        for i, off in enumerate((0x14, 0x18, 0x1C)):
            r.w32(m + off, base[i]); r.w32(o + off, base[i] + rng.choice((0, 3, -0x800, 0x4000)))
        r.w16(0x4AA6E, 0x200 * mi); r.w16(0x4AA6C, mi); r.w8(0x4A957, 0)
        for k in range(1, 16):
            if k not in (mi, oi): r.w16(REC(k), 0)
        if mi != 0: r.w16(REC(0), 0)
        r.w16(0x54E72, 0xFFFF)
        r.run(0x2B5E2, d2=base[0], d3=base[1], d4=base[2])
        res = r.reg("d0") & 0xFFFF
        # model of the hit (inner box: the test of part A): recompute from the final positions
        d = [s32(r.u32(o + 0x14 + 4 * k)) - base[k] for k in range(3)]
        outer, inner = (0x10000, 0x5000)
        n_ = ocls & 0xF0; dm = mcls & 0xF0
        if ocls == 0x15: outer, inner = 0x5000, 0x1000
        elif n_ == 0x30: continue
        elif n_ == 0x20: continue
        elif dm == 0 or dm != n_: outer, inner = 0x10000, 0x5000
        else: outer, inner = 0x7000, 0x1000
        valid = (ow0 & 0x40) and not (ow0 & 0x600) and og != mg and ((mw0 & 8) or (ow0 & 8))
        hit = bool(valid) and all(abs(x) <= inner for x in d)
        key = ("hit" if hit else "no", mi == 0, mcls & 0xF0, ocls & 0xF0)
        if not hit:
            if res == 0x40 and valid and all(abs(x) <= outer for x in d): pass        # the swept test may still hit: part A covers it
            continue
        # expectations
        ow0n = ow0; ob20 = 0; ow2 = 0; ost = 0; cam = 0xFFFF
        if ow0 & 0x1000:
            ow0n |= 0x200
            if n_ != 0:
                ob20 |= 2; cam = 0x200 * oi; ow2 |= 0x1000; ost = 1
        mb20 = 0; log = {0x40: 0, 0x44: 0, 0x48: 0}
        if mw0 & 8:
            if mg == 0:
                if mcls == 0 and not (ow0 & 8): log[0x44] += 1
                elif mcls == 1 and not (ow0 & 8): log[0x40] += 1
            if ow0 & 8: ob20 |= 0x80; mb20 |= 0x80
            elif mi == 0 and n_ == 0: log[0x48] += 1
        else:
            if mi == 0 and n_ == 0: log[0x48] += 1
        got = (res, r.u16(o), r.u8(o + 0x20), r.u16(o + 2) & 0x1000, r.u8(o + 5), r.u16(0x54E72), r.u8(m + 0x20), {k: r.u16(LOG + k) for k in (0x40, 0x44, 0x48)})
        want = (0x40, ow0n, ob20, ow2, ost, cam, mb20, log)
        seen[key] = seen.get(key, 0) + 1
        if got != want:
            bad += 1
            if bad < 8: print("DIFF effects m%d o%d mcls %02X ocls %02X mw0 %04X ow0 %04X g %d %d: got %s want %s" % (mi, oi, mcls, ocls, mw0, ow0, mg, og, got, want))
    print("effects: %d pairs, %d hits compared, %d kinds (mover is the player, mover class nibble, other class nibble), %s" % (n, sum(seen.values()), len(seen), "FAILED %d" % bad if bad else "0 differences"))
    return bad


def check_order(r, rng, n):
    """D. the scan order: a record before the mover is never tested (the pair is tested once, from the earlier record), a ship mover scans nothing."""
    bad = 0
    for it in range(n):
        load_scene(r, "air")
        mi = rng.choice((4, 9, 12)); oi = rng.choice(range(1, mi))
        mcls = rng.choice((0x12, 0x00)); ocls = 0x12
        base = [rng.randint(0x1000000, 0x2000000) for _ in range(3)]
        for a, cls, g in ((REC(mi), mcls, 3), (REC(oi), ocls, 5)):
            r.poke(a, bytes(0x200)); r.w16(a, 0x1048); r.w8(a + 0x62, cls); r.w8(a + 0x5E, g)
            for k, off in enumerate((0x14, 0x18, 0x1C)): r.w32(a + off, base[k])
        for k in range(16):
            if k not in (mi, oi): r.w16(REC(k), 0)
        r.w16(0x4AA6E, 0x200 * mi); r.w16(0x4AA6C, mi); r.w8(0x4A957, 0)
        r.run(0x2B5E2, d2=base[0], d3=base[1], d4=base[2])
        if r.u8(0x4A957) or (r.reg("d0") & 0xFFFF) == 0x40 or (r.u16(REC(oi)) & 0x200):
            bad += 1
    # a ship as the mover
    load_scene(r, "air")
    for a, cls, g in ((REC(3), 0x21, 3), (REC(6), 0x12, 5)):
        r.poke(a, bytes(0x200)); r.w16(a, 0x1048); r.w8(a + 0x62, cls); r.w8(a + 0x5E, g)
        for off in (0x14, 0x18, 0x1C): r.w32(a + off, 0x1500000)
    for k in range(16):
        if k not in (3, 6): r.w16(REC(k), 0)
    r.w16(0x4AA6E, 0x600); r.w16(0x4AA6C, 3); r.w8(0x4A957, 0)
    r.run(0x2B5E2, d2=0x1500000, d3=0x1500000, d4=0x1500000)
    if r.u8(0x4A957): bad += 1
    print("order: %d cases (record before the mover) and a ship mover, %s" % (n, "FAILED %d" % bad if bad else "0 differences"))
    return bad


def main(argv):
    miss = inputs_missing()
    if miss: print("skipped (missing: %s)" % ", ".join(miss)); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 600
    r = Rig(); rng = random.Random(seed)
    bad = check_boxes(r, rng, n * 4) + check_handler(r, rng, n) + check_effects(r, rng, n * 3) + check_order(r, rng, n)
    print("damage_check:", "FAILED" if bad else "ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
