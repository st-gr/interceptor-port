"""Synthetic bytes for the decoder tests, built from the layouts in
docs/re/02-assets-and-tables.md. Nothing here comes from the game."""
import struct

BASE = 0x1000          # flat address of byte 0 of a synthetic image


class Img:
    """A big-endian byte image addressed by flat addresses."""

    def __init__(self, size=0x4000, base=BASE):
        self.base = base
        self.b = bytearray(size)

    def w(self, addr, *words):
        for i, v in enumerate(words):
            struct.pack_into(">H", self.b, addr - self.base + 2 * i, v & 0xFFFF)
        return addr + 2 * len(words)

    def l(self, addr, *longs):
        for i, v in enumerate(longs):
            struct.pack_into(">I", self.b, addr - self.base + 4 * i, v & 0xFFFFFFFF)
        return addr + 4 * len(longs)

    def bytes_(self, addr, data):
        self.b[addr - self.base:addr - self.base + len(data)] = data
        return addr + len(data)

    @property
    def data(self):
        return bytes(self.b)


def make_hunk_exe(size_bytes, patches=None):
    """A one-hunk AmigaDOS executable (loaded at 0x10000) of zero code, for CLI tests.
    ``patches`` maps flat addresses to bytes written into it."""
    longs = size_bytes // 4
    code = bytearray(longs * 4)
    for addr, data in (patches or {}).items():
        code[addr - 0x10000:addr - 0x10000 + len(data)] = data
    return (struct.pack(">IIIII", 0x3F3, 0, 1, 0, 0) + struct.pack(">I", longs) +
            struct.pack(">II", 0x3E9, longs) + bytes(code) + struct.pack(">I", 0x3F2))


def flight_patches():
    """Bytes a zero image needs so that the flight tables (extract/flight.py) validate: the pad
    terminator entry (cell x -1) and word 0xFFFF in every pointer list (no pointers)."""
    patches = {0x47B72 + 16 * i + 2: b"\xff\xfe" for i in range(4)}   # negative cell x: entry skipped
    patches[0x47B72 + 4 * 16 + 2] = b"\xff\xff"
    for addr, n in ((0x3E0F8, 124), (0x3E174, 44), (0x3EDCC, 32), (0x3EDEC, 44)):
        patches[addr] = b"\xff" * n
    patches.update(lists_patches())
    patches.update(hull_patches())
    patches.update(burst_text_patches())
    patches.update(scenario_patches())
    patches.update(text_font_patches())
    patches.update(mapshapes_patches())
    return patches


def hull_patches():
    """The hull pointer table 0x3DACC (note 07 8.2a) and its three lists on a zero image: six faces (slot 1, 2, 3) and a terminator each."""
    out = {0x3DACC: struct.pack(">3I", 0x3DAFA, 0x3DD28, 0x3DD94)}
    for a in (0x3DAFA, 0x3DD28, 0x3DD94):
        out[a] = _be(*([6, 12, 18] * 6 + [0xFFFF, 0, 0]))
    return out


def burst_text_patches():
    """What a zero image needs so that the burst tables, the target text and the month names validate (extract/combat.py, versions 16): the pointer table
    0x315A2, the pen tables and the shape cycle the extractor checks, 26 blanks, and invented strings."""
    out = {0x315A2: struct.pack(">8I", *[0x315C2 + 18 * k for k in range(8)])}
    for a, pen in zip((0x316E5, 0x316F1, 0x316FD, 0x31709, 0x31715), (4, 7, None, 12, 1)):
        out[a] = bytes([11, 3] * 6) if pen is None else bytes([pen] * 12)
    out[0x31722] = _be(*[4 * ((5 * i + 2) % 8) for i in range(32)])   # an invented cycle of shape offsets
    out[0x3717C] = b" " * 26 + b"\0"
    for i, lab in enumerate((b"lab1", b"lab2", b"lab3")):
        out[0x37197 + 5 * i] = lab + b"\0"
    for i in range(8):
        out[0x371A6 + 7 * i] = (b"nm%d..." % i)[:6] + b"\0"
    out[0x36923] = b"ab cde" + b" fghij" + b"\0"   # the two fire prompts (version 19): invented
    out[0x31542] = _be(*[(-1) ** i * (i % 5 + 1) for i in range(18)])   # the flare star (version 22): invented offsets
    # the briefing chain (version 23): invented ids, rows at the byte offsets 0, 10, 22, 32, 42, 52 from 0x43EE6, a stray word between the second and third rows
    for r, ro in enumerate((0, 10, 22, 32, 42, 52)):
        out[0x43EE6 + ro] = _be(*[3 + 7 * r + i for i in range(5)])
    out[0x1E800] = bytes([0, 3, 9, 27, 229, 247, 253, 40, 40, 40, 17, 239, 33, 33])   # the head step bytes (version 23): signed, invented
    out[0x292CC] = b"".join((b"mon%d" % i).ljust(9, b"x")[::-1] for i in range(4))
    return out


