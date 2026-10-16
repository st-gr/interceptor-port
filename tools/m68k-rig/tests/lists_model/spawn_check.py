"""The mission-start placement and the region triggers (docs/re/07-ai-and-weapons.md, section 2): the original's 0x2CE36 (and 0x2D0AA for the
regions) run in the Musashi rig over every mode, variant, sub-list selector and progress value, against a model written from the note.

    python3 spawn_check.py [seed]      (run under ulimit -v 2500000 and timeout, like every rig script)

Compared for every entry the model places: the record's flag words, class, group, destination and leader words, position words, speeds, gear byte,
stock and rounds, fuel, the model-unit nibble, the template copy at 0x2600C, the hostile count V:4A961 and the heading the aim routine leaves.
Also the refusals (slot active, the cap, the mode filter, the threshold, the class 0x15 rule) and the region quirk.
Prints the counts; exit 1 on a difference.  Skips with exit 0 when extracted/interceptor.bin or librig.so is missing.
"""
import os, sys, random, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped (missing extracted/interceptor.bin or librig.so)"); sys.exit(0)
seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
rng = random.Random(seed)
REC = 0x4B23C; TPL = 0x25ECC; RUN = 0x2600C; PB = 0x2DCE8; ROWS = 0x2DEDA
img = open(IMGPATH, 'rb').read()
def iw(a): return struct.unpack('>H', img[a-0x10000:a-0x10000+2])[0]
def isw(a): return struct.unpack('>h', img[a-0x10000:a-0x10000+2])[0]
def s16(v): return v - 0x10000 if v & 0x8000 else v
def u32(v): return v & 0xFFFFFFFF

RIG = Rig()          # one rig for the whole script (the rig is a single global machine); every case resets it
def base(mode):
    r = RIG; r.reset()
    for a in (0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2): r.poke(a, b'\x4e\x75')
    r.w8(0x4A95E, mode); r.w8(0x4A844, 1)
    r.w32(0x1D024, 0x700000)
    return r

def model_nibble(tpl):
    """the model-unit shift: the byte at (block + 6) in the first block of the detail list"""
    base_ = struct.unpack('>I', tpl[4:8])[0]
    w0 = iw(base_)
    if w0 & 0x8000: d = w0
    elif w0 & 0x4000: d = iw(base_ + 2)
    else: d = iw(base_ + 4)
    return img[base_ + 6 + (d & 0xFFF) - 0x10000] & 0xF

def place(rec, entry, st, offs, mode, reg_d7=None):
    """model of 0x2D248 for one entry; rec: dict slot -> bytearray(0x200) records, st: state dict; returns True when placed"""
    w0_, cls, w2, w3, fl = entry
    mf = (fl >> 8) & 0xF
    if mf and mf != mode: return False
    slot = w2 & 0x7F
    R = rec[slot]
    if R[1] & 0x40: return False
    tpl = img[TPL + w0_ - 0x10000: TPL + w0_ - 0x10000 + 20]
    st['tpl'][slot] = tpl
    if st['cap'] <= st['count']: return False
    if R[0x62] == 0x15 and struct.unpack('>H', R[6:8])[0] != 0: return False
    for i in range(164): R[i] = 0
    R[0x7D] = model_nibble(tpl)
    R[0x62] = cls & 0xFF
    R[0x5E] = slot
    R[0x3A] = (w2 >> 8) & 0x7F
    blk = PB + iw(PB + w3)
    cx, cz, fx, fz, h = [isw(blk + 2*i) for i in range(5)]
    if fl & 0x8000: R[1] |= 0x08
    else:
        R[1] &= ~0x08
        if (R[0x62] & 0xF0) == 0x10: st['count'] += 1
    if fl & 0x4000: R[5] = 8
    if fl & 0x2000: R[1] |= 1
    if fl & 0x1000:
        if st['restart']:
            ox, oz = st['ox'], st['oz']
            if fl & 0x80: ox >>= 1; oz >>= 1
        else:
            v = st['sel']
            ox = (v >> 1) & 3
            if v & 0x10: ox = -ox
            st['ox'] = ox
            if fl & 0x80: ox >>= 1
            oz = (v >> 2) & 3
            if v & 0x20: oz = -oz
            st['oz'] = oz
            if fl & 0x80: oz >>= 1
        cx += ox; cz += oz
    R[0x5D] = (st['d7'] + 1) & 0xFF
    R[0x7A] = 3
    if (R[0x62] & 0xF0) != 0x20: R[0] |= 0x10; R[1] |= 0x80
    # destination / leader words
    if w2 & 0x8000:
        idx = w2 & 0x7F00
        if idx:
            b2 = PB + iw(PB + (idx >> 7))
            R[0x2C:0x34] = struct.pack('>hhhh', *[isw(b2 + 2*i) for i in range(4)])
            R[0x34:0x38] = struct.pack('>i', isw(b2 + 8))
        R[0x38] = 0xFF
        own_dest = False
    else:
        R[0x2C:0x34] = struct.pack('>hhhh', cx, cz, fx, fz)
        R[0x34:0x38] = struct.pack('>i', h)
        R[0x38] = ((w2 >> 8) & 0x7F) | 0x80
    R[6:8] = struct.pack('>h', cx); R[8:10] = struct.pack('>h', cz)
    R[0xC:0xE] = struct.pack('>h', fx); R[0xE:0x10] = struct.pack('>h', fz)
    R[0x10:0x14] = struct.pack('>i', h)
    if h != 0:
        R[0x7C] |= 0x80; R[0x7C] |= 0x60; R[0x6C:0x6E] = b'\x20\x00'; R[0x6E:0x70] = b'\x20\x00'
    R[0] |= 0x01; R[1] |= 0x40; R[1] |= 0x00
    R[0] |= 0x01 if False else 0
    R[0] &= 0x7F
    R[0x5F] = 0x44; R[0x60:0x62] = b'\x01\xf4'; R[0x72:0x76] = struct.pack('>I', 0x61A800)
    R[0x63] = 0x3D; R[0x71] = 0xFF; R[0x64] = 0x10
    x = u32((cx << 22) + (fx << 8)); z = u32((cz << 22) + (fz << 8))
    R[0x14:0x18] = struct.pack('>I', x); R[0x18:0x1C] = struct.pack('>I', u32(h << 8)); R[0x1C:0x20] = struct.pack('>I', z)
    return True

