// Tables the flight step reads (tools/extract/extract/flight.py; docs/re/05-flight-model.md, "Tables in the
// image" and "Extractor needs"). Pure functions over the relocated flat image.
//
// File formats are those documented in flight.py and tools/extract/README.md: little-endian i16 words unless
// the table is "raw big endian" (the spans and the ship blobs).
import { parse as parseHunks } from "./hunk.js";
import { DecodeError, Mem } from "./tables.js";
import { ByteWriter, concat, hex } from "./util.js";

export const WORD_TABLES = {
  // name: [address, words]
  flight_stick_player: [0x425d8, 62],
  flight_stick_ai: [0x42654, 64],
  flight_gust: [0x2ab18, 31],
  flight_sink: [0x2aad8, 31],
  flight_len: [0x208e8, 257],
  flight_terrain: [0x47f40, 32 * 32 + 3724 / 2],
  flight_dir: [0x206f2, 32],
};
export const PAD_TABLE = 0x47b72;
export const PAD_ENTRIES = 5;
export const DESCRIPTOR_BASE = 0x2600c;
export const SHIP_BLOBS = {
  // name: [[address, bytes], ...]
  flight_carrier: [[0x3e0f8, 124], [0x3e174, 44], [0x3e6ae, 0x3e95a - 0x3e6ae]],
  flight_ship2: [[0x3edcc, 32], [0x3edec, 44], [0x3f2b6, 0x3f420 - 0x3f2b6]],
};
// Image spans the port reads by address (note 05 17.9, 17.18 a). Sizes are checked.
export const SPANS = { flight_span_pitch: [0x3249a, 0x325b8], flight_span_trig: [0x3ae92, 0x49a12] };
export const SPAN_SIZES = { flight_span_pitch: 286, flight_span_trig: 60288 };
export const SHIP_SIZES = { flight_carrier: 852, flight_ship2: 438 };
export const PAD_QUAD_LIMIT = 64;   // words; the longest list in the image is 33

function words(mem, addr, n, name) {
  if (!mem.has(addr, 2 * n)) throw new DecodeError(`${name}: ${n} words at 0x${hex(addr)} lie outside the image`);
  const out = [];
  for (let i = 0; i < n; i++) out.push(mem.s16(addr + 2 * i));
  return out;
}

export function wordsBytes(list) {
  return new ByteWriter(2 * list.length + 2).many("i16", list).finish();
}

export function decodeWordTable(mem, name) {
  const [addr, n] = WORD_TABLES[name];
  const out = wordsBytes(words(mem, addr, n, name));
  if (out.length !== 2 * n) throw new DecodeError(`${name}: expected ${2 * n} bytes, got ${out.length}`);
  return out;
}

export function decodePads(mem) {
  const entries = [];
  for (let i = 0; i < PAD_ENTRIES; i++) entries.push(words(mem, PAD_TABLE + 16 * i, 8, "airfield pads"));
  if (entries[PAD_ENTRIES - 1][1] !== -1) {
    throw new DecodeError("airfield pads: the fifth entry is not the terminator (cell x -1)");
  }
  const all = entries.flat();
  for (let i = 0; i < PAD_ENTRIES - 1; i++) {
    const e = entries[i];
    if (e[1] < 0) {
      all.push(0);
      continue;
    }
    const ptr = mem.u32(DESCRIPTOR_BASE + e[0] + 0x10);
    const quads = [];
    let ended = false;
    for (let k = 0; k < PAD_QUAD_LIMIT; k++) {
      const w = mem.s16(ptr + 2 * k);
      quads.push(w);
      if (w === -1) { ended = true; break; }
    }
    if (!ended) throw new DecodeError(`airfield pads: quad list of entry ${i} at 0x${hex(ptr)} has no terminator`);
    if ((quads.length - 1) % 8) {
      throw new DecodeError(`airfield pads: quad list of entry ${i} is not a whole number of quads`);
    }
    all.push(quads.length, ...quads);
  }
  return wordsBytes(all);
}

/** The span's image bytes with the reference-machine value written over every relocated long that lies wholly inside it.
 *  referenceLongs: [[flat address, value], ...] */
