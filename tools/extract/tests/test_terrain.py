import pytest

from extract.tables import DecodeError
from extract.terrain import (decode_cell_table, decode_default_offsets, decode_item_list,
                             decode_position_block, decode_position_blocks, decode_terrain,
                             read_terrain, write_terrain)
from tests.synth import BASE, Img

A, B, C = BASE + 0x000, BASE + 0x100, BASE + 0x200      # table starts (128 words = 0x100 bytes)
ROWS = BASE + 0x300


def build():
    """One item per table; rows at ROWS; lists after the rows."""
    img = Img(0x1000)
    lists = {"A": BASE + 0x400, "B": BASE + 0x410, "C": BASE + 0x420}
    # list A: layer 3, id 45 at (100, 200); B: layer 0, id 70 at (4095, 0); C: layer 14 id 74
    img.bytes_(lists["A"], bytes([0x03, 45, 0, 100, 0, 200, 0xFF]))
    img.bytes_(lists["B"], bytes([0x00, 70, 0x0F, 0xFF, 0, 0, 0xFF]))
    img.bytes_(lists["C"], bytes([0x0E, 74, 8, 0, 8, 0, 0xFF]))
    empty = ROWS
    img.w(empty, 0xFFFF)
    rows = {"A": ROWS + 0x10, "B": ROWS + 0x20, "C": ROWS + 0x30}
    for name, table, x, z in (("A", A, 2, 5), ("B", B, 1, 7), ("C", C, 3, 9)):   # row index = z cell
        for i in range(128):
            img.w(table + 2 * i, (empty - table) & 0xFFFF)
        img.w(table + 2 * z, (rows[name] - table))
        a = img.w(rows[name], 2)
        a = img.w(a, x)
        img.l(a, lists[name])
    img.w(BASE + 0x500, *([0] * 16))
    return img, lists


def test_cell_table_one_cell_per_table():
    img, lists = build()
    assert decode_cell_table(img.data, A, 128, BASE, "t") == [(2, 5, lists["A"])]


def test_terrain_decodes_three_tables_lists_and_items():
    img, lists = build()
    tables = (("A", A, 24, 128), ("B", B, 24, 128), ("C", C, 22, 128))
    t = decode_terrain(img.data, BASE, tables, BASE + 0x500)
    assert [(n, len(c)) for n, s, g, c in t.tables] == [("A", 1), ("B", 1), ("C", 1)]
    assert [(i.layer, i.obj, i.dx, i.dz) for i in t.lists[0]] == [(3, 45, 100, 200)]
    assert [(i.layer, i.obj, i.dx, i.dz) for i in t.lists[1]] == [(0, 70, 4095, 0)]
    assert [(i.layer, i.obj, i.dx, i.dz) for i in t.lists[2]] == [(14, 74, 2048, 2048)]
    again = read_terrain(write_terrain(t))
    assert again.lists == t.lists and again.tables[2][3] == t.tables[2][3]
    assert again.tables[2][1:3] == (22, 128)


def test_two_cells_share_one_list_and_rows_without_cells_are_skipped():
    img, lists = build()
    # make table C row z=9 hold two cells (x 3 and 4) that both use list C
    row = ROWS + 0x30
    a = img.w(row, 4)
    a = img.w(a, 3, 4)
    img.l(a, lists["C"], lists["C"])
    cells = decode_cell_table(img.data, C, 128, BASE, "t")
    assert cells == [(3, 9, lists["C"]), (4, 9, lists["C"])]
    tables = (("C", C, 22, 128),)
    t = decode_terrain(img.data, BASE, tables, BASE + 0x500)
    assert len(t.lists) == 1 and [c.list_index for c in t.tables[0][3]] == [0, 0]


def test_item_list_layout_two_items():
    img = Img()
    img.bytes_(BASE, bytes([0x03, 0x21, 0x01, 0x40, 0x0B, 0x80, 0x1A, 0x37, 0x0C, 0x00, 0x02, 0x10, 0xFF]))
    items = decode_item_list(img.data, BASE, BASE)
    assert [(i.layer, i.obj, i.dx, i.dz) for i in items] == [(3, 0x21, 320, 2944), (10, 0x37, 3072, 528)]