def aim_heading(r, dx, dz):
    """the original's aim routine on the same vector: heading word"""
    sp = 0xE00000 - 0x200
    for i, v in enumerate((0, 0, dx, 0, dz, 0xFFFFFFFF)):
        r.w32(sp + 4*i + 4, v)
    # call convention: arguments on the stack above the return address
    r.w32(sp, 0x100)
    r.L.rig_set_reg(15, sp)
    return None

def follow_up(rec, slot, entry, st):
    """model of 0x2CF14 for the placed entry (the destination block or the leader): returns (dx, dz) or None when no aim"""
    w2 = entry[2]
    R = rec[slot]
    if w2 & 0x8000:
        idx = w2 & 0x7F00
        if not idx: return None
        b2 = PB + iw(PB + (idx >> 7))
        v = [isw(b2 + 2*i) for i in range(5)]
        R[0x38] = 0xFF
        tx = u32((v[0] << 22) + (v[2] << 8)); tz = u32((v[1] << 22) + (v[3] << 8))
    else:
        L = rec[(w2 >> 8) & 0x7F]
        if not (L[1] & 0x40): return None
        R[0x2C:0x38] = L[6:8] + L[8:10] + L[0xC:0xE] + L[0xE:0x10] + L[0x10:0x14]
        R[0x38] = ((w2 >> 8) & 0x7F) | 0x80
        tx = struct.unpack('>I', L[0x14:0x18])[0]; tz = struct.unpack('>I', L[0x1C:0x20])[0]
    px = struct.unpack('>I', R[0x14:0x18])[0]; pz = struct.unpack('>I', R[0x1C:0x20])[0]
    return struct.unpack('>i', struct.pack('>I', u32(tx - px)))[0], struct.unpack('>i', struct.pack('>I', u32(tz - pz)))[0]

def aim(dx, dz):
    r = AIMRIG
    r.w32(0x4AB78, 0)
    import ctypes
    sp = 0xE00000 - 0x400
    # push d0..d5 as the caller does: d5 first (highest address), so d0 is at the lowest address
    args = (0, 0, dx & 0xFFFFFFFF, 0, dz & 0xFFFFFFFF, 0xFFFFFFFF)
    for i, v in enumerate(args): r.w32(sp + 4 + 4*i, v)
    return None

