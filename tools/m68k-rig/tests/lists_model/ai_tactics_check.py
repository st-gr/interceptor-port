"""The AI's per-step gate, speed control, waypoints, target acquisition and evasion choice: the original's 0x278F6 (docs/re/07-ai-and-weapons.md,
section 4) in the Musashi rig against a model written from the note, over random scenes of an AI record, its target and the player.

    python3 ai_tactics_check.py [n] [seed]      (run under ulimit -v 2500000 and timeout)

Compared after one call: the RAM block 0x4A6E8-0x54EC0 (all records, the variable area) and the return value.  The helper routines that the note
treats as functions (the hypot approximation 0x20884, the frame offset 0x1D478) are called in the rig by the model.
Prints the counts; exit 1 on a difference.  Skips with exit 0 when extracted/interceptor.bin or librig.so is missing.
"""
import os, sys, random, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, IMGPATH, LIBPATH
if not (os.path.exists(IMGPATH) and os.path.exists(LIBPATH)):
    print("skipped (missing extracted/interceptor.bin or librig.so)"); sys.exit(0)
N = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
SEED = int(sys.argv[2]) if len(sys.argv) > 2 else 1
img = open(IMGPATH, 'rb').read()
def iw(a): return struct.unpack('>H', img[a-0x10000:a-0x10000+2])[0]
def isw(a): return struct.unpack('>h', img[a-0x10000:a-0x10000+2])[0]
def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v
def s8(v): v &= 0xFF; return v - 256 if v & 0x80 else v
LO, HI = 0x4A6E8, 0x54EC0
REC0 = 0x4B23C
EV = [(40,9),(40,5),(40,5),(40,5),(40,9),(40,9),(40,9),(40,5)]
def evtab(a, k): return (isw(a + 4*k), isw(a + 4*k + 2))

class Mem:
    def __init__(s, b): s.b = bytearray(b)
    def u8(s, a): return s.b[a - LO]
    def w8(s, a, v): s.b[a - LO] = v & 0xFF
    def u16(s, a): return (s.b[a-LO] << 8) | s.b[a-LO+1]
    def s16(s, a): return s16(s.u16(a))
    def w16(s, a, v): s.b[a-LO] = (v >> 8) & 0xFF; s.b[a-LO+1] = v & 0xFF
    def u32(s, a): return struct.unpack('>I', bytes(s.b[a-LO:a-LO+4]))[0]
    def s32(s, a): return s32(s.u32(a))
    def w32(s, a, v): s.b[a-LO:a-LO+4] = struct.pack('>I', v & 0xFFFFFFFF)

def hypot(rig, M, d2, d3, d4):
    rig.run(0x20884, d2=d2 & 0xFFFF, d3=d3 & 0xFFFF, d4=d4 & 0xFFFFFFFF)
    d1 = rig.reg('d1')
    M.w16(0x4ABF8, d1 & 0xFFFF)
    return s32(d1)

def frame_offset(rig, rec, M, d3, d4, d5):
    """0x1D478: the point (d3, d4, d5) model units in the frame of the record, as world longs"""
    rig.poke(rec, bytes(M.b[rec-LO:rec-LO+0x100]))
    rig.run(0x1D478, a1=rec, d3=d3 & 0xFFFF, d4=d4 & 0xFFFF, d5=d5 & 0xFFFF)
    return s32(rig.reg('d0')), s32(rig.reg('d1')), s32(rig.reg('d2'))

