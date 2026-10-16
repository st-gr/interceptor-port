"""The camera mover (the "tracking cameras" of camera modes 2 to 6), the mission-approach target choice and the fixed camera
(docs/re/07-ai-and-weapons.md, section 6) in the Musashi rig against a model written from the note.

    python3 camera_check.py [n] [seed]      (run under ulimit -v 2500000 and timeout)

  1. the mover `0x2D8E8` (stages 0 to 8 of the stage machine `V:4A86E`): random camera and target positions around every threshold, the stage, the
     flags, the arrival counter, the damped step and the camera offsets compared;
  2. the approach target choice `0x2DA7C` over random lists and records;
  3. the fixed camera `0x1E3EE` for every start location 0 to 4, with random carrier records.
Prints the counts; exit 1 on a difference.  Skips with exit 0 when extracted/interceptor.bin or librig.so is missing.
"""
import os, sys, random, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped (missing extracted/interceptor.bin or librig.so)"); sys.exit(0)
N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
img = open(IMGPATH, 'rb').read()
def iw(a): return struct.unpack('>H', img[a-0x10000:a-0x10000+2])[0]
def isw(a): return struct.unpack('>h', img[a-0x10000:a-0x10000+2])[0]
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v
LO, HI = 0x4A6E8, 0x54EC0
P = 0x4B23C
rig = Rig(); rig.reset()
def fresh():
    rig.reset()
    for a in (0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x1F1D6): rig.poke(a, b'\x4e\x75')
class M:
    def __init__(s): s.b = bytearray(rig.peek(LO, HI - LO))
    def u8(s, a): return s.b[a-LO]
    def w8(s, a, v): s.b[a-LO] = v & 0xFF
    def u16(s, a): return (s.b[a-LO] << 8) | s.b[a-LO+1]
    def w16(s, a, v): s.b[a-LO] = (v >> 8) & 0xFF; s.b[a-LO+1] = v & 0xFF
    def s32(s, a): return s32(struct.unpack('>I', bytes(s.b[a-LO:a-LO+4]))[0])
    def w32(s, a, v): s.b[a-LO:a-LO+4] = struct.pack('>I', v & 0xFFFFFFFF)
def norm(x, y, z, length):
    rig.run(0x29C0E, d0=length, d5=x & 0xFFFFFFFF, d6=y & 0xFFFFFFFF, d7=z & 0xFFFFFFFF)
    return s32(rig.reg('d5')), s32(rig.reg('d6')), s32(rig.reg('d7'))
def frame_offset(rec, m, d3, d4, d5):
    rig.poke(rec, bytes(m.b[rec-LO:rec-LO+0x100]))
    rig.run(0x1D478, a1=rec, d3=d3 & 0xFFFF, d4=d4 & 0xFFFF, d5=d5 & 0xFFFF)
    return s32(rig.reg('d0')), s32(rig.reg('d1')), s32(rig.reg('d2'))

