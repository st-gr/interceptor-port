import struct
from pathlib import Path

import pytest

from extract.flow import (decode_menu_font, decode_demo, decode_scenario, decode_text, decode_training, write_demo, write_training, palette_words, write_menu_font, write_palettes,
                          write_scenario, write_text)
from extract.tables import DecodeError, IMAGE_BASE
from tests.synth import SCENARIO_REGIONS, SCENARIO_START, scenario_patches

REAL = Path(__file__).resolve().parents[3] / "assets"


def image(patches=None, size=0x40000):
    img = bytearray(size)
    for addr, data in (patches or {}).items():
        img[addr - IMAGE_BASE:addr - IMAGE_BASE + len(data)] = data
    return bytes(img)


def sections(blob):
    """Split scenario.bin into [(count, record_size, body)]."""
    out, at = [], 0
    while at < len(blob):
        count, size = struct.unpack_from("<HH", blob, at)
        body = blob[at + 4:at + 4 + count * size]
        assert len(body) == count * size
        out.append((count, size, body))
        at += 4 + count * size
    return out


def le(body, fmt):
    n = struct.calcsize("<" + fmt)
    return [struct.unpack_from("<" + fmt, body, i) for i in range(0, len(body), n)]


def test_palettes_scale_four_bit_words_by_17(tmp_path):
    p = scenario_patches()
    p[0x29F24] = b"".join(struct.pack(">H", w) for w in [0x0000, 0x000F, 0x00F0, 0x0F00, 0x0123] + [0x0FFF] * 27)
    p[0x29F64] = struct.pack(">H", 0x0FB2) * 32
    write_palettes(image(p), tmp_path)
    menu = (tmp_path / "palette" / "menu.pal").read_bytes()
    flash = (tmp_path / "palette" / "flash.pal").read_bytes()
    assert len(menu) == len(flash) == 96
    assert menu[:15] == bytes([0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 0, 17, 34, 51])
    assert flash == bytes([255, 187, 34]) * 32


def test_palette_rejects_a_word_above_12_bits_naming_entry_and_address():
    p = {0x29F24 + 2 * 5: b"\x12\x34"}
    with pytest.raises(DecodeError, match=r"palette at 0x29F24: entry 5 is 0x1234"):
        palette_words(image(p), 0x29F24)


def test_palette_outside_the_image():
    with pytest.raises(DecodeError, match="palette"):
        palette_words(bytes(100), 0x29F24)


def test_scenario_sub_tables(tmp_path):
    write_scenario(image(scenario_patches()), tmp_path)
    s = sections((tmp_path / "tables" / "scenario.bin").read_bytes())
    assert [(c, r) for c, r, _ in s] == [(2, 16), (4, 14), (4, 10), (10, 12), (2, 8), (4, 4), (4, 10), (0, 0)]
    # start blocks: little-endian signed words, the -1 terminator not stored
    assert le(s[0][2], "8h") == SCENARIO_START
    # regions: header word, box, count word, first entry, entry count
    regs = le(s[1][2], "7H")
    assert [r[:5] for r in regs] == [(*box, cnt) for box, cnt, _ in SCENARIO_REGIONS]
    assert [r[5:] for r in regs] == [(0, 1), (1, 0), (1, 2), (3, 1)]
    ents = le(s[2][2], "5H")
    assert ents == [e for _, _, es in SCENARIO_REGIONS for e in es]
    # placement rows: list index and count word per variant; rows 0-8 list 0, row 9 list 1
    rows = le(s[3][2], "6H")
    assert rows[0] == (0, 2, 0, 2, 0, 2) and rows[8] == (0, 2, 0, 2, 0, 2) and rows[9] == (1, 6, 1, 6, 1, 6)
    # lists: sub-list indices, shared sub-lists deduplicated
    assert le(s[4][2], "4H") == [(0, 1, 1, 2), (3, 3, 3, 3)]
    assert le(s[5][2], "2H") == [(0, 1), (1, 0), (1, 2), (3, 1)]
    assert [e[3] for e in le(s[6][2], "5H")] == [50, 51, 52, 53]


