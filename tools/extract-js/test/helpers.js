// Test helpers: the committed synthetic disk and the Python extractor's expected output for it.
import { readFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";
import assert from "node:assert/strict";

export const here = path.dirname(fileURLToPath(import.meta.url));
export const root = path.resolve(here, "..", "..", "..");
export const fixtures = path.join(here, "fixtures");

export function fixtureBytes(name) {
  const b = readFileSync(path.join(fixtures, name));
  return new Uint8Array(b.buffer, b.byteOffset, b.byteLength);
}

/** Reads a ustar archive into Map<name, Uint8Array> (regular files only). */
export function readTar(bytes) {
  const out = new Map();
  let pos = 0;
  const text = (o, n) => {
    let s = "";
    for (let i = 0; i < n && bytes[o + i]; i++) s += String.fromCharCode(bytes[o + i]);
    return s;
  };
  while (pos + 512 <= bytes.length && bytes[pos] !== 0) {
    const name = text(pos, 100);
    const prefix = text(pos + 345, 155);
    const size = parseInt(text(pos + 124, 12).trim() || "0", 8);
    const type = String.fromCharCode(bytes[pos + 156] || 48);
    pos += 512;
    if (type === "0") out.set(prefix ? `${prefix}/${name}` : name, bytes.slice(pos, pos + size));
    pos += Math.ceil(size / 512) * 512;
  }
  return out;
}

export function expectedFiles() {
  return readTar(fixtureBytes("synth-expected.tar"));
}

/** The synthetic disk's image, relocated at 0x10000 (what the decoders read). */
export async function syntheticImage() {
  const { Adf } = await import("../src/adf.js");
  const { relocate } = await import("../src/hunk.js");
  const { IMAGE_BASE } = await import("../src/tables.js");
  const disk = new Adf(fixtureBytes("synth.adf"));
  return relocate(disk.read("F-18 Interceptor"), IMAGE_BASE).image;
}

/** Compares a Map of produced files with the expected ones (by name and bytes) for the names given. */
export function assertFilesEqual(actual, names, expected = expectedFiles()) {
  for (const n of names) {
    const a = actual.get(n);
    const e = expected.get(n);
    assert.ok(a, `missing output file ${n}`);
    assert.ok(e, `no expected file ${n}`);
    assert.equal(a.length, e.length, `${n}: length ${a.length}, expected ${e.length}`);
    assert.ok(Buffer.compare(Buffer.from(a), Buffer.from(e)) === 0, `${n}: bytes differ from the Python extractor's`);
  }
}

export function pythonAvailable() {
  const r = spawnSync("python", ["-c", "import extract, amitools"], { encoding: "utf8" });
  return r.status === 0;
}
