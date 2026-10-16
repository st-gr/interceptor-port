"""Display-list builder tables (docs/re/04a-display-lists.md): sizes, samples and the real-image checks."""
import struct
from pathlib import Path

import pytest

from extract import lists
from extract.hunk import relocate
from extract.lists import decode_all, decode_pattern_lists
from extract.tables import DecodeError, IMAGE_BASE, RECORD_SIZE, RECORD_TABLE, Mem, decode_record
from extract.terrain import decode_terrain
from tests.synth import make_hunk_exe, flight_patches

REAL = Path(__file__).resolve().parents[3] / "extracted" / "interceptor.bin"
needs_image = pytest.mark.skipif(not REAL.exists(), reason="extracted/interceptor.bin absent")


def synthetic():
    img, _ = relocate(make_hunk_exe(0x3C000, flight_patches()), IMAGE_BASE)
    return img


@pytest.fixture(scope="module")
def real():
    return REAL.read_bytes()


@pytest.fixture(scope="module")
def out(real):
    return decode_all(real, IMAGE_BASE)


def test_synthetic_image_sizes():
    o = decode_all(synthetic(), IMAGE_BASE)
    sizes = {"lists_quad.bin": 16, "lists_octperm.bin": 32, "lists_group.bin": 16, "lists_subperm.bin": 64,
             "lists_tileperm.bin": 96, "lists_tiles.bin": 42, "lists_steep.bin": 610, "lists_high.bin": 3042,
             "lists_low.bin": 502, "lists_limit.bin": 16, "lists_vis.bin": 48, "lists_desc.bin": 134 * 8,
             "lists_ngclass.bin": 1024, "lists_ngpts.bin": 1452, "lists_ngshape.bin": 72}
    for k, n in sizes.items():
        assert len(o[k]) == n, k
    # the one invented landmark of tests/synth.py (object record 120): outer box, one node with a plane count of 0 and the one plane
    # that follows the count (extractor version 26; note 07 8.2 (4))
    import struct as _s
    assert o["lists_land.bin"] == (_s.pack("<5hB", 900, -50, 60, -70, 80, 1) + _s.pack("<5hB", 700, -40, 50, -60, 70, 0) + _s.pack("<6h", 11, -12, 13, 3, -4, 5))
    assert _s.unpack_from("<BBHHH", o["lists_desc.bin"], 8 * 120) == (3, 5, 0, 0xFFFF, 0)


def test_short_image_is_rejected():
    with pytest.raises(DecodeError):
        decode_all(synthetic()[:0x40000 - IMAGE_BASE], IMAGE_BASE)


def test_pattern_list_validation():
    good = struct.pack(">HH", 4, 8) + bytes([3, 2, 1, 2, 0xFF, 0, 1, 0, 0xFF])
    assert decode_pattern_lists(good[:4] + bytes([3, 2, 1, 2, 0xFF]) + bytes([0, 1, 0, 0xFF]), 2, "t")[0] == [(3, [1, 2])]
    for body in (bytes([21, 1, 0, 0xFF]), bytes([0, 0, 0xFF]), bytes([0, 1, 16, 0xFF]), bytes([0, 1, 0] * 15 + [0xFF]), bytes([0, 1, 0])):
        with pytest.raises(DecodeError):
            decode_pattern_lists(struct.pack(">H", 2) + body, 1, "t")
    with pytest.raises(DecodeError):
        decode_pattern_lists(struct.pack(">H", 0x40) + bytes(4), 1, "t")


