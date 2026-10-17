"""Checks for the note 06 addendum rules: gear mask order and per-variant rows, fuel bar rows, leading digits, view-code-1 fuel
region, radar scope (blip shift sweep, blip width, pens, the 10-blip cap, target blink, own-ship mark, cone lines), target box,
marker slew, marker shapes, lock circle and range dot.

Run from the repository root: python3 tools/hud-emu/verify_addendum.py   (exit 0 = all pass, 1 = a check failed)
Inputs (all under extracted/, git-ignored):
  interceptor.bin, interceptor.map.json          relocated image (tools/hunk/relocate.py)
  captures/cockpit-ground.vasnap                 memory for every check (the routines are run over its RAM)
  captures/cockpit-takeoff.ppm                   frame compared with the gear box and symbol of variant 0
  captures/views/c-KP6-1.vasnap and .ppm         view code 1 snapshot and frame for the fuel region check
A missing input skips the checks that need it with a message; with interceptor.bin or cockpit-ground.vasnap missing the
whole script exits 0 without running anything.
"""
import os, sys, struct
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
_need = ['extracted/interceptor.bin', 'extracted/interceptor.map.json', 'extracted/captures/cockpit-ground.vasnap']
_missing = [p for p in _need if not os.path.exists(os.path.join(ROOT, p))]
if _missing:
    print('SKIP verify_addendum: missing ' + ', '.join(_missing))
    sys.exit(0)
from panel_exp import *
from unicorn.m68k_const import *
from PIL import Image

E = 0x4b23c
P = pal()
fails = 0


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('  ' + str(detail) if detail and not ok else ''))
    fails += 0 if ok else 1


def text(gl, y=None):
    return ''.join(c for (yy, x, c) in sorted(gl, key=lambda g: g[1]) if y is None or yy == y)


def draw(e, routine, setup, force=True):
    e.glyphs = set()
    setup(e)
    if force:
        e.b(0x4a8ef, 3)
    e.l(0x4a79e, 0xfffff)
    e.call(routine)
    return set(e.glyphs)


# 1. gear symbol: variant v draws the mask at 0x42B1C - 0x1C * v (mask index 3 - v). The symbol is drawn by clearing the
# mask's bits to pen 0 inside the lamp box; mask row r lands on bitmap row 189 + r, so the first row is 189 + (first non-empty
# mask row) and the height is the span of non-empty rows; the changed pixels are exactly the mask's set bits (box columns 14 to 28).
IMGB = open(os.path.join(ROOT, 'extracted/interceptor.bin'), 'rb').read()
GEAR_ROWS = {0: (190, 6, 25), 1: (190, 5, 24), 2: (190, 4, 25), 3: (189, 4, 27)}     # first row, rows, pixels
for v in range(4):
    e = base_panel()
    before = e.img()
    e.b(E + 0x7c, v << 5); e.b(0x4a8fd, 3); e.b(0x4a8fe, 0)
    e.call(0x35808)
    after = e.img()
    ch = [(x, y) for y in range(185, 200) for x in range(320) if after[y][x] != before[y][x]]
    ptr = int.from_bytes(IMGB[0x357f6 - 0x10000 + 4 * v:0x357f6 - 0x10000 + 4 * v + 4], 'big')
    check('gear variant %d uses mask 0x%X' % (v, 0x42B1C - 0x1C * v), ptr == 0x42B1C - 0x1C * v)
    mo = ptr - 0x10000
    mrows = [int.from_bytes(IMGB[mo + 4 * i:mo + 4 * i + 4], 'big') for i in range(7)]
    live = [i for i, r in enumerate(mrows) if r]
    want_px = sum(bin(r).count('1') for r in mrows)
    first, nrows, npx = GEAR_ROWS[v]
    got_rows = sorted(set(y for x, y in ch))
    check('gear variant %d: first row %d, %d rows, %d pixels' % (v, first, nrows, npx),
          bool(ch) and got_rows[0] == first and got_rows[-1] - got_rows[0] + 1 == nrows and len(ch) == npx == want_px
          and 189 + live[0] == first and live[-1] - live[0] + 1 == nrows, (got_rows[:1], got_rows[-1:], len(ch), want_px))

# 1b. variant 0 equals the capture of the idle aircraft (cockpit-takeoff.ppm), box and symbol rows 182 to 197
cp = os.path.join(ROOT, 'extracted/captures/cockpit-takeoff.ppm')
if os.path.exists(cp):
    e = base_panel(); e.b(E + 0x7c, 0); e.b(0x4a8fd, 3); e.b(0x4a8fe, 0); e.call(0x35808)
    im = e.img()
    cap0 = Image.open(cp).convert('RGB').crop((186, 42, 826, 242)).resize((320, 200), Image.NEAREST)
    d = [(x, y) for y in range(182, 198) for x in range(8, 33) if P[im[y][x]] != cap0.getpixel((x, y))]
    check('gear box and symbol of variant 0 equal the capture', not d, d[:5])
