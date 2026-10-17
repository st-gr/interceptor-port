// Terrain decoder: cell index tables, item lists, position blocks (tools/extract/extract/terrain.py;
// docs/re/02-assets-and-tables.md sections 9 and 10). Pure over bytes; `base` is the flat address of byte 0 of `data`.
//
// The Python module also has a reader (read_terrain) used by its tests; the extractor does not need it.
import { IMAGE_BASE, Mem } from "./tables.js";
import { ByteWriter } from "./util.js";

// [name, table address, cell shift, grid size]
export const TABLES = [["A", 0x47374, 24, 32], ["B", 0x47474, 24, 32], ["C", 0x47574, 22, 128]];
export const DEFAULT_OFFSETS = 0x207c6;
export const POSITION_TABLE = 0x2dce8;
export const MAX_ITEMS_PER_LIST = 4096;
export const TERRAIN_MAGIC = "ITRN";
export const TERRAIN_VERSION = 2;

/** Items until a 0xFF byte. An item: a byte (low nibble layer, high nibble index into the default-offset table), a byte
 *  (bits 0-6 object id, bit 7 = use the default offset) and, with bit 7 clear, two unsigned words (dx, dz) 0..4095. */
export function decodeItemList(data, addr, base = IMAGE_BASE, name = "terrain item list") {
  const mem = new Mem(data, base, name);
  const items = [];
  let p = addr;
  for (;;) {
    const b0 = mem.u8(p);
    if (b0 === 0xff) return items;
    if (items.length >= MAX_ITEMS_PER_LIST) mem.fail("item list does not end", addr);
    const b1 = mem.u8(p + 1);
    if (b1 & 0x80) {
      mem.fail("item with the default-offset bit (bit 7) set; no shipped item has it and the table use is unverified", p);
    }
    const dx = mem.u16(p + 2), dz = mem.u16(p + 4);
    if (dx > 4095 || dz > 4095) mem.fail(`item offset (${dx}, ${dz}) outside 0..4095`, p);
    items.push({ layer: b0 & 15, obj: b1 & 0x7f, dx, dz });
    p += 6;
  }
}

/** A cell index table: `grid` words, one per row, each a byte offset from the table start to the row. Returns
 *  [[x, z, list address], ...] in z then x order. */
export function decodeCellTable(data, tableAddr, grid, base = IMAGE_BASE, name = "cell table") {
  const mem = new Mem(data, base, name);
  const cells = [];
  for (let z = 0; z < grid; z++) {
    const off = mem.u16(tableAddr + 2 * z);
    const row = tableAddr + off;
    const n2 = mem.s16(row);
    if (n2 < 0) continue;
    if (n2 & 1) mem.fail(`row of z ${z} has odd length word ${n2}`, row);
    const n = n2 / 2;
    const xs = [];
    for (let i = 0; i < n; i++) xs.push(mem.u16(row + 2 + 2 * i));
    for (let i = 1; i < n; i++) {
      if (xs[i] <= xs[i - 1]) mem.fail(`row of z ${z}: x indices are not strictly increasing`, row);
    }
    for (let i = 0; i < n; i++) {
      if (xs[i] >= grid) mem.fail(`row of z ${z}: x index ${xs[i]} outside the ${grid}-cell grid`, row);
    }
    const ptr = row + 2 + n2;
    for (let i = 0; i < n; i++) cells.push([xs[i], z, mem.u32(ptr + 4 * i)]);
  }
  return cells;
}

export function decodeDefaultOffsets(data, addr = DEFAULT_OFFSETS, base = IMAGE_BASE) {
  const mem = new Mem(data, base, "default-offset table");
  const out = [];
  for (let i = 0; i < 16; i++) out.push([mem.s8(addr + 2 * i), mem.s8(addr + 2 * i + 1)]);
  return out;
}

