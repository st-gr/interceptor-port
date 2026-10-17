#!/usr/bin/env python3
"""Compare the zone-flag model (zones.py) with the flags V:4A953 and V:4A954 of every state block (label s71_NAME) of the capture logs given, or of all
extracted/captures/logs/sky-ground-*.txt. The flags are refreshed on even (V:4A953) and odd (V:4A954) frame numbers only and a paused staged state keeps the value of its
last unpaused frame, so a state is compared only when the model and the block can disagree legitimately never: the block holds the inputs of the same frame, but a poked
position or height may not have been seen by the refresh; mismatches are listed, not hidden.

    python3 tools/vamiga-ref/sky-ground/check_zone_logs.py [log ...]"""
import glob, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
import logparse, zones


def main(logs):
    n = [0, 0, 0, 0]
    for fn in logs:
        L = logparse.load(fn)
        for k in L:
            if not k.startswith("s71_"):
                continue
            b = L[k]
            u = lambda a: b.u8(a - 0x4A700)
            alt = -b.s32(0x4AB30 - 0x4A700)
            cx, cz, ph, oc = b.s16(0x4AA00 - 0x4A700), b.s16(0x4AA02 - 0x4A700), u(0x4A908), u(0x4A90C)
            m1, m2 = zones.lookup(0x23188, cz, cx, alt, ph, oc), zones.lookup(0x233B8, cz, cx, alt, ph, oc)
            n[0] += 1
            n[1] += u(0x4A953) != 0
            n[2] += u(0x4A954) != 0
            if m1 != (u(0x4A953) != 0) or m2 != (u(0x4A954) != 0):
                n[3] += 1
                print("DIFFERENT", os.path.basename(fn), k[4:], "model", m1, m2, "block", u(0x4A953), u(0x4A954))
    print("states %d, with V:4A953 set %d, with V:4A954 set %d, model and block differ in %d" % tuple(n))
    return 1 if n[3] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:] or sorted(glob.glob(os.path.join(HERE, "..", "..", "..", "extracted", "captures", "logs", "sky-ground-*.txt")))))
