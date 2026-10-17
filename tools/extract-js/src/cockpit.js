// Cockpit data tables (tools/extract/extract/cockpit.py; docs/re/06-cockpit-hud.md, "Data" and "Extractor needs").
// The warning table, the one-plane symbol masks and the target-marker point lists are copied verbatim (big endian).
import { DecodeError, decodeTableBytes } from "./tables.js";
import { concat } from "./util.js";

export const WARNING_ADDR = 0x41ff0;         // hunk 59: 54 entries of 28 bytes
export const WARNING_COUNT = 54;
export const WARNING_SIZE = 28;
export const MASKS_ADDR = 0x42aac;           // hunk 62
export const MASKS_SIZE = 0x42c00 - 0x42aac;
export const MARKER_SMALL_ADDR = 0x3948c;    // hunk 40: 12 signed byte pairs
export const MARKER_SMALL_PAIRS = 12;
export const MARKER_LARGE_ADDR = 0x394a6;    // 44 signed byte pairs
export const MARKER_LARGE_PAIRS = 44;
export const ARCS_ADDR = 0x39408;            // hunk 40: lock circle arc, then the range dot arc
export const LOCK_ARC_SIZE = 42;
export const RANGE_ARC_SIZE = 40;
export const ARCS_SIZE = LOCK_ARC_SIZE + RANGE_ARC_SIZE;
export const MAPSHAPES_ADDR = 0x2fed8;       // hunk 20
export const MAPSHAPES_SIZE = 0x3001c - 0x2fed8;
export const MAPSHAPES_TABLE_WORDS = 17;

export function decodeWarnings(data, offset) {
  return decodeTableBytes(data, offset, WARNING_COUNT * WARNING_SIZE, "warning table");
}

export function decodeMasks(data, offset) {
  return decodeTableBytes(data, offset, MASKS_SIZE, "cockpit masks");
}

/** The two target-marker point lists as one 112-byte blob: 12 then 44 (dx, dy) signed byte pairs. */
export function decodeMarkers(data, smallOffset, largeOffset) {
  const small = decodeTableBytes(data, smallOffset, 2 * MARKER_SMALL_PAIRS, "target marker (small)");
  const large = decodeTableBytes(data, largeOffset, 2 * MARKER_LARGE_PAIRS, "target marker (large)");
  return concat([small, large]);
}

export function decodeArcs(data, offset) {
  return decodeTableBytes(data, offset, ARCS_SIZE, "target arcs");
}

export function decodeMapshapes(data, offset) {
  return decodeTableBytes(data, offset, MAPSHAPES_SIZE, "map shapes");
}

const s8 = (b) => (b ^ 128) - 128;

/** Structure check used by the real-asset test: class bits 1 or 2, printable text. */
export function checkWarnings(blob) {
  if (blob.length !== WARNING_COUNT * WARNING_SIZE) {
    throw new DecodeError(`warning table: ${blob.length} bytes, expected ${WARNING_COUNT * WARNING_SIZE}`);
  }
  for (let i = 0; i < WARNING_COUNT; i++) {
    const e = blob.subarray(i * WARNING_SIZE, (i + 1) * WARNING_SIZE);
    const cls = e[0] >> 6;
    if (cls !== 1 && cls !== 2) throw new DecodeError(`warning table: entry ${i} has class ${cls}`);
    for (let k = 1; k < 27; k++) {
      if (!(e[k] >= 0x20 && e[k] < 0x7f)) throw new DecodeError(`warning table: entry ${i} has a non-printable character`);
    }
  }
}

/** The small circle has radius 3 (12 points) and the large radius 10 (44 points, x from -11 to 10). */
export function checkMarkers(blob) {
  const extent = (from, to) => {
    let m = 0;
    for (let i = from; i < to; i++) m = Math.max(m, Math.abs(s8(blob[2 * i])), Math.abs(s8(blob[2 * i + 1])));
    return m;
  };
  if (extent(0, MARKER_SMALL_PAIRS) !== 3) throw new DecodeError("target marker (small): extent is not 3");
  const large = extent(MARKER_SMALL_PAIRS, blob.length >> 1);
  if (large !== 10 && large !== 11) throw new DecodeError("target marker (large): extent is not 10");
}