def test_item_with_default_offset_bit_is_refused_with_address():
    img = Img()
    img.bytes_(BASE + 8, bytes([0x40, 0x80 | 12, 0xFF]))
    with pytest.raises(DecodeError, match=r"at 0x1008: item with the default-offset bit"):
        decode_item_list(img.data, BASE + 8, BASE)


def test_item_list_without_terminator_is_truncated_error():
    img = Img(32)
    img.bytes_(BASE, bytes([0, 1, 0, 1, 0, 1]) * 5)
    with pytest.raises(DecodeError, match="outside the image"):
        decode_item_list(img.data, BASE, BASE)


def test_item_offset_over_4095_is_refused():
    img = Img()
    img.bytes_(BASE, bytes([0, 5, 0x10, 0, 0, 0, 0xFF]))
    with pytest.raises(DecodeError, match=r"outside 0\.\.4095"):
        decode_item_list(img.data, BASE, BASE)


def test_cell_table_rejects_unsorted_z_and_odd_length_naming_the_table():
    img, lists = build()
    row = ROWS + 0x30
    a = img.w(row, 4)
    a = img.w(a, 9, 9)
    img.l(a, lists["C"], lists["C"])
    with pytest.raises(DecodeError, match=r"terrain table C at 0x1330: row of z 9: x indices are not strictly increasing"):
        decode_cell_table(img.data, C, 128, BASE, "terrain table C")
    img.w(row, 3)
    with pytest.raises(DecodeError, match="odd length"):
        decode_cell_table(img.data, C, 128, BASE, "terrain table C")


def test_cell_table_truncated_row_pointers():
    img = Img(0x306)
    for i in range(128):
        img.w(BASE + 2 * i, 0x300)
    img.w(BASE + 0x300, 2)
    img.w(BASE + 0x302, 1)       # one pointer should follow but the image ends
    with pytest.raises(DecodeError, match="outside the image"):
        decode_cell_table(img.data, BASE, 128, BASE, "t")


def test_default_offsets_are_signed_byte_pairs():
    img = Img()
    img.bytes_(BASE, bytes([0xE0, 0xE0, 0xA0, 0xE0] + [0] * 28))
    d = decode_default_offsets(img.data, BASE, BASE)
    assert d[0] == (-32, -32) and d[1] == (-96, -32) and len(d) == 16


def test_position_block_five_signed_words():
    img = Img()
    T = BASE + 0x100
    # table: entry 0 empty, entry 1 -> block at +0x20
    img.w(T, 0, 0x20)
    img.w(T + 0x20, 70, 65, 6144, 6144, 0)
    b = decode_position_block(img.data, T, 2, BASE)
    assert (b.ref, b.cell_x, b.cell_z, b.fine_x, b.fine_z, b.height) == (2, 70, 65, 6144, 6144, 0)
    blocks = decode_position_blocks(img.data, T, BASE)
    assert [x.ref for x in blocks] == [2]
    img.w(T + 0x20, 65, 66, 4608, 9216, 7)
    assert decode_position_blocks(img.data, T, BASE)[0].height == 7


def test_position_block_reference_outside_image_is_an_error():
    img = Img(64)
    img.w(BASE, 0x7000)
    with pytest.raises(DecodeError, match="position block ref 0"):
        decode_position_block(img.data, BASE, 0, BASE)


def test_reader_checks_size_before_unpacking():
    img, _ = build()
    tables = (("A", A, 24, 128), ("B", B, 24, 128), ("C", C, 22, 128))
    b = write_terrain(decode_terrain(img.data, BASE, tables, BASE + 0x500))
    with pytest.raises(DecodeError, match="does not match the counts"):
        read_terrain(b[:-1])
    with pytest.raises(DecodeError, match="truncated table records"):
        read_terrain(b[:20])
