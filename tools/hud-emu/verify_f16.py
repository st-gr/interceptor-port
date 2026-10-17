"""Check the note's F-16 scale-HUD model (speed scale, altitude scale, G figure) against the original routine.

Runs 0x37D78 with the player's class byte set to 0x10 over the cockpit-ground snapshot, over a blank plane, and compares every pen
in the speed region (x < 130), the altitude region (x >= 200) and the G figure region with an independent model of the rules in
docs/re/06-cockpit-hud.md ("HUD (F-16...)"). Usage: python3 tools/hud-emu/verify_f16.py [first last [step]]  (speed in knots; the
altitude sweep uses the same index as tens of feet). Needs unicorn, numpy; about 0.3 s per value (the full 2,731 states take
about 5 minutes).

Run from the repository root: python3 tools/hud-emu/verify_f16.py   (exit 0 = every state matches, 1 = a mismatch)
Inputs (git-ignored, under extracted/): interceptor.bin, interceptor.map.json, captures/cockpit-ground.vasnap. If one is
missing the script prints SKIP and exits 0.
"""
import os, sys, struct
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
_need = ['extracted/interceptor.bin', 'extracted/interceptor.map.json', 'extracted/captures/cockpit-ground.vasnap']
_missing = [p for p in _need if not os.path.exists(os.path.join(ROOT, p))]
if _missing and __name__ == '__main__':
    print('SKIP verify_f16: missing ' + ', '.join(_missing))
    sys.exit(0)
import numpy as np
from util import *

E = 0x4b23c
IMG = open(ROOT + '/extracted/interceptor.bin', 'rb').read()
FB = 0x426d0 - 0x10000
GL = {}
for _i in range(64):
    _o = FB + struct.unpack('>H', IMG[FB + 2 * _i:FB + 2 * _i + 2])[0]
    GL[chr(0x20 + _i)] = IMG[_o:_o + 5]

def trunc_div(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q

def glyph(px, x, row, ch, pen=13):
    for r in range(5):
        for c in range(3):
            if GL[ch][r] >> (7 - c) & 1:
                px[(x + c, row + r)] = pen

def scale(px, value, label_x, point_x, tick_x, long_x, ptr, is_alt):
    """one scale: value is knots (speed) or tens of feet (altitude)"""
    low = value % 100
    if low <= 20:   n, d = value // 100 * 10, 0
    elif low <= 70: n, d = value // 100 * 10 + 5, -50
    else:           n, d = (value + 100) // 100 * 10, -100
    r0 = trunc_div(low + d, 3)
    y0 = 91 + r0
    def lab(num, top):
        s = '%03d' % (num % 1000) if is_alt else '%3d' % (num % 1000)   # speed: leading zeros blank, last digit kept
        for ch, x in zip(s, label_x):
            if ch != ' ': glyph(px, x, top, ch)
        if is_alt: px[(point_x, top + 4)] = 13
    lab(n, 89 + r0)
    k = 1
    while 89 + r0 - 15 * k >= 70:
        lab(n + 5 * k, 89 + r0 - 15 * k); k += 1
    k, m = 1, n
    while m != 0 and 89 + r0 + 15 * k <= 109:
        m -= 5; lab(m, 89 + r0 + 15 * k); k += 1
    # dotted ticks: single pixel at tick_x every 3 rows above (rows > 71) and below (rows < 112); two pixels on the centre row and every fifth
    def tick(y, long):
        px[(tick_x, y)] = 13
        if long: px[(long_x, y)] = 13
    tick(y0, True)
    k = 1
    while y0 - 3 * k > 71:
        tick(y0 - 3 * k, k % 5 == 0); k += 1
    k = 1
    while y0 + 3 * k < 112:
        tick(y0 + 3 * k, k % 5 == 0); k += 1
    for x in ptr: px[(x, 91)] = 13

def model(v, ft, gw=0):
    px = {}
    scale(px, v, (92, 96, 100), None, 105, 104, range(107, 112), False)
    for p in ((110, 86), (109, 86), (108, 87), (108, 88), (110, 89), (109, 89)): px[p] = 13
    scale(px, ft // 10, (215, 219, 225), 223, 212, 213, range(206, 211), True)
    return px

def gfig(gw):
    """G figure of the F-16: same value as the F/A-18 readout (V:4A9FE = 0, V:4A982 bit 1 clear here)"""
    w = gw
    v = ((w + (w >> 3)) >> 3) - 10
    px = {(114, 65): 13}
    sign = '-' if v >= 0 else ' '
    a = abs(v) % 100
    if sign == '-': glyph(px, 106, 61, '-')
    if a >= 10: glyph(px, 110, 61, str(a // 10))
    glyph(px, 117, 61, str(a % 10))
    return px

def regions(px):
    return {k: p for k, p in px.items() if (k[0] < 130 and 66 <= k[1] < 150) or (k[0] >= 200 and 66 <= k[1] < 150)}

if __name__ == '__main__':
    a = list(map(int, sys.argv[1:]))
    first, last, step = (a + [0, 2730, 1][len(a):])[:3] if a else (0, 2730, 1)
    e = Emu3(SNAP % 'cockpit-ground')
    for x in range(0x4a8f0, 0x4a8ff): e.b(x, 0)
    e.b(E + 0x62, 0x10); e.l(0x4a79e, 0xfffff); e.w(0x4a9fe, 0); e.b(0x4a982, 0)
    bad = 0; n = 0
    for i in range(first, last + 1, step):
        v = i; ft = 10 * i + 5 * (i & 1)
        gw = (i * 37) % 600 - 100
        for p in e.pl[:4]: e.uc.mem_write(p, bytes(8000))
        e.w(E + 0x6e, 12 * v + (i * 5) % 12 * (v < 2730)); e.l(E + 0x18, (ft // 5) << 10); e.w(E + 0x56, gw)
        err = e.call(0x37d78)
        pl = [np.unpackbits(np.frombuffer(bytes(e.uc.mem_read(p, 8000)), dtype=np.uint8)).reshape(200, 320) for p in e.pl[:4]]
        im = pl[0] * 8 + pl[1] * 4 + pl[2] * 2 + pl[3]
        ys, xs = np.nonzero(im)
        got = {(int(x), int(y)): int(im[y, x]) for y, x in zip(ys, xs)}
        want = model(v, ft); g = gfig(gw)
        r_got = regions(got); r_want = regions(want)
        g_got = {k: p for k, p in got.items() if 55 <= k[1] <= 66 and 100 <= k[0] < 125}
        n += 1
        if err or r_got != r_want or g_got != g:
            bad += 1
            if bad <= 5:
                print('MISMATCH v=%d ft=%d gw=%d err=%s' % (v, ft, gw, err))
                print('  only original (scales):', sorted(set(r_got) - set(r_want))[:12], ' only model:', sorted(set(r_want) - set(r_got))[:12])
                print('  G only original:', sorted(set(g_got) - set(g))[:8], ' only model:', sorted(set(g) - set(g_got))[:8])
    print('checked %d states, %d mismatches' % (n, bad))
    sys.exit(1 if bad else 0)