export function decodeSpan(mem, name, referenceLongs = []) {
  const [lo, hi] = SPANS[name];
  if (!mem.has(lo, hi - lo)) throw new DecodeError(`${name}: 0x${hex(lo)}..0x${hex(hi)} lies outside the image`);
  const out = mem.raw(lo, hi - lo);
  if (out.length !== SPAN_SIZES[name]) throw new DecodeError(`${name}: expected ${SPAN_SIZES[name]} bytes, got ${out.length}`);
  const dv = new DataView(out.buffer);
  for (const [addr, value] of referenceLongs) {
    if (lo <= addr && addr + 4 <= hi) dv.setUint32(addr - lo, value >>> 0, false);
  }
  return out;
}

const SEG_LINE = /^seg (\d+) flat ([0-9A-Fa-f]+) ram ([0-9A-Fa-f]+) /;

/** Map<hunk index, [flat start, RAM base]> from the loader log (lines "seg N flat F ram R size ..."). */
export function parseSegmentTable(text) {
  const segs = new Map();
  for (const line of text.split(/\r?\n|\r/)) {
    const m = SEG_LINE.exec(line);
    if (m) segs.set(parseInt(m[1], 10), [parseInt(m[2], 16), parseInt(m[3], 16)]);
  }
  return segs;
}

/** [[flat address, reference-machine value], ...] for every relocated long of the executable that lies in a junk span:
 *  the file's value plus the RAM base of the target hunk (note 05 17.9). */
export function referenceLongs(exe, base, segs) {
  const { sizes, contents, relocs } = parseHunks(exe);
  const flat = [];
  let a = base;
  for (const sz of sizes) {
    flat.push(a);
    a += sz;
  }
  for (const [i, [f]] of segs) {
    if (i < flat.length && f !== flat[i]) {
      throw new DecodeError(`segment table: hunk ${i} starts at 0x${hex(f)} there, 0x${hex(flat[i])} here (another executable?)`);
    }
  }
  if (segs.size < sizes.length) throw new DecodeError(`segment table names ${segs.size} hunks, the executable has ${sizes.length}`);
  const out = [];
  relocs.forEach((rl, i) => {
    for (const [off, tgt] of rl) {
      const addr = flat[i] + off;
      if (!Object.values(SPANS).some(([lo, hi]) => lo <= addr && addr + 4 <= hi)) continue;
      const c = contents[i];
      const v = off + 4 <= c.length ? new DataView(c.buffer, c.byteOffset, c.byteLength).getUint32(off, false) : 0;
      const seg = segs.get(tgt);
      if (!seg) throw new DecodeError(`segment table: no entry for hunk ${tgt}`);
      out.push([addr, (v + seg[1]) >>> 0]);
    }
  });
  return out;
}

export function decodeShipBlob(mem, name) {
  const out = concat(SHIP_BLOBS[name].map(([a, n]) => mem.raw(a, n)));
  if (out.length !== SHIP_SIZES[name]) throw new DecodeError(`${name}: expected ${SHIP_SIZES[name]} bytes, got ${out.length}`);
  // every pointer of the lists names a structure inside the structure block
  const st = SHIP_BLOBS[name][2];
  for (const [base, n] of [SHIP_BLOBS[name][0], SHIP_BLOBS[name][1]]) {
    let pos = 0;
    while (pos < n) {
      const w = mem.u16(base + pos);
      if (w === 0xffff) {
        pos += 2;
        continue;
      }
      const p = mem.u32(base + pos);
      if (!(st[0] <= p && p < st[0] + st[1])) {
        throw new DecodeError(`${name}: pointer 0x${hex(p)} at 0x${hex(base + pos)} is outside the structures`);
      }
      pos += 4;
    }
  }
  return out;
}

/** Map<file name, bytes> for every flight table. longs: referenceLongs(...) of the executable, or empty (the spans then keep flat values). */
export function decodeAll(image, base, longs = []) {
  const mem = new Mem(image, base, "flight tables");
  const out = new Map();
  for (const n of Object.keys(WORD_TABLES)) out.set(`${n}.bin`, decodeWordTable(mem, n));
  out.set("flight_pads.bin", decodePads(mem));
  for (const n of Object.keys(SPANS)) out.set(`${n}.bin`, decodeSpan(mem, n, longs));
  for (const n of Object.keys(SHIP_BLOBS)) out.set(`${n}.bin`, decodeShipBlob(mem, n));
  return out;
}