else:
    print('skip gear capture comparison: ' + cp + ' missing')

# 2. fuel bar: lit rows = min(22, max(0, level - 1)), level = (V:4A9AE & 0x7FFF) >> 10, rows counted up from 180
for digits in (0, 1024, 2048, 10000, 21503, 22528, 23552, 24999, 32767):
    e = base_panel()
    e.w(0x4a9ae, digits); e.l(0x4a79e, 0xfffff); e.b(0x4a8ef, 3); e.call(0x353ec)
    im = e.img()
    lit = [y for y in range(159, 181) if im[y][111] == 4 and im[y][112] == 4 and im[y][113] == 4 and im[y][114] == 4]
    n = len(lit)
    exp = min(22, max(0, (digits >> 10) - 1))
    check('fuel bar digits %d lit rows %d' % (digits, exp), n == exp and (n == 0 or min(lit) == 181 - n), (n, exp))

# 6. leading digits
for rounds, want in ((0, '0'), (5, '5'), (50, '50'), (500, '500'), (1234, '234'), (-1, '535')):
    e = base_panel(); e.glyphs = set()
    g = draw(e, 0x36724, lambda e: (e.b(E + 0x63, 0x10), e.w(E + 0x60, rounds)), force=False)
    digits = ''.join(c for c in text(g, 129) if c.isdigit())      # the label letters are not digits
    check('gun rounds %d shows %s' % (rounds, want), digits == want, digits)
for sp, want in ((0, '0'), (12, '1'), (120, '10'), (1200, '100'), (12000, '1000')):
    e = base_panel()
    g = draw(e, 0x36a10, lambda e: (e.w(E + 0x6e, sp), e.w(0x4a9b0, 0)))
    check('panel speed %d' % sp, text(g).strip().replace(' ', '') == want, text(g))
# G readout: tens blank when zero, units always; sign when v >= 0
for w, want in ((0, '10'), (8, '9'), (72, '-0'), (160, '-12')):
    e = base_panel()
    g = draw(e, 0x36828, lambda e: (e.w(E + 0x56, w), e.b(0x4a982, 0), e.w(0x4a9fe, 0)), force=False)
    s = ''.join(c for c in text(g, 121) if c.isdigit() or c == '-')   # digits and the sign; the label is dropped
    check('G readout w=%d' % w, s == want, s)

# 2b. view code 1: the picture's 19 lit bar rows at x 0..2 are the only bar pixels (the bar routine returns when x + offset <= 0)
snap = os.path.join(ROOT, 'extracted/captures/views/c-KP6-1.vasnap')
ppm = os.path.join(ROOT, 'extracted/captures/views/c-KP6-1.ppm')
if os.path.exists(snap) and os.path.exists(ppm):
    e = Emu3(snap)
    for p in e.pl[:4]: e.uc.mem_write(p, bytes(8000))
    e.b(0x4a8ee, 3); e.b(0x4a8ef, 3)
    for n in range(3): hud_frame(e, n)
    im = e.img()
    cap = Image.open(ppm).convert('RGB').crop((186, 42, 826, 242)).resize((320, 200), Image.NEAREST)
    diff = [(x, y) for y in range(155, 190) for x in range(0, 16) if P[im[y][x]] != cap.getpixel((x, y))]
    check('view code 1 fuel region identical to the capture', not diff, diff[:5])
else:
    print('skip view code 1 fuel region: ' + snap + ' or the .ppm missing')

# ---------------------------------------------------------------------------------------------------------------------
# Radar scope, target symbols: the real routines run over the cockpit-ground RAM with synthetic inputs on a blank bitmap.
D0, D1, D2, D4, A0 = UC_M68K_REG_D0, UC_M68K_REG_D1, UC_M68K_REG_D2, UC_M68K_REG_D4, UC_M68K_REG_A0
LIST = 0x53368          # radar list: 16-byte entries (three longs, flag word, two spare bytes), ended by an all-zero entry


def blank():
    e = Emu3(SNAP % 'cockpit-ground')
    for p in e.pl[:4]:
        e.uc.mem_write(p, bytes(8000))
    for a in range(0x4a8f0, 0x4a8ff):
        e.b(a, 0)
    e.w(0x4aa40, 0); e.w(0x4a990, 0); e.w(0x4aa3e, 0)       # no view offsets
    e.l(0x4a79e, 0xfffff)                                    # the plotter's pattern gate fully open
    return e


