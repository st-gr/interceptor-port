import test from "node:test";
import assert from "node:assert/strict";
import {
  CAMERA_RING, IMAGE_BASE, LOD_TABLE, RANGE_TABLE, DecodeError, Mem, cameraRingBytes, decodeCameraRing,
  decodeLodTable, decodeRangeTable, decodeRecord,
} from "../src/tables.js";
import { decodeAll as decodeSky } from "../src/sky.js";
import { syntheticImage, expectedFiles, assertFilesEqual } from "./helpers.js";

const off = (a) => a - IMAGE_BASE;

test("lod, range and camera-ring tables of the synthetic image equal the Python output", async () => {
  const image = await syntheticImage();
  const out = new Map();
  out.set("tables/lod.bin", decodeLodTable(image, off(LOD_TABLE)));
  out.set("tables/range.bin", decodeRangeTable(image, off(RANGE_TABLE)));
  const tags = { default: "default", class11: "11", class14: "14", class30: "30" };
  for (const [name, addr] of Object.entries(CAMERA_RING)) {
    out.set(`tables/camera_ring_${tags[name]}.bin`, cameraRingBytes(decodeCameraRing(image, off(addr), name)));
  }
  assertFilesEqual(out, [...out.keys()]);
});

test("the sky and sea tables of the synthetic image equal the Python output", async () => {
  const image = await syntheticImage();
  const out = new Map([...decodeSky(image, IMAGE_BASE)].map(([n, b]) => [`tables/${n}`, b]));
  assert.equal(out.size, 5);
  assertFilesEqual(out, [...out.keys()], expectedFiles());
});

test("a level-of-detail table that decreases or exceeds 15 is refused naming the entry", () => {
  const d = new Uint8Array(240);
  assert.equal(decodeLodTable(d, 0).length, 240);
  d[5] = 3; d[6] = 2;
  assert.throws(() => decodeLodTable(d, 0), (e) => e instanceof DecodeError && /entry 6 at offset 0x6 breaks the non-decreasing 0\.\.15 rule/.test(e.message));
  d.fill(16, 100);
  d[5] = 0; d[6] = 0;
  assert.throws(() => decodeLodTable(d, 0), /entry 100/);
  assert.throws(() => decodeLodTable(new Uint8Array(10), 0), /lod table: table at offset 0x0 needs 240 bytes, image has 10/);
});

test("Mem reads big-endian values and names the context and address of a read outside the image", () => {
  const m = new Mem(Uint8Array.of(0x12, 0x34, 0x56, 0x78, 0xff), 0x1000, "demo");
  assert.equal(m.u16(0x1000), 0x1234);
  assert.equal(m.u32(0x1000), 0x12345678);
  assert.equal(m.s8(0x1004), -1);
  assert.equal(m.s16(0x1003), 0x78ff);
  assert.equal(m.has(0x1004, 1), true);
  assert.equal(m.has(0x1004, 2), false);
  assert.throws(() => m.u32(0x1002), (e) => e instanceof DecodeError && e.message === "demo at 0x1002: read of 4 bytes outside the image");
  assert.throws(() => decodeRecord(new Uint8Array(10), 0, 7), /record table: record 7 at offset 0x0 is truncated/);
});
