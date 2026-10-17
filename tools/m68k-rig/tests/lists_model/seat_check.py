"""The ejection (docs/re/07-ai-and-weapons.md, section 6): the seat's own step 0x27BFA against a model, the nibble cycle and the seat spawn in the
whole world update, in the Musashi rig.

    python3 seat_check.py [n] [seed]      (run under ulimit -v 2500000 and timeout)

  1. 0x27BFA (the step of the classes 0x30 to 0x32) over random records, velocities, counters, hit and dying bits, player words and request bytes:
     the whole variable block compared with the model (view switch off) and, with the view switch on, the record and the request bytes;
  2. the nibble cycle of the player's byte +0x7C and the spawn of the seat record in the world update 0x26AF8 (step by step from the key's value 10,
     with and without the release bit W2 0x0020): the nibble per step, the step of the spawn, the fields of the new record.
Prints the counts; exit 1 on a difference.  Skips with exit 0 when extracted/interceptor.bin or librig.so is missing.
"""
import os, sys, random, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped (missing extracted/interceptor.bin or librig.so)"); sys.exit(0)
N = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v
LO, HI = 0x4A6E8, 0x54EC0
P = 0x4B23C
rig = Rig(); rig.reset()
STUBS = (0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x1F1D6, 0x1A0DC, 0x1A042, 0x1EC14, 0x1E4D2)
class M:
    def __init__(s): s.b = bytearray(rig.peek(LO, HI - LO))
    def u8(s, a): return s.b[a-LO]
    def w8(s, a, v): s.b[a-LO] = v & 0xFF
    def u16(s, a): return (s.b[a-LO] << 8) | s.b[a-LO+1]
    def w16(s, a, v): s.b[a-LO] = (v >> 8) & 0xFF; s.b[a-LO+1] = v & 0xFF
    def s32(s, a): return s32(struct.unpack('>I', bytes(s.b[a-LO:a-LO+4]))[0])
    def w32(s, a, v): s.b[a-LO:a-LO+4] = struct.pack('>I', v & 0xFFFFFFFF)

def seat_step(m, a, view_on):
    """model of 0x27BFA for the record at a; returns d0"""
    if m.u8(0x4A840) | m.u8(0x4A866): return 1
    w0 = m.u16(a)
    cls = m.u8(a + 0x62)
    if w0 & 0x400:
        m.w16(P + 2, m.u16(P + 2) | 0x20); m.w16(a, 0); m.w8(0x4A910, 0x0C); return 0
    vx, vy, vz = m.s32(a + 0x3E), m.s32(a + 0x42), m.s32(a + 0x46)
    c = s16(m.u16(a + 0x4C))
    def store():
        m.w32(a + 0x3E, vx); m.w32(a + 0x42, vy); m.w32(a + 0x46, vz)
        mx = max(abs(vx), abs(vy), abs(vz))
        m.w16(a + 0x6E, (mx >> 2) & 0xFFFF)
        return 1
    if w0 & 0x8000:
        if c > 0: m.w16(a + 0x4C, c - 1 if c < 5 else 4)
        if s16(m.u16(a + 0x4C)) <= 0 and cls == 0x30 and m.u8(0x4A8E2) == 0:
            m.w8(0x4A8E2, 2); m.w8(0x4A8E4, 4)
        if vx < 0xC0: vx = s32(vx + 0xC)
        vy = 0
        vz = vz - 1 if vz > 0 else vz + 1
        return store()
    if c < 0:
        m.w16(a + 0x4C, c + 1); d6 = 6; step = 'cc8'
    else:
        m.w16(a + 0x4C, c + 1); d6 = 2
        c1 = s16(m.u16(a + 0x4C))
        if c1 < 3: step = 'd16'
        else:
            d6 = 3
            if cls != 0x30: step = 'd16'
            else:
                d4 = m.u16(P)
                if not (d4 & 0x40): step = 'ce6'
                elif not (d4 & 4): step = 'cc8'
                else: step = 'ce6' if c1 < 5 else 'd16'
    if step == 'cc8':
        if m.u16(P) & 0x600: step = 'ce6'
        elif m.s32(a + 0x18) < 0x2000: step = 'ce6'
        elif s16(m.u16(a + 0x4C)) < 0x14: step = 'd16'
        else: step = 'ce6'
    if step == 'ce6':
        if view_on and m.u8(0x4A86D) and m.u16(0x4AA6C) != m.u16(0x4A994):
            m.w8(0x4A891, 0)       # the stubbed view routines change nothing else
        step = 'd16'
    c2 = s16(m.u16(a + 0x4C))
    d4 = abs(vx); d5 = abs(vz)
    if d5 < d4: d4, d5 = d5, d4
    t = s16(0xF0 - c2)
    if t <= 0: t = 0
    if not (d5 < t):
        vx = s32(vx - (vx >> d6)); vz = s32(vz - (vz >> d6))
    if c2 >= 3 and vy > 0: vy = s32(vy - 0x3C0)
    elif vy > -0x300: vy = s32(vy - 0x3C)
    else: vy = s32(vy - 0x300) >> 1
    return store()