@needs_image
def test_near_ground_tables_have_the_documented_shape(out, real):
    """Stage [90] of note 04: the class table is monotone along rows and columns (minimum 1, maximum 9), the point tables hold 96, 80 and 64 entries with the class limits 2, 3, 4 and
    the documented kinds, the shapes are the six triangles. The values themselves are compared with the image, never with literals."""
    m = Mem(real, IMAGE_BASE, "t")
    cls = out["lists_ngclass.bin"]
    assert cls == real[0x2C440 - IMAGE_BASE:0x2C440 - IMAGE_BASE + 1024]
    rows = [cls[32 * r:32 * r + 32] for r in range(32)]
    assert min(cls) == 1 and max(cls) == 9
    assert all(list(r) == sorted(r) for r in rows) and all(list(c) == sorted(c) for c in zip(*rows))
    pts = struct.unpack("<726h", out["lists_ngpts.bin"])
    assert pts == tuple(m.s16(0x2C840 + 2 * i) for i in range(726))
    p = 0
    for count, limit, kinds in ((96, 2, {1, 2, -12, -16, -20}), (80, 3, {1, 2, -16, -20}), (64, 4, {1, -20})):
        assert pts[p:p + 2] == (count, limit)
        trip = [pts[p + 2 + 3 * i:p + 5 + 3 * i] for i in range(count)]
        assert {t[2] for t in trip} == kinds
        assert all(-3744 <= t[0] <= 3776 and -3968 <= t[1] <= 3776 for t in trip)
        # the class lookup reaches row <= 19 and column <= 18 only (|x + cx'| at most 4,800 with the lattice offset in -1,023 .. 1,024), so it never leaves the 32 by 32 table
        assert max(abs(t[0]) + 1024 for t in trip) >> 8 <= 18 and max(abs(t[1]) + 1024 for t in trip) >> 8 <= 19
        p += 2 + 3 * count
    assert p == 726
    sh = struct.unpack("<36h", out["lists_ngshape.bin"])
    assert sh == tuple(m.s16(0x2CDEC + 2 * i) for i in range(36))
    for i in range(6):                              # structure only: isosceles triangles, apex on the axis (dx = 0), the base behind it
        (x0, z0, x1, z1, x2, z2) = sh[6 * i:6 * i + 6]
        assert x1 == -x0 and z1 == z0 and x2 == 0 and z0 < 0 < z2
    assert all(abs(sh[6 * (2 * k + 1)]) > abs(sh[6 * 2 * k]) for k in range(3))      # the scaled shape of a kind is bigger than the unscaled


@needs_image
def test_small_tables_samples(out, real):
    assert list(out["lists_quad.bin"][:4]) == [0, 0, 1, 1] and list(out["lists_quad.bin"][8:12]) == [2, 2, 3, 3]
    assert list(out["lists_group.bin"][:4]) == [0, 1, 1, 0]
    o = out["lists_octperm.bin"]
    assert list(o[:8]) == list(range(8)) and list(o[8:16]) == list(range(7, -1, -1))
    assert list(o[16:20]) == [3, 2, 1, 0] and list(o[24:28]) == [4, 5, 6, 7]
    s = out["lists_subperm.bin"]
    assert list(s[:16]) == list(range(16)) and list(s[16:24]) == [3, 2, 1, 0, 7, 6, 5, 4]
    t = out["lists_tileperm.bin"]
    assert list(t[:21]) == list(range(21)) and list(t[24:33]) == [2, 1, 0, 5, 4, 3, 8, 7, 6]
    tiles = [struct.unpack("bb", out["lists_tiles.bin"][2 * i:2 * i + 2]) for i in range(21)]
    assert tiles[0] == (1, 1) and tiles[1] == (0, 1) and tiles[2] == (-1, 1) and tiles[3] == (1, 0)
    assert tiles[4] == (0, 0) and tiles[20] == (-1, -2)
    # the limit table: (without gate, with gate) by band D = 0..3, in this order
    lim = struct.unpack("<8h", out["lists_limit.bin"])
    assert lim == tuple(Mem(real, IMAGE_BASE, "t").s16(lists.LIMIT_ADDR + 2 * i) for i in range(8))     # the words themselves are the publisher's data: compare with the image
    assert all(lim[2 * d + 1] > lim[2 * d] for d in range(4)) and list(lim[0::2]) == sorted(lim[0::2]) and list(lim[1::2]) == sorted(lim[1::2])   # the gate raises the limit; both columns grow with the band
    # the pre-test tables of note 04a section 8b rule 6: 0x23FF4 then 0x2400C
    # (the words themselves are the publisher's data: compare with the image, never with literals)
    v = struct.unpack("<24h", out["lists_vis.bin"])
    m = Mem(real, IMAGE_BASE, "t")
    assert v == tuple(m.s16(0x23FF4 + 2 * i) for i in range(24))
    assert v[0] == 0 and v[12] == 0 and list(v[:12]) == sorted(v[:12]) and list(v[12:]) == sorted(v[12:])


