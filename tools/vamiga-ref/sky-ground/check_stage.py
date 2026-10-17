#!/usr/bin/env python3
"""Check that the pokes of a staged capture script took: for every `log NAME` of the script the last pokes before it (pitch +0x66, heading +0x68, roll +0x6A of the
player record, the pause byte V:4A866, the height long +0x18) are compared with what the log holds for NAME (the record and the state block), and the view octant
V:4A90C is compared with the heading word the camera used (V:4AB46 in the cockpit, V:4AB4E in the external view; the map view has the heading 0).

    python3 tools/vamiga-ref/sky-ground/check_stage.py tools/vamiga-ref/scripts/sky-ground-mesh-c.txt extracted/captures/logs/sky-ground-mesh-c.txt

Prints one line per state and a summary: the states whose pokes did not take, and the histogram of the octants. Exit status 1 when any poke did not take."""
import collections, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import logparse
ANG_TOL = 10         # a step that runs between the thaw and the pause rebuilds the angle words from the matrix: equal within a few units
EXT_TOL = 60        # the chase camera's heading word trails the aircraft's by up to about 60 units (2 degrees) in the staged states


def expectations(script):
    cur, out = {}, {}
    for line in open(script):
        t = line.split()
        if not t:
            continue
        if t[0] == "poke":
            a = t[1].upper().replace("F:", "")
            v = int(t[3], 0)
            cur[a] = v
        elif t[0] == "log":
            out[t[1]] = dict(cur)
    return out


def main(script, log):
    ex, L = expectations(script), logparse.load(log)
    bad, octs = [], collections.Counter()
    for name, e in ex.items():
        if "s71_" + name not in L:
            continue
        blk, rec = L["s71_" + name], L["rec0_" + name]
        ext, mp = blk.u8(0x4A83D - 0x4A700), blk.u8(0x4A865 - 0x4A700)
        problems = []
        if "4A866" in e and blk.u8(0x4A866 - 0x4A700) != e["4A866"]:
            problems.append("pause byte %d, poked %d" % (blk.u8(0x4A866 - 0x4A700), e["4A866"]))
        for a, off in (("4B2A2", 0x66), ("4B2A4", 0x68), ("4B2A6", 0x6A)):
            if a in e and abs((rec.u16(off) - e[a] + 14400) % 28800 - 14400) > ANG_TOL:
                problems.append("record +%#x is %d, poked %d" % (off, rec.u16(off), e[a]))
        if "4B254" in e and abs(rec.s32(0x18) - e["4B254"]) > 0:
            problems.append("height %d, poked %d" % (rec.s32(0x18), e["4B254"]))
        hw = blk.s16(0x4AB4E - 0x4A700) if (ext and not mp) else (0 if mp else blk.s16(0x4AB46 - 0x4A700))
        oct_ = blk.u8(0x4A90C - 0x4A700)
        want = 0 if hw < 0 else hw // 3600
        if oct_ != want:
            problems.append("octant %d, heading word %d" % (oct_, hw))
        if ext and not mp and "4B2A4" in e and abs((hw - e["4B2A4"] + 14400) % 28800 - 14400) > EXT_TOL:
            problems.append("external heading word %d, poked heading %d" % (hw, e["4B2A4"]))
        octs[oct_] += 1
        print(name, "ok" if not problems else "; ".join(problems), "| view %s octant %d phase %d f954 %d" % ("map" if mp else "ext" if ext else "cockpit", oct_, blk.u8(0x4A908 - 0x4A700), blk.u8(0x4A954 - 0x4A700)))
        if problems:
            bad.append(name)
    print("states %d, poke not taken in %d %s; octants %s" % (len(ex), len(bad), bad, dict(sorted(octs.items()))))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
