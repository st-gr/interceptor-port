"""ctypes wrapper of build/sky-ground/librigblit.so (the rig with the blitter model) and the staging of one sky-fill frame.

    from rigw import Rig, stage, pens
    r = stage(pitch=0, heading=4000, roll=0, alt_long=-575057)      # camera matrix built by the game's own routines
    r.run(0x3486C)                                                  # the sky fill; pens(r) gives the 4-plane pen of every pixel

Addresses are flat-image addresses (docs/re/00-binary-layout.md). The image is extracted/interceptor.bin loaded at 0x10000."""
import ctypes, os, struct

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
_LIBPATH = os.path.join(ROOT, "build", "sky-ground", "librigblit.so")
_IMGPATH = os.path.join(ROOT, "extracted", "interceptor.bin")
if not (os.path.exists(_LIBPATH) and os.path.exists(_IMGPATH)):
    print("skipped: needs extracted/interceptor.bin and build/sky-ground/librigblit.so (bash tools/vamiga-ref/sky-ground/build.sh)")
    raise SystemExit(0)
LIB = ctypes.CDLL(_LIBPATH)
LIB.rig_init.argtypes = [ctypes.c_size_t]
LIB.rig_load_image.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
LIB.rig_poke.argtypes = [ctypes.c_uint32, ctypes.c_char_p, ctypes.c_size_t]
LIB.rig_peek.argtypes = [ctypes.c_uint32, ctypes.c_char_p, ctypes.c_size_t]
LIB.rig_get_reg.restype = ctypes.c_uint32
LIB.rig_get_reg.argtypes = [ctypes.c_int]
LIB.rig_set_reg.argtypes = [ctypes.c_int, ctypes.c_uint32]
LIB.rig_run.argtypes = [ctypes.c_uint32]
LIB.rig_blit_log.argtypes = [ctypes.c_int]
IMG = os.path.join(ROOT, "extracted", "interceptor.bin")
REGS = ["d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"]
P0, PS = 0x400000, 0x2000      # the four drawing planes of the staged frame (plane 0 first), 8000 bytes used of each
TAB = 0x4A726                  # the plane-pointer table the code reads through V:4A76E: planes 3, 2, 1, 0


class Rig:
    def __init__(self):
        LIB.rig_init(1 << 24)
        LIB.rig_load_image(IMG.encode(), 0x10000)
        z = bytes(0x54EC0 - 0x4A6E8)               # the variable hunks 71..73 start zeroed
        LIB.rig_poke(0x4A6E8, z, len(z))

    def peek(self, a, n):
        b = ctypes.create_string_buffer(n)
        LIB.rig_peek(a, b, n)
        return b.raw

    def poke(self, a, data):
        LIB.rig_poke(a, bytes(data), len(data))

    def s16(self, a): return struct.unpack(">h", self.peek(a, 2))[0]
    def u16(self, a): return struct.unpack(">H", self.peek(a, 2))[0]
    def s32(self, a): return struct.unpack(">i", self.peek(a, 4))[0]
    def u8(self, a): return self.peek(a, 1)[0]
    def w8(self, a, v): self.poke(a, struct.pack(">B", v & 0xFF))
    def w16(self, a, v): self.poke(a, struct.pack(">H", v & 0xFFFF))
    def w32(self, a, v): self.poke(a, struct.pack(">I", v & 0xFFFFFFFF))

    def reg(self, n): return LIB.rig_get_reg(REGS.index(n))

    def run(self, addr, **regs):
        for i in range(16):
            LIB.rig_set_reg(i, 0)
        for k, v in regs.items():
            LIB.rig_set_reg(REGS.index(k), v & 0xFFFFFFFF)
        return LIB.rig_run(addr)


def stage(pitch=0, heading=0, roll=0, alt_long=-0x80000, win=144, f953=0, f982=0, view=0, zoom=(0xA8, 0xFC, 0x80), state=None):
    """A rig with the sky-fill inputs set. state = the 2336 bytes of V:4A700.. from a capture log replaces the individual values (the exact
    state of a captured frame); otherwise the camera matrix is built by the game's own routines 0x32AB6 and 0x32C84 from the angle words."""
    r = Rig()
    if state is not None:
        r.poke(0x4A700, state)
    r.poke(P0, bytes(PS * 4))
    for i in range(4):
        r.w32(TAB + 4 * i, P0 + PS * (3 - i))
    r.w32(0x4A76E, TAB)
    r.w32(0x4A772, TAB)
    r.w32(0x4A79A, 0x500000)       # scratch plane of the generic polygon filler (used by the mesh pass)
    if state is None:
        r.w16(0x4AA3C, win); r.w8(0x4A953, f953); r.w16(0x4A982, f982); r.w8(0x4A83D, view)
        r.w32(0x4AB1E, alt_long)
        for i, z in enumerate(zoom):
            r.w16(0x4AAF6 + 2 * i, z)
        r.w16(0x4AB42, pitch); r.w16(0x4AB46, heading); r.w16(0x4AB4A, roll)
        r.run(0x32AB6, d0=pitch, d2=heading, d4=roll, a1=0x4AC90)
        r.run(0x32C84, a1=0x4AC90)
    return r


def pens(r):
    """The 320x200 pen map of the four drawing planes (list of rows)."""
    P = [r.peek(P0 + PS * k, 8000) for k in range(4)]
    return [[sum(((P[k][y * 40 + x // 8] >> (7 - x % 8)) & 1) << k for k in range(4)) for x in range(320)] for y in range(200)]


def polygon(r):
    """The screen polygon the ground routine left at V:5043C: count, then (x, y) pairs."""
    n = r.s16(0x5043C)
    return [(r.s16(0x5043E + 4 * i), r.s16(0x50440 + 4 * i)) for i in range(max(0, n))]
