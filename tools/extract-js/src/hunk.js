// Hunk executable loader (tools/extract/extract/hunk.py): lays the hunks out from a base address and
// applies the 32-bit relocations. The note addresses (docs/re) refer to this image at base 0x10000.
import { DecodeError } from "./tables.js";

const HUNK_NAME = 0x3e8, HUNK_CODE = 0x3e9, HUNK_DATA = 0x3ea, HUNK_BSS = 0x3eb;
const HUNK_RELOC32 = 0x3ec, HUNK_SYMBOL = 0x3f0, HUNK_DEBUG = 0x3f1, HUNK_END = 0x3f2, HUNK_HEADER = 0x3f3;
const HUNK_DREL32 = 0x3f7, HUNK_RELOC32SHORT = 0x3fc, HUNK_RELRELOC32 = 0x3fd;
const TYPE_NAMES = new Map([[HUNK_CODE, "CODE"], [HUNK_DATA, "DATA"], [HUNK_BSS, "BSS"]]);
const MASK = 0x3fffffff;   // top two bits of size/type words are memory flags

/**
 * Parses a hunk executable. Returns {sizes, types, contents, relocs, symbols}: per hunk the allocated
 * size in bytes, the type name, the file data, a list of [offset, target hunk] and a list of [name, offset].
 * Every run of every relocation block is kept (a block may list the same target hunk more than once).
 * Throws DecodeError (Python: ValueError) for a file that is not a hunk executable.
 */
export function parse(data) {
  const n = data.length;
  const dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const u32 = (p) => {
    if (p + 4 > n) throw new DecodeError("truncated hunk file");
    return dv.getUint32(p, false);
  };
  const u16 = (p) => {
    if (p + 2 > n) throw new DecodeError("truncated hunk file");
    return dv.getUint16(p, false);
  };
  const flagged = (w) => {
    if (w >>> 30 === 3) throw new DecodeError("unsupported 0xC0000000 memory-flag extension (extra flags longword)");
    return (w & MASK) >>> 0;
  };

  if (n < 4 || u32(0) !== HUNK_HEADER) throw new DecodeError("not a valid hunk file: no HUNK_HEADER");
  let p = 4;
  while (u32(p) !== 0) p += 4 + 4 * u32(p);   // resident library names
  p += 4;
  const table = u32(p), first = u32(p + 4), last = u32(p + 8);
  p += 12;
  const count = last - first + 1;
  if (first !== 0 || count <= 0 || count > table) throw new DecodeError("unsupported hunk table");
  const sizes = [];
  for (let i = 0; i < count; i++) sizes.push(4 * flagged(u32(p + 4 * i)));
  p += 4 * count;
  const types = [], contents = [], relocs = [], symbols = [];
  let cur = -1;
  while (p < n) {
    const t = flagged(u32(p));
    p += 4;
    if (t === HUNK_CODE || t === HUNK_DATA || t === HUNK_BSS) {
      cur += 1;
      if (cur >= count) throw new DecodeError("more hunks than the header declares");
      const size = 4 * flagged(u32(p));
      p += 4;
      types.push(TYPE_NAMES.get(t));
      relocs.push([]);
      symbols.push([]);
      if (t === HUNK_BSS) {
        contents.push(new Uint8Array(0));
      } else {
        if (p + size > n) throw new DecodeError("truncated hunk file");
        contents.push(data.slice(p, p + size));
        p += size;
      }
    } else if (cur < 0) {
      throw new DecodeError(`unexpected block 0x${t.toString(16)} before first hunk`);
    } else if (t === HUNK_RELOC32) {
      for (;;) {
        const c = u32(p);
        p += 4;
        if (c === 0) break;
        const tgt = u32(p);
        p += 4;
        for (let i = 0; i < c; i++) relocs[cur].push([u32(p + 4 * i), tgt]);
        p += 4 * c;
      }
    } else if (t === HUNK_DREL32 || t === HUNK_RELOC32SHORT) {
      for (;;) {
        const c = u16(p);
        p += 2;
        if (c === 0) break;
        const tgt = u16(p);
        p += 2;
        for (let i = 0; i < c; i++) relocs[cur].push([u16(p + 2 * i), tgt]);
        p += 2 * c;
      }
      p = (p + 3) & ~3;
    } else if (t === HUNK_SYMBOL) {
      for (;;) {
        const c = u32(p) & 0xffffff;
        p += 4;
        if (c === 0) break;
        const end = p + 4 * c;
        // bytes of the name without trailing NULs (Python: rstrip(b"\0"), decoded as latin-1)
        let e = Math.min(end, n);
        while (e > p && data[e - 1] === 0) e--;
        let name = "";
        for (let i = p; i < e; i++) name += String.fromCharCode(data[i]);
        symbols[cur].push([name, u32(p + 4 * c)]);
        p += 4 * c + 4;
      }
    } else if (t === HUNK_NAME || t === HUNK_DEBUG) {
      p += 4 * u32(p) + 4;
    } else if (t === HUNK_END) {
      // nothing
    } else {
      throw new DecodeError(`unsupported hunk block 0x${t.toString(16)}` + (t === HUNK_RELRELOC32 ? " (RELRELOC32)" : ""));
    }
  }
  if (p > n) throw new DecodeError("truncated hunk file");
  if (cur + 1 !== count) throw new DecodeError("fewer hunks than the header declares");
  return { sizes, types, contents, relocs, symbols };
}

/** Lays the hunks out from `base` and applies the relocations. Returns {image, info}. */
export function relocate(data, base) {
  const { sizes, types, contents, relocs, symbols: syms } = parse(data);
  const addrs = [];
  let a = base;
  for (const sz of sizes) {
    addrs.push(a);
    a += sz;
  }
  const image = new Uint8Array(a - base);
  const dv = new DataView(image.buffer);
  let pos = 0;
  contents.forEach((c, i) => {
    if (c.length > sizes[i]) throw new DecodeError("hunk data larger than its allocated size");
    image.set(c, pos);
    pos += sizes[i];
  });
  let entries = 0, applied = 0;
  relocs.forEach((rl, i) => {
    for (const [off, tgt] of rl) {
      entries += 1;
      if (tgt >= addrs.length || off + 4 > sizes[i]) throw new DecodeError(`bad relocation in hunk ${i}: offset ${off} target ${tgt}`);
      const at = addrs[i] - base + off;
      dv.setUint32(at, (dv.getUint32(at, false) + addrs[tgt]) >>> 0, false);
      applied += 1;
    }
  });
  const hunks = sizes.map((size, i) => ({ index: i, type: types[i], addr: addrs[i], size }));
  const symbols = [];
  syms.forEach((sl, i) => { for (const [name, v] of sl) symbols.push({ name, addr: addrs[i] + v, hunk: i }); });
  symbols.sort((x, y) => x.addr - y.addr || (x.name < y.name ? -1 : x.name > y.name ? 1 : 0));
  return { image, info: { base, hunks, symbols, reloc_entries: entries, reloc_count: applied } };
}