def test_scenario_missing_start_terminator_names_the_table():
    p = scenario_patches()
    p[0x47ADE + 32] = struct.pack(">8h", 1, 0, 0, 0, 0, 0, 0, 0) * 20
    with pytest.raises(DecodeError, match=r"start blocks at 0x47ADE: no terminator"):
        decode_scenario(image(p))


def test_scenario_unterminated_region_is_refused():
    img = bytearray(image(scenario_patches()))
    img[0x2DE3A - IMAGE_BASE + 10:0x2DE3A - IMAGE_BASE + 3000] = bytes(2990)
    with pytest.raises(DecodeError, match="region 0"):
        decode_scenario(bytes(img))


def test_real_assets_structure_only():
    f = REAL / "tables" / "scenario.bin"
    if not f.is_file():
        pytest.skip("assets/tables/scenario.bin not extracted")
    s = sections(f.read_bytes())
    assert [r for _, r, _ in s] == [16, 14, 10, 12, 8, 4, 10, 0]
    counts = [c for c, _, _ in s]
    assert counts[0] >= 4 and counts[1] == 4 and counts[3] == 10
    assert sum(r[6] for r in le(s[1][2], "7H")) == counts[2]
    assert sum(x[1] for x in le(s[5][2], "2H")) == counts[6]
    for pal in ("menu", "flash"):
        assert (REAL / "palette" / f"{pal}.pal").stat().st_size == 96


def test_region_pointer_outside_the_image_names_the_region():
    p = scenario_patches()
    p[0x2DE28 + 8] = struct.pack(">I", 0x7FFFFF)
    with pytest.raises(DecodeError, match=r"region 2: pointer 0x7FFFFF"):
        decode_scenario(image(p))


def test_scenario_above_the_core_caps_is_refused():
    p = scenario_patches()
    p[0x47ADE + 32] = struct.pack(">8h", 1, 0, 0, 0, 0, 0, 0, 0) * 8 + struct.pack(">8h", -1, 0, 0, 0, 0, 0, 0, 0)
    with pytest.raises(DecodeError, match=r"start blocks at 0x47ADE: no terminator within 8 blocks"):
        decode_scenario(image(p))


# ---- text records and the menu font ----
ID_TABLE, RECORDS, FONT = 0x43DFE, 0x43F24, 0x4283C


def rec(row, col, lines):
    """A record: header, lines (flags, colour, text, delay), the end byte."""
    out = bytes([row, col])
    for flags, colour, text, delay in lines:
        out += bytes([flags, colour]) + text + bytes([0, delay])
    return out + b"\xff"


def text_patches(records, id_map):
    """records: list of record bytes laid end to end from RECORDS; id_map: {id: record index or raw offset-from-table}."""
    p, at, addr = {}, 0, []
    blob = b"".join(records)
    for r in records:
        addr.append(RECORDS - ID_TABLE + at)
        at += len(r)
    p[RECORDS] = blob
    table = [0] * 110
    for i, v in id_map.items():
        table[i - 1] = addr[v] if isinstance(v, int) and v < 100 else v
    p[ID_TABLE] = b"".join(struct.pack(">H", w) for w in table)
    return p


SYN_RECORDS = [
    rec(6, 2, []),
    rec(10, 4, [(0, 0x23, b"AB CD", 0), (3, 0x56, b"", 20), (1, 0x26, b"X", 120)]),
    rec(199, 34, [(3, 0x95, b" " * 7, 4)]),
]
SYN_IDS = {i: 0 for i in range(1, 111)}
SYN_IDS.update({2: 1, 7: 1, 50: 2, 3: 1})
for _i in (18, 19, 21, 25):
    SYN_IDS[_i] = 0