def pixels(e):
    im = e.img()
    return {(x, y): im[y][x] for y in range(200) for x in range(320) if im[y][x]}


def rshift(v, k):
    """arithmetic shift right by k, rounded half up (one added when the last bit shifted out was set)"""
    return (v >> k) + (((v >> (k - 1)) & 1) if k else 0)


def set_record(e, idx, cls=0x10, b1=0x40, b20=0x40, y=0):
    a = E + idx * 0x200
    e.w(a, b1)                                  # word at +0: byte +1 is b1 (bit 6 present, bit 3 friendly)
    e.b(a + 0x62, cls); e.b(a + 0x20, b20); e.l(a + 0x10, y); e.b(a + 3, 0)


def radar(e, ents, nib, R=0, n=2, ab30=-13, target=-1, frame_ctr=0):
    """ents: (x, z, record index); runs 0x35CF2 once and returns the lit pixels"""
    e.b(0x4a8f0, 1); e.b(0x4a8ef, R); e.w(0x4a992, n); e.w(0x4aa78, target); e.l(0x4ab30, ab30); e.b(0x4a93b, frame_ctr)
    e.b(E + 0x63, (e.rb(E + 0x63) & 0xf0) | nib)
    buf = b''.join(struct.pack('>iii', x, 0, z) + struct.pack('>H', (i << 8) | 0x10) + b'\0\0' for (x, z, i) in ents)
    e.uc.mem_write(e.R(LIST), buf + bytes(16))
    e.call(0x35cf2)
    return pixels(e)


PLUS = {(158, 167), (157, 168), (158, 168), (159, 168)}
CONES = {(139 + i, 148 + i) for i in range(16)} | {(177 - i, 148 + i) for i in range(16)}   # lit pixels: row of the end point + 1

# 7. own-ship mark (pen 12, four pixels) and cone lines (pen 3, 32 pixels): which frames draw them
for R, n, want_plus, want_cone in ((3, 1, 1, 1), (2, 2, 1, 1), (1, 2, 1, 1), (0, 8, 1, 0), (0, 9, 1, 0), (0, 0, 0, 1), (0, 1, 0, 1), (0, 2, 0, 0), (0, 6, 0, 0), (0, 10, 0, 0), (0, 16, 0, 1), (0, 24, 1, 0)):
    e = blank()
    e.b(0x4a8ef, R); e.w(0x4a992, n)
    e.call(0x35cf2)
    p = pixels(e)
    got_plus = {k for k, v in p.items() if v == 12}
    got_cone = {k for k, v in p.items() if v == 3}
    ok = got_plus == (PLUS if want_plus else set()) and got_cone == (CONES if want_cone else set()) and len(p) == len(got_plus) + len(got_cone)
    check('radar marks R=%d n=%d: own-ship %s, cones %s' % (R, n, 'yes' if want_plus else 'no', 'yes' if want_cone else 'no'), ok, (len(got_plus), len(got_cone), len(p)))

# 8. blip position: low nibble of E+0x63 (not 13 plus the nibble), rounded half up, window x 158 +-27, y 167 -16..+21
bad = []
for nib in (9, 11, 12, 13, 3):
    h = 1 << (nib - 1)
    vecs = [(3000, -5000), (-70000, 40000), (1 << 20, 1 << 19), (-(1 << 14), 77), (h, h), (-h, -h), (3 * h, -h),
            (20 << nib, -10 << nib), (-27 << nib, 21 << nib), (27 << nib, 16 << nib), (28 << nib, 0), (0, 17 << nib), (0, -22 << nib), (-28 << nib, 0), (0, -21 << nib)]
    for (x, z) in vecs:
        e = blank(); set_record(e, 1)
        p = radar(e, [(x, z, 1)], nib)
        bx, by = 158 - rshift(x, nib), 167 - rshift(z, nib)
        keep = -27 <= bx - 158 <= 27 and -16 <= by - 167 <= 21 and (x or z)
        got = {k for k, v in p.items() if v not in (3, 12)}
        if got != ({(bx, by)} if keep else set()):
            bad.append((nib, x, z, (bx, by), sorted(got)))
check('radar blip position over 5 range nibbles and %d vectors each' % len(vecs), not bad, bad[:4])