def mover(m):
    C = [m.s32(0x4ACF6 + 4*i) for i in range(3)]; T = [m.s32(0x4AD0E + 4*i) for i in range(3)]
    d = [s32(T[i] - C[i]) for i in range(3)]
    D = max(abs(v) for v in d)
    st = m.u8(0x4A86E)
    def blend():
        pl = [m.s32(P + 0x14), m.s32(P + 0x18), m.s32(P + 0x1C)]
        a = [s32(C[i] + pl[i]) >> 1 for i in range(3)]
        b = [s32(a[0] + C[0]) >> 1, s32(a[1] + 0) >> 1 if False else None, s32(a[2] + C[2]) >> 1]
        # 2db4c: d0 += camera x, d2 += camera z (the y is not added); then halved
        b = [s32(a[0] + C[0]) >> 1, a[1] >> 1, s32(a[2] + C[2]) >> 1]
        c = [s32(b[i] + a[i]) >> 1 for i in range(3)]
        e = [s32(c[i] + a[i]) >> 1 for i in range(3)]
        f = [s32(e[i] + c[i]) >> 1 for i in range(3)]
        return f
    def to_player():
        for i in range(3): m.w32(0x4AD0E + 4*i, m.s32(P + 0x14 + 4*i))
    def offset_target(k):
        loc = m.u8(0x4A900)
        if k == 4:
            d4 = 4
            if loc in (1, 2): d3, d5 = 6, 3
            else: d3, d5 = -6, -9
        else:
            d4 = 1; d5 = 6
            d3 = 6 if loc in (1, 2) else -6
        # 1D466: the point (d3, d4, d5) model units of the player frame, in the player's frame without the vertical scale: through the rig
        rig.poke(P, bytes(m.b[P-LO:P-LO+0x100]))
        rig.run(0x1D466, a1=P, d3=d3 & 0xFFFF, d4=d4 & 0xFFFF, d5=d5 & 0xFFFF)
        r = [s32(rig.reg('d0')), s32(rig.reg('d1')), s32(rig.reg('d2'))]
        for i in range(3): m.w32(0x4AD0E + 4*i, r[i])
        return d4
    def do_step(d4, check_arrival=True):
        if check_arrival and D <= 0x240:
            if m.u8(0x4A966) < 6: return
            m.w8(0x4A86E, 6); m.w8(0x4A95D, 5); m.w8(0x4A86C, 0)
            m.w8(0x4A95D, m.u8(0x4A95D) - 1)
            if m.u8(0x4A95D) > 0 and m.u8(0x4A95D) < 128: return
            m.w16(0x4A97E, m.u16(0x4A97E) | 2); m.w8(0x4A966, 0); return
        dd = list(d); DD = D
        while DD >= 0x4800:
            dd = [v >> 2 for v in dd]; DD >>= 2
        n = norm(dd[0], dd[1], dd[2], 0x200)
        n = [s32(((v & 0xFFFF) - 0x10000 if v & 0x8000 else (v & 0xFFFF)) << d4) for v in n]      # ext.l of the low words
        old = [m.s32(0x4AD02 + 4*i) for i in range(3)]
        if old[0] | old[1] | old[2]:
            n = [s32(old[i] + (s32(n[i] - old[i]) >> 1)) for i in range(3)]
        for i in range(3): m.w32(0x4AD02 + 4*i, n[i])
        newC = [s32(C[i] + n[i]) for i in range(3)]
        for i in range(3): m.w32(0x4ACF6 + 4*i, newC[i])
        off = [s32(-(newC[0] & 0x3FFFFF)), s32(-newC[1]), s32(-(newC[2] & 0x3FFFFF))]
        for i in range(3): m.w32(0x4ACEA + 4*i, off[i])
    def setT(v):
        for i in range(3): m.w32(0x4AD0E + 4*i, v[i])
    if st == 0:
        d4 = 13
        if D > 0x1C00000: return do_step(d4)
        m.w8(0x4A86C, 1); m.w8(0x4A865, 0); d4 = 11
        if D > 0xD00000: return do_step(d4)
        m.w8(0x4A86E, 1); setT(blend()); return do_step(d4)
    if st == 1:
        d4 = 11
        if D > 0x200000: return do_step(d4)
        d4 = 10
        if D > 0x60000: return do_step(d4)
        m.w8(0x4A86E, 2); setT(blend()); return do_step(d4)
    if st == 2:
        d4 = 9
        if D > 0x60000: setT(blend()); return do_step(d4)
        d4 = 7
        if D > 0x30000: setT(blend()); return do_step(d4)
        m.w8(0x4A86E, 3); to_player(); return do_step(d4)
    if st == 3:
        d4 = 7
        if D > 0x60000: to_player(); return do_step(d4)
        d4 = 6
        if D > 0x60000: to_player(); return do_step(d4)
        d4 = 5
        if D > 0xD000: to_player(); return do_step(d4)
        d4 = 3
        if D > 0x8000: to_player(); return do_step(d4)
        m.w8(0x4A86E, 4); offset_target(4); return do_step(d4)     # the routine restores d4
    if st == 4:
        d4 = 2
        if D > 0x2800: return do_step(d4)
        m.w8(0x4A86E, 5); offset_target(5); return do_step(d4)
    if st == 5: return do_step(1)
    if st == 6:
        m.w8(0x4A95D, m.u8(0x4A95D) - 1)
        if 0 < m.u8(0x4A95D) < 128: return
        m.w16(0x4A97E, m.u16(0x4A97E) | 2); m.w8(0x4A966, 0); return
    if st == 7:
        if 0 < m.u8(0x4A967) < 128: arrive = True
        else:
            d4 = 14
            if D > 0x800000: return do_step(d4, False)
            d4 = 12
            if m.s32(0x4AD06) >= s32(0xFFF00000): arrive = True
            elif D > 0x100000: return do_step(d4, False)
            else: arrive = True
        m.w8(0x4A966, 3); m.w8(0x4A86E, 8)
        setT((0x10C00000, 0x5000000, 0x11400000)); return
    if st == 8:
        d4 = 14
        if D > 0x800000: return do_step(d4, False)
        d4 = 12
        if D > 0x100000: return do_step(d4, False)
        m.w8(0x4A966, 4); return
    raise KeyError(st)

