# tools/hunk/tests/test_relocate.py
import json
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from relocate import relocate  # noqa: E402

HUNK_HEADER, HUNK_CODE, HUNK_DATA, HUNK_BSS, HUNK_RELOC32, HUNK_END, HUNK_SYMBOL = (
    0x3F3, 0x3E9, 0x3EA, 0x3EB, 0x3EC, 0x3F2, 0x3F0)


def L(*words):
    return struct.pack(">%dI" % len(words), *words)


def make_hunk_file():
    """Three hunks laid out CODE (8 bytes), DATA (4 bytes), BSS (16 bytes).
    CODE has one 32-bit reloc at offset 4 pointing into DATA (hunk 1).
    DATA has one 32-bit reloc at offset 0 pointing into BSS (hunk 2), which has no file data."""
    code = bytes.fromhex("4E714E71" "00000000")          # nop nop ; long to patch
    data = bytes.fromhex("00000010")                      # long to patch: 0x10 + BSS base
    header = L(HUNK_HEADER, 0, 3, 0, 2, len(code) // 4, len(data) // 4, 16 // 4)
    code_hunk = L(HUNK_CODE, len(code) // 4) + code
    code_reloc = L(HUNK_RELOC32, 1, 1, 4, 0)              # 1 offset for hunk 1: offset 4; terminator
    data_hunk = L(HUNK_DATA, len(data) // 4) + data
    data_reloc = L(HUNK_RELOC32, 1, 2, 0, 0)              # 1 offset for hunk 2: offset 0; terminator
    bss_hunk = L(HUNK_BSS, 16 // 4)
    # Each entry: name length in longs, name padded to longs, value; a 0 length ends the table.
    # "_fn" is listed before "_first" (offset 0) so the test exercises sorting.
    code_sym = L(HUNK_SYMBOL, 1) + b"_fn\0" + L(4, 2) + b"_first\0\0" + L(0, 0)
    data_sym = L(HUNK_SYMBOL, 2) + b"_data\0\0\0" + L(0, 0)
    return (header + code_hunk + code_reloc + code_sym + L(HUNK_END)
            + data_hunk + data_reloc + data_sym + L(HUNK_END)
            + bss_hunk + L(HUNK_END))


def test_relocates_sequential_layout_including_bss_targets():
    image, info = relocate(make_hunk_file(), base=0x1000)
    assert info["base"] == 0x1000
    assert [(h["type"], h["addr"], h["size"]) for h in info["hunks"]] == [
        ("CODE", 0x1000, 8), ("DATA", 0x1008, 4), ("BSS", 0x100C, 16)]
    assert len(image) == 8 + 4 + 16
    assert image[:4] == bytes.fromhex("4E714E71")
    assert struct.unpack(">I", image[4:8])[0] == 0x1008            # CODE+4 patched with DATA base
    assert struct.unpack(">I", image[8:12])[0] == 0x10 + 0x100C    # DATA+0 patched with BSS base
    assert image[12:] == bytes(16)                                 # BSS zero-filled


def test_cli_writes_bin_and_map(tmp_path):
    src = tmp_path / "t.hunk"
    src.write_bytes(make_hunk_file())
    out_bin, out_map = tmp_path / "t.bin", tmp_path / "t.map.json"
    script = Path(__file__).resolve().parents[1] / "relocate.py"
    r = subprocess.run([sys.executable, str(script), str(src), str(out_bin), str(out_map), "--base", "0x1000"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert out_bin.stat().st_size == 28
    m = json.loads(out_map.read_text())
    assert m["hunks"][2]["type"] == "BSS"


def test_rejects_non_hunk_data():
    import pytest
    with pytest.raises(ValueError):
        relocate(b"this is not a hunk file at all", base=0x1000)


def test_symbols_are_absolute_and_sorted():
    _, info = relocate(make_hunk_file(), base=0x1000)
    assert info["symbols"] == [
        {"name": "_first", "addr": 0x1000, "hunk": 0},
        {"name": "_fn", "addr": 0x1004, "hunk": 0},
        {"name": "_data", "addr": 0x1008, "hunk": 1}]


def make_two_run_file():
    """CODE hunk of 12 bytes; one RELOC32 block lists target hunk 1 twice (offsets 4, then 8)."""
    code = bytes(4) + L(0x10) + L(0x20)
    data = bytes(4)
    header = L(HUNK_HEADER, 0, 2, 0, 1, 3, 1)
    reloc = L(HUNK_RELOC32, 1, 1, 4, 1, 1, 8, 0)
    return (header + L(HUNK_CODE, 3) + code + reloc + L(HUNK_END)
            + L(HUNK_DATA, 1) + data + L(HUNK_END))


def test_same_target_listed_twice_in_one_block_patches_both():
    image, info = relocate(make_two_run_file(), base=0x1000)
    assert struct.unpack(">I", image[4:8])[0] == 0x10 + 0x100C
    assert struct.unpack(">I", image[8:12])[0] == 0x20 + 0x100C
    assert info["reloc_entries"] == info["reloc_count"] == 2


def test_reloc_counts_match_on_synthetic_file():
    _, info = relocate(make_hunk_file(), base=0x1000)
    assert info["reloc_entries"] == info["reloc_count"] == 2


def make_short_file(block_type=0x3FC, short_body=None):
    """CODE hunk of 12 bytes with a 16-bit relocation block targeting hunk 1."""
    code = bytes(4) + L(0x10) + L(0x20)
    # count 2, target hunk 1, offsets 4 and 8, terminator 0: 6 words = 12 bytes
    # (odd count of words after the first two is what pads: here 5 halfwords + pad)
    body = short_body if short_body is not None else struct.pack(">HHHHH", 2, 1, 4, 8, 0) + bytes(2)
    return (L(HUNK_HEADER, 0, 2, 0, 1, 3, 1) + L(HUNK_CODE, 3) + code
            + L(block_type) + body + L(HUNK_END)
            + L(HUNK_DATA, 1) + bytes(4) + L(HUNK_END))


def test_reloc32short_with_longword_padding():
    image, info = relocate(make_short_file(), base=0x1000)
    assert struct.unpack(">I", image[4:8])[0] == 0x10 + 0x100C
    assert struct.unpack(">I", image[8:12])[0] == 0x20 + 0x100C
    assert info["reloc_entries"] == info["reloc_count"] == 2


def test_drel32_is_treated_as_absolute():
    image, info = relocate(make_short_file(block_type=0x3F7), base=0x1000)
    assert struct.unpack(">I", image[4:8])[0] == 0x10 + 0x100C
    assert info["reloc_count"] == 2


def test_relreloc32_is_rejected():
    import pytest
    with pytest.raises(ValueError, match="RELRELOC32"):
        relocate(make_short_file(block_type=0x3FD), base=0x1000)


def test_truncated_files_raise_value_error():
    import pytest
    full32 = make_two_run_file()
    cut = full32.index(L(HUNK_RELOC32)) + 12      # inside the RELOC32 block
    with pytest.raises(ValueError):
        relocate(full32[:cut], base=0x1000)
    fullshort = make_short_file()
    cut = fullshort.index(L(0x3FC)) + 4 + 4       # inside the RELOC32SHORT block
    with pytest.raises(ValueError):
        relocate(fullshort[:cut], base=0x1000)
    with pytest.raises(ValueError):               # NAME block skipped past the end
        relocate(L(HUNK_HEADER, 0, 1, 0, 0, 1) + L(HUNK_CODE, 1) + bytes(4) + L(0x3E8, 50), base=0x1000)


def test_memory_flag_extension_is_rejected():
    import pytest
    bad = L(HUNK_HEADER, 0, 1, 0, 0, 0xC0000001) + L(HUNK_CODE, 1) + bytes(4) + L(HUNK_END)
    with pytest.raises(ValueError, match="memory-flag"):
        relocate(bad, base=0x1000)