# 9. blip width: two pixels (x - 1 and x) when the object's +0x10 long is at least V:4AB30 negated
for yv, want_w in ((12, 1), (13, 2), (14, 2)):
    e = blank(); set_record(e, 1, y=yv)
    p = radar(e, [(3000, -5000, 1)], 13, ab30=-13)
    xs = sorted(k[0] for k, v in p.items() if v not in (3, 12))
    check('radar blip width at +0x10 = %d against -13' % yv, xs == ([158] if want_w == 1 else [157, 158]), xs)

# 10. pens: 5 classes 0x2x and 0x3x; 4 friendly; 2 missile (class nibble 0); 1 hostile aircraft; 8 hidden (bit 1 of +0x20)
for name, kw, pen in (('ship 0x20', dict(cls=0x21), 5), ('sub 0x30', dict(cls=0x30), 5), ('friendly', dict(cls=0x10, b1=0x48), 4),
                      ('missile', dict(cls=0x00), 2), ('hostile aircraft', dict(cls=0x10), 1), ('hidden', dict(cls=0x10, b20=0x42), 8)):
    e = blank(); set_record(e, 1, **kw)
    p = radar(e, [(3000, -5000, 1)], 13)
    pens = {v for k, v in p.items() if v not in (3, 12)}
    check('radar blip pen for ' + name, pens == {pen}, pens)

# 11. at most 10 blips per frame
e = blank()
for i in range(1, 13):
    set_record(e, i)
p = radar(e, [(i << 13, 0, i) for i in range(1, 13)], 13)
n_bl = len({k for k, v in p.items() if v not in (3, 12)})
check('radar draws at most 10 blips (12 entries in range)', n_bl == 10, n_bl)

# 12. the designated target's blip is drawn only when bit 0 of V:4A93B is set after the increment of the radar draw
for ctr, want in ((0, 1), (1, 0), (2, 1), (3, 0)):
    e = blank(); set_record(e, 1)
    p = radar(e, [(3000, -5000, 1)], 13, target=0x200, frame_ctr=ctr)
    got = len({k for k, v in p.items() if v not in (3, 12)})
    check('designated target blip with V:4A93B = %d: %s' % (ctr, 'drawn' if want else 'skipped'), got == want, got)

# 13. target box (0x38D8C): border of columns x-9..x+9, rows y-7..y+9 in pen 10 around the target point V:4A9EE/F0
def box_setup(e, tx=150, ty=70, mx=159, my=91, wpn=0x10):
    e.b(E + 0x63, wpn); e.w(E + 0x56, 0); e.w(0x4a9ee, tx); e.w(0x4a9f0, ty); e.w(0x4a9fa, mx); e.w(0x4a9fc, my); e.w(0x4aa78, -1); e.l(0x4ac08, 0)

e = blank(); box_setup(e); e.call(0x38d8c)
p = pixels(e)
border = {(x, y) for x in range(141, 160) for y in range(63, 80) if x in (141, 159) or y in (63, 79)}
check('target box around (150,70): 68 pixels, pen 10, columns 141..159, rows 63..79', set(p) == border and set(p.values()) == {10}, (len(p), sorted(set(p) ^ border)[:4]))
check('slew leaves V:4A9EE at -1 and copies the target point to V:4A9F2/F4', (e.rw(0x4a9ee), e.rw(0x4a9f2), e.rw(0x4a9f4)) == (-1, 150, 70), (e.rw(0x4a9ee), e.rw(0x4a9f2), e.rw(0x4a9f4)))
check('marker slew from (159,91) to (155,87) in four sub-steps', (e.rw(0x4a9fa), e.rw(0x4a9fc)) == (155, 87), (e.rw(0x4a9fa), e.rw(0x4a9fc)))
e = blank(); box_setup(e, mx=0, my=0); e.call(0x38d8c)
check('invalid marker (x <= 0) restarts at (159,91) then slews three sub-steps', (e.rw(0x4a9fa), e.rw(0x4a9fc)) == (156, 88), (e.rw(0x4a9fa), e.rw(0x4a9fc)))
e = blank(); box_setup(e, wpn=0); e.call(0x38d8c)
check('no weapon selected: marker x = -1', e.rw(0x4a9fa) == -1, e.rw(0x4a9fa))
for (mx, my, wx, wy, why) in ((152, 70, 152, 70, 'right of the target by 2 and level: stays'), (150, 72, 150, 72, 'below by 2: stays'),
                              (148, 70, 150, 70, 'left by 2: moves to the target'), (150, 68, 150, 70, 'above by 2: moves to the target')):
    e = blank(); box_setup(e, mx=mx, my=my); e.call(0x38d8c)
    check('marker slew, marker %s' % why, (e.rw(0x4a9fa), e.rw(0x4a9fc)) == (wx, wy), (e.rw(0x4a9fa), e.rw(0x4a9fc)))

