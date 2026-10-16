import test from "node:test";
import assert from "node:assert/strict";
import {
  decodeCellTable, decodeItemList, decodePositionBlocks, decodeTerrain, positionsText, terrainIndexText, writeTerrain, POSITION_TABLE,
} from "../src/terrain.js";
import { DecodeError, IMAGE_BASE } from "../src/tables.js";
import { syntheticImage, expectedFiles } from "./helpers.js";

const text = (b) => new TextDecoder().decode(b);

test("the terrain files of the synthetic image equal the Python output", async () => {
  const image = await syntheticImage();
  const expected = expectedFiles();
  const t = decodeTerrain(image);
  assert.deepEqual(writeTerrain(t), expected.get("terrain/terrain.bin"));
  assert.equal(terrainIndexText(t), text(expected.get("terrain/index.txt")));
  assert.equal(positionsText(decodePositionBlocks(image, POSITION_TABLE, IMAGE_BASE)), text(expected.get("terrain/positions.txt")));
});

test("an item list ends at 0xFF; bit 7 and offsets above 4095 are refused", () => {
  const d = Uint8Array.of(0x21, 0x05, 0x01, 0x00, 0x0f, 0xff, 0xff);
  assert.deepEqual(decodeItemList(d, 0, 0), [{ layer: 1, obj: 5, dx: 256, dz: 4095 }]);
  assert.throws(() => decodeItemList(Uint8Array.of(0, 0x85, 0, 0, 0, 0, 0xff), 0, 0, "L"), (e) => e instanceof DecodeError && e.message.startsWith("L at 0x0: item with the default-offset bit (bit 7) set"));
  assert.throws(() => decodeItemList(Uint8Array.of(0, 1, 0x10, 0, 0, 0, 0xff), 0, 0), /item offset \(4096, 0\) outside 0\.\.4095/);
});

test("a cell table row: x keys strictly increasing and inside the grid, then the list pointers", () => {
  // grid 2: row 0 at offset 4: length word 4 (two cells), keys 0 and 1, pointers; row 1 empty (-1)
  const d = new Uint8Array(64);
  const dv = new DataView(d.buffer);
  dv.setUint16(0, 4, false); dv.setUint16(2, 0x20, false);
  dv.setInt16(4, 4, false); dv.setUint16(6, 0, false); dv.setUint16(8, 1, false); dv.setUint32(10, 0x111, false); dv.setUint32(14, 0x222, false);
  dv.setInt16(0x20, -1, false);
  assert.deepEqual(decodeCellTable(d, 0, 2, 0), [[0, 0, 0x111], [1, 0, 0x222]]);
  dv.setUint16(8, 0, false);
  assert.throws(() => decodeCellTable(d, 0, 2, 0, "T"), /T at 0x4: row of z 0: x indices are not strictly increasing/);
  dv.setUint16(8, 5, false);
  assert.throws(() => decodeCellTable(d, 0, 2, 0, "T"), /x index 5 outside the 2-cell grid/);
});

test("position blocks: the table ends where the lowest block starts", () => {
  const d = new Uint8Array(0x40);
  const dv = new DataView(d.buffer);
  dv.setUint16(0, 0, false); dv.setUint16(2, 8, false); dv.setUint16(4, 8, false);   // entry 0 none; entries 1, 2 -> block at 8
  [1, 2, 3, 4, -5].forEach((v, i) => dv.setInt16(8 + 2 * i, v, false));
  const blocks = decodePositionBlocks(d, 0, 0);
  assert.equal(positionsText(blocks), "1 1 2 3 4 -5\n2 1 2 3 4 -5\n");
});