def gate(M, rig, idx):
    """0x278F6 for the record idx (a1); returns d0"""
    a = REC0 + 0x200 * idx
    u8 = lambda o: M.u8(a + o); w8 = lambda o, v: M.w8(a + o, v)
    u16 = lambda o: M.u16(a + o); s16_ = lambda o: M.s16(a + o); w16 = lambda o, v: M.w16(a + o, v)
    s32_ = lambda o: M.s32(a + o); w32 = lambda o, v: M.w32(a + o, v)
    if M.u8(0x4A866): return 1
    cls = u8(0x62) & 0xF0
    if cls in (0, 0x30, 0x20): raise KeyError('class')
    w16(0x4C, u16(0x4C) - 1)
    if not (u8(0) & 0x10): return 1
    if not M.u8(0x4A846): return 1
    oc = (u8(0x20) >> 1) & 1
    scripted = u8(2) & 1
    if oc:
        w32(0x34, 0)
        w16(0x2C, u16(6)); w16(0x2E, u16(8)); w16(0x30, u16(0xC)); w16(0x32, u16(0xE))
        d0 = s16_(0x6C)
        if d0 > 0x1200:
            d0 -= 0x90
            if d0 < 0x1200: d0 = 0x1200
        else:
            d0 += 0x90
            if d0 > 0x1200: d0 = 0x1200
        w16(0x6C, d0); w16(0x6E, d0); w16(2, u16(2) & 0xDFF7)
        return 1
    if not scripted:
        if s16_(0x2C) >= 0:
            speed(M, rig, idx)
    return waypoints(M, rig, idx)

def rangecalc(M, rig, idx):
    a = REC0 + 0x200 * idx
    u8 = lambda o: M.u8(a + o); w8 = lambda o, v: M.w8(a + o, v)
    s16_ = lambda o: M.s16(a + o); w16 = lambda o, v: M.w16(a + o, v)
    w8(0x39, u8(0x39) + 0x10)
    skip = False
    if s16_(0x4A) >= 0x480:
        d1 = 0x20
        if s16_(0x4A) >= 0x900: d1 = 0x50
        d0 = u8(0x39) & 0xF0
        if d1 >= d0: return
    w8(0x39, u8(0x39) & 0xF)
    d2 = s16_(0x2C)
    if d2 < 0: return
    d4 = s16_(0x2E); d0 = s16_(0x30); d1 = s16_(0x32); d3 = s32_if(M, a + 0x34)
    d2 = s16(d2 - s16_(6)) << 16
    d2 = s32(d2) >> 2
    d0 = s16(d0 - s16_(0xC))
    d2 = s32(d2 + d0)
    if d2 < 0: d2 = -d2
    d4 = s16(d4 - s16_(8)) << 16
    d4 = s32(d4) >> 2
    d1 = s16(d1 - s16_(0xE))
    d4 = s32(d4 + d1)
    if d4 < 0: d4 = -d4
    d3 = s32(d3 - M.s32(a + 0x10))
    if d3 < 0: d3 = -d3
    if d2 > 0x7F00 or d3 > 0x7F00 or d4 > 0x7F00:
        w16(0x4A, 0x7FFF)
        if idx != 0: w8(0x63, u8(0x63) | 0xF0)
        return
    d1 = hypot(rig, M, d2, d3, d4)
    w16(0x4A, d1 & 0xFFFF)
    w8(4, u8(4) | 1)
    if d1 > 0x36C0:
        if idx != 0: w8(0x63, u8(0x63) | 0xF0)
        return
    if d1 > 0x1E00:
        if u8(0x7A) == 4: w8(0x7A, 3)
    else:
        if u8(0x7A) == 3: w8(0x7A, 4)
    if idx == 0: return
    if d1 > 0x1800:
        w16(0x4A, 0x7FFF); w8(0x63, u8(0x63) | 0xF0); return
    if d1 > 0x300:
        w8(0x63, u8(0x63) & 0xF)
        if d1 > 0xC00: w8(0x63, u8(0x63) | 0x30)
        else: w8(0x63, u8(0x63) | 0x20)
        return
    w8(0x63, (u8(0x63) & 0xF) | 0x10)

def s32_if(M, addr): return M.s32(addr)

