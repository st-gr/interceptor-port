// IFF ILBM decoder (tools/extract/extract/ilbm.py): BMHD, CMAP, BODY, ByteRun1 compression, optional mask plane.
// Output is one byte per pixel, row-major.
import { DecodeError } from "./tables.js";

function ascii4(b, o) {
  return String.fromCharCode(b[o], b[o + 1], b[o + 2], b[o + 3]);
}

function* chunks(data, start, end) {
  const dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
  let pos = start;
  while (pos + 8 <= end) {
    const cid = ascii4(data, pos);
    const length = dv.getUint32(pos + 4, false);
    yield [cid, data.subarray(pos + 8, Math.min(pos + 8 + length, data.length))];
    pos += 8 + length + (length & 1);
  }
}

export function unpackByterun1(src, outLen) {
  const out = new Uint8Array(outLen);
  let n = 0;
  let i = 0;
  while (n < outLen && i < src.length) {
    const c = src[i];
    i += 1;
    if (c < 128) {
      const run = Math.min(c + 1, src.length - i);
      for (let k = 0; k < run && n < outLen; k++) out[n++] = src[i + k];
      i += c + 1;
    } else if (c > 128) {
      if (i >= src.length) throw new DecodeError("ILBM body is truncated");
      const v = src[i];
      for (let k = 0; k < 257 - c && n < outLen; k++) out[n++] = v;
      i += 1;
    }
    // c == 128 is a no-op by definition
  }
  return out.subarray(0, n);
}

/** Returns {width, height, nplanes, palette: [[r, g, b], ...], pixels: Uint8Array}. */
export function decodeIlbm(data) {
  if (data.length < 12 || ascii4(data, 0) !== "FORM" || ascii4(data, 8) !== "ILBM") {
    throw new DecodeError("not an IFF ILBM picture");
  }
  const dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const formLen = dv.getUint32(4, false);
  let bmhd = null, cmap = null, body = null;
  for (const [cid, chunk] of chunks(data, 12, 8 + formLen)) {
    if (cid === "BMHD") bmhd = chunk;
    else if (cid === "CMAP") cmap = chunk;
    else if (cid === "BODY") body = chunk;
  }
  if (bmhd === null || body === null) throw new DecodeError("ILBM is missing BMHD or BODY");
  if (bmhd.length < 20) throw new DecodeError("ILBM header is truncated");
  const bv = new DataView(bmhd.buffer, bmhd.byteOffset, bmhd.byteLength);
  const w = bv.getUint16(0, false), h = bv.getUint16(2, false);
  const nplanes = bmhd[8], masking = bmhd[9], compression = bmhd[10];
  const rowBytes = Math.floor((w + 15) / 16) * 2;
  const rowsPerPixelRow = nplanes + (masking === 1 ? 1 : 0);
  const needed = rowBytes * rowsPerPixelRow * h;
  const raw = compression === 0 ? body : unpackByterun1(body, needed);
  if (raw.length < needed) throw new DecodeError("ILBM body is truncated");
  const pixels = new Uint8Array(w * h);
  let pos = 0;
  for (let row = 0; row < h; row++) {
    const base = row * w;
    for (let plane = 0; plane < rowsPerPixelRow; plane++) {
      const planeRow = raw.subarray(pos, pos + rowBytes);
      pos += rowBytes;
      if (plane >= nplanes) continue;   // mask plane
      const bit = 1 << plane;
      for (let x = 0; x < w; x++) {
        if (planeRow[x >> 3] & (0x80 >> (x & 7))) pixels[base + x] |= bit;
      }
    }
  }
  const palette = [];
  if (cmap && cmap.length) {
    for (let i = 0; i < cmap.length - 2; i += 3) palette.push([cmap[i], cmap[i + 1], cmap[i + 2]]);
  }
  return { width: w, height: h, nplanes, palette, pixels };
}
