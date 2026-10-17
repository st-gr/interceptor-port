#!/usr/bin/env python3
"""Render the F/A-18 Interceptor flight computer (codewheel) set to a challenge, for a human to read.

    python tools/vamiga-ref/wheel_view.py 5D5A /tmp/wheel.png [FA18_codewheel_dir]

Stacks the three scans (git-ignored, repo root: FA18_codewheel_1_3.jpeg .. _3_3.jpeg) the way the manual
describes: the first challenge character on the inner disk (scan 1, the one with the labelled windows), the
second on the middle disk (scan 2), the third on the outer disk (scan 3), all lined up on one radial; then
look in the window of scan 1 labelled with the fourth character. This is a viewer, not a reader: you read
the number yourself. Needs numpy and pillow. Calibration (centres, rim angle of character 0, scale of
scans 1 and 3 relative to scan 2) was measured from the scans and checked against the manual's worked
example (9E5F gives the number the manual quotes) and against the game (the derived code was accepted).
"""
import math, sys, os
import numpy as np
from PIL import Image

HEX = "0123456789ABCDEF"
CEN = {1: (385.9, 371.9), 2: (387.7, 385.3), 3: (384.7, 385.6)}   # disk centres in the scans
TH0 = {1: 6.55, 2: 24.25, 3: 64.75}   # screen angle (deg, clockwise from east) of the sector of '0'
SCALE = {1: 0.893, 2: 1.0, 3: 1.065}  # scan scale to the common (scan 2) scale
N = 800
C0 = N / 2


def layer(path, k, rot):
    im = Image.open(path).convert("RGB")
    cx, cy = CEN[k]
    s = SCALE[k]
    th = math.radians(rot)
    c, sn = math.cos(th), math.sin(th)
    coeffs = (c / s, sn / s, cx - (c * C0 + sn * C0) / s,
              -sn / s, c / s, cy - (-sn * C0 + c * C0) / s)
    return im.transform((N, N), Image.AFFINE, coeffs, resample=Image.BICUBIC, fillcolor=(0, 0, 0))


def windows(lay, rmax):
    a = np.asarray(lay).astype(float)
    ys, xs = np.mgrid[0:N, 0:N]
    r = np.hypot(xs - C0, ys - C0)
    return (a.sum(2) < 120) & (r < rmax) & (r > 45)


def render(code, d):
    a1, a2, a3 = code[:3].upper()
    for ch in (a1, a2, a3):
        if ch not in HEX:
            raise SystemExit("challenge characters must be hexadecimal digits")
    t1 = TH0[1] + 22.5 * HEX.index(a1)
    rots = {1: 0.0,
            2: t1 - (TH0[2] + 22.5 * HEX.index(a2)),
            3: t1 - (TH0[3] + 22.5 * HEX.index(a3))}
    L = {k: layer(os.path.join(d, "FA18_codewheel_%d_3.jpeg" % k), k, rots[k]) for k in (1, 2, 3)}
    out = np.asarray(L[3]).copy()
    w2 = windows(L[2], 330)
    out[~w2] = np.asarray(L[2])[~w2]
    w1 = windows(L[1], 300)
    out[~w1] = np.asarray(L[1])[~w1]
    return Image.fromarray(out.astype(np.uint8))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    img = render(sys.argv[1], sys.argv[3] if len(sys.argv) > 3 else ".")
    img.save(sys.argv[2])
    print("wrote", sys.argv[2], "- find the window labelled", sys.argv[1][3].upper())
