"""The jolt routine 0x29A5E (docs/re/07-ai-and-weapons.md, section 13.3): the kind byte V:4A878 (1 to 4), the index V:4A947, the active bit 0x0400 of V:4A984 and the
three offset variables V:4A990, V:4AA3C, V:4A9D0 over random call sequences, against the rule of the note.

    python3 jolt_check.py [SEED] [N]

Model: kind 0 does nothing; with |V:4AA3E| above 14 the kind is cleared and nothing else changes (and the redraw request routine 0x1EFF4 is not called); otherwise, when
the index has reached the table length (4) the end is taken (kind and index cleared, bit 0x0400 cleared, V:4A9D0 = 0, V:4AA3C = 0x90; V:4A990 stays); else the first
call of a jolt (bit 0x0400 clear) zeroes the index and sets the bit, the offset d = table[kind][index] (-1, +1, 0, 0 for kinds 1 and 3; +1, -1, 0, 0 for 2 and 4) is
stored as V:4A990 = d, V:4AA3C = 0x90 + d, V:4A9D0 = 40 d and the index steps.  The request routine's effect (the 14 request bytes V:4A8EE.. set to 3) is compared too.
Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so.  Skips with exit 0 when one is missing.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH

TABLE = {1: (-1, 1, 0, 0), 2: (1, -1, 0, 0), 3: (-1, 1, 0, 0), 4: (1, -1, 0, 0)}
REQ = (0x4A8EE, 0x4A8EF, 0x4A8F1, 0x4A8F2, 0x4A8F3, 0x4A8F4, 0x4A8F5, 0x4A8F6, 0x4A8F7, 0x4A8F8, 0x4A8F9, 0x4A8FB, 0x4A8FC, 0x4A8FD)


def s16(v): return v - 0x10000 if v & 0x8000 else v


def main(argv):
    if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
        print("skipped (missing: extracted/interceptor.bin or librig.so)"); return 0
    seed = int(argv[1]) if len(argv) > 1 else 1
    n = int(argv[2]) if len(argv) > 2 else 400
    rng = random.Random(seed); r = Rig(); r.reset(); bad = 0; calls = 0; jolts = 0
    for it in range(n):
        r.reset()
        st = dict(kind=0, idx=0, bit=0, y=0, row=0x90, off=0)
        r.w8(0x4A878, 0); r.w8(0x4A947, 0); r.w16(0x4A990, 0); r.w16(0x4AA3C, 0x90); r.w32(0x4A9D0, 0)
        for step in range(rng.randint(3, 14)):
            if rng.random() < 0.25 and st["kind"] == 0:
                st["kind"] = rng.choice((1, 2, 3, 4)); r.w8(0x4A878, st["kind"])
            v3e = rng.choice((0, 0, 0, 0x32, 14, -14, 15, -15, 1, 0x32))
            r.w16(0x4AA3E, v3e)
            for a in REQ: r.w8(a, 0)
            r.run(0x29A5E)
            calls += 1
            reqd = False
            if st["kind"]:
                if abs(v3e) > 14:
                    st["kind"] = 0
                else:
                    reqd = True
                    if st["idx"] >= 4:
                        st["kind"] = 0; st["idx"] = 0; st["bit"] = 0; st["off"] = 0; st["row"] = 0x90
                    else:
                        if not st["bit"]:
                            st["idx"] = 0; st["bit"] = 1
                        d = TABLE[st["kind"]][st["idx"]]; st["idx"] += 1
                        st["y"] = d; st["row"] = 0x90 + d; st["off"] = 40 * d
                        jolts += 1
            got = (r.u8(0x4A878), r.u8(0x4A947), bool(r.u16(0x4A984) & 0x400), s16(r.u16(0x4A990)), r.u16(0x4AA3C), r.s32(0x4A9D0))
            want = (st["kind"], st["idx"], bool(st["bit"]), st["y"], st["row"], st["off"])
            gotreq = all(r.u8(a) == 3 for a in REQ)
            if got != want or gotreq != reqd:
                bad += 1
                if bad < 6: print("DIFF it %d step %d v3e %d got %s want %s req %s/%s" % (it, step, v3e, got, want, gotreq, reqd))
    print("jolt_check: %d calls, %d offsets stored, %s" % (calls, jolts, "FAILED %d" % bad if bad else "0 differences"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
