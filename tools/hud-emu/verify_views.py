"""Snapshot checks of the view rules of notes 03 and 06 (round M3-T4): the external camera longs, the ring offset of the full-matrix directions, the head-hold byte
and the view-state words, read from the reference-emulator snapshots (no 68000 emulation).

Run from the repository root:  python3 tools/hud-emu/verify_views.py   (exit 0 = all pass, 1 = a check failed)
Inputs (git-ignored): extracted/interceptor.map.json and the snapshots under extracted/captures/views/ (views-ground.txt, views-airborne.txt) and extracted/captures/airborne.vasnap.
Missing inputs: prints SKIP and exits 0.
Checks:
  1. external snapshots: V:4ACEA/EE/F2 = the negated camera position (x and z masked to 22 bits), V:4AB1A/1E/22 equal them (0x1F1D8 with V:4A864 clear), V:4AB34 = the unmasked position,
     V:4AB2A/2C/2E the longs >> 8 as words, V:4AB30 the height long >> 8; V:4AB40/44/48 equal record 0's pitch, heading, roll words;
  2. ring offset on the snapshots with direction 0 or 4 (the full matrix at +0x92): camera position - record position = (matrix x scaled ring words) >> 4, the clamp taken into account;
  3. the aim pair V:4AB4C/4E of the level -1 chase (ext-L-1) is (720, 2240); V:4A85E is 1 in every external snapshot and 0 in the map snapshots only by the map code;
  4. the map snapshots: V:4A865 = 1, V:4A86C = 0, aim (7200, 0), V:4A83D nonzero, the camera x and z longs equal the viewed record's position, V:4ACFA is a map altitude and V:4ACEE its negation,
     the window's last row 179;
  3b. the target point of ext-L-1: record position plus (0, 0, 1) times the +0x92 matrix shifted right by 4, minus the camera, gives the aim pair (720, 2240) through the target rule; the shift 6
     gives (760, 2240), which the snapshot does not hold;
  5. head codes: HUD step and window row of c-KP6-1 to 4, c-KP7, c-KP1, c-KP2, and V:4A949 in 0..5 or 255.
"""
import os, sys, struct
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
V = ROOT + '/extracted/captures/views/%s.vasnap'
EXT = ['ext-L-1', 'ext-L2', 'ext-L4', 'a-ext0', 'a-ext2', 'a-ext4', 'a-ext8', 'e-L4-KP5', 'e-L4-KPDOT', 'e-L1', 'e-L2', 'e-L3', 'e-L4']
MAP = ['tower-1', 'tower-2', 'tower-low']
HEAD = {'c-KP6-1': (1, -7, 144), 'c-KP6-2': (2, -14, 144), 'c-KP6-3': (3, -21, 167), 'c-KP6-4': (4, 50, 167), 'c-KP7': (12, 50, 167), 'c-KP1': (13, 50, 167), 'c-KP2': (6, 50, 179)}
need = ['extracted/interceptor.map.json'] + ['extracted/captures/views/%s.vasnap' % n for n in EXT + MAP + list(HEAD)]
missing = [p for p in need if not os.path.exists(os.path.join(ROOT, p))]
if missing:
    print('SKIP verify_views: missing ' + ', '.join(missing)); sys.exit(0)
from snapmem import S

fails = 0
ringed = 0


def check(ok, what):
    global fails
    if not ok:
        fails += 1; print('FAIL', what)


def w32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


