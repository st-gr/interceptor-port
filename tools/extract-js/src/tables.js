// Flat-image access shared by the decoders, and the small fixed tables (tools/extract/extract/tables.py).
//
// Every decoder is a pure function over bytes: `data` is the relocated flat image (or any byte
// array laid out the same way) and addresses are the note's flat addresses, converted with `base`
// (0x10000 for the real image).
import { hex } from "./util.js";

export const IMAGE_BASE = 0x10000;

// Fixed table addresses (docs/re/02-assets-and-tables.md, hunk numbers in the note).
export const RECORD_TABLE = 0x25ecc;
export const RECORD_SIZE = 20;
export const RECORD_COUNT = 134;
export const DEFAULT_OFFSET_TABLE = 0x207c6;
export const LOD_TABLE = 0x20e4a;
export const LOD_SIZE = 240;
export const RANGE_TABLE = 0x4621c;
export const RANGE_SIZE = 64;
// insertion order is the Python dict's: default, class11, class14, class30
export const CAMERA_RING = {
  default: 0x2ce34 + 0x830,
  class11: 0x2ce34 + 0x86c,
  class14: 0x2ce34 + 0x8a8,
  class30: 0x2ce34 + 0x8e4,
};
export const CAMERA_RING_SIZE = 60;

/** A malformed or unsupported table; the message names table and address. */
export class DecodeError extends Error {
  constructor(message) {
    super(message);
    this.name = "DecodeError";
  }
}

/** A documented BLOCKING item (note 02): the data is well formed but the note cannot say what it means. */
export class BlockedError extends DecodeError {
  constructor(message) {
    super(message);
    this.name = "BlockedError";
  }
}

/** Big-endian reads at flat addresses with a context label for errors. */
export class Mem {
  constructor(data, base = IMAGE_BASE, ctx = "image") {
    this.data = data;
    this.base = base;
    this.ctx = ctx;
    this.dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
  }

  fail(msg, addr = null) {
    const where = addr === null ? "" : ` at 0x${hex(addr)}`;
    throw new DecodeError(`${this.ctx}${where}: ${msg}`);
  }

  _chk(addr, n) {
    const o = addr - this.base;
    if (o < 0 || o + n > this.data.length) this.fail(`read of ${n} bytes outside the image`, addr);
    return o;
  }

  u8(addr) { return this.data[this._chk(addr, 1)]; }
  u16(addr) { return this.dv.getUint16(this._chk(addr, 2), false); }
  s16(addr) { return this.dv.getInt16(this._chk(addr, 2), false); }
  u32(addr) { return this.dv.getUint32(this._chk(addr, 4), false); }
  s8(addr) { return this.dv.getInt8(this._chk(addr, 1)); }

  raw(addr, n) {
    const o = this._chk(addr, n);
    return this.data.slice(o, o + n);
  }

  has(addr, n = 1) {
    const o = addr - this.base;
    return o >= 0 && o + n <= this.data.length;
  }
}

function dvOf(data) {
  return new DataView(data.buffer, data.byteOffset, data.byteLength);
}

/** One 20-byte object record (five big-endian longs) at byte `offset`. */
export function decodeRecord(data, offset, recId = 0) {
  if (offset < 0 || offset + RECORD_SIZE > data.length) {
    throw new DecodeError(`record table: record ${recId} at offset 0x${hex(offset)} is truncated`);
  }
  const dv = dvOf(data);
  return {
    id: recId,
    handler: dv.getUint32(offset, false),
    base: dv.getUint32(offset + 4, false),
    detail_list: dv.getUint32(offset + 8, false),
    alt_list: dv.getUint32(offset + 12, false),
    aux: dv.getUint32(offset + 16, false),
  };
}

export function decodeRecordTable(data, offset, count = RECORD_COUNT) {
  const out = [];
  for (let i = 0; i < count; i++) out.push(decodeRecord(data, offset + RECORD_SIZE * i, i));
  return out;
}

/** A raw byte table (level-of-detail shifts, camera rings). */
export function decodeTableBytes(data, offset, size, name) {
  if (offset < 0 || offset + size > data.length) {
    throw new DecodeError(`${name}: table at offset 0x${hex(offset)} needs ${size} bytes, image has ${data.length}`);
  }
  return data.slice(offset, offset + size);
}

export function decodeLodTable(data, offset) {
  const t = decodeTableBytes(data, offset, LOD_SIZE, "lod table");
  for (let i = 1; i < LOD_SIZE; i++) {
    if (t[i] < t[i - 1] || t[i] > 15) {
      throw new DecodeError(`lod table: entry ${i} at offset 0x${hex(offset + i)} breaks the non-decreasing 0..15 rule`);
    }
  }
  return t;
}

/** Ten camera offsets of three signed big-endian words each (note 04). */
export function decodeCameraRing(data, offset, name = "camera ring") {
  const raw = decodeTableBytes(data, offset, CAMERA_RING_SIZE, name);
  const dv = dvOf(raw);
  const ring = [];
  for (let i = 0; i < 10; i++) ring.push([dv.getInt16(6 * i, false), dv.getInt16(6 * i + 2, false), dv.getInt16(6 * i + 4, false)]);
  return ring;
}

/** Little-endian i16 x, y, z for each of the ten offsets (60 bytes). */
export function cameraRingBytes(ring) {
  const out = new Uint8Array(60);
  const dv = dvOf(out);
  let o = 0;
  for (const e of ring) for (const v of e) { dv.setInt16(o, v, true); o += 2; }
  return out;
}

/** The 64-byte visibility-range table, verbatim. */
export function decodeRangeTable(data, offset) {
  return decodeTableBytes(data, offset, RANGE_SIZE, "range table");
}
