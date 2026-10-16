// Minimal indexed-colour PNG writer (tools/extract/extract/png.py). No dependencies.
//
// The image data is a zlib stream of stored (uncompressed) deflate blocks of at most 65,535 bytes, the
// last one carrying the final flag: the same bytes as the Python writer, which writes them by hand too.
import { concat } from "./util.js";

const STORED_BLOCK_MAX = 0xffff;

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

export function crc32(bytes, crc = 0) {
  let c = ~crc >>> 0;
  for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
  return ~c >>> 0;
}

export function adler32(bytes) {
  let a = 1, b = 0;
  for (let i = 0; i < bytes.length; i++) {
    a = (a + bytes[i]) % 65521;
    b = (b + a) % 65521;
  }
  return ((b << 16) | a) >>> 0;
}

function be32(v) {
  return Uint8Array.of(v >>> 24, (v >>> 16) & 255, (v >>> 8) & 255, v & 255);
}

function chunk(cid, data) {
  const id = Uint8Array.of(cid.charCodeAt(0), cid.charCodeAt(1), cid.charCodeAt(2), cid.charCodeAt(3));
  return concat([be32(data.length), id, data, be32(crc32(concat([id, data])))]);
}

/** A zlib stream (header 0x78 0x01, stored blocks, Adler-32) of `raw`. */
export function zlibStored(raw) {
  const parts = [Uint8Array.of(0x78, 0x01)];
  let pos = 0;
  for (;;) {
    const n = Math.min(STORED_BLOCK_MAX, raw.length - pos);
    const final = pos + n >= raw.length ? 1 : 0;
    parts.push(Uint8Array.of(final, n & 255, n >> 8, (n ^ 0xffff) & 255, (n ^ 0xffff) >> 8), raw.subarray(pos, pos + n));
    pos += n;
    if (final) break;
  }
  parts.push(be32(adler32(raw)));
  return concat(parts);
}

/** The PNG file for `pixels` (one palette index per pixel) with `palette` ([[r, g, b], ...]). */
export function writePng(width, height, pixels, palette) {
  if (pixels.length !== width * height) throw new Error("pixel buffer size does not match width*height");
  const ihdr = concat([be32(width), be32(height), Uint8Array.of(8, 3, 0, 0, 0)]);
  const plte = Uint8Array.from(palette.flatMap((c) => [...c]));
  const rows = new Uint8Array(height * (width + 1));
  for (let y = 0; y < height; y++) rows.set(pixels.subarray(y * width, (y + 1) * width), y * (width + 1) + 1);
  return concat([
    Uint8Array.of(0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a),
    chunk("IHDR", ihdr),
    chunk("PLTE", plte),
    chunk("IDAT", zlibStored(rows)),
    chunk("IEND", new Uint8Array(0)),
  ]);
}
