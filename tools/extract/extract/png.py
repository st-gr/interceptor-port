"""Minimal indexed-colour PNG writer. No dependencies beyond zlib's CRC and Adler checksums.

The image data is a zlib stream of stored (uncompressed) deflate blocks, written by hand, so
that the JavaScript port (tools/extract-js/src/png.js) can produce the same bytes without a
deflate implementation: both ports must agree byte for byte. Blocks hold at most 65,535 bytes;
the last one carries the final flag."""
import struct
import zlib

STORED_BLOCK_MAX = 0xFFFF


def _chunk(cid: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + cid + data
            + struct.pack(">I", zlib.crc32(cid + data) & 0xFFFFFFFF))


def zlib_stored(raw: bytes) -> bytes:
    """A zlib stream (header 0x78 0x01, stored blocks, Adler-32) of ``raw``."""
    out = bytearray(b"\x78\x01")
    pos = 0
    while True:
        n = min(STORED_BLOCK_MAX, len(raw) - pos)
        final = 1 if pos + n >= len(raw) else 0
        out += struct.pack("<BHH", final, n, n ^ 0xFFFF) + raw[pos:pos + n]
        pos += n
        if final:
            break
    out += struct.pack(">I", zlib.adler32(raw) & 0xFFFFFFFF)
    return bytes(out)


def write_png(path, width: int, height: int, pixels: bytes, palette) -> None:
    if len(pixels) != width * height:
        raise ValueError("pixel buffer size does not match width*height")
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)
    plte = b"".join(bytes(c) for c in palette)
    rows = b"".join(b"\x00" + pixels[y * width:(y + 1) * width] for y in range(height))
    idat = zlib_stored(rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(_chunk(b"IHDR", ihdr))
        f.write(_chunk(b"PLTE", plte))
        f.write(_chunk(b"IDAT", idat))
        f.write(_chunk(b"IEND", b""))
