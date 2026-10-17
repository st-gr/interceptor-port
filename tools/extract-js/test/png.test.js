import test from "node:test";
import assert from "node:assert/strict";
import { inflateSync } from "node:zlib";
import { crc32 } from "node:zlib";
import { writePng, zlibStored, adler32, crc32 as myCrc32 } from "../src/png.js";

function chunksOf(png) {
  const dv = new DataView(png.buffer, png.byteOffset, png.byteLength);
  const out = new Map();
  let pos = 8;
  while (pos < png.length) {
    const len = dv.getUint32(pos);
    const id = String.fromCharCode(...png.subarray(pos + 4, pos + 8));
    const body = png.subarray(pos + 8, pos + 8 + len);
    assert.equal(dv.getUint32(pos + 8 + len), myCrc32(png.subarray(pos + 4, pos + 8 + len)), `CRC of ${id}`);
    out.set(id, body);
    pos += 12 + len;
  }
  return out;
}

test("the checksums agree with zlib's", () => {
  const data = Uint8Array.from({ length: 70000 }, (_, i) => (i * 31 + (i >> 8)) & 255);
  assert.equal(myCrc32(data), crc32(data));
  // Adler-32 of "Wikipedia" is the textbook 0x11E60398
  assert.equal(adler32(new TextEncoder().encode("Wikipedia")), 0x11e60398);
});

test("a small indexed image round-trips through a zlib inflate", () => {
  const pixels = Uint8Array.of(0, 1, 2, 3, 3, 2, 1, 0);
  const png = writePng(4, 2, pixels, [[0, 0, 0], [255, 0, 0], [0, 255, 0], [0, 0, 255]]);
  assert.deepEqual([...png.slice(0, 8)], [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  const c = chunksOf(png);
  assert.deepEqual([...c.get("IHDR")], [0, 0, 0, 4, 0, 0, 0, 2, 8, 3, 0, 0, 0]);
  assert.deepEqual([...c.get("PLTE")], [0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255]);
  assert.deepEqual([...inflateSync(c.get("IDAT"))], [0, 0, 1, 2, 3, 0, 3, 2, 1, 0]);
});

test("more than 65,535 bytes of rows make several stored blocks, as in the Python writer", () => {
  const raw = Uint8Array.from({ length: 90300 }, (_, i) => i & 15);
  const z = zlibStored(raw);
  assert.deepEqual([...z.slice(0, 5)], [0x78, 0x01, 0, 0xff, 0xff]);
  assert.deepEqual([...inflateSync(z)], [...raw]);
  assert.equal(z.length, 2 + (5 + 65535) + (5 + 90300 - 65535) + 4);
  // an empty stream is one final empty block
  assert.deepEqual([...zlibStored(new Uint8Array(0))].slice(0, 7), [0x78, 0x01, 1, 0, 0, 0xff, 0xff]);
});