for n in EXT:
    s = S(V % n)
    pos = [s.l(0x4ACF6 + 4 * i) for i in range(3)]
    neg = [s.l(0x4ACEA + 4 * i) for i in range(3)]
    check(neg == [-(pos[0] & 0x3FFFFF), -pos[1], -(pos[2] & 0x3FFFFF)], n + ' negated masked position')
    check([s.l(0x4AB1A + 4 * i) for i in range(3)] == neg, n + ' camera longs equal the negated position')
    check([s.l(0x4AB34 + 4 * i) for i in range(3)] == pos, n + ' V:4AB34 unmasked position')
    check([s.w(0x4AB2A + 2 * i) for i in range(3)] == [((neg[i] >> 8) + 0x8000 & 0xFFFF) - 0x8000 for i in range(3)], n + ' words')
    check(s.l(0x4AB30) == neg[1] >> 8, n + ' height long')
    check([s.l(0x4AB40 + 4 * i) for i in range(3)] == [s.w(0x4B23C + 0x66 + 2 * i) for i in range(3)], n + ' record 0 angles in V:4AB40')
    check(s.u8(0x4A864) == 0 and s.u8(0x4A85E) == 1, n + ' V:4A864 clear, V:4A85E set')
    d, lvl, cls = s.u8(0x4A96A), s.u8(0x4A83D), s.u8(0x4B23C + 0x62)
    if d in (0, 4) and cls in (0x11, 0x10) and lvl in (0xFF, 1, 2, 3, 4):
        ringed += 1
        base = {0x11: 0x2D6A0, 0x10: 0x2D664}[cls]
        w = [s.w(base + 6 * d + 2 * j) for j in range(3)]
        if lvl == 1: w = [x + (x >> 1) for x in w]
        elif lvl in (2, 3, 4): w = [x << (lvl - 1) for x in w]
        m = [s.w(0x4B23C + 0x92 + 2 * i) for i in range(9)]
        out = [sum(m[3 * i + j] * w[j] for j in range(3)) >> 4 for i in range(3)]
        rec = [s.l(0x4B23C + 0x14 + 4 * i) for i in range(3)]
        exp = [rec[i] + out[i] for i in range(3)]
        exp[1] = max(exp[1], (7 << 8))
        check([w32(x) for x in exp] == pos, '%s ring offset %s != %s' % (n, exp, pos))
s = S(V % 'ext-L-1')
check((s.w(0x4AB4C), s.w(0x4AB4E)) == (720, 2240), 'ext-L-1 aim pair')
# the aim target point (note 03, "External camera aim"): the viewed record's position plus (0, 0, 1) rotated by the nine words at +0x92 and shifted right by 4, minus the camera longs; the
# pair of the first frame (a snap) is the target of that vector, and the shift 6 would give 760 instead of 720. The model is the rig-verified one of view_check.py.
img = ROOT + '/extracted/interceptor.bin'
if os.path.exists(img):
    sys.path.insert(0, ROOT + '/tools/m68k-rig/tests/lists_model')
    import view_check as vc
    tab = struct.unpack('>1024h', open(img, 'rb').read()[0x42C00 - 0x10000:0x42C00 - 0x10000 + 2048])
    rec = 0x4B23C + s.w(0x4A996)
    m = [s.w(rec + 0x92 + 2 * i) for i in range(9)]
    pos = [s.l(rec + 0x14 + 4 * i) for i in range(3)]
    cam = [s.l(0x4ACF6 + 4 * i) for i in range(3)]
    for shift, want in ((4, (720, 2240)), (6, (760, 2240))):
        pt = [pos[i] + (m[3 * i + 2] >> shift) for i in range(3)]       # the offset (0, 0, 1): only the third column of the matrix
        got = vc.aim_target(tab, *[w32(pt[i] - cam[i]) for i in range(3)])
        check(got == want, 'ext-L-1 target point at shift %d: %s, expected %s' % (shift, got, want))
for n in MAP:
    s = S(V % n)
    check(s.u8(0x4A865) == 1 and s.u8(0x4A86C) == 0 and (s.w(0x4AB4C), s.w(0x4AB4E)) == (7200, 0) and s.u8(0x4A83D) != 0, n + ' map state')
    # the map camera is directly above the viewed record (note 06 view item 2): x and z longs equal the record's position, the height is a map altitude (0x1000000 to 0x7000000 in odd steps
    # of 0x2000000), the negated masked copies V:4ACEA.. follow, the window's last row is 179
    rec = 0x4B23C + s.w(0x4A996)
    check([s.l(0x4ACF6), s.l(0x4ACFE)] == [s.l(rec + 0x14), s.l(rec + 0x1C)], n + ' map camera above the record')
    check(s.l(0x4ACFA) in (0x1000000, 0x3000000, 0x5000000, 0x7000000) and s.l(0x4ACEE) == -s.l(0x4ACFA), n + ' map altitude and its negated copy')
    check(s.w(0x4AA3C) == 179, n + ' map window row')
for n, (code, step, row) in HEAD.items():
    s = S(V % n)
    check((s.u8(0x4A85F), s.w(0x4AA3E), s.w(0x4AA3C)) == (code, step, row), n + ' head code, step, window row')
    check(s.u8(0x4A949) in (0, 1, 2, 3, 4, 5, 255), n + ' hold byte')
print('verify_views: %d ring-offset snapshots, %d failures' % (ringed, fails))
sys.exit(1 if fails else 0)