def speed(M, rig, idx):
    a = REC0 + 0x200 * idx
    u8 = lambda o: M.u8(a + o); w8 = lambda o, v: M.w8(a + o, v)
    u16 = lambda o: M.u16(a + o); s16_ = lambda o: M.s16(a + o); w16 = lambda o, v: M.w16(a + o, v)
    rangecalc(M, rig, idx)
    d2 = s16_(0x6C)
    cls = u8(0x62)
    d1 = None
    path = None
    if u8(0x38) == 0xFF:
        if cls != 0x14:
            path = '27a94'
        else:
            if u16(2) & 0x80:
                d1 = s16_(0x6C)
                if d1 < 0x100:
                    w8(0x2B, 0); return finish_key(M, a, 0)
                d1 >>= 1; path = '27ade'
            else:
                d1 = 0x1680
                if M.s32(a + 0x10) > 0x6C0: path = '27ad4'
                else:
                    if s8(u8(0x7C)) < 0: w8(0x7C, u8(0x7C) ^ 0x80)
                    d1 = 0x600; path = '27ad4'
    else:
        a2 = REC0 + 0x200 * (u8(0x38) & 0x7F)
        if not (u8(1) & 0x40):
            w8(0x38, 0x80); a2 = REC0
        d1 = M.s16(a2 + 0x6C)
        if u8(4) & 0x20:
            r = s16_(0x4A)
            if r <= 0x240: path = '27a84'
            elif r >= 0x600: path = '27ac4'
            else: path = '27ad4'
        else:
            st = u8(5)
            if u8(2) & 1 or st == 0 or st == 1:
                d3 = u8(0x64) & 0x60
                if s16_(0x4A) < 0x180: path = '27ac4'
                elif d3 != 0x60:
                    path = '27ac4' if s16_(0x4A) <= 0x300 else '27ad4'
                else: path = '27a84'
            else:
                if s16_(0x4A) <= 0x360: path = '27ad4'
                elif st == 6: path = '27a84'
                else: path = '27ac4'
    if path == '27a84':
        if s16_(0x4A) >= 0x300: path = '27a94'
        else:
            d1 = s16(d1 + d1)
            path = '27ad4' if d1 <= 0x1BC0 else '27a94'
    if path == '27a94':
        if cls == 0x15: d1 = 0x1EC0
        elif u8(1) & 8: d1 = 0x15C0
        else: d1 = 0x1D40
        path = '27ad4'
    if path == '27ac4':
        d0 = d1 >> 3
        if d0 >= 0x420: d0 = 0x420
        d1 = s16(d1 - d0)
        path = '27ad4'
    if path == '27ad4':
        if d1 <= 0x900: d1 = 0x900
    d1 = s16(d1 + d2) >> 1
    d0 = s16(d1 - d2); d3 = 0x90; d4 = d3
    if d0 < 0: d0 = -d0; d4 = -d4
    if d0 >= d3:
        d2 = s16(d2 + d4); d1 = d2
    cur = s16_(0x6C)
    if d1 == cur: k = 0
    elif d1 < cur: k = 2
    else: k = 1
    finish_key(M, a, k)

def finish_key(M, a, k):
    M.w8(a + 0x65, (M.u8(a + 0x65) & 0xFC) | k)

def waypoints(M, rig, idx):
    a = REC0 + 0x200 * idx
    u8 = lambda o: M.u8(a + o); w8 = lambda o, v: M.w8(a + o, v)
    u16 = lambda o: M.u16(a + o); s16_ = lambda o: M.s16(a + o); w16 = lambda o, v: M.w16(a + o, v)
    if M.u8(0x4A866): return 1
    if u8(5) == 8 and not (u8(2) & 1) and s16_(0x4A) <= 0x480:
        blk = 0x2DCE8 + iw(0x2DCE8 + 2 * u8(0x3A))
        found = False
        while True:
            if struct.unpack('>I', img[blk-0x10000:blk-0x10000+4])[0] == struct.unpack('>I', bytes(M.b[a+0x2C-LO:a+0x2C-LO+4]))[0] and \
               struct.unpack('>I', img[blk+4-0x10000:blk+8-0x10000])[0] == struct.unpack('>I', bytes(M.b[a+0x30-LO:a+0x30-LO+4]))[0]:
                found = True; break
            blk += 10
            if isw(blk) < 0: break
        if not found:
            M.w16(0x4AA56, 0x35)
        else:
            if isw(blk + 10) < 0:
                if u8(1) & 0x80:
                    w8(1, u8(1) & 0x7F)
                    if u8(0x62) == 0x15: w16(0, u16(0) | 0x200)
            else:
                for i in range(4): w16(0x2C + 2*i, isw(blk + 10 + 2*i))
                M.w32(a + 0x34, isw(blk + 18))
                w16(0x4A, 0x7FFF)
    return acquire(M, rig, idx)