bad = n = 0; branch = {}
for it in range(N):
    rig.reset()
    for s in STUBS: rig.poke(s, b'\x4e\x75')
    m = M()
    slot = rng.choice((1, 2, 3))
    a = P + 0x200 * slot
    cls = rng.choice((0x30, 0x30, 0x31, 0x32))
    m.w8(a + 0x62, cls)
    m.w16(a, rng.choice((0x10CA, 0x10CA, 0x80CA, 0x10DA, 0x04CA, 0x90CA)))
    m.w16(a + 0x4C, rng.choice((-20, -5, -1, 0, 1, 2, 3, 4, 5, 20, 100, rng.randint(-30, 130))))
    m.w32(a + 0x18, rng.choice((0, 0x1000, 0x2000, 0x2001, rng.randint(0, 0x100000))))
    for o in (0x3E, 0x42, 0x46): m.w32(a + o, rng.choice((0, rng.randint(-3000, 3000), rng.randint(-100000, 100000), 0xC0, -0x300, -0x2FF)))
    m.w16(P, rng.choice((0x11C8, 0x51CC, 0x11C0, 0x1188, 0x0200, 0x10C8 | 0x0600, 0x11CC)))
    m.w16(P + 2, rng.choice((0, 0x2000, 0x0020)))
    m.w8(0x4A840, rng.choice((0, 0, 0, 1))); m.w8(0x4A866, rng.choice((0, 0, 0, 0xFF)))
    m.w8(0x4A8E2, rng.choice((0, 0, 1, 2)))
    view_on = rng.random() < 0.3
    m.w8(0x4A86D, 1 if view_on else 0); m.w16(0x4AA6C, slot); m.w16(0x4A994, rng.choice((0, slot, 5)))
    rig.poke(LO, bytes(m.b))
    m2 = M.__new__(M); m2.b = bytearray(m.b)
    rig.run(0x27BFA, a1=a, a2=P, a6=0x710000)
    got = rig.peek(LO, HI - LO); gd0 = rig.reg('d0') & 0xFF
    ret = seat_step(m2, a, view_on)
    exp = bytes(m2.b)
    mask = set(range(0x4AB00 - LO, 0x4AC80 - LO))
    if view_on: mask |= {0x4A891 - LO, 0x4A910 - LO}
    g = bytes(0 if i in mask else v for i, v in enumerate(got)); e = bytes(0 if i in mask else v for i, v in enumerate(exp))
    n += 1
    if g != e or gd0 != (ret & 0xFF):
        bad += 1
        if bad < 5: print("  differs:", hex(cls), hex(m.u16(a)), m.u16(a + 0x4C), gd0, ret, [(hex(LO + i), g[i], e[i]) for i in range(len(g)) if g[i] != e[i]][:6])
print("1. seat step: %d cases, %d differences" % (n, bad))

# ---- 2. the nibble cycle and the spawns in the world update
def base():
    rig.reset()
    for a in STUBS: rig.poke(a, b'\x4e\x75')
    rig.w8(0x4A95E, 1); rig.w16(0x4A984, 0x0840); rig.w8(0x4A83D, 0); rig.w16(0x4A994, 0); rig.w16(0x4A996, 0)
    rig.w8(0x4A844, 1); rig.w32(0x4A7B2, 0x800000)
    for a in range(0x4A83C, 0x4A848):
        if a not in (0x4A83D, 0x4A83E, 0x4A840): rig.w8(a, 1)
    rig.w32(0x1D024, 0x700000)
    rig.w16(P, 0x11C8); rig.w8(P + 0x62, 0x11); rig.w32(P + 0x18, 0x2000000); rig.w32(P + 0x10, 0x20000)
    rig.w16(P + 0x6C, 0x1200); rig.w16(P + 0x6E, 0x1200); rig.w8(P + 0x7D, 3)
    for o in (0x92, 0x9A, 0xA2, 0x80, 0x88, 0x90): rig.w16(P + o, 0x4000)

