"""IFF ILBM decoder (Electronic Arts Interchange File Format, 1985).

Only what the game's pictures need: BMHD, CMAP, BODY, ByteRun1 compression,
optional mask plane. Output is one byte per pixel, row-major.
"""
from dataclasses import dataclass
import struct


@dataclass
class Ilbm:
    width: int
    height: int
    nplanes: int
    palette: list
    pixels: bytes


def _chunks(data: bytes, start: int, end: int):
    pos = start
    while pos + 8 <= end:
        cid = data[pos:pos + 4]
        (length,) = struct.unpack(">I", data[pos + 4:pos + 8])
        yield cid, data[pos + 8:pos + 8 + length]
        pos += 8 + length + (length & 1)


def unpack_byterun1(src: bytes, out_len: int) -> bytes:
    out = bytearray()
    i = 0
    while len(out) < out_len and i < len(src):
        n = src[i]
        i += 1
        if n < 128:
            out += src[i:i + n + 1]
            i += n + 1
        elif n > 128:
            out += bytes([src[i]]) * (257 - n)
            i += 1
        # n == 128 is a no-op by definition
    return bytes(out[:out_len])


def decode_ilbm(data: bytes) -> Ilbm:
    if len(data) < 12 or data[:4] != b"FORM" or data[8:12] != b"ILBM":
        raise ValueError("not an IFF ILBM picture")
    (form_len,) = struct.unpack(">I", data[4:8])
    bmhd = cmap = body = None
    for cid, chunk in _chunks(data, 12, 8 + form_len):
        if cid == b"BMHD":
            bmhd = chunk
        elif cid == b"CMAP":
            cmap = chunk
        elif cid == b"BODY":
            body = chunk
    if bmhd is None or body is None:
        raise ValueError("ILBM is missing BMHD or BODY")
    (w, h, _x, _y, nplanes, masking, compression, _pad, _transp,
     _xa, _ya, _pw, _ph) = struct.unpack(">HHhhBBBBHBBhh", bmhd[:20])
    row_bytes = ((w + 15) // 16) * 2
    rows_per_pixel_row = nplanes + (1 if masking == 1 else 0)
    needed = row_bytes * rows_per_pixel_row * h
    raw = body if compression == 0 else unpack_byterun1(body, needed)
    if len(raw) < needed:
        raise ValueError("ILBM body is truncated")
    pixels = bytearray(w * h)
    pos = 0
    for row in range(h):
        base = row * w
        for plane in range(rows_per_pixel_row):
            plane_row = raw[pos:pos + row_bytes]
            pos += row_bytes
            if plane >= nplanes:
                continue  # mask plane
            bit = 1 << plane
            for x in range(w):
                if plane_row[x >> 3] & (0x80 >> (x & 7)):
                    pixels[base + x] |= bit
    palette = []
    if cmap:
        palette = [(cmap[i], cmap[i + 1], cmap[i + 2]) for i in range(0, len(cmap) - 2, 3)]
    return Ilbm(w, h, nplanes, palette, bytes(pixels))