def text_image(records=None, ids=None):
    return image(text_patches(records or SYN_RECORDS, ids or SYN_IDS))


def test_text_round_trip_synthetic(tmp_path):
    write_text(text_image(), tmp_path)
    t = (tmp_path / "tables" / "text.bin").read_bytes()
    ids = (tmp_path / "tables" / "text_ids.bin").read_bytes()
    (n,) = struct.unpack_from("<H", t)
    assert n == 4
    at, lines = 2, []
    for _ in range(n):
        fid, k, row, col, flags, colour, delay, ln = struct.unpack_from("<HBBBBBBH", t, at)
        lines.append((fid, k, row, col, flags, colour, delay, t[at + 10:at + 10 + ln]))
        at += 10 + ln
    assert at == len(t)
    assert lines == [(2, 0, 10, 4, 0, 0x23, 0, b"AB CD"), (2, 1, 10, 4, 3, 0x56, 20, b""),
                     (2, 2, 10, 4, 1, 0x26, 120, b"X"), (50, 0, 199, 34, 3, 0x95, 4, b" " * 7)]
    (cnt,) = struct.unpack_from("<H", ids)
    assert cnt == 110 and len(ids) == 2 + 6 * 110
    ent = {i: struct.unpack_from("<HHH", ids, 2 + 6 * (i - 1)) for i in range(1, 111)}
    assert [ent[i] for i in (1, 2, 3, 7, 50)] == [(1, 0, 0), (2, 0, 3), (3, 0, 3), (7, 0, 3), (50, 3, 1)]
    for i in (18, 19, 21, 25):
        assert ent[i] == (i, 0, 0)


def test_text_refusals_name_the_record():
    def bad(lines, match, row=10, col=4, ids=None):
        recs = [rec(6, 2, []), rec(row, col, lines)]
        ids = ids or {i: 0 for i in range(1, 111)} | {2: 1}
        with pytest.raises(DecodeError, match=match):
            decode_text(text_image(recs, ids))
    bad([(0, 0x23, b"A\x7fB", 0)], r"id 2 at 0x[0-9A-F]+: line 0 has text byte 0x7F")
    bad([(0, 0x23, b"A\x10", 0)], r"id 2 .*text byte 0x10")
    bad([(2, 0x23, b"A", 0)], r"id 2 .*flags 0x02")
    bad([(1, 0x21, b"A", 0)], r"id 2 .*typed line 0 has delay 1")
    bad([(3, 0x30, b"A", 0)], r"id 2 .*delay 0")
    bad([(0, 0x23, b"A", 121)], r"id 2 .*delay 121")
    bad([(0, 0x23, b"A", 0)], r"id 2 .*row 200", row=200)
    bad([(0, 0x23, b"A", 0)], r"id 2 .*column 5 is odd", col=5)
    bad([(0, 0x23, b"A" * 46, 0)], r"id 2 .*longer than 45")
    bad([(0, 0x23, b"A" * 45, 0)], r"id 2 .*wider than 320", col=10)
    bad([(0, 0x23, b"A", 0)], r"id 2 .*outside the record span", ids={i: 0 for i in range(1, 111)} | {2: 0x7000})


def test_text_unterminated_record_runs_off_the_span():
    p = text_patches([rec(6, 2, [])], {i: 0 for i in range(1, 111)})
    p[0x46150] = bytes([10, 2, 0, 0x23] + [0x41] * 20)
    p[ID_TABLE + 2 * 4] = struct.pack(">H", 0x46150 - ID_TABLE)
    with pytest.raises(DecodeError, match=r"id 5 at 0x46150: runs outside the record span"):
        decode_text(image(p))


def font_patches(offs=None, glyphs=None):
    offs = offs or [0x80 + 7 * (i % 8) for i in range(64)]
    glyphs = glyphs or [b"\x00" * 7] + [bytes((i * 8 + k * 16) & 0xF8 for k in range(7)) for i in range(1, 8)]
    return {FONT: b"".join(struct.pack(">H", o) for o in offs), FONT + 0x80: b"".join(glyphs)}


