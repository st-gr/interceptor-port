"""Cockpit data tables (docs/re/06-cockpit-hud.md, "Data" and "Extractor needs").

Pure functions over the relocated flat image, like the other decoders. The warning
table, the one-plane symbol masks and the target-marker point lists are copied verbatim
(big-endian, as in the image); the frontend reads their layouts from note 06.
"""
from .tables import DecodeError, decode_table_bytes

WARNING_ADDR = 0x41FF0         # hunk 59: 54 entries of 28 bytes
WARNING_COUNT = 54
WARNING_SIZE = 28
MASKS_ADDR = 0x42AAC           # hunk 62: gear lamp mask, 4 gear symbols, ball mask, ball table 2, compass strip
MASKS_SIZE = 0x42C00 - 0x42AAC
MARKER_SMALL_ADDR = 0x3948C    # hunk 40: 12 signed byte pairs
MARKER_SMALL_PAIRS = 12
MARKER_LARGE_ADDR = 0x394A6    # 44 signed byte pairs
MARKER_LARGE_PAIRS = 44
ARCS_ADDR = 0x39408            # hunk 40: lock circle quarter arc (20 pairs + terminator pair (-1,-1)), then the range dot arc (19 pairs + terminator pair)
LOCK_ARC_SIZE = 42
RANGE_ARC_SIZE = 40
ARCS_SIZE = LOCK_ARC_SIZE + RANGE_ARC_SIZE
MAPSHAPES_ADDR = 0x2FED8       # hunk 20: the 16 map direction shapes, then the ship shape at 0x30012 (triples (dy, dx, length) ended by 0x80)
MAPSHAPES_SIZE = 0x3001C - 0x2FED8

# Offsets inside masks.bin (the note's addresses minus MASKS_ADDR)
MASK_GEAR_LAMP = 0x42AAC - MASKS_ADDR      # 7 rows of 2 words
MASK_GEAR_SYMBOLS = 0x42AC8 - MASKS_ADDR   # four symbols of 28 bytes
MASK_BALL = 0x42B38 - MASKS_ADDR           # 21 rows of 2 words
MASK_BALL2 = 0x42B8C - MASKS_ADDR          # 8 rows of 4 bytes
MASK_COMPASS = 0x42BAC - MASKS_ADDR        # 7 rows of 12 bytes


def decode_warnings(data: bytes, offset: int) -> bytes:
    """The 54 x 28 byte warning table, verbatim: flag byte, 26 characters, one flag byte."""
    return decode_table_bytes(data, offset, WARNING_COUNT * WARNING_SIZE, "warning table")


def decode_masks(data: bytes, offset: int) -> bytes:
    """The hunk 62 symbol masks, verbatim (340 bytes, layout in the MASK_* offsets)."""
    return decode_table_bytes(data, offset, MASKS_SIZE, "cockpit masks")


def decode_markers(data: bytes, small_offset: int, large_offset: int) -> bytes:
    """The two target-marker point lists as one 112-byte blob: 12 then 44 (dx, dy) signed byte pairs."""
    small = decode_table_bytes(data, small_offset, 2 * MARKER_SMALL_PAIRS, "target marker (small)")
    large = decode_table_bytes(data, large_offset, 2 * MARKER_LARGE_PAIRS, "target marker (large)")
    return small + large


def check_warnings(blob: bytes) -> None:
    """Structure check used by the real-asset test: class bits 1 or 2, printable text."""
    if len(blob) != WARNING_COUNT * WARNING_SIZE:
        raise DecodeError(f"warning table: {len(blob)} bytes, expected {WARNING_COUNT * WARNING_SIZE}")
    for i in range(WARNING_COUNT):
        e = blob[i * WARNING_SIZE:(i + 1) * WARNING_SIZE]
        if (e[0] >> 6) not in (1, 2):
            raise DecodeError(f"warning table: entry {i} has class {e[0] >> 6}")
        if any(not 0x20 <= c < 0x7F for c in e[1:27]):
            raise DecodeError(f"warning table: entry {i} has a non-printable character")


