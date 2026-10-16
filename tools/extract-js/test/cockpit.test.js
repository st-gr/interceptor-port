import test from "node:test";
import assert from "node:assert/strict";
import {
  ARCS_ADDR, MAPSHAPES_ADDR, MARKER_LARGE_ADDR, MARKER_SMALL_ADDR, MASKS_ADDR, WARNING_ADDR,
  decodeArcs, decodeMapshapes, decodeMarkers, decodeMasks, decodeWarnings, checkWarnings, checkArcs, splitMapshapes,
  checkMapshapes, mapshapesHash,
} from "../src/cockpit.js";
import { DecodeError, IMAGE_BASE } from "../src/tables.js";
import { syntheticImage, assertFilesEqual, expectedFiles } from "./helpers.js";

const off = (a) => a - IMAGE_BASE;

test("the cockpit tables of the synthetic image equal the Python output", async () => {
  const image = await syntheticImage();
  const out = new Map([
    ["cockpit/warnings.bin", decodeWarnings(image, off(WARNING_ADDR))],
    ["cockpit/masks.bin", decodeMasks(image, off(MASKS_ADDR))],
    ["cockpit/markers.bin", decodeMarkers(image, off(MARKER_SMALL_ADDR), off(MARKER_LARGE_ADDR))],
    ["cockpit/arcs.bin", decodeArcs(image, off(ARCS_ADDR))],
    ["cockpit/mapshapes.bin", decodeMapshapes(image, off(MAPSHAPES_ADDR))],
  ]);
  assertFilesEqual(out, [...out.keys()]);
});

test("a table that runs past the image is refused naming the table", () => {
  assert.throws(() => decodeWarnings(new Uint8Array(100), 0), (e) => e instanceof DecodeError && /^warning table: table at offset 0x0 needs 1512 bytes, image has 100$/.test(e.message));
  assert.throws(() => decodeMarkers(new Uint8Array(40), 0, 30), /target marker \(large\)/);
});

test("the structure checks of the real-asset tests", () => {
  const w = new Uint8Array(54 * 28);
  assert.throws(() => checkWarnings(w), /entry 0 has class 0/);
  for (let i = 0; i < 54; i++) { w[i * 28] = 0x40; w.fill(0x41, i * 28 + 1, i * 28 + 27); }
  checkWarnings(w);
  w[28 * 3 + 5] = 7;
  assert.throws(() => checkWarnings(w), /entry 3 has a non-printable character/);
  const arcs = new Uint8Array(82);
  assert.throws(() => checkArcs(arcs), /lock circle arc/);
  const [offs, shapes] = splitMapshapes(Uint8Array.from([...new Array(34).fill(0), 1, 2, 3, 0x80]));
  assert.equal(offs.length, 17);
  assert.deepEqual(shapes, [[[1, 2, 3]]]);
});

test("the map shapes of the synthetic disk pass the structure check with the invented hash and not with the real constant", () => {
  const blob = expectedFiles().get("cockpit/mapshapes.bin");
  const SYNTH_FNV1A = 0xc559a977;   // tests/synth.py SYNTH_MAPSHAPES_FNV1A: the invented set
  assert.equal(mapshapesHash(blob), SYNTH_FNV1A);
  checkMapshapes(blob, undefined, SYNTH_FNV1A);
  assert.throws(() => checkMapshapes(blob), /hash to 0x/);
  const changed = Uint8Array.from(blob);
  changed[40] ^= 1;
  assert.throws(() => checkMapshapes(changed, undefined, SYNTH_FNV1A), /hash to 0x/);
  const narrow = Uint8Array.from(blob);
  narrow[34 + 3 * 4 + 2] = 1;
  assert.throws(() => checkMapshapes(narrow, undefined, 0), /widest at the bottom/);
});
