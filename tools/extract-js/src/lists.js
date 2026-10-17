// Display-list builder tables (tools/extract/extract/lists.py; docs/re/04a-display-lists.md, Constants; sections 3, 5b and 7).
// Verbatim tables are written as they are in the image (big endian); the limit and near-ground point tables are
// little-endian i16; the footprint and reference data are decoded and written as little-endian i16.
import { IMAGE_BASE, RECORD_COUNT, RECORD_SIZE, RECORD_TABLE, DecodeError, Mem, decodeRecord } from "./tables.js";
import { ByteWriter } from "./util.js";

// name -> [address, size]
export const VERBATIM = {
  "lists_quad.bin": [0x4625c, 16],
  "lists_octperm.bin": [0x4626c, 32],
  "lists_group.bin": [0x4628c, 16],
  "lists_subperm.bin": [0x4629c, 64],
  "lists_tileperm.bin": [0x462dc, 96],
  "lists_tiles.bin": [0x20674, 42],
};
// name -> [address, size, number of offset-table words]
export const BLOBS = {
  "lists_steep.bin": [0x4633a, 610, 4],
  "lists_high.bin": [0x4659c, 3042, 32],
  "lists_low.bin": [0x4717e, 502, 64],
};
const LIMIT_ADDR = 0x21022;
const LIMIT_WORDS = 8;
const VIS_ADDR = 0x23ff4;
const VIS_WORDS = 24;
const MAX_ENTRIES = 14;
const NG_CLASS_ADDR = 0x2c440, NG_CLASS_SIZE = 1024;
const NG_POINTS_ADDR = 0x2c840, NG_POINTS_SIZE = 1452;
const NG_SHAPES_ADDR = 0x2cdec, NG_SHAPES_SIZE = 72;

const CLASSES = new Map([[0x21c40, 1], [0x21c34, 2], [0x21d0c, 3], [0x22cfc, 4], [0x22d0a, 5],
  [0x21c44, 6], [0x26944, 7], [0x2699e, 8], [0x26a3e, 9]]);
const DESC_SIZE = 8;
const NONE = 0xffff;
const MAX_NODES = 4;
const MAX_PLANES = 8;
const SLOT_SIZE = 256;

function rawOf(data, base, addr, n, name) {
  return new Mem(data, base, name).raw(addr, n);
}

/** Parses the lists reachable from the offset table (nwords big-endian words, each a byte offset from the start of the
 *  blob). Returns, per table word, [[tile, [sub-cells]], ...]. */
export function decodePatternLists(blob, nwords, name) {
  if (blob.length < 2 * nwords) throw new DecodeError(`${name}: blob of ${blob.length} bytes is shorter than its offset table`);
  const out = [];
  for (let i = 0; i < nwords; i++) {
    const off = (blob[2 * i] << 8) | blob[2 * i + 1];
    if (off < 2 * nwords || off >= blob.length) throw new DecodeError(`${name}: offset word ${i} = ${off} is outside the blob`);
    let p = off;
    const entries = [];
    for (;;) {
      if (p >= blob.length) throw new DecodeError(`${name}: list of word ${i} runs past the blob`);
      const t = blob[p];
      if (t & 0x80) break;
      if (t > 20) throw new DecodeError(`${name}: list of word ${i}: tile byte ${t} above 20 at offset ${p}`);
      if (entries.length >= MAX_ENTRIES) throw new DecodeError(`${name}: list of word ${i} has more than ${MAX_ENTRIES} entries`);
      if (p + 2 > blob.length) throw new DecodeError(`${name}: list of word ${i} truncated`);
      const n = blob[p + 1];
      if (!(n >= 1 && n <= 16) || p + 2 + n > blob.length) {
        throw new DecodeError(`${name}: list of word ${i}: bad sub-cell count ${n} at offset ${p + 1}`);
      }
      const subs = Array.from(blob.subarray(p + 2, p + 2 + n));
      if (Math.max(...subs) > 15) throw new DecodeError(`${name}: list of word ${i}: sub-cell above 15 at offset ${p + 2}`);
      entries.push([t, subs]);
      p += 2 + n;
    }
    out.push(entries);
  }
  return out;
}

function s16Words(data, base, name, addr, count) {
  const m = new Mem(data, base, name);
  const w = new ByteWriter(2 * count);
  for (let i = 0; i < count; i++) w.i16(m.s16(addr + 2 * i));
  return w.finish();
}

/** Map<file name, bytes> of the verbatim and re-encoded tables. */
export function decodeVerbatimTables(data, base = IMAGE_BASE) {
  const out = new Map();
  for (const [name, [addr, size]] of Object.entries(VERBATIM)) out.set(name, rawOf(data, base, addr, size, name));
  for (const [name, [addr, size, nwords]] of Object.entries(BLOBS)) {
    const blob = rawOf(data, base, addr, size, name);
    decodePatternLists(blob, nwords, name);
    out.set(name, blob);
  }
  out.set("lists_limit.bin", s16Words(data, base, "lists_limit", LIMIT_ADDR, LIMIT_WORDS));
  out.set("lists_vis.bin", s16Words(data, base, "lists_vis", VIS_ADDR, VIS_WORDS));
  out.set("lists_ngclass.bin", rawOf(data, base, NG_CLASS_ADDR, NG_CLASS_SIZE, "lists_ngclass"));
  for (const [name, addr, size] of [["lists_ngpts.bin", NG_POINTS_ADDR, NG_POINTS_SIZE], ["lists_ngshape.bin", NG_SHAPES_ADDR, NG_SHAPES_SIZE]]) {
    out.set(name, s16Words(data, base, name, addr, size / 2));
  }
  const quad = out.get("lists_quad.bin"), grp = out.get("lists_group.bin");
  if (Math.max(...quad) > 3 || Math.max(...grp) > 3) throw new DecodeError("lists_quad/lists_group: class value above 3");
  const tp = out.get("lists_tileperm.bin");
  for (let r = 0; r < 4; r++) {
    if (Math.max(...tp.subarray(24 * r, 24 * r + 21)) > 20) throw new DecodeError("lists_tileperm: tile above 20");
  }
  return out;
}

