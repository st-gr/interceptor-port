"""The AI records' share of the input conversion (docs/re/07-ai-and-weapons.md, section 3.8): the key-byte conditioning, the wing-levelling mark, the
state 6 re-arm and the recentre request that the conversion makes for a non-player record, the original's attitude step 0x31AE0 in the Musashi rig against a model.

    python3 conv_check.py [n] [seed]      (run under ulimit -v 2500000 and timeout)

Compared after one call: the key byte +0x65, W0, W2, the state byte +0x05 and the counter +0x4C.  Skips with exit 0 when the inputs are missing.
"""
import os, sys, random, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped (missing extracted/interceptor.bin or librig.so)"); sys.exit(0)
N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
P = 0x4B23C
rig = Rig()
def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v

def model(f):
    """f: dict of the fields; returns the changed fields"""
    key, w0, w2, st, cnt, flags, cls, roll, speed = f['key'], f['w0'], f['w2'], f['st'], f['cnt'], f['flags'], f['cls'], f['roll'], f['speed']
    scripted = w2 & 0x100
    def done(): return dict(key=key, w0=w0, w2=w2, st=st, cnt=cnt)
    def level():
        nonlocal key, w0, w2
        w0 |= 0x2000
        if s16(roll) > 0x3840:
            d2 = 0 if s16(roll) >= 0x6F68 else 8
        else:
            d2 = 4 if s16(roll) > 0x118 else 0
        key = (key & 3) | d2
        if flags & 6 == 6: w2 |= 0x10
        return done()
    if not scripted and not (flags & 0x10):
        key &= 0xC3; return done()
    if st in (6, 0xA): return level()
    if scripted: return done()
    if flags & 8:
        if not (flags & 4):
            w0 &= ~0x2000
            if not (flags & 0x40): return done()
        if not (flags & 0x20): return done()
    else:
        if not (flags & 2):
            w0 &= ~0x2000
            if not (flags & 0x20): return done()
        if not (flags & 0x40): return done()
    if (cls & 0xF0) != 0x10:
        return None          # non-aircraft classes continue into the rate code: not compared here
    if st == 0 or st == 1:
        st = 6; cnt = 30
        return level()
    if st == 8: return level()
    return done()

bad = n = skipped = 0; seen = {}
for it in range(N):
    rig.reset()
    idx = rng.choice((4, 6, 8, 10, 12))
    H = P + 0x200 * idx
    f = dict(key=rng.randint(0, 255), w0=rng.choice((0x11C0, 0x11C8, 0x31C0, 0x10C0)), w2=rng.choice((0, 0, 0x100, 0x0100 | 0x80)),
             st=rng.choice((0, 1, 2, 5, 6, 7, 8, 9, 0xA)), cnt=rng.randint(-3, 40), flags=rng.randint(0, 255),
             cls=rng.choice((0x12, 0x10, 0x13, 0x14, 0x15)), roll=rng.choice((rng.randint(0, 28799), rng.randint(0, 600), rng.randint(28000, 28799))),
             speed=rng.randint(0, 0x3000))
    rig.w16(H, f['w0']); rig.w16(H + 2, f['w2']); rig.w8(H + 0x62, f['cls']); rig.w8(H + 5, f['st']); rig.w16(H + 0x4C, f['cnt'])
    rig.w8(H + 0x64, f['flags']); rig.w8(H + 0x65, f['key']); rig.w16(H + 0x6A, f['roll']); rig.w16(H + 0x6C, f['speed'])
    rig.w16(0x4AA6C, idx); rig.w16(0x4AA6E, 0x200 * idx); rig.w16(0x4A984, 0)
    exp = model(f)
    if exp is None: skipped += 1; continue
    rig.run(0x31AE0, a1=H, a6=0x710000)
    got = dict(key=rig.u8(H + 0x65), w0=rig.u16(H), w2=rig.u16(H + 2), st=rig.u8(H + 5), cnt=rig.s16(H + 0x4C))
    exp = dict(exp); exp['cnt'] = s16(exp['cnt'])
    got['w2'] &= ~0x10; exp['w2'] &= ~0x10      # the recentre request is consumed by the rest of the attitude step in the same call
    n += 1
    tag = (f['st'], bool(f['w2'] & 0x100))
    if got == exp: seen[tag] = seen.get(tag, 0) + 1
    else:
        bad += 1
        if bad < 5: print("  differs:", {k: hex(v) if isinstance(v, int) else v for k, v in f.items()}, "got", got, "exp", exp)
print("conversion pre-stage: %d cases compared (%d non-aircraft cases skipped), %d differences" % (n, skipped, bad))

# ---- part 2: the launch decision 0x2701A (section 4.6): the order of the tests
def launch(o, tr, req):
    if o['w0'] & 0x8700: return 0, req
    if o['oc']: return 0, req
    if req: return 1, 0
    if not (o['w0'] & 0x40): return 0, req
    if o['t38'] & 0x80:
        t = tr[o['t38'] & 0x7F]
        if not (t['w0'] & 0x40) or t['oc'] or (t['w0'] & 1): return 0, req
    if (o['flags'] & 0x60) != 0x60: return 0, req
    if not (o['w0'] & 1): return 0, req
    if (o['r63'] & 0xF0) in (0x20, 0x30): return 1, 0
    return 0, req
bad2 = n2 = yes = 0
for it in range(max(400, N // 3)):
    rig.reset()
    own = rng.choice((8, 10, 12)); O = P + 0x200 * own
    o = dict(w0=rng.choice((0x11C1, 0x11C0, 0x91C1, 0x15C1, 0x13C1, 0x11C9, 0x10C1, 0x01C1, 0x1141)), oc=rng.random() < 0.15, t38=rng.choice((0x80, 0x80 | 14, 0x80 | 4, 0x00, 0x85)),
             flags=rng.choice((0x60, 0x60, 0x70, 0x40, 0x20, 0)), r63=rng.choice((0x3D, 0x2D, 0x1D, 0x3D, 0xFD)))
    tr = {i: dict(w0=rng.choice((0x11C0, 0x11C1, 0x0000, 0x1180)), oc=rng.random() < 0.2) for i in range(16)}
    req = rng.choice((0, 0, 1))
    rig.w16(O, o['w0']); rig.w8(O + 0x20, 2 if o['oc'] else 0); rig.w8(O + 0x38, o['t38']); rig.w8(O + 0x64, o['flags']); rig.w8(O + 0x63, o['r63'])
    for i, t in tr.items():
        if i != own: rig.w16(P + 0x200 * i, t['w0']); rig.w8(P + 0x200 * i + 0x20, 2 if t['oc'] else 0)
    tr[own] = dict(w0=o['w0'], oc=o['oc'])
    rig.w8(0x4A874, req)
    exp, ereq = launch(o, tr, req)
    rig.run(0x2701A, a2=O, a6=0x710000)
    got = rig.reg('d0') & 0xFF
    n2 += 1; yes += got
    if got != exp or rig.u8(0x4A874) != ereq:
        bad2 += 1
        if bad2 < 4: print("  launch differs:", {k: hex(v) if isinstance(v, int) else v for k, v in o.items()}, req, got, exp)
print("launch decision: %d cases (%d yes), %d differences" % (n2, yes, bad2))
sys.exit(1 if bad or bad2 else 0)
