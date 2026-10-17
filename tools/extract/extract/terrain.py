"""Terrain decoder: cell index tables, item lists, position blocks.

Layouts: docs/re/02-assets-and-tables.md sections 9 and 10. All functions are pure
over bytes; ``base`` is the flat address of byte 0 of ``data``.
"""
import struct
from dataclasses import dataclass, field
from typing import Dict, List, Tuple

from .tables import IMAGE_BASE, DecodeError, Mem

TABLES = (("A", 0x47374, 24, 32), ("B", 0x47474, 24, 32), ("C", 0x47574, 22, 128))
DEFAULT_OFFSETS = 0x207C6
POSITION_TABLE = 0x2DCE8
MAX_ITEMS_PER_LIST = 4096

TERRAIN_MAGIC = b"ITRN"
TERRAIN_VERSION = 2


@dataclass
class Item:
    layer: int
    obj: int
    dx: int
    dz: int


@dataclass
class Cell:
    x: int
    z: int
    list_index: int


@dataclass
class Terrain:
    # per table: (name, cell shift in position bits, grid size, cells)
    tables: List[Tuple[str, int, int, List[Cell]]] = field(default_factory=list)
    lists: List[List[Item]] = field(default_factory=list)
    list_addrs: List[int] = field(default_factory=list)
    default_offsets: List[Tuple[int, int]] = field(default_factory=list)


def decode_item_list(data: bytes, addr: int, base: int = IMAGE_BASE, name: str = "terrain item list"):
    """Items until a 0xFF byte. An item: a byte (low nibble layer, high nibble index
    into the default-offset table), a byte (bits 0-6 object id, bit 7 = use the
    default offset) and, with bit 7 clear, two unsigned words (dx, dz) 0..4095."""
    mem = Mem(data, base, name)
    items = []
    p = addr
    while True:
        b0 = mem.u8(p)
        if b0 == 0xFF:
            return items
        if len(items) >= MAX_ITEMS_PER_LIST:
            mem.fail("item list does not end", addr)
        b1 = mem.u8(p + 1)
        if b1 & 0x80:
            mem.fail("item with the default-offset bit (bit 7) set; no shipped item has it "
                     "and the table use is unverified", p)
        dx, dz = mem.u16(p + 2), mem.u16(p + 4)
        if dx > 4095 or dz > 4095:
            mem.fail(f"item offset ({dx}, {dz}) outside 0..4095", p)
        items.append(Item(b0 & 15, b1 & 0x7F, dx, dz))
        p += 6


def decode_cell_table(data: bytes, table_addr: int, grid: int, base: int = IMAGE_BASE,
                      name: str = "cell table"):
    """A cell index table: ``grid`` words, one per row, each a byte offset from the table
    start to the row. The row index is the Z cell; a row is a word with twice the number of
    cells (negative: empty), then that many bytes of strictly increasing X cell keys (words),
    then one 32-bit item-list pointer per cell (docs/re/04a-display-lists.md, correction 4).
    Returns [(x, z, list address)] in z then x order."""
    mem = Mem(data, base, name)
    cells = []
    for z in range(grid):
        off = mem.u16(table_addr + 2 * z)
        row = table_addr + off
        n2 = mem.s16(row)
        if n2 < 0:
            continue
        if n2 & 1:
            mem.fail(f"row of z {z} has odd length word {n2}", row)
        n = n2 // 2
        xs = [mem.u16(row + 2 + 2 * i) for i in range(n)]
        for i in range(1, n):
            if xs[i] <= xs[i - 1]:
                mem.fail(f"row of z {z}: x indices are not strictly increasing", row)
        for i in range(n):
            if xs[i] >= grid:
                mem.fail(f"row of z {z}: x index {xs[i]} outside the {grid}-cell grid", row)
        ptr = row + 2 + n2
        for i in range(n):
            cells.append((xs[i], z, mem.u32(ptr + 4 * i)))
    return cells


def decode_default_offsets(data: bytes, addr: int = DEFAULT_OFFSETS, base: int = IMAGE_BASE):
    mem = Mem(data, base, "default-offset table")
    return [(mem.s8(addr + 2 * i), mem.s8(addr + 2 * i + 1)) for i in range(16)]


def decode_terrain(data: bytes, base: int = IMAGE_BASE, tables=TABLES,
                   default_offsets_addr: int = DEFAULT_OFFSETS) -> Terrain:
    t = Terrain()
    t.default_offsets = decode_default_offsets(data, default_offsets_addr, base)
    index: Dict[int, int] = {}
    for name, addr, shift, grid in tables:
        cells = []
        for x, z, laddr in decode_cell_table(data, addr, grid, base, f"terrain table {name}"):
            if laddr not in index:
                index[laddr] = len(t.lists)
                t.lists.append(decode_item_list(data, laddr, base, f"terrain table {name} item list"))
                t.list_addrs.append(laddr)
            cells.append(Cell(x, z, index[laddr]))
        t.tables.append((name, shift, grid, cells))
    return t