export function checkArcs(blob) {
  if (blob.length !== ARCS_SIZE) throw new DecodeError(`target arcs: ${blob.length} bytes, expected ${ARCS_SIZE}`);
  const pair = (o, i) => [s8(blob[o + 2 * i]), s8(blob[o + 2 * i + 1])];
  const is = (p, x, y) => p[0] === x && p[1] === y;
  if (!is(pair(0, 0), 0, 12) || !is(pair(0, 19), 14, 0) || !is(pair(0, 20), -1, -1)) {
    throw new DecodeError("target arcs: lock circle arc does not run (0,12) to (14,0) and end with (-1,-1)");
  }
  if (!is(pair(LOCK_ARC_SIZE, 0), 0, 11) || !is(pair(LOCK_ARC_SIZE, 18), 13, 0) || !is(pair(LOCK_ARC_SIZE, 19), -1, -1)) {
    throw new DecodeError("target arcs: range dot arc does not run (0,11) to (13,0) in 19 pairs and end with (-1,-1)");
  }
}

/** [offsets, shapes]: the 17 offset words and the shapes as lists of [dy, dx, length] triples. */
export function splitMapshapes(blob) {
  const n = 2 * MAPSHAPES_TABLE_WORDS;
  const offsets = [];
  for (let i = 0; i < MAPSHAPES_TABLE_WORDS; i++) offsets.push((blob[2 * i] << 8) | blob[2 * i + 1]);
  const shapes = [];
  let cur = [];
  let i = n;
  while (i < blob.length) {
    if (blob[i] === 0x80) {
      shapes.push(cur);
      cur = [];
      i += 1;
      continue;
    }
    if (i + 3 > blob.length) throw new DecodeError("map shapes: truncated triple");
    cur.push([s8(blob[i]), s8(blob[i + 1]), blob[i + 2]]);
    i += 3;
  }
  if (cur.length) throw new DecodeError("map shapes: last shape has no terminator");
  return [offsets, shapes];
}

// FNV-1a (32 bits) of the bytes of the 17 shapes (everything after the offset words): the one constant that identifies the shipped set.
export const MAPSHAPES_FNV1A = 0xf434eed5;

export function fnv1a32(data) {
  let h = 0x811c9dc5;
  for (const b of data) h = Math.imul(h ^ b, 0x01000193) >>> 0;
  return h;
}

export function mapshapesHash(blob) {
  return fnv1a32(blob.subarray(2 * MAPSHAPES_TABLE_WORDS));
}

const hex8 = (v) => v.toString(16).toUpperCase().padStart(8, "0");

export function checkMapshapes(blob, size = MAPSHAPES_SIZE, wantHash = MAPSHAPES_FNV1A) {
  if (blob.length !== size) throw new DecodeError(`map shapes: ${blob.length} bytes, expected ${size}`);
  const [offsets, shapes] = splitMapshapes(blob);
  if (shapes.length !== 17) throw new DecodeError(`map shapes: ${shapes.length} shapes, expected 17 (16 directions and the ship)`);
  let start = 2 * MAPSHAPES_TABLE_WORDS;
  for (let k = 0; k < 16; k++) {
    if (offsets[k] !== start) throw new DecodeError(`map shapes: offset word ${k} is ${offsets[k]}, the shape starts at ${start}`);
    start += 3 * shapes[k].length + 1;
  }
  if (offsets[16] !== offsets[0]) throw new DecodeError("map shapes: the 17th offset word does not repeat sector 0");
  shapes.forEach((shape, k) => {
    if (!shape.length || shape.some((r) => r[2] === 0)) throw new DecodeError(`map shapes: shape ${k} is empty or has a run of length 0`);
  });
  const lens0 = shapes[0].map((r) => r[2]);
  if (lens0.length !== 5 || lens0[4] !== Math.max(...lens0)) throw new DecodeError("map shapes: sector 0 is not five rows with the widest at the bottom");
  if (shapes[16].length !== 3) throw new DecodeError("map shapes: the ship shape is not three rows");
  const h = mapshapesHash(blob);
  if (h !== wantHash) throw new DecodeError(`map shapes: the shapes hash to 0x${hex8(h)}, expected 0x${hex8(wantHash)}`);
}
