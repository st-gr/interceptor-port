import test from "node:test";
import assert from "node:assert/strict";
import { parse, relocate } from "../src/hunk.js";
import { DecodeError } from "../src/tables.js";
import { syntheticImage } from "./helpers.js";

function longs(...v) {
  const b = new Uint8Array(4 * v.length);
  const dv = new DataView(b.buffer);
  v.forEach((x, i) => dv.setUint32(4 * i, x >>> 0, false));
  return b;
}
const cat = (...p) => Uint8Array.from(p.flatMap((x) => [...x]));

// An invented three-hunk executable: hunk 0 (CODE, 3 longs) holds a pointer into hunk 1 and one into itself,
// hunk 1 (DATA, 2 longs) holds a pointer into hunk 0; hunk 2 is BSS.
function build() {
  return cat(
    longs(0x3f3, 0, 3, 0, 2, 3, 2, 1),
    longs(0x3e9, 3, 0x11111111, 0x00000010, 0x33333333),
    longs(0x3ec, 1, 1, 4),            // one entry, target hunk 1, at offset 4
    longs(1, 0, 8, 0),                // one entry, target hunk 0, at offset 8; end of block
    longs(0x3f2),
    longs(0x3ea, 2, 0x00000002, 0x44444444),
    longs(0x3ec, 1, 0, 0, 0),         // one entry, target hunk 0, at offset 0; end of block
    longs(0x3f2),
    longs(0x3eb, 1),
    longs(0x3f2),
  );
}

test("parse keeps hunk sizes, types, contents and every relocation run", () => {
  const h = parse(build());
  assert.deepEqual(h.sizes, [12, 8, 4]);
  assert.deepEqual(h.types, ["CODE", "DATA", "BSS"]);
  assert.deepEqual(h.relocs, [[[4, 1], [8, 0]], [[0, 0]], []]);
  assert.equal(h.contents[0].length, 12);
  assert.equal(h.contents[2].length, 0);
});

test("relocate lays the hunks out from the base and adds the target hunk's address", () => {
  const { image, info } = relocate(build(), 0x10000);
  assert.equal(image.length, 24);
  const dv = new DataView(image.buffer);
  assert.equal(dv.getUint32(0, false), 0x11111111);
  assert.equal(dv.getUint32(4, false), 0x10 + 0x1000c);    // hunk 1 starts at 0x1000C
  assert.equal(dv.getUint32(8, false), 0x33333333 + 0x10000);
  assert.equal(dv.getUint32(12, false), 2 + 0x10000);      // hunk 1's first long points into hunk 0
  assert.deepEqual(info.hunks.map((x) => x.addr), [0x10000, 0x1000c, 0x10014]);
  assert.equal(info.reloc_count, 3);
});

test("a file that is not a hunk executable is refused", () => {
  assert.throws(() => parse(new Uint8Array(8)), (e) => e instanceof DecodeError && /no HUNK_HEADER/.test(e.message));
  assert.throws(() => parse(build().slice(0, 50)), /truncated hunk file/);
});

test("the synthetic executable relocates to a 0x3C000-byte image carrying the invented patches", async () => {
  const image = await syntheticImage();
  assert.equal(image.length, 0x3c000);
  assert.equal(image[0x47b72 - 0x10000 + 3], 0xfe);   // the pad terminator entry written by tests/synth.py
});