def check_markers(blob: bytes) -> None:
    """The small circle has radius 3 (12 points) and the large radius 10 (44 points, x from -11 to 10)."""
    pairs = [(((blob[2 * i] ^ 128) - 128), ((blob[2 * i + 1] ^ 128) - 128)) for i in range(len(blob) // 2)]
    small, large = pairs[:MARKER_SMALL_PAIRS], pairs[MARKER_SMALL_PAIRS:]
    if max(max(abs(x), abs(y)) for x, y in small) != 3:
        raise DecodeError("target marker (small): extent is not 3")
    if max(max(abs(x), abs(y)) for x, y in large) not in (10, 11):   # the real list spans -11..10 on x
        raise DecodeError("target marker (large): extent is not 10")


def decode_arcs(data: bytes, offset: int) -> bytes:
    """The lock circle arc (42 bytes) and the range dot arc (40 bytes) as one 82-byte blob, verbatim."""
    return decode_table_bytes(data, offset, ARCS_SIZE, "target arcs")


def check_arcs(blob: bytes) -> None:
    """Note 06, 'Lock circle point list' and 'Range dot': the lock arc runs from (0,12) to (14,0) in 20 pairs plus the
    terminator pair (-1,-1); the range dot arc runs from (0,11) to (13,0) in 19 pairs plus the terminator pair (-1,-1)."""
    if len(blob) != ARCS_SIZE:
        raise DecodeError(f"target arcs: {len(blob)} bytes, expected {ARCS_SIZE}")
    s8 = lambda b: (b ^ 128) - 128
    lock = [(s8(blob[2 * i]), s8(blob[2 * i + 1])) for i in range(LOCK_ARC_SIZE // 2)]
    rng = [(s8(blob[LOCK_ARC_SIZE + 2 * i]), s8(blob[LOCK_ARC_SIZE + 2 * i + 1])) for i in range(RANGE_ARC_SIZE // 2)]
    if lock[0] != (0, 12) or lock[19] != (14, 0) or lock[20] != (-1, -1):
        raise DecodeError("target arcs: lock circle arc does not run (0,12) to (14,0) and end with (-1,-1)")
    if rng[0] != (0, 11) or rng[18] != (13, 0) or rng[19] != (-1, -1):
        raise DecodeError("target arcs: range dot arc does not run (0,11) to (13,0) in 19 pairs and end with (-1,-1)")


def decode_mapshapes(data: bytes, offset: int) -> bytes:
    """The map direction table and shapes (324 bytes), verbatim: 17 offset words (the 16 sectors and a repeat of sector 0),
    the 16 direction shapes, then the ship shape."""
    return decode_table_bytes(data, offset, MAPSHAPES_SIZE, "map shapes")


MAPSHAPES_TABLE_WORDS = 17


def split_mapshapes(blob: bytes):
    """(offsets, shapes): the 17 offset words and the shapes as lists of (dy, dx, length) triples, split at the 0x80
    terminators of the bytes after the offset table."""
    n = 2 * MAPSHAPES_TABLE_WORDS
    offsets = [int.from_bytes(blob[2 * i:2 * i + 2], "big") for i in range(MAPSHAPES_TABLE_WORDS)]
    shapes, cur, i = [], [], n
    while i < len(blob):
        if blob[i] == 0x80:
            shapes.append(cur)
            cur = []
            i += 1
            continue
        if i + 3 > len(blob):
            raise DecodeError("map shapes: truncated triple")
        cur.append(tuple((b ^ 128) - 128 for b in blob[i:i + 2]) + (blob[i + 2],))
        i += 3
    if cur:
        raise DecodeError("map shapes: last shape has no terminator")
    return offsets, shapes


# FNV-1a (32 bits) of the bytes of the 17 shapes (everything after the offset words): the one constant that identifies the shipped
# set. The note gives the structure in prose (sector 0: five rows, the widest at the bottom; the ship: three rows) and not the rows.
MAPSHAPES_FNV1A = 0xF434EED5


def fnv1a32(data: bytes) -> int:
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def mapshapes_hash(blob: bytes) -> int:
    """The FNV-1a hash of the shapes' bytes (the 17 shapes with their terminators, without the offset words)."""
    return fnv1a32(blob[2 * MAPSHAPES_TABLE_WORDS:])


def check_mapshapes(blob: bytes, size: int = MAPSHAPES_SIZE, want_hash: int = MAPSHAPES_FNV1A) -> None:
    """Note 06, 'Marks': 16 direction shapes and the ship shape; the offset words point at the shape starts and repeat sector 0;
    every run is at least one pixel long; sector 0 has five rows, the widest at the bottom; the ship shape has three rows. The
    bytes of the shapes must hash to ``want_hash`` (the real set's constant by default; the synthetic disk passes its own)."""
    if len(blob) != size:
        raise DecodeError(f"map shapes: {len(blob)} bytes, expected {size}")
    offsets, shapes = split_mapshapes(blob)
    if len(shapes) != 17:
        raise DecodeError(f"map shapes: {len(shapes)} shapes, expected 17 (16 directions and the ship)")
    start = 2 * MAPSHAPES_TABLE_WORDS
    for k in range(16):
        if offsets[k] != start:
            raise DecodeError(f"map shapes: offset word {k} is {offsets[k]}, the shape starts at {start}")
        start += 3 * len(shapes[k]) + 1
    if offsets[16] != offsets[0]:
        raise DecodeError("map shapes: the 17th offset word does not repeat sector 0")
    for k, shape in enumerate(shapes):
        if not shape or any(ln == 0 for _, _, ln in shape):
            raise DecodeError(f"map shapes: shape {k} is empty or has a run of length 0")
    lens0 = [ln for _, _, ln in shapes[0]]
    if len(lens0) != 5 or lens0[-1] != max(lens0):
        raise DecodeError("map shapes: sector 0 is not five rows with the widest at the bottom")
    if len(shapes[16]) != 3:
        raise DecodeError("map shapes: the ship shape is not three rows")
    if mapshapes_hash(blob) != want_hash:
        raise DecodeError(f"map shapes: the shapes hash to 0x{mapshapes_hash(blob):08X}, expected 0x{want_hash:08X}")