# 14. marker shapes (0x3936E, pen 10): diamond far from the target (D4 = 0), ring in the lock box (D4 /= 0, n and 3 /= 0)
def shape(x, y, ring, n=1):
    e = blank(); e.w(0x4a992, n); e.w(0x4aa0c, 10)
    e.call(0x3936e, regs={D0: x, D1: y, D4: 1 if ring else 0}); return pixels(e)

DIAMOND_LIST = [(dx, dy) for dx in range(-3, 4) for dy in range(-3, 4) if abs(dx) + abs(dy) == 3]
p = shape(150, 70, False)
check('diamond: 12 pixels of radius 3 in pen 10', len(p) == 12 and {(x - 150, y - 70) for x, y in p} == set(DIAMOND_LIST) and set(p.values()) == {10}, len(p))
for (x, y, want) in ((89, 70, 1), (88, 70, 0), (229, 70, 1), (230, 70, 0), (150, 40, 1), (150, 39, 0), (150, 140, 1), (150, 141, 0)):
    check('diamond at (%d,%d): %s' % (x, y, 'drawn' if want else 'not drawn'), bool(shape(x, y, False)) == bool(want))
p = shape(159, 91, True)
check('ring: 44 pixels, x 148..169, rows 81..100 around (159,91)', len(p) == 44 and (min(k[0] for k in p), max(k[0] for k in p), min(k[1] for k in p), max(k[1] for k in p)) == (148, 169, 81, 100), len(p))
for n, want in ((1, 44), (2, 44), (3, 44), (4, 0), (8, 0), (5, 44)):
    check('ring blinks: frame counter %d draws %d pixels' % (n, want), len(shape(159, 91, True, n=n)) == want)

# 15. lock circle (0x3905C): 74 distinct pixels in x 145..173, rows 79..102 around (159,91) for the full count
for cnt in (0x4f, 0x50):
    e = blank(); e.w(0x4aa0c, 10)
    e.call(0x3905c, regs={D0: 159, D1: 91, D2: cnt, A0: e.R(0x39408)})
    p = pixels(e)
    check('lock circle count 0x%X: 74 pixels, x 145..173, rows 79..102' % cnt, len(p) == 74 and (min(k[0] for k in p), max(k[0] for k in p), min(k[1] for k in p), max(k[1] for k in p)) == (145, 173, 79, 102), len(p))

# 16. range dot (0x392AE): 2 by 2 pixels in pen 13, position index k on the arc list 0x39432
for cx, k, rel in ((159, 0, (-1, -11)), (159, 19, (12, -1)), (159, 38, (-1, 10)), (160, 0, (0, -11)), (160, 38, (0, 10))):
    e = blank(); e.w(0x4aa0c, 13)
    e.call(0x392ae, regs={D0: cx, D1: 91, D2: k, A0: e.R(0x39432)})
    p = pixels(e)
    want = {(cx + rel[0] + i, 91 + rel[1] + j) for i in range(2) for j in range(2)}
    check('range dot centre %d k=%d: x %d..%d rows %d..%d' % (cx, k, rel[0], rel[0] + 1, rel[1], rel[1] + 1), set(p) == want and set(p.values()) == {13}, sorted(p))

# 17. data: lock circle list 20 pairs + terminator; dot list 19 pairs + terminator; then a second terminator pair and a 23-pair list
def spairs(a, n):
    return [tuple(v - 256 if v > 127 else v for v in IMGB[a - 0x10000 + 2 * i:a - 0x10000 + 2 * i + 2]) for i in range(n)]
check('lock circle list 0x39408: 20 pairs (0,12)..(14,0) and the terminator at 0x3942E', spairs(0x39408, 21)[0] == (0, 12) and spairs(0x39408, 21)[19] == (14, 0) and spairs(0x39408, 21)[20] == (-1, -1))
check('dot list 0x39432: 19 pairs (0,11)..(13,0) and the terminator at 0x39458', spairs(0x39432, 20)[0] == (0, 11) and spairs(0x39432, 20)[18] == (13, 0) and spairs(0x39432, 20)[19] == (-1, -1))
sec = spairs(0x3945a, 25)
check('0x3945A..0x3948C: a second terminator pair, 23 pairs (0,14)..(16,0) from 0x3945C, terminator at 0x3948A', sec[0] == (-1, -1) and sec[1] == (0, 14) and sec[23] == (16, 0) and sec[24] == (-1, -1)
      and all(p != (-1, -1) for p in sec[1:24]))

print('failures', fails)
sys.exit(1 if fails else 0)