/** Returns {tables: [[name, shift, grid, cells]], lists, listAddrs, defaultOffsets}; a cell is {x, z, listIndex}. */
export function decodeTerrain(data, base = IMAGE_BASE, tables = TABLES, defaultOffsetsAddr = DEFAULT_OFFSETS) {
  const t = { tables: [], lists: [], listAddrs: [], defaultOffsets: [] };
  t.defaultOffsets = decodeDefaultOffsets(data, defaultOffsetsAddr, base);
  const index = new Map();
  for (const [name, addr, shift, grid] of tables) {
    const cells = [];
    for (const [x, z, laddr] of decodeCellTable(data, addr, grid, base, `terrain table ${name}`)) {
      if (!index.has(laddr)) {
        index.set(laddr, t.lists.length);
        t.lists.push(decodeItemList(data, laddr, base, `terrain table ${name} item list`));
        t.listAddrs.push(laddr);
      }
      cells.push({ x, z, listIndex: index.get(laddr) });
    }
    t.tables.push([name, shift, grid, cells]);
  }
  return t;
}

export function writeTerrain(t) {
  const nitems = t.lists.reduce((n, l) => n + l.length, 0);
  const w = new ByteWriter(1024);
  w.ascii(TERRAIN_MAGIC).u16(TERRAIN_VERSION).u16(t.tables.length).u32(t.lists.length).u32(nitems);
  let first = 0;
  for (const [, shift, grid, cells] of t.tables) {
    w.u16(shift).u16(grid).u32(first).u32(cells.length);
    first += cells.length;
  }
  for (const [a, b] of t.defaultOffsets) w.i8(a).i8(b);
  for (const [, , , cells] of t.tables) for (const c of cells) w.u16(c.x).u16(c.z).u32(c.listIndex);
  let p = 0;
  for (const l of t.lists) {
    w.u32(p).u32(l.length);
    p += l.length;
  }
  for (const l of t.lists) for (const i of l) w.u8(i.layer).u8(i.obj).u16(i.dx).u16(i.dz);
  return w.finish();
}

/** Position block reached through word table entry `ref` (byte offset into the table at `tableAddr`): the entry is an
 *  offset from the table start to five signed words (x cell, z cell, x fine, z fine, height). */
export function decodePositionBlock(data, tableAddr, ref, base = IMAGE_BASE) {
  const mem = new Mem(data, base, `position block ref ${ref}`);
  const off = mem.u16(tableAddr + ref);
  const a = tableAddr + off;
  const w = [0, 1, 2, 3, 4].map((i) => mem.s16(a + 2 * i));
  return { ref, cellX: w[0], cellZ: w[1], fineX: w[2], fineZ: w[3], height: w[4] };
}

/** All blocks of the word table. The table length is not stored; it ends where the lowest block starts, so the length is
 *  half the smallest non-zero entry. Entries equal to zero carry no block. */
export function decodePositionBlocks(data, tableAddr = POSITION_TABLE, base = IMAGE_BASE) {
  const mem = new Mem(data, base, "position table");
  const probe = [];
  let i = 0;
  const limit = 1024;
  let smallest = null;
  while (i < limit) {
    if (smallest !== null && 2 * i >= smallest) break;
    const v = mem.u16(tableAddr + 2 * i);
    if (v && (smallest === null || v < smallest)) smallest = v;
    probe.push(v);
    i += 1;
  }
  const out = [];
  probe.forEach((v, idx) => {
    if (v) out.push(decodePositionBlock(data, tableAddr, 2 * idx, base));
  });
  return out;
}

/** terrain/index.txt as the CLI writes it. */
export function terrainIndexText(t) {
  const lines = t.tables.map(([n, s, g, c]) => `table ${n} cells ${c.length} grid ${g} cell_shift ${s}\n`);
  lines.push(`lists ${t.lists.length} items ${t.lists.reduce((n, l) => n + l.length, 0)}\n`);
  return lines.join("");
}

/** terrain/positions.txt as the CLI writes it. */
export function positionsText(blocks) {
  return blocks.map((b) => `${Math.floor(b.ref / 2)} ${b.cellX} ${b.cellZ} ${b.fineX} ${b.fineZ} ${b.height}\n`).join("");
}
