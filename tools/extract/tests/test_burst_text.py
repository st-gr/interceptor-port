"""The burst tables, the target text and the month names (extractor version 16; extract/combat.py).

The images are invented (tests/synth.py burst_text_patches on a zero image); nothing here is data of the game. The tests of the real files check sizes only."""
import json
import struct
from pathlib import Path

import pytest

from extract.combat import (BURST_BYTES, CHAIN_BYTES, HEAD_STEP_BYTES, decode_briefing_chain, decode_head_step, PROMPTS_BYTES, STAR_BYTES, TARGET_TEXT_BYTES, decode_burst, decode_months, decode_prompts, decode_star, decode_target_text, write_burst)
from extract.manifest import EXTRACTOR_VERSION
from extract.tables import DecodeError, IMAGE_BASE
from tests.synth import burst_text_patches

REAL = Path(__file__).resolve().parents[3] / "assets"


def image(size=0x40000):
    img = bytearray(size)
    for addr, data in burst_text_patches().items():
        img[addr - IMAGE_BASE:addr - IMAGE_BASE + len(data)] = data
    return bytes(img)


def put(img, addr, data):
    b = bytearray(img)
    b[addr - IMAGE_BASE:addr - IMAGE_BASE + len(data)] = data
    return bytes(b)


def test_the_extractor_version_has_reached_23():
    assert EXTRACTOR_VERSION >= 23   # the burst, text and chain tables came with 23


def test_burst_layout_is_the_documented_409_bytes():
    img = bytearray(image())
    for k in range(72):   # invented shape words, signed
        struct.pack_into(">h", img, 0x315C2 - IMAGE_BASE + 2 * k, (k - 36) * 111)
    for g, a in enumerate((0x31652, 0x31679, 0x316A0)):
        for j in range(36):
            img[a - IMAGE_BASE + j] = (g * 36 + j + 130) & 0xFF
    for j in range(27):
        img[0x316C7 - IMAGE_BASE + j] = (200 + j) & 0xFF
    for i, a in enumerate((0x316E5, 0x316F1, 0x316FD, 0x31709, 0x31715)):   # invented pens: a different pen in every byte of every table
        for j in range(12):
            img[a - IMAGE_BASE + j] = (i + j) % 16
    for i in range(32):                                                      # an invented cycle: byte offsets of shapes, not the shipped order
        struct.pack_into(">H", img, 0x31722 - IMAGE_BASE + 2 * i, 4 * ((3 * i + 1) % 8))
    b = decode_burst(bytes(img))
    assert len(b) == BURST_BYTES == 409
    assert b[:4] == b"BRST" and struct.unpack_from("<H", b, 4) == (2,)
    assert struct.unpack_from("<72h", b, 6) == tuple((k - 36) * 111 for k in range(72))       # words little endian
    assert b[150:150 + 36] == bytes((j + 130) & 0xFF for j in range(36))                       # the offset triples keep the image's bytes
    assert b[150 + 72:150 + 108] == bytes((72 + j + 130) & 0xFF for j in range(36))
    assert b[258:285] == bytes((200 + j) & 0xFF for j in range(27))
    assert b[285:345] == bytes((i + j) % 16 for i in range(5) for j in range(12))                # the pen tables as the image has them
    assert struct.unpack_from("<32H", b, 345) == tuple(4 * ((3 * i + 1) % 8) for i in range(32))   # the cycle, little endian


@pytest.mark.parametrize("what,addr,data,match", [
    ("pointer", 0x315A2 + 4, struct.pack(">I", 0x315C3), r"pointer table at 0x315A2: entry 1"),
    ("pen not a pen", 0x316F1, bytes([3] * 5 + [16] + [3] * 6), r"pen table 1 at 0x316F1"),
    ("cycle odd", 0x31722 + 2 * 9, struct.pack(">H", 6), r"shape cycle at 0x31722"),
    ("cycle past the table", 0x31722 + 2 * 9, struct.pack(">H", 32), r"shape cycle at 0x31722"),
])
def test_burst_checks_what_the_core_relies_on_and_names_the_table(what, addr, data, match):
    with pytest.raises(DecodeError, match=match):
        decode_burst(put(image(), addr, data))


def test_burst_exports_the_pens_and_the_cycle_whatever_they_are_so_the_game_data_decides():
    b = decode_burst(put(put(image(), 0x316FD, bytes([1, 2] * 6)), 0x31722 + 2 * 3, struct.pack(">H", 20)))
    assert b[285 + 24:285 + 36] == bytes([1, 2] * 6)
    assert struct.unpack_from("<H", b, 345 + 6) == (20,)


def test_burst_refuses_a_table_outside_the_image():
    with pytest.raises(DecodeError, match=r"burst table .* at 0x315C2"):
        decode_burst(image()[:0x315C2 - IMAGE_BASE + 10])


def test_target_text_layout_has_the_counts_then_labels_then_names():
    b = decode_target_text(image())
    assert len(b) == TARGET_TEXT_BYTES == 68
    assert struct.unpack_from("<4H", b) == (3, 4, 8, 6)
    assert b[8:20] == b"lab1lab2lab3"                           # no terminators
    assert b[20:26] == b"nm0..." and b[62:68] == b"nm7..."


@pytest.mark.parametrize("addr,data,match", [
    (0x3717C + 3, b"x", r"blank line"),
    (0x37197 + 4, b"y", r"label 0 at 0x37197"),
    (0x371A6 + 7 * 5 + 2, b"\0", r"name 5 at 0x371C9"),
])
def test_target_text_refuses_a_malformed_string_naming_its_address(addr, data, match):
    with pytest.raises(DecodeError, match=match):
        decode_target_text(put(image(), addr, data))