def acquire(M, rig, idx):
    a = REC0 + 0x200 * idx
    u8 = lambda o: M.u8(a + o); w8 = lambda o, v: M.w8(a + o, v)
    u16 = lambda o: M.u16(a + o); s16_ = lambda o: M.s16(a + o); w16 = lambda o, v: M.w16(a + o, v)
    if u8(5) == 8:
        return tactical(M, rig, idx, None)
    w0 = u16(0)
    if w0 & 8:
        if w0 & 2: raise KeyError('27e70')
        return 1
    if u8(0x38) == 0xFF:
        a3 = REC0
        return retarget_player(M, rig, idx, a3)
    a3 = REC0 + 0x200 * (u8(0x38) & 0x7F)
    if u8(0x7A) == 5: w8(0x7A, 3)
    if not (M.u8(a3 + 1) & 0x40):
        return lost_target(M, rig, idx)
    w16(0, u16(0) | 1)
    if a3 == REC0: return avoidance(M, rig, idx, a3)
    return retarget_player(M, rig, idx, a3)

def retarget_player(M, rig, idx, a3):
    a = REC0 + 0x200 * idx
    w8 = lambda o, v: M.w8(a + o, v)
    prog = s8(M.u8(0x4A95F))
    if prog >= 3: d6 = 0x300000
    elif prog >= 2: d6 = 0x240000
    else: d6 = 0x180000
    ok = True
    for o in (0x14, 0x18, 0x1C):
        if abs(s32(M.s32(REC0 + o) - M.s32(a + o))) > d6: ok = False; break
    if ok: w8(0x38, 0x80)
    return avoidance(M, rig, idx, a3)

def avoidance(M, rig, idx, a3):
    a = REC0 + 0x200 * idx
    u8 = lambda o: M.u8(a + o); w8 = lambda o, v: M.w8(a + o, v)
    u16 = lambda o: M.u16(a + o); w16 = lambda o, v: M.w16(a + o, v)
    if u16(2) & 1:
        if u8(0x64) & 1:
            d4 = 0x60 if (u8(0x64) & 2) else -0x60
            d3 = 0; d5 = -0x30
        else:
            d3 = 0xA8 if (u8(0x64) & 2) else -0x60
            d4 = 0; d5 = -0x30
    else:
        if (M.u16(0x4A992) & 0xFF) == 0:
            d3 = 0x14
            t = s8(M.u8(a3 + 0x28)); t = -t if t < 0 else t
            if t > d3 or abs(s8(M.u8(a3 + 0x2A))) > d3:
                pass    # V:4A842 is stored here and read by nothing that matters (see the note); masked in the comparison
        d3 = 0; d4 = 0; d5 = -0x24
    d0, d1, d2 = frame_offset(rig, a3, M, d3, d4, d5)
    f3 = (d0 >> 8) & 0x3FFF; f4 = (d2 >> 8) & 0x3FFF
    c0 = s16(((d0 & 0xFFFFFFFF) >> 16) >> 6 if False else (s16(d0 >> 16) >> 6))
    c2 = s16(s16(d2 >> 16) >> 6)
    w16(0x2C, c0); w16(0x2E, c2); w16(0x30, f3); w16(0x32, f4)
    M.w32(a + 0x34, s32(d1) >> 8)
    return tactical(M, rig, idx, a3)