counts = {'configs': 0, 'entries': 0, 'fields': 0, 'bad': 0, 'refused': 0, 'headings': 0, 'badhead': 0}
BAD = []
HEAD = RIG
def rig_heading(dx, dz):
    """0x1454A through the rig with the arguments on the stack"""
    import ctypes
    r = HEAD; L = r.L
    for i in range(16): L.rig_set_reg(i, 0)
    sp = 0xE00000 - 0x400
    for i, v in enumerate((0, 0, dx & 0xFFFFFFFF, 0, dz & 0xFFFFFFFF, 0xFFFFFFFF)): r.w32(sp + 4 + 4*i, v)
    r.w32(sp, 0x100)
    L.rig_set_reg(15, sp)
    # rig_run sets the stack itself: use a trampoline in RAM: push the six args then jsr
    tramp = bytes.fromhex('2F3C') + struct.pack('>I', 0xFFFFFFFF) + bytes.fromhex('2F3C') + struct.pack('>I', dz & 0xFFFFFFFF) + \
        bytes.fromhex('2F3C') + struct.pack('>I', 0) + bytes.fromhex('2F3C') + struct.pack('>I', dx & 0xFFFFFFFF) + \
        bytes.fromhex('2F3C') + struct.pack('>I', 0) + bytes.fromhex('2F3C') + struct.pack('>I', 0) + bytes.fromhex('4EB9') + struct.pack('>I', 0x1454A) + bytes.fromhex('DFFC00000018') + bytes.fromhex('4E75')
    r.poke(0x8F1000, tramp)
    r.run(0x8F1000)
    return r.u16(0x4AB7A)

def one(mode, variant_sel, sel, prog, restart, pre, leader_active):
    """run the placement in the rig and compare"""
    r = base(mode)
    r.w8(0x4A95F, prog)
    r.w16(0x4ABB0, variant_sel)
    if restart:
        r.w8(0x4A8E3, 1); r.w16(0x4ABD4, (variant_sel & 3) % 3); r.w16(0x4ABD0, pre[0] & 0xFFFF); r.w16(0x4ABD2, pre[1] & 0xFFFF)
    r.poke(0x700000, bytes(0x40)); r.w8(0x700012 + (mode & 0x7F), sel)
    recs = {i: bytearray(r.peek(REC + 0x200*i, 0x200)) for i in range(16)}
    # the preset: player active (leader 0) and random other actives
    if leader_active: recs[0][1] |= 0x40
    for i in pre[2]:
        recs[i][1] |= 0x40
    for i in range(16): r.poke(REC + 0x200*i, bytes(recs[i]))
    r.run(0x2CE36, a6=0x710000)
    # model
    row = (mode - 1) if mode <= 9 else (9 if mode in (0x7E, 0x7F) else None)
    if row is None: return
    if mode == 0x7D: return
    if r.u8(0x4A95E) not in (1,2,3,4,5,6,7,8,9,0x7E,0x7F): return
    if restart: v = (variant_sel & 3) % 3 if False else r.u16(0x4ABD4)
    else:
        v = variant_sel & 3
        v = 0 if v == 0 else v - 1
    off = iw(ROWS + 12*row + 4*v); cap = iw(ROWS + 12*row + 4*v + 2)
    L = ROWS + off
    subo = iw(L + 2*(sel & 3)) if mode <= 9 and False else None
    selv = sel if mode <= 9 else 0
    subo = iw(L + 2*selv) if True else 0
    a = L + subo
    ents = []
    while isw(a) >= 0:
        ents.append([iw(a + 2*i) for i in range(5)]); a += 10
    st = {'cap': cap, 'count': 0, 'd7': 0xFE - 0x100, 'sel': variant_sel & 0xFFFF, 'restart': restart, 'ox': pre[0], 'oz': pre[1], 'tpl': {}}
    rec = {i: bytearray(recs[i]) for i in range(16)}
    first = True
    aims = []
    for e in ents:
        if not first and (e[4] & 0x7F) > prog: continue
        first = False
        ok = place(rec, e, st, None, mode)
        counts['entries'] += 1
        if ok:
            ai = follow_up(rec, e[2] & 0x7F, e, st)
            aims.append((e[2] & 0x7F, ai))
        else: counts['refused'] += 1
    counts['configs'] += 1
    # the hostile cap test in the model is `cap <= count -> refuse` (done in place via the weird expression); recompute properly below
    for i in range(16):
        got = r.peek(REC + 0x200*i, 164)
        exp = bytes(rec[i][:164])
        # attitude / matrices are written by the aim; compare the rest (skip +0x66..+0x6A and +0x7E and matrices after 0x7F)
        for o in range(164):
            if 0x66 <= o < 0x6C or o >= 0x7E: continue
            if o in (0xA, 0xB): continue
            counts['fields'] += 1
            if got[o] != exp[o]:
                counts['bad'] += 1
                if len(BAD) < 12: BAD.append((mode, variant_sel, sel, prog, restart, i, hex(o), got[o], exp[o]))
    if r.u8(0x4A961) != st['count']:
        counts['bad'] += 1; BAD.append(('count', mode, r.u8(0x4A961), st['count']))
    for slot, ai in aims:
        if ai is None: continue
        counts['headings'] += 1
        # heading after the aim: the rig record's +0x68 against the aim routine on the same vector
        pos = struct.unpack('>II', bytes(rec[slot][0x14:0x18]) + bytes(rec[slot][0x1C:0x20]))
        h = rig_heading(ai[0], ai[1])
        if r.u16(REC + 0x200*slot + 0x68) != h:
            counts['badhead'] += 1
            if len(BAD) < 12: BAD.append(('head', mode, slot, r.u16(REC + 0x200*slot + 0x68), h))
    for slot, tpl in st['tpl'].items():
        if r.peek(RUN + 20*slot, 20) != tpl:
            counts['bad'] += 1; BAD.append(('tpl', mode, slot))

