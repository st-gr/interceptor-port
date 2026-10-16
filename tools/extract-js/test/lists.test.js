import test from "node:test";
import assert from "node:assert/strict";
import { decodeAll, decodeFootprint, decodePatternLists } from "../src/lists.js";
import { DecodeError, IMAGE_BASE } from "../src/tables.js";
import { syntheticImage, assertFilesEqual } from "./helpers.js";

test("the display-list tables of the synthetic image equal the Python output", async () => {
  const image = await syntheticImage();
  const out = decodeAll(image, IMAGE_BASE);
  assert.equal(out.size, 6 + 3 + 5 + 2);
  assertFilesEqual(new Map([...out].map(([n, b]) => [`tables/${n}`, b])), [...out.keys()].map((n) => `tables/${n}`));
});

test("pattern lists: tile, sub-cell count and sub-cells are validated", () => {
  // offset table of one word (offset 2), then one list: tile 1, 2 sub-cells (3, 4), end
  const blob = Uint8Array.of(0, 2, 1, 2, 3, 4, 0xff);
  assert.deepEqual(decodePatternLists(blob, 1, "t"), [[[1, [3, 4]]]]);
  assert.throws(() => decodePatternLists(Uint8Array.of(0, 2, 21, 1, 0, 0xff), 1, "t"), (e) => e instanceof DecodeError && /tile byte 21 above 20 at offset 2/.test(e.message));
  assert.throws(() => decodePatternLists(Uint8Array.of(0, 2, 1, 17, 0, 0xff), 1, "t"), /bad sub-cell count 17 at offset 3/);
  assert.throws(() => decodePatternLists(Uint8Array.of(0, 9, 1), 1, "t"), /offset word 0 = 9 is outside the blob/);
});

test("a footprint whose node chain loops, or that is not well formed, is none", () => {
  // model at 0x100 (base 0): leaf-only list word 0x8010 -> block at 0x110; its word 0x0020 -> footprint at 0x120
  const d = new Uint8Array(0x400);
  const dv = new DataView(d.buffer);
  dv.setUint16(0x100, 0x8010, false);
  dv.setUint16(0x110, 0x0020, false);
  // outer box 5 words at 0x120, first node at 0x12A: box, next = 0x80000000 (end), 0 planes
  [1, 2, 3, 4, 5].forEach((v, i) => dv.setInt16(0x120 + 2 * i, v, false));
  [6, 7, 8, 9, 10].forEach((v, i) => dv.setInt16(0x12a + 2 * i, v, false));
  dv.setUint32(0x134, 0x80000000, false);
  dv.setUint16(0x138, 0, false);
  // a node with a plane count of 0 carries the one plane that follows the count (extractor version 26)
  assert.deepEqual(decodeFootprint(d, 0, 0x100), [[1, 2, 3, 4, 5], [[[6, 7, 8, 9, 10], [[[0, 0, 0], [0, 0, 0]]], 0]]]);
  dv.setUint32(0x134, 0x12a, false);   // the node points at itself
  assert.equal(decodeFootprint(d, 0, 0x100), null);
  assert.equal(decodeFootprint(d, 0, 0x3ff), null, "reads outside the image make no footprint");
});
