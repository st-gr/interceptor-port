import struct
import zlib
from extract.png import write_png


def test_writes_indexed_png_that_round_trips(tmp_path):
    path = tmp_path / "t.png"
    pixels = bytes([0, 1, 2, 3, 3, 2, 1, 0])
    palette = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)]
    write_png(path, 4, 2, pixels, palette)
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    # parse chunks
    pos = 8
    chunks = {}
    while pos < len(data):
        (ln,) = struct.unpack(">I", data[pos:pos + 4])
        cid = data[pos + 4:pos + 8]
        chunks[cid] = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
    w, h, depth, ctype = struct.unpack(">IIBB", chunks[b"IHDR"][:10])
    assert (w, h, depth, ctype) == (4, 2, 8, 3)
    assert chunks[b"PLTE"] == bytes([0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255])
    raw = zlib.decompress(chunks[b"IDAT"])
    assert raw == b"\x00" + pixels[:4] + b"\x00" + pixels[4:]


def test_large_image_is_written_as_several_stored_blocks(tmp_path):
    # 300 x 300 pixels: 90,300 bytes of rows, so two stored blocks (65,535 + the rest); the JavaScript port writes the same bytes
    path = tmp_path / "big.png"
    pixels = bytes((x * 7 + y * 3) & 15 for y in range(300) for x in range(300))
    write_png(path, 300, 300, pixels, [(i, i, i) for i in range(16)])
    data = path.read_bytes()
    pos, idat = 8, b""
    while pos < len(data):
        (ln,) = struct.unpack(">I", data[pos:pos + 4])
        if data[pos + 4:pos + 8] == b"IDAT":
            idat = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
    assert idat[:2] == b"\x78\x01" and idat[2] == 0 and idat[3:5] == b"\xff\xff"
    raw = zlib.decompress(idat)
    assert raw == b"".join(b"\x00" + pixels[y * 300:(y + 1) * 300] for y in range(300))
