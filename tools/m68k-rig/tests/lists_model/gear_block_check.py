"""The gear block of the F/A-18 models (3, 4, 6, 7: op 54, op 0, op 52, six op 53, op 55, op 13) drawn through the real path, per progress state n = 0 to 7 (docs/re/03-3d-pipeline.md, "Ops 52 and 53").

    python3 gear_block_check.py [SEARCH_SEEDS]        (default 80; skips with exit 0 without the extracted/ inputs)

Model 4 is the player's entity in the queue C of a random frame with an external view (V:4A83D = 1); the walker runs it with the operation handlers logged (primlog) and the polygon filler, the line routine, the
sprites and the disc replaced by loggers.  The camera seeds in which the program reaches op 52 are searched; for each, the byte +0x7C of the record is set to n << 4 for n = 0 to 7, the walk is run with the slot
array untouched and with three random fillings of the whole array (0x4D43C, 700 slots).  Checks: n = 0 to 5 run the handlers 54, 52, 53, 55, 13 in this order, n = 6 and 7 run 54 then 0 and none of 52, 53, 55 of the block,
and every state's primitives are identical under the three fillings (nothing depends on slot contents the object does not write)."""
import ctypes, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ent_common import *
import walk_check as W
import primlog as P


def main():
    nseeds = int(sys.argv[1]) if len(sys.argv) > 1 else 80
    if inputs_missing(): print("skipped (missing inputs)"); return 0
    r = Rig(); r.L.rig_set_max_instructions(30000000)
    HOOK = ctypes.CFUNCTYPE(None); log = []

    def mk(kind):
        def f():
            if kind == "poly":
                n = struct.unpack(">h", r.peek(0x5043C, 2))[0]; k = max(0, min(n, 12))
                log.append(("poly", r.u16(0x4AA0C) & 0xF, n, struct.unpack(">%dh" % (2 * k), r.peek(0x5043E, 4 * k))))
            elif kind == "line":
                log.append(("line", r.u16(0x4AA0C) & 0xF, r.reg("d0") & 0xFFFF, r.reg("d1") & 0xFFFF, r.reg("d2") & 0xFFFF, r.reg("d3") & 0xFFFF))
            else:
                log.append((kind, r.reg("d0") & 0xFFFF, r.reg("d1") & 0xFFFF, r.u16(0x4AA0C) & 0xF))
        return HOOK(f)
    hooks = {a: mk(k) for a, k in ((0x34A28, "poly"), (0x3455E, "line"), (0x340D4, "s1"), (0x340EA, "s2"), (0x3414E, "s3"), (0x33CAC, "disc"))}

    def run(seed, n, poison):
        rng = random.Random(seed)
        W.random_frame(r, rng, tag="air", active=True, extra=0, camr=300)
        player_desc(r, 4)
        e = W.window(r, "C")[0]
        r.w16(e, 0x10); r.w32(e + 2, 0x2600C)
        for off in (6, 8, 10, 0x10, 0x14): r.w16(e + off, 0)
        r.w8(e + 0x12, 0); r.w8(e + 0x13, 0); r.w16(e + 24, 0xFFFF)
        R = 0x4B23C; r.w8(R + 1, r.u8(R + 1) | 0x40)
        cx = r.u32(0x4AB1A); cz = r.u32(0x4AB22); cy = r.s32(0x4AB1E)
        r.w32(R + 0x14, cx & 0xFFFFF); r.w32(R + 0x1C, cz & 0xFFFFF); r.w32(R + 0x18, -cy)
        r.w8(R + 0x7C, n << 4); r.w8(0x4A83D, 1); r.w8(0x4A923, 0)
        P.install(r); P.clear_log(r)
        if poison is not None:
            pr = random.Random(poison); r.poke(0x4D43C, bytes(pr.getrandbits(8) for _ in range(6 * 700)))
        for a, h in hooks.items(): r.L.rig_hook_set(a, h, 0)
        del log[:]
        W.walk(r, "C"); r.L.rig_hooks_clear()
        return list(log), [x[1] for x in P.read_log(r) if x[0] == 3]
    seeds = []
    for sd in range(1, nseeds + 1):
        if 52 in run(sd, 0, None)[1]: seeds.append(sd)
    bad = 0; draws = 0; fewer = []
    for sd in seeds:
        base5 = None
        for n in range(8):
            ev, ops = run(sd, n, None); draws += 1
            seq = [o for o in ops if o in (54, 52, 53, 55, 13, 0, 76)][:6]
            if n <= 5:
                ok = ops[ops.index(54):ops.index(54) + 5] == [54, 52, 53, 55, 13]
            else:
                ok = ops[ops.index(54):ops.index(54) + 2] == [54, 0] and 52 not in ops and 53 not in ops
            for pz in (1, 2, 3):
                ev2, _ = run(sd, n, pz); draws += 1
                ok = ok and ev2 == ev
            if not ok: bad += 1; print("DIFF seed", sd, "n", n, ops[:12])
            if n == 5: base5 = len(ev)
            if n == 6: fewer.append(base5 - len(ev))
    print("gear block: %d camera seeds reach op 52 (of %d searched), 8 states each, %d draws, differences %d; primitives fewer at n = 6 than n = 5: %s" % (len(seeds), nseeds, draws, bad, fewer))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
