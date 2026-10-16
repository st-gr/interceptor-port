import test from "node:test";
import assert from "node:assert/strict";
import { Adf, ADF_SIZE } from "../src/adf.js";
import { ExtractError } from "../src/errors.js";
import { fixtureBytes } from "./helpers.js";

test("reads the files of the synthetic disk in directory order", () => {
  const disk = new Adf(fixtureBytes("synth.adf"));
  const names = disk.listdir("text");
  assert.deepEqual(names.filter((n) => n !== "textply" && n !== "textctl"), ["texti0a", "textger", "textegn", "textcpt", "snda", "sndb", "sndc", "texttre", "texti1", "texti2", "texti3", "texti4", "texti5", "text201", "textwnd", "texti0b"]);
  assert.equal(names.length, 18);
  assert.deepEqual(disk.read("text/textegn").slice(0, 4), new Uint8Array([0, 127, 255, 128]));
  assert.equal(disk.read("text/textegn").length, 3000);
  assert.deepEqual(disk.read("text/SNDA"), new Uint8Array([1, 2, 3, 4]), "names compare without regard to case");
  const exe = disk.read("F-18 Interceptor");
  assert.deepEqual(exe.slice(0, 4), new Uint8Array([0, 0, 3, 0xf3]), "the executable starts with HUNK_HEADER");
  assert.ok(exe.length > 0x3c000);
});

test("a missing file or directory is named", () => {
  const disk = new Adf(fixtureBytes("synth.adf"));
  assert.throws(() => disk.read("pix/nothing"), /file pix\/nothing not found on the disk/);
  assert.throws(() => disk.listdir("nodir"), /directory nodir not found/);
});

test("a short file, a wrong size and a file that is no AmigaDOS disk are refused", () => {
  assert.throws(() => new Adf(new Uint8Array(100)), (e) => e instanceof ExtractError && /too short/.test(e.message) && /ADF/.test(e.message));
  assert.throws(() => new Adf(new Uint8Array(ADF_SIZE + 1)), (e) => e instanceof ExtractError && /ADF/.test(e.message));
  assert.throws(() => new Adf(new Uint8Array(ADF_SIZE)), (e) => e instanceof ExtractError && /AmigaDOS/.test(e.message));
  const noRoot = fixtureBytes("synth.adf").slice();
  noRoot[880 * 512 + 8] ^= 1;   // breaks the root block's checksum
  assert.throws(() => new Adf(noRoot), /root block/);
});

test("flux images are refused by name", () => {
  for (const [magic, name] of [["SCP\0", "SCP"], ["HXCPICFE", "HFE"], ["HXCHFEV3", "HFE"], ["MAMEFLOPPYIMAGE\0", "MFI"]]) {
    const b = new Uint8Array(2000);
    b.set(new TextEncoder().encode(magic));
    assert.throws(() => new Adf(b), (e) => e instanceof ExtractError && e.message.includes(name) && /not an ADF/.test(e.message));
  }
});
