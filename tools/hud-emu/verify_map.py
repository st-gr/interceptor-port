"""Verification of the map renderer (0x2FC58) of note 06: overlay of the three tower frames, line endpoints, label rule,
mark pens, hiding rule and heading-to-shape mapping.

Run from the repository root:  python3 tools/hud-emu/verify_map.py   (exit 0 = all pass, 1 = a check failed)
Inputs (git-ignored, under extracted/):
  interceptor.bin, interceptor.map.json                      relocated image (tools/hunk/relocate.py)
  captures/views/tower-1.vasnap and tower-1.ppm              map view at altitude 0x3000000 (also the base of the synthetic-entity checks)
  captures/views/tower-2.vasnap and tower-2.ppm              map view at 0x1000000
  captures/views/tower-low.vasnap and tower-low.ppm          map view at 0x7000000
If any of these is missing the script prints SKIP and exits 0.
"""
import os, sys, struct
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
_need = ['extracted/interceptor.bin', 'extracted/interceptor.map.json'] + ['extracted/captures/views/tower-%s.%s' % (n, x) for n in ('1', '2', 'low') for x in ('vasnap', 'ppm')]
_missing = [p for p in _need if not os.path.exists(os.path.join(ROOT, p))]
if _missing:
    print('SKIP verify_map: missing ' + ', '.join(_missing))
    sys.exit(0)
from emu3 import *
from unicorn.m68k_const import *
from PIL import Image
P = pal()
RP = {p: i for i, p in reversed(list(enumerate(P)))}
VIEWS = ROOT + '/extracted/captures/views/%s'
SHAPES = 0x2FED8
SHIP = 0x30012

def capimg(path):
    im = Image.open(path).convert('RGB').crop((186, 42, 826, 242)).resize((320, 200), Image.NEAREST)
    return [[RP.get(im.getpixel((x, y)), -1) for x in range(320)] for y in range(200)]

def s16(v):
    v &= 0xFFFF
    return v - 65536 if v > 32767 else v

def tdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q

def run_map(name, setup=None):
    e = Emu3(VIEWS % (name + '.vasnap'))
    for p in e.pl[:4]:
        e.uc.mem_write(p, bytes(8000))
    if setup:
        setup(e)
    lines = []
    def hook(uc, a, sz, ud):
        if a == e.R(0x3455E):
            r = lambda k: s16(uc.reg_read(k))
            lines.append((r(UC_M68K_REG_D0), r(UC_M68K_REG_D1), r(UC_M68K_REG_D2), r(UC_M68K_REG_D3)))
    e.uc.hook_add(UC_HOOK_CODE, hook)
    e.glyphs = set()
    err = e.call(0x2FC58, maxi=20000000)
    assert err is None, err
    return e, lines

def model_lines(cx, cy, cz):
    """grid line positions from the rule in the note: returns ({i: x}, {j: y_call})"""
    camx, camy, camz = s16(cx >> 16), s16(cy >> 16), s16(cz >> 16)
    Z = (-128 * s16(-camy)) >> 8
    V, H = {}, {}
    for i in range(24):
        X = (168 * s16(0xF680 + 0x1C0 * i - camx)) >> 8
        V[i] = 319 - (160 + tdiv(160 * X, Z))
    for j in range(16):
        Y = (252 * s16(0xFEB0 + 0x230 * j - camz)) >> 8
        H[j] = 179 - (90 + tdiv(90 * Y, Z))
    return V, H

