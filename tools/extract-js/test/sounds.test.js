import test from "node:test";
import assert from "node:assert/strict";
import { wavBytes, writeSample, sampleRateHz, referencePeriod, PAL_CLOCK_HZ } from "../src/sounds.js";
import { buildSoundTable, NONE, OBJECTS, RECORD_BYTES } from "../src/sound_table.js";
import { Adf } from "../src/adf.js";
import { assertFilesEqual, fixtureBytes } from "./helpers.js";

const hex = (b) => Buffer.from(b).toString("hex");

test("the synthetic samples' .raw and .wav equal the Python output; the index order is the disk's", () => {
  const disk = new Adf(fixtureBytes("synth.adf"));
  const files = new Map();
  for (const name of ["snda", "sndb", "sndc", "textegn"]) writeSample(disk.read(`text/${name}`), name, files);
  assert.equal(files.size, 8);
  assertFilesEqual(files, [...files.keys()]);
});

test("WAV: 8-bit mono PCM, signed samples turned unsigned, odd data padded as Python's wave module does", () => {
  // bytes produced by Python 3.15's wave module for the same input
  assert.equal(hex(wavBytes(Uint8Array.of(0, 1, 2, 3), 8000)), "524946462800000057415645666d74201000000001000100401f0000401f000001000800646174610400000080818283");
  assert.equal(hex(wavBytes(Uint8Array.of(0, 1, 2), 8000)), "524946462800000057415645666d74201000000001000100401f0000401f000001000800646174610300000080818200");
  assert.equal(hex(wavBytes(new Uint8Array(0), 8000)), "524946462400000057415645666d74201000000001000100401f0000401f0000010008006461746100000000");
  assert.equal(hex(wavBytes(Uint8Array.of(0, 127, 255, 128), 8000).subarray(44)), "80ff7f00");
});

test("the WAV rate follows the reference period (3,546,895 / period)", () => {
  for (const name of ["textegn", "unknown-file", "texti1", "text201"]) {
    assert.ok(Math.abs(sampleRateHz(name) * referencePeriod(name) - PAL_CLOCK_HZ) <= (referencePeriod(name) >> 1) + 1);
  }
});

function checkStructure(bytes, names, sizes) {
  assert.equal(bytes.length, OBJECTS * RECORD_BYTES);
  assert.equal(OBJECTS, 37);
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.length);
  const kinds = new Set();
  for (let n = 0; n < OBJECTS; n++) {
    const o = n * RECORD_BYTES;
    const [file, kind, voice, volume] = [0, 1, 2, 3].map((k) => dv.getUint8(o + k));
    const [offset, length, period, next, script] = [dv.getUint32(o + 4), dv.getUint32(o + 8), dv.getUint16(o + 12), dv.getUint8(o + 16), dv.getUint8(o + 17)];
    assert.equal(dv.getUint16(o + 18), 0);
    kinds.add(kind);
    if (kind === NONE) { assert.deepEqual([file, offset, length], [NONE, 0, 0]); continue; }
    assert.ok(kind <= 2 && voice < 4 && volume <= 0x3f && period > 0 && script <= 4);
    assert.ok(next === NONE || (next < OBJECTS && next !== n));
    if (kind === 0) {
      assert.ok(file < names.length);
      assert.ok(offset + length <= sizes.get(names[file]));
    } else {
      assert.ok(file === NONE && offset === 0 && length > 0 && dv.getInt8(o + 14) === -1);
    }
  }
  return kinds;
}

test("the object table has 37 records inside the files; a missing file leaves its objects absent, a short one is clamped", () => {
  const names = ["a", "textcpt", "textegn", "textwnd"];
  const full = new Map([["textcpt", 100], ["textegn", 100], ["textwnd", 100]]);
  const kinds = checkStructure(buildSoundTable(names, full), names, full);
  assert.ok([0, 1, 2, NONE].every((k) => kinds.has(k)));
  const short = new Map([["textcpt", 3], ["textegn", 5]]);
  checkStructure(buildSoundTable(names, short), names, short);
  const none = new Map();
  assert.ok(checkStructure(buildSoundTable(names, none), names, none).has(1), "the synthesised objects need no file");
});
