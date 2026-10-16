"""Sizes (and a few documented samples) of the flight tables, docs/re/05-flight-model.md."""
import struct
from pathlib import Path

import pytest

from extract.flight import (SHIP_SIZES, SPAN_SIZES, SPANS, WORD_TABLES, decode_all, decode_pads, decode_span, parse_segment_table,
                            reference_longs)
from extract.hunk import relocate
from extract.tables import DecodeError, IMAGE_BASE, Mem
from tests.synth import flight_patches, make_hunk_exe

REAL = Path(__file__).resolve().parents[3] / "extracted" / "interceptor.bin"
EXPECTED_WORDS = {"flight_stick_player": 62, "flight_stick_ai": 64, "flight_gust": 31, "flight_sink": 31,
                  "flight_len": 257,
                  "flight_terrain": 32 * 32 + 3724 // 2, "flight_dir": 32}


def synthetic_image():
    img, _ = relocate(make_hunk_exe(0x3C000, flight_patches()), IMAGE_BASE)
    return img


def words(blob):
    return struct.unpack("<%dh" % (len(blob) // 2), blob)


def test_every_table_has_the_documented_size():
    out = decode_all(synthetic_image(), IMAGE_BASE)
    for name, n in EXPECTED_WORDS.items():
        assert WORD_TABLES[name][1] == n
        assert len(out[name + ".bin"]) == 2 * n, name
    assert len(out["flight_carrier.bin"]) == 124 + 44 + 684 == SHIP_SIZES["flight_carrier"]
    assert len(out["flight_ship2.bin"]) == 32 + 44 + 362 == SHIP_SIZES["flight_ship2"]
    assert len(out["flight_pads.bin"]) % 2 == 0 and len(words(out["flight_pads.bin"])) == 40 + 4


def test_junk_spans_have_the_sizes_of_the_note():
    """Note 05 17.9 (and correction C20): 0x3249A..0x325B8 is 286 bytes, 0x3AE92..0x49A12 is 60,288 bytes (17.18 a)."""
    out = decode_all(synthetic_image(), IMAGE_BASE)
    assert SPANS == {"flight_span_pitch": (0x3249A, 0x325B8), "flight_span_trig": (0x3AE92, 0x49A12)}
    assert SPAN_SIZES == {"flight_span_pitch": 286, "flight_span_trig": 60288}
    for name, size in SPAN_SIZES.items():
        assert len(out[name + ".bin"]) == size == SPANS[name][1] - SPANS[name][0]


def test_span_bytes_are_the_image_bytes_in_order():
    img = bytearray(synthetic_image())
    for addr in (0x3249A, 0x325B7, 0x3AE92, 0x436E8, 0x49A11):
        img[addr - IMAGE_BASE] = addr & 0xFF
    out = decode_all(bytes(img), IMAGE_BASE)
    a, b = out["flight_span_pitch.bin"], out["flight_span_trig.bin"]
    assert (a[0], a[-1]) == (0x9A, 0xB7) and (b[0], b[0x436E8 - 0x3AE92], b[-1]) == (0x92, 0xE8, 0x11)


def test_pad_table_without_terminator_is_rejected():
    img = bytearray(synthetic_image())
    img[0x47B72 - IMAGE_BASE + 4 * 16 + 2] = 0
    img[0x47B72 - IMAGE_BASE + 4 * 16 + 3] = 0
    with pytest.raises(DecodeError, match="terminator"):
        decode_pads(Mem(bytes(img), IMAGE_BASE))


def test_table_outside_the_image_is_rejected():
    with pytest.raises(DecodeError, match="outside the image"):
        decode_all(bytes(0x1000), IMAGE_BASE)


@pytest.mark.skipif(not REAL.exists(), reason="extracted/interceptor.bin absent")
def test_real_image_samples_from_the_note():
    out = decode_all(REAL.read_bytes(), IMAGE_BASE)
    for name, n in EXPECTED_WORDS.items():
        assert len(out[name + ".bin"]) == 2 * n
    p = words(out["flight_stick_player.bin"])
    assert (p[1], p[10], p[20], p[60]) == (27, 225, 468, 1611)
    a = words(out["flight_stick_ai.bin"])
    assert (a[1], a[10], a[60]) == (117, 360, 1611)
    g = words(out["flight_gust.bin"])
    s = words(out["flight_sink.bin"])
    assert (g[0], g[8], g[30]) == (384, 144, 6) and (s[0], s[10], s[30]) == (512, 144, 1)
    # the pitch scale (0x3259A) and the sine table (0x436E8) sit inside the spans; the word before the scale is 0x4E75 (17.9)
    sp, st = out["flight_span_pitch.bin"], out["flight_span_trig.bin"]
    sw = struct.unpack(">%dh" % (len(sp) // 2), sp)
    base = (0x3259A - 0x3249A) // 2
    assert sw[base:base + 3] == (256, 240, 220) and sw[base + 13:base + 15] == (0, 0) and sw[base - 1] == 0x4E75 and sw[base - 2] == 84
    tw = struct.unpack(">%dh" % (len(st) // 2), st)
    sine = (0x436E8 - 0x3AE92) // 2
    assert (tw[sine], tw[sine + 900]) == (0, 16384)
    ln = words(out["flight_len.bin"])
    assert (ln[0], ln[128], ln[256]) == (16384, 18318, 23170)
    pads = words(out["flight_pads.bin"])
    assert len(pads) > 5 * 8      # five 8-word entries (the four pads and the terminator), then the quad lists: the values are the publisher's data


SEGMENTS = Path(__file__).resolve().parents[1] / "extract" / "segments.txt"      # the table shipped with the package
EXE = Path(__file__).resolve().parents[3] / "extracted" / "disk" / "F-18 Interceptor"


def test_the_shipped_segment_tables_of_both_extractors_are_the_same_text():
    js = Path(__file__).resolve().parents[2] / "extract-js" / "segments.txt"
    assert SEGMENTS.read_bytes() == js.read_bytes()
    segs = parse_segment_table(SEGMENTS.read_text())
    assert sorted(segs) == list(range(185)) and all(flat >= 0x10000 for flat, _ in segs.values())


def test_segment_table_lines_are_parsed():
    segs = parse_segment_table("seg 0 flat 010000 ram C07B28 size 41804 segsize 41812 ok\nimg_10000 F:10000 00\nseg 71 flat 04A6E8 ram C42230 size 1 segsize 2 ok\n")
    assert segs == {0: (0x10000, 0xC07B28), 71: (0x4A6E8, 0xC42230)}


def test_a_relocated_long_inside_a_span_is_replaced_and_one_outside_is_not():
    img = bytearray(synthetic_image())
    mem = Mem(bytes(img), IMAGE_BASE)
    longs = [(0x41D76, 0x00C3F5D4), (0x32540, 0x00C2A9E0), (0x20000, 0x12345678), (0x49A10, 0xDEADBEEF)]   # the last two are not wholly inside a span
    a = decode_span(mem, "flight_span_trig", longs)
    b = decode_span(mem, "flight_span_pitch", longs)
    assert a[0x41D76 - 0x3AE92:0x41D7A - 0x3AE92] == bytes.fromhex("00C3F5D4")
    assert b[0x32540 - 0x3249A:0x32544 - 0x3249A] == bytes.fromhex("00C2A9E0")
    assert a[0x49A0E - 0x3AE92:] == bytes(4)   # the long straddling the end of the span is left alone
    assert len(a) == SPAN_SIZES["flight_span_trig"] and len(b) == SPAN_SIZES["flight_span_pitch"]


@pytest.mark.skipif(not (EXE.exists() and SEGMENTS.exists() and REAL.exists()), reason="extracted executable, image or segment table absent")
def test_the_spans_carry_340_reference_machine_longs_as_the_note_counts():
    exe = EXE.read_bytes()
    longs = reference_longs(exe, IMAGE_BASE, parse_segment_table(SEGMENTS.read_text()))
    lo = [a for a, _ in longs if a < 0x40000]
    hi = [a for a, _ in longs if a >= 0x40000]
    assert sorted(a for a in lo if a < 0x3AE92) == [0x32540, 0x32560, 0x32568]
    # 17.18 a: the span now starts at 0x3AE92 (145 longs between 0x3AE92 and 0x4130A), 337 in all
    assert len(lo) + len(hi) == 340 and len([a for a in lo + hi if a >= 0x3AE92]) == 337
    assert sum(1 for a in hi if 0x415B8 <= a <= 0x41E1B) == 12 and sum(1 for a in hi if 0x4767C <= a <= 0x499D5) == 180
    assert sum(1 for a in lo + hi if 0x3AE92 <= a < 0x4130A) == 145
    out = decode_all(REAL.read_bytes(), IMAGE_BASE, longs)
    st = out["flight_span_trig.bin"]
    flat = decode_all(REAL.read_bytes(), IMAGE_BASE)["flight_span_trig.bin"]
    # a reference value is a RAM address in the 0x00C0.... range, the flat one is below 0x100000 (note 05 17.9: the high word is 0xC3 or 0xC4 for the hunks involved)
    a0, v0 = [(a, v) for a, v in longs if a == 0x41D76][0]
    assert st[a0 - 0x3AE92:a0 - 0x3AE92 + 4] == v0.to_bytes(4, "big") and (v0 >> 16) in (0xC3, 0xC4, 0xC2, 0xC1, 0xC0) and flat[a0 - 0x3AE92:a0 - 0x3AE92 + 4] != st[a0 - 0x3AE92:a0 - 0x3AE92 + 4]
    # every other byte of the span is the image byte
    diff = sum(1 for i in range(len(st)) if st[i] != flat[i])
    assert 0 < diff <= 4 * 337


@pytest.mark.skipif(not (EXE.exists() and SEGMENTS.exists()), reason="extracted executable or segment table absent")
def test_the_reference_value_uses_the_relocation_entrys_target_hunk():
    """17.9, 17.18 a: value = file long + RAM base of the entry's TARGET hunk, not of the hunk the flat address of the long lies in."""
    from extract.hunk import parse as parse_hunks
    exe = EXE.read_bytes()
    segs = parse_segment_table(SEGMENTS.read_text())
    sizes, _t, contents, relocs, _s = parse_hunks(exe)
    flat, a = [], IMAGE_BASE
    for sz in sizes:
        flat.append(a)
        a += sz
    got = dict(reference_longs(exe, IMAGE_BASE, segs))
    cross = 0
    for i, rl in enumerate(relocs):
        for off, tgt in rl:
            addr = flat[i] + off
            if addr in got:
                assert got[addr] == (struct.unpack_from(">I", contents[i], off)[0] + segs[tgt][1]) & 0xFFFFFFFF
                cross += tgt != i
    assert cross > 0   # at least one long points into another hunk than the one it lies in