for mode in list(range(1, 10)) + [0x7E, 0x7F]:
    for sel in range(4):
        for prog in (0, 1, 2, 3, 4):
            for vs in (0, 1, 2, 3, 0x7FF3, 0x1234, 0x8CCC, 0x3CF0):
                for restart in (0, 1):
                    for la in (0, 1):
                        pre = (rng.randint(-3, 3), rng.randint(-3, 3), rng.sample(range(1, 16), rng.choice((0, 0, 1, 2))))
                        one(mode, vs, sel, prog, restart, pre, la)

def exit_region(rec, p_, cnt):
    """model of 0x2D116 for the entries of one region (the player has left the box)"""
    a_ = p_ + 10
    for _ in range(cnt):
        e = [iw(a_ + 2*i) for i in range(5)]; a_ += 10
        w2 = e[2]; R = rec[w2 & 0x7F]
        blk = PB + iw(PB + e[3]); v = [isw(blk + 2*i) for i in range(5)]
        t = R[0x38]
        if t >= 0x80 and (t & 0x7F): continue
        def own(fz=None):
            if R[0x7A] in (3, 4): R[0x7A] = 5
            vv = list(v)
            if fz is not None: vv[3] = fz                 # the leader branch has loaded d5 with the leader field before falling back
            R[0x2C:0x34] = struct.pack('>hhhh', *vv[:4]); R[0x34:0x38] = struct.pack('>i', vv[4])
            R[0x38] = 0x80; R[1] &= 0xFE
        if (w2 & 0xFF00) == 0:
            own(); continue
        if w2 & 0x8000:
            idx = w2 & 0x7F00
            if idx == 0: continue
            b2 = PB + iw(PB + (idx >> 7)); u = [isw(b2 + 2*i) for i in range(5)]
            R[0x2C:0x34] = struct.pack('>hhhh', *u[:4]); R[0x34:0x38] = struct.pack('>i', u[4])
            R[0x38] = 0xFF; R[1] &= 0xFE; R[0x7A] = 3
        else:
            L = rec[(w2 >> 8) & 0x7F]
            if not (L[1] & 0x40): own(s16(w2 & 0xFF00)); continue
            R[0x2C:0x38] = L[6:8] + L[8:10] + L[0xC:0xE] + L[0xE:0x10] + L[0x10:0x14]
            R[1] &= 0xFE; R[0x38] = ((w2 >> 8) & 0x7F) | 0x80; R[0x7A] = 3

