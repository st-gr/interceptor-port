"""Flat-image access shared by the decoders, and the small fixed tables.

Every decoder is a pure function over bytes: ``data`` is the relocated flat
image (or any byte string laid out the same way) and addresses are the note's
flat addresses, converted with ``base`` (0x10000 for the real image).
"""
import struct
from dataclasses import dataclass

IMAGE_BASE = 0x10000

# Fixed table addresses (docs/re/02-assets-and-tables.md, hunk numbers in the note).
RECORD_TABLE = 0x25ECC
RECORD_SIZE = 20
RECORD_COUNT = 134
DEFAULT_OFFSET_TABLE = 0x207C6       # 16 pairs of signed bytes
LOD_TABLE = 0x20E4A                  # 240 bytes (note 03)
LOD_SIZE = 240
RANGE_TABLE = 0x4621C                # 64 bytes: visibility range per view distance >> 7 (note 02 Constants)
RANGE_SIZE = 64
CAMERA_RING = {                      # note 04: h26 + 0x830, 0x86C, 0x8A8, 0x8E4
    "default": 0x2CE34 + 0x830,
    "class11": 0x2CE34 + 0x86C,
    "class14": 0x2CE34 + 0x8A8,
    "class30": 0x2CE34 + 0x8E4,
}
CAMERA_RING_SIZE = 60


class DecodeError(ValueError):
    """A malformed or unsupported table; the message names table and address."""


class BlockedError(DecodeError):
    """A documented BLOCKING item (note 02): the data is well formed but the
    note cannot say what it means. Lenient decoding can flag it and go on."""


class Mem:
    """Big-endian reads at flat addresses with a context label for errors."""

    def __init__(self, data: bytes, base: int = IMAGE_BASE, ctx: str = "image"):
        self.data = data
        self.base = base
        self.ctx = ctx

    def fail(self, msg: str, addr: int = None):
        where = "" if addr is None else f" at 0x{addr:X}"
        raise DecodeError(f"{self.ctx}{where}: {msg}")

    def _chk(self, addr: int, n: int):
        o = addr - self.base
        if o < 0 or o + n > len(self.data):
            self.fail(f"read of {n} bytes outside the image", addr)
        return o

    def u8(self, addr):
        return self.data[self._chk(addr, 1)]

    def u16(self, addr):
        return struct.unpack_from(">H", self.data, self._chk(addr, 2))[0]

    def s16(self, addr):
        return struct.unpack_from(">h", self.data, self._chk(addr, 2))[0]

    def u32(self, addr):
        return struct.unpack_from(">I", self.data, self._chk(addr, 4))[0]

    def s8(self, addr):
        v = self.u8(addr)
        return v - 256 if v >= 128 else v

    def raw(self, addr, n):
        o = self._chk(addr, n)
        return self.data[o:o + n]

    def has(self, addr, n=1):
        o = addr - self.base
        return o >= 0 and o + n <= len(self.data)


@dataclass
class Record:
    id: int
    handler: int
    base: int
    detail_list: int
    alt_list: int
    aux: int


def decode_record(data: bytes, offset: int, rec_id: int = 0) -> Record:
    """One 20-byte object record (five big-endian longs) at byte ``offset``."""
    if offset < 0 or offset + RECORD_SIZE > len(data):
        raise DecodeError(f"record table: record {rec_id} at offset 0x{offset:X} is truncated")
    return Record(rec_id, *struct.unpack_from(">5I", data, offset))


def decode_record_table(data: bytes, offset: int, count: int = RECORD_COUNT):
    return [decode_record(data, offset + RECORD_SIZE * i, i) for i in range(count)]


def decode_table_bytes(data: bytes, offset: int, size: int, name: str) -> bytes:
    """A raw byte table (level-of-detail shifts, camera rings)."""
    if offset < 0 or offset + size > len(data):
        raise DecodeError(f"{name}: table at offset 0x{offset:X} needs {size} bytes, image has {len(data)}")
    return bytes(data[offset:offset + size])


def decode_lod_table(data: bytes, offset: int) -> bytes:
    t = decode_table_bytes(data, offset, LOD_SIZE, "lod table")
    for i in range(1, LOD_SIZE):
        if t[i] < t[i - 1] or t[i] > 15:
            raise DecodeError(f"lod table: entry {i} at offset 0x{offset + i:X} breaks the non-decreasing 0..15 rule")
    return t


def decode_camera_ring(data: bytes, offset: int, name: str = "camera ring"):
    """Ten camera offsets of three signed big-endian words each (note 04)."""
    raw = decode_table_bytes(data, offset, CAMERA_RING_SIZE, name)
    return [struct.unpack_from(">3h", raw, 6 * i) for i in range(10)]


def camera_ring_bytes(ring) -> bytes:
    """Little-endian i16 x, y, z for each of the ten offsets (60 bytes)."""
    return b"".join(struct.pack("<3h", *e) for e in ring)


def decode_range_table(data: bytes, offset: int) -> bytes:
    """The 64-byte visibility-range table, verbatim (value x 256 = range in world units)."""
    return decode_table_bytes(data, offset, RANGE_SIZE, "range table")
