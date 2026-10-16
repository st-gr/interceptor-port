#!/usr/bin/env python3
"""Compare the models with emulator frames: for every state of a capture log (labels s71_NAME: the state block V:4A700, 2336 bytes) the sky region
the model draws is compared with the colours of extracted/captures/sky/NAME.ppm (sky pen 5 = (43,43,89); ground pens 4 and 6 = (0,56,0) and
(0,28,72) in the reference emulator's colour curve). Pixels of other colours (HUD, panel, objects) are not compared.

    python3 tools/vamiga-ref/sky-ground/check_frames.py extracted/captures/logs/sky-ground-attitude.txt [frame_dir [file_prefix]]
    python3 tools/vamiga-ref/sky-ground/check_frames.py extracted/captures/logs/views.txt extracted/captures/openers view-     # the view captures of 01b

States whose camera matrix was caught mid-update (rows not yet scaled by the zoom triple) are reported and skipped; the capture scripts
(gen-sky-ground.py) stage every state with the game paused, so a frozen machine can still stop between the matrix build and the row scale."""
import math, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
import logparse, gmodel as gm, fmodel as fm

SKY, GROUND = (43, 43, 89), [(0, 56, 0), (0, 28, 72)]


def load_ppm(path):
    d = open(path, "rb").read()
    parts = d.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def game_px(im, x, y):
    """Bitmap pixel (x, y): frame column 186 + 2x (+1 as the sample), frame row 42 + y."""
    w, _, b = im
    o = ((42 + y) * w + 186 + 2 * x + 1) * 3
    return tuple(b[o:o + 3])


def state(b):
    w = lambda a: b.s16(a - 0x4A700)
    return dict(M=[w(0x4AC90 + 2 * i) for i in range(9)], alt=b.s32(0x4AB1E - 0x4A700), f982=w(0x4A982), win=w(0x4AA3C), view=b.u8(0x4A83D - 0x4A700),
                f953=b.u8(0x4A953 - 0x4A700), pitch=w(0x4AB42) & 0xFFFF, roll=w(0x4AB4A) & 0xFFFF, cpitch=w(0x4AB4C) & 0xFFFF,
                zoom=[w(0x4AAF6 + 2 * i) for i in range(3)])


def torn(st):
    for r in range(3):
        n = math.sqrt(sum(v * v for v in st["M"][3 * r:3 * r + 3]))
        if abs(n - st["zoom"][r]) > 0.04 * st["zoom"][r] + 2:
            return True
    return False


def model_plane(st):
    corn = gm.corners(st["M"], st["alt"])
    pitch = st["pitch"] if st["view"] == 0 else st["cpitch"]
    mp = gm.polygon(corn, (st["f982"] >> 1) & 1, st["roll"], st["view"] == 0, pitch)
    if mp is None:
        return mp, "none", [[0] * 320 for _ in range(200)]
    if mp == "FULL":
        mp = [(0, 0), (319, 0), (319, 179), (0, 179)]
    kind, plane = fm.fillpoly(mp, st["win"])
    return mp, kind, plane


def compare(st, im):
    mp, kind, plane = model_plane(st)
    ns = ng = bs = bg = 0
    for y in range(1, st["win"] + 1):
        for x in range(320):
            c = game_px(im, x, y)
            if c == SKY:
                ns += 1
                bs += not plane[y][x]
            elif c in GROUND:
                ng += 1
                bg += bool(plane[y][x])
    return ns, ng, bs, bg


if __name__ == "__main__":
    log = sys.argv[1]
    fdir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "..", "..", "extracted", "captures", "sky")
    prefix = sys.argv[3] if len(sys.argv) > 3 else ""
    L = logparse.load(log)
    tot = [0, 0, 0, 0, 0, 0]
    for k in L:
        if not k.startswith("s71_"):
            continue
        st = state(L[k])
        if torn(st):
            print("skipped (matrix caught mid-update):", k)
            tot[5] += 1
            continue
        ns, ng, bs, bg = compare(st, load_ppm(os.path.join(fdir, prefix + k[4:] + ".ppm")))
        tot[0] += 1; tot[1] += ns; tot[2] += ng; tot[3] += bs; tot[4] += bg
        if bs or bg:
            print("MISMATCH", k, "sky pixels the model calls ground:", bs, "ground pixels the model calls sky:", bg)
    print("states %d, sky pixels %d, ground pixels %d, mismatches sky %d ground %d, skipped %d" % tuple(tot))
    sys.exit(1 if tot[3] or tot[4] else 0)
