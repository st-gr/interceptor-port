import struct

import pytest

from extract.hunk import relocate
from extract.tables import (CAMERA_RING_SIZE, DecodeError, LOD_SIZE, Mem, camera_ring_bytes,
                            decode_camera_ring, decode_lod_table, decode_record,
                            decode_record_table, RECORD_SIZE)
from tests.synth import BASE, Img, make_hunk_exe


def test_record_fields_are_five_big_endian_longs():
    img = Img()
    img.l(BASE + 40, 0x25A10, 0x41230, 0x41270, 0x41268, 0)
    r = decode_record(img.data, 40, 2)
    assert (r.id, r.handler, r.base, r.detail_list, r.alt_list, r.aux) == (2, 0x25A10, 0x41230, 0x41270, 0x41268, 0)
    t = decode_record_table(img.data, 0, 4)
    assert len(t) == 4 and t[2] == r and t[3].handler == 0


def test_truncated_record_names_table_and_offset():
    with pytest.raises(ValueError, match=r"record table: record 3 at offset 0x3C is truncated"):
        decode_record(bytes(70), 60, 3)


def test_lod_table_must_be_non_decreasing_and_at_most_15():
    ok = bytes([0, 1, 1, 2] + [15] * (LOD_SIZE - 4))
    assert decode_lod_table(ok, 0) == ok
    bad = bytes([0, 2, 1] + [3] * (LOD_SIZE - 3))
    with pytest.raises(DecodeError, match=r"lod table: entry 2 at offset 0x2"):
        decode_lod_table(bad, 0)
    with pytest.raises(DecodeError, match="lod table"):
        decode_lod_table(ok[:100], 0)


def test_camera_ring_is_ten_triples_of_signed_words():
    raw = b"".join(struct.pack(">3h", i, -i, 2 * i) for i in range(10))
    ring = decode_camera_ring(raw, 0)
    assert ring[3] == (3, -3, 6) and len(ring) == 10
    assert camera_ring_bytes(ring) == b"".join(struct.pack("<3h", i, -i, 2 * i) for i in range(10))
    assert len(camera_ring_bytes(ring)) == CAMERA_RING_SIZE
    with pytest.raises(DecodeError, match="camera ring"):
        decode_camera_ring(raw[:50], 0)


def test_mem_error_names_context_and_address():
    m = Mem(bytes(4), BASE, "model 7")
    with pytest.raises(DecodeError, match=r"model 7 at 0x1004: read of 2 bytes outside the image"):
        m.u16(BASE + 4)


def test_relocate_applies_relocations_and_lays_hunks_out_from_base():
    # hunk 0: one long that points at hunk 1 offset 4 (relocation); hunk 1: 8 bytes
    exe = struct.pack(">IIIII", 0x3F3, 0, 2, 0, 1) + struct.pack(">II", 1, 2)
    exe += struct.pack(">II", 0x3E9, 1) + struct.pack(">I", 4)
    exe += struct.pack(">IIII", 0x3EC, 1, 1, 0) + struct.pack(">I", 0) + struct.pack(">I", 0x3F2)
    exe += struct.pack(">II", 0x3EA, 2) + bytes(8) + struct.pack(">I", 0x3F2)
    img, info = relocate(exe, 0x10000)
    assert struct.unpack_from(">I", img, 0)[0] == 0x10004 + 4
    assert [h["addr"] for h in info["hunks"]] == [0x10000, 0x10004]


def test_hunk_executable_helper_loads():
    img, info = relocate(make_hunk_exe(64), 0x10000)
    assert len(img) == 64


def test_relocate_equals_the_project_tool():
    """extract/hunk.py is a copy of tools/hunk/relocate.py; both must give the same image."""
    import importlib.util
    from pathlib import Path
    path = Path(__file__).resolve().parents[2] / "hunk" / "relocate.py"
    spec = importlib.util.spec_from_file_location("hunk_relocate", path)
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    exe = struct.pack(">IIIII", 0x3F3, 0, 2, 0, 1) + struct.pack(">II", 1, 2)
    exe += struct.pack(">II", 0x3E9, 1) + struct.pack(">I", 4)
    exe += struct.pack(">IIII", 0x3EC, 1, 1, 0) + struct.pack(">I", 0) + struct.pack(">I", 0x3F2)
    exe += struct.pack(">II", 0x3EA, 2) + bytes(8) + struct.pack(">I", 0x3F2)
    assert relocate(exe, 0x10000) == tool.relocate(exe, 0x10000)
    assert relocate(exe, 0x20000)[0] == tool.relocate(exe, 0x20000)[0]


def test_range_table_is_64_bytes_verbatim():
    from extract.tables import decode_range_table
    buf = bytes(range(11)) + bytes([127] * 53)
    assert decode_range_table(b"\0\0" + buf, 2) == buf
    with pytest.raises(DecodeError, match="range table"):
        decode_range_table(buf[:63], 0)
