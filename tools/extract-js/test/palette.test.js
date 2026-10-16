import test from "node:test";
import assert from "node:assert/strict";
import { decodeFadeTable, decodeFont, fadeBytes, paletteBytes, FADE_TABLE, FONT_ADDR } from "../src/palette.js";
import { DecodeError, IMAGE_BASE } from "../src/tables.js";
import { syntheticImage, expectedFiles } from "./helpers.js";

const off = (a) => a - IMAGE_BASE;

test("the synthetic image's fade table, flight palette and HUD font equal the Python output", async () => {
  const image = await syntheticImage();
  const expected = expectedFiles();
  const fade = decodeFadeTable(image, off(FADE_TABLE));
  assert.deepEqual(fadeBytes(fade), expected.get("palette/fade.pal"));
  assert.deepEqual(paletteBytes(fade[0]), expected.get("palette/flight.pal"));
  const font = decodeFont(image, off(FONT_ADDR));
  assert.deepEqual(Uint8Array.from(font.flatMap((g) => [...g])), expected.get("fonts/hud.bin"));
});

test("a 12-bit colour is scaled by 17 and a word above 0x0FFF is refused naming the offset", () => {
  const d = new Uint8Array(2 * 4);
  d.set([0x0f, 0xb2, 0x01, 0x23], 0);
  const steps = decodeFadeTable(d, 0, 1, 4);
  assert.deepEqual(steps, [[[255, 187, 34], [17, 34, 51], [0, 0, 0], [0, 0, 0]]]);
  d[4] = 0x12;
  assert.throws(() => decodeFadeTable(d, 0, 1, 4), (e) => e instanceof DecodeError && /step 0 colour 2 at offset 0x4 is 0x1200, not a 12-bit colour/.test(e.message));
  assert.throws(() => decodeFadeTable(d, 0, 2, 4), /needs 16 bytes, image has 8/);
});

test("a font glyph outside the hunk or with bits outside the three columns is refused", () => {
  const d = new Uint8Array(872);
  assert.equal(decodeFont(d, 0).length, 64);
  d[1] = 0xff; d[0] = 0x03;   // glyph 0 at offset 0x3FF: outside the 872-byte hunk
  assert.throws(() => decodeFont(d, 0), /glyph 0 \(code 0x20\) at offset 0x0 points to 0x3FF, outside the hunk/);
  d[0] = 0; d[1] = 0x80; d[0x80] = 0x01;
  assert.throws(() => decodeFont(d, 0), /glyph 0 at hunk offset 0x80 has bits outside the three pixel columns/);
});
