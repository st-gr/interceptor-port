import struct
import pytest
from extract.ilbm import decode_ilbm, unpack_byterun1


def make_ilbm(width, height, nplanes, planes_rows, palette, compression=0, masking=0):
    """planes_rows: list over rows of list over planes of row bytes (already packed)."""
    bmhd = struct.pack(">HHhhBBBBHBBhh", width, height, 0, 0, nplanes, masking,
                       compression, 0, 0, 10, 11, width, height)
    cmap = b"".join(bytes(c) for c in palette)
    body = b"".join(b"".join(row) for row in planes_rows)

    def chunk(cid, data):
        return cid + struct.pack(">I", len(data)) + data + (b"\0" if len(data) & 1 else b"")

    inner = b"ILBM" + chunk(b"BMHD", bmhd) + chunk(b"CMAP", cmap) + chunk(b"BODY", body)
    return b"FORM" + struct.pack(">I", len(inner)) + inner


def test_decodes_two_plane_uncompressed_image():
    # 16x2, two planes. Row 0: plane0 = 0b1010..., plane1 = 0b1100...
    rows = [
        [bytes([0b10101010, 0]), bytes([0b11001100, 0])],
        [bytes([0, 0]), bytes([0, 0])],
    ]
    data = make_ilbm(16, 2, 2, rows, [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)])
    img = decode_ilbm(data)
    assert (img.width, img.height, img.nplanes) == (16, 2, 2)
    assert img.palette == [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)]
    # pixel x: bit from plane0 is bit0, from plane1 is bit1
    assert list(img.pixels[:8]) == [3, 2, 1, 0, 3, 2, 1, 0]
    assert list(img.pixels[16:24]) == [0] * 8


def test_mask_plane_is_skipped_and_odd_width_rows_are_word_aligned():
    # width 9 -> row bytes = 2 (padded to a word). masking=1 adds a mask plane per row.
    rows = [[bytes([0xFF, 0x80]), bytes([0xFF, 0xFF])]]  # plane0 then mask
    data = make_ilbm(9, 1, 1, rows, [(0, 0, 0), (255, 255, 255)], masking=1)
    img = decode_ilbm(data)
    assert img.width == 9 and len(img.pixels) == 9
    assert list(img.pixels) == [1] * 9


def test_byterun1_literal_and_repeat_runs():
    # n=2 -> copy 3 literal bytes; n=0xFE (254) -> repeat next byte 257-254=3 times; 0x80 no-op
    src = bytes([2, 10, 20, 30, 0xFE, 7, 0x80])
    assert unpack_byterun1(src, 6) == bytes([10, 20, 30, 7, 7, 7])


def test_compressed_body_is_unpacked():
    # one row, one plane, width 16 -> 2 bytes per row, encoded as a 2-byte literal run
    body_rows = [[bytes([1, 0xAA, 0x55])]]
    data = make_ilbm(16, 1, 1, body_rows, [(0, 0, 0), (9, 9, 9)], compression=1)
    img = decode_ilbm(data)
    assert list(img.pixels) == [1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1]


def test_rejects_non_ilbm():
    with pytest.raises(ValueError):
        decode_ilbm(b"RIFF\0\0\0\0WAVE")
