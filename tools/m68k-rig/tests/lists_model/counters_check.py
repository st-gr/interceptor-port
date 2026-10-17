"""What the aircraft/weapon collision scan 0x2B5E2 does to the other record and to the flight-log counters (docs/re/07-ai-and-weapons.md, section 13.4; note 08 10.2).

    python3 counters_check.py [SEED] [N]

The scan is run in the rig with the mover (V:4AA6E) and one other record at the same position, so that the inner box always hits.  Compared with the rule of the note:
 - the result word (0x40 for a hit) and the contact flag V:4A957;
 - the other record: when its W0 bit 0x1000 is set it gets W0 bit 0x0200 (crashed); then, for a class whose high nibble is not 0, B20 bit 1 (out of control); then,
   (the cruise-missile exception of the code never matches), V:54E72 = its offset, W2 bit 0x1000 and the state byte 1;
 - the log record (V:1D024) counters: +0x44 (AIM-9L hits) when the mover has W0 bit 0x0008, group 0, class 0 and the other has bit 0x0008 clear; +0x40 (AIM-120 hits) the
   same with class 1; +0x48 (hit by a missile) when the mover is the player (V:4AA6E = 0) and the other's class has the high nibble 0 and the "friendly" case does not apply;
 - B20 bit 7 of both records: set when the mover has bit 0x0008 and the other has bit 0x0008 (a hit on a not-counted record), cleared otherwise.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so, extracted/captures/logs/lists.txt.  Skips with exit 0 when one is missing.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, load_scene, inputs_missing

REC = lambda i: 0x4B23C + 0x200 * i
LOGBUF = 0x700000


def main(argv):
    miss = inputs_missing()
    if miss: print("skipped (missing: %s)" % ", ".join(miss)); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 1500
    rng = random.Random(seed); r = Rig(); bad = 0; hits = 0; counted = {0x44: 0, 0x40: 0, 0x48: 0}
    for it in range(n):
        load_scene(r, "air")
        r.poke(LOGBUF, bytes(0x80)); r.w32(0x1D024, LOGBUF)
        mi = rng.choice((0, 1, 2, 4)); oi = rng.choice([i for i in range(mi + 1, 14) if i != 14])
        mcls = rng.choice((0x00, 0x01, 0x11, 0x12, 0x10)); ocls = rng.choice((0x00, 0x01, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x30, 0x31))
        mw0 = rng.choice((0x11C8, 0x11C0, 0x10CA, 0x10C2, 0x11C8)); ow0 = rng.choice((0x11C0, 0x11C8, 0x01C0, 0x01C8, 0x11C0 | 0x200, 0x11C0 | 0x400, 0x0040))
        mg = rng.choice((0, 0, 1, 5)); og = rng.choice((0, 3, 7))
        pos = [rng.randint(0x1000000, 0x2000000) for _ in range(3)]
        for k in range(16):
            if k not in (mi, oi): r.w16(REC(k), 0)
        for a, cls, w0, g in ((REC(mi), mcls, mw0, mg), (REC(oi), ocls, ow0, og)):
            r.poke(a, bytes(0x200)); r.w16(a, w0); r.w8(a + 0x62, cls); r.w8(a + 0x5E, g)
            for i, o in enumerate((0x14, 0x18, 0x1C)): r.w32(a + o, pos[i])
        r.w16(0x4AA6E, 0x200 * mi); r.w16(0x4AA6C, mi); r.w8(0x4A957, 0); r.w16(0x54E72, 0x7777)
        r.w8(REC(mi) + 0x20, rng.choice((0, 0x80))); r.w8(REC(oi) + 0x20, rng.choice((0, 0x80)))
        r.run(0x2B5E2, d2=pos[0], d3=pos[1], d4=pos[2])
        res = r.reg("d0") & 0xFFFF
        # --- the model
        mn = mcls & 0xF0; on = ocls & 0xF0
        collides = (mn != 0x20) and (ow0 & 0x40) and not (ow0 & 0x600) and og != mg and ((mw0 & 8) or (ow0 & 8)) and on != 0x30 and on != 0x20
        e_res = 0x40 if collides else 0
        if not collides:
            ok = res == 0 and r.u8(0x4A957) == 0 and r.u16(0x54E72) == 0x7777 and not (r.u16(REC(oi) + 2) & 1)
        else:
            hits += 1
            ok = res == 0x40 and r.u8(0x4A957) == 1 and (r.u16(REC(oi) + 2) & 1)
            o = REC(oi)
            e_w0 = ow0; e_b20 = r.u8(o + 0x20)    # B20 of the other before the call is not recoverable: compare the changed bits only
            if ow0 & 0x1000:
                e_w0 |= 0x200
                ok &= bool(r.u16(o) & 0x200)
                if on != 0:
                    ok &= bool(r.u8(o + 0x20) & 2)
                    # the test for the cruise missile compares the masked class (0x10) with 0x15 and never matches: class 0x15 is treated as any aircraft
                    ok &= r.u16(0x54E72) == 0x200 * oi and bool(r.u8(o + 2) & 0x10) and r.u8(o + 5) == 1
            else:
                ok &= (r.u16(o) & 0x200) == (ow0 & 0x200) and r.u16(0x54E72) == 0x7777
            c44 = c40 = c48 = 0
            if mw0 & 8:
                if mg == 0:
                    if mcls == 0 and not (ow0 & 8): c44 = 1
                    if mcls == 1 and not (ow0 & 8): c40 = 1
            if (not (mw0 & 8)) or (not (ow0 & 8)):
                if mi == 0 and on == 0: c48 = 1
            ok &= (r.u16(LOGBUF + 0x44), r.u16(LOGBUF + 0x40), r.u16(LOGBUF + 0x48)) == (c44, c40, c48)
            counted[0x44] += c44; counted[0x40] += c40; counted[0x48] += c48
            if (mw0 & 8) and (ow0 & 8):
                ok &= bool(r.u8(REC(mi) + 0x20) & 0x80) and bool(r.u8(o + 0x20) & 0x80)
            else:
                ok &= not (r.u8(REC(mi) + 0x20) & 0x80) and not (r.u8(o + 0x20) & 0x80)
        if not ok:
            bad += 1
            if bad < 6: print("DIFF it %d mover %d cls %02X w0 %04X g %d / other %d cls %02X w0 %04X g %d: res %X" % (it, mi, mcls, mw0, mg, oi, ocls, ow0, og, res))
    print("counters_check: %d scans, %d hits, counters +0x44 %d +0x40 %d +0x48 %d, %s" % (n, hits, counted[0x44], counted[0x40], counted[0x48], "FAILED %d" % bad if bad else "0 differences"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
