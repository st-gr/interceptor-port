import struct

import pytest

from extract.palette import (FONT_HUNK_SIZE, decode_fade_table, decode_font, fade_bytes,
                             palette_bytes)
from extract.tables import DecodeError


def words(*ws):
    return b"".join(struct.pack(">H", w) for w in ws)


def test_fade_table_scales_four_bit_channels_by_17():
    steps = [[(s * 16 + c) & 0xFFF for c in range(16)] for s in range(16)]
    raw = words(*[w for st in steps for w in st])
    t = decode_fade_table(b"\0\0" + raw, 2)
    assert len(t) == 16 and all(len(s) == 16 for s in t)
    assert t[0][5] == (0, 0, 85)            # 0x005
    assert t[1][0] == (0, 17, 0)            # 0x010
    flat = fade_bytes(t)
    assert len(flat) == 16 * 16 * 3 and flat[:3 * 16] == palette_bytes(t[0])
    assert palette_bytes([(1, 2, 3), (4, 5, 6)]) == bytes([1, 2, 3, 4, 5, 6])


def test_fade_table_rejects_non_12_bit_colour_naming_step_colour_and_offset():
    raw = bytearray(words(*([0] * 256)))
    struct.pack_into(">H", raw, 2 * (3 * 16 + 4), 0x1234)
    with pytest.raises(DecodeError, match=r"step 3 colour 4 at offset 0x68 is 0x1234"):
        decode_fade_table(bytes(raw), 0)


def test_fade_table_truncated():
    with pytest.raises(DecodeError, match="fade table"):
        decode_fade_table(bytes(100), 0)


def font_hunk(glyphs):
    """64 offset words, then the 5-byte glyphs; glyphs maps char index -> bytes."""
    blank = 128
    out = bytearray(FONT_HUNK_SIZE)
    for i in range(64):
        struct.pack_into(">H", out, 2 * i, blank)
    nxt = 136
    for i, g in glyphs.items():
        struct.pack_into(">H", out, 2 * i, nxt)
        out[nxt:nxt + 5] = g
        nxt += 5
    return bytes(out)


def test_font_glyphs_are_five_rows_indexed_by_code_minus_0x20():
    a = bytes([0xE0, 0xA0, 0xE0, 0xA0, 0xA0])
    h = font_hunk({0x41 - 0x20: a})
    g = decode_font(h, 0)
    assert len(g) == 64 and g[0x41 - 0x20] == a and g[0] == bytes(5)


def test_font_glyph_pointing_outside_the_hunk_names_glyph_and_offset():
    h = bytearray(font_hunk({}))
    struct.pack_into(">H", h, 2 * 7, FONT_HUNK_SIZE - 2)
    with pytest.raises(DecodeError, match=r"glyph 7 \(code 0x27\) at offset 0xE points to"):
        decode_font(bytes(h), 0)


def test_font_glyph_with_pixels_outside_three_columns_is_rejected():
    h = font_hunk({1: bytes([0xE0, 0xE0, 0xE0, 0xE0, 0x01])})
    with pytest.raises(DecodeError, match="outside the three pixel columns"):
        decode_font(h, 0)