# The invented map shapes (note 06 "Marks": structure only): 17 offset words, 16 direction shapes of 5 or 6 rows, a ship shape of 3 rows, each shape ended by 0x80;
# 324 bytes in all, the size the extractor reads. Sector 0 has five rows, the widest at the bottom. SYNTH_MAPSHAPES_FNV1A is the invented set's hash.
SYNTH_MAPSHAPES_FNV1A = 0xC559A977


def mapshapes_blob():
    shapes = [[(-3, 1, 2), (-2, 0, 2), (-1, -1, 4), (0, 0, 5), (1, -2, 7)]]
    for k in range(1, 16):
        shapes.append([(i - 3, (3 * k + 5 * i) % 7 - 3, 1 + (k + 2 * i) % 6) for i in range(6 if k <= 8 else 5)])
    shapes.append([(-1, 2, 2), (0, -4, 9), (1, -3, 7)])
    body, offsets, pos = b"", [], 34
    for sh in shapes:
        offsets.append(pos)
        enc = b"".join(bytes([dy & 255, dx & 255, ln]) for dy, dx, ln in sh) + b"\x80"
        body += enc
        pos += len(enc)
    blob = b"".join(o.to_bytes(2, "big") for o in offsets[:16] + [offsets[0]]) + body
    assert len(blob) == 324
    return blob


def mapshapes_patches():
    return {0x2FED8: mapshapes_blob()}


LAND_MODEL = 0x4A000   # flat address of the invented landmark model (lists_patches)


def lists_patches():
    """Bytes that make the three pattern blobs of the display-list builder (extract/lists.py) validate on a
    zero image: every offset-table word points at one list of one entry (tile 0, one sub-cell 0, end)."""
    out = {}
    for addr, nwords in ((0x4633A, 4), (0x4659C, 32), (0x4717E, 64)):
        off = 2 * nwords
        out[addr] = (off.to_bytes(2, "big")) * nwords + bytes([0, 1, 0, 0xFF])
    # One invented landmark model whose footprint has a node with a plane count of 0 (extractor version 26): object record 120 (class 0x21D0C,
    # model base LAND_MODEL); the model's leaf word 0x8010 names the block at +0x10, whose first word 0x0020 names the footprint at +0x20: the outer
    # box (5 words), then one node (box 5 words, next-node long 0x80000000 = end, count 0) and the six words that follow the count.
    rec = 0x25ECC + 20 * 120
    out[rec] = struct.pack(">5I", 0x21D0C, LAND_MODEL, 0, 0, 0)
    out[LAND_MODEL] = struct.pack(">H", 0x8010)
    out[LAND_MODEL + 0x10] = struct.pack(">H", 0x0020)
    out[LAND_MODEL + 0x20] = struct.pack(">5h", 900, -50, 60, -70, 80) + struct.pack(">5hIH", 700, -40, 50, -60, 70, 0x80000000, 0) + struct.pack(">6h", 11, -12, 13, 3, -4, 5)
    return out


def _be(*words):
    return b"".join(struct.pack(">H", w & 0xFFFF) for w in words)