def run_cycle(passes, press=None, release_at=None):
    """returns the list of the player's nibble after each pass and the events"""
    base(); rig.w8(P + 0x7C, 0xE0 | 0x0A)
    seq = []; ev = {}
    for k in range(passes):
        if press and k in press: rig.w8(P + 0x7C, rig.u8(P + 0x7C) | 0x0A)
        if release_at is not None and k == release_at: rig.w16(P + 2, rig.u16(P + 2) | 0x20)
        rig.run(0x26AF8, a6=0x710000)
        seq.append(rig.u8(P + 0x7C) & 15)
        r1 = P + 0x200
        st = (hex(rig.u16(r1)), hex(rig.u8(r1 + 0x62)), rig.u16(0x4AA7A), rig.u16(0x4A97A))
        if st != ev.get('last'): ev[k] = st; ev['last'] = st
    ev.pop('last')
    return seq, ev
seq, ev = run_cycle(14)
want = [9, 8, 7, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6]
ok_a = seq == want
print("2a. nibble per pass from the key's value 10:", seq, "as stated" if ok_a else "DIFFERENT")
print("    record 1 / latch events:", ev)
ok_b = (5 in ev and ev[5][1] == '0x30' and ev[5][2] == 0x200 and ev[5][3] == 0)
seq2, ev2 = run_cycle(14, release_at=9)
ok_c = seq2[:9] == want[:9] and seq2[9] == 0 and (rig.u16(P + 2) & 0x20) == 0
print("2b. W2 0x0020 set in pass 9 (the hold at 6): nibble per pass", seq2[8:12], "and the bit cleared by the step:", "as stated" if ok_c else "DIFFERENT")
seq3, ev3 = run_cycle(8, press={3})      # a second press while the nibble is 7: 7 | 0x0A = 15
print("2c. second key press while the nibble is 7: nibble per pass", seq3, "record 1:", ev3)
ok_d = any(v[1] == '0x31' and v[3] == 0x200 for k, v in ev3.items())

# ---- 3. the spawn routines 0x27522 (seat) and 0x2758E (parachute) called directly
def norm(x, y, z, length):
    rig.run(0x29C0E, d0=length, d5=x & 0xFFFFFFFF, d6=y & 0xFFFFFFFF, d7=z & 0xFFFFFFFF)
    return s32(rig.reg('d5')), s32(rig.reg('d6')), s32(rig.reg('d7'))
