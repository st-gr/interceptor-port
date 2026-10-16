#!/usr/bin/env python3
"""The released chaff against the emulator's picture, with the camera logged AT THE DRAW (docs/re/07-ai-and-weapons.md 7.7, 11.4).

    python chaffdraw_check.py [VIEW [LOG [FRAMEDIR]]]        (VIEW chase, back or head; run under ulimit -v 2500000 and timeout)

LOG is the emulator log of tools/vamiga-ref/scripts/combat7-chaffdraw*.txt (generator gen-combat7.py chaffdraw|chaffdrawback|chaffdrawhead): the chaff drawer
0x3140C is stopped at each call by `runto`, and the pool, the camera variables V:4AB1A.., the view matrix V:4AC90, the cells and the clip words are peeked at the
stop, so they are the state the drawer sees. At each stop the frame is written too; it shows the picture of the previous pass (the flip comes at the end of a pass).
The original's drawer is run in the rig on the logged state with the pixel, line and point routines stubbed to log the screen coordinates and the pen; the points it
predicts are looked for in the frame of the next stop. Skips with exit 0 without the log, the frames or the image.
A game pixel is two frame pixels wide and one line high; the game area starts at frame column 186 and line 43 (note 01a, Geometry).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "vamiga-ref"))
from common import ROOT, inputs_missing  # noqa: E402
from combat import stage, callf  # noqa: E402
from flare_chaff_replay import install, calls, POOL, LOG, PTR  # noqa: E402
import logparse  # noqa: E402


def pixel_colour(im, gx, gy):
    if not (0 <= gx < 320 and 0 <= gy < 200):
        return None
    return im.getpixel((186 + 2 * gx, 43 + gy))


def main(argv):
    view = argv[1] if len(argv) > 1 else "chase"
    log = argv[2] if len(argv) > 2 else os.path.join(ROOT, "extracted", "captures", "logs", "combat7-chaffdraw%s.txt" % {"chase": "", "back": "back", "head": "head", "fixed": "fixed"}[view])
    framedir = argv[3] if len(argv) > 3 else os.path.join(ROOT, "extracted", "captures", "combat7")
    miss = inputs_missing()
    if miss or not os.path.exists(log):
        print("skipped (missing: %s)" % ", ".join(miss or ["the capture log"]))
        return 0
    try:
        from PIL import Image
    except ImportError:
        print("skipped (no PIL)")
        return 0
    L = logparse.load(log)
    pal = L["pal"].data
    c13 = struct.unpack(">H", pal[26:28])[0]
    print("view %s, colour 13 = %03X" % (view, c13))
    r = stage(place=False, silence=False)
    for a, k in ((0x3455E, 2), (0x34168, 1), (0x340D4, 3)):
        install(r, a, k)
    snap = r.peek(0x4A6E8, 0x54EC0 - 0x4A6E8)
    k = 0
    total_pred = total_lit = 0
    while "s%d_pool" % k in L:
        pool = L["s%d_pool" % k].data
        slot = None
        for s in range(10):
            fl = struct.unpack_from(">H", pool, 64 * s + 0x26)[0]
            if fl & 0x2001 == 0x2001:
                slot = s
        if slot is None:
            k += 1
            continue
        life = struct.unpack_from(">h", pool, 64 * slot + 0x28)[0]
        r.poke(0x4A6E8, snap)
        r.poke(POOL, pool[64 * slot:64 * slot + 64])
        r.poke(0x4AB1A, L["s%d_cam" % k].data)
        r.poke(0x4AC90, L["s%d_mat" % k].data)
        r.poke(0x4AA04, L["s%d_cell" % k].data)
        r.w16(0x4AA3C, L["s%d_lowlimit" % k].u16(0))
        r.w16(0x4A79E, 0xF)
        r.w16(0x4A7A0, 0xFFFF)
        r.w16(0x4A7A2, 0)
        r.w32(PTR, LOG)
        callf(r, 0x3140C, (0,))
        c = calls(r)
        pts = []
        for kind, d, pen in c:
            if kind == 2:
                pts += [(d[0], d[1]), (d[2], d[3])]
            elif kind in (1, 3):
                pts.append((d[0], d[1]))
        pens = sorted(set(pen for _, _, pen in c))
        line = "stop %d pass %d life %d: %d calls, pens %s, points %s" % (k, L["s%d_pass" % k].u16(0), life, len(c), pens, pts[:6])
        f1 = os.path.join(framedir, "chaff-%s-%02d.ppm" % (view, k + 1))
        f0 = os.path.join(framedir, "chaff-%s-%02d.ppm" % (view, k))
        if pts and os.path.exists(f1):
            im1, im0 = Image.open(f1).convert("RGB"), Image.open(f0).convert("RGB")
            lit = {}
            for gx, gy in pts:
                # the plane pointers of the draw table sit one row above the displayed first row (note 07 7.7): a plot at y shows at y - 1
                col = pixel_colour(im1, gx, gy - 1)
                old = pixel_colour(im0, gx, gy - 1)
                if col is not None and col != old:
                    lit[col] = lit.get(col, 0) + 1
            total_pred += len(pts)
            total_lit += sum(lit.values())
            line += "; points changed against the previous frame: %d of %d, colours %s" % (sum(lit.values()), len(pts), lit)
        print(line)
        k += 1
    print("predicted points %d, changed %d" % (total_pred, total_lit))
    # the original's own point and line routines, unstubbed, on the same logged states: the pixels they write into four plane buffers of the rig
    r = stage(place=False, silence=False)
    snap = r.peek(0x4A6E8, 0x54EC0 - 0x4A6E8)
    TAB, PL = 0x300000, [0x310000 + 0x2000 * i for i in range(4)]
    k = 0
    while "s%d_pool" % k in L:
        pool = L["s%d_pool" % k].data
        r.poke(0x4A6E8, snap)
        r.poke(POOL, pool[:64])
        r.poke(0x4AB1A, L["s%d_cam" % k].data)
        r.poke(0x4AC90, L["s%d_mat" % k].data)
        r.poke(0x4AA04, L["s%d_cell" % k].data)
        r.w16(0x4AA3C, L["s%d_lowlimit" % k].u16(0))
        r.w16(0x4A79E, 0xF)
        r.w16(0x4A7A0, 0xFFFF)
        r.w16(0x4A7A2, 0)
        for i in range(4):
            r.w32(TAB + 4 * i, PL[i])
            r.poke(PL[i], bytes(0x2000))
        r.w32(0x4A76E, TAB)
        callf(r, 0x3140C, (0,))
        n = 0
        for row in range(200):
            for byte in range(40):
                n += bin(sum(r.u8(PL[p] + row * 40 + byte) for p in range(4))).count("1") > 0
        print("stop %d: the original's routines write pixels in %d plane bytes" % (k, n))
        k += 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