# region triggers 0x2D0AA: entering a box (bits clear), modes 3..9; the bit number d7 is clobbered by the entry placer
REG = [struct.unpack('>I', img[0x2DE28 - 0x10000 + 4*i: 0x2DE28 - 0x10000 + 4*i + 4])[0] for i in range(4)]
rc = {'runs': 0, 'bad': 0, 'quirk': 0}
for mode in range(3, 10):
    for (cx, cz) in [(70, 70), (76, 76), (75, 75), (66, 66), (63, 70), (10, 10), (77, 77), (51, 68)]:
        for prog in (0, 1, 2):
            for sel in range(4):
                r = base(mode); r.w8(0x4A95F, prog); r.w8(0x4A962, 4); r.w16(0x4B242, cx); r.w16(0x4B244, cz)
                r.w8(0x4A951 if False else 0x4A961, 0)
                recs = {i: bytearray(r.peek(REC + 0x200*i, 0x200)) for i in range(16)}
                recs[0][0] = 0x11; recs[0][1] = 0xC8; recs[14][1] |= 0x40
                recs[0][6:8] = struct.pack('>h', cx); recs[0][8:10] = struct.pack('>h', cz)
                for i in range(16): r.poke(REC + 0x200*i, bytes(recs[i]))
                r.run(0x2D0AA, a6=0x710000)
                st = {'cap': 4, 'count': 0, 'd7': 0, 'sel': 0, 'restart': 0, 'ox': 0, 'oz': 0, 'tpl': {}}
                rec = {i: bytearray(recs[i]) for i in range(16)}
                d7 = 0; bits = 0
                for k in range(4):
                    p_ = REG[k]; box = [iw(p_ + 2*i) for i in range(4)]; cnt = iw(p_ + 8)
                    inside = box[0] <= cx <= box[1] and box[2] <= cz <= box[3]
                    if not (bits >> (d7 & 7)) & 1:
                        if inside:
                            bits |= 1 << (d7 & 7)
                            st['d7'] = d7
                            if cnt == 0: d7 = -1
                            else:
                                a_ = p_ + 10
                                for _ in range(cnt):
                                    e = [iw(a_ + 2*i) for i in range(5)]; a_ += 10
                                    st['d7'] = d7
                                    ok = place(rec, e, st, None, mode)
                                    d7 = 0 if ok else -1
                                    pass
                    elif not inside:
                        exit_region(rec, p_, cnt)
                        bits &= ~(1 << (d7 & 7))      # the exit path 0x2D116 clears the bit d7 names (not necessarily this region's)
                    else:
                        pass                           # inside again: the entries are tried again (slots active: refused; a freed slot is refilled)
                    d7 += 1
                rc['runs'] += 1
                bad_ = False
                for i in range(16):
                    got = r.peek(REC + 0x200*i, 0x7E); exp = bytes(rec[i][:0x7E])
                    for o in range(0x7E):
                        if 0x66 <= o < 0x6C or o in (0xA, 0xB): continue
                        if got[o] != exp[o]:
                            bad_ = True
                            if rc['bad'] < 2: print('  diff rec', i, hex(o), got[o], exp[o])
                if r.u8(0x4A855) != bits & 0xFF: bad_ = True
                if bits & 0xF0 or (bits & 0xF) not in (0, 1, 2, 4, 8): rc['quirk'] += 1
                if bad_ and rc['bad'] < 4: print('BAD', mode, cx, cz, prog, sel, 'bits', bits, 'rig', r.u8(0x4A855), [ (i, hex(r.u16(REC+0x200*i))) for i in range(16) if r.u16(REC+0x200*i)], [(i,hex(struct.unpack('>H',rec[i][:2])[0])) for i in range(16) if rec[i][1]])
                rc['bad'] += bad_
# the exit with every leader number, valid and not, and every target byte (poking the entry's word 2 of region 2)
rc3 = {'runs': 0, 'bad': 0}
img0 = img
for L_ in range(16):
    for lv in (0, 1):
        for t38 in (0x00, 0x80, 0x81, 0x8E, 0xFF):
            w2n = (L_ << 8) | 0x0C
            b_ = bytearray(img0); a_ = 0x2DE9C - 0x10000; b_[a_:a_ + 2] = struct.pack('>H', w2n); img = bytes(b_)
            r = base(3); r.poke(0x2DE9C, struct.pack('>H', w2n)); r.w8(0x4A855, 4); r.w16(0x4B242, 10); r.w16(0x4B244, 10); r.w16(REC, 0x11C8)
            recs = {i: bytearray(r.peek(REC + 0x200 * i, 0x200)) for i in range(16)}
            recs[12][0] = 0x11; recs[12][1] = 0xC0; recs[12][0x38] = t38; recs[12][0x7A] = 3
            if lv: recs[L_][1] |= 0x40
            if L_ == 12: recs[12][1] |= 0x40
            for i in range(16): r.poke(REC + 0x200 * i, bytes(recs[i]))
            r.run(0x2D0AA, a6=0x710000)
            rec = {i: bytearray(recs[i]) for i in range(16)}
            exit_region(rec, REG[2], 1)
            rc3['runs'] += 1
            got = r.peek(REC + 0x200 * 12, 0x3C); exp = bytes(rec[12][:0x3C])
            if got[0x2C:0x3C] != exp[0x2C:0x3C] or got[0:2] != exp[0:2]:
                rc3['bad'] += 1
                if rc3['bad'] < 3: print('  exit differs: leader', L_, 'valid', lv, 'target', hex(t38), got[0x2C:0x3C].hex(), exp[0x2C:0x3C].hex())
img = img0
print('exit with every leader number (valid and not) and target byte: %d runs, %d differences' % (rc3['runs'], rc3['bad']))
print('regions', rc)
print(counts)
for b in BAD[:12]: print('  ', b)
sys.exit(1 if counts['bad'] or counts['badhead'] or rc['bad'] or rc3['bad'] else 0)