def lost_target(M, rig, idx):
    """28156: the target record is not active: send the record to its placement entry's position block"""
    a = REC0 + 0x200 * idx
    u8 = lambda o: M.u8(a + o); w8 = lambda o, v: M.w8(a + o, v)
    u16 = lambda o: M.u16(a + o); w16 = lambda o, v: M.w16(a + o, v)
    if not (u8(0) & 0x10): return 1
    a3 = M.u32(0x4A7F2)
    def search(a3):
        while isw(a3) >= 0:
            if (iw(a3 + 4) & 0xFF) == M.u16(0x4AA6C): return a3
            a3 += 10
        return None
    f = search(a3)
    if f is None:
        d0 = s8(u8(0x5D))
        if d0 > 0:
            reg = struct.unpack('>I', img[0x2DE28 - 0x10000 + 4*(d0-1): 0x2DE28 - 0x10000 + 4*(d0-1) + 4])[0]
            f = search(reg + 10)
            if f is None: raise KeyError('hang')      # the original loops here for ever (no entry of the region names the record)
        if f is None:
            if d0 <= 0: M.w16(0x4AA56, 0x34)
            M.w8(0x4A86F, 0); return 1
    blk = 0x2DCE8 + iw(0x2DCE8 + iw(f + 6))
    for i in range(4): w16(0x2C + 2*i, isw(blk + 2*i))
    M.w32(a + 0x34, isw(blk + 8))
    w8(0x38, 0xFF); w16(0, u16(0) & 0xFFFE)
    M.w8(0x4A86F, 0); return 1

def tactical(M, rig, idx, a3):
    a = REC0 + 0x200 * idx
    u8 = lambda o: M.u8(a + o); w8 = lambda o, v: M.w8(a + o, v)
    u16 = lambda o: M.u16(a + o); s16_ = lambda o: M.s16(a + o); w16 = lambda o, v: M.w16(a + o, v)
    def done():
        M.w8(0x4A86F, 0); return 1
    if u8(0x20) & 2: return done()
    # 281E2
    if (u8(0x39) & 0xF0) == 0x10:
        if a3 is None: raise KeyError('a3')
        if M.s16(a3 + 0x4A) > 0x3000: w8(4, u8(4) & 0xDF)
        else:
            v = [s32(M.s32(a3 + o) - M.s32(a + o)) >> 8 for o in (0x14, 0x18, 0x1C)]
            x, y, z = norm(rig, *v)
            d7 = s32(M.s16(a3 + 0x96) * x + M.s16(a3 + 0x9C) * y + M.s16(a3 + 0xA2) * z)
            if d7 >= 0 or d7 > -0x2C0000: w8(4, u8(4) & 0xDF)
            else:
                d4 = s32(M.s16(a3 + 0x96) * M.s16(a + 0x96) + M.s16(a3 + 0x9C) * M.s16(a + 0x9C) + M.s16(a3 + 0xA2) * M.s16(a + 0xA2))
                if d4 < 0 or d4 < 0xD000000: w8(4, u8(4) & 0xDF)
                else: w8(4, u8(4) | 0x20)
    st = u8(5)
    if st == 6: return done()
    if st == 8:
        if M.u8(0x4A95E) != 5 or M.u8(0x4A86F) == 0: return done()
    elif st not in (0, 1) and False: pass
    if not (u8(4) & 0x20): return done()
    if a3 is None: raise KeyError('a3b')
    base = 0x30284
    d2 = 0
    if M.s16(a3 + 0x4A) <= 0x300:
        d1 = M.s16(a + 0x6C) - M.s16(a3 + 0x6C)
        if d1 < 0: d1 = -d1; d2 = 1
        if d1 >= 0xC0:
            base += 0x20
            if not d2: base += 0x20
    else:
        base += 0x60
        if M.s16(a3 + 0x4A) <= 0xC00: sub = True
        elif M.s16(a3 + 0x4A) <= 0x1E00: base += 0x60; sub = True
        else: base += 0x60; base += 0x60; sub = False
        if sub:
            d1 = M.s16(a + 0x6C) - M.s16(a3 + 0x6C)
            if d1 < 0: d1 = -d1; d2 = 1
            if d1 >= 0xC0:
                base += 0x20
                if not d2: base += 0x20
            d0 = s16(M.u16(0x4AA78)) if False else M.s16(0x4AA78)
            if d0 >= 0 and d0 == M.u16(0x4AA6E) and M.u8(0x4A86F):
                M.w8(0x4A86F, 0); base += 0x10
    p = s8(M.u8(0x4A95F))
    if p > 3: p = 3
    cnt, newst = isw(base + 4 * (p if p >= 0 else 0)), isw(base + 4 * (p if p >= 0 else 0) + 2)
    w16(0x4C, cnt)
    if u8(5) == 8 and (u8(1) & 8):
        w8(1, u8(1) & 0xF7); M.w8(0x4A961, M.u8(0x4A961) + 1); M.w8(0x4A962, M.u8(0x4A962) + 1)
    w8(5, newst)
    return done()

