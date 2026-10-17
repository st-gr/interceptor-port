import test from "node:test";
import assert from "node:assert/strict";
import { decodeAll, decodeSpan, parseSegmentTable, referenceLongs, decodePads, decodeShipBlob } from "../src/flight.js";
import { DecodeError, IMAGE_BASE, Mem } from "../src/tables.js";
import { syntheticImage, assertFilesEqual } from "./helpers.js";

const longs = (...v) => {
  const b = new Uint8Array(4 * v.length);
  v.forEach((x, i) => new DataView(b.buffer).setUint32(4 * i, x >>> 0, false));
  return b;
};
const cat = (...p) => Uint8Array.from(p.flatMap((x) => [...x]));

test("the flight tables of the synthetic image equal the Python output (flat spans)", async () => {
  const image = await syntheticImage();
  const out = decodeAll(image, IMAGE_BASE, []);
  assert.equal(out.size, 7 + 1 + 2 + 2);
  assertFilesEqual(new Map([...out].map(([n, b]) => [`tables/${n}`, b])), [...out.keys()].map((n) => `tables/${n}`));
});

test("the segment table log is parsed and its flat starts are checked against the executable", () => {
  const segs = parseSegmentTable("junk\nseg 0 flat 010000 ram C07B28 size 41804 segsize 41812 ok\nseg 1 flat 04A000 ram C41000 size 8 segsize 8 ok\n");
  assert.deepEqual([...segs], [[0, [0x10000, 0xc07b28]], [1, [0x4a000, 0xc41000]]]);
});

// An invented two-hunk executable: hunk 0 (code, 0x3A000 bytes) has two relocated longs, one inside the trig span (0x3AE92..) at 0x3AEA0
// and one outside every span; hunk 1 is 8 bytes.
test("reference longs: the file's value plus the RAM base of the target hunk, only inside the spans", () => {
  const n0 = 0x3a000 / 4;
  const code = new Uint8Array(0x3a000);
  const dv = new DataView(code.buffer);
  dv.setUint32(0x3aea0 - 0x10000, 0x100, false);
  dv.setUint32(0x20000 - 0x10000, 0x200, false);
  const exe = cat(
    longs(0x3f3, 0, 2, 0, 1, n0, 2),
    longs(0x3e9, n0), code,
    longs(0x3ec, 2, 1, 0x3aea0 - 0x10000, 0x20000 - 0x10000, 0),
    longs(0x3f2),
    longs(0x3e9, 2, 0, 0), longs(0x3f2),
  );
  const segs = parseSegmentTable("seg 0 flat 010000 ram C00000 size 1 segsize 1 ok\nseg 1 flat 04A000 ram C40000 size 8 segsize 8 ok\n");
  const got = referenceLongs(exe, IMAGE_BASE, segs);
  assert.deepEqual(got, [[0x3aea0, 0x100 + 0xc40000]]);
  // decodeSpan writes the reference value over the long
  const image = new Uint8Array(0x3a008);
  const span = decodeSpan(new Mem(image, IMAGE_BASE, "t"), "flight_span_trig", got);
  assert.equal(new DataView(span.buffer).getUint32(0x3aea0 - 0x3ae92, false), 0xc40100);
  assert.equal(span.length, 60288);
  // a segment table of another executable is refused
  assert.throws(() => referenceLongs(exe, IMAGE_BASE, parseSegmentTable("seg 0 flat 020000 ram C00000 size 1\nseg 1 flat 04A000 ram C40000 size 1\n")),
    (e) => e instanceof DecodeError && /hunk 0 starts at 0x20000 there, 0x10000 here/.test(e.message));
  assert.throws(() => referenceLongs(exe, IMAGE_BASE, parseSegmentTable("seg 0 flat 010000 ram C00000 size 1\n")), /names 1 hunks, the executable has 2/);
});

test("pads without a terminator entry and ship blobs with a stray pointer are refused", () => {
  assert.throws(() => decodePads(new Mem(new Uint8Array(0x50000), 0, "flight tables")), /the fifth entry is not the terminator/);
  const img = new Uint8Array(0x50000);
  const dv = new DataView(img.buffer);
  dv.setUint32(0x3e0f8, 0x12345678, false);
  assert.throws(() => decodeShipBlob(new Mem(img, 0, "flight tables"), "flight_carrier"), /flight_carrier: pointer 0x12345678 at 0x3E0F8 is outside the structures/);
});