def test_menu_font_follows_the_offset_table(tmp_path):
    write_menu_font(image(font_patches()), tmp_path)
    f = (tmp_path / "fonts" / "menu5x7.bin").read_bytes()
    assert len(f) == 448
    g = font_patches()[FONT + 0x80]
    for i in range(64):
        assert f[7 * i:7 * i + 7] == g[7 * (i % 8):7 * (i % 8) + 7]


def test_menu_font_refusals_name_the_character():
    offs = [0x80 + 7 * (i % 8) for i in range(64)]
    offs[0x41 - 0x20] = 0x1F4
    with pytest.raises(DecodeError, match=r"character 0x41 has offset 0x01F4"):
        decode_menu_font(image(font_patches(offs)))
    offs[0x41 - 0x20] = 0x7F
    with pytest.raises(DecodeError, match=r"character 0x41 has offset 0x007F"):
        decode_menu_font(image(font_patches(offs)))
    g = [b"\x00" * 7] + [b"\x08" * 7] * 7
    g[2] = b"\x00\x00\x04\x00\x00\x00\x00"
    with pytest.raises(DecodeError, match=r"glyph of character 0x2[0-9A-F] has bits set in the low three"):
        decode_menu_font(image(font_patches(glyphs=g)))
    with pytest.raises(DecodeError, match=r"not blank"):
        decode_menu_font(image(font_patches(glyphs=[b"\x80" * 7] * 8)))
    offs = [0x81] + [0x80] * 63
    with pytest.raises(DecodeError, match=r"first offset is 0x0081"):
        decode_menu_font(image(font_patches(offs)))


def test_real_text_structure_only():
    t, ids = REAL / "tables" / "text.bin", REAL / "tables" / "text_ids.bin"
    if not (t.is_file() and ids.is_file()):
        pytest.skip("assets/tables/text.bin not extracted")
    b = t.read_bytes()
    (n,) = struct.unpack_from("<H", b)
    assert n == 337
    at, with_text, chars = 2, 0, 0
    for _ in range(n):
        fid, k, row, col, flags, colour, delay, ln = struct.unpack_from("<HBBBBBBH", b, at)
        text = b[at + 10:at + 10 + ln]
        assert len(text) == ln <= 45 and 1 <= fid <= 110
        assert row < 200 and col % 2 == 0 and col <= 34 and flags in (0, 1, 3) and delay <= 120
        assert all(0x20 <= c <= 0x5F for c in text)
        assert 8 * col + 7 * max(ln - 1, 0) + 5 <= 320
        with_text += ln > 0
        chars += ln
        at += 10 + ln
    assert at == len(b) and with_text == 241 and chars == 7137
    d = ids.read_bytes()
    assert struct.unpack_from("<H", d)[0] == 110 and len(d) == 2 + 6 * 110
    firsts = set()
    for i in range(110):
        rid, first, cnt = struct.unpack_from("<HHH", d, 2 + 6 * i)
        assert rid == i + 1 and first + cnt <= n
        if cnt:
            firsts.add(first)
        if rid in (18, 19, 21, 25):
            assert cnt == 0
    assert len(firsts) == 91 - 3


def test_real_menu_font_structure_only():
    f = REAL / "fonts" / "menu5x7.bin"
    if not f.is_file():
        pytest.skip("assets/fonts/menu5x7.bin not extracted")
    b = f.read_bytes()
    assert len(b) == 448 and not any(x & 7 for x in b) and b[:7] == bytes(7)
    assert len({b[7 * i:7 * i + 7] for i in range(64)}) <= 54


