"""Shared staging for the combat checks (docs/re/07-ai-and-weapons.md, Part B): the air scene with the mission-start placement of a mode, the world update
0x26AF8 as one step, record access.  Inputs (git-ignored): extracted/interceptor.bin, tools/m68k-rig/librig.so, extracted/captures/logs/lists.txt.
Run under `ulimit -v 2500000` and `timeout` (one Rig per script).
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, load_scene, inputs_missing

REC = lambda i: 0x4B23C + 0x200 * i
LOGBUF = 0x700000
_RIG = {}


def rig():
    if "r" not in _RIG:
        _RIG["r"] = Rig()
    return _RIG["r"]


def stage(mode=3, progress=0, selector=3, place=True, silence=True):
    """the air scene (player in record 0, carrier in record 14), the flight log record at LOGBUF, optionally the mission-start placement of `mode`."""
    r = rig()
    load_scene(r, "air")
    if silence:
        for a in (0x18E54, 0x1A18C, 0x1F11E, 0x1F0E2):          # the stubs of spawn_check.py: sound/notice side routines
            r.poke(a, b"\x4e\x75")
    r.poke(LOGBUF, bytes(0x80))
    r.w32(0x1D024, LOGBUF)
    r.w8(0x4A95E, mode); r.w8(0x4A95F, progress); r.w8(0x4A844, 1)
    r.w16(0x4ABB0, selector)
    if place:
        r.run(0x2CE36)
    return r


def world(r, n=1):
    """n passes of the world update 0x26AF8 (the frame counter V:4A992 advances once per pass like the frame routine)."""
    for _ in range(n):
        r.run(0x26AF8)
        r.w16(0x4A992, (r.u16(0x4A992) + 1) & 0xFFFF)


def rec(r, i):
    return r.peek(REC(i), 0x200)


def place_ahead(r, i, dist, dy=0, dx=0):
    """put record i `dist` draw units ahead of the player along its nose (+0x96/+0x9C/+0xA2, 1.0 = 0x4000), `dy` higher, `dx` to the side, cells and fine words
    consistent; the heading and the speed words of the record are the player's."""
    P = REC(0); R = REC(i)
    m = struct.unpack(">9h", r.peek(P + 0x92, 18))        # +0x92,+0x94,+0x96 / +0x98,+0x9A,+0x9C / +0x9E,+0xA0,+0xA2
    fwd = (m[2], m[5], m[8]); lat = (m[0], m[3], m[6])
    px = [r.s32(P + o) for o in (0x14, 0x18, 0x1C)]
    pos = [px[k] + ((dist * fwd[k] + dx * lat[k]) >> 14) * 256 for k in range(3)]
    pos[1] += dy * 256
    pos = [v & 0xFFFFFFFF for v in pos]
    set_pos(r, i, pos)


def set_pos(r, i, pos):
    R = REC(i)
    x, y, z = pos
    r.w32(R + 0x14, x); r.w32(R + 0x18, y); r.w32(R + 0x1C, z)
    r.w16(R + 6, x >> 22); r.w16(R + 8, z >> 22)
    r.w16(R + 0xC, (x & 0x3FFFFF) >> 8); r.w16(R + 0xE, (z & 0x3FFFFF) >> 8)
    r.w32(R + 0x10, (y & 0xFFFFFFFF) >> 8 if y < 0x80000000 else (y >> 8) | 0xFF000000)


# ---------------------------------------------------------------- hooks
SNDLOG = 0x7F0000          # records of (id, arg1 long, arg2 long) written by the sound-call stubs
SNDPTR = 0x7EFFF0
STUBS = 0x7E0000


def _w(*v):
    return b"".join(struct.pack(">H", x & 0xFFFF) for x in v)