@needs_image
def test_pattern_blob_samples(out):
    def lists_of(name, n):
        b = out[name]
        offs = struct.unpack(">%dH" % n, b[:2 * n])
        return b, offs, decode_pattern_lists(b, n, name)
    b, offs, ls = lists_of("lists_high.bin", 32)
    assert offs[0] == 0x40
    assert [(t, len(s)) for t, s in ls[0]] == [(0, 13), (2, 9), (1, 16), (5, 3), (3, 1), (4, 4)]
    b, offs, ls = lists_of("lists_steep.bin", 4)
    assert offs[0] == 8 and len(ls[0]) == 13
    b, offs, ls = lists_of("lists_low.bin", 64)
    assert offs[0] == 0x80 and [(t, len(s)) for t, s in ls[0]] == [(t, 1) for t, _ in ls[0]] and len(ls[0]) == 4


@needs_image
def test_lod_and_hypotenuse_tables_match_the_note(real):
    lod = real[0x20E4A - IMAGE_BASE:0x20E4A - IMAGE_BASE + 240]
    assert lod[0] == 0 and lod[1] == 1 and lod[12] == 4 and lod[239] == 15 and len(set(lod)) == 16
    m = Mem(real, IMAGE_BASE)
    t = [m.u16(0x208E8 + 2 * i) for i in range(257)]
    assert (t[0], t[128], t[256]) == (16384, 18318, 23170)
    import math
    from decimal import Decimal
    for r in range(257):
        v = (Decimal(r * r) / Decimal(256 * 256) + 1).sqrt() * 16384
        assert t[r] == int(v + Decimal("0.5")), r


