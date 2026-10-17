import test from "node:test";
import assert from "node:assert/strict";
import {
  decodeDemo, decodeMenuFont, decodeScenario, decodeText, decodeTraining, paletteWords, writeDemo, writeMenuFont, writePalettes, writeScenario,
  writeText, writeTraining, DEMO_FRAMES,
} from "../src/flow.js";
import { DecodeError } from "../src/tables.js";
import { Adf } from "../src/adf.js";
import { syntheticImage, assertFilesEqual, fixtureBytes } from "./helpers.js";

test("palettes, scenario, text, menu font, training and demo equal the Python output", async () => {
  const image = await syntheticImage();
  const disk = new Adf(fixtureBytes("synth.adf"));
  const files = new Map();
  writePalettes(image, files);
  writeScenario(image, files);
  writeTraining(image, files);
  writeText(image, files);
  writeMenuFont(image, files);
  writeDemo(disk.read("text/textply"), disk.read("text/textctl"), files);
  assert.deepEqual([...files.keys()].sort(), [
    "fonts/menu5x7.bin", "palette/flash.pal", "palette/menu.pal", "tables/demo.bin", "tables/scenario.bin", "tables/text.bin",
    "tables/text_ids.bin", "tables/training.bin",
  ]);
  assertFilesEqual(files, [...files.keys()]);
});

test("the synthetic text has 110 ids and the four ids that name no record carry no lines", async () => {
  const [text, ids] = decodeText(await syntheticImage());
  const dv = new DataView(ids.buffer, ids.byteOffset, ids.byteLength);
  assert.equal(dv.getUint16(0, true), 110);
  for (const id of [18, 19, 21, 25]) assert.equal(dv.getUint16(2 + 6 * (id - 1) + 4, true), 0);
  assert.equal(new DataView(text.buffer, text.byteOffset).getUint16(0, true), 1);
});

test("refusals name the table and the address", () => {
  const image = new Uint8Array(0x40000);
  assert.throws(() => decodeScenario(new Uint8Array(100)), (e) => e instanceof DecodeError && /start blocks at 0x47ADE: needs 16 bytes, image has 100$/.test(e.message));
  assert.throws(() => paletteWords(new Uint8Array(10), 0x29f24), /palette at 0x29F24: needs 64 bytes, image has 10$/);
  image[0x29f24 - 0x10000] = 0x1f;
  assert.throws(() => paletteWords(image, 0x29f24), /palette at 0x29F24: entry 0 is 0x1F00, not a 12-bit colour$/);
  assert.throws(() => decodeMenuFont(new Uint8Array(0x40000)), /menu font: first offset is 0x0000, expected 0x0080 \(character 0x20\)$/);
  assert.throws(() => decodeDemo(new Uint8Array(5), new Uint8Array(5)), /text\/textply: 5 bytes, expected 3676$/);
  assert.throws(() => decodeDemo(new Uint8Array(DEMO_FRAMES), new Uint8Array(5)), /text\/textctl: 5 bytes, expected 14704 \(3676 x 4\)$/);
  assert.throws(() => decodeTraining(new Uint8Array(100)), /training table A row 0 at 0x2749A: outside the image$/);
});

test("the demo stream: key bytes carried over, the words turned little endian", () => {
  const ply = Uint8Array.from({ length: DEMO_FRAMES }, (_, i) => i & 255);
  const ctl = new Uint8Array(4 * DEMO_FRAMES);
  ctl.set([0x12, 0x34, 0xab, 0xcd], 0);
  const out = decodeDemo(ply, ctl);
  assert.equal(out.length, 4 + DEMO_FRAMES + 4 * DEMO_FRAMES);
  assert.deepEqual([...out.subarray(0, 4)], [0x5c, 0x0e, 0, 0]);
  assert.deepEqual([...out.subarray(4 + DEMO_FRAMES, 4 + DEMO_FRAMES + 4)], [0x34, 0x12, 0xcd, 0xab]);
});
