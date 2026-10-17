"""Shared helpers for the display-list model checks (docs/re/04a-display-lists.md, Verified by).

Reads the user's git-ignored extracted/interceptor.bin at run time (never committed) and, for the rig
checks, the shared library tools/m68k-rig/librig.so (make -C tools/m68k-rig librig.so) and the emulator
log extracted/captures/logs/lists.txt written by tools/vamiga-ref/scripts/openers-lists.txt.
"""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
IMGPATH = os.environ.get("INTERCEPTOR_BIN", os.path.join(ROOT, "extracted", "interceptor.bin"))
LOGPATH = os.environ.get("LISTS_LOG", os.path.join(ROOT, "extracted", "captures", "logs", "lists.txt"))
LIBPATH = os.environ.get("RIGLIB", os.path.join(ROOT, "tools", "m68k-rig", "librig.so"))
sys.path.insert(0, os.path.join(ROOT, "tools", "vamiga-ref"))


def image():
    return open(IMGPATH, "rb").read()


def load_rig_module():
    import ctypes
    L = ctypes.CDLL(LIBPATH)
    L.rig_init.argtypes = [ctypes.c_size_t]
    L.rig_load_image.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
    L.rig_poke.argtypes = [ctypes.c_uint32, ctypes.c_char_p, ctypes.c_size_t]
    L.rig_peek.argtypes = [ctypes.c_uint32, ctypes.c_char_p, ctypes.c_size_t]
    L.rig_get_reg.restype = ctypes.c_uint32
    L.rig_get_reg.argtypes = [ctypes.c_int]
    L.rig_set_reg.argtypes = [ctypes.c_int, ctypes.c_uint32]
    L.rig_run.argtypes = [ctypes.c_uint32]
    return L


_RIG_MODULE = None


class Rig:
    """One Musashi instance with the game image at 0x10000 (the rig keeps a single global machine: every Rig() shares it,
    and a fresh Rig() re-initialises the same memory block rather than allocating another 14 MB; build one per script)."""
    NAMES = ["d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"]

    def __init__(self):
        global _RIG_MODULE
        if _RIG_MODULE is None: _RIG_MODULE = load_rig_module()
        self.L = _RIG_MODULE
        # RAM up to 0xE00000: the custom-chip range 0xDFF000 is plain RAM here (no chip model: a read returns the last value written, or 0), so the
        # blitter-busy waits of the drawer end; the stack top is 0xDFFF00
        self.L.rig_init(0xE00000)

    def reset(self):
        self.L.rig_load_image(IMGPATH.encode(), 0x10000)
        z = bytes(0x54EC0 - 0x4A6E8)
        self.L.rig_poke(0x4A6E8, z, len(z))

    def peek(self, a, n):
        import ctypes
        b = ctypes.create_string_buffer(n)
        self.L.rig_peek(a, b, n)
        return b.raw

    def poke(self, a, data):
        self.L.rig_poke(a, bytes(data), len(data))

    def u8(self, a): return self.peek(a, 1)[0]
    def s16(self, a): return struct.unpack(">h", self.peek(a, 2))[0]
    def u16(self, a): return struct.unpack(">H", self.peek(a, 2))[0]
    def s32(self, a): return struct.unpack(">i", self.peek(a, 4))[0]
    def u32(self, a): return struct.unpack(">I", self.peek(a, 4))[0]
    def w8(self, a, v): self.poke(a, struct.pack(">B", v & 0xFF))
    def w16(self, a, v): self.poke(a, struct.pack(">H", v & 0xFFFF))
    def w32(self, a, v): self.poke(a, struct.pack(">I", v & 0xFFFFFFFF))

    def run(self, addr, **kw):
        for i in range(16):
            self.L.rig_set_reg(i, 0)
        for k, v in kw.items():
            self.L.rig_set_reg(self.NAMES.index(k), v & 0xFFFFFFFF)
        return self.L.rig_run(addr)

    def reg(self, n): return self.L.rig_get_reg(self.NAMES.index(n))


_LOG = {}


def load_log():
    import logparse
    if LOGPATH not in _LOG:
        _LOG[LOGPATH] = logparse.load(LOGPATH)
    return _LOG[LOGPATH]


def inputs_missing():
    """Names of the user's git-ignored inputs that are absent (the checks skip when this is not empty)."""
    return [os.path.relpath(p, ROOT) for p in (IMGPATH, LOGPATH, LIBPATH) if not os.path.exists(p)]


def load_scene(rig, tag):
    """Load the logged state of scene `tag` ('deck' or 'air') and prepare the builder like test_lists_diff.c."""
    log = load_log()
    rig.reset()
    rig.run(0x1F31C)                                    # occupancy maps
    rig.poke(0x53870, b"".join(i.to_bytes(2, "big") for i in range(22)))   # sort index table
    for lab, addr in (("s71_", 0x4A700), ("recs_", 0x4B23C), ("sec_", 0x4D23C)):
        rig.poke(addr, log[lab + tag].data)
    return log


def run_builder(rig, request=0x0B):
    rig.w8(0x4A910, request)
    return rig.run(0x1F770, a6=0x70000)