function firstLeaf(m, M) {
  const w0 = m.u16(M);
  if (w0 & 0x8000) return w0;
  if (w0 & 0x4000) return m.u16(M + 2);
  return m.u16(M + 4);
}

function box(m, a) {
  const b = [];
  for (let i = 0; i < 5; i++) b.push(m.s16(a + 2 * i));
  return b;
}

/** [outerBox, [[box, [[ref3, normal3], ...], n], ...]] of the model at M, or null when not well formed. n is the node's plane count; a node with n = 0
 *  carries one plane (the first dot product is formed before the count is looked at, note 07 8.2 (4); extractor version 26). */
export function decodeFootprint(data, base, M) {
  const m = new Mem(data, base, "footprint");
  try {
    const leaf = firstLeaf(m, M);
    if (leaf === 0xffff) return null;
    const blk = M + (leaf & 0xfff);
    const F = M + (m.u16(blk) & 0xfff);
    const outer = box(m, F);
    const nodes = [];
    let a = F + 10;
    const seen = new Set();
    for (;;) {
      if (seen.has(a) || nodes.length >= MAX_NODES) return null;
      seen.add(a);
      const bx = box(m, a);
      const nxt = m.u32(a + 10);
      const n = m.u16(a + 14);
      if (n > MAX_PLANES) return null;
      const planes = [];
      for (let k = 0; k < Math.max(n, 1); k++) {
        const p = a + 16 + 12 * k;
        planes.push([[0, 1, 2].map((i) => m.s16(p + 2 * i)), [0, 1, 2].map((i) => m.s16(p + 6 + 2 * i))]);
      }
      nodes.push([bx, planes, n]);
      if (nxt & 0x80000000) break;
      a = nxt;
    }
    let total = 4 + 10;
    for (const [, , cnt] of nodes) total += 14 + 6 * cnt;
    if (total > SLOT_SIZE) return null;
    return [outer, nodes];
  } catch (e) {
    if (e instanceof DecodeError) return null;
    throw e;
  }
}

function packFootprint(fp) {
  const [outer, nodes] = fp;
  const w = new ByteWriter(64);
  w.many("i16", outer).u8(nodes.length);
  for (const [bx, planes, n] of nodes) {
    w.many("i16", bx).u8(n);
    for (const [ref, nrm] of planes) w.many("i16", ref).many("i16", nrm);
  }
  return w.finish();
}

function decodeReference(data, base, aux) {
  const m = new Mem(data, base, "reference planes");
  const c = m.u16(aux);
  const ptr = m.u32(aux + 6);
  const planes = [];
  for (let k = 0; k <= c; k++) {
    const p = ptr + 12 * k;
    planes.push([0, 1, 2, 3, 4, 5].map((i) => m.s16(p + 2 * i)));
  }
  return [c, planes];
}

/** [lists_desc.bin, lists_land.bin]. Record r of the object record table at 0x25ECC. */
export function decodeLand(data, base = IMAGE_BASE) {
  const m = new Mem(data, base, "descriptor table");
  const desc = new ByteWriter(RECORD_COUNT * DESC_SIZE);
  const land = new ByteWriter(256);
  for (let r = 0; r < RECORD_COUNT; r++) {
    const rec = decodeRecord(data, RECORD_TABLE - base + RECORD_SIZE * r, r);
    let cls = 0, flags = 0, foot = NONE, ref = NONE;
    if (rec.handler || rec.base || rec.detail_list || rec.alt_list || rec.aux) {
      cls = CLASSES.has(rec.handler) ? CLASSES.get(rec.handler) : 255;
    }
    const M = rec.base;
    if (0 < M && M < 0x80000000) {
      flags |= 1;
      const container = cls === 1 || cls === 2 || cls === 3;   // type B models (4, 5) have another container (note 02 section 7)
      let qual;
      try {
        if (!container) throw new DecodeError("not a detail-list model");
        const leaf = firstLeaf(m, M);
        qual = leaf !== 0xffff && Boolean(m.u8(M + 7 + (leaf & 0xfff)) & 0x10);
      } catch (e) {
        if (!(e instanceof DecodeError)) throw e;
        qual = false;
      }
      const fp = container ? decodeFootprint(data, base, M) : null;
      if (fp !== null) {
        flags |= 4;
        foot = land.len;
        land.bytes(packFootprint(fp));
      }
      if (qual && r < 16) qual = false;   // templates 0..15 are copied into run-time slots; not static landmarks (note 04a section 4)
      if (qual) {
        flags |= 2;
        if (rec.aux) {
          const [c, planes] = decodeReference(data, base, rec.aux);
          ref = land.len;
          land.u16(c);
          for (const p of planes) land.many("i16", p);
        }
      }
    }
    desc.u8(cls).u8(flags).u16(foot).u16(ref).u16(0);
  }
  if (desc.len !== RECORD_COUNT * DESC_SIZE || land.len >= NONE) throw new DecodeError("lists_desc/lists_land: bad size");
  return [desc.finish(), land.finish()];
}

export function decodeAll(data, base = IMAGE_BASE) {
  const out = decodeVerbatimTables(data, base);
  const [d, l] = decodeLand(data, base);
  out.set("lists_desc.bin", d);
  out.set("lists_land.bin", l);
  return out;
}
