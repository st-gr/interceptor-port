import pytest

from extract.cockpit import (ARCS_ADDR, ARCS_SIZE, MAPSHAPES_ADDR, MAPSHAPES_SIZE, MARKER_LARGE_ADDR, MARKER_SMALL_ADDR, MASKS_ADDR, MASKS_SIZE, WARNING_ADDR,
                             check_arcs, check_mapshapes, mapshapes_hash, check_markers, check_warnings, decode_arcs, decode_mapshapes,
                             decode_markers, decode_masks, decode_warnings, split_mapshapes)
from extract.tables import DecodeError
from tests.synth import Img


def test_warning_table_is_copied_verbatim_with_its_size():
    img = Img(size=0x10000, base=0x41000)
    blob = bytes(range(256)) * 6
    img.bytes_(WARNING_ADDR, blob[:1512])
    assert decode_warnings(img.data, WARNING_ADDR - 0x41000) == blob[:1512]


def test_masks_blob_has_the_documented_340_bytes():
    assert MASKS_SIZE == 340
    img = Img(size=0x10000, base=0x42000)
    img.bytes_(MASKS_ADDR, bytes(range(200)) + bytes(140))
    assert decode_masks(img.data, MASKS_ADDR - 0x42000)[:5] == bytes(range(5))


def test_truncated_tables_name_the_table():
    with pytest.raises(DecodeError, match="warning table"):
        decode_warnings(bytes(100), 0)
    with pytest.raises(DecodeError, match="cockpit masks"):
        decode_masks(bytes(100), 0)
    with pytest.raises(DecodeError, match="target marker"):
        decode_markers(bytes(100), 0, 50)


def test_warning_structure_check():
    good = b"".join(bytes([0x80]) + b"A" * 26 + b"\0" for _ in range(54))
    check_warnings(good)
    bad = bytearray(good)
    bad[28 * 3] = 0x00
    with pytest.raises(DecodeError, match="entry 3"):
        check_warnings(bytes(bad))
    bad = bytearray(good)
    bad[28 * 5 + 4] = 0
    with pytest.raises(DecodeError, match="entry 5"):
        check_warnings(bytes(bad))


def test_marker_extents_are_three_and_ten():
    small = bytes([3, 0] + [0, 0] * 11)
    large = bytes([0xF6, 0] + [0, 0] * 43)   # -10
    check_markers(small + large)
    with pytest.raises(DecodeError, match="small"):
        check_markers(bytes(2 * 12) + large)
    with pytest.raises(DecodeError, match="large"):
        check_markers(small + bytes(88))
    assert MARKER_SMALL_ADDR < MARKER_LARGE_ADDR


def pairs(*p):
    return b"".join(bytes([a & 255, b & 255]) for a, b in p)


