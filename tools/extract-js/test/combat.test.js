import test from "node:test";
import assert from "node:assert/strict";
import { writeAi, writeBurst, writeTemplates, decodeBurst, decodeMonths, decodeStar, decodeBriefingChain, decodeHeadStep } from "../src/combat.js";
import { DecodeError } from "../src/tables.js";
import { syntheticImage, assertFilesEqual } from "./helpers.js";

test("templates, ai, burst, target text, months, prompts, star, briefing chain and head step equal the Python output", async () => {
  const image = await syntheticImage();
  const files = new Map();
  writeTemplates(image, files);
  writeAi(image, files);
  writeBurst(image, files);
  assert.deepEqual([...files.keys()].sort(), [
    "tables/ai.bin", "tables/briefing_chain.bin", "tables/burst.bin", "tables/head_step.bin", "tables/months.bin",
    "tables/prompts.bin", "tables/star.bin", "tables/target_text.bin", "tables/templates.bin",
  ]);
  assertFilesEqual(files, [...files.keys()]);
});

test("a table outside the image or a value the note does not allow is refused naming the table", () => {
  const small = new Uint8Array(0x1000);
  assert.throws(() => decodeBurst(small), (e) => e instanceof DecodeError && /burst table pointer table at 0x315A2: needs 32 bytes, image has 4096$/.test(e.message));
  assert.throws(() => decodeStar(small), /flare star at 0x31542: needs 36 bytes, image has 4096$/);
  assert.throws(() => decodeBriefingChain(small), /briefing chain at 0x43EE6: needs the bytes up to 0x43F24, image has 4096$/);
  const image = new Uint8Array(0x40000);
  image[0x1e800 - 0x10000] = 100;   // a head step beyond +-64
  assert.throws(() => decodeHeadStep(image), /head step table at 0x1E800 holds a step beyond \+-64: not the table of note 06$/);
  image.fill(0x41, 0x292cc - 0x10000, 0x292cc - 0x10000 + 36);
  image[0x292cc - 0x10000] = 0x07;
  assert.throws(() => decodeMonths(image), /month name 0 at 0x292CC holds a non-printable byte$/);
});

test("month names are stored reversed and read back in reading order", () => {
  const image = new Uint8Array(0x40000);
  const at = 0x292cc - 0x10000;
  for (let i = 0; i < 4; i++) image.set(new TextEncoder().encode("123456789".replace(/./g, (c) => String.fromCharCode(c.charCodeAt(0) + i))).reverse(), at + 9 * i);
  const out = decodeMonths(image);
  assert.equal(new TextDecoder().decode(out.subarray(0, 9)), "123456789");
  assert.equal(new TextDecoder().decode(out.subarray(27, 36)), "456789:;<");
});