def model_labels(V, H):
    out = set()
    for i in range(1, 24, 2):          # odd lines carry labels 115 + (i - 1) / 2
        x = V[i]
        if not (0 <= x <= 319) or x - 12 < 8:
            continue
        d0 = min(x - 12, 300)
        for k, ch in enumerate('%03d' % (115 + (i - 1) // 2)):
            out.add((174, 4 * (d0 >> 2) + 8 + 4 * k, ch))
    for j in range(1, 16, 2):
        y = H[j]
        if not (0 <= y <= 179):
            continue
        d1 = max(4, min(175, y - 1))
        for k, ch in enumerate('%02d' % (34 + (j - 1) // 2)):
            out.add((d1, 8 + 4 * k, ch))
    return out

ok = True
def check(label, cond, detail=''):
    global ok
    print('%-60s %s %s' % (label, 'ok' if cond else 'FAIL', detail))
    ok = ok and cond

for n in ('tower-1', 'tower-2', 'tower-low'):
    e, L = run_map(n)
    im = e.img()
    cap = capimg(VIEWS % (n + '.ppm'))
    ov = [(x, y, im[y][x]) for y in range(180) for x in range(320) if im[y][x]]
    bad = sum(1 for x, y, v in ov if cap[y][x] != (0 if v == 3 else v))
    check('%s overlay pixels equal to capture' % n, bad == 0, '(%d pixels, %d differ)' % (len(ov), bad))
    cx, cy, cz = e.rl(0x4ACF6), e.rl(0x4ACFA), e.rl(0x4ACFE)
    V, H = model_lines(cx, cy, cz)
    vs = sorted({l[0] for l in L if l[1] == 0 and l[3] == 179 and l[0] == l[2]})
    hs = sorted({l[1] for l in L if l[0] == 0 and l[2] == 319 and l[1] == l[3]})
    check('%s vertical line columns = model' % n, vs == sorted(x for x in V.values() if 0 <= x <= 319), str(vs))
    check('%s horizontal line rows (call y) = model' % n, hs == sorted(y for y in H.values() if 0 <= y <= 179), str(hs))
    got = {(r, x, c) for (r, x, c) in e.glyphs}
    check('%s label glyphs = model' % n, got == model_labels(V, H), '(%d glyphs)' % len(got))

# marks: pens, hide rule, heading to shape
def shape_pixels(addr, cx, cy):
    o = addr - 0x10000
    img = open(ROOT + '/extracted/interceptor.bin', 'rb').read()
    px = set()
    while img[o] != 0x80:
        dy, dx, ln = struct.unpack('bbb', img[o:o + 3])
        o += 3
        for k in range(ln):
            px.add((cx + dx + k, cy + dy + 1))     # horizontal runs are drawn one row below
    return px

IMGB = open(ROOT + '/extracted/interceptor.bin', 'rb').read()
offs = [struct.unpack('>h', IMGB[SHAPES - 0x10000 + 2 * i:SHAPES - 0x10000 + 2 * i + 2])[0] for i in range(16)]
camx, camz = 0x10545920, 0x10A404F0
def ent(e, i, w0, cls, x, z, b3=0, b20=0x40, hd=0):
    b = 0x4B23C + 0x200 * i
    e.w(b, w0); e.b(b + 3, b3); e.b(b + 0x62, cls); e.b(b + 0x20, b20)
    e.l(b + 0x14, x); e.l(b + 0x1C, z); e.w(b + 0x68, hd); e.b(b + 0x71, 0xFF); e.b(b + 0x70, 0)
def clear_others(e):
    for i in range(1, 16):
        e.w(0x4B23C + 0x200 * i, 0x10)
def one(w0, cls, hd, b3=0, b20=0x40, player=False, n=115):
    def s(e):
        clear_others(e)
        e.w(0x4A992, n)
        if not player:
            e.w(0x4B23C, 0x11C8); e.b(0x4B23C + 3, 0x80)           # hide the player
        else:
            e.b(0x4B23C + 3, 0)
        ent(e, 3, w0, cls, camx + 0x200000, camz + 0x100000, b3, b20, hd)
    e, L = run_map('tower-1', s)
    im = e.img()
    return e, im
# screen centre of the mark: x 159 - 160*X/Z, y 90 - 90*Y/Z
Zc = 384
X = (168 * 32) >> 8; Y = (252 * 16) >> 8
mx = 319 - (160 + tdiv(160 * X, Zc)); my = 179 - (90 + tdiv(90 * Y, Zc)) + 1
for label, w0, cls, pen, b3, b20 in (('friendly (bit 3 of byte 1)', 0x4048, 0x10, 13, 0, 0x40), ('hostile', 0x4040, 0x10, 1, 0, 0x40)):
    e, im = one(w0, cls, 0, b3, b20)
    px = {(x, y) for y in range(180) for x in range(320) if im[y][x] == pen}
    check('mark %s: pen %d, shape 0 at the projected point' % (label, pen), px == shape_pixels(SHAPES + offs[0], mx, my))
e, im = one(0x4048, 0x10, 0, b3=0x80)
check('friendly with bit 7 of byte 3 set is not drawn', not any(im[y][x] == 13 for y in range(180) for x in range(320)))
e, im = one(0x4040, 0x10, 0, b3=0x80)
check('hostile with bit 7 of byte 3 set is still drawn', any(im[y][x] == 1 for y in range(180) for x in range(320)))
e, im = one(0x4040, 0x10, 0, n=114)
check('marks are drawn on odd frame counts only', not any(im[y][x] == 1 for y in range(180) for x in range(320)))
e, im = one(0x0008, 0x10, 0)
check('entity without bit 6 of word 0 is not drawn', not any(im[y][x] in (1, 13) for y in range(180) for x in range(320)))
for hd, want in ((0, 0), (899, 0), (900, 1), (2699, 1), (2700, 2), (14400, 8), (27899, 15), (27900, 0), (28799, 0)):
    e, im = one(0x4040, 0x10, hd)
    px = {(x, y) for y in range(180) for x in range(320) if im[y][x] == 1}
    got = [k for k in range(16) if px == shape_pixels(SHAPES + offs[k], mx, my)]
    check('heading word %d draws shape %d' % (hd, want), got == [want], str(got))
def player(b3, hd):
    def st(e):
        clear_others(e)
        e.w(0x4A992, 115)
        e.w(0x4B23C, 0x11C8); e.b(0x4B23C + 3, b3); e.w(0x4B23C + 0x68, hd)
    e, L = run_map('tower-1', st)
    return e.img()
im = player(0x00, 2240)
px = {(x, y) for y in range(180) for x in range(320) if im[y][x] == 13}
check('airborne player: pen 13 shape of heading 2240 (sector 1) at (159, 90)', px == shape_pixels(SHAPES + offs[1], 159, 90))
im = player(0x80, 2240)
check('player on the ground (bit 7 of byte 3): no mark', not any(im[y][x] == 13 for y in range(180) for x in range(320)))
print('ALL OK' if ok else 'FAILURES')
sys.exit(0 if ok else 1)
