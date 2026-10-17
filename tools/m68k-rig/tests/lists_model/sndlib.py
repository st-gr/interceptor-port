"""Shared harness of sound_check.py: the sound library's operating-system calls replaced by hooks, the objects, the hardware shim.

The game's sound library (docs/re/10-sound.md) calls a memory allocator, a file-size routine and stdio-like file routines in the C runtime; here they are
hooks: the allocator hands out rig RAM from a bump pointer, the file routines read the user's extracted sample files (assets/sounds/<name>.raw, git-ignored).
"""
import ctypes, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import Rig, ROOT, IMGPATH, LIBPATH

SOUNDS = os.path.join(ROOT, "assets", "sounds")
TABLE = 0x5591C            # 37 object pointers
SLOTS = 0x54ED0            # current object of each voice
HWDESC = 0x54EE0           # four channel descriptors of 0x1C bytes (written by the library's data, not by code)
HEAP = 0x200000
HOOKFN = ctypes.CFUNCTYPE(None)


def have_inputs():
    miss = [p for p in (IMGPATH, LIBPATH, os.path.join(SOUNDS, "textcpt.raw")) if not os.path.exists(p)]
    return miss


class Lib:
    """One rig with the library's OS hooks installed."""

    def __init__(self):
        self.r = Rig()
        self.L = self.r.L
        self._decl()
        self.reset()

    def _decl(self):
        L = self.L
        L.rig_hook_set.argtypes = [ctypes.c_uint32, HOOKFN, ctypes.c_uint32]
        L.rig_hook_result.argtypes = [ctypes.c_uint32]
        L.rig_io_log_set.argtypes = [ctypes.c_int]
        L.rig_io_read_zero.argtypes = [ctypes.c_int]
        L.rig_io_log_get.argtypes = [ctypes.c_void_p, ctypes.c_int]
        L.rig_io_log_get.restype = ctypes.c_size_t
        L.rig_set_max_instructions.argtypes = [ctypes.c_uint32]
        self._keep = []

    def reset(self):
        self.r.reset()
        self.heap = HEAP
        self.cur = None
        self.requested = []          # (name, ...) of every file the loader asked for
        self.r.w32(0x55462, 1)       # the library's "initialised" flag (set by its start-up routine)
        L = self.L
        self._keep = []
        def hook(addr, fn, d0=0):
            cb = HOOKFN(fn); self._keep.append(cb); L.rig_hook_set(addr, cb, d0)
        hook(0x58B60, self._alloc); hook(0x58B78, lambda: None)
        hook(0x55480, self._size); hook(0x56014, self._fopen); hook(0x56428, self._fread); hook(0x5626E, lambda: None)

    def arg(self, i):
        sp = self.r.reg("a7")
        return self.r.u32(sp + 4 + 4 * i)

    def cstr(self, a):
        s = b""
        while True:
            c = self.r.peek(a, 1)
            if c == b"\0": return s.decode()
            s += c; a += 1

    def _alloc(self):
        size = self.arg(0); self.heap = (self.heap + 7) & ~7
        p = self.heap; self.heap += size
        self.r.poke(p, bytes(size))
        self.L.rig_hook_result(p)

    def _file(self, name):
        base = name.split("/")[-1]
        return os.path.join(SOUNDS, base + ".raw")

    def _size(self):
        name = self.cstr(self.arg(0)); self.requested.append(name)
        p = self._file(name)
        self.L.rig_hook_result(os.path.getsize(p) if os.path.exists(p) else 0)

    def _fopen(self):
        name = self.cstr(self.arg(0)); p = self._file(name)
        self.cur = p if os.path.exists(p) else None
        self.L.rig_hook_result(1 if self.cur else 0)

    def _fread(self):
        ptr, size, count = self.arg(0), self.arg(1), self.arg(2)
        data = open(self.cur, "rb").read(size * count)
        self.r.poke(ptr, data)
        self.L.rig_hook_result(count)

    # ---- the objects -------------------------------------------------------------------------------------------------
    def obj_ptr(self, n):
        return self.r.u32(TABLE + 4 * n)

    def obj(self, n):
        p = self.obj_ptr(n)
        if not p: return None
        w = struct.unpack(">16I", self.r.peek(p, 0x40))
        return {"ptr": p, "data": w[0], "len_raw": w[1], "period": w[2], "vol": w[3], "count": w[4], "reload": w[5], "pdelta": w[6], "vdelta": w[7],
                "next": w[8], "wait": w[11], "script": w[12], "pos": w[13], "ptimer": w[14], "vtimer": w[15], "words": w}


class Hw:
    """A minimal Paula shim around the rig: the rig delivers the audio interrupts (rig_irq_audio: INTREQ and DMACON writes of the game raise the channel's
    interrupt, the handler 0x55158 is called between instructions), ``blank`` runs the library's vertical-blank server 0x551D8, ``end_of_block`` raises the
    interrupt of a channel whose sample block ended; the custom-register write log is the witness."""

    def __init__(self, lib):
        self.lib = lib; self.r = lib.r; self.L = lib.L
        self.L.rig_irq_audio.argtypes = [ctypes.c_int, ctypes.c_uint32, ctypes.c_uint32]
        self.L.rig_irq_raise.argtypes = [ctypes.c_int]
        self.L.rig_irq_audio(1, 0x55158, 0x54EC0)
        self.L.rig_io_log_set(1)

    def drain(self):
        """the register writes since the last drain: (pc, register offset, value) with the size in bits 16 and up of the offset stripped"""
        n = self.L.rig_io_log_get(None, 0)
        if n > 65536: raise RuntimeError("register log overflow")
        buf = (ctypes.c_uint32 * (4 * n))()
        self.L.rig_io_log_get(buf, n)
        self.L.rig_io_log_set(1)
        return [(buf[4 * i], buf[4 * i + 1] & 0xFFFF, buf[4 * i + 2]) for i in range(n)]

    def blank(self):
        self.r.run(0x551D8)
        return self.drain()

    def end_of_block(self, ch):
        self.L.rig_irq_raise(ch)
        self.r.run(0x110)
        return self.drain()

    @staticmethod
    def pervol(entries):
        """last AUDxPER and AUDxVOL value written per voice in `entries` (None when not written)"""
        res = [dict(per=None, vol=None) for _ in range(4)]
        for pc, reg, val in entries:
            if 0x0A0 <= reg < 0x0E0:
                ch = (reg - 0x0A0) >> 4; off = (reg - 0x0A0) & 0xF
                if off == 6: res[ch]["per"] = val & 0xFFFF
                elif off == 8: res[ch]["vol"] = val & 0xFFFF
        return res