def good_arcs():
    lock = [(0, 12)] + [(i, 11 - i // 2) for i in range(1, 19)] + [(14, 0), (-1, -1)]
    rng = [(0, 11)] + [(i, 10 - i // 2) for i in range(1, 18)] + [(13, 0), (-1, -1)]
    return pairs(*lock) + pairs(*rng)


def test_arcs_blob_is_82_bytes_and_structure_checked():
    assert ARCS_SIZE == 82
    img = Img(size=0x10000, base=0x39000)
    blob = good_arcs()
    assert len(blob) == 82
    img.bytes_(ARCS_ADDR, blob)
    assert decode_arcs(img.data, ARCS_ADDR - 0x39000) == blob
    check_arcs(blob)
    with pytest.raises(DecodeError, match="lock circle"):
        check_arcs(pairs((0, 11)) + blob[2:])
    with pytest.raises(DecodeError, match="range dot"):
        check_arcs(blob[:42] + pairs((1, 11)) + blob[44:])
    with pytest.raises(DecodeError, match="range dot"):
        check_arcs(blob[:80] + pairs((0, 0)))          # the terminator pair of the range arc
    with pytest.raises(DecodeError, match="target arcs"):
        decode_arcs(bytes(10), 0)


def good_mapshapes():
    """The invented set of tests/synth.py (324 bytes, structure only)."""
    from tests.synth import mapshapes_blob
    return mapshapes_blob()


def test_map_shapes_are_split_after_the_offset_table_and_checked():
    from tests.synth import SYNTH_MAPSHAPES_FNV1A
    blob = good_mapshapes()
    offsets, shapes = split_mapshapes(blob)
    assert offsets[0] == 34 == offsets[16] and len(shapes) == 17
    assert len(shapes[0]) == 5 and len(shapes[16]) == 3
    assert MAPSHAPES_SIZE == 324 == len(blob)
    # the hash path: the invented set passes with the invented hash, and not with the real constant or a changed byte
    assert mapshapes_hash(blob) == SYNTH_MAPSHAPES_FNV1A
    check_mapshapes(blob, want_hash=SYNTH_MAPSHAPES_FNV1A)
    with pytest.raises(DecodeError, match="hash to 0x"):
        check_mapshapes(blob)                                      # the real set's constant
    changed = bytearray(blob)
    changed[40] ^= 1                                               # a byte inside a shape: the structure holds, the hash does not
    with pytest.raises(DecodeError, match="hash to 0x"):
        check_mapshapes(bytes(changed), want_hash=SYNTH_MAPSHAPES_FNV1A)
    # a blob of another size is refused; the real table fills the 324 bytes exactly
    with pytest.raises(DecodeError, match="expected 324"):
        check_mapshapes(blob[:-3] + b"\x80", want_hash=0)
    img = Img(size=0x10000, base=0x2F000)
    img.bytes_(MAPSHAPES_ADDR, blob)
    assert decode_mapshapes(img.data, MAPSHAPES_ADDR - 0x2F000) == blob
    bad = bytearray(blob)
    bad[2] ^= 1                                        # an offset word that does not point at the shape start
    with pytest.raises(DecodeError, match="offset word"):
        check_mapshapes(bytes(bad), want_hash=0)
    with pytest.raises(DecodeError, match="no terminator"):
        split_mapshapes(bytes(34) + bytes([1, 2, 3]))


def test_map_shape_structure_rules_come_before_the_hash():
    from tests.synth import mapshapes_blob
    blob = bytearray(mapshapes_blob())
    first_len = 34 + 2                                              # the length byte of sector 0's first row
    narrow_bottom = bytearray(blob)
    narrow_bottom[34 + 3 * 4 + 2] = 1                               # the last row of sector 0 is no longer the widest
    with pytest.raises(DecodeError, match="widest at the bottom"):
        check_mapshapes(bytes(narrow_bottom), want_hash=0)
    zero_run = bytearray(blob)
    zero_run[first_len] = 0
    with pytest.raises(DecodeError, match="length 0"):
        check_mapshapes(bytes(zero_run), want_hash=0)


def test_extractor_version_matches_the_core():
    """The core refuses assets of another version (src/core/assets.h), so the two numbers move together."""
    import re
    from pathlib import Path
    from extract.manifest import EXTRACTOR_VERSION
    header = (Path(__file__).resolve().parents[3] / "src" / "core" / "assets.h").read_text()
    m = re.search(r"#define\s+CORE_ASSETS_VERSION\s+(\d+)", header)
    assert m and int(m.group(1)) == EXTRACTOR_VERSION


def test_real_extracted_cockpit_tables_pass_their_checks():
    """Skipped without a local extraction (assets/ is git-ignored): the extracted target arcs and map shapes keep the structure note 06 gives."""
    from pathlib import Path
    root = Path(__file__).resolve().parents[3] / "assets" / "cockpit"
    if not (root / "arcs.bin").exists():
        pytest.skip("assets/cockpit/arcs.bin not extracted")
    check_arcs((root / "arcs.bin").read_bytes())
    check_mapshapes((root / "mapshapes.bin").read_bytes())
    check_warnings((root / "warnings.bin").read_bytes())
    check_markers((root / "markers.bin").read_bytes())