def test_text_above_the_core_caps_is_refused():
    one = rec(10, 0, [(0, 0x23, b"", 0)] * 255)
    ids = {i: 0 for i in range(1, 111)}
    ids.update({2: 1, 3: 2})
    with pytest.raises(DecodeError, match=r"text: 510 lines"):
        decode_text(text_image([rec(6, 2, []), one, rec(10, 0, [(0, 0x23, b"", 0)] * 255)], ids))
    with pytest.raises(DecodeError, match=r"more than 255 lines"):
        decode_text(text_image([rec(6, 2, []), rec(10, 4, [(0, 0x23, b"", 0)] * 256)], {**ids, 3: 0}))


# ---- demo stream and training tables ----
def test_demo_repacks_the_two_files(tmp_path):
    ply = bytes((i * 7) & 255 for i in range(3676))
    ctl = b"".join(struct.pack(">HH", (i * 3) & 0xFFFF, 0xFF00 | (i & 255)) for i in range(3676))
    write_demo(ply, ctl, tmp_path)
    b = (tmp_path / "tables" / "demo.bin").read_bytes()
    assert struct.unpack_from("<I", b)[0] == 3676 and len(b) == 4 + 3676 * 5
    assert b[4:4 + 3676] == ply
    assert struct.unpack_from("<HH", b, 4 + 3676 + 4 * 5) == (15, 0xFF05)


def test_demo_wrong_sizes_name_the_file():
    with pytest.raises(DecodeError, match="text/textply: 100 bytes, expected 3676"):
        decode_demo(bytes(100), bytes(4 * 3676))
    with pytest.raises(DecodeError, match="text/textctl: 3676 bytes, expected 14704"):
        decode_demo(bytes(3676), bytes(3676))


def training_patches():
    p = {0x2749A: b"".join(struct.pack(">hhi", 40 + i, 20 + i, 0) for i in range(8)),
         0x274E2: b"".join(struct.pack(">II", 0x103 + i, 0x80000000 | (0x0B + i)) for i in range(8)),
         0x3132C: struct.pack(">11h", *range(-5, 6)), 0x31342: struct.pack(">11h", *range(100, 111))}
    return p


def test_training_tables_round_trip(tmp_path):
    write_training(image(training_patches()), tmp_path)
    s = sections((tmp_path / "tables" / "training.bin").read_bytes())
    assert [(c, r) for c, r, _ in s] == [(8, 8), (8, 8), (11, 2), (11, 2)]
    assert le(s[0][2], "hhi") == [(40 + i, 20 + i, 0) for i in range(8)]
    assert le(s[1][2], "iI") == [(0x103 + i, 0x80000000 | (0x0B + i)) for i in range(8)]
    assert [x[0] for x in le(s[2][2], "h")] == list(range(-5, 6))
    assert [x[0] for x in le(s[3][2], "h")] == list(range(100, 111))


def test_training_refuses_a_script_pointer_naming_the_row():
    p = training_patches()
    p[0x274E2 + 8 * 3 + 4] = struct.pack(">I", 0x00012345)
    with pytest.raises(DecodeError, match=r"table B row 3 at 0x274FA"):
        decode_training(image(p))


def test_real_demo_and_training_structure_only():
    f, t = REAL / "tables" / "demo.bin", REAL / "tables" / "training.bin"
    if not (f.is_file() and t.is_file()):
        pytest.skip("assets/tables/demo.bin not extracted")
    b = f.read_bytes()
    assert struct.unpack_from("<I", b)[0] == 3676 and len(b) == 4 + 3676 * 5
    keys = b[4:4 + 3676]
    assert keys.find(b"\xff\xff\xff") == 2046
    assert not any((REAL / "sounds" / f"{n}.raw").exists() for n in ("textply", "textctl"))
    s = sections(t.read_bytes())
    assert [(c, r) for c, r, _ in s] == [(8, 8), (8, 8), (11, 2), (11, 2)]
    assert all(row[1] & 0x80000000 for row in le(s[1][2], "iI"))
    assert all(row[2] == 0 for row in le(s[0][2], "hhi"))