bad = n = 0; stages = {}
for it in range(N):
    fresh()
    st = rng.randrange(0, 9)
    thr = {0: (0x1C00000, 0xD00000), 1: (0x200000, 0x60000), 2: (0x60000, 0x30000), 3: (0x60000, 0xD000, 0x8000), 4: (0x2800,), 5: (0x240,), 6: (), 7: (0x800000, 0x100000), 8: (0x800000, 0x100000)}[st]
    t = rng.choice(thr) if thr else 1000
    Dv = max(0x100, t + rng.choice((-3000, -1, 0, 1, 3000, t // 3, -t // 3)))
    mode = rng.choice((6, 6, 2, 3, 0)) if st < 7 else rng.choice((2, 2, 3))
    cam = [rng.randint(0, 0x1FFFFFFF), rng.randint(0, 0x1000000), rng.randint(0, 0x1FFFFFFF)]
    ax = rng.randrange(3); sg = rng.choice((-1, 1))
    tgt = [s32(cam[i] + rng.randint(-Dv, Dv)) for i in range(3)]
    tgt[ax] = s32(cam[ax] + sg * Dv)
    m = M()
    m.w8(0x4A86E, st); m.w8(0x4A966, mode); m.w8(0x4A900, rng.choice((0, 1, 2, 3, 4))); m.w8(0x4A95D, rng.choice((5, 3, 1, 0)))
    m.w8(0x4A967, rng.choice((0, 0, 1, 0xFF)))
    for i in range(3):
        m.w32(0x4ACF6 + 4*i, cam[i]); m.w32(0x4AD0E + 4*i, tgt[i])
        m.w32(0x4AD02 + 4*i, rng.choice((0, rng.randint(-5000, 5000))))
        m.w32(P + 0x14 + 4*i, rng.randint(0, 0x1FFFFFFF))
    m.w16(0x4A996, 0)
    rig.poke(LO, bytes(m.b))
    m2 = M2 = M.__new__(M); m2.b = bytearray(m.b)
    rig.run(0x2D8E8, a6=0x710000)
    got = rig.peek(LO, HI - LO)
    rig.poke(LO, bytes(m.b))
    try:
        mover(m2)
    except KeyError:
        continue
    exp = bytes(m2.b)
    MASK = list(range(0x4AB00 - LO, 0x4AC80 - LO))
    g = bytes(0 if i in MASK else v for i, v in enumerate(got)); e = bytes(0 if i in MASK else v for i, v in enumerate(exp))
    n += 1; stages[st] = stages.get(st, 0) + 1
    if g != e:
        bad += 1
        if bad < 6:
            print("  differs: stage", st, "mode", mode, "D", hex(Dv), [(hex(LO + i), g[i], e[i]) for i in range(len(g)) if g[i] != e[i]][:6])
print("1. mover: %d cases, %d differences; per stage %s" % (n, bad, sorted(stages.items())))
bad1 = bad

# ---- 2. the approach target choice 0x2DA7C
def choose(m):
    m.w8(0x4A967, 1); m.w16(0x4AA72, 0); m.w8(0x4A86E, 7)
    a2 = m.s32(0x4A7F2) if False else struct.unpack('>I', bytes(m.b[0x4A7F2-LO:0x4A7F2-LO+4]))[0]
    sel = 0
    while isw(a2) >= 0:
        d3 = ((iw(a2 + 4) << 8) & 0xFFFF)
        d3 = (d3 + d3) & 0xFFFF
        a2 += 10
        rec = P + d3
        if m.u8(rec + 0x62) & 0xF0 != 0x10: continue
        if not (m.u8(rec + 1) & 0x40): continue
        sel = d3; m.w16(0x4AA72, d3)
        if not (m.u8(rec + 1) & 8): break
    if sel == 0:
        m.w16(0x4AA56, 0x3E)
        T = (0x10C00000, 0x5000000, 0x11400000)
    else:
        rec = P + sel
        m.w8(0x4A967, 0xFF)
        y = m.s32(0x4ACFA)
        T = (m.s32(rec + 0x14), s32(y - (y >> 2)), m.s32(rec + 0x1C))
    for i in range(3): m.w32(0x4AD0E + 4*i, T[i])
bad2 = n2 = 0
subs = []
for row in range(10):
    for v in range(3):
        L = 0x2DEDA + iw(0x2DEDA + 12*row + 4*v)
        for k in range(4): subs.append(L + iw(L + 2*k))
for it in range(max(300, N // 4)):
    fresh()
    m = M()
    sp = rng.choice(subs)
    struct.pack_into('>I', m.b, 0x4A7F2 - LO, sp)
    for slot in range(16):
        r = P + 0x200 * slot
        m.w8(r + 0x62, rng.choice((0x10, 0x12, 0x13, 0x20, 0x00)))
        m.w8(r + 1, rng.choice((0x40, 0x40, 0x48, 0x00, 0x08)))
        m.w32(r + 0x14, rng.randint(0, 0x1FFFFFFF)); m.w32(r + 0x1C, rng.randint(0, 0x1FFFFFFF))
    m.w32(0x4ACFA, rng.randint(0, 0x2000000))
    rig.poke(LO, bytes(m.b))
    m2 = M.__new__(M); m2.b = bytearray(m.b)
    rig.run(0x2DA7C, a6=0x710000)
    got = rig.peek(LO, HI - LO)
    choose(m2)
    exp = bytes(m2.b)
    g = bytes(0 if i in MASK else v for i, v in enumerate(got)); e = bytes(0 if i in MASK else v for i, v in enumerate(exp))
    n2 += 1
    if g != e:
        bad2 += 1
        if bad2 < 4: print("  differs (approach target):", hex(sp), [(hex(LO + i), g[i], e[i]) for i in range(len(g)) if g[i] != e[i]][:6])
print("2. approach target choice: %d cases, %d differences" % (n2, bad2))

# ---- 3. the fixed camera 0x1E3EE
def fixed(m):
    loc = m.u8(0x4A900)
    if loc >= 0x80: loc -= 0x100
    base = 0x47ADE + 16 * loc
    w = iw(base)
    if w & 0x8000:
        rec = P + (((w & 0x7FFF) << 8) * 2 & 0xFFFF)
        if m.u8(rec + 0x62) == 0x20: off = (-36, 47, -48)
        else: off = (0, 20, -106)
        pos = frame_offset(rec, m, *off)
    else:
        d = [isw(0x47B30 + 16 * loc + 2 * i) for i in range(6)]
        x = s32(d[0] << 22); z = s32(d[1] << 22)
        k = (d[2] * 4) & 0xFFFF
        x = s32(x + (isw(0x206F2 + k) << 8)); z = s32(z + (isw(0x206F2 + k + 2) << 8))
        x = s32(x + (d[4] << 8)); z = s32(z + (d[5] << 8))
        pos = (x, s32(d[3] << 8), z)
    for i in range(3): m.w32(0x4ACF6 + 4*i, pos[i])
    m.w32(0x4ACEA, s32(-(pos[0] & 0x3FFFFF))); m.w32(0x4ACEE, s32(-pos[1])); m.w32(0x4ACF2, s32(-(pos[2] & 0x3FFFFF)))
    m.w8(0x4AA54, m.u8(0x4AA54) | 4); m.w8(0x4A85E, 0xFF); m.w8(0x4A86D, 0)
    if m.u8(0x4A83D) == 0:
        m.w8(0x4A86C, 1); m.w8(0x4A865, 0)
        rec = P + (struct.unpack('>H', bytes(m.b[0x4A996-LO:0x4A996-LO+2]))[0])
        m.w16(0x4A9E2, struct.unpack('>H', bytes(m.b[rec+0x68-LO:rec+0x68-LO+2]))[0])
        m.w16(0x4A97E, m.u16(0x4A97E) | 2); m.w8(0x4A8EB, 0xFF)
    else:
        m.w8(0x4A85E, 0)
bad3 = n3 = 0
for it in range(max(200, N // 6)):
    fresh()
    m = M()
    m.w8(0x4A900, rng.choice((0, 1, 2, 3, 4)))
    m.w8(0x4A83D, rng.choice((0, 1, 2)))
    for slot in (14, 15, 0):
        r = P + 0x200 * slot
        m.w8(r + 0x62, rng.choice((0x20, 0x21, 0x11)))
        for o in (0x14, 0x18, 0x1C): m.w32(r + o, rng.randint(0, 0x1FFFFFFF))
        for o in (0x92, 0x94, 0x96, 0x98, 0x9A, 0x9C, 0x9E, 0xA0, 0xA2): m.w16(r + o, rng.randint(-16384, 16384))
        m.w16(r + 0x68, rng.randint(0, 28799))
    rig.poke(LO, bytes(m.b))
    m2 = M.__new__(M); m2.b = bytearray(m.b)
    rig.run(0x1E3EE, a6=0x710000)
    got = rig.peek(LO, HI - LO)
    rig.poke(LO, bytes(m.b))
    fixed(m2)
    exp = bytes(m2.b)
    # the key epilogue after the handler (0x1EF68) latches the key state: compare only the camera variables
    keep = [range(0x4ACEA, 0x4ACEA + 12), range(0x4ACF6, 0x4ACF6 + 12), [0x4A86D, 0x4A85E, 0x4A86C, 0x4A865], range(0x4A9E2, 0x4A9E4)]
    ok = all(got[a - LO] == exp[a - LO] for r_ in keep for a in r_) and (got[0x4AA54 - LO] & 4) == (exp[0x4AA54 - LO] & 4)
    n3 += 1
    if not ok:
        bad3 += 1
        if bad3 < 4: print("  differs (fixed camera):", m.u8(0x4A900), [(hex(a), got[a - LO], exp[a - LO]) for r_ in keep for a in r_ if got[a - LO] != exp[a - LO]][:6])
print("3. fixed camera: %d cases, %d differences" % (n3, bad3))
sys.exit(1 if bad1 or bad2 or bad3 else 0)