def _l(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


def hook_calls(r, targets):
    """Replace each routine in `targets` ({address: id}) by a stub that logs (id, first long arg, second long arg) at SNDLOG and returns at once (the
    routines are the sound requests; their own effects are irrelevant to the weapon rules).  Rig RAM patched only."""
    addr = STUBS
    for t, ident in targets.items():
        c = _w(0x48E7, 0x8080)                       # movem.l d0/a0,-(a7)
        c += _w(0x2079) + _l(SNDPTR)                 # movea.l SNDPTR.l,a0
        c += _w(0x30FC, ident)                       # move.w #id,(a0)+
        c += _w(0x202F, 12, 0x20C0)                  # move.l (12,a7),d0 ; move.l d0,(a0)+
        c += _w(0x202F, 16, 0x20C0)                  # move.l (16,a7),d0 ; move.l d0,(a0)+
        c += _w(0x23C8) + _l(SNDPTR)                 # move.l a0,SNDPTR.l
        c += _w(0x4CDF, 0x0101, 0x4E75)              # movem.l (a7)+,d0/a0 ; rts
        r.poke(addr, c)
        r.poke(t, _w(0x4EF9) + _l(addr))            # jmp addr.l
        addr += (len(c) + 3) & ~3
    r.w32(SNDPTR, SNDLOG)


def read_calls(r):
    """the logged calls since the last read: [(id, arg1, arg2)]"""
    end = r.u32(SNDPTR); out = []; a = SNDLOG
    while a < end:
        out.append((r.u16(a), r.u32(a + 2), r.u32(a + 6))); a += 10
    r.w32(SNDPTR, SNDLOG)
    return out


def rts_stub(r, addrs):
    for a in addrs:
        r.poke(a, b"\x4e\x75")


def put_at(r, i, pos, vel=(0, 0, 0)):
    """set record i's position longs (units of 1/256: x, y, z) and the derived cell/fine/height words like the placement does."""
    a = REC(i)
    x, y, z = [p & 0xFFFFFFFF for p in pos]
    r.w32(a + 0x14, x); r.w32(a + 0x18, y); r.w32(a + 0x1C, z)
    r.w16(a + 6, x >> 22); r.w16(a + 8, z >> 22)
    r.w16(a + 0xC, (x & 0x3FFFFF) >> 8); r.w16(a + 0xE, (z & 0x3FFFFF) >> 8)
    r.w32(a + 0x10, y >> 8)


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def pos(r, i):
    a = REC(i)
    return [s32(r.u32(a + o)) for o in (0x14, 0x18, 0x1C)]


def stub_sound(r, log=0x7F0000):
    """replace the sound routine 0x1A0DC by a logger: counts calls at log, stores the two stack arguments of the last call at log+4, log+8."""
    code = bytes.fromhex("23EF0004") + (log + 4).to_bytes(4, "big") + bytes.fromhex("23EF0008") + (log + 8).to_bytes(4, "big") + \
        bytes.fromhex("52B9") + log.to_bytes(4, "big") + bytes.fromhex("4E75")
    r.poke(0x1A0DC, code)
    r.poke(log, bytes(12))
    return log


def call(r, addr, *args):
    """rig_call: C ABI, arguments pushed right to left as longs; returns D0."""
    import ctypes
    arr = (ctypes.c_uint32 * max(len(args), 1))(*[a & 0xFFFFFFFF for a in args])
    r.L.rig_call.restype = ctypes.c_uint32
    r.L.rig_call.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]
    return r.L.rig_call(addr, arr, len(args))


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def callf(r, addr, args=(), **regs):
    """run `addr` with C-ABI stack arguments (longs) and the given registers (the others zero); returns D0"""
    import ctypes
    for i in range(16):
        r.L.rig_set_reg(i, 0)
    for k, v in regs.items():
        r.L.rig_set_reg(r.NAMES.index(k), v & 0xFFFFFFFF)
    arr = (ctypes.c_uint32 * max(1, len(args)))(*[a & 0xFFFFFFFF for a in args])
    r.L.rig_call.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_int]
    r.L.rig_call.restype = ctypes.c_uint32
    return r.L.rig_call(addr, arr, len(args))
