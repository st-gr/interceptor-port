#!/usr/bin/env python3
"""Run the original sea-mesh pass 0x2F19C after the sky fill in the rig, from the exact state of a captured frame, and compare the pens with the frame.

    python3 tools/vamiga-ref/sky-ground/check_mesh.py extracted/captures/logs/sky-ground-attitude.txt [label ...]

Needs the logs written by sky-ground-*.txt (labels s71_NAME: the state block V:4A700, 2336 bytes, and rec0_NAME: the player record, 512 bytes at V:4B23C) and the
frames in extracted/captures/sky/. The pass reads the player record and the camera words and draws pen 6 polygons through the generic filler; sky 5, sea 6 and land 4
are compared with the frame colours (43,43,89), (0,28,72) and (0,56,0). Prints one line per state: compared pixels and differences. A state whose camera matrix was caught mid-update is skipped."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
import logparse, rigw, check_frames as cf

CMAP = {(43, 43, 89): 5, (0, 28, 72): 6, (0, 56, 0): 4}
PEN7 = (43, 43, 43)      # sky pen 5 with plane 1 set by a mesh polygon above the horizon (entry 7, 0x444); the HUD uses pen 7 as well, so only the rig's pen 7 pixels are compared


def run(log, label, fdir):
    L = logparse.load(log)
    st = cf.state(L["s71_" + label])
    if cf.torn(st):
        return None
    r = rigw.stage(state=L["s71_" + label].data)
    r.poke(0x4B23C, L["rec0_" + label].data)
    r.run(0x3486C)
    r.run(0x2F19C)
    pn = rigw.pens(r)
    im = cf.load_ppm(os.path.join(fdir, label + ".ppm"))
    tot = bad = 0
    for y in range(1, st["win"] + 1):
        for x in range(320):
            c = cf.game_px(im, x, y)
            if c in CMAP:
                tot += 1
                bad += pn[y][x] != CMAP[c]
            if pn[y][x] == 7:
                tot += 1
                bad += c != PEN7
    return tot, bad


if __name__ == "__main__":
    log = sys.argv[1]
    fdir = os.path.join(HERE, "..", "..", "..", "extracted", "captures", "sky")
    L = logparse.load(log)
    labels = sys.argv[2:] or [k[4:] for k in L if k.startswith("s71_")]
    sumbad = 0
    for lab in labels:
        res = run(log, lab, fdir)
        print(lab, "skipped" if res is None else "pixels %d, different %d" % res)
        sumbad += 0 if res is None else res[1]
    sys.exit(1 if sumbad else 0)