@needs_image
def test_origin_tables_follow_the_formulas(real):
    m = Mem(real, IMAGE_BASE)
    tiles = [(m.s8(0x20674 + 2 * i), m.s8(0x20675 + 2 * i)) for i in range(21)]
    w16 = lambda v: ((v + 0x8000) & 0xFFFF) - 0x8000
    for i, (dx, dz) in enumerate(tiles):
        # tiers A and B: 16384 * d, stored as a 16-bit word (a tile two cells away wraps: 32768 reads -32768)
        assert (m.s16(0x2069E + 4 * i), m.s16(0x2069E + 4 * i + 2)) == (w16(16384 * dx), w16(16384 * dz))
        assert (m.s16(0x20732 + 4 * i), m.s16(0x20732 + 4 * i + 2)) == (4096 * dx, 4096 * dz)
    for n in range(16):
        assert (m.s16(0x206F2 + 4 * n), m.s16(0x206F2 + 4 * n + 2)) == (4096 * (3 - n % 4), 4096 * (3 - n // 4))
        assert (m.s16(0x20786 + 4 * n), m.s16(0x20786 + 4 * n + 2)) == (1024 * (3 - n % 4), 1024 * (3 - n // 4))


def desc_of(out, item):
    return struct.unpack_from("<BBHHH", out["lists_desc.bin"], 8 * (item + 16))


def footprint(out, off):
    b, land = off, out["lists_land.bin"]
    outer = struct.unpack_from("<5h", land, b)
    n = land[b + 10]
    p, nodes = b + 11, []
    for _ in range(n):
        box = struct.unpack_from("<5h", land, p)
        k = land[p + 10]
        nodes.append((box, k))
        p += 11 + 12 * max(k, 1)   # a node with a count of 0 carries one plane (version 26)
    return outer, nodes


@needs_image
def test_descriptor_classes_and_landmarks(out, real):
    assert len(out["lists_desc.bin"]) == 134 * 8
    qual = [r - 16 for r in range(134) if out["lists_desc.bin"][8 * r + 1] & 2]
    assert qual == [42, 70, 88]
    assert desc_of(out, 45)[0] == 1 and desc_of(out, 42)[0] == 3 and desc_of(out, 17)[0] == 3
    assert struct.unpack_from("<B", out["lists_desc.bin"], 0)[0] == 6        # template record 0: 0x21C44
    assert out["lists_desc.bin"][8 * 16] == 0                                 # run-time slot: all zero
    # reference planes: item 42 has c = 3 (four planes); 70 and 88 share one aux block
    c, flags, foot, ref, _ = desc_of(out, 42)
    land = out["lists_land.bin"]
    assert struct.unpack_from("<H", land, ref)[0] == 3
    r70, r88 = desc_of(out, 70)[3], desc_of(out, 88)[3]
    n = 2 + 12 * 4
    assert land[r70:r70 + n] == land[r88:r88 + n]
    m = Mem(real, IMAGE_BASE)
    assert decode_record(real, RECORD_TABLE - IMAGE_BASE + RECORD_SIZE * (42 + 16), 58).aux == 0x406B2
    assert m.u32(0x406B2 + 6) == 0x406E2
    first = struct.unpack_from("<6h", land, ref + 2)
    assert first == tuple(m.s16(0x406E2 + 2 * i) for i in range(6))


@needs_image
def test_footprints_of_the_table_c_items(out, real):
    t = decode_terrain(real)
    ids = sorted({it.obj for c in t.tables[2][3] for it in t.lists[c.list_index]})
    assert ids == [32, 33, 34, 35, 36, 37, 38, 42, 45, 46, 47, 48, 49, 70, 74, 86, 87, 88, 91, 92, 117]
    m = Mem(real, IMAGE_BASE)
    missing = []
    for i in ids:
        cls, flags, foot, ref, _ = desc_of(out, i)
        if not flags & 4:
            missing.append(i)
            continue
        outer, nodes = footprint(out, foot)
        assert 1 <= len(nodes) <= 4 and all(k <= 8 for _, k in nodes)
        assert 4 + 10 + sum(14 + 6 * k for _, k in nodes) <= 256
    # NOTE-GAP: these models have the part-id word 0xFFFF at their nearest block, so the original's footprint
    # pointer (model base + 0xFFF) points at arbitrary image bytes; the extractor writes no footprint for them
    assert missing == [32, 33, 34, 35, 36, 37, 38, 49, 117]
    for i in missing:
        M = decode_record(real, RECORD_TABLE - IMAGE_BASE + RECORD_SIZE * (i + 16), 0).base
        blk = M + (lists._first_leaf(m, M) & 0xFFF)
        assert m.u16(blk) == 0xFFFF
    outer, nodes = footprint(out, desc_of(out, 42)[2])
    assert outer == (0x500, -2304, 2304, -2304, 2304) and [k for _, k in nodes] == [4]
    outer, nodes = footprint(out, desc_of(out, 86)[2])
    assert outer == (0x350, -376, 376, -376, 376) and [k for _, k in nodes] == [5]


@needs_image
def test_terrain_rows_are_z_and_keys_are_x(real):
    t = decode_terrain(real)
    name, shift, grid, cells = t.tables[2]
    assert (name, grid) == ("C", 128)
    row65 = [c for c in cells if c.z == 65]
    assert [c.x for c in row65] == [63]
    items = t.lists[row65[0].list_index]
    assert (items[0].layer, items[0].obj, items[0].dx, items[0].dz) == (0, 45, 3840, 256)
    assert not any(c.x == 70 and c.z == 65 for c in cells)
    assert [(c.z, c.x) for c in cells] == sorted((c.z, c.x) for c in cells)
