"""Palette and HUD font decoders (docs/re/02-assets-and-tables.md sections 11 and 12)."""
import struct

from .tables import DecodeError, Mem

FADE_TABLE = 0x29FA4          # 16 steps x 16 colours, 12-bit words 0x0RGB
FADE_STEPS = 16
FADE_COLOURS = 16
FONT_ADDR = 0x426D0           # hunk 61
FONT_HUNK_SIZE = 872
FONT_CHARS = 64               # character codes 0x20 .. 0x5F
GLYPH_ROWS = 5


def decode_fade_table(data: bytes, offset: int, steps: int = FADE_STEPS,
                      colours: int = FADE_COLOURS):
    """Fade table at byte ``offset``: ``steps`` x ``colours`` words, each 0x0RGB with
    4-bit channels. Returns steps lists of (r, g, b), channels scaled to 8 bits by 17."""
    need = steps * colours * 2
    if offset < 0 or offset + need > len(data):
        raise DecodeError(f"fade table at offset 0x{offset:X}: needs {need} bytes, "
                          f"image has {len(data)}")
    out = []
    for s in range(steps):
        step = []
        for c in range(colours):
            at = offset + 2 * (s * colours + c)
            w = struct.unpack_from(">H", data, at)[0]
            if w > 0x0FFF:
                raise DecodeError(f"fade table: step {s} colour {c} at offset 0x{at:X} is "
                                  f"0x{w:04X}, not a 12-bit colour")
            step.append((((w >> 8) & 15) * 17, ((w >> 4) & 15) * 17, (w & 15) * 17))
        out.append(step)
    return out


def palette_bytes(colours) -> bytes:
    return bytes(v for rgb in colours for v in rgb)


def fade_bytes(steps) -> bytes:
    return b"".join(palette_bytes(s) for s in steps)


def decode_font(data: bytes, offset: int, hunk_size: int = FONT_HUNK_SIZE):
    """HUD font. The hunk starts with 64 words, indexed by character code minus 0x20,
    each the byte offset from the hunk start of a 5-byte glyph (one byte per row, the
    three pixels in bits 7 to 5). Returns 64 glyphs of 5 bytes (shared glyphs repeated)."""
    if offset < 0 or offset + hunk_size > len(data):
        raise DecodeError(f"HUD font at offset 0x{offset:X}: hunk of {hunk_size} bytes is truncated")
    glyphs = []
    for i in range(FONT_CHARS):
        off = struct.unpack_from(">H", data, offset + 2 * i)[0]
        if off + GLYPH_ROWS > hunk_size:
            raise DecodeError(f"HUD font: glyph {i} (code 0x{0x20 + i:02X}) at offset "
                              f"0x{offset + 2 * i:X} points to 0x{off:X}, outside the hunk")
        g = bytes(data[offset + off:offset + off + GLYPH_ROWS])
        if any(b & 0x1F for b in g):
            raise DecodeError(f"HUD font: glyph {i} at hunk offset 0x{off:X} has bits outside "
                              f"the three pixel columns")
        glyphs.append(g)
    return glyphs