# the normaliser (0x29C0E) through the rig
def norm(rig, x, y, z, length=0xC0):
    rig.run(0x29C0E, d0=length, d5=x & 0xFFFFFFFF, d6=y & 0xFFFFFFFF, d7=z & 0xFFFFFFFF)
    return s32(rig.reg('d5')), s32(rig.reg('d6')), s32(rig.reg('d7'))

def rand_scene(rng, rig):
    """a fresh RAM block: the player, the AI record (index idx) and a target record"""
    rig.reset()
    rig.poke(0x1F1D6, b'\x4e\x75')
    M = Mem(rig.peek(LO, HI - LO))
    idx = rng.choice((4, 6, 8, 10, 12))
    cx = rng.randint(60, 80); cz = rng.randint(60, 80)
    def fillrec(i, cls, active=True):
        a = REC0 + 0x200 * i
        M.w16(a, 0x11C0 if active else 0)
        M.w8(a + 0x62, cls)
        M.w16(a + 6, cx + rng.randint(-1, 1)); M.w16(a + 8, cz + rng.randint(-1, 1))
        M.w16(a + 0xC, rng.randint(0, 16383)); M.w16(a + 0xE, rng.randint(0, 16383))
        M.w32(a + 0x10, rng.randint(0, 0x8000)); M.w32(a + 0x18, M.s32(a + 0x10) << 8)
        M.w32(a + 0x14, (M.s16(a + 6) << 22) + (M.s16(a + 0xC) << 8)); M.w32(a + 0x1C, (M.s16(a + 8) << 22) + (M.s16(a + 0xE) << 8))
        M.w16(a + 0x6C, rng.randint(0x800, 0x3000)); M.w16(a + 0x6E, M.u16(a + 0x6C))
        M.w16(a + 0x4A, rng.choice((0x7FFF, rng.randint(0, 0x500), rng.randint(0, 0x2000), rng.randint(0, 0x4000))))
        M.w8(a + 0x28, rng.randint(-30, 30)); M.w8(a + 0x2A, rng.randint(-30, 30))
        for o in (0x92, 0x94, 0x96, 0x98, 0x9A, 0x9C, 0x9E, 0xA0, 0xA2): M.w16(a + o, rng.randint(-16384, 16384))
        M.w16(a + 0x16, rng.randint(0, 65535))
    fillrec(0, 0x11); fillrec(idx, rng.choice((0x12, 0x12, 0x13, 0x10, 0x14, 0x15, 0x16)))
    tgt = rng.choice((0, 0, 14, 4, 8)) if idx not in (4, 8) else rng.choice((0, 0, 14, 6))
    if tgt != idx and tgt != 0: fillrec(tgt, 0x20 if tgt == 14 else 0x12, active=rng.random() < .9)
    a = REC0 + 0x200 * idx
    M.w16(a + 0, rng.choice((0x11C0, 0x11C0, 0x11C1, 0x01C0, 0x11C8 & ~0x8 | 0x8)) & ~2)
    if rng.random() < .15: M.w8(a + 0x20, 2)
    M.w8(a + 2, rng.choice((0, 0, 0, 1)) )
    M.w8(a + 3, rng.choice((0, 0, 0x80, 1)))
    M.w8(a + 4, rng.choice((0, 0x20, 0x20)))
    M.w8(a + 5, rng.choice((0, 1, 6, 8, 3, 0xA)))
    M.w8(a + 0x38, rng.choice((0xFF, 0x80, 0x80 | tgt, 0x80 | tgt, 0x80 | tgt)))
    M.w8(a + 0x39, rng.choice((0, 0x10, 0x20, 0x00, 0x30, 0x50)))
    M.w8(a + 0x3A, rng.randint(0, 6)); M.w8(a + 0x5D, rng.choice((0, 1, 2, 3, 4, 0xFF))); M.w8(a + 0x7A, rng.choice((3, 4, 5)))
    M.w8(a + 0x64, rng.randint(0, 255)); M.w8(a + 0x65, rng.randint(0, 255)); M.w8(a + 0x7C, rng.randint(0, 255))
    M.w16(a + 0x2C, rng.choice((cx + rng.randint(-2, 2), -1, cx + rng.randint(-40, 40))))
    M.w16(a + 0x2E, cz + rng.randint(-2, 2)); M.w16(a + 0x30, rng.randint(0, 16383)); M.w16(a + 0x32, rng.randint(0, 16383))
    M.w32(a + 0x34, rng.randint(0, 0x8000))
    M.w16(a + 0x4C, rng.randint(-5, 40))
    # waypoint state: put the destination on a block of the list so that it can be found
    if rng.random() < .5 and M.u8(a + 5) == 8:
        blk = 0x2DCE8 + iw(0x2DCE8 + 2 * M.u8(a + 0x3A))
        n = 0
        while isw(blk + 10 * n) >= 0 and n < 20: n += 1
        if n:
            k = rng.randrange(n) if n else 0
            for i in range(4): M.w16(a + 0x2C + 2*i, isw(blk + 10*k + 2*i))
            M.w16(a + 0x4A, rng.randint(0, 0x480))
            M.w8(a + 2, 0)
    M.w8(0x4A866, 0); M.w8(0x4A846, 1); M.w8(0x4A95E, rng.choice((3, 5, 6)))
    M.w8(0x4A95F, rng.choice((0, 1, 2, 3, 4)))
    M.w8(0x4A86F, rng.choice((0, 1)))
    M.w16(0x4A992, rng.choice((0, 256, rng.randint(0, 65535))))
    M.w16(0x4AA78, rng.choice((0xFFFF, 0x200 * idx, 0x200 * 6)))
    M.w16(0x4AA6E, 0x200 * idx); M.w16(0x4AA6C, idx)
    M.w8(0x4A961, 1); M.w8(0x4A962, 2)
    M.w8(REC0 + 1, 0x40)       # the player's flag byte: active
    subs = []
    for row in range(10):
        for v in range(3):
            L = 0x2DEDA + iw(0x2DEDA + 12*row + 4*v)
            for k in range(4): subs.append(L + iw(L + 2*k))
    M.w32(0x4A7F2, rng.choice(subs))
    return M, idx