def test_prompts_hold_the_two_six_character_strings_missile_form_first():
    b = decode_prompts(image())
    assert len(b) == PROMPTS_BYTES == 16
    assert struct.unpack_from("<2H", b) == (2, 6)
    assert b[4:10] == b"ab cde" and b[10:16] == b" fghij"             # no terminators


@pytest.mark.parametrize("addr,data,match", [
    (0x36923 + 2, b"\x01", r"fire prompt 0 at 0x36923"),
    (0x36929 + 5, b"\x7f", r"fire prompt 1 at 0x36929 holds"),
    (0x36929 + 6, b"x", r"not followed by a terminator"),
])
def test_prompts_refuse_what_the_core_would_misdraw(addr, data, match):
    with pytest.raises(DecodeError, match=match):
        decode_prompts(put(image(), addr, data))


def test_months_are_unreversed_and_in_calendar_order():
    b = decode_months(image())
    assert len(b) == 36
    assert [b[9 * i:9 * i + 9] for i in range(4)] == [b"mon%d" % i + b"x" * 5 for i in range(4)]


def test_months_refuse_a_non_printable_byte():
    with pytest.raises(DecodeError, match=r"month name 2"):
        decode_months(put(image(), 0x292CC + 18 + 4, b"\x01"))


def test_the_star_is_three_segments_of_two_points_as_little_endian_words():
    b = decode_star(image())
    assert len(b) == STAR_BYTES == 38 and struct.unpack_from("<H", b)[0] == 3
    assert struct.unpack_from("<18h", b, 2) == tuple((-1) ** i * (i % 5 + 1) for i in range(18))


def test_the_star_refuses_an_offset_that_is_not_a_few_units():
    with pytest.raises(DecodeError, match=r"flare star at 0x31542"):
        decode_star(put(image(), 0x31542 + 6, struct.pack(">h", 300)))
    with pytest.raises(DecodeError, match=r"flare star at 0x31542"):
        decode_star(image()[:0x31542 - IMAGE_BASE + 20])


def test_the_briefing_chain_is_thirty_ids_in_mode_order():
    b = decode_briefing_chain(image())
    assert len(b) == CHAIN_BYTES == 30
    assert list(b) == [3 + 7 * r + i for r in range(6) for i in range(5)]


def test_the_briefing_chain_refuses_a_word_that_is_not_a_record_id():
    with pytest.raises(DecodeError, match=r"row 3 entry 1"):
        decode_briefing_chain(put(image(), 0x43EE6 + 32 + 2, struct.pack(">H", 0)))
    with pytest.raises(DecodeError, match=r"row 5 entry 4"):
        decode_briefing_chain(put(image(), 0x43EE6 + 52 + 8, struct.pack(">H", 0x1234)))
    with pytest.raises(DecodeError, match=r"briefing chain at 0x43EE6"):
        decode_briefing_chain(image()[:0x43EE6 - IMAGE_BASE + 20])


def test_the_head_step_is_fourteen_signed_bytes_kept_as_they_are():
    b = decode_head_step(image())
    assert len(b) == HEAD_STEP_BYTES == 14
    assert [x - 256 if x > 127 else x for x in b] == [0, 3, 9, 27, -27, -9, -3, 40, 40, 40, 17, -17, 33, 33]


def test_the_head_step_refuses_a_step_beyond_the_table_range():
    with pytest.raises(DecodeError, match=r"head step table at 0x1E800"):
        decode_head_step(put(image(), 0x1E800 + 5, bytes([100])))
    with pytest.raises(DecodeError, match=r"head step table at 0x1E800"):
        decode_head_step(image()[:0x1E800 - IMAGE_BASE + 5])


def test_the_files_are_written_under_tables(tmp_path):
    write_burst(image(), tmp_path)
    t = tmp_path / "tables"
    assert (t / "burst.bin").stat().st_size == 409
    assert (t / "target_text.bin").stat().st_size == 68
    assert (t / "months.bin").stat().st_size == 36
    assert (t / "prompts.bin").stat().st_size == 16
    assert (t / "star.bin").stat().st_size == 38
    assert (t / "briefing_chain.bin").stat().st_size == 30
    assert (t / "head_step.bin").stat().st_size == 14


def test_real_tables_have_the_documented_sizes_only():
    m = REAL / "manifest.json"
    if not m.exists() or json.loads(m.read_text())["extractor_version"] < 23:
        pytest.skip("no extracted assets of version 23")
    t = REAL / "tables"
    assert (t / "burst.bin").read_bytes()[:4] == b"BRST" and (t / "burst.bin").stat().st_size == 409
    assert (t / "target_text.bin").stat().st_size == 68
    assert (t / "months.bin").stat().st_size == 36
    assert (t / "prompts.bin").stat().st_size == 16
    assert (t / "star.bin").stat().st_size == 38
    chain = (t / "briefing_chain.bin").read_bytes()   # structure only: 6 rows of 5 record ids, no values pinned
    assert len(chain) == 30 and all(1 <= x <= 110 for x in chain)
    assert (t / "head_step.bin").stat().st_size == 14
    files = json.loads(m.read_text())["files"]
    assert {"tables/burst.bin", "tables/target_text.bin", "tables/months.bin", "tables/prompts.bin", "tables/star.bin", "tables/briefing_chain.bin", "tables/head_step.bin"} <= set(files)