_HDR = struct.Struct("<4sHHII")      # magic, version, table count, list count, item count
_TAB = struct.Struct("<HHII")        # shift, grid, first cell, cell count
_CELL = struct.Struct("<HHI")        # x cell, z cell, list index (z then x order)
_LIST = struct.Struct("<II")         # first item, item count
_ITEM = struct.Struct("<BBHH")       # layer, object id, dx, dz


def write_terrain(t: Terrain) -> bytes:
    nitems = sum(len(l) for l in t.lists)
    out = [_HDR.pack(TERRAIN_MAGIC, TERRAIN_VERSION, len(t.tables), len(t.lists), nitems)]
    first = 0
    for name, shift, grid, cells in t.tables:
        out.append(_TAB.pack(shift, grid, first, len(cells)))
        first += len(cells)
    out.append(b"".join(struct.pack("<2b", *d) for d in t.default_offsets))
    for name, shift, grid, cells in t.tables:
        for c in cells:
            out.append(_CELL.pack(c.x, c.z, c.list_index))
    p = 0
    for l in t.lists:
        out.append(_LIST.pack(p, len(l)))
        p += len(l)
    for l in t.lists:
        for i in l:
            out.append(_ITEM.pack(i.layer, i.obj, i.dx, i.dz))
    return b"".join(out)


def read_terrain(b: bytes, names=("A", "B", "C")) -> Terrain:
    if len(b) < _HDR.size:
        raise DecodeError("terrain file: truncated header")
    magic, ver, nt, nl, ni = _HDR.unpack_from(b, 0)
    if magic != TERRAIN_MAGIC or ver != TERRAIN_VERSION:
        raise DecodeError(f"terrain file: bad magic or version ({magic!r}, {ver})")
    p = _HDR.size
    if len(b) < p + nt * _TAB.size:
        raise DecodeError("terrain file: truncated table records")
    heads = []
    for _ in range(nt):
        heads.append(_TAB.unpack_from(b, p))
        p += _TAB.size
    total_cells = sum(h[3] for h in heads)
    need = p + 32 + total_cells * _CELL.size + nl * _LIST.size + ni * _ITEM.size
    if len(b) != need:
        raise DecodeError(f"terrain file: size {len(b)} does not match the counts (expected {need})")
    t = Terrain()
    t.default_offsets = [struct.unpack_from("<2b", b, p + 2 * i) for i in range(16)]
    p += 32
    cells = []
    for _ in range(total_cells):
        cells.append(Cell(*_CELL.unpack_from(b, p)))
        p += _CELL.size
    for i, (shift, grid, first, n) in enumerate(heads):
        t.tables.append((names[i] if i < len(names) else str(i), shift, grid, cells[first:first + n]))
    lheads = []
    for _ in range(nl):
        lheads.append(_LIST.unpack_from(b, p))
        p += _LIST.size
    items = []
    for _ in range(ni):
        items.append(Item(*_ITEM.unpack_from(b, p)))
        p += _ITEM.size
    t.lists = [items[f:f + n] for f, n in lheads]
    return t


@dataclass
class PositionBlock:
    ref: int            # byte offset into the word table (2 * entry index)
    cell_x: int
    cell_z: int
    fine_x: int
    fine_z: int
    height: int


def decode_position_block(data: bytes, table_addr: int, ref: int, base: int = IMAGE_BASE) -> PositionBlock:
    """Position block reached through word table entry ``ref`` (byte offset into the
    table at ``table_addr``): the entry is an offset from the table start to five signed
    words (x cell, z cell, x fine, z fine, height; fine and height in units of 256
    position units; cell = table C cell)."""
    mem = Mem(data, base, f"position block ref {ref}")
    off = mem.u16(table_addr + ref)
    a = table_addr + off
    w = [mem.s16(a + 2 * i) for i in range(5)]
    return PositionBlock(ref, *w)


def decode_position_blocks(data: bytes, table_addr: int = POSITION_TABLE, base: int = IMAGE_BASE):
    """All blocks of the word table. The table length is not stored; it ends where the
    lowest block starts (the blocks follow the table), so the length is half the smallest
    non-zero entry. Entries equal to zero carry no block."""
    mem = Mem(data, base, "position table")
    probe = []
    i = 0
    limit = 1024
    # entries are offsets; the table cannot run into the block area
    smallest = None
    while i < limit:
        if smallest is not None and 2 * i >= smallest:
            break
        v = mem.u16(table_addr + 2 * i)
        if v and (smallest is None or v < smallest):
            smallest = v
        probe.append(v)
        i += 1
    out = []
    for idx, v in enumerate(probe):
        if v:
            out.append(decode_position_block(data, table_addr, 2 * idx, base))
    return out