def main():
    rng = random.Random(SEED)
    rig = Rig(); rig.reset()
    ok = bad = skip = 0; first = []; cover = {}
    for n in range(N):
        M, idx = rand_scene(rng, rig)
        blob = bytes(M.b)
        rig.poke(LO, blob)
        # the original
        rc = rig.run(0x278F6, a1=REC0 + 0x200 * idx, a2=REC0, a3=0x12345)
        got = rig.peek(LO, HI - LO); gd0 = rig.reg('d0') & 0xFF
        # the model, on a fresh copy
        rig.poke(LO, blob)
        M2 = Mem(blob)
        try:
            ret = gate(M2, rig, idx)
        except KeyError as e:
            skip += 1; continue
        exp = bytes(M2.b)
        st = (M.u8(REC0 + 0x200*idx + 5), M2.u8(REC0 + 0x200*idx + 5))
        cover[st] = cover.get(st, 0) + 1
        MASK = list(range(0x4AB00 - LO, 0x4AD00 - LO)) + [0x4A842 - LO]      # the composition and arithmetic scratch words written by the helper routines
        got = bytes(0 if i in MASK else v for i, v in enumerate(got)); exp = bytes(0 if i in MASK else v for i, v in enumerate(exp))
        if got == exp and gd0 == (ret & 0xFF): ok += 1
        else:
            bad += 1
            if len(first) < 5:
                dif = [(hex(LO + i), got[i], exp[i]) for i in range(len(got)) if got[i] != exp[i]][:6]
                first.append((n, idx, gd0, ret, dif))
    print("scenes:", N, "compared:", ok + bad, "identical:", ok, "different:", bad, "outside the model:", skip)
    print("state before/after:", sorted(cover.items()))
    for f in first: print("  ", f)
    sys.exit(1 if bad else 0)
main()