SCENARIO_START = [(11, 12, 5, 21, 22, 23, -24, 37), (-32760, 0, 0, 0, 0, 0, 0, 0)]
CAMERA_POINTS = [(31, 41, 3, 59, 265, 358), (-97, 93, 2, 38, 462, 643), (-3, 27, 9, 50, 28, 841)]   # invented
SCENARIO_REGIONS = [   # (box, count word, entries); an entry's first word is below 0x8000 and its last word is arbitrary
    ((1, 2, 3, 4), 0, [(40, 7, 9, 31, 0x8001)]),
    ((5, 6, 7, 8), 0, []),
    ((9, 10, 11, 12), 1, [(80, 3, 4097, 33, 2), (81, 4, 5, 6, 7)]),
    ((13, 14, 15, 16), 1, [(140, 9, 0x8123, 35, 0x0F00)]),
]
SCENARIO_ENTRY = lambda i: (100 + 3 * i, 5 + i, 0x8003 + i, 50 + i, 0x4001)
# list A at table offset 120: heads (8, 20, 20, 22); S0: 1 entry, S1: none (shared by heads 1, 2), S3: 2 entries
# list B at table offset 164: all four heads one sub-list of 1 entry. Rows 0..8 use list A, row 9 list B.
SCENARIO_ROW_COUNTS = [2, 2, 2]


def scenario_patches():
    """A small valid scenario area (start blocks, region table, placement table with two lists) for the
    zero-image tests and for tests/test_flow.py, in the layout of docs/re/02 section 9 and docs/re/05 17.14."""
    out = {}
    blk = b"".join(struct.pack(">8h", *b) for b in SCENARIO_START) + struct.pack(">8h", -1, 0, 0, 0, 0, 0, 0, 0)
    out[0x47ADE] = blk
    for i, pt in enumerate(CAMERA_POINTS):   # three records of six words, 16 bytes apart (the code shifts the index left by 4)
        out[0x47B30 + 16 * i] = _be(*pt, 0x7777, 0x7778)
    addr = 0x2DE3A   # region bodies sit after the pointer table; the table is 4 absolute 32-bit pointers
    for r, (box, cnt, ents) in enumerate(SCENARIO_REGIONS):
        out[0x2DE28 + 4 * r] = _be(addr >> 16, addr & 0xFFFF)
        body = _be(*box, cnt) + b"".join(_be(*e) for e in ents) + _be(0xFFFF)
        out[addr] = body
        addr += len(body)
    row = lambda lo, c: _be(lo, c) * 3
    rows = b"".join(row(120, 2) for _ in range(9)) + row(164, 6)
    out[0x2DEDA] = rows
    lA = _be(8, 20, 20, 22) + _be(*SCENARIO_ENTRY(0)) + _be(0xFFFF) + _be(0xFFFF) + _be(*SCENARIO_ENTRY(1)) + _be(*SCENARIO_ENTRY(2)) + _be(0xFFFF)
    lB = _be(8, 8, 8, 8) + _be(*SCENARIO_ENTRY(3)) + _be(0xFFFF)
    assert len(lA) == 44 and 120 + len(lA) == 164
    out[0x2DEDA + 120] = lA
    out[0x2DEDA + 164] = lB
    return out


def text_font_patches():
    """A minimal text area (one one-line record named by every id but the four non-records, whose offset is 0) and a
    font whose 64 characters all share one blank glyph, for the zero-image tests."""
    rec = bytes([10, 4, 0, 0x23]) + b"INVENTED" + bytes([0, 0, 0xFF])
    offs = [0x126 if i not in (18, 19, 21, 25) else 0 for i in range(1, 111)]
    return {0x43DFE: b"".join(struct.pack(">H", o) for o in offs), 0x43F24: rec,
            0x4283C: struct.pack(">64H", *([0x80] * 64)), 0x428BC: bytes(7),
            0x274E2: b"".join(struct.pack(">II", 0x103, 0x80000000 | (0x0B + i)) for i in range(8))}   # training table B: scripts have bit 31 set