def lw(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
bad3 = n3 = 0
img = open(IMGPATH, 'rb').read()
def isw(a): return struct.unpack('>h', img[a-0x10000:a-0x10000+2])[0]
for it in range(max(60, N // 10)):
    rig.reset()
    for s_ in STUBS: rig.poke(s_, b'\x4e\x75')
    m = M()
    slot = rng.choice((1, 2, 3)); a = P + 0x200 * slot
    parachute = rng.random() < 0.4
    for o in range(0, 0xA4, 2): m.w16(P + o, rng.randint(0, 65535))
    for o in (0x92, 0x94, 0x96, 0x98, 0x9A, 0x9C, 0x9E, 0xA0, 0xA2): m.w16(P + o, rng.randint(-16384, 16384))
    for o in (0x14, 0x18, 0x1C): m.w32(P + o, rng.randint(0, 0x1FFFFFFF))
    for o in (0x3E, 0x42, 0x46): m.w32(P + o, rng.randint(-5000, 5000))
    m.w16(P + 2, rng.choice((0x0080, 0x0000, 0x2084)))
    m.w16(0x4AA6C, slot); m.w8(0x4A83D, 1)
    rig.poke(LO, bytes(m.b)); m2 = M.__new__(M); m2.b = bytearray(m.b)
    rig.run(0x2758E if parachute else 0x27522, a1=a, a2=P, a6=0x710000)
    got = rig.peek(LO, HI - LO); gd = rig.peek(0x2600C + 20 * slot, 20)
    # the model of the fields that do not depend on the attitude composition
    cls = 0x31 if parachute else 0x30
    for o in range(0, 0xA4): m2.b[a + o - LO] = m2.b[P + o - LO]
    m2.w16(a, (m2.u16(P) & 0x7FFF) | 0x10C0 | 0x0100 | 0x0002)
    m2.w8(a + 0x62, cls)
    for o in (0x56, 0x58, 0x5A): m2.w16(a + o, 0)
    m2.w8(a + 0x64, 0)
    m2.w16(a + 2, m2.u16(a + 2) & ~0x0004)
    ent = 0 if cls == 0x30 else 1
    off = [isw(0x2789E + 6 * ent + 2 * i) for i in range(3)]
    pl = [[lw(m.u16(P + 0x92 + 6 * r + 2 * c)) for c in range(3)] for r in range(3)]
    for r, o in enumerate((0x14, 0x18, 0x1C)):
        m2.w32(a + o, s32(m.s32(P + o) + (sum(off[c] * pl[r][c] for c in range(3)) >> 6)))
    m2.w16(a + 0x26, 0x14); m2.w16(a + 0x76, 0)
    if m2.u16(a + 2) & 0x80 or (m.u16(P + 2) & 0x80):
        m2.w32(a + 0x42, 0)
    m2.w16(a + 2, m2.u16(a + 2) & ~0x80)
    m2.w16(a + 0x2C, 0xFFFF); m2.w8(0x4A910, 0x8C)
    m2.w8(a + 0x38, m.u8(P + 0x38))
    t = [lw(m.u16(P + o)) >> 2 for o in (0x94, 0x9A, 0xA0)]
    if cls != 0x30: t = [lw(-v) for v in t]
    th = norm(t[0], t[1], t[2], 0x3C0)
    th = [lw(v) for v in th]
    for i, o in enumerate((0x3E, 0x42, 0x46)): m2.w32(a + o, s32(m2.s32(a + o) + th[i]))
    m2.w16(a + 0x4C, 0xFFEC)
    m2.w16(0x4AA7A if not parachute else 0x4A97A, a - P)
    ex = bytes(m2.b)
    mask = set(range(0x4AB00 - LO, 0x4AC80 - LO))
    fields = [o for o in range(0xA4) if o not in range(0x66, 0x6C) and o < 0x80 and o not in (0xA, 0xB)]
    ok = all(got[a - LO + o] == ex[a - LO + o] for o in fields) and got[0x4A910 - LO] == 0x8C and \
         (got[0x4AA7A - LO:0x4AA7A - LO + 2] == ex[0x4AA7A - LO:0x4AA7A - LO + 2]) and (got[0x4A97A - LO:0x4A97A - LO + 2] == ex[0x4A97A - LO:0x4A97A - LO + 2])
    tpl = 0x25ECC + (0xDC if parachute else 0x28)
    ok = ok and gd == img[tpl - 0x10000:tpl - 0x10000 + 20]
    n3 += 1
    if not ok:
        bad3 += 1
        if bad3 < 4: print("  differs (spawn):", parachute, [(hex(o), got[a - LO + o], ex[a - LO + o]) for o in fields if got[a - LO + o] != ex[a - LO + o]][:6])
print("3. spawn routines: %d cases (seat and parachute), %d differences" % (n3, bad3))

# ---- 4. records 1 to 3 all in use: the seat goes to record 5 (the second seat routine entry, 0x26F8E)
base(); rig.w8(P + 0x7C, 0xE0 | 0x0A)
for r in (1, 2, 3):
    a_ = P + 0x200 * r
    rig.w16(a_, 0x10C0); rig.w8(a_ + 0x62, 0x00); rig.w16(a_ + 0x4C, 300); rig.w8(a_ + 0x5E, r)
at5 = None
for k in range(12):
    rig.run(0x26AF8, a6=0x710000)
    if (rig.u16(P + 0xA00) & 0x40) and rig.u8(P + 0xA00 + 0x62) == 0x30 and at5 is None: at5 = k
ok_e = at5 == 5 and rig.u16(0x4AA7A) == 0xA00
print("4. records 1-3 in use (weapons): the seat spawns in record 5 at pass %s, latch %s: %s" % (at5, hex(rig.u16(0x4AA7A)), "as stated" if ok_e else "DIFFERENT"))

# ---- 5. the landing in the world update: when the hit bit appears and when request 2 is made
base(); rig.w8(P + 0x7C, 0xE0 | 0x06)
a_ = P + 0x200
rig.w16(a_, 0x10CA); rig.w8(a_ + 0x62, 0x30); rig.w16(a_ + 0x4C, 200); rig.w8(a_ + 0x5E, 1)
rig.w32(a_ + 0x18, 2000); rig.w32(a_ + 0x10, 8); rig.w32(a_ + 0x42, s32(-769 & 0xFFFFFFFF)); rig.w16(a_ + 0x6C, 0x300)
for o in (0x92, 0x9A, 0xA2, 0x80, 0x88, 0x90): rig.w16(a_ + o, 0x4000)
rig.w16(0x4AA7A, 0x200)
rig.w32(a_ + 0x3E, 500); rig.w32(a_ + 0x46, 300)
hit = req = None; trace5 = []; xs = []
for k in range(14):
    rig.run(0x26AF8, a6=0x710000)
    h = rig.u16(a_) & 0x8000
    if h and hit is None: hit = k
    if rig.u8(0x4A8E2) == 2 and req is None: req = k
    trace5.append((k, hex(rig.u16(a_) & 0x9000), rig.s16(a_ + 0x4C), rig.u8(0x4A8E2))); xs.append((rig.u32(a_ + 0x14), rig.u32(a_ + 0x1C)))
print("5. landing in the world update: hit bit at pass %s, request 2 at pass %s (%s passes later); counter after each pass: %s" % (hit, req, None if hit is None or req is None else req - hit, [t[2] for t in trace5]))
still = hit is not None and all(xs[k] == xs[hit] for k in range(hit, len(xs))) and xs[hit] != xs[0]
print('   position after the landing pass is constant (a landed seat does not move):', still)
ok_f = still and hit is not None and req == hit + 5     # the hit branch runs in the 5 passes after the landing pass: counter 4, 3, 2, 1, 0

# ---- 6. which nibble values the seat routine sees: the key is handled before the world update, the player's step then moves the nibble once
res6 = {}
for v in (0x0A, 0x0B, 0x0E, 0x0F, 0x09, 0x07):
    base(); rig.w8(P + 0x7C, 0xE0 | v)       # the nibble as the key handler leaves it
    rig.run(0x26AF8, a6=0x710000)
    r1 = P + 0x200
    res6[v] = (rig.u8(P + 0x7C) & 15, hex(rig.u8(r1 + 0x62)) if rig.u16(r1) & 0x40 else '-', rig.u16(0x4A97A))
print("6. nibble after the key (before the pass) -> nibble after the pass, class of record 1, parachute latch:", {hex(k): v for k, v in res6.items()})
ok_g = res6[0x0F][1] == '0x31' and res6[0x0E][1] == '-' and res6[0x0B][1] == '-'

# ---- 7. the view switch of the seat's step with the real 0x1EC14 and 0x1E4D2 (not stubbed): what the view byte becomes
res7 = {}
for v83d in (0, 1, 2):
    for cls in (0x30, 0x31):
        rig.reset()
        for a in (0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2, 0x1F1D6, 0x1A0DC, 0x1A042): rig.poke(a, b'\x4e\x75')
        rig.w8(0x4A83D, v83d); rig.w8(0x4A8EB, 1 if v83d else 0); rig.w8(0x4A86D, 1); rig.w16(0x4AA6C, 1); rig.w16(0x4A994, 0)
        rig.w16(P, 0x11C8); rig.w16(P + 0x200, 0x10CA); rig.w8(P + 0x200 + 0x62, cls); rig.w16(P + 0x200 + 0x4C, 25); rig.w32(P + 0x200 + 0x18, 0x40000)
        rig.run(0x27BFA, a1=P + 0x200, a2=P, a6=0x710000)
        res7[(v83d, cls)] = (rig.u8(0x4A83D), rig.u16(0x4A994), rig.u8(0x4A96A))
print("7. view switch of the seat step, (V:4A83D before, class) -> (V:4A83D after, viewed record, ring direction):", {(a, hex(b)): v for (a, b), v in res7.items()})
ok_h = all(res7[(0, 0x30)][:2] == (0, 1) for _ in (0,)) and res7[(0, 0x31)][1] == 0 and res7[(1, 0x30)][1] == 1
sys.exit(1 if bad or bad3 or not (ok_a and ok_b and ok_c and ok_d and ok_e and ok_f and ok_g and ok_h) else 0)
